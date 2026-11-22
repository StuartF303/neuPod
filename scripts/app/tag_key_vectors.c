// SPDX-License-Identifier: MPL-2.0
// For each file named on the command line, runs the firmware's own tag parser (app/audio/meta.c: flac_parse or
// mp3_parse, with the library scanner's 4096-byte comment scratch and the library tags enabled, as lib_build.c sets
// them up) and the song key that index_file() in app/library/lib_build.c builds from it: lib_collate_key() of the
// title, or of the file name when untagged, with the title sort tag. Also the artist and album tags as the parser stored them
// and the entity keys index_file() / stage_entity() fold artists and albums on: lib_collate_key() of the artist (or
// "Unknown Artist") and of the album tag, with no sort tag (the app does not store sort tags for them yet). An untagged
// album's key is its folder's name, which a lone file does not have: that column is then empty. Prints one line per file:
//   <file name>\t<title hex>\t<sort_title hex>\t<key hex>\t<artist hex>\t<album hex>\t<artist key hex>\t<album key hex>
// Built and run by scripts/app/gen_tag_key_vectors.py; the app's tests compare its Dart port with these lines.
#include <stdio.h>
#include <string.h>

#include "library.h"
#include "meta.h"

#define TAG_SCRATCH 4096u /* lib_build.c */
#define POOL_STR 65u      /* lib_build.c: LIB_TEXT_MAX */

static int32_t file_rd(void *ctx, uint64_t offset, void *dst, uint32_t n) {
    FILE *f = ctx;
    if (fseek(f, (long)offset, SEEK_SET) != 0) return -1;
    return (int32_t)fread(dst, 1, n, f);
}

static void cut(char *dst, size_t cap, const char *s) { // lib_build.c
    size_t n = strlen(s);
    if (n > cap - 1) {
        n = cap - 1;
        while (n > 0 && ((uint8_t)s[n] & 0xC0) == 0x80) n--;
    }
    memcpy(dst, s, n);
    dst[n] = '\0';
}

static void hex(const char *s) {
    for (; *s; s++) printf("%02x", (uint8_t)*s);
}

static void key_hex(const char *text) { // lib_collate_key() of a pool string, no sort tag
    char pooled[POOL_STR];
    cut(pooled, sizeof pooled, text);
    uint8_t key[LIB_KEY_MAX];
    size_t kn = lib_collate_key(pooled, NULL, key, sizeof key);
    for (size_t k = 0; k < kn; k++) printf("%02x", key[k]);
}

int main(int argc, char **argv) {
    static uint8_t scratch[TAG_SCRATCH];
    for (int i = 1; i < argc; i++) {
        FILE *f = fopen(argv[i], "rb");
        if (f == NULL) return 1;
        fseek(f, 0, SEEK_END);
        uint64_t size = (uint64_t)ftell(f);
        const char *name = strrchr(argv[i], '/');
        name = name ? name + 1 : argv[i];
        audio_tags_ext_t ext;
        memset(&ext, 0, sizeof ext);
        meta_set_scratch(scratch, sizeof scratch);
        meta_set_tags_ext(&ext);
        audio_tags_t tags;
        memset(&tags, 0, sizeof tags);
        size_t n = strlen(name);
        if (n > 5 && strcmp(name + n - 5, ".flac") == 0) {
            static flac_info_t fi;
            flac_parse(file_rd, f, size, &fi);
            tags = fi.tags;
        } else {
            static mp3_info_t mi;
            mp3_parse(file_rd, f, size, &mi);
            tags = mi.tags;
        }
        meta_set_tags_ext(NULL);
        meta_set_scratch(NULL, 0);
        fclose(f);
        char title[POOL_STR];
        cut(title, sizeof title, tags.title[0] ? tags.title : name);
        uint8_t key[LIB_KEY_MAX];
        size_t kn = lib_collate_key(title, ext.sort_title, key, sizeof key);
        printf("%s\t", name);
        hex(tags.title);
        printf("\t");
        hex(ext.sort_title);
        printf("\t");
        for (size_t k = 0; k < kn; k++) printf("%02x", key[k]);
        printf("\t");
        hex(tags.artist);
        printf("\t");
        hex(tags.album);
        printf("\t");
        key_hex(tags.artist[0] ? tags.artist : "Unknown Artist");
        printf("\t");
        if (tags.album[0]) key_hex(tags.album);
        printf("\n");
    }
    return 0;
}
