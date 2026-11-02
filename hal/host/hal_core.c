// SPDX-License-Identifier: MPL-2.0
// Host stand-in for core 1: a native thread (not an RTOS task) running the audio core loop.
#include <pthread.h>

#include "hal_core.h"

size_t hal_core1_stack_free_min(void) { return 0; }

static void *core1_thread(void *arg) {
    ((void (*)(void))arg)();
    return NULL;
}

hal_status_t hal_core1_launch(void (*entry)(void)) {
    if (entry == NULL) {
        return HAL_EINVAL;
    }
    pthread_t t;
    if (pthread_create(&t, NULL, core1_thread, (void *)entry) != 0) {
        return HAL_ERR;
    }
    pthread_detach(t);
    return HAL_OK;
}
