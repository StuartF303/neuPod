// SPDX-License-Identifier: MPL-2.0
#include <stdio.h>
#include <string.h>

#include "hal_system.h"
#include "os.h"
#include "panic.h"

// A panic while panicking (e.g. an assert inside the printing path) must not recurse.
static volatile int g_panicking;

void neupod_panic(neupod_panic_code_t code, const char *detail) {
    static char msg[128];
    if (g_panicking) {
        hal_panic((uint8_t)code, "PANIC: nested panic\n");
    }
    g_panicking = 1;
    const char *kind = code == NEUPOD_PANIC_STACK_OVERFLOW ? "stack overflow in task"
                       : code == NEUPOD_PANIC_MALLOC_FAILED ? "out of memory"
                       : code == NEUPOD_PANIC_ASSERT        ? "assertion failed"
                                                            : "fatal error";
    // snprintf is not signal/ISR-safe in general, but the alternative is no message at all, and
    // it only formats into a static buffer (no heap, no stdio locks).
    snprintf(msg, sizeof msg, "\nPANIC: %s: %s (LED code %u)\n", kind,
             detail != NULL ? detail : "?", (unsigned)code);
    // Task context: freeze the UI and every other task so the failure is not masked by a screen that
    // keeps redrawing; the radio worker keeps running because the status LED (behind the CYW43 chip)
    // needs it, which is why hal_panic can still blink the code. In interrupt/kernel context nothing
    // blocking is possible: skip straight to the halt (UART message, no LED on the Pico 2 W).
    if (hal_panic_can_block()) {
        os_freeze_other_tasks();
    }
    hal_panic((uint8_t)code, msg);
}

void neupod_panic_assert(const char *file, int line) {
    // Keep just the file's base name; __FILE__ is a long absolute path.
    const char *base = strrchr(file, '/');
    char where[48];
    snprintf(where, sizeof where, "%s:%d", base != NULL ? base + 1 : file, line);
    neupod_panic(NEUPOD_PANIC_ASSERT, where);
}
