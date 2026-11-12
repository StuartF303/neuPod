// SPDX-License-Identifier: MPL-2.0
/*
 * y2-a2dp: bring-up tool that streams audio to a Bluetooth headset (A2DP source) from the Innioasis Y2 testbed, using
 * BTstack over /dev/stpbt. Not product code. See README.md.
 *
 *   y2-a2dp scan
 *   y2-a2dp play [--addr XX:XX:XX:XX:XX:XX | --name SUBSTR | --last] [--wav FILE] [--seconds N]
 *   options for both: --hci-dump FILE (PacketLogger format), --find-timeout N
 *
 * The A2DP source / AVRCP target structure follows BTstack's example/a2dp_source_demo.c (BlueKitchen licence).
 */
#define _GNU_SOURCE

#include <errno.h>
#include <getopt.h>
#include <inttypes.h>
#include <math.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <time.h>

#include "btstack.h"
#include "btstack_run_loop_posix.h"
#include "btstack_tlv_posix.h"
#include "classic/btstack_link_key_db_tlv.h"
#include "hci_dump_posix_fs.h"
#include "hci_transport_h4.h"

#include "btstack_chipset_mtk_consys.h"
#include "btstack_uart_stpbt.h"

#define STPBT_DEVICE      "/dev/stpbt"
#define BT_ADDR_FILE      "/data/BT_Addr" // read only; never written
#define STATE_DIR         "/opt/neupod-bt"
#define TLV_PATH          STATE_DIR "/y2-a2dp.tlv"
#define TLV_TAG_LAST_DEV  0x59324c42u // 'Y2LB': address of the last headset we streamed to

#define DEFAULT_NAME        "AirPods"
#define DEFAULT_SECONDS     30
#define DEFAULT_FIND_SECS   60
#define SCAN_UNITS_1280MS   8 // 10.24 s
#define NAME_REQUEST_TIMEOUT_MS 6000
#define CONNECT_TIMEOUT_MS  30000
#define SHUTDOWN_WATCHDOG_MS 6000
#define TAIL_DRAIN_MS       1000

#define MAX_DEVICES 64

// exit codes
#define EXIT_USAGE      64
#define EXIT_NOT_FOUND  3
#define EXIT_TRANSPORT  4
#define EXIT_LINK       5

#define NUM_CHANNELS 2
#define AUDIO_TIMEOUT_MS 10
#define SBC_STORAGE_SIZE 1030
#define SINE_HZ 440.0
#define SINE_DBFS (-12.0)

// ---------------------------------------------------------------------------------------------------------------------
// logging

static struct timespec t_start;

static void logt(const char * fmt, ...) __attribute__((format(printf, 1, 2)));
static void logt(const char * fmt, ...){
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    long ms = (now.tv_sec - t_start.tv_sec) * 1000L + (now.tv_nsec - t_start.tv_nsec) / 1000000L;
    fprintf(stderr, "[%4ld.%03ld] ", ms / 1000, ms % 1000);
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
}

// ---------------------------------------------------------------------------------------------------------------------
// application state

typedef enum {
    APP_POWERING_ON,
    APP_SCANNING,
    APP_RESOLVING_NAMES,
    APP_CONNECTING,
    APP_CONFIGURING,
    APP_STREAMING,
    APP_CLOSING,
    APP_OFF,
} app_state_t;

static const char * const app_state_names[] = {
    "POWERING_ON", "SCANNING", "RESOLVING_NAMES", "CONNECTING", "CONFIGURING", "STREAMING", "CLOSING", "OFF",
};

static app_state_t app_state = APP_POWERING_ON;

static void set_state(app_state_t s){
    if (s == app_state) return;
    logt("state %s -> %s", app_state_names[app_state], app_state_names[s]);
    app_state = s;
}

typedef enum { CMD_SCAN, CMD_PLAY } command_t;

static struct {
    command_t   cmd;
    bool        have_addr;
    bd_addr_t   addr;
    const char *name;      // substring, case-insensitive
    bool        use_last;
    const char *wav_path;
    int         seconds;   // 0 = until the end of the WAV file
    int         find_secs;
    const char *hci_dump_path;
} opt;

typedef struct {
    bd_addr_t addr;
    uint32_t  cod;
    int       rssi;
    bool      rssi_valid;
    char      name[64];
    bool      name_valid;
    uint8_t   psrm;
    uint16_t  clock_offset;
    bool      name_tried;
} device_t;

static device_t devices[MAX_DEVICES];
static int      num_devices;

static bd_addr_t target_addr;
static bool      target_known;
static bool      inquiry_active;
static int       exit_code;
static bool      shutdown_requested;
static uint32_t  find_deadline_ms; // run loop time

static volatile sig_atomic_t signal_flag;
static btstack_timer_source_t signal_timer;
static btstack_timer_source_t phase_timer;     // find / connect / duration
static btstack_timer_source_t name_timer;      // one remote name request
static btstack_timer_source_t watchdog_timer;  // forced power-off while closing
static btstack_timer_source_t drain_timer;     // wait for the tail of the audio to play

static btstack_packet_callback_registration_t hci_event_callback_registration;
static btstack_tlv_posix_t tlv_context;
static const btstack_tlv_t * tlv_impl;

// ---------------------------------------------------------------------------------------------------------------------
// audio source: 440 Hz sine at -12 dBFS or a 16-bit 44.1 kHz stereo WAV

#define WAV_RATE 44100

static FILE *   wav_file;
static uint32_t wav_frames_left;
static bool     source_done;
static double   sine_phase;
static double   sine_amplitude;
static char     track_title[64] = "440 Hz test tone";

static bool wav_open(const char * path){
    FILE * f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "y2-a2dp: cannot open %s: %s\n", path, strerror(errno));
        return false;
    }
    uint8_t hdr[12];
    if (fread(hdr, 1, 12, f) != 12 || memcmp(hdr, "RIFF", 4) != 0 || memcmp(hdr + 8, "WAVE", 4) != 0) {
        fprintf(stderr, "y2-a2dp: %s is not a RIFF/WAVE file\n", path);
        fclose(f);
        return false;
    }
    bool have_fmt = false;
    for (;;) {
        uint8_t ch[8];
        if (fread(ch, 1, 8, f) != 8) {
            fprintf(stderr, "y2-a2dp: %s: no data chunk\n", path);
            fclose(f);
            return false;
        }
        uint32_t len = little_endian_read_32(ch, 4);
        if (memcmp(ch, "fmt ", 4) == 0) {
            uint8_t fmt[16];
            if (len < 16 || fread(fmt, 1, 16, f) != 16) {
                fprintf(stderr, "y2-a2dp: %s: bad fmt chunk\n", path);
                fclose(f);
                return false;
            }
            unsigned format = little_endian_read_16(fmt, 0), channels = little_endian_read_16(fmt, 2);
            uint32_t rate = little_endian_read_32(fmt, 4);
            unsigned bits = little_endian_read_16(fmt, 14);
            if (format != 1 || channels != 2 || rate != WAV_RATE || bits != 16) {
                fprintf(stderr, "y2-a2dp: %s must be 16-bit PCM, stereo, %d Hz (got format %u, %u ch, %" PRIu32 " Hz, %u bit)\n",
                        path, WAV_RATE, format, channels, rate, bits);
                fclose(f);
                return false;
            }
            have_fmt = true;
            if (fseek(f, (long) (len - 16 + (len & 1)), SEEK_CUR) != 0) {
                fclose(f);
                return false;
            }
        } else if (memcmp(ch, "data", 4) == 0) {
            if (!have_fmt) {
                fprintf(stderr, "y2-a2dp: %s: data chunk before fmt chunk\n", path);
                fclose(f);
                return false;
            }
            long start = ftell(f);
            long end = (fseek(f, 0, SEEK_END) == 0) ? ftell(f) : -1;
            if (start < 0 || end < start || fseek(f, start, SEEK_SET) != 0) {
                fclose(f);
                return false;
            }
            uint64_t avail = (uint64_t) (end - start);
            if (len > avail) len = (uint32_t) avail; // truncated or streamed header: play what is there
            wav_frames_left = len / 4;
            if (wav_frames_left == 0) {
                fprintf(stderr, "y2-a2dp: %s: no audio data\n", path);
                fclose(f);
                return false;
            }
            wav_file = f;
            const char * base = strrchr(path, '/');
            snprintf(track_title, sizeof(track_title), "%s", base ? base + 1 : path);
            return true;
        } else {
            if (fseek(f, (long) (len + (len & 1)), SEEK_CUR) != 0) {
                fclose(f);
                return false;
            }
        }
    }
}

static void produce_audio(int16_t * pcm, int num_frames){
    if (wav_file) {
        uint32_t want = (uint32_t) num_frames < wav_frames_left ? (uint32_t) num_frames : wav_frames_left;
        size_t got = want ? fread(pcm, 4, want, wav_file) : 0;
        if (got < (size_t) num_frames) {
            memset(&pcm[got * NUM_CHANNELS], 0, ((size_t) num_frames - got) * 4);
            if (!source_done) logt("audio: end of file");
            source_done = true;
        }
        wav_frames_left -= (uint32_t) got;
        if (wav_frames_left == 0) source_done = true;
        return;
    }
    const double step = 2.0 * M_PI * SINE_HZ / (double) WAV_RATE; // rate is fixed to 44.1 kHz at negotiation
    for (int i = 0; i < num_frames; i++) {
        int16_t v = (int16_t) lrint(sine_amplitude * sin(sine_phase));
        pcm[2 * i] = v;
        pcm[2 * i + 1] = v;
        sine_phase += step;
        if (sine_phase > 2.0 * M_PI) sine_phase -= 2.0 * M_PI;
    }
}

// ---------------------------------------------------------------------------------------------------------------------
// A2DP source (adapted from a2dp_source_demo.c)

typedef struct {
    uint16_t a2dp_cid;
    uint8_t  local_seid;
    uint8_t  remote_seid;
    uint8_t  stream_opened;
    uint16_t avrcp_cid;

    uint32_t time_audio_data_sent; // ms
    uint32_t acc_num_missed_samples;
    uint32_t samples_ready;
    btstack_timer_source_t audio_timer;
    uint8_t  streaming;
    int      max_media_payload_size;
    uint32_t rtp_timestamp;

    uint8_t  sbc_storage[SBC_STORAGE_SIZE];
    uint16_t sbc_storage_count;
    uint8_t  sbc_ready_to_send;
} media_context_t;

// 44.1 kHz only (the WAV and the sine are generated at that rate), stereo and joint stereo.
static uint8_t media_sbc_codec_capabilities[] = {
    (AVDTP_SBC_44100 << 4) | AVDTP_SBC_STEREO | AVDTP_SBC_JOINT_STEREO,
    0xFF,
    2, 53
};

typedef struct {
    int num_channels;
    int sampling_frequency;
    int block_length;
    int subbands;
    int min_bitpool_value;
    int max_bitpool_value;
    btstack_sbc_channel_mode_t      channel_mode;
    btstack_sbc_allocation_method_t allocation_method;
} sbc_configuration_t;

static media_context_t media;
static sbc_configuration_t sbc_configuration;
static const btstack_sbc_encoder_t * sbc_encoder_instance;
static btstack_sbc_encoder_bluedroid_t sbc_encoder_state;
static uint8_t media_sbc_codec_configuration[4];
static int current_sample_rate = WAV_RATE;

static uint8_t sdp_a2dp_source_service_buffer[150];
static uint8_t sdp_avrcp_target_service_buffer[200];
static uint8_t sdp_avrcp_controller_service_buffer[200];
static uint8_t device_id_sdp_service_buffer[100];

static avrcp_track_t track = {
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01}, 1, track_title, "y2-a2dp", "neuPod testbed", "test", 0, 0,
};
static avrcp_playback_status_t playback_status = AVRCP_PLAYBACK_STATUS_STOPPED;

static void request_shutdown(const char * reason, int code);
static void power_off(void);

static void a2dp_send_media_packet(void){
    int num_bytes_in_frame = sbc_encoder_instance->sbc_buffer_length(&sbc_encoder_state);
    int bytes_in_storage = media.sbc_storage_count;
    uint8_t num_sbc_frames = (uint8_t) (bytes_in_storage / num_bytes_in_frame);
    media.sbc_storage[0] = num_sbc_frames; // no fragmentation
    a2dp_source_stream_send_media_payload_rtp(media.a2dp_cid, media.local_seid, 0, media.rtp_timestamp,
                                              media.sbc_storage, (uint16_t) (bytes_in_storage + 1));
    unsigned int samples_per_frame = sbc_encoder_instance->num_audio_frames(&sbc_encoder_state);
    media.rtp_timestamp += num_sbc_frames * samples_per_frame;
    media.sbc_storage_count = 0;
    media.sbc_ready_to_send = 0;
}

static void a2dp_fill_sbc_audio_buffer(media_context_t * context){
    unsigned int samples_per_frame = sbc_encoder_instance->num_audio_frames(&sbc_encoder_state);
    uint16_t sbc_frame_len = sbc_encoder_instance->sbc_buffer_length(&sbc_encoder_state);
    while (!source_done
        && context->samples_ready >= samples_per_frame
        && (context->max_media_payload_size - context->sbc_storage_count) >= sbc_frame_len) {
        int16_t pcm_frame[256 * NUM_CHANNELS];
        produce_audio(pcm_frame, (int) samples_per_frame);
        // first byte of the storage is the SBC payload header
        sbc_encoder_instance->encode_signed_16(&sbc_encoder_state, pcm_frame, &context->sbc_storage[1 + context->sbc_storage_count]);
        context->sbc_storage_count = (uint16_t) (context->sbc_storage_count + sbc_frame_len);
        context->samples_ready -= samples_per_frame;
    }
}

static void drain_timer_handler(btstack_timer_source_t * timer){
    (void) timer;
    request_shutdown("end of audio", 0);
}

static void a2dp_audio_timeout_handler(btstack_timer_source_t * timer){
    media_context_t * context = (media_context_t *) btstack_run_loop_get_timer_context(timer);
    btstack_run_loop_set_timer(&context->audio_timer, AUDIO_TIMEOUT_MS);
    btstack_run_loop_add_timer(&context->audio_timer);
    uint32_t now = btstack_run_loop_get_time_ms();

    uint32_t update_period_ms = AUDIO_TIMEOUT_MS;
    if (context->time_audio_data_sent > 0) update_period_ms = now - context->time_audio_data_sent;

    uint32_t num_samples = (update_period_ms * (uint32_t) current_sample_rate) / 1000;
    context->acc_num_missed_samples += (update_period_ms * (uint32_t) current_sample_rate) % 1000;
    while (context->acc_num_missed_samples >= 1000) {
        num_samples++;
        context->acc_num_missed_samples -= 1000;
    }
    context->time_audio_data_sent = now;
    context->samples_ready += num_samples;

    if (context->sbc_ready_to_send) return;

    a2dp_fill_sbc_audio_buffer(context);

    bool full = (context->sbc_storage_count + sbc_encoder_instance->sbc_buffer_length(&sbc_encoder_state)) > context->max_media_payload_size;
    // at the end of the file flush the partial packet, then give the headset time to play out what it has buffered
    bool flush = source_done && context->sbc_storage_count > 0;
    if (full || flush) {
        context->sbc_ready_to_send = 1;
        a2dp_source_stream_endpoint_request_can_send_now(context->a2dp_cid, context->local_seid);
    }
    if (source_done && !context->sbc_ready_to_send && context->sbc_storage_count == 0) {
        btstack_run_loop_set_timer_handler(&drain_timer, drain_timer_handler);
        btstack_run_loop_set_timer(&drain_timer, TAIL_DRAIN_MS);
        btstack_run_loop_add_timer(&drain_timer);
        btstack_run_loop_remove_timer(&context->audio_timer); // nothing more to produce
    }
}

static void a2dp_timer_start(media_context_t * context){
    context->max_media_payload_size = btstack_min(a2dp_max_media_payload_size(context->a2dp_cid, context->local_seid), SBC_STORAGE_SIZE);
    context->sbc_storage_count = 0;
    context->sbc_ready_to_send = 0;
    context->streaming = 1;
    btstack_run_loop_remove_timer(&context->audio_timer);
    btstack_run_loop_set_timer_handler(&context->audio_timer, a2dp_audio_timeout_handler);
    btstack_run_loop_set_timer_context(&context->audio_timer, context);
    btstack_run_loop_set_timer(&context->audio_timer, AUDIO_TIMEOUT_MS);
    btstack_run_loop_add_timer(&context->audio_timer);
}

static void a2dp_timer_stop(media_context_t * context){
    context->time_audio_data_sent = 0;
    context->acc_num_missed_samples = 0;
    context->samples_ready = 0;
    context->streaming = 0;
    context->sbc_storage_count = 0;
    context->sbc_ready_to_send = 0;
    btstack_run_loop_remove_timer(&context->audio_timer);
}

static void duration_timer_handler(btstack_timer_source_t * timer){
    (void) timer;
    request_shutdown("requested duration reached", 0);
}

static void connect_timeout_handler(btstack_timer_source_t * timer){
    (void) timer;
    request_shutdown("no A2DP stream within the connect timeout", EXIT_LINK);
}

static void a2dp_source_packet_handler(uint8_t packet_type, uint16_t channel, uint8_t * packet, uint16_t size){
    (void) channel;
    (void) size;
    uint8_t status;
    uint8_t local_seid;
    bd_addr_t address;
    uint16_t cid;

    if (packet_type != HCI_EVENT_PACKET) return;
    if (hci_event_packet_get_type(packet) != HCI_EVENT_A2DP_META) return;

    switch (hci_event_a2dp_meta_get_subevent_code(packet)) {
        case A2DP_SUBEVENT_SIGNALING_CONNECTION_ESTABLISHED:
            a2dp_subevent_signaling_connection_established_get_bd_addr(packet, address);
            cid = a2dp_subevent_signaling_connection_established_get_a2dp_cid(packet);
            status = a2dp_subevent_signaling_connection_established_get_status(packet);
            if (status != ERROR_CODE_SUCCESS) {
                logt("a2dp: signalling connection failed, status 0x%02x", status);
                media.a2dp_cid = 0;
                if (shutdown_requested) power_off(); // our own disconnect before the link was up
                else request_shutdown("A2DP connection failed", EXIT_LINK);
                break;
            }
            media.a2dp_cid = cid;
            if (shutdown_requested) {
                a2dp_source_disconnect(cid);
                break;
            }
            logt("a2dp: signalling connected to %s, cid 0x%02x, local seid %u", bd_addr_to_str(address), cid, media.local_seid);
            set_state(APP_CONFIGURING);
            break;

        case A2DP_SUBEVENT_SIGNALING_MEDIA_CODEC_SBC_CONFIGURATION: {
            cid = a2dp_subevent_signaling_media_codec_sbc_configuration_get_a2dp_cid(packet);
            if (cid != media.a2dp_cid) return;
            media.remote_seid = a2dp_subevent_signaling_media_codec_sbc_configuration_get_remote_seid(packet);

            sbc_configuration.num_channels = a2dp_subevent_signaling_media_codec_sbc_configuration_get_num_channels(packet);
            sbc_configuration.sampling_frequency = a2dp_subevent_signaling_media_codec_sbc_configuration_get_sampling_frequency(packet);
            sbc_configuration.block_length = a2dp_subevent_signaling_media_codec_sbc_configuration_get_block_length(packet);
            sbc_configuration.subbands = a2dp_subevent_signaling_media_codec_sbc_configuration_get_subbands(packet);
            sbc_configuration.min_bitpool_value = a2dp_subevent_signaling_media_codec_sbc_configuration_get_min_bitpool_value(packet);
            sbc_configuration.max_bitpool_value = a2dp_subevent_signaling_media_codec_sbc_configuration_get_max_bitpool_value(packet);
            avdtp_channel_mode_t channel_mode = (avdtp_channel_mode_t) a2dp_subevent_signaling_media_codec_sbc_configuration_get_channel_mode(packet);
            uint8_t allocation_method = a2dp_subevent_signaling_media_codec_sbc_configuration_get_allocation_method(packet);
            // Adapt the Bluetooth spec definition to what the SBC encoder expects
            sbc_configuration.allocation_method = (btstack_sbc_allocation_method_t) (allocation_method - 1);
            switch (channel_mode) {
                case AVDTP_CHANNEL_MODE_JOINT_STEREO: sbc_configuration.channel_mode = SBC_CHANNEL_MODE_JOINT_STEREO; break;
                case AVDTP_CHANNEL_MODE_STEREO:       sbc_configuration.channel_mode = SBC_CHANNEL_MODE_STEREO; break;
                case AVDTP_CHANNEL_MODE_DUAL_CHANNEL: sbc_configuration.channel_mode = SBC_CHANNEL_MODE_DUAL_CHANNEL; break;
                case AVDTP_CHANNEL_MODE_MONO:         sbc_configuration.channel_mode = SBC_CHANNEL_MODE_MONO; break;
                default:
                    request_shutdown("unsupported SBC channel mode", EXIT_LINK);
                    return;
            }
            logt("a2dp: SBC configuration %d Hz, mode %d, blocks %d, subbands %d, bitpool %d..%d, remote seid %u",
                 sbc_configuration.sampling_frequency, (int) channel_mode, sbc_configuration.block_length, sbc_configuration.subbands,
                 sbc_configuration.min_bitpool_value, sbc_configuration.max_bitpool_value, media.remote_seid);
            if (sbc_configuration.sampling_frequency != WAV_RATE) {
                request_shutdown("headset configured a sample rate other than 44.1 kHz", EXIT_LINK);
                return;
            }
            current_sample_rate = sbc_configuration.sampling_frequency;
            sbc_encoder_instance = btstack_sbc_encoder_bluedroid_init_instance(&sbc_encoder_state);
            sbc_encoder_instance->configure(&sbc_encoder_state, SBC_MODE_STANDARD,
                                            sbc_configuration.block_length, sbc_configuration.subbands,
                                            sbc_configuration.allocation_method, sbc_configuration.sampling_frequency,
                                            sbc_configuration.max_bitpool_value, sbc_configuration.channel_mode);
            break;
        }

        case A2DP_SUBEVENT_SIGNALING_DELAY_REPORT:
            logt("a2dp: delay report %d.%d ms", a2dp_subevent_signaling_delay_report_get_delay_100us(packet) / 10,
                 a2dp_subevent_signaling_delay_report_get_delay_100us(packet) % 10);
            break;

        case A2DP_SUBEVENT_SIGNALING_MEDIA_CODEC_SBC_CAPABILITY:
            logt("a2dp: remote SBC endpoint, seid %u", a2dp_subevent_signaling_media_codec_sbc_capability_get_remote_seid(packet));
            break;

        case A2DP_SUBEVENT_SIGNALING_CAPABILITIES_COMPLETE:
            logt("a2dp: remote capabilities queried");
            break;

        case A2DP_SUBEVENT_STREAM_ESTABLISHED:
            a2dp_subevent_stream_established_get_bd_addr(packet, address);
            status = a2dp_subevent_stream_established_get_status(packet);
            if (status != ERROR_CODE_SUCCESS) {
                logt("a2dp: stream failed, status 0x%02x", status);
                request_shutdown("A2DP stream setup failed", EXIT_LINK);
                break;
            }
            logt("a2dp: stream open, cid 0x%02x", a2dp_subevent_stream_established_get_a2dp_cid(packet));
            media.stream_opened = 1;
            if (tlv_impl) tlv_impl->store_tag(&tlv_context, TLV_TAG_LAST_DEV, address, sizeof(bd_addr_t));
            if (shutdown_requested) break;
            status = a2dp_source_start_stream(media.a2dp_cid, media.local_seid);
            if (status != ERROR_CODE_SUCCESS) logt("a2dp: start stream failed, status 0x%02x", status);
            break;

        case A2DP_SUBEVENT_STREAM_STARTED:
            local_seid = a2dp_subevent_stream_started_get_local_seid(packet);
            playback_status = AVRCP_PLAYBACK_STATUS_PLAYING;
            if (media.avrcp_cid) {
                avrcp_target_set_now_playing_info(media.avrcp_cid, &track, 1);
                avrcp_target_set_playback_status(media.avrcp_cid, AVRCP_PLAYBACK_STATUS_PLAYING);
            }
            a2dp_timer_start(&media);
            set_state(APP_STREAMING);
            logt("a2dp: stream started, local seid %u, max payload %d", local_seid, media.max_media_payload_size);
            btstack_run_loop_remove_timer(&phase_timer);
            if (opt.seconds > 0) {
                btstack_run_loop_set_timer_handler(&phase_timer, duration_timer_handler);
                btstack_run_loop_set_timer(&phase_timer, (uint32_t) opt.seconds * 1000u);
                btstack_run_loop_add_timer(&phase_timer);
            }
            break;

        case A2DP_SUBEVENT_STREAMING_CAN_SEND_MEDIA_PACKET_NOW:
            if (media.streaming && media.sbc_storage_count > 0) a2dp_send_media_packet();
            break;

        case A2DP_SUBEVENT_STREAM_SUSPENDED:
            playback_status = AVRCP_PLAYBACK_STATUS_PAUSED;
            if (media.avrcp_cid) avrcp_target_set_playback_status(media.avrcp_cid, AVRCP_PLAYBACK_STATUS_PAUSED);
            logt("a2dp: stream suspended");
            a2dp_timer_stop(&media);
            break;

        case A2DP_SUBEVENT_STREAM_RELEASED:
            playback_status = AVRCP_PLAYBACK_STATUS_STOPPED;
            cid = a2dp_subevent_stream_released_get_a2dp_cid(packet);
            logt("a2dp: stream released, cid 0x%02x", cid);
            if (cid == media.a2dp_cid) media.stream_opened = 0;
            if (media.avrcp_cid) {
                avrcp_target_set_now_playing_info(media.avrcp_cid, NULL, 1);
                avrcp_target_set_playback_status(media.avrcp_cid, AVRCP_PLAYBACK_STATUS_STOPPED);
            }
            a2dp_timer_stop(&media);
            if (shutdown_requested && media.a2dp_cid) a2dp_source_disconnect(media.a2dp_cid);
            break;

        case A2DP_SUBEVENT_SIGNALING_CONNECTION_RELEASED:
            cid = a2dp_subevent_signaling_connection_released_get_a2dp_cid(packet);
            logt("a2dp: signalling released, cid 0x%02x", cid);
            if (cid == media.a2dp_cid) {
                media.avrcp_cid = 0;
                media.a2dp_cid = 0;
                media.stream_opened = 0;
                a2dp_timer_stop(&media);
                if (!shutdown_requested) request_shutdown("headset dropped the A2DP connection", EXIT_LINK);
                else power_off();
            }
            break;

        default:
            break;
    }
}

static void avrcp_packet_handler(uint8_t packet_type, uint16_t channel, uint8_t * packet, uint16_t size){
    (void) channel;
    (void) size;
    bd_addr_t event_addr;
    uint16_t local_cid;
    uint8_t status;

    if (packet_type != HCI_EVENT_PACKET) return;
    if (hci_event_packet_get_type(packet) != HCI_EVENT_AVRCP_META) return;

    switch (packet[2]) {
        case AVRCP_SUBEVENT_CONNECTION_ESTABLISHED:
            local_cid = avrcp_subevent_connection_established_get_avrcp_cid(packet);
            status = avrcp_subevent_connection_established_get_status(packet);
            if (status != ERROR_CODE_SUCCESS) {
                logt("avrcp: connection failed, status 0x%02x", status);
                return;
            }
            media.avrcp_cid = local_cid;
            avrcp_subevent_connection_established_get_bd_addr(packet, event_addr);
            logt("avrcp: connected to %s, cid 0x%02x", bd_addr_to_str(event_addr), local_cid);
            avrcp_target_support_event(local_cid, AVRCP_NOTIFICATION_EVENT_PLAYBACK_STATUS_CHANGED);
            avrcp_target_support_event(local_cid, AVRCP_NOTIFICATION_EVENT_TRACK_CHANGED);
            avrcp_target_support_event(local_cid, AVRCP_NOTIFICATION_EVENT_NOW_PLAYING_CONTENT_CHANGED);
            avrcp_target_set_now_playing_info(local_cid, NULL, 1);
            avrcp_controller_enable_notification(local_cid, AVRCP_NOTIFICATION_EVENT_VOLUME_CHANGED);
            break;
        case AVRCP_SUBEVENT_CONNECTION_RELEASED:
            logt("avrcp: disconnected, cid 0x%02x", avrcp_subevent_connection_released_get_avrcp_cid(packet));
            media.avrcp_cid = 0;
            break;
        default:
            break;
    }
}

static void avrcp_target_packet_handler(uint8_t packet_type, uint16_t channel, uint8_t * packet, uint16_t size){
    (void) channel;
    (void) size;
    uint8_t status = ERROR_CODE_SUCCESS;

    if (packet_type != HCI_EVENT_PACKET) return;
    if (hci_event_packet_get_type(packet) != HCI_EVENT_AVRCP_META) return;

    switch (packet[2]) {
        case AVRCP_SUBEVENT_PLAY_STATUS_QUERY:
            status = avrcp_target_play_status(media.avrcp_cid, track.song_length_ms, track.song_position_ms, playback_status);
            break;
        case AVRCP_SUBEVENT_OPERATION: {
            avrcp_operation_id_t op = (avrcp_operation_id_t) avrcp_subevent_operation_get_operation_id(packet);
            bool pressed = avrcp_subevent_operation_get_button_pressed(packet) > 0;
            logt("avrcp: operation %s (%s)", avrcp_operation2str(op), pressed ? "press" : "release");
            if (!pressed || shutdown_requested) break;
            switch (op) {
                case AVRCP_OPERATION_ID_PLAY:  status = a2dp_source_start_stream(media.a2dp_cid, media.local_seid); break;
                case AVRCP_OPERATION_ID_PAUSE: status = a2dp_source_pause_stream(media.a2dp_cid, media.local_seid); break;
                case AVRCP_OPERATION_ID_STOP:  status = a2dp_source_stop_stream(media.a2dp_cid, media.local_seid); break;
                default: break;
            }
            break;
        }
        default:
            break;
    }
    if (status != ERROR_CODE_SUCCESS) logt("avrcp: could not answer event 0x%02x, status 0x%02x", packet[2], status);
}

static void avrcp_controller_packet_handler(uint8_t packet_type, uint16_t channel, uint8_t * packet, uint16_t size){
    (void) channel;
    (void) size;
    if (packet_type != HCI_EVENT_PACKET) return;
    if (hci_event_packet_get_type(packet) != HCI_EVENT_AVRCP_META) return;
    if (!media.avrcp_cid) return;
    if (packet[2] == AVRCP_SUBEVENT_NOTIFICATION_VOLUME_CHANGED) {
        logt("avrcp: headset volume %d%%", avrcp_subevent_notification_volume_changed_get_absolute_volume(packet) * 100 / 127);
    }
}

// ---------------------------------------------------------------------------------------------------------------------
// device table, discovery, connect

static device_t * find_device(const bd_addr_t addr){
    for (int i = 0; i < num_devices; i++) {
        if (bd_addr_cmp(devices[i].addr, addr) == 0) return &devices[i];
    }
    return NULL;
}

static bool name_matches(const char * name){
    return opt.name && strcasestr(name, opt.name) != NULL;
}

static void start_inquiry(void){
    inquiry_active = true;
    set_state(APP_SCANNING);
    uint8_t status = gap_inquiry_start(SCAN_UNITS_1280MS);
    if (status != ERROR_CODE_SUCCESS) {
        inquiry_active = false;
        logt("inquiry start failed, status 0x%02x", status);
        request_shutdown("could not start inquiry", EXIT_LINK);
    }
}

static void connect_to(const bd_addr_t addr){
    memcpy(target_addr, addr, sizeof(bd_addr_t));
    target_known = true;
    set_state(APP_CONNECTING);
    logt("connecting A2DP to %s (pairs first if there is no link key)", bd_addr_to_str(target_addr));
    btstack_run_loop_remove_timer(&phase_timer);
    btstack_run_loop_set_timer_handler(&phase_timer, connect_timeout_handler);
    btstack_run_loop_set_timer(&phase_timer, CONNECT_TIMEOUT_MS);
    btstack_run_loop_add_timer(&phase_timer);
    uint8_t status = a2dp_source_establish_stream(target_addr, &media.a2dp_cid);
    if (status != ERROR_CODE_SUCCESS) {
        logt("a2dp_source_establish_stream failed, status 0x%02x", status);
        request_shutdown("could not start the A2DP connection", EXIT_LINK);
    }
}

static const device_t * match_in_table(void){
    for (int i = 0; i < num_devices; i++) {
        if (devices[i].name_valid && name_matches(devices[i].name)) return &devices[i];
    }
    return NULL;
}

static void print_scan_table(void){
    printf("%-17s  %5s  %6s  %s\n", "ADDRESS", "RSSI", "CLASS", "NAME");
    for (int i = 0; i < num_devices; i++) {
        const device_t * d = &devices[i];
        char rssi[8] = "-";
        if (d->rssi_valid) snprintf(rssi, sizeof(rssi), "%d", d->rssi);
        printf("%-17s  %5s  %06" PRIx32 "  %s\n", bd_addr_to_str(d->addr), rssi, d->cod, d->name_valid ? d->name : "(no name)");
    }
    printf("%d device(s)\n", num_devices);
    fflush(stdout);
}

static void name_timeout_handler(btstack_timer_source_t * timer);

// Ask for the next name we do not have yet; when none is left, finish the scan or decide what to connect to.
static void resolve_next_name(void){
    for (int i = 0; i < num_devices; i++) {
        device_t * d = &devices[i];
        if (d->name_valid || d->name_tried) continue;
        d->name_tried = true;
        set_state(APP_RESOLVING_NAMES);
        logt("name request to %s", bd_addr_to_str(d->addr));
        uint8_t status = gap_remote_name_request(d->addr, d->psrm, (uint16_t) (d->clock_offset | 0x8000));
        if (status != ERROR_CODE_SUCCESS) {
            logt("name request refused, status 0x%02x", status);
            continue;
        }
        btstack_run_loop_set_timer_handler(&name_timer, name_timeout_handler);
        btstack_run_loop_set_timer(&name_timer, NAME_REQUEST_TIMEOUT_MS);
        btstack_run_loop_add_timer(&name_timer);
        return;
    }
    // all names resolved (or not resolvable)
    if (opt.cmd == CMD_SCAN) {
        print_scan_table();
        request_shutdown("scan complete", 0);
        return;
    }
    const device_t * m = match_in_table();
    if (m) {
        logt("match: %s \"%s\"", bd_addr_to_str(m->addr), m->name);
        connect_to(m->addr);
        return;
    }
    if (btstack_run_loop_get_time_ms() >= find_deadline_ms) {
        logt("no device with \"%s\" in its name was found", opt.name);
        request_shutdown("device not found", EXIT_NOT_FOUND);
        return;
    }
    logt("no match yet, scanning again");
    start_inquiry();
}

static void name_timeout_handler(btstack_timer_source_t * timer){
    (void) timer;
    logt("name request timed out");
    resolve_next_name();
}

static bool last_device(bd_addr_t out){
    if (!tlv_impl) return false;
    int n = tlv_impl->get_tag(&tlv_context, TLV_TAG_LAST_DEV, out, sizeof(bd_addr_t));
    return n == (int) sizeof(bd_addr_t);
}

static void on_working(void){
    bd_addr_t local;
    gap_local_bd_addr(local);
    logt("controller up, local address %s, secure connections %s", bd_addr_to_str(local),
         gap_secure_connections_active() ? "active" : "not available (controller is Bluetooth 4.0)");

    if (shutdown_requested) { power_off(); return; }

    if (opt.cmd == CMD_SCAN) {
        start_inquiry();
        return;
    }
    if (opt.have_addr) {
        connect_to(opt.addr);
        return;
    }
    if (opt.use_last) {
        bd_addr_t a;
        if (!last_device(a)) {
            logt("--last: no headset has been streamed to yet");
            request_shutdown("no last device", EXIT_NOT_FOUND);
            return;
        }
        connect_to(a);
        return;
    }
    // name search; a bonded headset that is not in pairing mode will not answer an inquiry (use --addr or --last for it)
    find_deadline_ms = btstack_run_loop_get_time_ms() + (uint32_t) opt.find_secs * 1000u;
    logt("searching for a device named like \"%s\" (up to %d s)", opt.name, opt.find_secs);
    start_inquiry();
}

static void hci_packet_handler(uint8_t packet_type, uint16_t channel, uint8_t * packet, uint16_t size){
    (void) channel;
    (void) size;
    if (packet_type != HCI_EVENT_PACKET) return;
    bd_addr_t address;

    switch (hci_event_packet_get_type(packet)) {
        case BTSTACK_EVENT_POWERON_FAILED:
            logt("HCI power-on failed (is /dev/stpbt present and the chip powered by the stock loader?)");
            hci_dump_posix_fs_close();
            exit(2);
            break;

        case BTSTACK_EVENT_STATE:
            switch (btstack_event_state_get_state(packet)) {
                case HCI_STATE_WORKING:
                    on_working();
                    break;
                case HCI_STATE_OFF:
                    logt("controller off");
                    set_state(APP_OFF);
                    btstack_tlv_posix_deinit(&tlv_context);
                    hci_dump_posix_fs_close();
                    exit(exit_code);
                    break;
                default:
                    break;
            }
            break;

        case HCI_EVENT_PIN_CODE_REQUEST:
            logt("pairing: legacy PIN requested, refusing");
            hci_event_pin_code_request_get_bd_addr(packet, address);
            gap_pin_code_negative(address);
            break;

        case HCI_EVENT_USER_CONFIRMATION_REQUEST:
            hci_event_user_confirmation_request_get_bd_addr(packet, address);
            logt("pairing: confirmation request from %s (just works), accepting", bd_addr_to_str(address));
            gap_ssp_confirmation_response(address);
            break;

        case HCI_EVENT_LINK_KEY_NOTIFICATION:
            reverse_bd_addr(&packet[2], address); // event: bd_addr (LSB first), link key, key type
            logt("pairing: link key stored for %s", bd_addr_to_str(address)); // the key itself is never logged
            break;

        case HCI_EVENT_AUTHENTICATION_COMPLETE_EVENT:
            logt("pairing: authentication complete, status 0x%02x", hci_event_authentication_complete_get_status(packet));
            break;

        case HCI_EVENT_CONNECTION_COMPLETE:
            hci_event_connection_complete_get_bd_addr(packet, address);
            logt("acl: connection to %s, status 0x%02x", bd_addr_to_str(address), hci_event_connection_complete_get_status(packet));
            break;

        case HCI_EVENT_DISCONNECTION_COMPLETE:
            logt("acl: disconnected, handle 0x%04x, reason 0x%02x",
                 hci_event_disconnection_complete_get_connection_handle(packet), hci_event_disconnection_complete_get_reason(packet));
            break;

        case GAP_EVENT_INQUIRY_RESULT: {
            gap_event_inquiry_result_get_bd_addr(packet, address);
            device_t * d = find_device(address);
            if (!d) {
                if (num_devices >= MAX_DEVICES) break;
                d = &devices[num_devices++];
                memset(d, 0, sizeof(*d));
                memcpy(d->addr, address, sizeof(bd_addr_t));
            }
            d->cod = gap_event_inquiry_result_get_class_of_device(packet);
            d->psrm = gap_event_inquiry_result_get_page_scan_repetition_mode(packet);
            d->clock_offset = gap_event_inquiry_result_get_clock_offset(packet);
            if (gap_event_inquiry_result_get_rssi_available(packet)) {
                d->rssi = (int8_t) gap_event_inquiry_result_get_rssi(packet);
                d->rssi_valid = true;
            }
            if (gap_event_inquiry_result_get_name_available(packet)) {
                int len = gap_event_inquiry_result_get_name_len(packet);
                if (len >= (int) sizeof(d->name)) len = (int) sizeof(d->name) - 1;
                memcpy(d->name, gap_event_inquiry_result_get_name(packet), (size_t) len);
                d->name[len] = 0;
                d->name_valid = true;
            }
            logt("inquiry: %s class %06" PRIx32 " rssi %d name \"%s\"", bd_addr_to_str(d->addr), d->cod,
                 d->rssi_valid ? d->rssi : 0, d->name_valid ? d->name : "?");
            if (opt.cmd == CMD_PLAY && inquiry_active && d->name_valid && name_matches(d->name)) {
                logt("match: %s \"%s\"", bd_addr_to_str(d->addr), d->name);
                inquiry_active = false;
                gap_inquiry_stop();
                connect_to(d->addr);
            }
            break;
        }

        case GAP_EVENT_INQUIRY_COMPLETE:
            if (!inquiry_active) break; // we stopped it ourselves
            inquiry_active = false;
            logt("inquiry complete, %d device(s)", num_devices);
            resolve_next_name();
            break;

        case HCI_EVENT_REMOTE_NAME_REQUEST_COMPLETE: {
            if (app_state != APP_RESOLVING_NAMES) break;
            btstack_run_loop_remove_timer(&name_timer);
            hci_event_remote_name_request_complete_get_bd_addr(packet, address);
            device_t * d = find_device(address);
            uint8_t status = hci_event_remote_name_request_complete_get_status(packet);
            if (d && status == ERROR_CODE_SUCCESS) {
                snprintf(d->name, sizeof(d->name), "%s", hci_event_remote_name_request_complete_get_remote_name(packet));
                d->name_valid = true;
                logt("name: %s \"%s\"", bd_addr_to_str(address), d->name);
            } else {
                logt("name request to %s failed, status 0x%02x", bd_addr_to_str(address), status);
            }
            resolve_next_name();
            break;
        }

        default:
            break;
    }
}

// ---------------------------------------------------------------------------------------------------------------------
// shutdown

static bool powering_off;

static void power_off(void){
    if (powering_off) return;
    powering_off = true;
    set_state(APP_CLOSING);
    btstack_run_loop_remove_timer(&watchdog_timer);
    btstack_run_loop_remove_timer(&phase_timer);
    btstack_run_loop_remove_timer(&name_timer);
    btstack_run_loop_remove_timer(&drain_timer);
    logt("powering the controller off");
    hci_power_control(HCI_POWER_OFF);
}

static void watchdog_handler(btstack_timer_source_t * timer){
    (void) timer;
    logt("clean disconnect timed out, powering off anyway");
    power_off();
}

static void request_shutdown(const char * reason, int code){
    if (shutdown_requested) return;
    shutdown_requested = true;
    exit_code = code;
    logt("shutting down: %s (exit code %d)", reason, code);
    set_state(APP_CLOSING);
    btstack_run_loop_remove_timer(&phase_timer);
    btstack_run_loop_remove_timer(&name_timer);
    btstack_run_loop_remove_timer(&drain_timer);
    a2dp_timer_stop(&media);

    btstack_run_loop_set_timer_handler(&watchdog_timer, watchdog_handler);
    btstack_run_loop_set_timer(&watchdog_timer, SHUTDOWN_WATCHDOG_MS);
    btstack_run_loop_add_timer(&watchdog_timer);

    if (inquiry_active) {
        inquiry_active = false;
        gap_inquiry_stop();
    }
    if (hci_get_state() != HCI_STATE_WORKING) {
        // still powering on (or already off): no links to close
        if (hci_get_state() == HCI_STATE_OFF) { hci_dump_posix_fs_close(); exit(exit_code); }
        power_off();
        return;
    }
    if (media.a2dp_cid) {
        // close the stream first so the headset sees an orderly teardown; STREAM_RELEASED then disconnects
        if (media.stream_opened && a2dp_source_stop_stream(media.a2dp_cid, media.local_seid) == ERROR_CODE_SUCCESS) return;
        if (a2dp_source_disconnect(media.a2dp_cid) == ERROR_CODE_SUCCESS) return;
    }
    power_off();
}

static void signal_handler(int sig){
    (void) sig;
    signal_flag = 1;
}

static void signal_poll(btstack_timer_source_t * timer){
    if (signal_flag) {
        signal_flag = 0;
        request_shutdown("interrupted", 130);
    }
    btstack_run_loop_set_timer(timer, 100);
    btstack_run_loop_add_timer(timer);
}

static void transport_error(const char * message){
    logt("transport error: %s (the chip may have reset; power-cycle it with the stock loader and retry)", message);
    hci_dump_posix_fs_close();
    exit(EXIT_TRANSPORT);
}

// ---------------------------------------------------------------------------------------------------------------------
// setup

static bool read_local_bd_addr(bd_addr_t out){
    FILE * f = fopen(BT_ADDR_FILE, "rb");
    if (!f) {
        fprintf(stderr, "y2-a2dp: cannot read %s: %s\n", BT_ADDR_FILE, strerror(errno));
        return false;
    }
    uint8_t buf[6];
    size_t n = fread(buf, 1, sizeof(buf), f);
    fclose(f);
    if (n != sizeof(buf)) {
        fprintf(stderr, "y2-a2dp: %s is shorter than 6 bytes\n", BT_ADDR_FILE);
        return false;
    }
    memcpy(out, buf, sizeof(bd_addr_t)); // the file holds the address MSB first, as BTstack does
    if (btstack_is_null_bd_addr(out)) {
        fprintf(stderr, "y2-a2dp: %s holds an all-zero address\n", BT_ADDR_FILE);
        return false;
    }
    return true;
}

static int services_init(void){
    // Ask to be master when a headset reconnects, so the headset stays the slave
    hci_set_master_slave_policy(0);
    hci_set_inquiry_mode(INQUIRY_MODE_RSSI_AND_EIR);

    l2cap_init();

    a2dp_source_init();
    a2dp_source_register_packet_handler(&a2dp_source_packet_handler);
    avdtp_stream_endpoint_t * ep = a2dp_source_create_stream_endpoint(AVDTP_AUDIO, AVDTP_CODEC_SBC,
        media_sbc_codec_capabilities, sizeof(media_sbc_codec_capabilities),
        media_sbc_codec_configuration, sizeof(media_sbc_codec_configuration));
    if (!ep) {
        fprintf(stderr, "y2-a2dp: not enough memory for the stream endpoint\n");
        return 1;
    }
    avdtp_set_preferred_sampling_frequency(ep, WAV_RATE);
    media.local_seid = avdtp_local_seid(ep);
    avdtp_source_register_delay_reporting_category(media.local_seid);

    avrcp_init();
    avrcp_register_packet_handler(&avrcp_packet_handler);
    avrcp_target_init();
    avrcp_target_register_packet_handler(&avrcp_target_packet_handler);
    avrcp_controller_init();
    avrcp_controller_register_packet_handler(&avrcp_controller_packet_handler);

    sdp_init();
    memset(sdp_a2dp_source_service_buffer, 0, sizeof(sdp_a2dp_source_service_buffer));
    a2dp_source_create_sdp_record(sdp_a2dp_source_service_buffer, sdp_create_service_record_handle(), AVDTP_SOURCE_FEATURE_MASK_PLAYER, NULL, NULL);
    btstack_assert(de_get_len(sdp_a2dp_source_service_buffer) <= sizeof(sdp_a2dp_source_service_buffer));
    sdp_register_service(sdp_a2dp_source_service_buffer);

    memset(sdp_avrcp_target_service_buffer, 0, sizeof(sdp_avrcp_target_service_buffer));
    avrcp_target_create_sdp_record(sdp_avrcp_target_service_buffer, sdp_create_service_record_handle(), AVRCP_FEATURE_MASK_CATEGORY_PLAYER_OR_RECORDER, NULL, NULL);
    btstack_assert(de_get_len(sdp_avrcp_target_service_buffer) <= sizeof(sdp_avrcp_target_service_buffer));
    sdp_register_service(sdp_avrcp_target_service_buffer);

    memset(sdp_avrcp_controller_service_buffer, 0, sizeof(sdp_avrcp_controller_service_buffer));
    avrcp_controller_create_sdp_record(sdp_avrcp_controller_service_buffer, sdp_create_service_record_handle(), AVRCP_FEATURE_MASK_CATEGORY_MONITOR_OR_AMPLIFIER, NULL, NULL);
    btstack_assert(de_get_len(sdp_avrcp_controller_service_buffer) <= sizeof(sdp_avrcp_controller_service_buffer));
    sdp_register_service(sdp_avrcp_controller_service_buffer);

    memset(device_id_sdp_service_buffer, 0, sizeof(device_id_sdp_service_buffer));
    device_id_create_sdp_record(device_id_sdp_service_buffer, sdp_create_service_record_handle(), DEVICE_ID_VENDOR_ID_SOURCE_BLUETOOTH, BLUETOOTH_COMPANY_ID_BLUEKITCHEN_GMBH, 1, 1);
    btstack_assert(de_get_len(device_id_sdp_service_buffer) <= sizeof(device_id_sdp_service_buffer));
    sdp_register_service(device_id_sdp_service_buffer);

    gap_set_local_name("neuPod Y2");
    gap_set_class_of_device(0x20041c); // audio service, audio/video major class, portable audio
    gap_discoverable_control(0);       // we connect out; the headset reconnects to us when bonded
    gap_connectable_control(1);

    // pairing: Secure Simple Pairing, no input / no output, general bonding
    gap_ssp_set_enable(1);
    gap_ssp_set_io_capability(SSP_IO_CAPABILITY_NO_INPUT_NO_OUTPUT);
    gap_ssp_set_authentication_requirement(SSP_IO_AUTHREQ_MITM_PROTECTION_NOT_REQUIRED_GENERAL_BONDING);
    gap_set_bondable_mode(1);
    gap_secure_connections_enable(true); // takes effect only if the controller supports it

    hci_event_callback_registration.callback = &hci_packet_handler;
    hci_add_event_handler(&hci_event_callback_registration);
    return 0;
}

static void usage(FILE * out){
    fprintf(out,
        "usage: y2-a2dp scan [--hci-dump FILE]\n"
        "       y2-a2dp play [--addr XX:XX:XX:XX:XX:XX | --name SUBSTR | --last] [--wav FILE] [--seconds N]\n"
        "                    [--find-timeout N] [--hci-dump FILE]\n"
        "  scan   inquiry for 10 s, then print address, RSSI, class and name of each device\n"
        "  play   find the headset (default: name containing \"" DEFAULT_NAME "\"), pair, connect, stream, disconnect\n"
        "         --last           use the headset streamed to last time (no inquiry; needs a link key)\n"
        "         --wav FILE       16-bit stereo 44.1 kHz WAV; default is a 440 Hz sine at -12 dBFS\n"
        "         --seconds N      stop after N seconds of streaming (default %d for the sine, end of file for a WAV)\n"
        "         --find-timeout N give up searching by name after N seconds (default %d)\n"
        "  --hci-dump FILE   write an HCI trace (PacketLogger format)\n", DEFAULT_SECONDS, DEFAULT_FIND_SECS);
}

static int parse_args(int argc, char ** argv){
    if (argc < 2) { usage(stderr); return EXIT_USAGE; }
    if (strcmp(argv[1], "scan") == 0) opt.cmd = CMD_SCAN;
    else if (strcmp(argv[1], "play") == 0) opt.cmd = CMD_PLAY;
    else { usage(strcmp(argv[1], "--help") == 0 ? stdout : stderr); return strcmp(argv[1], "--help") == 0 ? 0 : EXIT_USAGE; }

    opt.find_secs = DEFAULT_FIND_SECS;
    opt.seconds = -1;
    static const struct option longopts[] = {
        { "addr", required_argument, NULL, 'a' },
        { "name", required_argument, NULL, 'n' },
        { "last", no_argument, NULL, 'l' },
        { "wav", required_argument, NULL, 'w' },
        { "seconds", required_argument, NULL, 's' },
        { "find-timeout", required_argument, NULL, 'f' },
        { "hci-dump", required_argument, NULL, 'd' },
        { "help", no_argument, NULL, 'h' },
        { NULL, 0, NULL, 0 },
    };
    optind = 2;
    int c;
    while ((c = getopt_long(argc, argv, "", longopts, NULL)) != -1) {
        switch (c) {
            case 'a':
                if (!sscanf_bd_addr(optarg, opt.addr)) { fprintf(stderr, "y2-a2dp: bad address '%s'\n", optarg); return EXIT_USAGE; }
                opt.have_addr = true;
                break;
            case 'n': opt.name = optarg; break;
            case 'l': opt.use_last = true; break;
            case 'w': opt.wav_path = optarg; break;
            case 's': opt.seconds = atoi(optarg); if (opt.seconds < 0) { usage(stderr); return EXIT_USAGE; } break;
            case 'f': opt.find_secs = atoi(optarg); if (opt.find_secs <= 0) { usage(stderr); return EXIT_USAGE; } break;
            case 'd': opt.hci_dump_path = optarg; break;
            case 'h': usage(stdout); return 0;
            default: usage(stderr); return EXIT_USAGE;
        }
    }
    if (optind != argc) { usage(stderr); return EXIT_USAGE; }
    if (opt.cmd == CMD_SCAN && (opt.have_addr || opt.name || opt.use_last || opt.wav_path || opt.seconds >= 0)) {
        fprintf(stderr, "y2-a2dp: scan takes only --hci-dump\n");
        return EXIT_USAGE;
    }
    if ((opt.have_addr ? 1 : 0) + (opt.name ? 1 : 0) + (opt.use_last ? 1 : 0) > 1) {
        fprintf(stderr, "y2-a2dp: use only one of --addr, --name, --last\n");
        return EXIT_USAGE;
    }
    if (opt.cmd == CMD_PLAY && !opt.have_addr && !opt.use_last && !opt.name) opt.name = DEFAULT_NAME;
    if (opt.seconds < 0) opt.seconds = opt.wav_path ? 0 : DEFAULT_SECONDS;
    return -1; // continue
}

int main(int argc, char ** argv){
    clock_gettime(CLOCK_MONOTONIC, &t_start);

    int rc = parse_args(argc, argv);
    if (rc >= 0) return rc;

    // Validate everything that does not need the chip before touching it (opening the device powers it on).
    if (opt.wav_path && !wav_open(opt.wav_path)) return EXIT_USAGE;
    sine_amplitude = 32767.0 * pow(10.0, SINE_DBFS / 20.0);

    bd_addr_t local_addr;
    if (!read_local_bd_addr(local_addr)) return 1;
    if (mkdir(STATE_DIR, 0700) != 0 && errno != EEXIST) {
        fprintf(stderr, "y2-a2dp: cannot create %s: %s\n", STATE_DIR, strerror(errno));
        return 1;
    }

    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = signal_handler;
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);

    btstack_memory_init();
    btstack_run_loop_init(btstack_run_loop_posix_get_instance());

    if (opt.hci_dump_path) {
        hci_dump_posix_fs_open(opt.hci_dump_path, HCI_DUMP_PACKETLOGGER);
        hci_dump_init(hci_dump_posix_fs_get_instance());
        logt("HCI trace -> %s", opt.hci_dump_path);
    }

    static hci_transport_config_uart_t transport_config = {
        .type = HCI_TRANSPORT_CONFIG_UART,
        .device_name = STPBT_DEVICE,
        .baudrate_init = 115200, // ignored by the stpbt driver
        .baudrate_main = 0,
        .flowcontrol = BTSTACK_UART_FLOWCONTROL_OFF,
        .parity = BTSTACK_UART_PARITY_OFF,
    };
    btstack_uart_stpbt_set_error_handler(transport_error);
    hci_init(hci_transport_h4_instance_for_uart(btstack_uart_stpbt_instance()), &transport_config);

    // MediaTek vendor script (address, radio, TX power, sleep) must run before HCI_Reset.
    btstack_chipset_mtk_consys_set_bd_addr(local_addr);
    hci_set_chipset(btstack_chipset_mtk_consys_instance());
    hci_enable_custom_pre_init();

    // Link keys and the last-headset record live in one TLV file.
    tlv_impl = btstack_tlv_posix_init_instance(&tlv_context, TLV_PATH);
    btstack_tlv_set_instance(tlv_impl, &tlv_context);
    hci_set_link_key_db(btstack_link_key_db_tlv_get_instance(tlv_impl, &tlv_context));

    if (services_init() != 0) return 1;

    btstack_run_loop_set_timer_handler(&signal_timer, signal_poll);
    btstack_run_loop_set_timer(&signal_timer, 100);
    btstack_run_loop_add_timer(&signal_timer);

    logt("local address %s (from %s); opening %s (takes about 1.3 s)", bd_addr_to_str(local_addr), BT_ADDR_FILE, STPBT_DEVICE);
    hci_power_control(HCI_POWER_ON);
    btstack_run_loop_execute();
    return exit_code;
}
