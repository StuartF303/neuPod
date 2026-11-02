// SPDX-License-Identifier: MPL-2.0
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "nfs.h"
#include "audio_engine.h"
#include "os.h"
#include "player.h"
#include "storage_service.h"
#include "wav.h"

typedef enum { REQ_LIST, REQ_SPACE, REQ_PREFETCH, REQ_PLAY, REQ_STOP } req_kind_t;

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
    uint32_t wait = (kind == REQ_PLAY || kind == REQ_STOP) ? 50u : OS_NO_WAIT;
    return g_req_q != NULL && os_queue_send(g_req_q, &r, wait);
}
bool storage_play_request(const char *path) { return path != NULL && post(REQ_PLAY, path, 0); }
bool storage_stop_request(void) { return post(REQ_STOP, NULL, 0); }

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

// ---- WAV streaming: card -> audio core's PCM data ring --------------------------------------------------

#define STREAM_CHUNK 2048u // one SD read; small enough not to hold the task (priority 5) for long
static struct {
    nfs_file_t *f;
    uint64_t remaining;
    bool active;
} g_stream;
static uint8_t g_chunk[STREAM_CHUNK];

static void stream_close(void) {
    if (g_stream.f != NULL) {
        (void)nfs_close(g_stream.f);
    }
    g_stream.f = NULL;
    g_stream.active = false;
}

static int32_t wav_read_cb(void *ctx, uint64_t off, void *dst, uint32_t n) {
    size_t got = 0;
    if (nfs_seek((nfs_file_t *)ctx, off) != NFS_OK || nfs_read((nfs_file_t *)ctx, dst, n, &got) != NFS_OK) {
        return -1;
    }
    return (int32_t)got;
}

static void stream_start(const char *path) {
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
    wav_info_t w;
    wav_err_t we = wav_parse(wav_read_cb, f, size, &w);
    if (we != WAV_OK) {
        (void)nfs_close(f);
        player_file_failed(wav_err_str(we));
        return;
    }
    if (nfs_seek(f, w.data_offset) != NFS_OK) {
        (void)nfs_close(f);
        player_file_failed("Cannot read the audio data");
        return;
    }
    uint32_t want = audio_pcm_started_count() + 1u;
    if (!audio_pcm_start(w.rate, w.channels, w.bits)) {
        (void)nfs_close(f);
        player_file_failed("Audio engine busy, try again");
        return;
    }
    // The audio core flushes its data ring while setting the stream up (and re-clocks the DAC if the rate
    // changed): write nothing before it says it is ready.
    for (uint32_t t = 0; audio_pcm_started_count() != want; t += 2) {
        if (t > 1500u) {
            (void)nfs_close(f);
            player_file_failed("Audio engine did not respond");
            return;
        }
        os_delay_ms(2);
    }
    g_stream.f = f;
    g_stream.remaining = w.data_bytes;
    g_stream.active = true;
    player_file_started(&w);
}

static void stream_pump(void) {
    if (!g_stream.active || audio_data_space() < STREAM_CHUNK) {
        return;
    }
    size_t want = g_stream.remaining < STREAM_CHUNK ? (size_t)g_stream.remaining : STREAM_CHUNK;
    size_t got = 0;
    nfs_err_t err = nfs_read(g_stream.f, g_chunk, want, &got);
    if (err != NFS_OK) {
        stream_close();
        (void)audio_stop();
        player_file_failed("Card read error during playback");
        return;
    }
    if (got > 0) {
        (void)audio_data_write(g_chunk, (uint32_t)got); // space was checked above and we are the only producer
        g_stream.remaining -= got;
    }
    if (got == 0 || g_stream.remaining == 0) {
        (void)audio_pcm_end(); // the audio core reports the end once everything has played
        stream_close();
    }
}

void storage_task(void *arg) {
    (void)arg;
    for (;;) {
        storage_req_t rq;
        // While a file streams, wake every millisecond to keep the audio data ring full.
        if (!os_queue_recv(g_req_q, &rq, g_stream.active ? 1u : OS_WAIT_FOREVER)) {
            stream_pump();
            continue;
        }
        storage_result_t res = {0};
        switch (rq.kind) {
        case REQ_PLAY:
            stream_start(rq.path);
            continue;
        case REQ_STOP:
            stream_close();
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
