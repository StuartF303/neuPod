// SPDX-License-Identifier: MPL-2.0
// What the firmware's library builder makes of a list of tracks: builds a library with lib_build() (app/library/lib_build.c)
// on a FatFs disk image, from tiny FLAC files carrying the tags given on stdin, then prints the Artists list, the Albums list,
// the album table and the Songs list as lib_list_row() / lib_album() give them. The app's drift library view
// (mobile/lib/data/library_view/drift_library_view.dart) folds artists and albums the same way; its entity tests
// (mobile/test/data/library_view/drift_library_view_test.dart) seed the tracks of fixtures/<name>.tsv and expect what this
// program printed for them, fixtures/<name>.expected.
//
// Input, one track per line, created (and so walked) in that order: path<TAB>title<TAB>artist<TAB>album<TAB>track
// (an empty field is no tag). mobile/tool/library_entities/gen_library_entities.sh builds this program and regenerates
// every fixture; CI runs it and fails if a fixture no longer matches the firmware. A scratch disk image is made in the
// current folder and removed at the end.
#include "library_fixture.h"

#include "library.h"

static uint8_t g_file[16384];
static uint8_t g_scratch[LIB_SCRATCH_MIN] __attribute__((aligned(8)));

int main(void) {
    if (!fx_volume(16u << 20, 0)) return 1;
    char line[1024];
    while (fgets(line, sizeof line, stdin)) {
        line[strcspn(line, "\r\n")] = '\0';
        if (line[0] == '\0') continue;
        char *f[5] = {0};
        char *p = line;
        for (int i = 0; i < 5; i++) {
            f[i] = p;
            char *t = p ? strchr(p, '\t') : NULL;
            if (t) *t = '\0';
            p = t ? t + 1 : NULL;
        }
        fx_tags_t tg = {.title = f[1], .artist = f[2], .album = f[3], .track = f[4] ? atoi(f[4]) : 0};
        size_t n = fx_flac(g_file, &tg, 10);
        if (!fx_put(f[0], g_file, n)) {
            fprintf(stderr, "put %s failed\n", f[0]);
            fx_cleanup();
            return 1;
        }
    }
    lib_build_cfg_t cfg = {.fingerprint = 1};
    lib_build_stats_t st;
    lib_err_t e = lib_build(&cfg, g_scratch, sizeof g_scratch, &st);
    static lib_reader_t rd;
    if (e != LIB_OK || lib_open(&rd, NULL) != LIB_OK) {
        fprintf(stderr, "build %d\n", e);
        fx_cleanup();
        return 1;
    }
    lib_row_t r;
    printf("# artists (name, albums)\n");
    for (uint32_t i = 0; i < lib_list_count(&rd, LIB_LIST_ARTISTS); i++) {
        lib_list_row(&rd, LIB_LIST_ARTISTS, i, &r);
        lib_entity_t a;
        lib_artist(&rd, r.id, &a);
        printf("%s\t%u\n", r.text, (unsigned)a.count);
    }
    printf("# albums list (title, artist, songs)\n");
    for (uint32_t i = 0; i < lib_list_count(&rd, LIB_LIST_ALBUMS); i++) {
        lib_list_row(&rd, LIB_LIST_ALBUMS, i, &r);
        printf("%s\t%s\t%u\n", r.text, r.sub, (unsigned)r.count);
    }
    printf("# album table (title, artist)\n");
    for (uint32_t i = 0; i < lib_manifest(&rd)->albums; i++) {
        lib_entity_t al, ar;
        lib_album(&rd, i, &al);
        ar.name[0] = '\0';
        if (al.artist != LIB_NONE) lib_artist(&rd, al.artist, &ar);
        printf("%s\t%s\n", al.name, ar.name);
    }
    printf("# songs (title, artist, album)\n");
    for (uint32_t i = 0; i < lib_list_count(&rd, LIB_LIST_SONGS); i++) {
        lib_list_row(&rd, LIB_LIST_SONGS, i, &r);
        printf("%s\t%s\t%s\n", r.text, r.sub, r.album);
    }
    lib_close(&rd);
    fx_cleanup();
    return 0;
}
