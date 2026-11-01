// SPDX-License-Identifier: MPL-2.0
/**
 * @file clickwheel_frame.h
 * @brief Portable iPod click-wheel frame decoder and frame-alignment helper.
 *
 * Protocol facts (from Rockbox): the wheel streams 32-bit frames, LSB first. A frame is valid
 * iff (frame & CLICKWHEEL_FRAME_MASK) == CLICKWHEEL_FRAME_MAGIC. Buttons are in bits 8..12,
 * finger position in bits 16..22, bit 30 is "finger on wheel".
 *
 * Pure C, no platform dependencies: linked by the HALs, the host simulator and the tests.
 */
#ifndef NEUPOD_CLICKWHEEL_FRAME_H
#define NEUPOD_CLICKWHEEL_FRAME_H

#include "hal_input.h"
#include "hal_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Bits that must match CLICKWHEEL_FRAME_MAGIC: bit 31 and the low byte. */
#define CLICKWHEEL_FRAME_MASK 0x800000FFu
/** Required value of (frame & CLICKWHEEL_FRAME_MASK). */
#define CLICKWHEEL_FRAME_MAGIC 0x8000001Au

/** Button field: bits 8..12 (Select=8, Right=9, Left=10, Play=11, Menu=12). */
#define CLICKWHEEL_BUTTON_SHIFT 8u
#define CLICKWHEEL_BUTTON_MASK 0x1Fu
/** Finger position field: bits 16..22, 0..95 when valid. */
#define CLICKWHEEL_POSITION_SHIFT 16u
#define CLICKWHEEL_POSITION_MASK 0x7Fu
/** Bit 30: finger is on the wheel. */
#define CLICKWHEEL_TOUCH_BIT (1u << 30)

/** Packet the host sends at start-up and after Hold is released. */
#define CLICKWHEEL_INIT_PACKET 0x8000023Au

/**
 * Decode one 32-bit frame.
 *
 * Rejected (out->valid == false, all other fields zeroed, returns false) when the magic
 * check fails, or when the finger is touching the wheel and the position is >= 96 (no such
 * wheel position exists, so the frame is corrupt even though the magic matched). When the
 * finger is not touching, the position field is not meaningful and is reported as 0.
 *
 * @param out may be NULL to only test validity.
 * @return true iff the frame is valid.
 */
bool clickwheel_decode(uint32_t frame, hal_wheel_state *out);

/** Build the frame a wheel would send for @p state (host simulator and tests). */
uint32_t clickwheel_encode(const hal_wheel_state *state);

/**
 * Frame-alignment finder. Frame boundaries on the wire are not signalled, so the receiver
 * slides a 32-bit window over the LSB-first bit stream until the magic check passes (a
 * candidate), then requires the next frame exactly 32 bits later to pass too. Only then is
 * alignment locked and frames reported (from the second frame on, so the first frame after
 * joining a stream is consumed). A lost frame while locked drops back to searching.
 *
 * The magic only has 9 fixed bits, so ~1 in 512 random windows pass; requiring two in a row
 * makes a false report ~1 in 262144 per candidate.
 * Zero-initialise (or clickwheel_sync_init) before use.
 */
typedef enum {
    CLICKWHEEL_SYNC_SEARCH = 0, /**< Sliding, no candidate. */
    CLICKWHEEL_SYNC_CANDIDATE,  /**< One valid window seen, confirming 32 bits later. */
    CLICKWHEEL_SYNC_LOCKED,     /**< Aligned; frames are reported. */
} clickwheel_sync_state;

typedef struct {
    uint32_t window;   /**< Last 32 bits, newest in bit 31 (LSB-first stream shifted right). */
    uint8_t filled;    /**< Bits received so far, saturating at 32. */
    uint8_t countdown; /**< Bits until the next expected frame boundary. */
    clickwheel_sync_state state;
} clickwheel_sync;

void clickwheel_sync_init(clickwheel_sync *s);

/**
 * Push one stream bit (@p bit nonzero = 1, in arrival order).
 * @return true if this bit completed a valid frame; @p out (if non-NULL) receives it.
 */
bool clickwheel_sync_push_bit(clickwheel_sync *s, unsigned bit, hal_wheel_state *out);

/**
 * Push a 32-bit word whose bit 0 arrived first (PIO shift-right autopush order).
 * Words need not be frame aligned.
 * @return number of valid frames completed by this word (at most 1); @p out (if non-NULL) is written
 *         with the last one only when the return value is non-zero.
 */
unsigned clickwheel_sync_push_word(clickwheel_sync *s, uint32_t word, hal_wheel_state *out);

#ifdef __cplusplus
}
#endif

#endif // NEUPOD_CLICKWHEEL_FRAME_H
