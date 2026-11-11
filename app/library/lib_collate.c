// SPDX-License-Identifier: MPL-2.0
// Collation for the library index (docs/adr/0017-music-library-index.md, "Sort"): a byte key per name such that memcmp order is
// the display order. No locale, no heap.
//
// Key bytes, one per character, ordered by class:
//   0x20..0x7E   ASCII symbols and space, as themselves (so a space sorts first)
//   0x7F + UTF-8 any other non-letter character: a marker then its UTF-8 bytes, so it sorts by code point after the ASCII symbols
//   0xB0..0xB9   digits
//   0xC0..0xD9   letters a..z (case and accents folded away)
// A key never contains 0x00, so zero padding sorts a prefix first.
#include <string.h>

#include "library.h"

#define K_MARK 0x7Fu
#define K_DIGIT 0xB0u
#define K_LETTER 0xC0u

typedef struct {
    uint16_t lo, hi; /* code point range */
    char s[3];       /* folded ASCII letters, 1 or 2 */
} fold_t;

// Latin-1 Supplement and Latin Extended-A. Not listed (U+00D7 x, U+00F7 /): symbols, kept as they are.
static const fold_t k_fold[] = {
    {0xC0, 0xC5, "a"},  {0xC6, 0xC6, "ae"}, {0xC7, 0xC7, "c"},  {0xC8, 0xCB, "e"},  {0xCC, 0xCF, "i"},  {0xD0, 0xD0, "d"},
    {0xD1, 0xD1, "n"},  {0xD2, 0xD6, "o"},  {0xD8, 0xD8, "o"},  {0xD9, 0xDC, "u"},  {0xDD, 0xDD, "y"},  {0xDE, 0xDE, "th"},
    {0xDF, 0xDF, "ss"}, {0xE0, 0xE5, "a"},  {0xE6, 0xE6, "ae"}, {0xE7, 0xE7, "c"},  {0xE8, 0xEB, "e"},  {0xEC, 0xEF, "i"},
    {0xF0, 0xF0, "d"},  {0xF1, 0xF1, "n"},  {0xF2, 0xF6, "o"},  {0xF8, 0xF8, "o"},  {0xF9, 0xFC, "u"},  {0xFD, 0xFD, "y"},
    {0xFE, 0xFE, "th"}, {0xFF, 0xFF, "y"},
    {0x100, 0x105, "a"}, {0x106, 0x10D, "c"}, {0x10E, 0x111, "d"}, {0x112, 0x11B, "e"}, {0x11C, 0x123, "g"}, {0x124, 0x127, "h"},
    {0x128, 0x131, "i"}, {0x132, 0x133, "ij"}, {0x134, 0x135, "j"}, {0x136, 0x138, "k"}, {0x139, 0x142, "l"}, {0x143, 0x14B, "n"},
    {0x14C, 0x151, "o"}, {0x152, 0x153, "oe"}, {0x154, 0x159, "r"}, {0x15A, 0x161, "s"}, {0x162, 0x167, "t"}, {0x168, 0x173, "u"},
    {0x174, 0x175, "w"}, {0x176, 0x178, "y"}, {0x179, 0x17E, "z"}, {0x17F, 0x17F, "s"},
};

static const char *fold_lookup(uint32_t cp) {
    if (cp < 0xC0 || cp > 0x17F) return NULL;
    for (size_t i = 0; i < sizeof k_fold / sizeof k_fold[0]; i++) {
        if (cp >= k_fold[i].lo && cp <= k_fold[i].hi) return k_fold[i].s;
    }
    return NULL;
}

// Next UTF-8 character; malformed bytes come back as themselves (as Latin-1), so every byte is consumed.
static uint32_t next_cp(const uint8_t **pp) {
    const uint8_t *p = *pp;
    uint32_t c = *p++;
    if (c >= 0xC2 && c < 0xE0 && (p[0] & 0xC0) == 0x80) {
        c = ((c & 0x1Fu) << 6) | (p[0] & 0x3Fu);
        p += 1;
    } else if (c >= 0xE0 && c < 0xF0 && (p[0] & 0xC0) == 0x80 && (p[1] & 0xC0) == 0x80) {
        c = ((c & 0x0Fu) << 12) | ((uint32_t)(p[0] & 0x3Fu) << 6) | (p[1] & 0x3Fu);
        p += 2;
    } else if (c >= 0xF0 && c < 0xF5 && (p[0] & 0xC0) == 0x80 && (p[1] & 0xC0) == 0x80 && (p[2] & 0xC0) == 0x80) {
        c = ((c & 0x07u) << 18) | ((uint32_t)(p[0] & 0x3Fu) << 12) | ((uint32_t)(p[1] & 0x3Fu) << 6) | (p[2] & 0x3Fu);
        p += 3;
    }
    *pp = p;
    return c;
}

static size_t put(uint8_t *key, size_t cap, size_t n, uint8_t b) {
    if (n < cap) key[n] = b;
    return n + 1;
}

size_t lib_collate_key(const char *name, const char *sort, uint8_t *key, size_t cap) {
    const char *src = (sort != NULL && sort[0] != '\0') ? sort : name;
    if (cap == 0) return 0;
    if ((src[0] == 'T' || src[0] == 't') && (src[1] == 'H' || src[1] == 'h') && (src[2] == 'E' || src[2] == 'e') && src[3] == ' ' && src[4] != '\0') {
        src += 4; // "The Beatles" sorts under B
    }
    size_t n = 0;
    const uint8_t *p = (const uint8_t *)src;
    while (*p != 0 && n < cap) {
        uint32_t cp = next_cp(&p);
        if (cp < 0x80) {
            if (cp >= 'a' && cp <= 'z') n = put(key, cap, n, (uint8_t)(K_LETTER + (cp - 'a')));
            else if (cp >= 'A' && cp <= 'Z') n = put(key, cap, n, (uint8_t)(K_LETTER + (cp - 'A')));
            else if (cp >= '0' && cp <= '9') n = put(key, cap, n, (uint8_t)(K_DIGIT + (cp - '0')));
            else if (cp >= 0x20 && cp < 0x7F) n = put(key, cap, n, (uint8_t)cp);
            // control characters and DEL do not sort
            continue;
        }
        const char *f = fold_lookup(cp);
        if (f != NULL) {
            for (; *f != '\0'; f++) n = put(key, cap, n, (uint8_t)(K_LETTER + (*f - 'a')));
            continue;
        }
        n = put(key, cap, n, K_MARK); // everything else: by code point, as UTF-8
        if (cp < 0x800) {
            n = put(key, cap, n, (uint8_t)(0xC0 | (cp >> 6)));
            n = put(key, cap, n, (uint8_t)(0x80 | (cp & 0x3F)));
        } else if (cp < 0x10000) {
            n = put(key, cap, n, (uint8_t)(0xE0 | (cp >> 12)));
            n = put(key, cap, n, (uint8_t)(0x80 | ((cp >> 6) & 0x3F)));
            n = put(key, cap, n, (uint8_t)(0x80 | (cp & 0x3F)));
        } else {
            n = put(key, cap, n, (uint8_t)(0xF0 | (cp >> 18)));
            n = put(key, cap, n, (uint8_t)(0x80 | ((cp >> 12) & 0x3F)));
            n = put(key, cap, n, (uint8_t)(0x80 | ((cp >> 6) & 0x3F)));
            n = put(key, cap, n, (uint8_t)(0x80 | (cp & 0x3F)));
        }
    }
    if (n < cap) key[n] = 0; // a key always ends in a zero byte when it fits: lib_collate_bucket() of an empty name is 0
    return n < cap ? n : cap;
}

int lib_collate_cmp(const char *a, const char *sa, const char *b, const char *sb) {
    uint8_t ka[LIB_KEY_MAX], kb[LIB_KEY_MAX];
    size_t na = lib_collate_key(a, sa, ka, sizeof ka), nb = lib_collate_key(b, sb, kb, sizeof kb);
    size_t n = na < nb ? na : nb;
    int c = memcmp(ka, kb, n);
    if (c != 0) return c < 0 ? -1 : 1;
    return na == nb ? 0 : (na < nb ? -1 : 1);
}

unsigned lib_collate_bucket(const uint8_t *key) {
    return key[0] >= K_LETTER && key[0] < K_LETTER + 26u ? 1u + (unsigned)(key[0] - K_LETTER) : 0u;
}
