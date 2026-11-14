// SPDX-License-Identifier: MPL-2.0
/**
 * @file audio_route.h
 * @brief Where the sound goes (core 0): the wired output or the Bluetooth headset, and what happens to playback when the headset goes away.
 *
 * ADR-0019 (owner decisions 2 and 3): the saved setting "Audio output" is Auto, Wired or (BT-3, design review 2026-11-12) Bluetooth. Auto: Bluetooth as soon as a headset's audio stream is
 * open, otherwise wired. Wired: never Bluetooth. Bluetooth: the output is Bluetooth at once, headset or not (Play with none pauses with "Headset not connected"), and it stays Bluetooth through a Disconnect; the setting is the
 * only thing that moves it. Once the output is Bluetooth it stays Bluetooth until the headset is deliberately let go
 * (Disconnect / Forget) or the setting says Wired: a headset that is switched off or walks out of range pauses the player (position kept, ring
 * emptied) and shows a banner, and nothing comes out of the wired output by itself. Changing the output needs a quiet audio core
 * (ADR-0013): playback is stopped (its place is kept, as for Stop), the audio stream is closed, the output is selected and the stream reopened.
 * Speaker (owner-approved 2026-11-14, Y2 only): a fourth Audio output value, and a separate Settings > Speaker On / Off, default Off. The speaker is the wired sink's
 * third choice: Bluetooth if a headset's stream is open (Auto), else headphones if the jack has them, else the speaker if Speaker is On, else no output (silence, and a
 * running track pauses with "No audio output" when the speaker is turned off under it; a fresh Play with nothing to play on stays a silent null sink, as before).
 * Audio output = Speaker never uses Bluetooth; with Speaker Off it plays nothing. Headphones in always win over the speaker (the hardware cannot do both), and
 * pulling them out while playing pauses rather than jumping to the speaker. hal_audio_set_speaker() carries the decision to the HAL, which does the PA safety.
 * The A2DP stream is started when something plays on Bluetooth and suspended after a few seconds of silence.
 */
#ifndef NEUPOD_AUDIO_ROUTE_H
#define NEUPOD_AUDIO_ROUTE_H

#include <stdbool.h>
#include <stdint.h>

#include "audio_engine.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum { AUDIO_ROUTE_AUTO = 0, AUDIO_ROUTE_WIRED, AUDIO_ROUTE_BLUETOOTH, AUDIO_ROUTE_SPEAKER } audio_route_pref_t;

typedef enum {
    ROUTE_NOTICE_NONE = 0,
    ROUTE_NOTICE_HEADSET_DROPPED, /**< the headset went away while playing: "Headset disconnected" */
    ROUTE_NOTICE_NOT_CONNECTED,   /**< Play was pressed on the Bluetooth output with no headset: "Headset not connected" */
    ROUTE_NOTICE_HEADPHONES_UNPLUGGED, /**< the wired headphones were pulled out while playing: "Headphones unplugged" */
    ROUTE_NOTICE_LINE_OUT_OFF,    /**< the jack was unplugged with Line out on: it switched itself Off ("Line out off") */
    ROUTE_NOTICE_NO_OUTPUT,       /**< the speaker was playing and was turned off (Settings > Speaker): "No audio output" */
} audio_route_notice_t;

#define AUDIO_ROUTE_SUSPEND_MS 4000u /**< silence on the Bluetooth output for this long suspends the A2DP stream */
#define AUDIO_ROUTE_BANNER_MS 3000u  /**< how long the UI shows a notice */

/** After audio_engine_start() and player_init(): give the Bluetooth stack the PCM source, and apply the saved setting once it is known. */
void audio_route_init(void);

audio_route_pref_t audio_route_pref(void);
/** Settings > Speaker: may the internal speaker be used at all (saved, default Off). Needs a speaker (hal_audio_speaker_available()) to have any effect. */
bool audio_route_speaker_enabled(void);
void audio_route_set_speaker_enabled(bool on);
/**
 * Settings > Line out (saved, default Off; owner decision 2026-11-14): the jack at full level for an external amplifier. Active only while the output is the wired
 * jack with a plug in it: the DAC is then fixed at its top index and the software volume at 0 dB (no boost), and the wheel does not change the volume. Ignored on the
 * speaker and on Bluetooth. It switches itself Off when the jack is empty (unplugged, or at boot) so in-ear headphones never get full level unexpectedly.
 * audio_route_line_out_enabled() is the saved setting (false on a device without the hardware); audio_route_line_out_active() is "in force now".
 */
bool audio_route_line_out_enabled(void);
bool audio_route_line_out_active(void);
/** Turn it On (needs a plug in the jack: refused, false, otherwise) or Off. */
bool audio_route_set_line_out_enabled(bool on);
/** Save the setting (settings store) and apply it at the next service tick. */
void audio_route_set_pref(audio_route_pref_t pref);

/** The output in use now (wired until a headset's stream opens under Auto). */
audio_output_t audio_route_output(void);

/** The user disconnected or forgot the headset (Settings > Bluetooth): Bluetooth stops being the output once the link is down. */
void audio_route_user_released(void);

/** Housekeeping, every 100 ms from the monitor task: switch the output, start / suspend the stream, pause on link loss. */
void audio_route_service(void);

/** The latest notice for the UI and its running number (it changes with every new notice; ROUTE_NOTICE_NONE before the first). */
audio_route_notice_t audio_route_notice(uint32_t *seq);

#ifdef __cplusplus
}
#endif

#endif // NEUPOD_AUDIO_ROUTE_H
