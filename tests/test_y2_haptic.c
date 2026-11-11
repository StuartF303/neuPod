// SPDX-License-Identifier: MPL-2.0
// Y2 haptics logic: effect -> pulse length, the override string, the coalescing gate and the timed_output write
// (against a fake sysfs file).
#include "unit.h"
#include "y2_fake_fs.h"
#include "y2_haptic.h"

static void read_file(const char *path, char *out, size_t cap) {
    FILE *f = fopen(path, "r");
    out[0] = '\0';
    if (f != NULL) {
        size_t n = fread(out, 1, cap - 1, f);
        out[n] = '\0';
        fclose(f);
    }
}

TEST(defaults_are_one_table) {
    y2_haptic_table_t t;
    y2_haptic_table_default(&t);
    ASSERT_EQ(15, t.ms[Y2_HAPTIC_TICK]);
    ASSERT_EQ(30, t.ms[Y2_HAPTIC_CLICK]);
    ASSERT_EQ(60, t.ms[Y2_HAPTIC_STRONG]);
}

TEST(effect_ids_map_to_kinds) {
    ASSERT_EQ(Y2_HAPTIC_TICK, y2_haptic_kind_for_effect(3)); // the app's ERM click effect
    ASSERT_EQ(Y2_HAPTIC_TICK, y2_haptic_kind_for_effect(24));
    ASSERT_EQ(Y2_HAPTIC_STRONG, y2_haptic_kind_for_effect(1));
    ASSERT_EQ(Y2_HAPTIC_STRONG, y2_haptic_kind_for_effect(4)); // the app's LRA click effect
    ASSERT_EQ(Y2_HAPTIC_CLICK, y2_haptic_kind_for_effect(7));
    ASSERT_EQ(Y2_HAPTIC_CLICK, y2_haptic_kind_for_effect(100));
}

TEST(env_override_parsing) {
    y2_haptic_table_t t;
    y2_haptic_table_default(&t);
    ASSERT_EQ(0, y2_haptic_table_parse(&t, NULL));
    ASSERT_EQ(0, y2_haptic_table_parse(&t, ""));
    ASSERT_EQ(15, t.ms[0]);

    ASSERT_EQ(0, y2_haptic_table_parse(&t, "20,40,90"));
    ASSERT_EQ(20, t.ms[0]);
    ASSERT_EQ(40, t.ms[1]);
    ASSERT_EQ(90, t.ms[2]);

    y2_haptic_table_default(&t);
    ASSERT_EQ(0, y2_haptic_table_parse(&t, " 25 ,, 80\n")); // blanks, an empty field keeps its default
    ASSERT_EQ(25, t.ms[0]);
    ASSERT_EQ(30, t.ms[1]);
    ASSERT_EQ(80, t.ms[2]);

    y2_haptic_table_default(&t);
    ASSERT_EQ(0, y2_haptic_table_parse(&t, "50")); // fewer fields: the rest keep defaults
    ASSERT_EQ(50, t.ms[0]);
    ASSERT_EQ(30, t.ms[1]);
    ASSERT_EQ(500, (int)Y2_HAPTIC_MAX_MS);
    ASSERT_EQ(0, y2_haptic_table_parse(&t, "1,500,500")); // the limits are accepted
    ASSERT_EQ(1, t.ms[0]);
    ASSERT_EQ(500, t.ms[1]);
}

TEST(bad_fields_keep_defaults_and_are_counted) {
    y2_haptic_table_t t;
    y2_haptic_table_default(&t);
    ASSERT_EQ(3, y2_haptic_table_parse(&t, "abc,0,501")); // not a number, zero, over the limit
    ASSERT_EQ(15, t.ms[0]);
    ASSERT_EQ(30, t.ms[1]);
    ASSERT_EQ(60, t.ms[2]);
    ASSERT_EQ(2, y2_haptic_table_parse(&t, "12x,-5,70")); // the good field still applies
    ASSERT_EQ(70, t.ms[2]);
    ASSERT_EQ(15, t.ms[0]);
    ASSERT_EQ(1, y2_haptic_table_parse(&t, "10,20,30,40")); // a fourth field
    ASSERT_EQ(10, t.ms[0]);
    ASSERT_EQ(1, y2_haptic_table_parse(&t, "99999999999,,")); // overflow cannot wrap into range
    ASSERT_EQ(10, t.ms[0]);
}

TEST(gate_coalesces_a_fast_scroll) {
    y2_haptic_gate_t g = {0};
    ASSERT_TRUE(y2_haptic_gate_pass(&g, 1000, 15)); // first tick fires; next may start at 1060
    ASSERT_FALSE(y2_haptic_gate_pass(&g, 1010, 15));
    ASSERT_FALSE(y2_haptic_gate_pass(&g, 1059, 15));
    ASSERT_TRUE(y2_haptic_gate_pass(&g, 1060, 15));
    // a wheel spinning every 10 ms for a second: at most one pulse per 60 ms, so the motor is off at least 45 ms in 60
    int fired = 0;
    y2_haptic_gate_t s = {0};
    for (uint32_t t = 0; t < 1000; t += 10) {
        fired += y2_haptic_gate_pass(&s, t, 15) ? 1 : 0;
    }
    ASSERT_EQ(17, fired);
    ASSERT_TRUE(fired * 15 < 1000 / 3); // motor on for under a third of the time
    // a longer pulse holds the gate longer
    y2_haptic_gate_t l = {0};
    ASSERT_TRUE(y2_haptic_gate_pass(&l, 0, 60));
    ASSERT_FALSE(y2_haptic_gate_pass(&l, 104, 15));
    ASSERT_TRUE(y2_haptic_gate_pass(&l, 105, 15));
}

TEST(gate_survives_clock_wrap) {
    y2_haptic_gate_t g = {0};
    ASSERT_TRUE(y2_haptic_gate_pass(&g, 0xFFFFFFF0u, 15)); // free_at wraps past zero
    ASSERT_FALSE(y2_haptic_gate_pass(&g, 0xFFFFFFFFu, 15));
    ASSERT_FALSE(y2_haptic_gate_pass(&g, 20, 15));
    ASSERT_TRUE(y2_haptic_gate_pass(&g, 60, 15));
}

TEST(writes_milliseconds_to_the_node) {
    fs_new();
    fs_write("sys/class/timed_output/vibrator/enable", "");
    char p[400], buf[32];
    fs_path("sys/class/timed_output/vibrator/enable", p, sizeof p);
    ASSERT_TRUE(y2_haptic_node_ok(p));
    ASSERT_TRUE(y2_haptic_write(p, 15));
    read_file(p, buf, sizeof buf);
    ASSERT_EQ(0, strcmp(buf, "15\n"));
    ASSERT_TRUE(y2_haptic_write(p, 0)); // the safe state
    read_file(p, buf, sizeof buf);
    ASSERT_EQ(0, strncmp(buf, "0\n", 2)); // a regular file is not truncated like sysfs (a stale tail may remain)
    fs_destroy();
}

TEST(missing_node_is_reported) {
    ASSERT_FALSE(y2_haptic_node_ok("/nonexistent/timed_output/vibrator/enable"));
    ASSERT_FALSE(y2_haptic_write("/nonexistent/timed_output/vibrator/enable", 15));
}

int main(void) {
    RUN(defaults_are_one_table);
    RUN(effect_ids_map_to_kinds);
    RUN(env_override_parsing);
    RUN(bad_fields_keep_defaults_and_are_counted);
    RUN(gate_coalesces_a_fast_scroll);
    RUN(gate_survives_clock_wrap);
    RUN(writes_milliseconds_to_the_node);
    RUN(missing_node_is_reported);
    return RESULT();
}
