// SPDX-License-Identifier: MPL-2.0
// The capability re-arm state machine that only the Y2 build has (caps_allow_late_ok, caps_force_degraded and the
// re-armed branch of caps_resolve). app/runtime/capabilities.c is compiled here with -DNEUPOD_PLATFORM_Y2=1; the OS mutex
// is stubbed (single thread).
#include <string.h>

#include "capabilities.h"
#include "unit.h"

os_mutex_t *os_mutex_create(void) { return (os_mutex_t *)(void *)&unit_checks; }
bool os_mutex_lock(os_mutex_t *m, uint32_t t) { (void)m; (void)t; return true; }
void os_mutex_unlock(os_mutex_t *m) { (void)m; }

static cap_state_t st(void) {
    capabilities_t c;
    caps_get(&c);
    return c.state[CAP_STORAGE];
}
static const char *txt(capabilities_t *c) {
    caps_get(c);
    return c->text[CAP_STORAGE];
}

TEST(a_degraded_capability_is_final_without_rearm) {
    caps_init();
    caps_resolve(CAP_STORAGE, false, "none", "no card");
    ASSERT_EQ(CAP_DEGRADED, st());
    ASSERT_FALSE(caps_resolve(CAP_STORAGE, true, NULL, "card ok"));
    ASSERT_EQ(CAP_DEGRADED, st());
}

TEST(allow_late_ok_lets_one_later_result_replace_it) {
    capabilities_t c;
    caps_init();
    caps_resolve(CAP_STORAGE, false, "none", "no card");
    caps_allow_late_ok(CAP_STORAGE);
    ASSERT_TRUE(caps_resolve(CAP_STORAGE, true, NULL, "card ok"));
    ASSERT_EQ(CAP_OK, st());
    ASSERT_TRUE(strcmp("card ok", txt(&c)) == 0);
    ASSERT_FALSE(caps_resolve(CAP_STORAGE, false, "x", "again")); // an OK capability is not overwritten
    ASSERT_EQ(CAP_OK, st());
}

TEST(a_rearmed_failure_replaces_the_reason_and_is_one_shot) {
    capabilities_t c;
    caps_init();
    caps_resolve(CAP_STORAGE, false, "none", "no card");
    caps_allow_late_ok(CAP_STORAGE);
    ASSERT_TRUE(caps_resolve(CAP_STORAGE, false, "nofs", "mount: no filesystem"));
    ASSERT_EQ(CAP_DEGRADED, st());
    ASSERT_TRUE(strcmp("mount: no filesystem", txt(&c)) == 0);
    ASSERT_FALSE(caps_resolve(CAP_STORAGE, true, NULL, "card ok")); // not re-armed any more
    ASSERT_EQ(CAP_DEGRADED, st());
}

TEST(allow_late_ok_is_a_no_op_unless_degraded) {
    caps_init();
    caps_allow_late_ok(CAP_STORAGE); // PENDING
    caps_resolve(CAP_STORAGE, true, NULL, "card ok");
    caps_allow_late_ok(CAP_STORAGE); // OK
    ASSERT_EQ(CAP_OK, st());
    caps_force_degraded(CAP_DISPLAY, "x", "never checked"); // PENDING: untouched
    capabilities_t c;
    caps_get(&c);
    ASSERT_EQ(CAP_PENDING, c.state[CAP_DISPLAY]);
}

TEST(force_degraded_marks_ok_degraded_and_rearms_it) {
    capabilities_t c;
    caps_init();
    caps_resolve(CAP_STORAGE, true, NULL, "card ok");
    caps_force_degraded(CAP_STORAGE, "none", "card removed");
    ASSERT_EQ(CAP_DEGRADED, st());
    ASSERT_TRUE(strcmp("card removed", txt(&c)) == 0);
    ASSERT_TRUE(caps_resolve(CAP_STORAGE, true, NULL, "card ok")); // no allow_late_ok needed: forced degrade re-arms
    ASSERT_EQ(CAP_OK, st());
}

TEST(a_timeout_still_allows_a_late_ok_as_before) {
    caps_init();
    caps_resolve_timeout(CAP_STORAGE, "slow", "timed out");
    ASSERT_EQ(CAP_DEGRADED, st());
    ASSERT_TRUE(caps_resolve(CAP_STORAGE, true, NULL, "card ok"));
    ASSERT_EQ(CAP_OK, st());
}

int main(void) {
    RUN(a_degraded_capability_is_final_without_rearm);
    RUN(allow_late_ok_lets_one_later_result_replace_it);
    RUN(a_rearmed_failure_replaces_the_reason_and_is_one_shot);
    RUN(allow_late_ok_is_a_no_op_unless_degraded);
    RUN(force_degraded_marks_ok_degraded_and_rearms_it);
    RUN(a_timeout_still_allows_a_late_ok_as_before);
    return RESULT();
}
