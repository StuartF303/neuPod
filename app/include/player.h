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
    uint32_t vol_att_db;
    bool muted;            /**< output stage mute line asserted */
} player_info_t;

/** After audio_engine_start(): create state and apply the default (low) volume. */
void player_init(void);

/** Latest state (cheap; also folds in end-of-file and stream faults reported by the audio core). */
void player_get(player_info_t *out);

/** Start @p mode, or stop it if it is already the active test signal. TONE_OFF stops. */
void player_tone_toggle(tone_mode_t mode);
/** Play the WAV at @p path (display name @p name). Result arrives through player_get(). */
void player_play_file(const char *path, const char *name);
/** Pause <-> resume. On a finished track (or a completed MD5 check) it plays the same file again from the start. Otherwise no-op. */
void player_pause_toggle(void);
void player_stop(void);

/** Volume steps: positive = louder, 2 dB each, clamped to [hard cap, off]. Returns the new attenuation in dB. */
uint32_t player_volume_step(int32_t steps);

/**
 * Apply a volume saved in the settings store (attenuation in dB) at boot. Ignored if the user already changed the volume
 * this session, and clamped to be at most as loud as the power-on default (AUDIO_VOL_DEFAULT_ATT_DB): a saved loud setting
 * never comes back at full blast after a power cycle (headphone safety).
 */
void player_restore_volume(uint32_t att_db);
#define PLAYER_RESTORE_MIN_ATT_DB AUDIO_VOL_DEFAULT_ATT_DB

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
} player_file_t;
void player_file_started(const player_file_t *f);
void player_file_failed(const char *msg);
/** Count the PCM ends seen by the audio core at the moment this stream was requested. */
uint32_t player_file_generation(void);

#ifdef __cplusplus
}
#endif

#endif // NEUPOD_PLAYER_H
