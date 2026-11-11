// SPDX-License-Identifier: MPL-2.0
#include <stdio.h>
#include <string.h>

#include "battery_store.h"
#include "bt_service.h"
#include "boot.h"
#include "boot_briefs.h"
#include "capabilities.h"
#include "audio_engine.h"
#include "hal_audio.h"
#include "hal_battery.h"
#include "hal_haptic.h"
#include "hal_i2c.h"
#include "hal_input.h"
#include "hal_radio.h"
#include "hal_storage.h"
#include "hal_system.h"
#include "input_events.h"
#include "nfs.h"
#include "os.h"
#include "runtime.h"
#include "runtime_config.h"
#include "settings_service.h"
#include "settings_store.h"

#define I2C_BUS_HZ 400000u
#define I2C_LOCK_MS 500u

// Deadlines from the moment boot_start() ran. Generous but bounded: the longest legitimate case
// is a slow card (identify ~1 s + mount), everything else answers in milliseconds or not at all.
#define TIMEOUT_STORAGE_MS 4000u
#define TIMEOUT_I2C_MS 1500u
#define TIMEOUT_WHEEL_MS 1500u
#define TIMEOUT_RADIO_MS 4800u // radio driver (firmware load) + Bluetooth HCI bring-up (controller firmware patch); ends before the splash cap (5 s)
#define RADIO_GIVE_UP_MS 12000u // the radio check keeps waiting this long, so a late HCI still upgrades the result

static uint32_t g_start_ms;
static bool g_started;

static const char *status_name(hal_status_t st) {
    switch (st) {
    case HAL_OK:
        return "ok";
    case HAL_ERR:
        return "error";
    case HAL_EINVAL:
        return "bad argument";
    case HAL_ETIMEOUT:
        return "timeout";
    case HAL_ENODEV:
        return "no response";
    case HAL_ENOTSUP:
        return "not supported";
    case HAL_EBUSY:
        return "busy";
    }
    return "unknown";
}

static const char *fs_name(hal_fs_type_t t) {
    switch (t) {
    case HAL_FS_FAT12:
        return "FAT12";
    case HAL_FS_FAT16:
        return "FAT16";
    case HAL_FS_FAT32:
        return "FAT32";
    case HAL_FS_EXFAT:
        return "exFAT";
    case HAL_FS_NONE:
        break;
    }
    return "no filesystem";
}

// --- Checks ----------------------------------------------------------------------------------

#if NEUPOD_RT_HOTPLUG
static os_mutex_t *g_storage_check_mu;         // one storage check at a time: the boot check and every hot-plug retry
static volatile bool g_storage_check_active;   // a check holds the mutex (the hot-plug poll does not start another)
static bool g_tried_this_card;                  // hot-plug: the card now in the slot already failed a check

static void storage_check_begin(void) {
    if (g_storage_check_mu != NULL) {
        (void)os_mutex_lock(g_storage_check_mu, OS_WAIT_FOREVER);
    }
    g_storage_check_active = true;
}

// @p ok: the verdict of a boot check (a hot-plug re-run has its own bookkeeping).
static void storage_check_end(bool ok) {
    if (!ok && rt_config()->storage_hotplug) {
        g_tried_this_card = hal_storage_card_present(); // a card that is there and failed is not retried until re-inserted
    }
    g_storage_check_active = false;
    if (g_storage_check_mu != NULL) {
        os_mutex_unlock(g_storage_check_mu);
    }
}
#else
static inline void storage_check_begin(void) {}
static inline void storage_check_end(bool ok) { (void)ok; }
#endif

// Hot-plug builds (the Y2): a check that fails leaves no descriptor behind, so a card that is pulled and put back (or a
// second attempt) starts from a clean HAL. Other targets keep the card initialised as before.
static void storage_check_failed(void) {
    if (NEUPOD_RT_STORAGE_HOTPLUG()) {
        hal_storage_deinit();
    }
}

// Returns true when the card is usable (mounted, or the host's image): the settings store may then load and save.
// @p retry: not the boot check but a hot-plug re-run. It must not touch the write lock (nothing may be unmounted or locked
// underneath a settings write that is in flight) and reports a failure through caps_resolve() too (the capability was re-armed).
static bool check_storage_inner(bool retry) {
    if (!NEUPOD_RT_HOTPLUG || !retry) {
        hal_storage_set_write_enabled(false); // read-only boot: the lock is the default, make it explicit
    }
    hal_storage_status_t st = hal_storage_init();
    if (st != HAL_STORAGE_OK) {
        caps_resolve(CAP_STORAGE, false, st == HAL_STORAGE_NO_CARD ? BR_SD_NONE : BR_SD_INIT, "card init: %s", hal_storage_status_str(st));
        storage_check_failed();
        return false;
    }
    hal_card_info_t info;
    if (hal_storage_get_info(&info) != HAL_STORAGE_OK) {
        caps_resolve(CAP_STORAGE, false, BR_SD_INFO, "card info unreadable");
        storage_check_failed();
        return false;
    }
    st = hal_storage_mount();
    if (st == HAL_STORAGE_UNSUPPORTED) {
        // Host: the disk image is accessed through the file API, there is no FatFs mount.
        caps_resolve(CAP_STORAGE, true, NULL, "%s, no mount on this platform", hal_storage_card_type_str(info.type));
        return true;
    }
    if (st != HAL_STORAGE_OK) {
        caps_resolve(CAP_STORAGE, false, st == HAL_STORAGE_NO_FILESYSTEM ? BR_SD_NOFS : BR_SD_MOUNT, "mount: %s", hal_storage_status_str(st));
        storage_check_failed();
        return false;
    }
    uint32_t gb10 = (uint32_t)(info.capacity_bytes * 10u / 1000000000u); // tenths of a GB
    caps_resolve(CAP_STORAGE, true, NULL, "%s %u.%u GB, %s, read-only", hal_storage_card_type_str(info.type),
                 (unsigned)(gb10 / 10u), (unsigned)(gb10 % 10u), fs_name(hal_storage_fs_type()));
    return true;
}

static void check_storage(void *arg) {
    (void)arg;
    storage_check_begin();
    bool ok = check_storage_inner(false);
    settings_service_storage_result(ok); // after the verdict is posted: loading the settings must not delay the splash
    storage_check_end(ok);
}

static void check_battery(void *arg) {
    (void)arg;
    if (!os_mutex_lock(rt_i2c_mutex(), I2C_LOCK_MS)) {
        caps_resolve(CAP_BATTERY, false, BR_I2C_BUSY, "i2c bus busy");
        return;
    }
    hal_status_t st = hal_battery_init(); // ACK at 0x36 and VERSION == 0x001X
    hal_battery_status_t bat = {0};
    if (st == HAL_OK) {
        st = hal_battery_read(&bat);
    }
    os_mutex_unlock(rt_i2c_mutex());
    if (st != HAL_OK) {
        caps_resolve(CAP_BATTERY, false, BR_GAUGE, "gauge (0x36): %s", status_name(st));
        return;
    }
    if (bat.mv < HAL_BATTERY_PRESENT_MV) {
        caps_resolve(CAP_BATTERY, false, BR_NO_CELL, "no cell: %u mV", (unsigned)bat.mv);
        return;
    }
    if (bat.mv > 4600u) {
        caps_resolve(CAP_BATTERY, false, BR_BAD_CELL, "implausible cell voltage %u mV", (unsigned)bat.mv);
        return;
    }
    battery_store_publish(&bat);
    caps_resolve(CAP_BATTERY, true, NULL, "%u mV, %u%%", (unsigned)bat.mv, (unsigned)bat.soc_pct);
}

static void check_haptics(void *arg) {
    (void)arg;
    if (!os_mutex_lock(rt_i2c_mutex(), I2C_LOCK_MS)) {
        caps_resolve(CAP_HAPTICS, false, BR_I2C_BUSY, "i2c bus busy");
        return;
    }
    // hal_haptic_init() verifies the ACK at 0x5A and DEVICE_ID == 7, and is motor-safe.
    hal_status_t st = hal_haptic_init();
    if (st == HAL_OK) {
        st = hal_haptic_standby(true); // never leave the driver (and so the motor) awake
    }
    if (st != HAL_OK) {
        (void)hal_haptic_safe_state(); // RTP = 0, GO = 0, standby, whatever else failed
    }
    os_mutex_unlock(rt_i2c_mutex());
    if (st != HAL_OK) {
#ifdef NEUPOD_PLATFORM_Y2
        caps_resolve(CAP_HAPTICS, false, BR_HAPTIC, "timed_output vibrator: %s", status_name(st));
#else
        caps_resolve(CAP_HAPTICS, false, BR_HAPTIC, "driver (0x5A): %s", status_name(st));
#endif
        return;
    }
#ifdef NEUPOD_PLATFORM_Y2
    caps_resolve(CAP_HAPTICS, true, NULL, "timed_output vibrator (ERM)");
#else
    caps_resolve(CAP_HAPTICS, true, NULL, "DRV2605L, standby");
#endif
}

static void check_wheel(void *arg) {
    (void)arg;
    // input_task is the only caller of hal_input_poll(); this check only starts the receiver and then
    // waits for it to report valid frames.
    hal_status_t st = hal_input_init();
    if (st != HAL_OK) {
        caps_resolve(CAP_WHEEL, false, BR_WHEEL_RX, "receiver: %s", status_name(st));
        return;
    }
    // The init packet is not implemented yet (TODO(hw)): a failure here is not fatal. What matters
    // is whether valid frames arrive.
    hal_status_t rs = hal_input_reinit();
    // Wait until shortly before this check's own deadline (measured from boot_start, not from when this
    // task happened to start), so "not connected" is decided here and not by the timeout.
    while (os_tick_ms() - g_start_ms + 300u < TIMEOUT_WHEEL_MS) {
        if (rt_wheel_frames_seen() > 0) {
            caps_resolve(CAP_WHEEL, true, NULL, "frames valid (init packet: %s)", status_name(rs));
            return;
        }
        os_delay_ms(10);
    }
    caps_resolve(CAP_WHEEL, false, BR_WHEEL_OFF, "no valid frame from the wheel (init packet: %s)",
                 status_name(rs));
}

static void check_audio(void *arg) {
    (void)arg;
    // Read-only: the audio core owns the sink. It publishes the sink's rate once the first block has been served,
    // so a non-zero rate means the stream really runs. The DAC itself cannot be detected electrically (I2S is
    // one-way and the Audio Pack has no readable ID), so "ok" means the I2S transmitter and its DMA run, not that a DAC answered.
    const char *backend = hal_audio_backend_name();
    if (strcmp(backend, "null") == 0) {
        caps_resolve(CAP_AUDIO, false, BR_AUDIO, "output is the null sink (no DAC output)");
        return;
    }
    for (int i = 0; i < 40; i++) {
        audio_stats_t st;
        if (audio_stats_get(&st) && st.sink_rate_hz != 0 && st.blocks > 2) {
            caps_resolve(CAP_AUDIO, true, NULL, "%s stream running at %u Hz (DAC presence not detectable)", backend,
                         (unsigned)st.sink_rate_hz);
            return;
        }
        os_delay_ms(10);
    }
    caps_resolve(CAP_AUDIO, false, BR_AUDIO, "%s stream did not start", backend);
}

static void check_radio(void *arg) {
    (void)arg;
    // Bring up the radio chip's driver (RP2350: required for the status LED too; its async context is also BTstack's thread).
    hal_status_t st = hal_system_late_init();
    if (st != HAL_OK) {
        caps_resolve(CAP_BLUETOOTH, false, BR_RADIO_CHIP, "radio chip did not initialise");
        caps_resolve(CAP_WIFI, false, BR_RADIO_CHIP, "radio chip did not initialise");
        return;
    }
    hal_status_led(true); // first LED write loads the chip firmware; do it here, not in the monitor
    hal_status_led(false);
    // "ok" means the driver and lwIP are up, not that a network is joined (joining is the Wi-Fi service's business).
    caps_resolve(CAP_WIFI, true, NULL, "driver and lwIP up (joining is separate)");

    // Do not bring Bluetooth up before the settings store has loaded (link keys live there): wait briefly for the first load.
    while (os_tick_ms() - g_start_ms < 3500u) {
        capabilities_t c;
        caps_get(&c);
        if (settings_generation() != 0 || c.state[CAP_STORAGE] == CAP_DEGRADED) {
            break; // loaded, or there is no card to wait for
        }
        os_delay_ms(20);
    }
    st = bt_service_start(); // hci power on, BD_ADDR and HCI version go to the UART from the radio worker
    if (st != HAL_OK) {
        caps_resolve(CAP_BLUETOOTH, false, BR_BT_START, "Bluetooth start failed: %s", status_name(st));
        return;
    }
    // Wait for HCI. Not given up at this check's deadline: boot_poll() marks the capability degraded then, and an OK that
    // arrives later still upgrades it (caps_resolve_timeout), which is what a slow firmware load needs.
    while (os_tick_ms() - g_start_ms < RADIO_GIVE_UP_MS) {
        bt_service_tick();
        hal_bt_state_t bs = hal_bt_state();
        if (bs == HAL_BT_ON || bs == HAL_BT_SCANNING) {
            uint8_t a[6] = {0};
            (void)hal_bt_local_address(a);
            caps_resolve(CAP_BLUETOOTH, true, NULL, "HCI up, BD_ADDR %02X:%02X:%02X:%02X:%02X:%02X", a[0], a[1], a[2], a[3],
                         a[4], a[5]);
            return;
        }
        if (bs == HAL_BT_FAILED) {
            caps_resolve(CAP_BLUETOOTH, false, BR_BT_HCI, "Bluetooth controller did not come up");
            return;
        }
        os_delay_ms(20);
    }
    caps_resolve(CAP_BLUETOOTH, false, BR_BT_HCI, "Bluetooth controller did not come up in %u s", (unsigned)(RADIO_GIVE_UP_MS / 1000u));
}

typedef struct {
    const char *name;
    os_task_fn fn;
    uint32_t stack;
    uint32_t timeout_ms;
    uint8_t priority;
    cap_id_t caps[2]; // capabilities this check owns (CAP_COUNT = unused slot)
} check_t;

// Every check task runs through this wrapper, which reports how much of its stack it really used.
static void check_runner(void *arg) {
    const check_t *c = arg;
    c->fn(NULL);
    printf("boot: %-9s task stack free min %u B\n", c->name, (unsigned)os_stack_free_min());
}

static const check_t k_checks[] = {
    {"chk_sd", check_storage, RT_STACK_CHECK_STORAGE, TIMEOUT_STORAGE_MS, RT_PRIO_CHECK, {CAP_STORAGE, CAP_COUNT}},
    {"chk_batt", check_battery, RT_STACK_CHECK, TIMEOUT_I2C_MS, RT_PRIO_I2C, {CAP_BATTERY, CAP_COUNT}},
    {"chk_hapt", check_haptics, RT_STACK_CHECK, TIMEOUT_I2C_MS, RT_PRIO_I2C, {CAP_HAPTICS, CAP_COUNT}},
    {"chk_whl", check_wheel, RT_STACK_CHECK, TIMEOUT_WHEEL_MS, RT_PRIO_CHECK, {CAP_WHEEL, CAP_COUNT}},
    {"chk_aud", check_audio, RT_STACK_CHECK, 500u, RT_PRIO_CHECK, {CAP_AUDIO, CAP_COUNT}},
    {"chk_radio", check_radio, RT_STACK_CHECK_RADIO, TIMEOUT_RADIO_MS, RT_PRIO_CHECK, {CAP_BLUETOOTH, CAP_WIFI}},
};

void boot_start(void) {
#if NEUPOD_RT_HOTPLUG
    if (g_storage_check_mu == NULL) {
        g_storage_check_mu = os_mutex_create(); // before the check tasks exist
    }
#endif
    g_start_ms = os_tick_ms();
    g_started = true;
    // The bus is initialised once, here; the two I2C checks then take turns on it (rt_i2c_mutex).
    hal_i2c_err_t ie = hal_i2c_init(I2C_BUS_HZ);
    for (size_t i = 0; i < sizeof k_checks / sizeof k_checks[0]; i++) {
        const check_t *c = &k_checks[i];
        bool is_i2c = c->fn == check_battery || c->fn == check_haptics;
        if (is_i2c && ie != HAL_I2C_OK) {
            for (int k = 0; k < 2; k++) {
                if (c->caps[k] < CAP_COUNT) {
                    caps_resolve(c->caps[k], false, BR_I2C_INIT, "i2c bus init failed (%d)", (int)ie);
                }
            }
            continue;
        }
        os_task_t *t = os_task_create(&(os_task_cfg_t){.name = c->name,
                                                       .fn = check_runner,
                                                       .arg = (void *)c,
                                                       .stack_bytes = c->stack,
                                                       .priority = c->priority,
                                                       .core = RT_CORE_OTHER});
        if (t == NULL) {
            for (int k = 0; k < 2; k++) {
                if (c->caps[k] < CAP_COUNT) {
                    caps_resolve(c->caps[k], false, BR_NOMEM, "could not start check (out of memory)");
                }
            }
        }
    }
}

// --- Card inserted or removed after boot (Y2) -------------------------------------------------------------------------
#if NEUPOD_RT_HOTPLUG

#define HOTPLUG_POLL_MS 2000u
static volatile bool g_hotplug_busy;

static void hotplug_check(void *arg) {
    (void)arg;
    // One check at a time, and never while a settings write is in flight (a card that was pulled mid-write): try again later.
    if (g_storage_check_mu == NULL || !os_mutex_lock(g_storage_check_mu, 0) ) {
        g_tried_this_card = false;
        g_hotplug_busy = false;
        return;
    }
    if (nfs_write_scope_active()) {
        g_tried_this_card = false;
        os_mutex_unlock(g_storage_check_mu);
        g_hotplug_busy = false;
        return;
    }
    g_storage_check_active = true;
    printf("boot: card appeared, checking storage again\n");
    caps_allow_late_ok(CAP_STORAGE);
    bool ok = check_storage_inner(true);
    settings_service_storage_result(ok);
    g_storage_check_active = false;
    os_mutex_unlock(g_storage_check_mu);
    g_hotplug_busy = false;
}

void boot_storage_hotplug_poll(void) {
    static uint32_t last_ms;
    if (!g_started || g_hotplug_busy || g_storage_check_active) {
        return;
    }
    uint32_t now = os_tick_ms();
    if (now - last_ms < HOTPLUG_POLL_MS) {
        return;
    }
    last_ms = now;
    capabilities_t c;
    caps_get(&c);
    bool present = hal_storage_card_present();
    if (c.state[CAP_STORAGE] == CAP_OK) {
        if (!present) { // pulled while in use
            caps_force_degraded(CAP_STORAGE, BR_SD_NONE, "card removed");
            settings_service_storage_result(false); // settings go back to RAM-only
            g_tried_this_card = false;
        }
        return;
    }
    if (c.state[CAP_STORAGE] != CAP_DEGRADED) {
        return; // still checking (PENDING)
    }
    if (!present) {
        g_tried_this_card = false;
        return;
    }
    if (g_tried_this_card) {
        return; // this card already failed the check: wait until it has been taken out and put back
    }
    g_tried_this_card = true;
    g_hotplug_busy = true;
    if (os_task_create(&(os_task_cfg_t){.name = "chk_sd2", .fn = hotplug_check, .stack_bytes = RT_STACK_CHECK_STORAGE,
                                        .priority = RT_PRIO_CHECK, .core = RT_CORE_OTHER}) == NULL) {
        g_hotplug_busy = false;
        g_tried_this_card = false;
    }
}

#endif // NEUPOD_RT_HOTPLUG

bool boot_poll(void) {
    if (!g_started) {
        return false;
    }
    uint32_t elapsed = os_tick_ms() - g_start_ms;
    for (size_t i = 0; i < sizeof k_checks / sizeof k_checks[0]; i++) {
        const check_t *c = &k_checks[i];
        if (elapsed < c->timeout_ms) {
            continue;
        }
        for (int k = 0; k < 2; k++) {
            if (c->caps[k] < CAP_COUNT) {
                // No-op unless still pending; a late "ok" from the check can still upgrade it (logged).
                caps_resolve_timeout(c->caps[k], BR_TIMEOUT, "check timed out after %u ms", (unsigned)c->timeout_ms);
            }
        }
    }
    return caps_all_resolved();
}
