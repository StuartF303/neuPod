// SPDX-License-Identifier: MPL-2.0
/*
 * Runtime assembly: task creation, the input task, the storage task stub and the monitor task.
 * The task table (priorities / cores / stacks) lives in runtime_config.h; docs/runtime.md
 * explains the choices.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "audio_engine.h"
#include "battery_store.h"
#include "bt_service.h"
#include "boot.h"
#include "capabilities.h"
#include "hal_battery.h"
#include "hal_console.h"
#include "hal_audio.h"
#include "hal_core.h"
#include "hal_input.h"
#include "hal_storage.h"
#include "hal_system.h"
#include "input_events.h"
#include "neupod_version.h"
#include "os.h"
#include "panic.h"
#include "codec.h"
#include "player.h"
#include "runtime.h"
#include "runtime_config.h"
#include "settings_service.h"
#include "wifi_service.h"
#include "haptic_service.h"
#include "storage_service.h"
#include "ui.h"

static neupod_runtime_config_t g_cfg;
static os_mutex_t *g_i2c_mutex;
static os_queue_t *g_input_queue;

const neupod_runtime_config_t *rt_config(void) { return &g_cfg; }
os_mutex_t *rt_i2c_mutex(void) { return g_i2c_mutex; }
os_queue_t *rt_input_queue(void) { return g_input_queue; }

// --- Input task ------------------------------------------------------------------------------
// Above the UI so wheel frames are never missed while LVGL renders: the RP2350 click-wheel PIO
// FIFO overflows if not drained within ~4 ms (hal_input.c), hence the 2 ms period.

static volatile uint32_t g_wheel_frames;
uint32_t rt_wheel_frames_seen(void) { return g_wheel_frames; }

static void send_events(const input_event_t *ev, size_t n) {
    for (size_t i = 0; i < n; i++) {
        (void)os_queue_send(g_input_queue, &ev[i], OS_NO_WAIT); // drop if the UI lags
    }
}

// This task is the ONLY caller of hal_input_poll() and of the console reader (the PIO FIFO and the
// frame sync are single-consumer); the wheel boot check just waits for g_wheel_frames.
static void input_task(void *arg) {
    (void)arg;
    hal_wheel_state prev = {0};
    uint32_t last = os_tick_ms();
    for (;;) {
        hal_wheel_state cur;
        if (hal_input_poll(&cur) && cur.valid) {
            g_wheel_frames++;
            input_event_t ev[8];
            size_t n = input_events_diff(&prev, &cur, ev, 8);
            // Wheel-sourced events are forwarded only once the wheel passed its boot check (or is a
            // simulation): a floating, unwired receiver must not inject phantom presses. Other
            // sources (console keys below) never depend on the wheel.
            if (hal_input_is_simulated() || caps_available(CAP_WHEEL)) {
                send_events(ev, n);
            }
            prev = cur;
        }
        if (g_cfg.console_keys) {
            int key;
            while ((key = hal_console_getc_timeout(0)) >= 0) {
                input_event_t ev[2];
                send_events(ev, input_events_from_key(key, ev, 2));
            }
        }
        os_delay_until_ms(&last, 2);
    }
}

// --- Monitor task ----------------------------------------------------------------------------

#define HEARTBEAT_MS 1000u
#define GAUGE_MS 5000u
#define STATS_MS 10000u

static void print_audio_stats(void) {
    audio_stats_t a;
    if (!audio_stats_get(&a)) {
        printf("audio: stats unavailable (audio core not publishing)\n");
        return;
    }
    printf("audio: blocks=%u underruns=%u sink_missed=%u period_us min/avg/max=%u/%u/%u (nominal %u) "
           "jitter_max_us=%u ring_min=%u/%u decoded=%u data_bytes=%u events_dropped=%u core1_stack_free=%uB\n",
           (unsigned)a.blocks, (unsigned)a.underruns, (unsigned)a.sink_missed,
           (unsigned)a.period_min_us, (unsigned)a.period_avg_us, (unsigned)a.period_max_us,
           (unsigned)a.period_nominal_us, (unsigned)a.jitter_max_us, (unsigned)a.ring_min_frames,
           (unsigned)a.ring_frames, (unsigned)a.decoded_blocks, (unsigned)a.data_bytes,
           (unsigned)a.events_dropped, (unsigned)hal_core1_stack_free_min());
    static const char *const src[] = {"none", "tone", "pcm"};
    printf("audio sink: %s rate=%u.%03u Hz (requested %u Hz, clkdiv=%u+%u/256) dma_underruns=%u muted=%u "
           "source=%s audible=%u volume=-%udB pcm_frames=%u\n",
           hal_audio_backend_name(), (unsigned)(a.sink_rate_mhz / 1000u), (unsigned)(a.sink_rate_mhz % 1000u),
           (unsigned)a.sink_rate_hz, (unsigned)(a.sink_clkdiv_x256 >> 8), (unsigned)(a.sink_clkdiv_x256 & 0xFFu),
           (unsigned)a.sink_missed, (unsigned)a.muted, a.source < 3u ? src[a.source] : "?", (unsigned)a.audible,
           (unsigned)a.volume_att_db, (unsigned)a.pcm_frames);
    static const char *const codecs[] = {"pcm", "flac", "mp3", "alac"};
    static const char *const verdict[] = {"-", "running", "MD5 OK", "MD5 MISMATCH", "no MD5 in file"};
    printf("audio decoder: codec=%s decode_us_per_256_frames avg/max=%u/%u load=%u.%u%% of a core, decoded=%u frames "
           "verify=%s codec_error=%u arena_high_water=%uB/%uB\n",
           a.codec < 4u ? codecs[a.codec] : "?", (unsigned)a.dec_us_block_avg, (unsigned)a.dec_us_block_max,
           (unsigned)(a.dec_pct_x10 / 10u), (unsigned)(a.dec_pct_x10 % 10u), (unsigned)a.decoded_frames,
           a.verify_state < 5u ? verdict[a.verify_state] : "?", (unsigned)a.codec_error,
           (unsigned)codec_arena_high_water(), (unsigned)CODEC_ARENA_BYTES);
    (void)audio_stats_new_window();
}

// Events from the audio core: print them (rare by construction: underruns are limited to ~1/s).
static void drain_audio_events(void) {
    static const char *const names[] = {"?", "stream opened", "stream closed", "UNDERRUN",
                                        "end of track", "command error", "STORM (PCM data ran dry, stopped and muted)"};
    audio_event_t e;
    while (audio_event_poll(&e)) {
        printf("audio event: %s (%u)\n", e.type < sizeof names / sizeof names[0] ? names[e.type] : "?",
               (unsigned)e.value);
        if (e.type == AUDIO_EVT_END_OF_TRACK) {
            player_log_end();
        }
    }
}

static void print_wifi_stats(void) {
    static const char *const names[] = {"down", "idle", "joining", "no-ip", "up", "failed", "bad-password"};
    wifi_info_t w;
    wifi_get_info(&w);
    hal_wifi_mem_t m;
    hal_wifi_mem(&m);
    char ssid[HAL_WIFI_SSID_MAX + 1];
    hal_log_text(ssid, sizeof ssid, w.ssid);
    printf("wifi: state=%s%s ssid=%s rssi=%d ip=%s ntp=%u saved=%u reconnects=%u failures=%u lwip mem=%u/%u (max %u) pbuf=%u/%u (max %u) errs=%u\n",
           w.state <= HAL_WIFI_BAD_AUTH ? names[w.state] : "?", w.paused_for_playback ? "(paused for playback)" : "", ssid[0] ? ssid : "-",
           w.rssi, w.ip[0] ? w.ip : "-", (unsigned)w.ntp_synced, (unsigned)w.saved, (unsigned)w.reconnects, (unsigned)w.failures,
           (unsigned)m.mem_used, (unsigned)m.mem_size, (unsigned)m.mem_max, (unsigned)m.pbuf_used, (unsigned)m.pbuf_size,
           (unsigned)m.pbuf_max, (unsigned)m.errors);
}

static void print_ui_stats(void) {
    ui_perf_t u;
    if (ui_perf_get(&u)) {
        printf("ui: frames=%u frame_us avg/max=%u/%u lvgl_mem=%u/%uB (max %uB, frag %u%%)\n", (unsigned)u.frames,
               (unsigned)u.frame_avg_us, (unsigned)u.frame_max_us, (unsigned)u.mem_used, (unsigned)u.mem_total,
               (unsigned)u.mem_max_used, (unsigned)u.mem_frag_pct);
    }
    ui_perf_new_window();
}

static void print_task_stats(void) {
    static os_task_info_t info[OS_STATS_MAX_TASKS];
    size_t n = os_task_stats(info, OS_STATS_MAX_TASKS);
    printf("cpu%%(of one core)/stack-free-min:");
    for (size_t i = 0; i < n; i++) {
        printf(" %s %u.%u%%/%uB", info[i].name, (unsigned)(info[i].cpu_permille / 10u),
               (unsigned)(info[i].cpu_permille % 10u), (unsigned)info[i].stack_free_min_bytes);
    }
    printf("\nheap: free=%uB min_free=%uB\n", (unsigned)os_heap_free(), (unsigned)os_heap_free_min());
}

// Core-1 liveness (LOG ONLY; must stay silent on a good run): the block interrupt completes a block every
// 5.8 ms, so the counter must move within a second once the stream is open.
static void check_audio_alive(void) {
    static uint32_t last_blocks;
    static unsigned stalled_secs;
    audio_stats_t a;
    if (!audio_stats_get(&a)) {
        printf("monitor: audio stats unavailable (core 1 stopped mid-update?)\n");
        return;
    }
    if (a.blocks != last_blocks) {
        if (stalled_secs != 0) {
            printf("monitor: audio core running again after %u s\n", stalled_secs);
        }
        stalled_secs = 0;
    } else if (a.blocks != 0 || os_tick_ms() > 3000u) {
        stalled_secs++;
        printf("monitor: AUDIO CORE STALLED (blocks=%u, %u s)\n", (unsigned)a.blocks, stalled_secs);
    }
    last_blocks = a.blocks;
}

static void read_gauge(void) {
    if (!caps_available(CAP_BATTERY)) {
        return; // the gauge failed its boot check: nothing to read
    }
    hal_battery_status_t bat = {0};
    hal_status_t st = HAL_ETIMEOUT;
    if (os_mutex_lock(g_i2c_mutex, 200)) {
        os_priority_set(RT_PRIO_I2C); // see runtime_config.h: keep the UI from outlasting the bus timeout
        st = hal_battery_read(&bat);
        os_priority_set(RT_PRIO_MONITOR); // explicit base, never read back from the kernel
        os_mutex_unlock(g_i2c_mutex);
    }
    if (st == HAL_OK) {
        battery_store_publish(&bat);
    } else {
        printf("battery: gauge read failed (%d), keeping the last value\n", (int)st);
    }
}

#define SHUTDOWN_WAIT_MS 2000u

// Orderly stop: no card write may be cut short. Bounded, so a stuck task cannot keep the process from stopping.
static void shutdown_now(void) {
    printf("runtime: stop requested, shutting down\n");
    uint32_t t0 = os_tick_ms();
    while (!storage_service_idle() && os_tick_ms() - t0 < SHUTDOWN_WAIT_MS) {
        os_delay_ms(20);
    }
    if (!storage_service_idle()) {
        printf("runtime: storage still busy after %u ms, stopping anyway\n", (unsigned)SHUTDOWN_WAIT_MS);
    }
    hal_storage_set_write_enabled(false); // reopens read-only: everything written is synced first
    (void)hal_storage_sync();
    hal_storage_unmount();
    printf("runtime: stopped\n");
    fflush(stdout);
    exit(0);
}

static void monitor_task(void *arg) {
    (void)arg;
    uint32_t last = os_tick_ms();
    uint32_t start = last;
    uint32_t next_beat = HEARTBEAT_MS, next_gauge = GAUGE_MS, next_stats = STATS_MS;
    bool led = false;
    bool done_called = false;
    for (;;) {
        os_delay_until_ms(&last, 100);
        uint32_t t = os_tick_ms() - start;
        drain_audio_events();
        if (g_cfg.storage_hotplug) {
            boot_storage_hotplug_poll();
        }
        if (g_cfg.stop_requested != NULL && g_cfg.stop_requested()) {
            shutdown_now();
        }
        player_service(); // the queue moves on even when no screen is polling the player
        if (t >= next_beat) {
            next_beat += HEARTBEAT_MS;
            check_audio_alive();
            led = !led;
            hal_status_led(led);
            printf("neuPod alive (%s, %u s)\n", hal_platform_name(), (unsigned)(t / 1000u));
        }
        if (t >= next_gauge) {
            next_gauge += GAUGE_MS;
            read_gauge();
        }
        if (t >= next_stats) {
            next_stats += STATS_MS;
            print_audio_stats();
            print_ui_stats();
            print_wifi_stats();
            print_task_stats();
        }
        if (g_cfg.run_ms != 0 && !done_called && t >= g_cfg.run_ms) {
            done_called = true;
            print_audio_stats();
            print_task_stats();
            if (g_cfg.on_done != NULL) {
                g_cfg.on_done(g_cfg.ctx);
            }
        }
    }
}

// --- Assembly --------------------------------------------------------------------------------

// Every permanent task is essential: without the UI, input, storage or monitor the product is not
// running, so failing to create one is a fatal error, not a degraded mode.
static void must_create(const char *name, os_task_fn fn, uint32_t stack, uint8_t prio, os_core_t core) {
    if (os_task_create(&(os_task_cfg_t){.name = name, .fn = fn, .stack_bytes = stack,
                                        .priority = prio, .core = core}) == NULL) {
        neupod_panic(NEUPOD_PANIC_MALLOC_FAILED, name);
    }
}

void neupod_runtime_run(const neupod_runtime_config_t *cfg) {
    memset(&g_cfg, 0, sizeof g_cfg);
    if (cfg != NULL) {
        g_cfg = *cfg;
    }
    printf("neuPod %s (%s) starting on %s\n", NEUPOD_VERSION, NEUPOD_GIT_HASH, hal_platform_name());

    caps_init();
    battery_store_init();
    g_i2c_mutex = os_mutex_create();
    g_input_queue = os_queue_create(sizeof(input_event_t), 16);
    settings_service_init();
    bt_service_init();
    wifi_service_init();
    storage_service_init();
    haptic_service_init();
    if (g_i2c_mutex == NULL || g_input_queue == NULL || !storage_service_ready() || !haptic_service_ready()) {
        neupod_panic(NEUPOD_PANIC_MALLOC_FAILED, "runtime objects (i2c mutex / input / storage queues)");
    }

    // Core 1 is not an RTOS core: launch the bare-metal audio engine and open the stream. Done before
    // the scheduler starts, so the audio clock runs from the first millisecond.
    if (!audio_engine_start() || !audio_open(AUDIO_SAMPLE_RATE, 2, 16)) {
        printf("neuPod: audio core did not start\n");
    }
    player_init(); // default volume -30 dB goes to the audio core before anything can play
    must_create("storage", storage_task, RT_STACK_STORAGE, RT_PRIO_STORAGE, RT_CORE_OTHER);
    must_create("haptic", haptic_task, RT_STACK_HAPTIC, RT_PRIO_HAPTIC, RT_CORE_OTHER);
    must_create("wifi", wifi_task, RT_STACK_WIFI, RT_PRIO_WIFI, RT_CORE_OTHER);
    must_create("input", input_task, RT_STACK_INPUT, RT_PRIO_INPUT, RT_CORE_OTHER);
    must_create("ui", ui_task, RT_STACK_UI, RT_PRIO_UI, RT_CORE_OTHER);
    must_create("monitor", monitor_task, RT_STACK_MONITOR, RT_PRIO_MONITOR, RT_CORE_OTHER);
    os_start();
}
