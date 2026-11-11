// SPDX-License-Identifier: MPL-2.0
// Core-0 stream controller: one track playing, a gapless successor prepared behind it. See track_stream.h and docs/audio.md.
// (Not core-1 code.)
#include "track_stream.h"

#include <stdio.h>
#include <string.h>

#include "audio_engine.h"

typedef struct {
    void *rctx; /* NULL: not open */
    char path[256];
    track_feed_t feed;
    track_info_t info;
} trk_t;
typedef enum { NEXT_NONE, NEXT_READY, NEXT_ANNOUNCED, NEXT_NO } next_state_t;
#define NEXT_WAIT_MS 300u // at the end of a track, how long to wait for the audio core to accept the announcement

static const track_stream_ops_t *g_ops;
static struct {
    bool active;
    trk_t cur, nxt;
    next_state_t next_state;
    // seek handshake: 0 none, 1 acked and waiting for the audio core to flush
    int seek_state;
    // the current track is fully fed and nothing follows: its end is published (retried until done), and the stream stays
    // registered until the audio core reports the end, so a late seek request is still answered
    bool finishing;
    uint32_t ended_base;
    uint32_t promotions;     // successors that started being fed
    uint32_t changes_base;   // audio_track_change_count() when this stream started
    uint32_t changes_told;   // handoffs already reported through ops->track_changed
    uint32_t wait_since_ms;  // when the current track finished feeding while a prepared successor was not yet announced (0 = not waiting)
} g_stream;
static uint8_t g_chunk[TRACK_FEED_CHUNK];

void track_stream_init(const track_stream_ops_t *ops) { g_ops = ops; }

bool track_stream_active(void) { return g_stream.active; }

static void trk_close(trk_t *t) {
    if (t->rctx != NULL) {
        g_ops->close(t->rctx);
        t->rctx = NULL;
    }
}

void track_stream_stop(void) {
    audio_stream_next_cancel();
    trk_close(&g_stream.cur);
    trk_close(&g_stream.nxt);
    memset(&g_stream, 0, sizeof g_stream);
}

// Open @p path and parse it into @p t. NULL on success, else the reason.
static const char *trk_open(trk_t *t, const char *path, bool verify, uint64_t frame, char *msg, size_t msg_n) {
    memset(t, 0, sizeof *t);
    meta_read_fn rd;
    void *rctx;
    uint64_t size = 0;
    snprintf(t->path, sizeof t->path, "%s", path);
    const char *fail = g_ops->open(path, &rd, &rctx, &size, msg, msg_n);
    if (fail != NULL) {
        return fail;
    }
    meta_set_scratch(g_chunk, sizeof g_chunk); // header parsing borrows the chunk buffer: no feed step is running now
    fail = track_feed_open(&t->feed, path, size, rd, rctx, verify, &t->info);
    if (fail != NULL) {
        g_ops->close(rctx);
        return fail;
    }
    t->rctx = rctx;
    if (frame != 0) {
        (void)track_feed_seek(&t->feed, &t->info, frame); // nothing fed yet; on a codec that cannot seek the track simply starts at its beginning
    }
    return NULL;
}

void track_stream_start(const char *path, bool verify) { track_stream_start_at(path, verify, 0); }

void track_stream_start_at(const char *path, bool verify, uint64_t frame) {
    track_stream_stop();
    char msg[80];
    const char *fail = trk_open(&g_stream.cur, path, verify, frame, msg, sizeof msg);
    if (fail != NULL) {
        memset(&g_stream, 0, sizeof g_stream);
        g_ops->failed(fail);
        return;
    }
    uint32_t want = audio_pcm_started_count() + 1u;
    if (!audio_stream_start(&g_stream.cur.info.desc)) {
        track_stream_stop();
        g_ops->failed("Audio engine busy, try again");
        return;
    }
    // The audio core flushes its data ring while setting the stream up (and re-clocks the DAC if the rate
    // changed): write nothing before it says it is ready.
    for (uint32_t t = 0; audio_pcm_started_count() != want; t += 2) {
        if (t > 1500u) {
            track_stream_stop();
            g_ops->failed("Audio engine did not respond");
            return;
        }
        g_ops->delay_ms(2);
    }
    g_stream.ended_base = audio_pcm_ended_count();
    g_stream.changes_base = audio_track_change_count();
    g_stream.active = true;
    g_stream.next_state = verify ? NEXT_NO : NEXT_NONE;
    g_ops->started(g_stream.cur.path, &g_stream.cur.info);
}

// Same sample rate and channel count: the only case the audio core can continue without touching the sink (ADR-0003: no resampling).
static bool gapless_compatible(const audio_stream_desc_t *a, const audio_stream_desc_t *b) {
    return a->rate == b->rate && a->channels == b->channels && !b->verify;
}

// Open and announce the track that follows the current one. Called while the current track plays, and once more when it has
// been fed completely.
static void next_prepare(void) {
    if (g_stream.next_state == NEXT_NONE) {
        if (audio_track_change_count() - g_stream.changes_base != g_stream.promotions) {
            return; // the previous handoff has not played out yet; one pending boundary at a time
        }
        char path[256];
        if (!g_ops->next_track(path, sizeof path)) {
            g_stream.next_state = NEXT_NO;
            return;
        }
        char msg[80];
        const char *fail = trk_open(&g_stream.nxt, path, false, 0, msg, sizeof msg);
        if (fail != NULL) {
            printf("storage: next track '%s' not prepared (%s): it will start normally\n", path, fail);
            g_stream.next_state = NEXT_NO;
            return;
        }
        if (!gapless_compatible(&g_stream.cur.info.desc, &g_stream.nxt.info.desc)) {
            printf("storage: next track '%s' differs in rate or channels: no gapless handoff\n", path);
            trk_close(&g_stream.nxt);
            g_stream.next_state = NEXT_NO;
            return;
        }
        g_stream.next_state = NEXT_READY;
    }
    if (g_stream.next_state == NEXT_READY && audio_stream_next(&g_stream.nxt.info.desc)) {
        g_stream.next_state = NEXT_ANNOUNCED;
        g_ops->next_announced(&g_stream.nxt.info);
    }
}

// Reposition the feed for the audio core's seek request (stream byte offset) and tell it we are idle.
static void stream_seek(uint64_t off) {
    // audio_seek_ack() withdraws the end marker: the track is fed again from the offset and the marker is published after it.
    g_stream.finishing = false;
    track_feed_rewind(&g_stream.cur.feed, off);
    audio_seek_ack();
    if (g_stream.next_state == NEXT_ANNOUNCED && !audio_stream_next_pending()) {
        g_stream.next_state = NEXT_READY; // the seek withdrew the announcement (not yet being fed); the prepared track can be announced again
    }
    g_stream.seek_state = 1;
}

static void stream_error(const char *msg) {
    track_stream_stop();
    (void)audio_stop();
    (g_ops->stream_failed != NULL ? g_ops->stream_failed : g_ops->failed)(msg);
}

// The current track is completely fed. With a successor announced its bytes follow at once; otherwise this was the last one.
static void stream_track_done(void) {
    if (g_stream.next_state == NEXT_NONE || g_stream.next_state == NEXT_READY) {
        next_prepare(); // last chance to announce: the end cannot be published after the successor's bytes
        if (g_stream.next_state == NEXT_READY) {
            uint32_t now = g_ops->now_ms() | 1u;
            if (g_stream.wait_since_ms == 0) {
                g_stream.wait_since_ms = now;
            }
            if ((int32_t)(now - g_stream.wait_since_ms) < (int32_t)NEXT_WAIT_MS) {
                return; // the audio core has not accepted it yet (decoder still opening): try again next tick
            }
            trk_close(&g_stream.nxt);
            g_stream.next_state = NEXT_NO; // too late: the track ends normally and the player starts the next one
            audio_stream_next_cancel();
        }
        // NEXT_NONE here: the previous handoff has not played out yet, so no further one can be announced. Publish the end now
        // regardless: the audio core may be opening this very track and wait for its end (a file smaller than the decoder's read
        // chunk), and holding the end back would stall it.
    }
    g_stream.wait_since_ms = 0;
    if (g_stream.next_state == NEXT_ANNOUNCED && audio_stream_next_pending()) {
        (void)audio_stream_end(); // publishes the length; the stream's bytes from now on belong to the successor
        trk_close(&g_stream.cur);
        g_stream.cur = g_stream.nxt; // take over the successor (and its open file)
        track_feed_relocated(&g_stream.cur.feed);
        memset(&g_stream.nxt, 0, sizeof g_stream.nxt);
        g_stream.next_state = NEXT_NONE;
        g_stream.promotions++;
        return;
    }
    g_stream.next_state = NEXT_NO;
    g_stream.finishing = true; // the end is published from track_stream_pump, retried until done
}

void track_stream_pump(void) {
    if (!g_stream.active) {
        return;
    }
    if (audio_pcm_ended_count() != g_stream.ended_base) { // the audio core ended the stream (normally, or on a fault): nothing more can be asked
        track_stream_stop();
        return;
    }
    if (audio_track_change_count() - g_stream.changes_base != g_stream.changes_told && g_stream.changes_told < g_stream.promotions) {
        g_stream.changes_told++; // the successor is playing now: it is the current track (one handoff is pending at most)
        g_ops->track_changed(g_stream.cur.path, &g_stream.cur.info);
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
        if (!audio_stream_end_published()) {
            (void)audio_stream_end();
        }
        return;
    }
    if (g_stream.next_state == NEXT_NONE || g_stream.next_state == NEXT_READY) {
        // Prepare the successor early (while the track plays), but only when the ring is full: preparing reads headers from
        // the card and must not starve the feed.
        if (audio_data_space() < TRACK_FEED_CHUNK && audio_stream_next_ok()) {
            next_prepare();
        }
    }
    track_feed_status_t st = track_feed_step(&g_stream.cur.feed, g_chunk);
    if (st == TRACK_FEED_ERROR) {
        stream_error("Card read error during playback");
    } else if (st == TRACK_FEED_DONE) {
        stream_track_done();
    }
}
