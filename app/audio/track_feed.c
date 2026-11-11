// SPDX-License-Identifier: MPL-2.0
// Core-0 side of one track: header parsing and feeding the audio data ring. See track_feed.h. (Not core-1 code.)
#include "track_feed.h"

#include <stdio.h>
#include <string.h>

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

static void fill_format(track_info_t *pf, const char *name, unsigned bits, uint32_t rate, unsigned channels) {
    char r[12];
    rate_text(rate, r, sizeof r);
    snprintf(pf->format, sizeof pf->format, "%s %u/%s%s", name, bits, r, channels == 1 ? " mono" : "");
}

const char *track_feed_open(track_feed_t *t, const char *path, uint64_t size, meta_read_fn rd, void *rctx, bool verify,
                            track_info_t *pf) {
    memset(t, 0, sizeof *t);
    memset(pf, 0, sizeof *pf);
    t->rd = rd;
    t->rctx = rctx;
    audio_stream_desc_t *d = &pf->desc;
    const char *fail = NULL;
    if (size > 0xF0000000u) {
        return "File too large"; // stream lengths are 32-bit (audio_shared.h)
    }
    snprintf(pf->tags.title, sizeof pf->tags.title, "%s", base_name(path));
    pf->title_is_name = true;

    if (ext_is(path, ".wav")) {
        wav_info_t w;
        wav_err_t we = wav_parse(rd, rctx, size, &w);
        if (we != WAV_OK) {
            fail = wav_err_str(we);
        } else {
            d->codec = AUDIO_CODEC_PCM;
            d->rate = w.rate;
            d->channels = w.channels;
            d->bits = w.bits;
            pf->rate = w.rate;
            pf->total_frames = w.frames;
            fill_format(pf, "WAV", w.bits, w.rate, w.channels);
            t->kind = TRACK_FEED_BYTES;
            t->base = w.data_offset;
            t->data_len = w.data_bytes;
            pf->seekable = !verify; // the byte of any frame
        }
    } else if (ext_is(path, ".flac")) {
        flac_info_t fi;
        meta_err_t me = flac_parse(rd, rctx, size, &fi);
        if (me != META_OK) {
            fail = meta_err_str(me);
        } else {
            d->codec = AUDIO_CODEC_FLAC;
            d->rate = fi.rate;
            d->channels = fi.channels;
            d->bits = fi.bits;
            d->verify = verify;
            d->total_frames = fi.total_frames;
            memcpy(d->md5, fi.md5, 16);
            pf->rate = fi.rate;
            pf->total_frames = fi.total_frames;
            pf->tags = fi.tags;
            pf->title_is_name = fi.tags.title[0] == '\0';
            if (pf->title_is_name) {
                snprintf(pf->tags.title, sizeof pf->tags.title, "%s", base_name(path));
            }
            fill_format(pf, "FLAC", fi.bits, fi.rate, fi.channels);
            t->kind = TRACK_FEED_BYTES;
            t->hdr_len = flac_make_header(&fi, t->hdr);
            t->base = fi.audio_offset;
            t->data_len = size > fi.audio_offset ? size - fi.audio_offset : 0;
            // Without a seek table the only way to a frame is decoding from the start (seconds of silence): not offered.
            uint64_t sp_sample, sp_off;
            pf->seekable = !verify && fi.total_frames != 0 && flac_seek_point(rd, rctx, size, fi.total_frames, &sp_sample, &sp_off);
        }
    } else if (ext_is(path, ".mp3") && !verify) {
        mp3_info_t mi;
        meta_err_t me = mp3_parse(rd, rctx, size, &mi);
        if (me != META_OK) {
            fail = meta_err_str(me);
        } else {
            d->codec = AUDIO_CODEC_MP3;
            d->rate = mi.rate;
            d->channels = mi.channels;
            d->bits = 32;
            pf->rate = mi.rate;
            pf->total_frames = mi.total_frames;
            pf->estimated = mi.estimated;
            pf->tags = mi.tags;
            pf->title_is_name = mi.tags.title[0] == '\0';
            if (pf->title_is_name) {
                snprintf(pf->tags.title, sizeof pf->tags.title, "%s", base_name(path));
            }
            snprintf(pf->format, sizeof pf->format, "MP3 %s%uk", mi.vbr ? "VBR ~" : "", (unsigned)mi.bitrate_kbps);
            t->kind = TRACK_FEED_BYTES;
            t->base = mi.audio_offset;
            t->data_len = size > mi.audio_offset ? size - mi.audio_offset : 0;
        }
    } else if ((ext_is(path, ".m4a") || ext_is(path, ".mp4")) && !verify) {
        meta_err_t me = mp4_parse(rd, rctx, size, &t->mp4);
        if (me != META_OK) {
            fail = meta_err_str(me);
        } else {
            const mp4_info_t *m = &t->mp4;
            d->codec = AUDIO_CODEC_ALAC;
            d->rate = m->rate;
            d->channels = m->channels;
            d->bits = m->bits;
            d->cookie_len = m->cookie_len;
            d->total_frames = m->total_frames;
            d->skip_frames = m->skip_frames;
            d->trim_exact = m->trimmed ? 1u : 0u;
            memcpy(d->cookie, m->cookie, m->cookie_len);
            pf->rate = m->rate;
            pf->total_frames = m->total_frames;
            pf->tags = m->tags;
            pf->title_is_name = m->tags.title[0] == '\0';
            if (pf->title_is_name) {
                snprintf(pf->tags.title, sizeof pf->tags.title, "%s", base_name(path));
            }
            fill_format(pf, "ALAC", m->bits, m->rate, m->channels);
            t->kind = TRACK_FEED_M4A;
            mp4_iter_init(&t->it, &t->mp4);
        }
    } else if (verify) {
        fail = "Verify works on FLAC files";
    } else {
        fail = "Not a playable file";
    }
    if (fail != NULL) {
        return fail;
    }
    pf->verify = verify;
    pf->file_size = size;
    pf->codec = d->codec;
    memcpy(pf->md5, d->md5, 16);
    t->remaining = t->data_len;
    return NULL;
}

uint64_t track_feed_seek(track_feed_t *t, track_info_t *pf, uint64_t frame) {
    audio_stream_desc_t *d = &pf->desc;
    pf->start_frame = 0;
    if (frame == 0 || pf->verify || (pf->total_frames != 0 && frame >= pf->total_frames)) {
        return 0;
    }
    if (d->codec == AUDIO_CODEC_PCM) {
        uint64_t off = frame * d->channels * ((d->bits + 7u) / 8u);
        if (off >= t->data_len) {
            return 0;
        }
        t->base += off;
        t->data_len -= off;
    } else if (d->codec == AUDIO_CODEC_FLAC) {
        uint64_t sample, off;
        if (d->total_frames == 0 || !flac_seek_point(t->rd, t->rctx, pf->file_size, frame, &sample, &off) || off >= t->data_len) {
            return 0;
        }
        t->base += off;
        t->data_len -= off;
        // The synthesised STREAMINFO announces the frames that really follow; the decoder drops the ones before the target.
        uint64_t left = d->total_frames - sample;
        t->hdr[21] = (uint8_t)((t->hdr[21] & 0xF0u) | ((left >> 32) & 0xFu));
        t->hdr[22] = (uint8_t)(left >> 24);
        t->hdr[23] = (uint8_t)(left >> 16);
        t->hdr[24] = (uint8_t)(left >> 8);
        t->hdr[25] = (uint8_t)left;
        d->skip_frames = (uint32_t)(frame - sample);
        d->total_frames -= frame;
    } else if (d->codec == AUDIO_CODEC_MP3) {
        uint64_t at, landed;
        if (!mp3_seek_point(t->rd, t->rctx, pf->file_size, t->base, pf->total_frames, frame, &at, &landed) || at >= t->base + t->data_len) {
            return 0;
        }
        frame = landed; // where the frame header really is, so the saved position does not drift from resume to resume
        t->data_len -= at - t->base; // no Xing/LAME frame any more: the decoder does not trim, it plays what follows
        t->base = at;
    } else if (d->codec == AUDIO_CODEC_ALAC) {
        const mp4_info_t *m = &t->mp4;
        uint64_t raw = frame + m->skip_frames;
        uint64_t k = m->frame_length != 0 ? raw / m->frame_length : m->sample_count;
        if (k >= m->sample_count) {
            return 0;
        }
        if (!mp4_iter_seek(&t->it, t->rd, t->rctx, (uint32_t)k)) {
            mp4_iter_init(&t->it, &t->mp4);
            return 0;
        }
        d->skip_frames = (uint32_t)(raw - k * m->frame_length);
        d->total_frames = d->total_frames > frame ? d->total_frames - frame : 0;
    } else {
        return 0;
    }
    t->remaining = t->data_len;
    pf->start_frame = frame;
    return frame;
}

void track_feed_relocated(track_feed_t *t) { t->it.info = &t->mp4; }

void track_feed_rewind(track_feed_t *t, uint64_t off) {
    if (t->kind == TRACK_FEED_BYTES) {
        if (off < t->hdr_len) {
            t->hdr_pos = (uint32_t)off;
            t->remaining = t->data_len;
        } else {
            uint64_t in = off - t->hdr_len;
            if (in > t->data_len) {
                in = t->data_len;
            }
            t->hdr_pos = t->hdr_len;
            t->remaining = t->data_len - in;
        }
    } else { // packet streams have no byte positions a decoder can seek to; restart from the first packet
        mp4_iter_init(&t->it, &t->mp4);
        t->cur_left = 0;
        t->len_pending = false;
    }
}

track_feed_status_t track_feed_step(track_feed_t *t, uint8_t *buf) {
    uint32_t space = audio_data_space();
    if (t->kind == TRACK_FEED_BYTES) {
        if (t->hdr_pos < t->hdr_len) {
            uint32_t n = t->hdr_len - t->hdr_pos;
            if (space >= n) {
                (void)audio_data_write(t->hdr + t->hdr_pos, n);
                t->hdr_pos += n;
            }
            return TRACK_FEED_MORE;
        }
        if (t->remaining == 0) {
            return TRACK_FEED_DONE;
        }
        if (space < TRACK_FEED_CHUNK) {
            return TRACK_FEED_MORE;
        }
        size_t want = t->remaining < TRACK_FEED_CHUNK ? (size_t)t->remaining : TRACK_FEED_CHUNK;
        int32_t got = t->rd(t->rctx, t->base + (t->data_len - t->remaining), buf, (uint32_t)want);
        if (got < 0) {
            return TRACK_FEED_ERROR;
        }
        if (got > 0) {
            (void)audio_data_write(buf, (uint32_t)got); // space was checked and we are the only producer
            t->remaining -= (uint64_t)got;
        }
        return got == 0 || t->remaining == 0 ? TRACK_FEED_DONE : TRACK_FEED_MORE;
    }
    // TRACK_FEED_M4A
    if (t->cur_left == 0 && !t->len_pending) {
        uint64_t off;
        uint32_t sz;
        if (!mp4_iter_next(&t->it, t->rd, t->rctx, &off, &sz)) {
            return TRACK_FEED_DONE;
        }
        t->cur_off = off;
        t->cur_left = t->cur_size = sz;
        t->len_pending = true;
        return TRACK_FEED_MORE;
    }
    if (t->len_pending) {
        if (space >= 4) {
            uint8_t l[4] = {(uint8_t)(t->cur_size >> 24), (uint8_t)(t->cur_size >> 16), (uint8_t)(t->cur_size >> 8),
                            (uint8_t)t->cur_size};
            (void)audio_data_write(l, 4);
            t->len_pending = false;
        }
        return TRACK_FEED_MORE;
    }
    if (space < 256u) {
        return TRACK_FEED_MORE;
    }
    size_t want = t->cur_left < TRACK_FEED_CHUNK ? t->cur_left : TRACK_FEED_CHUNK;
    if (want > space) {
        want = space;
    }
    int32_t got = t->rd(t->rctx, t->cur_off + (t->cur_size - t->cur_left), buf, (uint32_t)want);
    if (got <= 0) {
        return TRACK_FEED_ERROR;
    }
    (void)audio_data_write(buf, (uint32_t)got);
    t->cur_left -= (uint32_t)got;
    return TRACK_FEED_MORE;
}
