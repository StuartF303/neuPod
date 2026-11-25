// SPDX-License-Identifier: MPL-2.0
/**
 * @file y2_poweroff.h
 * @brief Request a system power-off by starting the init system's poweroff command, detached from this process.
 *
 * The command is /sbin/poweroff (busybox init then runs the rc stop scripts, including S91neupod-fw, which SIGTERMs this
 * firmware's process group). It is started in its own session so that group kill cannot take it down, and it is never
 * waited for, so the firmware can exit straight away and neither side blocks on the other. The command can be replaced
 * with the environment variable NEUPOD_Y2_POWEROFF (an absolute path, no arguments) or at build time with
 * -DY2_POWEROFF_CMD="..."; host tests use it so they never power off the PC.
 */
#ifndef NEUPOD_Y2_POWEROFF_H
#define NEUPOD_Y2_POWEROFF_H

#include <stdbool.h>
#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef Y2_POWEROFF_CMD
#define Y2_POWEROFF_CMD "/sbin/poweroff"
#endif
#define Y2_POWEROFF_ENV "NEUPOD_Y2_POWEROFF"

/** The command that would be run: the environment override if set and non-empty, else Y2_POWEROFF_CMD. */
const char *y2_poweroff_command(void);

#ifndef Y2_REBOOT_CMD
#define Y2_REBOOT_CMD "/sbin/reboot"
#endif
#define Y2_REBOOT_ENV "NEUPOD_Y2_REBOOT"

/** The reboot command that would be run: the environment override if set and non-empty, else Y2_REBOOT_CMD. */
const char *y2_reboot_command(void);

/** Start @p cmd (an absolute path, no arguments) detached, as for poweroff. True if started; never waits. */
bool y2_spawn_detached(const char *cmd, pid_t *pid);

/** Start the reboot command detached (charging mode: Power held 2 s). Same rules as y2_poweroff_spawn(). */
bool y2_reboot_spawn(pid_t *pid);

/** Start the poweroff command detached. True if it was started (@p pid, may be NULL, gets its pid). Never waits. */
bool y2_poweroff_spawn(pid_t *pid);

#ifdef __cplusplus
}
#endif

#endif // NEUPOD_Y2_POWEROFF_H
