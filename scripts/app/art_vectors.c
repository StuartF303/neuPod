// SPDX-License-Identifier: MPL-2.0
// What the firmware makes of album art, for the phone app's tests (owner: "firmware behaviour is the rule"). Runs the
// firmware's own code compiled for the host: the tag parsers' choice of embedded picture (app/audio/meta.c:
// flac_picture / id3_picture / meta_art_offer), the order of sources and the cover file names (app/audio/art.c:
// art_sources, art_folder_rank, art_folder_pick) and whether its JPEG decoder takes a picture (art_decode, TJpgDec).
// Built and run by scripts/app/gen_art_vectors.py. Modes, one line of output per argument:
//   tags <file>...   <name>\t<offset>\t<length>\t<kind>\t<type>\t<decode>\t<sources>
//                    kind: none jpeg png other; decode: art_decode() of the chosen picture ("-" when there is none);
//                    sources: art_sources(embedded, folder_has = true), e.g. "embedded,folder" or "folder"
//   image <file>...  <name>\t<decode>\t<w>x<h>\t<pixels>   (art_decode() of the whole file, as a cover file in the folder
//                    is decoded; the thumbnail's size and its colour at the centres of its four quarters, top left, top right,
//                    bottom left, bottom right, as rrggbb; "-" for both when it does not decode)
//   rank <name>...   <name>\t<art_folder_rank()>
//   pick <a|b|c>...  <listing>\t<rank>\t<name picked>   (art_folder_pick() over the names in that order)
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "art.h"
#include "meta.h"

static int32_t file_rd(void *ctx, uint64_t offset, void *dst, uint32_t n) {
    FILE *f = ctx;
    if (fseek(f, (long)offset, SEEK_SET) != 0) return -1;
    return (int32_t)fread(dst, 1, n, f);
}

static const char *base(const char *p) {
    const char *s = strrchr(p, '/');
    return s ? s + 1 : p;
}

static const char *kind_name(uint8_t k) {
    switch (k) {
    case ART_JPEG: return "jpeg";
    case ART_PNG: return "png";
    case ART_OTHER: return "other";
    default: return "none";
    }
}

static const char *err_name(art_err_t e) {
    switch (e) {
    case ART_OK: return "ok";
    case ART_E_READ: return "read";
    case ART_E_FORMAT: return "format";
    case ART_E_UNSUPPORTED: return "unsupported";
    case ART_E_TOO_BIG: return "too_big";
    case ART_E_MEMORY: return "memory";
    case ART_E_ABORTED: return "aborted";
    }
    return "?";
}

static uint16_t px[ART_PIXELS];

static art_info_t last;

static art_err_t decode(FILE *f, uint64_t off, uint32_t len) {
    return art_decode(file_rd, f, off, len, px, &last, NULL, NULL);
}

static void colour(uint32_t x, uint32_t y) {
    uint16_t p = px[y * last.w + x];
    unsigned r = (p >> 11) & 31u, g = (p >> 5) & 63u, b = p & 31u;
    printf("%02x%02x%02x", (r << 3) | (r >> 2), (g << 2) | (g >> 4), (b << 3) | (b >> 2));
}

static int tags(const char *path) {
    FILE *f = fopen(path, "rb");
    if (f == NULL) return 1;
    fseek(f, 0, SEEK_END);
    uint64_t size = (uint64_t)ftell(f);
    audio_tags_t t;
    memset(&t, 0, sizeof t);
    size_t n = strlen(path);
    if (n > 5 && strcmp(path + n - 5, ".flac") == 0) {
        static flac_info_t fi;
        flac_parse(file_rd, f, size, &fi);
        t = fi.tags;
    } else {
        static mp3_info_t mi;
        mp3_parse(file_rd, f, size, &mi);
        t = mi.tags;
    }
    const char *d = t.art.kind == ART_NONE ? "-" : err_name(decode(f, t.art.offset, t.art.length));
    art_src_t order[2];
    size_t k = art_sources(&t.art, true, order);
    printf("%s\t%llu\t%u\t%s\t%u\t%s\t", base(path), (unsigned long long)t.art.offset, t.art.length, kind_name(t.art.kind),
           t.art.type, d);
    for (size_t i = 0; i < k; i++) printf("%s%s", i ? "," : "", order[i] == ART_SRC_EMBEDDED ? "embedded" : "folder");
    printf("\n");
    fclose(f);
    return 0;
}

static int image(const char *path) {
    FILE *f = fopen(path, "rb");
    if (f == NULL) return 1;
    fseek(f, 0, SEEK_END);
    uint32_t size = (uint32_t)ftell(f);
    art_err_t e = decode(f, 0, size);
    printf("%s\t%s\t", base(path), err_name(e));
    if (e != ART_OK) {
        printf("-\t-\n");
    } else {
        uint32_t w = last.w, h = last.h;
        printf("%ux%u\t", w, h);
        colour(w / 4, h / 4);
        printf(",");
        colour(3 * w / 4, h / 4);
        printf(",");
        colour(w / 4, 3 * h / 4);
        printf(",");
        colour(3 * w / 4, 3 * h / 4);
        printf("\n");
    }
    fclose(f);
    return 0;
}

typedef struct {
    char names[16][128];
    int n, at;
} listing_t;

static bool next_name(void *ctx, char *name, size_t cap) {
    listing_t *l = ctx;
    if (l->at >= l->n) return false;
    snprintf(name, cap, "%s", l->names[l->at++]);
    return true;
}

static void pick(const char *arg) {
    listing_t l = {0};
    char buf[2048];
    snprintf(buf, sizeof buf, "%s", arg);
    for (char *s = strtok(buf, "|"); s != NULL && l.n < 16; s = strtok(NULL, "|")) snprintf(l.names[l.n++], 128, "%s", s);
    char best[128];
    int r = art_folder_pick(next_name, &l, best, sizeof best);
    printf("%s\t%d\t%s\n", arg, r, best);
}

int main(int argc, char **argv) {
    if (argc < 2) return 2;
    for (int i = 2; i < argc; i++) {
        if (strcmp(argv[1], "tags") == 0) {
            if (tags(argv[i]) != 0) return 1;
        } else if (strcmp(argv[1], "image") == 0) {
            if (image(argv[i]) != 0) return 1;
        } else if (strcmp(argv[1], "rank") == 0) {
            printf("%s\t%d\n", argv[i], art_folder_rank(argv[i]));
        } else if (strcmp(argv[1], "pick") == 0) {
            pick(argv[i]);
        } else {
            return 2;
        }
    }
    return 0;
}
