// SPDX-License-Identifier: MPL-2.0
/**
 * @file player.h
 * @brief Core-0 playback control for bring-up: test tones and WAV files, transport and volume.
 *
 * The UI calls this; it talks to the audio core (audio_engine.h) and, for files, to the storage task, which
 * reads the card and feeds the PCM data ring (storage_service.h). Volume is software attenuation in 2 dB
 * steps and always starts LOW (AUDIO_VOL_DEFAULT_ATT_DB, -30 dB): see docs/audio.md.
 */
#ifndef NEUPOD_PLAYER_H
#define NEUPOD_PLAYER_H

#include <stdbool.h>
#include <stdint.h>

#include "audio_dsp.h"
#include "audio_engine.h"
#include "meta.h"
#include "play_queue.h"
#include "storage_service.h"
#include "wav.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    PLAYER_IDLE = 0,
    PLAYER_OPENING, /**< a file was chosen, its header is being read */
    PLAYER_PLAYING,
    PLAYER_PAUSED,
    PLAYER_ENDED,   /**< the file played to its end */
    PLAYER_ERROR,   /**< could not play, or playback was stopped by a fault; see msg */
    PLAYER_VERIFYING, /**< FLAC verify run in progress (no sound) */
    PLAYER_VERIFIED,  /**< verify finished: see verify */
} player_state_t;

#define PLAYER_PATH_MAX 256u /**< longest file path remembered (same as NFS_PATH_MAX) */

typedef enum { PLAYER_SRC_NONE = 0, PLAYER_SRC_TONE, PLAYER_SRC_FILE } player_src_t;

typedef struct {
    player_state_t state;
    player_src_t src;
    tone_mode_t tone;      /**< PLAYER_SRC_TONE */
    char name[64];         /**< file name or tone name */
    audio_tags_t tags;     /**< title / artist / album / track number when the file has them */
    bool title_is_name;    /**< tags.title is the file name (no title tag) */
    bool estimated;        /**< total length is an estimate (MP3 without a Xing/Info frame count) */
    uint32_t verify;       /**< audio_verify_t (verify runs) */
    uint32_t codec;        /**< audio_codec_t */
    char format[32];       /**< e.g. "WAV 16/44.1" */
    char msg[80];          /**< reason when state is PLAYER_ERROR */
    uint32_t rate;
    uint64_t total_frames; /**< 0 for tones */
    uint32_t elapsed_s;    /**< from the audio core's own frame count */
    uint64_t pos_frames;   /**< the same position in frames of the track (a seek in progress shows its target) */
    uint32_t track_id;     /**< changes when another track plays (a start, a gapless handoff), not on a seek within the track */
    bool seekable;         /**< playing or paused on a track player_seek() can move within (WAV, FLAC with a seek table) */
    uint32_t vol_att_db;
    bool muted;            /**< output stage mute line asserted */
    uint16_t queue_pos;    /**< file playback through a folder queue: 1-based position ("n of N"); 0 when there is no queue */
    uint16_t queue_len;
    uint8_t shuffle;       /**< pq_shuffle_t of the queue being played; 0 when there is no queue */
    uint8_t repeat;        /**< pq_repeat_t of the queue being played; 0 when there is no queue */
} player_info_t;

/** After audio_engine_start(): create state and apply the default (low) volume. */
void player_init(void);

/** Latest state (cheap; also folds in end-of-file and stream faults reported by the audio core). */
void player_get(player_info_t *out);

/** Start @p mode, or stop it if it is already the active test signal. TONE_OFF stops. */
void player_tone_toggle(tone_mode_t mode);
/** Play the file at @p path (display name @p name), alone: no queue. Result arrives through player_get(). */
void player_play_file(const char *path, const char *name);
/**
 * Play the file at @p index of the folder listing @p entries (of @p dir) and queue the rest of the playable files of that folder in
 * listing order (play_queue.h); the files before it stay queued for Previous. The queue then plays through by itself.
 */
void player_play_folder(const char *dir, const file_entry_t *entries, uint32_t count, uint32_t index);
/** Next queued track (hard start). On the last track: stop, and Now Playing goes idle. No-op without a queue. */
void player_next(void);
/** Previous queued track; more than 3 s into a track (or on the first one) it restarts the current track instead. No-op without a queue. */
void player_prev(void);
/** Housekeeping that must not depend on a screen polling player_get(): advances the queue when a track ended. Called by the monitor task. */
void player_service(void);
/** "Gapless playback" setting (settings store, default On). Off: tracks still follow each other, with the ordinary stop/start between. */
bool player_gapless_enabled(void);
void player_set_gapless(bool on);
/**
 * Shuffle (off / songs) and repeat (off / one / all) of the folder queue; settings store, default Off. Changes apply at once to the queue
 * being played (turning shuffle on keeps the current track current); a gapless handoff already announced to the audio core still happens.
 * Repeat-one only loops the track when it ends by itself: Next still moves on. Repeat-all wraps at the end of the queue, re-shuffling
 * (never opening the new round with the track that just ended, if there is another).
 */
pq_shuffle_t player_shuffle(void);
pq_repeat_t player_repeat(void);
void player_set_shuffle(pq_shuffle_t mode);
void player_set_repeat(pq_repeat_t mode);
/**
 * Move playback to PCM frame @p frame of the current track (exact for the seekable tracks, player_info_t.seekable). Done as a hard start of
 * the same file at that frame (the resume path: a short mute, the announced gapless successor is withdrawn and offered again), so the audio
 * core and its rings see an ordinary new stream. Paused stays paused. @p frame at or past the end of the track is the end of the track: the
 * queue moves on as it does when a track ends by itself (repeat-one restarts it, the last track stops). False (nothing done) when the track
 * is not seekable or nothing is playing.
 */
bool player_seek(uint64_t frame);
/** Pause <-> resume. On a finished track (or a completed MD5 check) it plays the same file again from the start. Otherwise no-op. */
void player_pause_toggle(void);
/** Pause if playing (the headset went away: audio_route.h); position kept. No-op in any other state. */
void player_pause(void);
void player_stop(void);

/** Volume steps: positive = louder, 2 dB each, clamped to [hard cap, off]. Returns the new attenuation in dB. */
uint32_t player_volume_step(int32_t steps);

/**
 * Hold the software volume at 0 dB (true) while the headset's absolute volume is the volume (bt_control.h), or give the user's software volume back
 * (false). The software volume the user chose is kept, and its steps and restores change it without applying it while held.
 */
void player_volume_hold_unity(bool hold);

/**
 * Apply a volume saved in the settings store (attenuation in dB) at boot, by player_volume_restore_att() with the output path saved beside
 * it. Ignored if the user already changed the volume this session.
 */
void player_restore_volume(uint32_t att_db);
#define PLAYER_RESTORE_MIN_ATT_DB AUDIO_VOL_DEFAULT_ATT_DB

/**
 * Where the sound goes. A volume is saved together with the path it was set for (SETTING_VOLUME_PATH) and only comes back as loud as it was
 * when the path is the same (docs/bluetooth.md, "Volume restore policy"). Line-out and optical are separate values here on purpose: any
 * difference counts as a change of path. Nothing in the firmware detects the path yet (no Bluetooth audio routing, no jack detect): until a
 * source calls player_set_output_path() it stays OUT_PATH_UNKNOWN and every restore is capped, as before.
 */
typedef enum { OUT_PATH_UNKNOWN = 0, OUT_PATH_WIRED, OUT_PATH_LINE, OUT_PATH_OPTICAL, OUT_PATH_BT } output_path_t;
/** The output path now in use (call when it changes, and before the settings load at boot if it is known then). */
void player_set_output_path(output_path_t path);
/** A headphone insertion was detected at boot (before the settings load). Caps a volume that was saved for line-out. */
void player_set_headphones_at_boot(bool inserted);
/**
 * The restore policy as a pure function: attenuation to apply for a volume saved as @p saved_att on @p saved_path, now on @p cur_path.
 * The saved level is honoured, except it is raised to PLAYER_RESTORE_MIN_ATT_DB (-30 dB) when the path is unknown on either side or differs
 * from the saved one, or when headphones were inserted at boot and the saved path was line-out. A level quieter than -30 dB is never changed.
 */
uint32_t player_volume_restore_att(uint32_t saved_att, output_path_t saved_path, output_path_t cur_path, bool headphones_at_boot);

/** Setting "Resume playing on power-up" (settings store, default Off): the boot resume starts playing when the saved record says it was playing. */
bool player_resume_autoplay(void);
void player_set_resume_autoplay(bool on);

/**
 * Resume at boot (storage task, once the settings are loaded and the saved folder was listed): queue the folder @p entries of @p dir with the
 * entry at @p index current (with the saved shuffle and repeat modes: under shuffle the order is new, the track is the saved one), and start it
 * PAUSED at PCM frame @p frame (the exactness depends on the codec: track_feed.h). With @p play it starts PLAYING instead (the caller
 * passes the setting AND the record's playing flag). PLAYER_RESUME_BUSY: the player is not idle (the saved record should be kept);
 * PLAYER_RESUME_INVALID: the entry is not playable (nothing started).
 */
typedef enum { PLAYER_RESUME_OK, PLAYER_RESUME_BUSY, PLAYER_RESUME_INVALID } player_resume_t;
player_resume_t player_resume_folder(const char *dir, const file_entry_t *entries, uint32_t count, uint32_t index, uint32_t frame,
                                   bool play);

/** Short name for a mode, e.g. "1 kHz tone". */
const char *player_tone_name(tone_mode_t mode);

/** FLAC verify: decode the file flat out (no sound) and compare the STREAMINFO MD5. */
void player_verify_file(const char *path, const char *name);
/** Log (UART) what just ended: the verify verdict for verify runs. Called by the monitor on the end-of-track event. */
void player_log_end(void);

/* ---- Called by the storage task while it streams a file (not for the UI) ---- */
typedef struct {
    char format[32];
    uint32_t rate;
    uint64_t total_frames;
    audio_tags_t tags;
    bool title_is_name, estimated, verify;
    uint32_t codec;
    uint8_t md5[16];
    uint64_t file_size;
    uint64_t start_frame; /**< the stream starts at this frame of the track (resume), 0 normally */
    bool seekable;        /**< track_info_t.seekable */
} player_file_t;
void player_file_started(const player_file_t *f);
void player_file_failed(const char *msg);
/**
 * The stream that was playing broke (card read error mid-track). It is not the answer to a start request, so it never touches the count of
 * outstanding starts: with a start outstanding (a seek, Next) the restart replaces the broken stream and the error is dropped; otherwise it is
 * shown like any playback error.
 */
void player_stream_error(const char *msg);
/** Start requests posted and not answered yet (player_file_started / player_file_failed). 0 when everything asked for has been answered. For tests. */
uint32_t player_starts_outstanding(void);
/**
 * Gapless: the path of the track after the one playing, if the audio core may be told about it ahead of time (setting on, queue
 * has a next track, a file is playing). False otherwise.
 */
bool player_next_track(char *path, size_t n);
/** The audio core was told to continue with the track player_next_track() returned; @p f describes it. The player shows it when the audio core reports the handoff. */
void player_next_announced(const player_file_t *f);
/** Count the PCM ends seen by the audio core at the moment this stream was requested. */
uint32_t player_file_generation(void);

#ifdef __cplusplus
}
#endif

#endif // NEUPOD_PLAYER_H
