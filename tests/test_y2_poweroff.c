// SPDX-License-Identifier: MPL-2.0
// Y2 poweroff request: the command is overridable (so the host never powers anything off) and is started detached, in its
// own session, with SIGPIPE and SIGALRM back at their defaults.
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include "unit.h"
#include "y2_poweroff.h"

// Writes an executable scratch script; the "command" is only ever this file, never the real /sbin/poweroff.
static void make_script(const char *path, const char *body) {
    FILE *f = fopen(path, "w");
    if (f != NULL) {
        fputs(body, f);
        fclose(f);
    }
    chmod(path, 0700);
}

TEST(default_command_is_sbin_poweroff_and_env_overrides) {
    unsetenv(Y2_POWEROFF_ENV);
    ASSERT_TRUE(strcmp(y2_poweroff_command(), Y2_POWEROFF_CMD) == 0);
    setenv(Y2_POWEROFF_ENV, "", 1);
    ASSERT_TRUE(strcmp(y2_poweroff_command(), Y2_POWEROFF_CMD) == 0); // empty is ignored
    setenv(Y2_POWEROFF_ENV, "/some/override", 1);
    ASSERT_TRUE(strcmp(y2_poweroff_command(), "/some/override") == 0);
}

TEST(spawn_runs_the_override_in_its_own_session_with_default_signals) {
    char dir[] = "/tmp/y2poXXXXXX";
    ASSERT_TRUE(mkdtemp(dir) != NULL);
    char script[96], out[96];
    snprintf(script, sizeof script, "%s/poweroff.sh", dir);
    snprintf(out, sizeof out, "%s/out", dir);
    // sid of the script itself ($$), its pid, and whether SIGPIPE is ignored (SigIgn bit 13 in /proc/self/status is for sh).
    char body[512];
    snprintf(body, sizeof body,
             "#!/bin/sh\necho \"$$ $(ps -o sid= -p $$ | tr -d ' ') $(grep SigIgn /proc/$$/status | tr -d '\\t ')\" > %s\n", out);
    make_script(script, body);
    signal(SIGPIPE, SIG_IGN); // as main() does; the child must not inherit it
    setenv(Y2_POWEROFF_ENV, script, 1);
    pid_t pid = 0;
    ASSERT_TRUE(y2_poweroff_spawn(&pid));
    ASSERT_TRUE(pid > 0);
    int st = -1;
    ASSERT_EQ(pid, waitpid(pid, &st, 0));
    ASSERT_TRUE(WIFEXITED(st) && WEXITSTATUS(st) == 0);
    long p = 0, sid = -1;
    char sig[64] = {0};
    FILE *f = fopen(out, "r");
    ASSERT_TRUE(f != NULL);
    ASSERT_EQ(3, fscanf(f, "%ld %ld %63s", &p, &sid, sig));
    fclose(f);
    ASSERT_EQ((long)pid, p);   // sh exec'd in place, same pid
    ASSERT_EQ((long)pid, sid); // session leader: the init script's group kill cannot reach it
    // SIGPIPE (ignored here) is default again in the child. Signals 32 and 33 (glibc's internal real-time signals,
    // bits 31/32) are masked out: under Docker's default seccomp profile (CI container jobs) they can show as
    // ignored in the child, and poweroff never uses them.
    unsigned long long ign = ~0ULL;
    ASSERT_EQ(1, sscanf(sig, "SigIgn:%llx", &ign));
    ASSERT_EQ(0ULL, ign & ~0x180000000ULL);
    unlink(script);
    unlink(out);
    rmdir(dir);
}

TEST(spawn_reports_a_missing_command) {
    setenv(Y2_POWEROFF_ENV, "/nonexistent/poweroff", 1);
    ASSERT_FALSE(y2_poweroff_spawn(NULL));
}

TEST(reboot_command_is_overridable_and_spawns_the_override) {
    unsetenv(Y2_REBOOT_ENV);
    ASSERT_TRUE(strcmp(y2_reboot_command(), "/sbin/reboot") == 0);
    char dir[] = "/tmp/y2rbXXXXXX";
    ASSERT_TRUE(mkdtemp(dir) != NULL);
    char script[96], out[96];
    snprintf(script, sizeof script, "%s/reboot.sh", dir);
    snprintf(out, sizeof out, "%s/out", dir);
    char body[256];
    snprintf(body, sizeof body, "#!/bin/sh\necho rebooted > %s\n", out);
    make_script(script, body);
    setenv(Y2_REBOOT_ENV, script, 1);
    ASSERT_TRUE(strcmp(y2_reboot_command(), script) == 0);
    pid_t pid = 0;
    ASSERT_TRUE(y2_reboot_spawn(&pid));
    int st = -1;
    ASSERT_EQ(pid, waitpid(pid, &st, 0));
    ASSERT_TRUE(access(out, F_OK) == 0);
    setenv(Y2_REBOOT_ENV, "/nonexistent/reboot", 1);
    ASSERT_FALSE(y2_reboot_spawn(NULL));
    unsetenv(Y2_REBOOT_ENV);
    unlink(script);
    unlink(out);
    rmdir(dir);
}

int main(void) {
    RUN(default_command_is_sbin_poweroff_and_env_overrides);
    RUN(spawn_runs_the_override_in_its_own_session_with_default_signals);
    RUN(spawn_reports_a_missing_command);
    RUN(reboot_command_is_overridable_and_spawns_the_override);
    return RESULT();
}
