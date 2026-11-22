// SPDX-License-Identifier: MPL-2.0
// What the firmware's library builder makes of a list of tracks: builds a library with lib_build() (app/library/lib_build.c)
// on a FatFs disk image, from tiny FLAC files carrying the tags given on stdin, then prints the Artists list, the Albums list,
// the album table and the Songs list as lib_list_row() / lib_album() give them. The app's drift library view
// (mobile/lib/data/library_view/drift_library_view.dart) folds artists and albums the same way; its entity tests
// (mobile/test/data/library_view/drift_library_view_test.dart) seed the tracks of fixtures/<name>.tsv and expect what this
// program printed for them, fixtures/<name>.expected.
//
// Input, one track per line, in any order: path<TAB>title<TAB>artist<TAB>album<TAB>track (an empty field is no tag).
//
// Walk order. lib_build() walks the card with f_readdir, which gives each folder's entries in the order of their directory
// entries; on a fresh volume that is the order they were created in. A phone cannot see that order, so the app's model of
// the walk (a lead decision) is depth first, each folder's entries (files and folders together) in byte order of their
// UTF-8 names. This program creates the files in exactly that order (walk_cmp below, the same order as the app's
// walkCompare in drift_library_view.dart): every folder is made when its first file is, and each folder's entries are
// made in byte order, so the firmware's own walk over the image is the app's model, whatever the order of the lines.
// Paths whose folders or files differ only in case are refused (exit 1): FAT folds case, so they would merge. mobile/tool/library_entities/gen_library_entities.sh builds this program and regenerates
// every fixture; CI runs it and fails if a fixture no longer matches the firmware. A scratch disk image is made in the
// current folder and removed at the end.
#include "library_fixture.h"

#include "library.h"

static uint8_t g_file[16384];
static uint8_t g_scratch[LIB_SCRATCH_MIN] __attribute__((aligned(8)));

#define MAX_LINES 256

// Depth-first walk order of two paths: compared folder by folder, a segment against its sibling by its bytes (a shorter
// segment that is a prefix of the other comes first).
static int walk_cmp(const void *pa, const void *pb) {
    const unsigned char *a = *(const unsigned char *const *)pa, *b = *(const unsigned char *const *)pb;
    for (;;) {
        // the end of a segment ('/' or the end of the path) sorts before any byte of a name
        unsigned x = (*a == '/' || *a == '\t' || *a == '\0') ? 0u : *a + 1u;
        unsigned y = (*b == '/' || *b == '\t' || *b == '\0') ? 0u : *b + 1u;
        if (x != y) return x < y ? -1 : 1;
        if (x == 0) {
            bool ea = *a != '/', eb = *b != '/'; // the path's end: a file, so the two are the same path
            if (ea || eb) return ea == eb ? 0 : ea ? -1 : 1;
        }
        a++;
        b++;
    }
}

// FAT and exFAT fold case: "Live" and "live" in one folder are one entry, so the second line would land in the first's
// folder (or overwrite its file) and the walk would no longer be the app's model, without any error. Before a path is
// created, each of its folders and the file are looked up; an entry that exists under a name differing from the one asked
// for (FatFs's own case folding, the volume's up-case table) is a clash. Returns the clashing prefix in @p clash, or false.
static bool case_clash(const char *path, char *clash, size_t cap) {
    char pre[256];
    size_t n = strlen(path);
    if (n >= sizeof pre) return false;
    for (size_t i = 1; i <= n; i++) {
        if (path[i] != '/' && path[i] != '\0') continue;
        memcpy(pre, path, i);
        pre[i] = '\0';
        const char *seg = strrchr(pre, '/');
        seg = seg != NULL ? seg + 1 : pre;
        FILINFO fno;
        if (f_stat(pre, &fno) == FR_OK && strcmp(fno.fname, seg) != 0) {
            snprintf(clash, cap, "%s (already there as \"%s\")", pre, fno.fname);
            return true;
        }
    }
    return false;
}

int main(void) {
    if (!fx_volume(16u << 20, 0)) return 1;
    static char *lines[MAX_LINES];
    size_t count = 0;
    char buf[1024];
    while (fgets(buf, sizeof buf, stdin)) {
        buf[strcspn(buf, "\r\n")] = '\0';
        if (buf[0] == '\0') continue;
        if (count == MAX_LINES || (lines[count] = strdup(buf)) == NULL) {
            fprintf(stderr, "too many lines\n");
            fx_cleanup();
            return 1;
        }
        count++;
    }
    qsort(lines, count, sizeof lines[0], walk_cmp);
    for (size_t i = 0; i < count; i++) {
        if (i > 0 && walk_cmp(&lines[i - 1], &lines[i]) == 0) {
            fprintf(stderr, "the same path twice: %s\n", lines[i]);
            fx_cleanup();
            return 1;
        }
        char *line = lines[i];
        char *f[5] = {0};
        char *p = line;
        for (int i = 0; i < 5; i++) {
            f[i] = p;
            char *t = p ? strchr(p, '\t') : NULL;
            if (t) *t = '\0';
            p = t ? t + 1 : NULL;
        }
        char clash[512];
        if (case_clash(f[0], clash, sizeof clash)) {
            fprintf(stderr, "paths that differ only in case: %s; FAT folds case, so they would be one entry\n", clash);
            fx_cleanup();
            return 1;
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
