// SPDX-License-Identifier: MPL-2.0
// Wheel input widget models: typing, cursor, pages, masking, limits, number/slider, picker, date/time, letter jump.
#include <string.h>

#include "unit.h"
#include "wheel_input.h"

static void pick_key(wi_text_t *t, const char *label) { // scroll to the key with @p label
    for (uint32_t i = 0; i < wi_text_strip_len(t); i++) {
        char k[8];
        wi_text_strip_key(t, i, k);
        if (strcmp(k, label) == 0) {
            t->sel = i;
            return;
        }
    }
    ASSERT_TRUE(0);
}
static void type(wi_text_t *t, const char *s) {
    for (; *s; s++) {
        char l[2] = {*s, 0};
        pick_key(t, l);
        wi_text_event(t, WI_EV_SELECT, 0);
    }
}

TEST(test_text_typing_and_pages) {
    wi_text_t t;
    wi_text_init(&t, "", 20, WI_CS_ANY, false, false);
    type(&t, "ab");
    ASSERT_EQ(0, strcmp(t.text, "ab"));
    pick_key(&t, "ABC");
    wi_text_event(&t, WI_EV_SELECT, 0);
    ASSERT_EQ(WI_PAGE_UPPER, t.page);
    type(&t, "Z");
    pick_key(&t, "123");
    wi_text_event(&t, WI_EV_SELECT, 0);
    type(&t, "7");
    pick_key(&t, "#+=");
    wi_text_event(&t, WI_EV_SELECT, 0);
    type(&t, "!");
    ASSERT_EQ(0, strcmp(t.text, "abZ7!"));
    pick_key(&t, "space");
    wi_text_event(&t, WI_EV_SELECT, 0);
    ASSERT_EQ(0, strcmp(t.text, "abZ7! "));
    // scroll wraps both ways
    t.sel = 0;
    wi_text_event(&t, WI_EV_SCROLL, -1);
    ASSERT_EQ(wi_text_strip_len(&t) - 1, t.sel);
    wi_text_event(&t, WI_EV_SCROLL, 1);
    ASSERT_EQ(0, t.sel);
}

TEST(test_text_cursor_backspace_cancel_confirm) {
    wi_text_t t;
    wi_text_init(&t, "abc", 10, WI_CS_ANY, false, false);
    ASSERT_EQ(3, t.cursor);
    wi_text_event(&t, WI_EV_LEFT, 0);
    wi_text_event(&t, WI_EV_MENU, 0); // deletes 'b'
    ASSERT_EQ(0, strcmp(t.text, "ac"));
    ASSERT_EQ(1, t.cursor);
    wi_text_insert(&t, 'X');
    ASSERT_EQ(0, strcmp(t.text, "aXc"));
    wi_text_event(&t, WI_EV_RIGHT, 0);
    wi_text_event(&t, WI_EV_RIGHT, 0); // clamps at the end
    ASSERT_EQ(3, t.cursor);
    wi_text_init(&t, "", 10, WI_CS_ANY, false, false);
    ASSERT_EQ(WI_CANCELLED, wi_text_event(&t, WI_EV_MENU, 0)); // empty: Menu cancels
    wi_text_insert(&t, 'q');
    ASSERT_EQ(WI_RUNNING, wi_text_event(&t, WI_EV_MENU, 0));   // non-empty: backspace
    ASSERT_EQ(WI_OK, wi_text_event(&t, WI_EV_PLAY, 0));
    pick_key(&t, "done");
    ASSERT_EQ(WI_OK, wi_text_event(&t, WI_EV_SELECT, 0));
}

TEST(test_text_limits_charset_masking_utf8) {
    wi_text_t t;
    wi_text_init(&t, "", 3, WI_CS_ANY, true, false);
    type(&t, "abcd"); // 4th rejected
    ASSERT_EQ(3, wi_text_chars(&t));
    ASSERT_TRUE(t.masked && !t.show);
    pick_key(&t, "show");
    wi_text_event(&t, WI_EV_SELECT, 0);
    ASSERT_TRUE(t.show);
    char k[8];
    pick_key(&t, "hide"); // label flips
    ASSERT_EQ(WI_KEY_SHOW, wi_text_strip_key(&t, t.sel, k));

    wi_text_init(&t, "", 8, WI_CS_DIGITS, false, false);
    ASSERT_EQ(WI_PAGE_DIGITS, t.page);
    ASSERT_FALSE(wi_text_insert(&t, 'a'));
    ASSERT_TRUE(wi_text_insert(&t, '5'));
    for (uint32_t i = 0; i < wi_text_strip_len(&t); i++) { // digits only: no page key, no space
        wi_text_strip_key(&t, i, k);
        ASSERT_TRUE(strcmp(k, "ABC") != 0 && strcmp(k, "space") != 0);
    }
    wi_text_init(&t, "", 8, WI_CS_HEX, false, false);
    ASSERT_TRUE(wi_text_insert(&t, 'f'));
    ASSERT_FALSE(wi_text_insert(&t, 'g'));

    // UTF-8: a two-byte character counts as one, backspace removes it whole, the cursor never lands inside it.
    wi_text_init(&t, "", 4, WI_CS_ANY, false, false);
    ASSERT_TRUE(wi_text_insert(&t, 0xE9)); // e acute
    ASSERT_TRUE(wi_text_insert(&t, 'x'));
    ASSERT_EQ(3, t.len);
    ASSERT_EQ(2, wi_text_chars(&t));
    wi_text_event(&t, WI_EV_LEFT, 0);
    wi_text_event(&t, WI_EV_LEFT, 0);
    ASSERT_EQ(0, t.cursor);
    wi_text_event(&t, WI_EV_RIGHT, 0);
    ASSERT_EQ(2, t.cursor);
    wi_text_event(&t, WI_EV_MENU, 0);
    ASSERT_EQ(0, strcmp(t.text, "x"));
    // initial text is not cut mid-character
    wi_text_init(&t, "caf\xC3\xA9", 10, WI_CS_ANY, false, false);
    ASSERT_EQ(5, t.len);
}

TEST(test_text_search_changed) {
    wi_text_t t;
    wi_text_init(&t, "", 20, WI_CS_ANY, false, true);
    ASSERT_FALSE(t.changed);
    type(&t, "a");
    ASSERT_TRUE(t.changed);
    t.changed = false;
    wi_text_event(&t, WI_EV_LEFT, 0);
    ASSERT_FALSE(t.changed); // moving the cursor is not an edit
    wi_text_event(&t, WI_EV_RIGHT, 0);
    wi_text_event(&t, WI_EV_MENU, 0);
    ASSERT_TRUE(t.changed);
}

TEST(test_number_and_slider) {
    wi_number_t n;
    char s[24];
    wi_number_init(&n, -300, -800, 0, 5, 1, "dB");
    wi_number_format(&n, s, sizeof s);
    ASSERT_EQ(0, strcmp(s, "-30.0 dB"));
    wi_number_event(&n, WI_EV_SCROLL, 4); // accelerated steps multiply
    ASSERT_EQ(-280, n.value);
    wi_number_event(&n, WI_EV_SCROLL, 1000);
    ASSERT_EQ(0, n.value); // clamped
    wi_number_event(&n, WI_EV_SCROLL, -100000);
    ASSERT_EQ(-800, n.value);
    ASSERT_EQ(WI_CANCELLED, wi_number_event(&n, WI_EV_MENU, 0));
    ASSERT_EQ(-300, n.value); // cancel restores
    wi_number_init(&n, 5, 0, 10, 1, 0, "min");
    wi_number_format(&n, s, sizeof s);
    ASSERT_EQ(0, strcmp(s, "5 min"));
    ASSERT_EQ(WI_OK, wi_number_event(&n, WI_EV_SELECT, 0));
    wi_number_init(&n, 7, 0, 100, 1, 2, "");
    wi_number_format(&n, s, sizeof s);
    ASSERT_EQ(0, strcmp(s, "0.07"));

    wi_slider_t sl;
    wi_slider_init(&sl, 100, 5, 100, 5, "%");
    wi_slider_event(&sl, WI_EV_SCROLL, 3);
    ASSERT_EQ(100, sl.value);
    ASSERT_FALSE(sl.changed);
    wi_slider_event(&sl, WI_EV_SCROLL, -2);
    ASSERT_EQ(90, sl.value);
    ASSERT_TRUE(sl.changed);
    sl.changed = false;
    ASSERT_EQ(WI_CANCELLED, wi_slider_event(&sl, WI_EV_MENU, 0));
    ASSERT_EQ(100, sl.value);
    ASSERT_TRUE(sl.changed); // the preview must be reverted by the caller
}

TEST(test_picker) {
    wi_picker_t p;
    wi_picker_init(&p, 3, 0, false, false);
    wi_picker_event(&p, WI_EV_SCROLL, -1);
    ASSERT_EQ(0, p.sel);
    wi_picker_event(&p, WI_EV_SCROLL, 5);
    ASSERT_EQ(2, p.sel); // clamp
    ASSERT_EQ(WI_OK, wi_picker_event(&p, WI_EV_SELECT, 0));
    wi_picker_init(&p, 3, 0, false, true);
    wi_picker_event(&p, WI_EV_SCROLL, -1);
    ASSERT_EQ(2, p.sel); // wrap
    wi_picker_event(&p, WI_EV_SCROLL, 4);
    ASSERT_EQ(0, p.sel);
    wi_picker_init(&p, 4, 1, true, false);
    ASSERT_EQ(WI_RUNNING, wi_picker_event(&p, WI_EV_SELECT, 0));
    wi_picker_event(&p, WI_EV_SCROLL, 2);
    wi_picker_event(&p, WI_EV_SELECT, 0);
    ASSERT_EQ(0xAu, p.mask);
    wi_picker_event(&p, WI_EV_SELECT, 0);
    ASSERT_EQ(0x2u, p.mask);
    ASSERT_EQ(WI_OK, wi_picker_event(&p, WI_EV_PLAY, 0));
    ASSERT_EQ(WI_CANCELLED, wi_picker_event(&p, WI_EV_MENU, 0));
    wi_picker_init(&p, 0, 0, false, false);
    ASSERT_EQ(WI_RUNNING, wi_picker_event(&p, WI_EV_SELECT, 0)); // empty: nothing to choose
}

TEST(test_datetime) {
    ASSERT_TRUE(wi_is_leap(2024));
    ASSERT_FALSE(wi_is_leap(2100));
    ASSERT_TRUE(wi_is_leap(2000));
    ASSERT_FALSE(wi_is_leap(2023));
    ASSERT_EQ(29, wi_days_in_month(2024, 2));
    ASSERT_EQ(28, wi_days_in_month(2023, 2));
    ASSERT_EQ(30, wi_days_in_month(2026, 4));
    wi_datetime_t t;
    wi_datetime_init(&t, true, true, 2024, 2, 29, 23, 59);
    t.field = 2; // year to a non-leap year: 29 Feb becomes 28 Feb
    wi_datetime_event(&t, WI_EV_SCROLL, 1);
    ASSERT_EQ(2025, t.y);
    ASSERT_EQ(28, t.d);
    wi_datetime_init(&t, true, true, 2026, 1, 31, 0, 0);
    t.field = 1; // Jan 31 -> Feb: day clamps
    wi_datetime_event(&t, WI_EV_SCROLL, 1);
    ASSERT_EQ(2, t.mo);
    ASSERT_EQ(28, t.d);
    t.field = 0; // day wraps inside the month
    wi_datetime_event(&t, WI_EV_SCROLL, 1);
    ASSERT_EQ(1, t.d);
    t.field = 3;
    wi_datetime_event(&t, WI_EV_SCROLL, -1);
    ASSERT_EQ(23, t.h);
    t.field = 4;
    wi_datetime_event(&t, WI_EV_SCROLL, 61);
    ASSERT_EQ(1, t.mi);
    // fields: time only starts at the hour; Select walks on and confirms on the last one
    wi_datetime_init(&t, false, true, 2026, 1, 1, 8, 30);
    ASSERT_EQ(3, t.field);
    ASSERT_EQ(WI_RUNNING, wi_datetime_event(&t, WI_EV_SELECT, 0));
    ASSERT_EQ(4, t.field);
    ASSERT_EQ(WI_OK, wi_datetime_event(&t, WI_EV_SELECT, 0));
    ASSERT_EQ(WI_CANCELLED, wi_datetime_event(&t, WI_EV_MENU, 0));
}

static char initial_of(void *ctx, int32_t i) { return ((const char *)ctx)[i]; }

TEST(test_letter_jump) {
    wi_letterjump_t j;
    wi_letterjump_init(&j);
    ASSERT_FALSE(wi_letterjump_scroll(&j, 1, 1000, 'a')); // slow: no overlay
    ASSERT_FALSE(wi_letterjump_scroll(&j, 1, 1100, 'a'));
    ASSERT_TRUE(wi_letterjump_scroll(&j, 5, 1200, 'm')); // fast
    ASSERT_EQ('M', j.letter);
    wi_letterjump_tick(&j, 1500);
    ASSERT_TRUE(j.active);
    wi_letterjump_tick(&j, 2000);
    ASSERT_FALSE(j.active);
    ASSERT_FALSE(wi_letterjump_scroll(&j, 1, 5000, '7')); // energy decayed
    wi_letterjump_scroll(&j, 8, 5050, '7');
    ASSERT_EQ('#', j.letter);
    const char *s = "#AABBBDEEFZ"; // sorted initials; '#' first
    ASSERT_EQ(0, wi_letter_first_index(11, initial_of, (void *)s, '#'));
    ASSERT_EQ(1, wi_letter_first_index(11, initial_of, (void *)s, 'A'));
    ASSERT_EQ(3, wi_letter_first_index(11, initial_of, (void *)s, 'B'));
    ASSERT_EQ(6, wi_letter_first_index(11, initial_of, (void *)s, 'C')); // no C: next letter present (D)
    ASSERT_EQ(10, wi_letter_first_index(11, initial_of, (void *)s, 'Z'));
    ASSERT_EQ(10, wi_letter_first_index(11, initial_of, (void *)s, 'z')); // case-insensitive
    ASSERT_EQ(0, wi_letter_first_index(0, initial_of, (void *)s, 'A'));
}

int main(void) {
    RUN(test_text_typing_and_pages);
    RUN(test_text_cursor_backspace_cancel_confirm);
    RUN(test_text_limits_charset_masking_utf8);
    RUN(test_text_search_changed);
    RUN(test_number_and_slider);
    RUN(test_picker);
    RUN(test_datetime);
    RUN(test_letter_jump);
    return RESULT();
}
