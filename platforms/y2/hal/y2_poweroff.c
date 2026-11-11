// SPDX-License-Identifier: MPL-2.0
#define _GNU_SOURCE // POSIX_SPAWN_SETSID
#include "y2_poweroff.h"

#include <signal.h>
#include <spawn.h>
#include <stdlib.h>

extern char **environ;

const char *y2_poweroff_command(void) {
    const char *e = getenv(Y2_POWEROFF_ENV);
    return (e != NULL && e[0] != '\0') ? e : Y2_POWEROFF_CMD;
}

const char *y2_reboot_command(void) {
    const char *e = getenv(Y2_REBOOT_ENV);
    return (e != NULL && e[0] != '\0') ? e : Y2_REBOOT_CMD;
}

bool y2_spawn_detached(const char *cmd, pid_t *pid) {
    posix_spawnattr_t at;
    if (posix_spawnattr_init(&at) != 0) {
        return false;
    }
    sigset_t none;
    sigemptyset(&none); // the RTOS port blocks signals in its threads; the child must start with none blocked
    sigset_t dfl; // main() ignores SIGPIPE and the RTOS port handles SIGALRM; ignored dispositions survive exec
    sigemptyset(&dfl);
    sigaddset(&dfl, SIGPIPE);
    sigaddset(&dfl, SIGALRM);
    bool ok = posix_spawnattr_setsigmask(&at, &none) == 0 && posix_spawnattr_setsigdefault(&at, &dfl) == 0 &&
              posix_spawnattr_setflags(&at, POSIX_SPAWN_SETSID | POSIX_SPAWN_SETSIGMASK | POSIX_SPAWN_SETSIGDEF) == 0;
    pid_t p = 0;
    if (ok) {
        char *const argv[] = {(char *)cmd, NULL};
        ok = posix_spawn(&p, argv[0], NULL, &at, argv, environ) == 0;
    }
    posix_spawnattr_destroy(&at);
    if (ok && pid != NULL) {
        *pid = p;
    }
    return ok;
}

bool y2_poweroff_spawn(pid_t *pid) { return y2_spawn_detached(y2_poweroff_command(), pid); }

bool y2_reboot_spawn(pid_t *pid) { return y2_spawn_detached(y2_reboot_command(), pid); }
