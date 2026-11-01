// SPDX-License-Identifier: MPL-2.0
#include "clickwheel_frame.h"

bool clickwheel_decode(uint32_t frame, hal_wheel_state *out) {
    hal_wheel_state st = {0};
    bool ok = (frame & CLICKWHEEL_FRAME_MASK) == CLICKWHEEL_FRAME_MAGIC;
    if (ok) {
        st.buttons = (uint8_t)((frame >> CLICKWHEEL_BUTTON_SHIFT) & CLICKWHEEL_BUTTON_MASK);
        st.touched = (frame & CLICKWHEEL_TOUCH_BIT) != 0;
        if (st.touched) {
            st.position = (uint8_t)((frame >> CLICKWHEEL_POSITION_SHIFT) & CLICKWHEEL_POSITION_MASK);
            if (st.position >= HAL_WHEEL_POSITIONS) {
                ok = false;
            }
        }
    }
    if (!ok) {
        st = (hal_wheel_state){0};
    }
    st.valid = ok;
    if (out != NULL) {
        *out = st;
    }
    return ok;
}

uint32_t clickwheel_encode(const hal_wheel_state *state) {
    uint32_t f = CLICKWHEEL_FRAME_MAGIC;
    f |= ((uint32_t)state->buttons & CLICKWHEEL_BUTTON_MASK) << CLICKWHEEL_BUTTON_SHIFT;
    if (state->touched) {
        f |= CLICKWHEEL_TOUCH_BIT;
        f |= ((uint32_t)state->position & CLICKWHEEL_POSITION_MASK) << CLICKWHEEL_POSITION_SHIFT;
    }
    return f;
}

void clickwheel_sync_init(clickwheel_sync *s) { *s = (clickwheel_sync){0}; }

bool clickwheel_sync_push_bit(clickwheel_sync *s, unsigned bit, hal_wheel_state *out) {
    s->window = (s->window >> 1) | (bit ? 0x80000000u : 0u);
    if (s->filled < 32) {
        s->filled++;
        if (s->filled < 32) {
            return false;
        }
    }

    hal_wheel_state st;
    bool ok = clickwheel_decode(s->window, &st);

    if (s->state == CLICKWHEEL_SYNC_LOCKED) {
        if (--s->countdown > 0) {
            return false;
        }
        // Expected frame boundary.
        if (ok) {
            s->countdown = 32;
            if (out != NULL) {
                *out = st;
            }
            return true;
        }
        s->state = CLICKWHEEL_SYNC_SEARCH; // lost alignment; resume sliding from the next bit
        return false;
    }

    if (s->state == CLICKWHEEL_SYNC_CANDIDATE) {
        if (--s->countdown > 0) {
            return false;
        }
        if (ok) { // second consecutive frame at +32 bits: alignment confirmed
            s->state = CLICKWHEEL_SYNC_LOCKED;
            s->countdown = 32;
            if (out != NULL) {
                *out = st;
            }
            return true;
        }
        s->state = CLICKWHEEL_SYNC_SEARCH; // chance match; keep sliding (this window is invalid)
        return false;
    }

    if (ok) {
        s->state = CLICKWHEEL_SYNC_CANDIDATE;
        s->countdown = 32;
    }
    return false;
}

unsigned clickwheel_sync_push_word(clickwheel_sync *s, uint32_t word, hal_wheel_state *out) {
    unsigned frames = 0;
    for (unsigned i = 0; i < 32; i++) {
        hal_wheel_state st;
        if (clickwheel_sync_push_bit(s, (word >> i) & 1u, &st)) {
            frames++;
            if (out != NULL) {
                *out = st;
            }
        }
    }
    return frames;
}
