// SPDX-License-Identifier: MPL-2.0
// Gapless playback through the REAL core-1 code (app/audio/audio_core.c, included below so the test can see its state) and the
// REAL core-0 stream controller (track_stream.c, track_feed.c, audio_ctl.c): two or more tracks are fed back to back and the
// samples that reach the sink are compared, frame for frame, with the concatenation of each track decoded on its own.
// A fake sink stands in for the DAC: a thread plays the role of the block interrupt (paced by data availability, so the test
// does not depend on scheduler timing), the core-1 main loop runs in another thread, this thread is "core 0".
#define _GNU_SOURCE
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "../app/audio/audio_core.c" /* white box: the test reads core 1's state */
#include "hal_core.h"
#include "hal_system.h"
#include "os.h"
#include "track_stream.h"
#include "unit.h"

#define ASSETS NEUPOD_TEST_ASSETS

// ---- fakes: sink, core launch, clock, mutex -----------------------------------------------------------------------------

static pthread_mutex_t g_cap_mu = PTHREAD_MUTEX_INITIALIZER;
#define CAP_MAX_FRAMES (1u << 20)
static int32_t *g_cap;                    // captured audible output, interleaved stereo
static uint32_t g_cap_frames;
static bool g_cap_on = true;
static uint32_t g_changes_seen_by_sink;
static uint32_t g_change_at[8];          // capture position (frames) at which the sink saw each gapless track change
static uint32_t g_change_n;
static volatile int g_sink_opens;
static volatile uint32_t g_sink_rate;
static volatile uint32_t g_sink_mute_toggles;
// Trace of every sink-side track change (for diagnosing a failure) and optional clock jitter (NEUPOD_TEST_JITTER=1).
typedef struct { uint32_t changes, consumed, written, bound; int have; } trace_t;
static trace_t g_trace[64];
static int g_trace_n;
static bool g_jitter;
static hal_audio_block_cb g_cb;
static pthread_t g_clock;
static volatile bool g_clock_run;
static bool g_last_mute = true;

hal_status_t hal_audio_init(void) { return HAL_OK; }
bool hal_audio_rate_supported(uint32_t r) { return r == 44100u || r == 48000u || r == 88200u || r == 96000u; }
void hal_audio_set_block_callback(hal_audio_block_cb cb) { g_cb = cb; }
uint32_t hal_audio_underruns(void) { return 0; }
void hal_audio_idle(void) { usleep(100); }
const char *hal_audio_backend_name(void) { return "test"; }
void hal_audio_force_mute(void) {}
void hal_audio_get_info(hal_audio_info_t *o) {
    memset(o, 0, sizeof *o);
    o->rate_hz = g_sink_rate;
    o->rate_actual_mhz = g_sink_rate * 1000u;
}
void hal_audio_mute(bool m) {
    if (m != g_last_mute) {
        g_last_mute = m;
        g_sink_mute_toggles++;
    }
}
uint64_t hal_micros(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000u + (uint64_t)ts.tv_nsec / 1000u;
}
int32_t hal_audio_write(const int32_t *s, uint32_t frames) {
    pthread_mutex_lock(&g_cap_mu);
    if (g_audio_track_changes != g_changes_seen_by_sink) { // the interrupt counted a handoff while preparing this very block
        g_changes_seen_by_sink = g_audio_track_changes;
        if (g_trace_n < 64) g_trace[g_trace_n++] = (trace_t){g_audio_track_changes, g_pcm_consumed, g_pcm_written, g_bound_pos, g_have_bound};
        if (g_change_n < 8) g_change_at[g_change_n++] = g_cap_frames;
    }
    if (g_cap_on && g_cur_gain != 0 && g_cap_frames + frames <= CAP_MAX_FRAMES) { // audible blocks only (not priming silence)
        memcpy(g_cap + (size_t)g_cap_frames * 2u, s, (size_t)frames * 8u);
        g_cap_frames += frames;
    }
    pthread_mutex_unlock(&g_cap_mu);
    return (int32_t)frames;
}
static void *clock_thread(void *arg) {
    (void)arg;
    // Real-time block clock on absolute deadlines, like the sink's interrupt: the PCM ring (93 ms) is what has to ride out a decoder opening.
    uint64_t period = (uint64_t)HAL_AUDIO_BLOCK_FRAMES * 1000000u / g_sink_rate, next = hal_micros() + period;
    while (g_clock_run) {
        while (g_clock_run && hal_micros() < next) usleep(200);
        if (g_jitter && rand() % 40 == 0) usleep((useconds_t)(rand() % 12000));
        if (g_clock_run && g_cb != NULL) g_cb(0);
        next += period;
    }
    return NULL;
}
hal_status_t hal_audio_open(const hal_audio_format *fmt) {
    g_sink_rate = fmt->sample_rate;
    g_sink_opens++;
    g_clock_run = true;
    pthread_create(&g_clock, NULL, clock_thread, NULL);
    return HAL_OK;
}
void hal_audio_close(void) {
    g_clock_run = false;
    pthread_join(g_clock, NULL);
    g_sink_rate = 0;
}
static void *core1_entry(void *arg) {
    ((void (*)(void))arg)();
    return NULL;
}
hal_status_t hal_core1_launch(void (*entry)(void)) {
    pthread_t t;
    pthread_create(&t, NULL, core1_entry, (void *)entry);
    return HAL_OK;
}
size_t hal_core1_stack_free_min(void) { return 0; }
struct os_mutex { pthread_mutex_t m; };
os_mutex_t *os_mutex_create(void) {
    os_mutex_t *m = calloc(1, sizeof *m);
    pthread_mutex_init(&m->m, NULL);
    return m;
}
bool os_mutex_lock(os_mutex_t *m, uint32_t timeout_ms) {
    (void)timeout_ms;
    pthread_mutex_lock(&m->m);
    return true;
}
void os_mutex_unlock(os_mutex_t *m) { pthread_mutex_unlock(&m->m); }

// ---- core 0 side: files, the "player" ------------------------------------------------------------------------------------

typedef struct { FILE *f; } file_t;
static int32_t rd_cb(void *ctx, uint64_t off, void *dst, uint32_t n) {
    file_t *f = ctx;
    if (fseek(f->f, (long)off, SEEK_SET) != 0) return -1;
    return (int32_t)fread(dst, 1, n, f->f);
}
static const char *ops_open(const char *path, meta_read_fn *rd, void **rctx, uint64_t *size, char *msg, size_t msg_n) {
    file_t *f = calloc(1, sizeof *f);
    f->f = fopen(path, "rb");
    if (f->f == NULL) {
        snprintf(msg, msg_n, "cannot open %s", path);
        free(f);
        return msg;
    }
    fseek(f->f, 0, SEEK_END);
    *size = (uint64_t)ftell(f->f);
    *rd = rd_cb;
    *rctx = f;
    return NULL;
}
static void ops_close(void *rctx) {
    file_t *f = rctx;
    fclose(f->f);
    free(f);
}

static char g_list[8][600];
static int g_list_n, g_list_pos;
static bool g_gapless = true;
static int g_art_calls;
static int g_announced, g_started, g_failed;
static volatile bool g_started_reset_cap;
static uint64_t g_start_req;      // play_list(): where the first track starts (resume), 0 = the beginning
static uint64_t g_started_frame;  // the start position the stream controller reported for the last start
static uint32_t g_seen_changes; // track changes the "player" has applied to its queue position
// Like player_next_track(): first fold in any handoff the audio core completed, THEN look at what follows. (The first version of this
// test advanced the position only in its own loop, so a handoff completing between that and the feed's question made the feed
// announce the same track again: a second, real handoff. The engine was right; see NEUPOD_TEST_RACE.)
static void sync_queue(void) {
    uint32_t c = audio_track_change_count();
    while (c != g_seen_changes) { g_seen_changes++; g_list_pos++; }
}
static bool ops_next_track(char *path, size_t n) {
    sync_queue();
    if (!g_gapless || g_list_pos + 1 >= g_list_n) return false;
    snprintf(path, n, "%s", g_list[g_list_pos + 1]);
    return true;
}
static void ops_next_announced(const track_info_t *i) { (void)i; g_announced++; }
static void ops_track_changed(const char *p, const track_info_t *i) { (void)p; (void)i; g_art_calls++; }
static void ops_started(const char *p, const track_info_t *i) {
    g_started_frame = i->start_frame;
    (void)p;
    g_started++;
    if (g_started_reset_cap) { // the audio core has flushed and is silent: what is captured from here on is this track only
        pthread_mutex_lock(&g_cap_mu);
        g_cap_frames = 0;
        g_change_n = 0;
        pthread_mutex_unlock(&g_cap_mu);
    }
}
static void ops_failed(const char *m) { printf("  stream failed: %s\n", m); g_failed++; }
static uint32_t ops_now(void) { return (uint32_t)(hal_micros() / 1000u); }
static void ops_delay(uint32_t ms) { usleep(ms * 1000u); }
static const track_stream_ops_t k_ops = {.open = ops_open, .close = ops_close, .next_track = ops_next_track,
                                         .next_announced = ops_next_announced, .started = ops_started, .track_changed = ops_track_changed, .failed = ops_failed,
                                         .now_ms = ops_now, .delay_ms = ops_delay};

// ---- references: every track decoded on its own, in this thread, before the engine runs -------------------------------------

typedef struct {
    char path[600];
    uint32_t rate, channels;
    uint32_t frames;
    int32_t *pcm; // stereo
} ref_t;
static ref_t g_ref[16];
static int g_ref_n;
static uint8_t g_scratch[2048];

static const ref_t *ref_of(const char *path) {
    for (int i = 0; i < g_ref_n; i++) if (strcmp(g_ref[i].path, path) == 0) return &g_ref[i];
    return NULL;
}

typedef struct { const uint8_t *d; size_t n, pos; } mio_t;
static size_t mio_read(void *c, void *dst, size_t n) {
    mio_t *m = c;
    size_t k = m->n - m->pos < n ? m->n - m->pos : n;
    memcpy(dst, m->d + m->pos, k);
    m->pos += k;
    return k;
}
static bool mio_seek(void *c, int64_t off, int origin) {
    mio_t *m = c;
    int64_t t = origin == 0 ? off : (int64_t)m->pos + off;
    if (t < 0 || (size_t)t > m->n) return false;
    m->pos = (size_t)t;
    return true;
}
static int32_t whole_rd(void *ctx, uint64_t off, void *dst, uint32_t n) {
    const mio_t *m = ctx;
    if (off >= m->n) return 0;
    size_t k = off + n > m->n ? m->n - (size_t)off : n;
    memcpy(dst, m->d + off, k);
    return (int32_t)k;
}

// Build the byte stream the way core 0 feeds it, decode it with the codec directly, return stereo frames.
static void add_ref(const char *path) {
    ref_t *r = &g_ref[g_ref_n++];
    snprintf(r->path, sizeof r->path, "%s", path);
    FILE *f = fopen(path, "rb");
    if (!f) { printf("  cannot open %s\n", path); g_ref_n--; return; }
    fseek(f, 0, SEEK_END);
    size_t n = (size_t)ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *d = malloc(n);
    if (fread(d, 1, n, f) != n) n = 0;
    fclose(f);
    mio_t whole = {d, n, 0};
    track_feed_t t;
    track_info_t info;
    const char *fail = track_feed_open(&t, path, n, whole_rd, &whole, false, &info);
    if (fail) { printf("  parse %s: %s\n", path, fail); g_ref_n--; free(d); return; }
    r->rate = info.desc.rate;
    r->channels = info.desc.channels;
    r->pcm = malloc((size_t)200000 * 8);
    if (info.desc.codec == AUDIO_CODEC_PCM) { // WAV: 16-bit little-endian stereo
        uint32_t frames = (uint32_t)(t.data_len / 4u);
        for (uint32_t i = 0; i < frames * 2u; i++) {
            int16_t s = (int16_t)(d[t.base + 2u * i] | (d[t.base + 2u * i + 1u] << 8));
            r->pcm[i] = (int32_t)((uint32_t)(int32_t)s << 16);
        }
        r->frames = frames;
        free(d);
        return;
    }
    uint8_t *stream = malloc(n + 4 * (size_t)t.mp4.sample_count + 64);
    size_t sn = 0;
    if (t.kind == TRACK_FEED_BYTES) {
        memcpy(stream, t.hdr, t.hdr_len);
        sn = t.hdr_len;
        memcpy(stream + sn, d + t.base, (size_t)t.data_len);
        sn += (size_t)t.data_len;
    } else {
        mp4_iter_t it;
        mp4_iter_init(&it, &t.mp4);
        uint64_t off;
        uint32_t sz;
        while (mp4_iter_next(&it, whole_rd, &whole, &off, &sz)) {
            stream[sn] = (uint8_t)(sz >> 24); stream[sn + 1] = (uint8_t)(sz >> 16); stream[sn + 2] = (uint8_t)(sz >> 8); stream[sn + 3] = (uint8_t)sz;
            memcpy(stream + sn + 4, d + off, sz);
            sn += 4 + sz;
        }
    }
    const audio_stream_desc_t *s = &info.desc;
    codec_params_t p = {.kind = s->codec == AUDIO_CODEC_FLAC ? CODEC_FLAC : s->codec == AUDIO_CODEC_MP3 ? CODEC_MP3 : CODEC_ALAC,
                        .rate = s->rate, .channels = s->channels, .bits = s->bits, .total_frames = s->total_frames,
                        .skip_frames = s->skip_frames, .trim_exact = s->trim_exact != 0, .cookie_len = s->cookie_len};
    memcpy(p.cookie, s->cookie, sizeof p.cookie);
    mio_t m = {stream, sn, 0};
    codec_io_t io = {mio_read, mio_seek, NULL, &m};
    codec_t c;
    if (codec_open(&c, &p, &io) != CODEC_OK) { printf("  codec_open failed for %s\n", path); g_ref_n--; return; }
    uint32_t total = 0;
    for (;;) {
        int32_t k = codec_read(&c, r->pcm + (size_t)total * 2u, 256);
        if (k <= 0) break;
        total += (uint32_t)k;
    }
    codec_close(&c);
    r->frames = total;
    free(stream);
    free(d);
}

static void write_wav(const char *path, uint32_t frames, double hz) {
    FILE *f = fopen(path, "wb");
    uint32_t data = frames * 4u, rate = 44100u;
    uint8_t h[44] = {'R', 'I', 'F', 'F', 0, 0, 0, 0, 'W', 'A', 'V', 'E', 'f', 'm', 't', ' ', 16, 0, 0, 0, 1, 0, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 4, 0, 16, 0, 'd', 'a', 't', 'a'};
    uint32_t riff = 36 + data, brate = rate * 4u;
    memcpy(h + 4, &riff, 4); memcpy(h + 24, &rate, 4); memcpy(h + 28, &brate, 4); memcpy(h + 40, &data, 4);
    fwrite(h, 1, 44, f);
    for (uint32_t i = 0; i < frames; i++) {
        int16_t l = (int16_t)(20000.0 * __builtin_sin(2.0 * 3.14159265358979 * hz * i / rate) + (i % 7) - 3), r = (int16_t)(-l / 2 + 1 + (int16_t)(i % 5));
        fwrite(&l, 2, 1, f); fwrite(&r, 2, 1, f);
    }
    fclose(f);
}

// ---- the "player" loop: feeds, advances like player.c does when the audio core ends a track on its own -----------------------

typedef struct {
    int final_ends;            // END_OF_TRACK value 0
    int handoffs;              // END_OF_TRACK value 1
    int cmd_errors, underruns, storms;
} ev_t;
static ev_t g_ev;
static void poll_events(void) {
    audio_event_t e;
    while (audio_event_poll(&e)) {
        if (e.type == AUDIO_EVT_END_OF_TRACK) { if (e.value == 0) g_ev.final_ends++; else g_ev.handoffs++; }
        else if (e.type == AUDIO_EVT_CMD_ERROR) { g_ev.cmd_errors++; printf("  [core 1 command error %u]\n", e.value); }
        else if (e.type == AUDIO_EVT_UNDERRUN) g_ev.underruns++;
        else if (e.type == AUDIO_EVT_STORM) g_ev.storms++;
    }
}

static void set_list(int n, const char *const *paths) {
    g_list_n = n;
    g_list_pos = 0;
    g_seen_changes = audio_track_change_count();
    for (int i = 0; i < n; i++) snprintf(g_list[i], sizeof g_list[i], "%s", paths[i]);
}

static void reset_capture(void) {
    pthread_mutex_lock(&g_cap_mu);
    g_cap_frames = 0;
    g_change_n = 0;
    g_changes_seen_by_sink = g_audio_track_changes;
    pthread_mutex_unlock(&g_cap_mu);
}

// Run the list from track 0 until the audio core reports the end of the last track (or ms elapse). Like the player: when a
// track ends with nothing following in the core, start the next one the ordinary way.
static bool play_list(uint32_t max_ms) {
    memset(&g_ev, 0, sizeof g_ev);
    g_announced = g_started = g_failed = 0;
    g_art_calls = 0;
    reset_capture();
    uint32_t ended0 = audio_pcm_ended_count();
    uint32_t changes0 = audio_track_change_count();
    uint32_t seen_ended = ended0;
    g_seen_changes = changes0;
    track_stream_start_at(g_list[0], false, g_start_req);
    g_start_req = 0;
    uint64_t t0 = hal_micros();
    while (hal_micros() - t0 < (uint64_t)max_ms * 1000u) {
        sync_queue();
        if (getenv("NEUPOD_TEST_RACE")) usleep(3000);
        track_stream_pump();
        poll_events();
        if (audio_pcm_ended_count() != seen_ended) {
            seen_ended = audio_pcm_ended_count();
            if (g_list_pos + 1 < g_list_n) {
                g_list_pos++;
                track_stream_start(g_list[g_list_pos], false);
                continue;
            }
            usleep(5000);
            poll_events();
            return true;
        }
        usleep(150);
    }
    return false;
}

// Compare the captured output with the concatenation of references [from, to).
static bool compare(const char *const *paths, int n, uint32_t *total_out) {
    uint32_t pos = 0;
    pthread_mutex_lock(&g_cap_mu);
    bool ok = true;
    for (int i = 0; i < n && ok; i++) {
        const ref_t *r = ref_of(paths[i]);
        if (r == NULL) {
            pthread_mutex_unlock(&g_cap_mu);
            return false;
        }
        for (uint32_t k = 0; k < r->frames; k++, pos++) {
            if (pos < 256u) continue; // the first audible block is faded in by the volume ramp
            if (pos >= g_cap_frames || g_cap[2u * pos] != r->pcm[2u * k] || g_cap[2u * pos + 1u] != r->pcm[2u * k + 1u]) {
                printf("  mismatch at output frame %u (track %d frame %u): captured %s\n", pos, i, k,
                       pos >= g_cap_frames ? "nothing" : "different samples");
                { uint32_t z = pos; while (z < g_cap_frames && g_cap[2u*z] == 0 && g_cap[2u*z+1] == 0) z++; printf("    zeros for %u frames, capture holds %u more\n", z - pos, g_cap_frames - z); }
                for (uint32_t d = 0; d < 4 && pos + d < g_cap_frames; d++) printf("    cap %d,%d  ref %d,%d\n", g_cap[2u*(pos+d)], g_cap[2u*(pos+d)+1], r->pcm[2u*(k+d)], r->pcm[2u*(k+d)+1]);
                ok = false;
                break;
            }
        }
    }
    if (total_out) *total_out = pos;
    pthread_mutex_unlock(&g_cap_mu);
    return ok;
}

static void wait_idle(void) {
    for (int i = 0; i < 400 && g_src != AUDIO_SRC_NONE; i++) { poll_events(); usleep(2000); }
}

static char P[16][600];
static void set_path(int i, const char *name) { snprintf(P[i], sizeof P[i], "%s/%s", ASSETS, name); }

// ---- tests ---------------------------------------------------------------------------------------------------------------

TEST(same_format_tracks_are_concatenated_sample_exactly) {
    const char *l[] = {P[0], P[0]}; // flac16 44.1k stereo, again
    set_list(2, l);
    ASSERT_TRUE(play_list(8000));
    uint32_t total;
    ASSERT_TRUE(compare(l, 2, &total));
    printf("  2 tracks, %u frames compared; handoffs=%d final ends=%d announced=%d sink opens=%d cmd errors=%d\n", total,
           g_ev.handoffs, g_ev.final_ends, g_announced, g_sink_opens, g_ev.cmd_errors);
    ASSERT_EQ(1, g_ev.handoffs);
    ASSERT_EQ(1, g_ev.final_ends);
    ASSERT_EQ(1, g_announced);
    ASSERT_EQ(1, g_art_calls); // the controller told the art service about the new track at the handoff
    ASSERT_EQ(0, g_ev.cmd_errors);
    ASSERT_EQ(0, g_failed);
    ASSERT_EQ(1, g_sink_opens);       // the sink was never closed or re-clocked
    ASSERT_EQ(0, g_isr_stats.underruns); // not one starved block across the two handoffs
    ASSERT_EQ(1, g_list_pos);         // the player saw the track change
}
TEST(track_change_event_fires_when_the_last_sample_has_played) {
    const char *l[] = {P[0], P[1]};
    set_list(2, l);
    ASSERT_TRUE(play_list(8000));
    const ref_t *a = ref_of(P[0]);
    ASSERT_EQ(1, g_change_n);
    // The sink saw the change in the block that carries the last sample of track 1: within one block (256 frames) of its length.
    printf("  track 1 is %u frames; the change was reported at output frame %u\n", a->frames, g_change_at[0]);
    ASSERT_TRUE(g_change_at[0] <= a->frames && a->frames - g_change_at[0] <= HAL_AUDIO_BLOCK_FRAMES);
}
static void pair_case(const char *a, const char *b) {
    printf("  pair %s -> %s\n", strrchr(a, 0x2f) + 1, strrchr(b, 0x2f) + 1);
    const char *l[] = {a, b};
    set_list(2, l);
    wait_idle();
    uint32_t u0 = g_isr_stats.underruns;
    ASSERT_TRUE(play_list(8000));
    ASSERT_TRUE(compare(l, 2, NULL));
    ASSERT_EQ(1, g_ev.handoffs);
    ASSERT_EQ(1, g_ev.final_ends);
    ASSERT_EQ(0, g_ev.cmd_errors);
    ASSERT_EQ(u0, g_isr_stats.underruns); // not a single starved block across the handoff
    wait_idle();
}
// (Tracks in these vectors are a fraction of a second; one handoff must play out before the next can be announced, so chains of
// very short tracks are tested pair by pair. Real tracks are minutes long.)
TEST(every_codec_hands_over_to_every_other_codec) {
    pair_case(P[5], P[0]); // wav -> flac
    pair_case(P[0], P[1]); // flac -> mp3
    pair_case(P[1], P[2]); // mp3 -> alac
    pair_case(P[2], P[5]); // alac -> wav
    pair_case(P[0], P[0]); // the same file twice
}
TEST(trimmed_alac_joins_a_mono_flac_exactly_both_ways) {
    pair_case(P[3], P[4]); // flac 48k mono -> alac 48k mono, edit list (6000 of 9600 frames)
    pair_case(P[4], P[3]);
    ASSERT_EQ(6000, ref_of(P[4])->frames);
}
TEST(rate_change_falls_back_to_a_reclock) {
    const char *l[] = {P[0], P[6]}; // 44.1k then 48k
    set_list(2, l);
    int opens0 = g_sink_opens;
    ASSERT_TRUE(play_list(8000));
    // track 1 ended on its own, the "player" started track 2 the ordinary way; the sink was re-clocked
    ASSERT_EQ(0, g_ev.handoffs);
    ASSERT_EQ(2, g_ev.final_ends);
    ASSERT_EQ(0, g_announced);
    ASSERT_TRUE(g_sink_opens > opens0);
    ASSERT_EQ(48000, g_sink_rate);
    ASSERT_EQ(0, g_ev.cmd_errors);
    // what plays after the re-clock is the second track, exactly (capture restarted at its start)
    const ref_t *b = ref_of(P[6]);
    pthread_mutex_lock(&g_cap_mu);
    uint32_t cf = g_cap_frames;
    pthread_mutex_unlock(&g_cap_mu);
    ASSERT_TRUE(cf >= b->frames);
    wait_idle();
}
TEST(second_track_after_a_reclock_is_exact) {
    // Track 2 alone after the rate change: capture only what follows its start.
    const char *l[] = {P[6]};
    set_list(1, l);
    g_started_reset_cap = true;
    ASSERT_TRUE(play_list(8000));
    g_started_reset_cap = false;
    ASSERT_TRUE(compare(l, 1, NULL));
    wait_idle();
    // and back to 44.1k: another re-clock, track exact
    const char *l2[] = {P[0]};
    set_list(1, l2);
    int opens0 = g_sink_opens;
    g_started_reset_cap = true;
    ASSERT_TRUE(play_list(8000));
    g_started_reset_cap = false;
    ASSERT_TRUE(compare(l2, 1, NULL));
    ASSERT_EQ(opens0 + 1, g_sink_opens);
    ASSERT_EQ(44100, g_sink_rate);
}
TEST(gapless_off_still_advances_with_the_ordinary_start) {
    const char *l[] = {P[0], P[1]};
    set_list(2, l);
    g_gapless = false;
    uint32_t toggles0 = g_sink_mute_toggles;
    ASSERT_TRUE(play_list(8000));
    g_gapless = true;
    ASSERT_EQ(0, g_ev.handoffs);
    ASSERT_EQ(2, g_ev.final_ends);   // each track ended on its own
    ASSERT_EQ(0, g_announced);
    ASSERT_EQ(0, g_ev.cmd_errors);
    printf("  amp mute line toggled %u times across the two tracks (it stays asserted-off across a gapless handoff)\n",
           (unsigned)(g_sink_mute_toggles - toggles0));
    wait_idle();
}
TEST(gapless_handoff_does_not_toggle_the_mute_line_or_ramp) {
    const char *l[] = {P[0], P[1]};
    set_list(2, l);
    wait_idle();
    uint32_t t0 = g_sink_mute_toggles;
    // run the whole list and count mute toggles only between the first audible block and the last: one open (unmute) at the start
    // and one close at the very end are expected; none in between
    ASSERT_TRUE(play_list(10000));
    uint32_t toggles = g_sink_mute_toggles - t0;
    printf("  mute toggles for 3 gapless tracks: %u\n", (unsigned)toggles);
    ASSERT_TRUE(toggles <= 2);
    ASSERT_TRUE(compare(l, 2, NULL));
    wait_idle();
}
TEST(skip_during_a_pending_next_stream_plays_only_the_new_track) {
    const char *l[] = {P[7], P[1]}; // a 3 s track: its successor is announced long before its end
    set_list(2, l);
    memset(&g_ev, 0, sizeof g_ev);
    g_announced = g_started = 0;
    wait_idle();
    reset_capture();
    uint32_t changes0 = audio_track_change_count();
    track_stream_start(l[0], false);
    for (int i = 0; i < 8000 && !audio_stream_next_pending(); i++) { track_stream_pump(); usleep(200); }
    ASSERT_TRUE(audio_stream_next_pending()); // track 2 is announced to the audio core
    // the user presses Next twice quickly: a hard start of track 3 (a different file) while the announcement is pending
    const char *skip_to[] = {P[2]};
    set_list(1, skip_to); // the queue now ends with the chosen track
    g_started_reset_cap = true;
    int started0 = g_started;
    uint32_t e0 = audio_pcm_ended_count();
    track_stream_start(skip_to[0], false);
    ASSERT_TRUE(g_started > started0);
    for (uint64_t t0 = hal_micros(); hal_micros() - t0 < 8000000u && audio_pcm_ended_count() == e0; ) {
        track_stream_pump();
        poll_events();
        usleep(150);
    }
    usleep(20000);
    poll_events();
    g_started_reset_cap = false;
    ASSERT_EQ(changes0, audio_track_change_count()); // the pending handoff never happened
    ASSERT_TRUE(compare(skip_to, 1, NULL));          // only the chosen track's samples after the restart
    ASSERT_EQ(0, g_ev.cmd_errors);
    wait_idle();
}
TEST(stop_during_a_pending_next_stream_is_safe) {
    const char *l[] = {P[7], P[1]};
    set_list(2, l);
    wait_idle();
    reset_capture();
    uint32_t changes0 = audio_track_change_count();
    track_stream_start(l[0], false);
    for (int i = 0; i < 8000 && !audio_stream_next_pending(); i++) { track_stream_pump(); usleep(200); }
    ASSERT_TRUE(audio_stream_next_pending());
    track_stream_stop();     // what REQ_STOP does
    ASSERT_TRUE(audio_stop());
    ASSERT_FALSE(audio_stream_next_pending());
    for (int i = 0; i < 200 && g_src != AUDIO_SRC_NONE; i++) usleep(2000);
    ASSERT_EQ(AUDIO_SRC_NONE, g_src);
    ASSERT_FALSE(g_next_pending);
    ASSERT_EQ(changes0, audio_track_change_count());
    // a stale announcement must not poison the next stream: play something else and check it is exact
    const char *l2[] = {P[2]};
    set_list(1, l2);
    g_started_reset_cap = true;
    ASSERT_TRUE(play_list(8000));
    g_started_reset_cap = false;
    ASSERT_TRUE(compare(l2, 1, NULL));
    ASSERT_EQ(0, g_ev.cmd_errors);
    wait_idle();
}
TEST(seek_withdraws_an_announced_stream_and_a_new_one_can_follow) {
    // Core-0 bookkeeping of the seek handshake (decoders only seek while a stream opens, but the rule must hold): announcing, then a
    // seek request, withdraws the announcement and the end marker; the next stream can be announced afterwards.
    wait_idle();
    audio_stream_desc_t d;
    memset(&d, 0, sizeof d);
    d.codec = AUDIO_CODEC_PCM; d.rate = 44100; d.channels = 2; d.bits = 16;
    uint32_t want = audio_pcm_started_count() + 1u;
    ASSERT_TRUE(audio_stream_start(&d));
    for (int i = 0; i < 500 && audio_pcm_started_count() != want; i++) usleep(1000);
    for (int i = 0; i < 500 && g_audio_open_seq != g_cur_seq; i++) usleep(1000);
    ASSERT_TRUE(audio_stream_next_ok());
    ASSERT_TRUE(audio_stream_next(&d));
    ASSERT_TRUE(audio_stream_next_pending());
    ASSERT_TRUE(g_audio_next_valid != 0);
    uint32_t seq = g_audio_next_valid;
    g_audio_seek_off = 0; // pretend the decoder asked to go back to the start
    g_audio_seek_req++;
    uint64_t off;
    ASSERT_TRUE(audio_seek_poll(&off));
    audio_seek_ack();
    ASSERT_FALSE(audio_stream_next_pending());
    ASSERT_EQ(0, g_audio_next_valid);
    ASSERT_EQ(SIO_END_UNKNOWN, g_audio_slot[g_cur_seq & 1u].end);
    g_audio_seek_done = g_audio_seek_req; // (core 1 would flush and say so)
    ASSERT_TRUE(audio_seek_done());
    ASSERT_TRUE(audio_stream_next_ok());
    ASSERT_TRUE(audio_stream_next(&d));
    ASSERT_TRUE(g_audio_next_valid != seq);
    audio_stream_next_cancel();
    ASSERT_TRUE(audio_stop());
    track_stream_stop();
    wait_idle();
}
TEST(an_incompatible_announcement_is_refused_by_the_core_and_the_track_ends_normally) {
    // Defence in depth: core 0 never announces a different rate or channel count, but if one slipped through, core 1 must not
    // splice it in: the first track ends normally (final end event) and an error is reported.
    wait_idle();
    memset(&g_ev, 0, sizeof g_ev);
    track_stream_start(P[0], false);
    for (int i = 0; i < 4000 && !audio_stream_next_ok(); i++) { track_stream_pump(); usleep(200); }
    audio_stream_desc_t d;
    memset(&d, 0, sizeof d);
    d.codec = AUDIO_CODEC_PCM; d.rate = 48000; d.channels = 2; d.bits = 16;
    ASSERT_TRUE(audio_stream_next(&d));
    // The feed: finish track 1 and publish its end, nothing of the "other" stream is written.
    uint32_t e0 = audio_pcm_ended_count();
    for (uint64_t t0 = hal_micros(); hal_micros() - t0 < 6000000u && audio_pcm_ended_count() == e0 && g_ev.cmd_errors == 0; ) {
        track_stream_pump();
        poll_events();
        usleep(150);
    }
    for (int i = 0; i < 100 && g_ev.final_ends == 0; i++) { poll_events(); usleep(5000); }
    ASSERT_EQ(1, g_ev.cmd_errors);
    ASSERT_EQ(1, g_ev.final_ends);
    track_stream_stop();
    wait_idle();
}

// ---- resume: start a track at a given frame ------------------------------------------------------------------------------

// Play one track from @p frame to its end; the output must equal the reference from the frame the controller reports.
static bool play_from(const char *path, uint64_t frame) {
    wait_idle();
    const char *l[] = {path};
    set_list(1, l);
    g_start_req = frame;
    return play_list(8000);
}
// Captured output == reference[start ...], same length.
static bool tail_exact(const char *path, uint32_t start) {
    const ref_t *r = ref_of(path);
    uint32_t want = r->frames - start;
    bool ok = true;
    pthread_mutex_lock(&g_cap_mu);
    if (g_cap_frames < want || g_cap_frames > want + 1100u) { // (the sink pads the end with silence: block rounding and the ramp-down)
        printf("  length: captured %u frames, expected %u\n", g_cap_frames, want);
        ok = false;
    }
    for (uint32_t k = 256; ok && k < g_cap_frames && k < want; k++) { // (the first block is faded in by the volume ramp)
        if (g_cap[2u * k] != r->pcm[2u * (start + k)] || g_cap[2u * k + 1u] != r->pcm[2u * (start + k) + 1u]) {
            printf("  mismatch at output frame %u (track frame %u)\n", k, start + k);
            ok = false;
        }
    }
    pthread_mutex_unlock(&g_cap_mu);
    return ok;
}

// Everything captured beyond the @p want frames of a stream must be silence, and not a lot of it: the sink plays the end out in whole blocks
// and the volume ramp-down follows the end event, so how many silent frames trail depends on when the block interrupt thread is scheduled
// (NEUPOD_TEST_JITTER delays it by up to 12 ms, a few blocks). A fixed slack (tail_exact's 1100) is too tight for that; audio there is a bug.
// Lock held.
static bool trailing_silence(uint32_t want) {
    if (g_cap_frames < want || g_cap_frames > want + 8192u) {
        printf("  length: captured %u frames, expected %u (+ silence)\n", g_cap_frames, want);
        return false;
    }
    for (uint32_t k = want; k < g_cap_frames; k++) {
        if (g_cap[2u * k] != 0 || g_cap[2u * k + 1u] != 0) {
            printf("  audio beyond the end of the stream at output frame %u (want %u)\n", k, want);
            return false;
        }
    }
    return true;
}
// tail_exact() with the trailing silence judged by trailing_silence().
static bool tail_exact_pad(const char *path, uint32_t start) {
    const ref_t *r = ref_of(path);
    uint32_t want = r->frames - start;
    pthread_mutex_lock(&g_cap_mu);
    bool ok = trailing_silence(want);
    for (uint32_t k = 256; ok && k < g_cap_frames && k < want; k++) {
        if (g_cap[2u * k] != r->pcm[2u * (start + k)] || g_cap[2u * k + 1u] != r->pcm[2u * (start + k) + 1u]) {
            printf("  mismatch at output frame %u (track frame %u)\n", k, start + k);
            ok = false;
        }
    }
    pthread_mutex_unlock(&g_cap_mu);
    return ok;
}

TEST(resume_wav_starts_on_the_exact_frame) {
    ASSERT_TRUE(play_from(P[7], 50000));
    ASSERT_EQ(50000, g_started_frame);
    ASSERT_TRUE(tail_exact(P[7], 50000));
    ASSERT_EQ(0, g_failed);
}
TEST(resume_flac_with_a_seek_table_starts_on_the_exact_frame) {
    ASSERT_TRUE(play_from(P[8], 9000)); // nearest earlier point: 8192, the decoder drops 808 frames
    ASSERT_EQ(9000, g_started_frame);
    ASSERT_TRUE(tail_exact(P[8], 9000));
    ASSERT_TRUE(play_from(P[8], 4096)); // exactly on a point
    ASSERT_EQ(4096, g_started_frame);
    ASSERT_TRUE(tail_exact(P[8], 4096));
    ASSERT_TRUE(play_from(P[8], 13000)); // inside the last (short) frame
    ASSERT_EQ(13000, g_started_frame);
    ASSERT_TRUE(tail_exact(P[8], 13000));
    ASSERT_EQ(0, g_failed);
}
TEST(resume_flac_without_a_seek_table_starts_at_the_beginning) {
    ASSERT_TRUE(play_from(P[0], 9000));
    ASSERT_EQ(0, g_started_frame);
    ASSERT_TRUE(tail_exact(P[0], 0));
    ASSERT_EQ(0, g_failed);
}
TEST(resume_alac_starts_on_the_exact_frame_trimmed_or_not) {
    ASSERT_TRUE(play_from(P[2], 10000)); // packet 2 (4096 frames each), the decoder drops 1808
    ASSERT_EQ(10000, g_started_frame);
    ASSERT_TRUE(tail_exact(P[2], 10000));
    ASSERT_TRUE(play_from(P[4], 3000)); // edit list: 2112 frames of priming are not part of the track, 6000 frames play
    ASSERT_EQ(3000, g_started_frame);
    ASSERT_TRUE(tail_exact(P[4], 3000));
    ASSERT_TRUE(play_from(P[4], 5999)); // the last frame
    ASSERT_EQ(5999, g_started_frame);
    ASSERT_TRUE(tail_exact(P[4], 5999));
    ASSERT_EQ(0, g_failed);
}
TEST(resume_mp3_starts_near_the_frame_and_plays_to_the_end) {
    char mp3[600];
    snprintf(mp3, sizeof mp3, "%s/mp3_44k_st.mp3", ASSETS);
    const ref_t *r = ref_of(P[1]);
    (void)r;
    ASSERT_TRUE(play_from(mp3, 22050));
    ASSERT_TRUE(g_started_frame > 22050 - 1200 && g_started_frame < 22050 + 1200); // the frame the landed header corresponds to (CBR: about one frame)
    ASSERT_EQ(0, g_failed);
    pthread_mutex_lock(&g_cap_mu);
    uint32_t got = g_cap_frames;
    pthread_mutex_unlock(&g_cap_mu);
    // half of the 44100-frame vector remains, give or take the frame granularity and the padding that is no longer trimmed
    printf("  mp3 resume at 22050 played %u frames\n", got);
    ASSERT_TRUE(got > 22050u - 4600u && got < 22050u + 4600u);
    // the seek point is a frame header: confirmed by the one after it
    FILE *f = fopen(mp3, "rb");
    static uint8_t buf[16000];
    size_t n = f ? fread(buf, 1, sizeof buf, f) : 0;
    if (f) fclose(f);
    mio_t m = {buf, n, 0};
    uint64_t at = 0, landed = 0;
    ASSERT_TRUE(mp3_seek_point(whole_rd, &m, n, 0, 44100, 22050, &at, &landed));
    ASSERT_TRUE(landed == g_started_frame);
    ASSERT_TRUE(at > 0 && at < n);
    ASSERT_TRUE(buf[at] == 0xFF && (buf[at + 1] & 0xE0) == 0xE0);
}
TEST(resume_past_the_end_starts_at_the_beginning) {
    ASSERT_TRUE(play_from(P[5], 1000000)); // 5000-frame WAV
    ASSERT_EQ(0, g_started_frame);
    ASSERT_TRUE(tail_exact(P[5], 0));
    ASSERT_EQ(0, g_failed);
}

// ---- seek while playing (UI-7): the player restarts the same file at a frame (a hard start; the resume path) -------------------

// Which codecs the player may offer Seek on: those whose track_feed_seek() lands on the exact frame.
static bool seekable_of(const char *path) {
    meta_read_fn rd;
    void *rctx;
    uint64_t size;
    char msg[80];
    if (ops_open(path, &rd, &rctx, &size, msg, sizeof msg) != NULL) return false;
    static track_feed_t t;
    track_info_t info;
    meta_set_scratch(g_scratch, sizeof g_scratch);
    bool ok = track_feed_open(&t, path, size, rd, rctx, false, &info) == NULL && info.seekable;
    ops_close(rctx);
    return ok;
}
TEST(only_wav_and_flac_with_a_seek_table_are_seekable) {
    char mp3[600];
    snprintf(mp3, sizeof mp3, "%s/mp3_44k_st.mp3", ASSETS);
    ASSERT_TRUE(seekable_of(P[7]));   // WAV
    ASSERT_TRUE(seekable_of(P[8]));   // FLAC with a seek table
    ASSERT_FALSE(seekable_of(P[0]));  // FLAC without one: reaching a frame would mean decoding from the start
    ASSERT_FALSE(seekable_of(P[2]));  // ALAC
    ASSERT_FALSE(seekable_of(mp3));   // MP3
}

// Play @p path for a moment, then seek to @p frame the way player_seek() does; the output after the restart must be the reference from
// that frame on, to the end. Frame-exact: every captured sample is compared.
static bool seek_while_playing(const char *path, uint64_t frame, bool *exact_start) {
    wait_idle();
    const char *l[] = {path};
    set_list(1, l);
    memset(&g_ev, 0, sizeof g_ev);
    g_failed = 0;
    reset_capture();
    track_stream_start(path, false);
    for (uint64_t t0 = hal_micros(); hal_micros() - t0 < 120000u;) { track_stream_pump(); usleep(150); } // some audio is playing
    g_started_reset_cap = true;
    track_stream_start_at(path, false, frame);
    g_started_reset_cap = false;
    *exact_start = g_started_frame == frame;
    uint32_t e0 = audio_pcm_ended_count();
    for (uint64_t t0 = hal_micros(); hal_micros() - t0 < 8000000u && audio_pcm_ended_count() == e0;) {
        track_stream_pump();
        poll_events();
        usleep(150);
    }
    usleep(20000);
    poll_events();
    return audio_pcm_ended_count() != e0;
}

TEST(seek_in_a_playing_wav_lands_on_the_exact_frame) {
    bool exact;
    ASSERT_TRUE(seek_while_playing(P[7], 80000, &exact));
    ASSERT_TRUE(exact);
    ASSERT_TRUE(tail_exact_pad(P[7], 80000));
    ASSERT_EQ(0, g_failed);
    ASSERT_EQ(0, g_ev.cmd_errors);
    ASSERT_TRUE(seek_while_playing(P[7], 3, &exact)); // backwards (the first part is played again), and the first frames of the file
    ASSERT_TRUE(exact);
    ASSERT_TRUE(tail_exact_pad(P[7], 3));
}
TEST(seek_in_a_playing_flac_with_a_seek_table_lands_on_the_exact_frame) {
    bool exact;
    ASSERT_TRUE(seek_while_playing(P[8], 9000, &exact)); // nearest earlier point 8192: the decoder drops 808 frames
    ASSERT_TRUE(exact);
    ASSERT_TRUE(tail_exact_pad(P[8], 9000));
    ASSERT_TRUE(seek_while_playing(P[8], 4097, &exact)); // one past a point
    ASSERT_TRUE(exact);
    ASSERT_TRUE(tail_exact_pad(P[8], 4097));
    ASSERT_EQ(0, g_failed);
    ASSERT_EQ(0, g_ev.cmd_errors);
}
TEST(seek_to_the_last_frame_plays_that_frame_and_ends) {
    bool exact;
    ASSERT_TRUE(seek_while_playing(P[5], 4999, &exact)); // the 5000-frame WAV: end of track follows at once (the player never asks for the end itself)
    ASSERT_TRUE(exact);
    ASSERT_TRUE(tail_exact_pad(P[5], 4999));
    ASSERT_EQ(1, g_ev.final_ends);
    ASSERT_EQ(0, g_failed);
}

// Captured output == first[from_a ...] followed by all of second.
static bool compare_tail_then(const char *first, uint32_t from_a, const char *second) {
    const ref_t *a = ref_of(first), *b = ref_of(second);
    uint32_t want = a->frames - from_a + b->frames;
    bool ok = true;
    pthread_mutex_lock(&g_cap_mu);
    ok = trailing_silence(want);
    for (uint32_t k = 256; ok && k < want && k < g_cap_frames; k++) {
        const int32_t *r = k < a->frames - from_a ? a->pcm + 2u * (from_a + k) : b->pcm + 2u * (k - (a->frames - from_a));
        if (g_cap[2u * k] != r[0] || g_cap[2u * k + 1u] != r[1]) {
            printf("  mismatch at output frame %u\n", k);
            ok = false;
        }
    }
    pthread_mutex_unlock(&g_cap_mu);
    return ok;
}

// The successor is announced to the audio core while the track plays; the seek withdraws it (hard start), and it is announced again for the
// restarted stream: the handoff must still be sample-exact, with the seek target as the start of the first part.
static bool seek_during_prefetch(const char *first, uint64_t frame, const char *second) {
    const char *l[] = {first, second};
    wait_idle();
    set_list(2, l);
    memset(&g_ev, 0, sizeof g_ev);
    g_announced = g_failed = 0;
    reset_capture();
    track_stream_start(first, false);
    for (int i = 0; i < 8000 && !audio_stream_next_pending(); i++) { track_stream_pump(); usleep(200); }
    if (!audio_stream_next_pending()) return false; // the successor is prepared and announced: the state under test
    g_list_pos = 0;
    g_seen_changes = audio_track_change_count(); // the player re-bases its handoff count at a hard start
    uint32_t changes0 = audio_track_change_count();
    g_started_reset_cap = true;
    track_stream_start_at(first, false, frame);
    g_started_reset_cap = false;
    if (g_started_frame != frame) return false;
    g_announced = 0;
    uint32_t e0 = audio_pcm_ended_count();
    for (uint64_t t0 = hal_micros(); hal_micros() - t0 < 10000000u && audio_pcm_ended_count() == e0;) {
        sync_queue();
        track_stream_pump();
        poll_events();
        usleep(150);
    }
    usleep(20000);
    poll_events();
    return audio_pcm_ended_count() != e0 && audio_track_change_count() == changes0 + 1u;
}

TEST(seek_during_gapless_prefetch_keeps_the_handoff_sample_exact_wav) {
    ASSERT_TRUE(seek_during_prefetch(P[7], 132300 - 30000, P[0])); // 30000 frames of the WAV, then the FLAC
    ASSERT_TRUE(compare_tail_then(P[7], 132300 - 30000, P[0]));
    ASSERT_EQ(1, g_announced);        // announced again after the restart
    ASSERT_EQ(1, g_ev.handoffs);
    ASSERT_EQ(1, g_ev.final_ends);
    ASSERT_EQ(0, g_ev.cmd_errors);
    ASSERT_EQ(0, g_ev.underruns);
    ASSERT_EQ(0, g_failed);
}
TEST(seek_during_gapless_prefetch_keeps_the_handoff_sample_exact_across_codecs) {
    // (the seekable FLAC vector is too short to be caught mid-prefetch: the WAV is the track being sought, an ALAC track follows)
    ASSERT_TRUE(seek_during_prefetch(P[7], 100000, P[2]));
    ASSERT_TRUE(compare_tail_then(P[7], 100000, P[2]));
    ASSERT_EQ(1, g_announced);
    ASSERT_EQ(1, g_ev.handoffs);
    ASSERT_EQ(0, g_ev.cmd_errors);
    ASSERT_EQ(0, g_ev.underruns);
    ASSERT_EQ(0, g_failed);
}
TEST(repeated_seeks_in_a_row_leave_only_the_last_one_playing) {
    // A scrub that is applied several times in quick succession: each is a hard start, the last one wins and nothing of the others leaks.
    wait_idle();
    const char *l[] = {P[7]};
    set_list(1, l);
    reset_capture();
    track_stream_start(P[7], false);
    for (uint64_t t0 = hal_micros(); hal_micros() - t0 < 60000u;) { track_stream_pump(); usleep(150); }
    const uint64_t at[] = {20000, 100000, 50000, 90000};
    for (int i = 0; i < 4; i++) {
        g_started_reset_cap = true;
        track_stream_start_at(P[7], false, at[i]);
        g_started_reset_cap = false;
        for (uint64_t t0 = hal_micros(); hal_micros() - t0 < 25000u;) { track_stream_pump(); usleep(150); }
    }
    g_started_reset_cap = true;
    track_stream_start_at(P[7], false, 90000);
    g_started_reset_cap = false;
    uint32_t e0 = audio_pcm_ended_count();
    for (uint64_t t0 = hal_micros(); hal_micros() - t0 < 8000000u && audio_pcm_ended_count() == e0;) { track_stream_pump(); poll_events(); usleep(150); }
    usleep(20000);
    poll_events();
    ASSERT_TRUE(audio_pcm_ended_count() != e0);
    ASSERT_TRUE(tail_exact_pad(P[7], 90000));
    ASSERT_EQ(0, g_ev.cmd_errors);
    ASSERT_EQ(0, g_ev.storms);
}

int main(void) {
    g_cap = malloc((size_t)CAP_MAX_FRAMES * 8u);
    g_jitter = getenv("NEUPOD_TEST_JITTER") != NULL;
    meta_set_scratch(g_scratch, sizeof g_scratch);
    track_stream_init(&k_ops);
    set_path(0, "flac16_44k_st.flac");
    set_path(2, "alac16_44k_st.m4a");
    set_path(3, "flac16_48k_mono.flac");
    set_path(4, "alac16_48k_mono_elst.m4a");
    snprintf(P[5], sizeof P[5], "/tmp/neupod_gapless_%d.wav", (int)getpid());
    write_wav(P[5], 5000, 440.0);
    // The MP3 vector padded with copies of itself: bigger than the decoder's 24 KB read chunk (a decoder that wants a full chunk
    // before it opens cannot accept a successor earlier than the end of such a small file), and the LAME length (44100 frames) cuts
    // the output, so the junk after the last real frame must be skipped to reach the stream end.
    snprintf(P[1], sizeof P[1], "/tmp/neupod_gapless_mp3_%d.mp3", (int)getpid());
    {
        char src[600];
        snprintf(src, sizeof src, "%s/mp3_44k_st.mp3", ASSETS);
        FILE *in = fopen(src, "rb"), *out = fopen(P[1], "wb");
        static uint8_t buf[20000];
        size_t n = in ? fread(buf, 1, sizeof buf, in) : 0;
        for (int i = 0; i < 4 && out && n; i++) fwrite(buf, 1, n, out);
        if (in) fclose(in);
        if (out) fclose(out);
    }
    snprintf(P[7], sizeof P[7], "/tmp/neupod_gapless_long_%d.wav", (int)getpid());
    write_wav(P[7], 132300, 330.0);
    set_path(6, "flac24_48k_st.flac");
    set_path(8, "flac16_44k_st_seek.flac");
    for (int i = 0; i <= 8; i++) add_ref(P[i]);
    if (g_ref_n != 9) {
        printf("could not load the references\n");
        return 1;
    }
    for (int i = 0; i < g_ref_n; i++) printf("  reference %-26s %2u kHz %u ch %6u frames\n", strrchr(g_ref[i].path, '/') + 1, g_ref[i].rate / 1000u, (unsigned)g_ref[i].channels, g_ref[i].frames);

    // "core 0" starts the engine: sink open at 44.1 kHz stereo, volume 0 dB (bit-exact path)
    ASSERT_TRUE(audio_engine_start());
    ASSERT_TRUE(audio_open(44100, 2, 32));
    ASSERT_TRUE(audio_set_volume(0));
    for (int i = 0; i < 500 && !g_stream_open; i++) usleep(1000);
    ASSERT_TRUE(g_stream_open);

    RUN(same_format_tracks_are_concatenated_sample_exactly);
    RUN(track_change_event_fires_when_the_last_sample_has_played);
    RUN(every_codec_hands_over_to_every_other_codec);
    RUN(trimmed_alac_joins_a_mono_flac_exactly_both_ways);
    RUN(rate_change_falls_back_to_a_reclock);
    RUN(second_track_after_a_reclock_is_exact);
    RUN(gapless_off_still_advances_with_the_ordinary_start);
    RUN(gapless_handoff_does_not_toggle_the_mute_line_or_ramp);
    RUN(skip_during_a_pending_next_stream_plays_only_the_new_track);
    RUN(stop_during_a_pending_next_stream_is_safe);
    RUN(seek_withdraws_an_announced_stream_and_a_new_one_can_follow);
    RUN(an_incompatible_announcement_is_refused_by_the_core_and_the_track_ends_normally);
    RUN(resume_wav_starts_on_the_exact_frame);
    RUN(resume_flac_with_a_seek_table_starts_on_the_exact_frame);
    RUN(resume_flac_without_a_seek_table_starts_at_the_beginning);
    RUN(resume_alac_starts_on_the_exact_frame_trimmed_or_not);
    RUN(resume_mp3_starts_near_the_frame_and_plays_to_the_end);
    RUN(resume_past_the_end_starts_at_the_beginning);
    RUN(only_wav_and_flac_with_a_seek_table_are_seekable);
    RUN(seek_in_a_playing_wav_lands_on_the_exact_frame);
    RUN(seek_in_a_playing_flac_with_a_seek_table_lands_on_the_exact_frame);
    RUN(seek_to_the_last_frame_plays_that_frame_and_ends);
    RUN(seek_during_gapless_prefetch_keeps_the_handoff_sample_exact_wav);
    RUN(seek_during_gapless_prefetch_keeps_the_handoff_sample_exact_across_codecs);
    RUN(repeated_seeks_in_a_row_leave_only_the_last_one_playing);
    if (unit_failures) for (int i = 0; i < g_trace_n; i++) printf("  trace %d: changes=%u consumed=%u written=%u bound=%u have=%d\n", i, g_trace[i].changes, g_trace[i].consumed, g_trace[i].written, g_trace[i].bound, g_trace[i].have);
    unlink(P[1]);
    unlink(P[5]);
    unlink(P[7]);
    return RESULT();
}
