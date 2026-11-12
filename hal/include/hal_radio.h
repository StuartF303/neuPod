// SPDX-License-Identifier: MPL-2.0
/**
 * @file hal_radio.h
 * @brief Wi-Fi and Bluetooth.
 *
 * Bluetooth (classic, via the CYW43439): stage 1 of docs/bluetooth.md provides bring-up, the local address and
 * device discovery (inquiry). Stage 2a adds pairing (SSP "just works") and a connection to one remote device
 * (hal_bt_pair / _connect / _disconnect / _forget, hal_bt_link_*): an ACL link, authentication and an SDP check that the
 * remote offers an A2DP sink. Stage 2b adds the A2DP stream on that link (hal_bt_stream_*, hal_bt_set_pcm_source): opened by itself once
 * the link is CONNECTED, started and suspended by the audio routing, reported with HAL_BT_EVT_STREAM.
 *
 * Threading: every hal_bt_* call may be made from any RTOS task on core 0. The event callback runs on the radio
 * worker (the CYW43 async-context task) with BTstack's lock held: it must be quick, must not block and must not call
 * back into hal_bt_*. Never call any of this from core 1. On the Y2 the stack runs on a thread of its own that must not touch
 * the RTOS, so the callback instead runs inside hal_bt_poll(), on the task that calls it (the UI frame); same rules.
 */
#ifndef NEUPOD_HAL_RADIO_H
#define NEUPOD_HAL_RADIO_H

#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "hal_bt_pcm.h"
#include "hal_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---- Wi-Fi (station mode) ---------------------------------------------------------------------------------- */

typedef enum { HAL_WIFI_SEC_OPEN, HAL_WIFI_SEC_WPA2, HAL_WIFI_SEC_WPA3, HAL_WIFI_SEC_WPA2_WPA3, HAL_WIFI_SEC_OTHER } hal_wifi_sec_t;

/** Copy @p src to @p dst (capacity @p n) for a log line: anything that is not printable ASCII (control bytes, UTF-8 bytes, DEL)
 * becomes '?', so a hostile network or device name cannot inject terminal escapes or fake log lines. */
static inline void hal_log_text(char *dst, size_t n, const char *src) {
    size_t i = 0;
    for (; n > 0 && i + 1 < n && src[i] != '\0'; i++) {
        unsigned char c = (unsigned char)src[i];
        dst[i] = c >= 0x20u && c < 0x7Fu ? (char)c : '?';
    }
    if (n > 0) {
        dst[i] = '\0';
    }
}

#define HAL_WIFI_SSID_MAX 32u
#define HAL_WIFI_PSK_MAX 63u

typedef struct {
    char ssid[HAL_WIFI_SSID_MAX + 1];
    uint8_t bssid[6];
    int8_t rssi;
    uint8_t channel;
    hal_wifi_sec_t sec;
} hal_wifi_ap_t;

typedef enum {
    HAL_WIFI_DOWN = 0, /**< radio / station mode off */
    HAL_WIFI_IDLE,     /**< on, not joined */
    HAL_WIFI_JOINING,
    HAL_WIFI_NO_IP,    /**< joined, waiting for DHCP */
    HAL_WIFI_UP,       /**< joined with an address */
    HAL_WIFI_FAILED,   /**< the last join failed (network not found, link error) */
    HAL_WIFI_BAD_AUTH, /**< wrong password */
} hal_wifi_status;

/** false on a platform with no Wi-Fi chip (the Y2): the boot check then reports Wi-Fi unavailable even though the radio driver, which
 * Bluetooth needs, came up. Every hal_wifi_* call on such a platform answers HAL_ENODEV / "down". */
bool hal_wifi_present(void);

/** Start the station interface (radio driver must be up). Cheap if already on. */
hal_status_t hal_wifi_start(void);
/** Leave any network and switch the station interface off (radio idle). */
void hal_wifi_stop(void);
hal_wifi_status hal_wifi_state(void);

/** Start a scan; results are fetched with hal_wifi_scan_next() until it returns false and *done is set. */
hal_status_t hal_wifi_scan_start(void);
bool hal_wifi_scan_next(hal_wifi_ap_t *out, bool *done);

/** Begin joining (WPA2/WPA3 personal, or open when @p psk is NULL or empty); poll hal_wifi_state(). */
hal_status_t hal_wifi_join(const char *ssid, const char *psk);
void hal_wifi_leave(void);
/** Give up a join that never finished: leave the network and report HAL_WIFI_FAILED (not IDLE) until the next join. */
void hal_wifi_abort_join(void);

#define HAL_WIFI_RSSI_UNKNOWN 0         /**< hal_wifi_rssi() when the link has no reading (a real RSSI is always negative) */
int hal_wifi_rssi(void);                 /**< dBm of the current link, HAL_WIFI_RSSI_UNKNOWN if unknown */
bool hal_wifi_ip(char out[16]);          /**< dotted address when UP */
bool hal_wifi_ssid(char out[33]);        /**< the joined network */

/** SNTP: ask @p server for the time (repeats about hourly); @p cb gets the Unix time on every answer. Runs on the network thread. */
typedef void (*hal_wifi_time_fn)(uint32_t unix_s);
hal_status_t hal_wifi_sntp_start(const char *server, hal_wifi_time_fn cb);
void hal_wifi_sntp_stop(void);

/**
 * HTTP/1.1 GET without TLS: @p on_data receives the body in pieces on the network thread (must not block; copy and return);
 * @p on_done gets the outcome (0 = ok, else an error number) and the total body bytes. Diagnostics (throughput test).
 */
typedef void (*hal_http_data_fn)(void *ctx, const uint8_t *data, uint32_t len);
typedef void (*hal_http_done_fn)(void *ctx, int err, uint32_t total);
hal_status_t hal_wifi_http_get(const char *host, uint16_t port, const char *path, hal_http_data_fn on_data, hal_http_done_fn on_done, void *ctx);
void hal_wifi_http_abort(void);

/** lwIP memory and pbuf high-water marks for the monitor line: used/max of the heap, the pbuf pool, and total lwIP memp errors. */
typedef struct { uint32_t mem_used, mem_max, mem_size, pbuf_used, pbuf_max, pbuf_size, errors; } hal_wifi_mem_t;
void hal_wifi_mem(hal_wifi_mem_t *out);
/** Call about every 200 ms: hardware: steps a join through the security modes (WPA2, WPA2/3, WPA3); host simulator: advances scans, joins and time. */
void hal_wifi_poll(void);

/* ---- Bluetooth: state, discovery ------------------------------------------------------------------------ */

typedef enum {
    HAL_BT_OFF = 0,  /**< not started */
    HAL_BT_STARTING, /**< controller firmware loading / HCI initialising */
    HAL_BT_ON,       /**< HCI up, idle */
    HAL_BT_SCANNING, /**< inquiry running */
    HAL_BT_FAILED,   /**< the stack or the controller did not come up */
} hal_bt_state_t;

#define HAL_BT_NAME_MAX 48u         /**< bytes kept of a remote name (UTF-8, NUL-terminated copy; 48 holds "Stuart's AirPods Pro - Find My" with its 3-byte apostrophe) */
#define HAL_BT_RSSI_UNKNOWN 127     /**< rssi value when the inquiry result carried none */
#define HAL_BT_COD_MAJOR_AUDIO_VIDEO 0x04u

typedef struct {
    uint8_t addr[6];            /**< MSB first */
    uint32_t cod;               /**< 24-bit Class of Device */
    int8_t rssi;                /**< dBm, HAL_BT_RSSI_UNKNOWN if not reported */
    bool audio_sink;            /**< major class Audio/Video (CoD bits 12..8 == 4): a headset, speaker, car kit ... */
    char name[HAL_BT_NAME_MAX + 1]; /**< "" until the remote name is known */
} hal_bt_device_t;

typedef enum {
    HAL_BT_EVT_STATE,     /**< state changed: ev.state */
    HAL_BT_EVT_DEVICE,    /**< a device was found, or its name arrived: ev.dev (the full record, repeated updates) */
    HAL_BT_EVT_SCAN_DONE, /**< inquiry (and the name requests that followed) finished or was stopped */
    HAL_BT_EVT_LINK,      /**< connection state changed: ev.link, ev.reason (FAILED only) and ev.dev (the remote device) */
    HAL_BT_EVT_STREAM,    /**< the audio stream changed state: ev.stream, ev.reason (FAILED only) and ev.codec (from OPEN on) */
    HAL_BT_EVT_VOLUME,    /**< the headset's absolute volume: ev.volume (0..127). The first one also says the headset has absolute volume (hal_bt_volume_supported()) */
    HAL_BT_EVT_KEY,       /**< a button on the headset: ev.key */
} hal_bt_event_type_t;

/** A headset button (AVRCP pass-through), reported on the press. FF and RW repeat while the button is held. */
typedef enum {
    HAL_BT_KEY_PLAY = 1,
    HAL_BT_KEY_PAUSE,
    HAL_BT_KEY_PLAY_PAUSE, /**< one button that toggles (many headsets send this; AirPods send PLAY or PAUSE according to the state they believe) */
    HAL_BT_KEY_NEXT,
    HAL_BT_KEY_PREV,
    HAL_BT_KEY_STOP,
    HAL_BT_KEY_FF,         /**< fast forward (held) */
    HAL_BT_KEY_RW,         /**< rewind (held) */
} hal_bt_key_t;

/* ---- Bluetooth: the audio stream (stage 2b) ------------------------------------------------------------------- */

/** State of the A2DP stream on the CONNECTED link. */
typedef enum {
    HAL_BT_STREAM_CLOSED = 0, /**< no stream (no link, not opened yet, or it was released) */
    HAL_BT_STREAM_OPENING,    /**< the audio connection is being set up (capabilities, configuration) */
    HAL_BT_STREAM_OPEN,       /**< configured, not started: hal_bt_stream_start() begins sending */
    HAL_BT_STREAM_STREAMING,  /**< media packets are being sent */
    HAL_BT_STREAM_SUSPENDED,  /**< was streaming, now paused (by us or by the headset); hal_bt_stream_start() resumes it */
    HAL_BT_STREAM_FAILED,     /**< the last open / start failed: see hal_bt_stream_reason() */
} hal_bt_stream_state_t;

typedef enum { HAL_BT_CODEC_NONE = 0, HAL_BT_CODEC_SBC, HAL_BT_CODEC_SBC_XQ, HAL_BT_CODEC_AAC, HAL_BT_CODEC_APTX } hal_bt_codec_kind_t;
typedef enum { HAL_BT_MODE_MONO = 0, HAL_BT_MODE_DUAL, HAL_BT_MODE_STEREO, HAL_BT_MODE_JOINT_STEREO } hal_bt_chmode_t;

/** The negotiated configuration of the stream, for the badge and the statistics. */
typedef struct {
    hal_bt_codec_kind_t codec; /**< SBC, or SBC_XQ when the bitpool in use is above the standard 53 */
    uint32_t rate;             /**< Hz */
    uint8_t channels;
    hal_bt_chmode_t mode;
    uint8_t bitpool_min, bitpool_max; /**< the range the headset accepts in the configuration */
    uint8_t bitpool;                  /**< the bitpool the encoder uses */
    uint16_t kbps;                    /**< bit rate of the encoded stream, kbit/s */
} hal_bt_codec_t;

/** A short label for the Now Playing badge: "SBC 44.1", "SBC-XQ" ... ("" for NONE). At most 15 characters. */
static inline void hal_bt_codec_label(const hal_bt_codec_t *c, char *out, size_t n) {
    if (c->codec == HAL_BT_CODEC_SBC_XQ) {
        snprintf(out, n, "SBC-XQ");
    } else if (c->codec == HAL_BT_CODEC_SBC) {
        unsigned khz10 = (unsigned)(c->rate / 100u); /* 44100 -> 441 */
        snprintf(out, n, "SBC %u.%u", khz10 / 10u, khz10 % 10u);
    } else {
        snprintf(out, n, "%s", c->codec == HAL_BT_CODEC_AAC ? "AAC" : c->codec == HAL_BT_CODEC_APTX ? "aptX" : "");
    }
}

/** Counters of the current stream (reset when it is opened; they keep counting across suspend and resume). */
typedef struct {
    uint32_t frames_encoded;     /**< codec frames produced (SBC: 128 samples each) */
    uint32_t packets_sent;
    uint32_t underruns;          /**< times the encoder wanted audio and the PCM ring had none */
    uint32_t drift_corrections;  /**< times the pacing trim against the ring's fill level changed step */
    int8_t link_quality;         /**< RSSI of the link in dBm where the controller reports one, HAL_BT_RSSI_UNKNOWN if not */
} hal_bt_stats_t;

/** State of the one connection (the remote headset). Independent of hal_bt_state_t, except that a link needs the stack ON. */
typedef enum {
    HAL_BT_LINK_DISCONNECTED = 0, /**< no link, nothing in progress */
    HAL_BT_LINK_PAIRING,          /**< Pair action: discoverable and connectable, ACL link, authentication (SSP), SDP */
    HAL_BT_LINK_CONNECTING,       /**< Connect to an already paired device: ACL link, authentication with the stored key, SDP */
    HAL_BT_LINK_CONNECTED,        /**< link up, authenticated, the remote offers an A2DP sink */
    HAL_BT_LINK_FAILED,           /**< the last Pair / Connect did not complete, or the link was lost: see hal_bt_link_reason() */
} hal_bt_link_state_t;

/** Why the link is in HAL_BT_LINK_FAILED. */
typedef enum {
    HAL_BT_FAIL_NONE = 0,
    HAL_BT_FAIL_NOT_FOUND, /**< the device did not answer the connection request (out of range, off, in use by something else) */
    HAL_BT_FAIL_AUTH,      /**< pairing was refused or the stored key no longer fits (forgotten on the headset: Forget and pair again) */
    HAL_BT_FAIL_NO_SINK,   /**< connected, but the device offers no A2DP sink (a phone, a keyboard ...) */
    HAL_BT_FAIL_TIMEOUT,   /**< no result within HAL_BT_PAIR_TIMEOUT_MS / HAL_BT_CONNECT_TIMEOUT_MS */
    HAL_BT_FAIL_LINK_LOST, /**< an established link ended without being asked to (the headset went away or switched off) */
    HAL_BT_FAIL_INTERNAL,  /**< the stack refused the request (out of buffers, busy) */
    HAL_BT_FAIL_BUSY,      /**< a Pair that the headset ended or had no resources for: busy with something else, or not in pairing mode */
    HAL_BT_FAIL_PIN,       /**< a pre-2.1 headset that wants a PIN: only Secure Simple Pairing is supported */
    HAL_BT_FAIL_STREAM,    /**< the audio connection could not be opened or started (the headset refused, or is busy with another source) */
    HAL_BT_FAIL_CODEC,     /**< the headset offers no audio format we can send (SBC, 44.1 kHz stereo) */
} hal_bt_fail_t;

/** One short phrase per reason, shared by the console and the UI ("Failed: <phrase>"; at most 19 characters). */
static inline const char *hal_bt_fail_text(hal_bt_fail_t r) {
    switch (r) {
    case HAL_BT_FAIL_NOT_FOUND: return "not found";
    case HAL_BT_FAIL_AUTH: return "pairing refused";
    case HAL_BT_FAIL_NO_SINK: return "not a headset";
    case HAL_BT_FAIL_TIMEOUT: return "timed out";
    case HAL_BT_FAIL_LINK_LOST: return "link lost";
    case HAL_BT_FAIL_INTERNAL: return "radio error";
    case HAL_BT_FAIL_BUSY: return "busy or not pairing";
    case HAL_BT_FAIL_PIN: return "PIN not supported";
    case HAL_BT_FAIL_STREAM: return "audio refused";
    case HAL_BT_FAIL_CODEC: return "no common format";
    case HAL_BT_FAIL_NONE: break;
    }
    return "error";
}

#define HAL_BT_PAIR_TIMEOUT_MS 30000u    /**< a Pair action is abandoned after this long */
#define HAL_BT_CONNECT_TIMEOUT_MS 20000u /**< ... and a Connect */

typedef struct {
    hal_bt_event_type_t type;
    hal_bt_state_t state;
    hal_bt_device_t dev;
    hal_bt_link_state_t link; /**< HAL_BT_EVT_LINK */
    hal_bt_fail_t reason;     /**< HAL_BT_EVT_LINK with link == HAL_BT_LINK_FAILED */
    uint32_t attempt;         /**< HAL_BT_EVT_LINK: the Pair / Connect attempt this event belongs to (hal_bt_link_attempt() at that time) */
    hal_bt_stream_state_t stream; /**< HAL_BT_EVT_STREAM */
    hal_bt_codec_t codec;     /**< HAL_BT_EVT_STREAM: the negotiated configuration (codec NONE until OPEN) */
    uint8_t volume;           /**< HAL_BT_EVT_VOLUME: 0..127 (0 = silent, 127 = the headset's loudest) */
    hal_bt_key_t key;         /**< HAL_BT_EVT_KEY */
} hal_bt_event_t;

typedef void (*hal_bt_event_fn)(const hal_bt_event_t *ev, void *ctx);

/**
 * Persistent tag/value/length storage for the Bluetooth stack (link keys, device database). Same contract as
 * BTstack's btstack_tlv_t, without its types: get returns the number of bytes copied (at most @p size; a NULL
 * @p buf asks for the value length instead), 0 if the tag is absent; put returns 0 on success; del removes the tag.
 * Provided by app/settings (SD-card backed, never the internal flash).
 */
typedef struct {
    int (*get)(void *ctx, uint32_t tag, uint8_t *buf, uint32_t size);
    int (*put)(void *ctx, uint32_t tag, const uint8_t *data, uint32_t size);
    void (*del)(void *ctx, uint32_t tag);
    void *ctx;
} hal_bt_storage_t;

/**
 * Bring Bluetooth up: register @p cb (may be NULL), give the stack its storage (NULL = none: pairing data is then
 * not kept), and power the controller on. Returns once the request is made; HAL_BT_EVT_STATE reports HAL_BT_ON when HCI
 * is up (about a second after the radio driver is). Needs the radio driver initialised (hal_system_late_init()).
 * On the host build this is a simulation.
 */
hal_status_t hal_bt_init(hal_bt_event_fn cb, void *ctx, const hal_bt_storage_t *storage);

hal_bt_state_t hal_bt_state(void);

/** The local Bluetooth device address (MSB first). false until the stack is up. */
bool hal_bt_local_address(uint8_t addr[6]);

/**
 * Start device discovery for @p duration_s seconds (rounded up to 1.28 s units, 1..48 s). Every device found is
 * reported with HAL_BT_EVT_DEVICE (twice for devices whose name had to be requested: first with an empty name),
 * then HAL_BT_EVT_SCAN_DONE. HAL_EBUSY if a scan runs or a Pair / Connect is being set up (an inquiry would starve it; a link that is
 * already CONNECTED does not prevent a scan), HAL_ENODEV if the stack is not up.
 */
hal_status_t hal_bt_inquiry_start(uint8_t duration_s);

/** Stop a running inquiry (HAL_BT_EVT_SCAN_DONE follows). No-op when idle. */
void hal_bt_inquiry_stop(void);

/* ---- Bluetooth: pairing and the connection (stage 2a) ------------------------------------------------------- */

/**
 * Pair with @p dev (address, class and name as found by the inquiry) and connect: the Pair action. The device becomes
 * connectable and bondable (never discoverable), for this headset only, ONLY for as long as this runs (until CONNECTED, FAILED or a disconnect), then
 * both go off again. Steps: outgoing ACL link, authentication with SSP "just works" (link key stored through
 * hal_bt_storage_t), then an SDP query for an A2DP sink. Progress is HAL_BT_EVT_LINK: PAIRING, then CONNECTED or FAILED.
 * HAL_ENODEV if the stack is not up, HAL_EBUSY if a scan runs (stop it first) or the link is not idle (PAIRING, CONNECTING
 * or CONNECTED), HAL_EINVAL for a NULL or all-zero address. Returns once the request is made.
 */
hal_status_t hal_bt_pair(const hal_bt_device_t *dev);

/**
 * Connect to a device that was paired before (its link key is in the storage): ACL link, authentication with the stored key,
 * SDP. Not discoverable, not connectable, not bondable. CONNECTING, then CONNECTED or FAILED. Same return codes as
 * hal_bt_pair(). Nothing connects by itself at power-up (stage 3).
 */
hal_status_t hal_bt_connect(const uint8_t addr[6]);

/** Cancel a Pair or Connect in progress, or drop the link. HAL_BT_EVT_LINK DISCONNECTED follows (no FAILED: it was asked for). No-op when idle. */
void hal_bt_disconnect(void);

/**
 * Forget a device: delete its link key from the storage (disconnecting first if it is the current peer, or cancelling a Pair /
 * Connect to it). The next connection needs a new Pair. Safe for an address that was never paired. HAL_OK: queued.
 * HAL_EBUSY: the request slots are full, try again later. HAL_ENODEV: the stack is not up (OFF, STARTING or FAILED), nothing was
 * done: delete the key straight from the storage with hal_bt_storage_delete_key(), which needs no stack.
 */
hal_status_t hal_bt_forget(const uint8_t addr[6]);

/**
 * BTstack's link key records (btstack_link_key_db_tlv.c, link_key_nvm_t): tag 'B','T','L' and the RAW slot index 0..7 (not an ASCII digit); value =
 * sequence number (4), address (6), key (16), key type (1 byte with the short enums this toolchain uses), padded to a multiple of 4 = 28 bytes.
 * hal_radio_bt.c asserts the size against BTstack's own types. Match a record by "at least offset + 6 bytes", not by an exact length.
 */
#define HAL_BT_KEY_SLOTS 8u
#define HAL_BT_KEY_RECORD_LEN 28u
#define HAL_BT_KEY_ADDR_OFFSET 4u
#define HAL_BT_KEY_TAG(i) (((uint32_t)'B' << 24) | ((uint32_t)'T' << 16) | ((uint32_t)'L' << 8) | (uint32_t)(i))
/** Delete the link key record for @p addr straight from @p st, without the stack (the same effect as hal_bt_forget()). */
static inline void hal_bt_storage_delete_key(const hal_bt_storage_t *st, const uint8_t addr[6]) {
    for (uint32_t i = 0; st != NULL && i < HAL_BT_KEY_SLOTS; i++) {
        uint8_t e[HAL_BT_KEY_RECORD_LEN];
        if (st->get(st->ctx, HAL_BT_KEY_TAG(i), e, sizeof e) >= (int)(HAL_BT_KEY_ADDR_OFFSET + 6u) && memcmp(e + HAL_BT_KEY_ADDR_OFFSET, addr, 6) == 0) {
            st->del(st->ctx, HAL_BT_KEY_TAG(i));
        }
    }
}

hal_bt_link_state_t hal_bt_link_state(void);
/** Number of the latest Pair / Connect attempt (counts up by one for every request that was accepted). A link event of an earlier attempt is stale. */
uint32_t hal_bt_link_attempt(void);
/** Why the link is FAILED (HAL_BT_FAIL_NONE in any other state; cleared by the next Pair / Connect). */
hal_bt_fail_t hal_bt_link_reason(void);
/** The remote device of the current attempt or link (PAIRING, CONNECTING, CONNECTED), or of the one that just FAILED (so a screen can tell
 * whose failure it is): address, class and name as given to Pair (Connect knows only the address); false when DISCONNECTED. */
bool hal_bt_link_device(hal_bt_device_t *out);
/** true while the device is connectable and bondable: only while a Pair action runs, and then for the chosen headset only. It is never discoverable. */
bool hal_bt_accepting(void);

/* ---- Bluetooth: the audio stream (stage 2b) ---------------------------------------------------------------- */

/** Register the PCM source (NULL = none: a started stream then sends nothing). Call before a stream starts; any task. @p src must stay valid for the life of the program. */
void hal_bt_set_pcm_source(const hal_bt_pcm_source_t *src);

/**
 * Open the A2DP stream on the CONNECTED link. The stack does this by itself right after CONNECTED, so the call is only for a retry after
 * HAL_BT_STREAM_FAILED or CLOSED. HAL_ENODEV if there is no CONNECTED link, HAL_EBUSY if a stream exists or is opening. Asynchronous:
 * HAL_BT_EVT_STREAM OPENING, then OPEN or FAILED.
 */
hal_status_t hal_bt_stream_open(void);
/** Begin sending (OPEN or SUSPENDED -> STREAMING). HAL_ENODEV if the stream is not OPEN / SUSPENDED. A no-op while it is already STREAMING. */
hal_status_t hal_bt_stream_start(void);
/** Pause sending (STREAMING -> SUSPENDED), the audio connection stays up. No-op in any other state. */
void hal_bt_stream_suspend(void);
/** Release the audio connection (the link stays). HAL_BT_EVT_STREAM CLOSED follows. No-op when CLOSED. */
void hal_bt_stream_close(void);

hal_bt_stream_state_t hal_bt_stream_state(void);
/** Why the stream is FAILED (HAL_BT_FAIL_NONE otherwise). */
hal_bt_fail_t hal_bt_stream_reason(void);
/** The negotiated configuration; false while CLOSED / OPENING / FAILED. */
bool hal_bt_stream_codec(hal_bt_codec_t *out);
/** Counters of the current stream (all zero when there is none). */
void hal_bt_stats(hal_bt_stats_t *out);

/* ---- Bluetooth: volume, headset keys, track information (stage 2c, AVRCP) ------------------------------------------ */

#define HAL_BT_VOLUME_MAX 127u /**< AVRCP absolute volume is 0..127 (0 % .. 100 %) */

/**
 * true while the headset has AVRCP absolute volume: it is connected and it has reported its volume. It is the headset that decides: the answer
 * can turn true a moment after CONNECTED (once AVRCP is up) and false when the link or the AVRCP channel goes. While true, the headset's own volume
 * is the volume of the player (hal_bt_volume_set / HAL_BT_EVT_VOLUME) and the software volume stays at 0 dB (docs/bluetooth.md); while false the
 * software volume is the volume.
 */
bool hal_bt_volume_supported(void);
/** The headset's volume 0..127 as last reported or set; -1 while hal_bt_volume_supported() is false. */
int hal_bt_volume_get(void);
/** Ask the headset to set its volume to @p volume (0..127, clamped). The value is what hal_bt_volume_get() returns at once; the headset's answer (it may round) follows as HAL_BT_EVT_VOLUME. HAL_ENODEV if not supported. */
hal_status_t hal_bt_volume_set(uint8_t volume);

#define HAL_BT_META_MAX 64u /**< bytes kept of a title / artist / album (the size of the tags the player reads; UTF-8, cut at a character boundary by hal_bt_meta_copy()) */

typedef enum { HAL_BT_PLAY_STOPPED = 0, HAL_BT_PLAY_PLAYING, HAL_BT_PLAY_PAUSED } hal_bt_play_status_t;

/** What the headset may show about the playing track (AVRCP metadata). */
typedef struct {
    uint32_t track_id;                 /**< changes with every track (not on a seek); the headset is told "track changed" when it does */
    char title[HAL_BT_META_MAX + 1];
    char artist[HAL_BT_META_MAX + 1];
    char album[HAL_BT_META_MAX + 1];
    uint32_t duration_ms;              /**< 0 = unknown */
    uint32_t position_ms;              /**< at the moment of the call; the stack adds the time that passes while PLAYING */
    hal_bt_play_status_t status;
} hal_bt_track_t;

/**
 * Tell the stack what is playing (any task; cheap; call it when the track or the play status changes, when the position jumps, and now and then
 * to keep the position honest). NULL = nothing is playing. With no AVRCP connection nothing is sent now; the latest information is what the
 * headset gets when it connects or asks. Strings must fit HAL_BT_META_MAX (hal_bt_meta_copy() cuts them). Returns false when the stack is
 * busy with the previous call (nothing was taken: call again a moment later, e.g. at the next service tick); true otherwise, also when there is
 * no stack to tell.
 */
bool hal_bt_track_info(const hal_bt_track_t *info);

/** Copy @p src into @p dst (size @p n >= 1), cutting at a UTF-8 character boundary so that no broken sequence is sent to a headset. */
static inline void hal_bt_meta_copy(char *dst, size_t n, const char *src) {
    size_t len = 0;
    while (len + 1 < n && src[len] != '\0') {
        len++;
    }
    if (src[len] != '\0') { /* cut: step back over continuation bytes (10xxxxxx) to the start of the character that does not fit */
        while (len > 0 && ((unsigned char)src[len] & 0xC0u) == 0x80u) {
            len--;
        }
    }
    for (size_t i = 0; i < len; i++) {
        dst[i] = src[i];
    }
    dst[len] = '\0';
}

/**
 * Give the HAL a chance to advance. Hardware: a no-op, events arrive by themselves from the radio worker. Host
 * simulator: moves the simulated bring-up and scan along (and delivers their events from the calling task). The runtime
 * calls it from the UI tick and the radio boot check; it must be cheap.
 */
void hal_bt_poll(void);

#ifdef __cplusplus
}
#endif

#endif // NEUPOD_HAL_RADIO_H
