// SPDX-License-Identifier: MPL-2.0
// For each file named on the command line, runs the firmware's own tag parser (app/audio/meta.c: flac_parse or
// mp3_parse, with the library scanner's 4096-byte comment scratch and the library tags enabled, as lib_build.c sets
// them up) and what index_file() in app/library/lib_build.c makes of the tags: the song key (lib_collate_key() of the
// title, or of the file name when untagged, with the title sort tag), the album artist it folds artists on (the album
// artist tag, else the artist, else "Unknown Artist"), the compilation test, and the entity keys stage_entity() folds
// artists and albums on (lib_collate_key() of the album artist's sort text, else the album artist; of the album sort
// tag, else the album tag). An untagged album's key is its folder's name, which a lone file does not have: that column
// is then empty. Prints one line per file, the texts as hex of the bytes the parser stored:
//   file, title, sort_title, song key, artist, album, album_artist, sort_artist, sort_album, sort_album_artist, genre,
//   track, disc, year, the COMPILATION / TCMP flag, aa (index_file's album artist), sort_aa, comp (index_file's),
//   artist entity key, album entity key, duration (s, as TR_DUR)
// Built and run by scripts/app/gen_tag_key_vectors.py; the app's tests compare its Dart port with these lines.
#include <stdbool.h>
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

static bool ieq_str(const char *a, const char *b) { // lib_build.c
    for (; *a != '\0' && *b != '\0'; a++, b++) {
        char x = *a, y = *b;
        if (x >= 'A' && x <= 'Z') x = (char)(x + 32);
        if (y >= 'A' && y <= 'Z') y = (char)(y + 32);
        if (x != y) return false;
    }
    return *a == *b;
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
        uint32_t rate = 0;
        uint64_t frames = 0;
        size_t n = strlen(name);
        if (n > 5 && strcmp(name + n - 5, ".flac") == 0) {
            static flac_info_t fi;
            flac_parse(file_rd, f, size, &fi);
            tags = fi.tags;
            rate = fi.rate;
            frames = fi.total_frames;
        } else {
            static mp3_info_t mi;
            mp3_parse(file_rd, f, size, &mi);
            tags = mi.tags;
            rate = mi.rate;
            frames = mi.total_frames;
        }
        meta_set_tags_ext(NULL);
        meta_set_scratch(NULL, 0);
        fclose(f);
        char title[POOL_STR], artist[POOL_STR], aa[POOL_STR];
        cut(title, sizeof title, tags.title[0] ? tags.title : name);
        uint8_t key[LIB_KEY_MAX];
        size_t kn = lib_collate_key(title, ext.sort_title, key, sizeof key);
        // index_file() (lib_build.c)
        cut(artist, sizeof artist, tags.artist[0] ? tags.artist : "Unknown Artist");
        cut(aa, sizeof aa, ext.album_artist[0] ? ext.album_artist : artist);
        const char *sort_aa = ext.sort_album_artist[0] ? ext.sort_album_artist : (!ext.album_artist[0] ? ext.sort_artist : "");
        bool comp = ext.compilation || ieq_str(aa, "Various Artists");
        printf("%s\t", name);
        hex(tags.title);
        printf("\t");
        hex(ext.sort_title);
        printf("\t");
        for (size_t k = 0; k < kn; k++) printf("%02x", key[k]);
        const char *texts[] = {tags.artist, tags.album, ext.album_artist, ext.sort_artist, ext.sort_album, ext.sort_album_artist, ext.genre};
        for (size_t k = 0; k < sizeof texts / sizeof texts[0]; k++) {
            printf("\t");
            hex(texts[k]);
        }
        printf("\t%u\t%u\t%u\t%u\t", (unsigned)tags.track, (unsigned)ext.disc, (unsigned)ext.year, ext.compilation ? 1u : 0u);
        hex(aa);
        printf("\t");
        hex(sort_aa);
        printf("\t%u\t", comp ? 1u : 0u);
        key_hex(sort_aa[0] ? sort_aa : aa); // ent_fill(): the sort text when there is one, else the name
        printf("\t");
        if (ext.sort_album[0]) key_hex(ext.sort_album);
        else if (tags.album[0]) key_hex(tags.album);
        printf("\t%u\n", (unsigned)(rate != 0 ? frames / rate : 0));
    }
    return 0;
}
