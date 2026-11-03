// SPDX-License-Identifier: MPL-2.0
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "nfs.h"
#include "audio_engine.h"
#include "os.h"
#include "player.h"
#include "settings_service.h"
#include "storage_service.h"
#include "meta.h"
#include "wav.h"

typedef enum { REQ_LIST, REQ_SPACE, REQ_PREFETCH, REQ_PLAY, REQ_STOP, REQ_VERIFY, REQ_SETTINGS_LOAD } req_kind_t;

typedef struct {
    req_kind_t kind;
    uint32_t seq;
    char path[NFS_PATH_MAX];
} storage_req_t;

static os_queue_t *g_req_q;
static os_queue_t *g_res_q;
static file_entry_t g_entries[FILES_MAX_ENTRIES];

void storage_service_init(void) {
    g_req_q = os_queue_create(sizeof(storage_req_t), 3);
    g_res_q = os_queue_create(sizeof(storage_result_t), 4);
}

bool storage_service_ready(void) { return g_req_q != NULL && g_res_q != NULL; }

static bool post(req_kind_t kind, const char *path, uint32_t seq) {
    storage_req_t r = {.kind = kind, .seq = seq};
    if (path != NULL) {
        strncpy(r.path, path, sizeof r.path - 1);
    }
    // Transport requests may wait briefly for a slot (a directory listing can be queued ahead of them).
    uint32_t wait = (kind == REQ_PLAY || kind == REQ_STOP || kind == REQ_VERIFY || kind == REQ_SETTINGS_LOAD) ? 50u : OS_NO_WAIT;
    return g_req_q != NULL && os_queue_send(g_req_q, &r, wait);
}
bool storage_play_request(const char *path) { return path != NULL && post(REQ_PLAY, path, 0); }
bool storage_stop_request(void) { return post(REQ_STOP, NULL, 0); }
bool storage_settings_load_request(void) { return post(REQ_SETTINGS_LOAD, NULL, 0); }
bool storage_verify_request(const char *path) { return path != NULL && post(REQ_VERIFY, path, 0); }

bool storage_list_request(const char *path, uint32_t seq) { return post(REQ_LIST, path, seq); }
bool storage_space_request(uint32_t seq) { return post(REQ_SPACE, NULL, seq); }
bool storage_prefetch_request(const char *path) { return path != NULL && post(REQ_PREFETCH, path, 0); }

bool storage_result_poll(storage_result_t *out) {
    return g_res_q != NULL && os_queue_recv(g_res_q, out, OS_NO_WAIT);
}

const file_entry_t *storage_list_entries(void) { return g_entries; }

// Names: dirs first, then case-insensitive alphabetical.
static int cmp_entries(const void *pa, const void *pb) {
    const file_entry_t *a = pa, *b = pb;
    if (a->is_dir != b->is_dir) {
        return a->is_dir ? -1 : 1;
    }
    for (size_t i = 0; i < FILES_NAME_LEN; i++) {
        int ca = a->name[i], cb = b->name[i];
        if (ca >= 'A' && ca <= 'Z') {
            ca += 'a' - 'A';
        }
        if (cb >= 'A' && cb <= 'Z') {
            cb += 'a' - 'A';
        }
        if (ca != cb) {
            return ca - cb;
        }
        if (ca == 0) {
            break;
        }
    }
    return 0;
}

// Copy a name, keeping at most FILES_NAME_LEN-1 bytes and never cutting a UTF-8 character in half.
static void copy_name(char *dst, const char *src) {
    size_t n = strlen(src);
    if (n > FILES_NAME_LEN - 1) {
        n = FILES_NAME_LEN - 1;
        while (n > 0 && ((unsigned char)src[n] & 0xC0u) == 0x80u) {
            n--; // src[n] is a continuation byte: drop the partial character
        }
    }
    memcpy(dst, src, n);
    dst[n] = '\0';
}

static void list_dir(const storage_req_t *rq, storage_result_t *res) {
    res->kind = STORAGE_RES_LIST;
    res->seq = rq->seq;
    uint32_t count = 0;
    bool truncated = false;
    nfs_err_t err = NFS_OK;
    // Two passes so that "dirs first" survives the entry cap: directories, then files.
    for (int pass = 0; pass < 2 && err == NFS_OK && !truncated; pass++) {
        nfs_dir_t *d;
        err = nfs_opendir(&d, rq->path);
        if (err != NFS_OK) {
            break;
        }
        uint32_t scanned = 0;
        for (;;) {
            nfs_dirent_t e;
            bool done = false;
            err = nfs_readdir(d, &e, &done);
            if (err != NFS_OK || done) {
                break;
            }
            if ((++scanned & 31u) == 0) {
                os_delay_ms(1); // let the UI and input run during a big directory
            }
            if (e.name[0] == '.' || e.is_dir != (pass == 0)) {
                continue; // hidden entries; and the other pass's kind
            }
            if (count >= FILES_MAX_ENTRIES) {
                truncated = true;
                break;
            }
            copy_name(g_entries[count].name, e.name);
            g_entries[count].size = e.size;
            g_entries[count].is_dir = e.is_dir;
            count++;
        }
        (void)nfs_closedir(d);
    }
    if (err == NFS_OK && count > 1) {
        qsort(g_entries, count, sizeof g_entries[0], cmp_entries);
    }
    res->err = (int)err;
    res->count = err == NFS_OK ? count : 0;
    res->truncated = truncated;
}

// ---- Streaming: card -> audio core's data ring ----------------------------------------------------------
//
// One file at a time. WAV: raw PCM bytes. FLAC / MP3: the compressed bytes in file order (FLAC with a synthesised
// 42-byte header in place of the file's metadata). M4A (ALAC): demuxed here, then each packet is sent with a
// 4-byte big-endian length in front. The decoders run on core 1; see docs/audio.md.

#define STREAM_CHUNK 2048u // one SD read; small enough not to hold the task (priority 5) for long
typedef enum { FEED_BYTES, FEED_M4A } feed_kind_t;
static struct {
    nfs_file_t *f;
    bool active;
    feed_kind_t kind;
    // FEED_BYTES: [hdr][file bytes from base, data_len of them]
    uint8_t hdr[FLAC_SYNTH_HEADER_BYTES];
    uint32_t hdr_len, hdr_pos;
    uint64_t base, data_len, remaining;
    // FEED_M4A
    mp4_info_t mp4;
    mp4_iter_t it;
    uint64_t cur_off;
    uint32_t cur_left, cur_size;
    bool len_pending;
    // seek handshake: 0 none, 1 acked and waiting for the audio core to flush
    int seek_state;
    // end of stream: file closed, the end marker still has to reach the audio core (retried until accepted) and the
    // stream stays registered until the audio core reports the end, so a late seek request is still answered
    bool finishing, end_sent;
    uint32_t ended_base;
} g_stream;
static uint8_t g_chunk[STREAM_CHUNK];

static void stream_close(void) {
    if (g_stream.f != NULL) {
        (void)nfs_close(g_stream.f);
    }
    g_stream.f = NULL;
    g_stream.active = false;
    g_stream.seek_state = 0;
    g_stream.finishing = g_stream.end_sent = false;
}

static int32_t file_read_cb(void *ctx, uint64_t off, void *dst, uint32_t n) {
    size_t got = 0;
    if (nfs_seek((nfs_file_t *)ctx, off) != NFS_OK || nfs_read((nfs_file_t *)ctx, dst, n, &got) != NFS_OK) {
        return -1;
    }
    return (int32_t)got;
}

static bool ext_is(const char *path, const char *ext) {
    size_t n = strlen(path), e = strlen(ext);
    if (n < e) {
        return false;
    }
    for (size_t i = 0; i < e; i++) {
        char c = path[n - e + i];
        if (c >= 'A' && c <= 'Z') {
            c = (char)(c + 'a' - 'A');
        }
        if (c != ext[i]) {
            return false;
        }
    }
    return true;
}

static const char *base_name(const char *path) {
    const char *s = strrchr(path, '/');
    return s != NULL ? s + 1 : path;
}

static void rate_text(uint32_t rate, char *out, size_t n) {
    unsigned k10 = rate / 100u;
    if (k10 % 10u == 0) {
        snprintf(out, n, "%u", k10 / 10u);
    } else {
        snprintf(out, n, "%u.%u", k10 / 10u, k10 % 10u);
    }
}

static void fill_format(player_file_t *pf, const char *name, unsigned bits, uint32_t rate, unsigned channels) {
    char r[12];
    rate_text(rate, r, sizeof r);
    snprintf(pf->format, sizeof pf->format, "%s %u/%s%s", name, bits, r, channels == 1 ? " mono" : "");
}

static void stream_start(const char *path, bool verify) {
    stream_close();
    char msg[80];
    nfs_file_t *f;
    nfs_err_t err = nfs_open(&f, path, NFS_O_READ);
    uint64_t size = 0;
    if (err == NFS_OK && (err = nfs_size(f, &size)) != NFS_OK) {
        (void)nfs_close(f);
    }
    if (err != NFS_OK) {
        snprintf(msg, sizeof msg, "Cannot open file: %s", nfs_err_str(err));
        player_file_failed(msg);
        return;
    }
    meta_set_scratch(g_chunk, sizeof g_chunk); // header parsing borrows the chunk buffer: no stream is feeding now
    player_file_t pf;
    memset(&pf, 0, sizeof pf);
    audio_stream_desc_t d;
    memset(&d, 0, sizeof d);
    const char *fail = NULL;
    memset(&g_stream, 0, sizeof g_stream);
    snprintf(pf.tags.title, sizeof pf.tags.title, "%s", base_name(path));
    pf.title_is_name = true;

    if (ext_is(path, ".wav")) {
        wav_info_t w;
        wav_err_t we = wav_parse(file_read_cb, f, size, &w);
        if (we != WAV_OK) {
            fail = wav_err_str(we);
        } else {
            d.codec = AUDIO_CODEC_PCM;
            d.rate = w.rate;
            d.channels = w.channels;
            d.bits = w.bits;
            pf.rate = w.rate;
            pf.total_frames = w.frames;
            fill_format(&pf, "WAV", w.bits, w.rate, w.channels);
            g_stream.kind = FEED_BYTES;
            g_stream.base = w.data_offset;
            g_stream.data_len = w.data_bytes;
        }
    } else if (ext_is(path, ".flac")) {
        flac_info_t fi;
        meta_err_t me = flac_parse(file_read_cb, f, size, &fi);
        if (me != META_OK) {
            fail = meta_err_str(me);
        } else {
            d.codec = AUDIO_CODEC_FLAC;
            d.rate = fi.rate;
            d.channels = fi.channels;
            d.bits = fi.bits;
            d.verify = verify;
            d.total_frames = fi.total_frames;
            memcpy(d.md5, fi.md5, 16);
            pf.rate = fi.rate;
            pf.total_frames = fi.total_frames;
            pf.tags = fi.tags;
            pf.title_is_name = fi.tags.title[0] == '\0';
            if (pf.title_is_name) {
                snprintf(pf.tags.title, sizeof pf.tags.title, "%s", base_name(path));
            }
            fill_format(&pf, "FLAC", fi.bits, fi.rate, fi.channels);
            g_stream.kind = FEED_BYTES;
            g_stream.hdr_len = flac_make_header(&fi, g_stream.hdr);
            g_stream.base = fi.audio_offset;
            g_stream.data_len = size > fi.audio_offset ? size - fi.audio_offset : 0;
        }
    } else if (ext_is(path, ".mp3") && !verify) {
        mp3_info_t mi;
        meta_err_t me = mp3_parse(file_read_cb, f, size, &mi);
        if (me != META_OK) {
            fail = meta_err_str(me);
        } else {
            d.codec = AUDIO_CODEC_MP3;
            d.rate = mi.rate;
            d.channels = mi.channels;
            d.bits = 32;
            pf.rate = mi.rate;
            pf.total_frames = mi.total_frames;
            pf.estimated = mi.estimated;
            pf.tags = mi.tags;
            pf.title_is_name = mi.tags.title[0] == '\0';
            if (pf.title_is_name) {
                snprintf(pf.tags.title, sizeof pf.tags.title, "%s", base_name(path));
            }
            snprintf(pf.format, sizeof pf.format, "MP3 %s%uk", mi.vbr ? "VBR ~" : "", (unsigned)mi.bitrate_kbps);
            g_stream.kind = FEED_BYTES;
            g_stream.base = mi.audio_offset;
            g_stream.data_len = size > mi.audio_offset ? size - mi.audio_offset : 0;
        }
    } else if ((ext_is(path, ".m4a") || ext_is(path, ".mp4")) && !verify) {
        meta_err_t me = mp4_parse(file_read_cb, f, size, &g_stream.mp4);
        if (me != META_OK) {
            fail = meta_err_str(me);
        } else {
            const mp4_info_t *m = &g_stream.mp4;
            d.codec = AUDIO_CODEC_ALAC;
            d.rate = m->rate;
            d.channels = m->channels;
            d.bits = m->bits;
            d.cookie_len = m->cookie_len;
            d.total_frames = m->total_frames;
            memcpy(d.cookie, m->cookie, m->cookie_len);
            pf.rate = m->rate;
            pf.total_frames = m->total_frames;
            pf.tags = m->tags;
            pf.title_is_name = m->tags.title[0] == '\0';
            if (pf.title_is_name) {
                snprintf(pf.tags.title, sizeof pf.tags.title, "%s", base_name(path));
            }
            fill_format(&pf, "ALAC", m->bits, m->rate, m->channels);
            g_stream.kind = FEED_M4A;
            mp4_iter_init(&g_stream.it, &g_stream.mp4);
        }
    } else if (verify) {
        fail = "Verify works on FLAC files";
    } else {
        fail = "Not a playable file";
    }
    if (fail != NULL) {
        (void)nfs_close(f);
        memset(&g_stream, 0, sizeof g_stream);
        player_file_failed(fail);
        return;
    }
    pf.verify = verify;
    pf.codec = d.codec;
    memcpy(pf.md5, d.md5, 16);

    uint32_t want = audio_pcm_started_count() + 1u;
    bool sent = d.codec == AUDIO_CODEC_PCM ? audio_pcm_start(d.rate, (uint8_t)d.channels, (uint8_t)d.bits) : audio_stream_start(&d);
    if (!sent) {
        (void)nfs_close(f);
        memset(&g_stream, 0, sizeof g_stream);
        player_file_failed("Audio engine busy, try again");
        return;
    }
    // The audio core flushes its data ring while setting the stream up (and re-clocks the DAC if the rate
    // changed): write nothing before it says it is ready.
    for (uint32_t t = 0; audio_pcm_started_count() != want; t += 2) {
        if (t > 1500u) {
            (void)nfs_close(f);
            memset(&g_stream, 0, sizeof g_stream);
            player_file_failed("Audio engine did not respond");
            return;
        }
        os_delay_ms(2);
    }
    g_stream.f = f;
    g_stream.remaining = g_stream.data_len;
    if (g_stream.kind == FEED_BYTES && nfs_seek(f, g_stream.base) != NFS_OK) {
        stream_close();
        (void)audio_stop();
        player_file_failed("Cannot read the audio data");
        return;
    }
    g_stream.ended_base = audio_pcm_ended_count();
    g_stream.active = true;
    player_file_started(&pf);
}

static void stream_finish(void) {
    if (g_stream.f != NULL) {
        (void)nfs_close(g_stream.f);
        g_stream.f = NULL;
    }
    g_stream.finishing = true; // the end marker is sent from stream_pump, retried until the audio core's command ring takes it
}

// Reposition the feed for the audio core's seek request (stream byte offset) and tell it we are idle.
static void stream_seek(uint64_t off) {
    if (g_stream.finishing) {
        g_stream.end_sent = false; // the audio core drops its end flag when it flushes: send the marker again afterwards
    } else if (g_stream.kind == FEED_BYTES) {
        if (off < g_stream.hdr_len) {
            g_stream.hdr_pos = (uint32_t)off;
            g_stream.remaining = g_stream.data_len;
            (void)nfs_seek(g_stream.f, g_stream.base);
        } else {
            uint64_t in = off - g_stream.hdr_len;
            if (in > g_stream.data_len) {
                in = g_stream.data_len;
            }
            g_stream.hdr_pos = g_stream.hdr_len;
            g_stream.remaining = g_stream.data_len - in;
            (void)nfs_seek(g_stream.f, g_stream.base + in);
        }
    } else { // packet streams have no byte positions a decoder can seek to; restart from the first packet
        mp4_iter_init(&g_stream.it, &g_stream.mp4);
        g_stream.cur_left = 0;
        g_stream.len_pending = false;
    }
    audio_seek_ack();
    g_stream.seek_state = 1;
}

static void stream_pump(void) {
    if (!g_stream.active) {
        return;
    }
    if (g_stream.finishing && audio_pcm_ended_count() != g_stream.ended_base) { // the audio core has finished: nothing more can be asked
        stream_close();
        return;
    }
    if (g_stream.seek_state == 0) {
        uint64_t off;
        if (audio_seek_poll(&off)) {
            stream_seek(off);
        }
    }
    if (g_stream.seek_state == 1) { // wait for the audio core to flush its ring before writing again
        if (audio_seek_done()) {
            g_stream.seek_state = 0;
        }
        return;
    }
    if (g_stream.finishing) {
        if (!g_stream.end_sent && audio_pcm_end()) {
            g_stream.end_sent = true; // keep the stream registered (1 ms ticks) until the audio core reports the end
        }
        return;
    }
    uint32_t space = audio_data_space();
    if (g_stream.kind == FEED_BYTES) {
        if (g_stream.hdr_pos < g_stream.hdr_len) {
            uint32_t n = g_stream.hdr_len - g_stream.hdr_pos;
            if (space >= n) {
                (void)audio_data_write(g_stream.hdr + g_stream.hdr_pos, n);
                g_stream.hdr_pos += n;
            }
            return;
        }
        if (space < STREAM_CHUNK) {
            return;
        }
        size_t want = g_stream.remaining < STREAM_CHUNK ? (size_t)g_stream.remaining : STREAM_CHUNK;
        size_t got = 0;
        nfs_err_t err = want != 0 ? nfs_read(g_stream.f, g_chunk, want, &got) : NFS_OK;
        if (err != NFS_OK) {
            stream_close();
            (void)audio_stop();
            player_file_failed("Card read error during playback");
            return;
        }
        if (got > 0) {
            (void)audio_data_write(g_chunk, (uint32_t)got); // space was checked and we are the only producer
            g_stream.remaining -= got;
        }
        if (got == 0 || g_stream.remaining == 0) {
            stream_finish();
        }
        return;
    }
    // FEED_M4A
    if (g_stream.cur_left == 0 && !g_stream.len_pending) {
        uint64_t off;
        uint32_t sz;
        if (!mp4_iter_next(&g_stream.it, file_read_cb, g_stream.f, &off, &sz)) {
            stream_finish();
            return;
        }
        g_stream.cur_off = off;
        g_stream.cur_left = g_stream.cur_size = sz;
        g_stream.len_pending = true;
        if (nfs_seek(g_stream.f, off) != NFS_OK) {
            stream_close();
            (void)audio_stop();
            player_file_failed("Card read error during playback");
            return;
        }
        return;
    }
    if (g_stream.len_pending) {
        if (space >= 4) {
            uint8_t l[4] = {(uint8_t)(g_stream.cur_size >> 24), (uint8_t)(g_stream.cur_size >> 16),
                            (uint8_t)(g_stream.cur_size >> 8), (uint8_t)g_stream.cur_size};
            (void)audio_data_write(l, 4);
            g_stream.len_pending = false;
        }
        return;
    }
    if (space < 256u) {
        return;
    }
    size_t want = g_stream.cur_left < STREAM_CHUNK ? g_stream.cur_left : STREAM_CHUNK;
    if (want > space) {
        want = space;
    }
    size_t got = 0;
    if (nfs_read(g_stream.f, g_chunk, want, &got) != NFS_OK || got == 0) {
        stream_close();
        (void)audio_stop();
        player_file_failed("Card read error during playback");
        return;
    }
    (void)audio_data_write(g_chunk, (uint32_t)got);
    g_stream.cur_left -= (uint32_t)got;
}

#define SETTINGS_POLL_MS 500u
// A save is a few KB of SD writes: while a file streams, do it only when the audio data ring is at least half full, so
// the card is never busy when the audio core is about to run dry (the ring is 32 KB: half is 93 ms of 16-bit/44.1 kHz).
#define SETTINGS_SAVE_MAX_RING_SPACE (16u * 1024u)

static void settings_poll(void) {
    static uint32_t last_ms;
    uint32_t now = os_tick_ms();
    if (now - last_ms < SETTINGS_POLL_MS / 2u) {
        return; // while streaming this runs every millisecond: look at most every 250 ms
    }
    last_ms = now;
    settings_service_poll(!g_stream.active || audio_data_space() <= SETTINGS_SAVE_MAX_RING_SPACE);
}

void storage_task(void *arg) {
    (void)arg;
    for (;;) {
        storage_req_t rq;
        // While a file streams, wake every millisecond to keep the audio data ring full.
        // Idle: wake twice a second so a pending settings save is not forgotten (nothing else wakes this task).
        if (!os_queue_recv(g_req_q, &rq, g_stream.active ? 1u : SETTINGS_POLL_MS)) {
            stream_pump();
            settings_poll();
            continue;
        }
        storage_result_t res = {0};
        switch (rq.kind) {
        case REQ_PLAY:
            stream_start(rq.path, false);
            continue;
        case REQ_VERIFY:
            stream_start(rq.path, true);
            continue;
        case REQ_STOP:
            stream_close();
            continue;
        case REQ_SETTINGS_LOAD:
            settings_service_load_now();
            continue;
        case REQ_LIST:
            list_dir(&rq, &res);
            break;
        case REQ_SPACE:
            res.kind = STORAGE_RES_SPACE;
            res.seq = rq.seq;
            res.err = (int)nfs_space(&res.total_bytes, &res.free_bytes);
            break;
        case REQ_PREFETCH:
            printf("storage: prefetch '%s' (stub: nothing to do yet)\n", rq.path);
            continue;
        }
        (void)os_queue_send(g_res_q, &res, 100);
        stream_pump();
    }
}
