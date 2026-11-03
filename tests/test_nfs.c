// SPDX-License-Identifier: MPL-2.0
// Portable file API: path normalisation and the host (POSIX) backend.
#define _XOPEN_SOURCE 700
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "nfs.h"
#include "unit.h"

static char g_root[] = "neupod_nfs_test_XXXXXX";

static void norm(const char *in, const char *want) {
    char out[NFS_PATH_MAX];
    ASSERT_EQ(NFS_OK, nfs_path_normalize(in, out, sizeof out));
    if (strcmp(out, want) != 0) {
        printf("  path '%s' -> '%s', wanted '%s'\n", in, out, want);
    }
    ASSERT_EQ(0, strcmp(out, want));
}

static void bad(const char *in) {
    char out[NFS_PATH_MAX];
    ASSERT_EQ(NFS_ERR_BAD_PATH, nfs_path_normalize(in, out, sizeof out));
}

TEST(test_path_normalize) {
    norm("/", "/");
    norm("//", "/");
    norm("a", "/a");
    norm("/a/b/c", "/a/b/c");
    norm("/a//b///c/", "/a/b/c");
    norm("/a/./b/.", "/a/b");
    norm("/a/b/../c", "/a/c");
    norm("/a/b/../../c", "/c");
    norm("/a/..", "/");
    norm("/a/../b/./c/..", "/b");
    norm("/music/Sigur Rós/Ágætis byrjun.flac", "/music/Sigur Rós/Ágætis byrjun.flac"); // UTF-8
    norm("/.hidden", "/.hidden");
    norm("/a/...", "/a/..."); // three dots is an ordinary name
    bad("");
    bad("/..");
    bad("/a/../..");
    bad("../x");
    bad("/a\\b");
    bad("/a\nb");
    char small[4];
    ASSERT_EQ(NFS_ERR_BAD_PATH, nfs_path_normalize("/abcdef", small, sizeof small));
    ASSERT_EQ(NFS_ERR_BAD_PATH, nfs_path_normalize(NULL, small, sizeof small));
    char longname[NFS_NAME_MAX + 3];
    memset(longname, 'x', sizeof longname);
    longname[0] = '/';
    longname[sizeof longname - 1] = '\0';
    bad(longname);
}

TEST(test_err_strings) {
    ASSERT_EQ(0, strcmp("OK", nfs_err_str(NFS_OK)));
    ASSERT_EQ(0, strcmp("NOT_FOUND", nfs_err_str(NFS_ERR_NOT_FOUND)));
}

static void write_str(const char *path, const char *s) {
    nfs_file_t *f;
    ASSERT_EQ(NFS_OK, nfs_open(&f, path, NFS_O_WRITE | NFS_O_CREATE | NFS_O_TRUNC));
    size_t n = 0;
    ASSERT_EQ(NFS_OK, nfs_write(f, s, strlen(s), &n));
    ASSERT_EQ(strlen(s), n);
    ASSERT_EQ(NFS_OK, nfs_close(f));
}

TEST(test_roundtrip_seek_size) {
    nfs_file_t *f;
    ASSERT_EQ(NFS_OK, nfs_mkdir("/t"));
    ASSERT_EQ(NFS_ERR_EXISTS, nfs_mkdir("/t"));
    write_str("/t/a.txt", "hello world");

    ASSERT_EQ(NFS_OK, nfs_open(&f, "/t/a.txt", NFS_O_READ));
    uint64_t v = 0;
    ASSERT_EQ(NFS_OK, nfs_size(f, &v));
    ASSERT_EQ(11, v);
    char buf[32] = {0};
    size_t n = 0;
    ASSERT_EQ(NFS_OK, nfs_read(f, buf, 5, &n));
    ASSERT_EQ(5, n);
    ASSERT_EQ(0, memcmp(buf, "hello", 5));
    ASSERT_EQ(NFS_OK, nfs_tell(f, &v));
    ASSERT_EQ(5, v);
    ASSERT_EQ(NFS_OK, nfs_seek(f, 6));
    ASSERT_EQ(NFS_OK, nfs_read(f, buf, sizeof buf, &n)); // short read at EOF
    ASSERT_EQ(5, n);
    ASSERT_EQ(0, memcmp(buf, "world", 5));
    ASSERT_EQ(NFS_OK, nfs_read(f, buf, sizeof buf, &n)); // at EOF
    ASSERT_EQ(0, n);
    ASSERT_EQ(NFS_OK, nfs_seek(f, 1000)); // read-only: clamped to the size
    ASSERT_EQ(NFS_OK, nfs_tell(f, &v));
    ASSERT_EQ(11, v);
    ASSERT_EQ(NFS_ERR_INVALID, nfs_write(f, "x", 1, NULL)); // not writable
    ASSERT_EQ(NFS_OK, nfs_close(f));

    // Append, then overwrite in the middle, then seek past the end on a writable handle.
    ASSERT_EQ(NFS_OK, nfs_open(&f, "/t/a.txt", NFS_O_WRITE | NFS_O_APPEND));
    ASSERT_EQ(NFS_OK, nfs_tell(f, &v));
    ASSERT_EQ(11, v);
    ASSERT_EQ(NFS_OK, nfs_write(f, "!", 1, NULL));
    ASSERT_EQ(NFS_OK, nfs_seek(f, 0));
    ASSERT_EQ(NFS_OK, nfs_write(f, "J", 1, NULL));
    ASSERT_EQ(NFS_OK, nfs_seek(f, 20));
    ASSERT_EQ(NFS_OK, nfs_write(f, "z", 1, NULL));
    ASSERT_EQ(NFS_OK, nfs_size(f, &v));
    ASSERT_EQ(21, v);
    ASSERT_EQ(NFS_OK, nfs_sync(f));
    ASSERT_EQ(NFS_OK, nfs_close(f));

    // Truncate on open.
    ASSERT_EQ(NFS_OK, nfs_open(&f, "/t/a.txt", NFS_O_WRITE | NFS_O_TRUNC));
    ASSERT_EQ(NFS_OK, nfs_size(f, &v));
    ASSERT_EQ(0, v);
    ASSERT_EQ(NFS_OK, nfs_close(f));

    // Preallocate an empty file; size becomes the reserved size, position 0.
    ASSERT_EQ(NFS_OK, nfs_open(&f, "/t/pre.bin", NFS_O_WRITE | NFS_O_CREATE | NFS_O_EXCL));
    ASSERT_EQ(NFS_OK, nfs_preallocate(f, 4096, false));
    ASSERT_EQ(NFS_OK, nfs_size(f, &v));
    ASSERT_EQ(4096, v);
    ASSERT_EQ(NFS_OK, nfs_tell(f, &v));
    ASSERT_EQ(0, v);
    ASSERT_EQ(NFS_ERR_INVALID, nfs_preallocate(f, 8192, false)); // no longer empty
    ASSERT_EQ(NFS_OK, nfs_close(f));
    nfs_extent_t ex;
    ASSERT_EQ(NFS_OK, nfs_open(&f, "/t/pre.bin", NFS_O_READ));
    ASSERT_EQ(NFS_ERR_UNSUPPORTED, nfs_file_extent(f, &ex)); // host has no raw extents
    ASSERT_EQ(NFS_ERR_UNSUPPORTED, nfs_fast_seek(f));
    ASSERT_EQ(NFS_OK, nfs_close(f));
}

TEST(test_errors_and_flags) {
    nfs_file_t *f = NULL;
    ASSERT_EQ(NFS_ERR_NOT_FOUND, nfs_open(&f, "/nope.txt", NFS_O_READ));
    ASSERT_EQ(NFS_ERR_NOT_FOUND, nfs_open(&f, "/nodir/x.txt", NFS_O_WRITE | NFS_O_CREATE));
    ASSERT_EQ(NFS_ERR_NOT_FOUND, nfs_unlink("/nope.txt"));
    nfs_stat_t st;
    ASSERT_EQ(NFS_ERR_NOT_FOUND, nfs_stat("/nope.txt", &st));
    ASSERT_EQ(NFS_ERR_BAD_PATH, nfs_open(&f, "/../etc/passwd", NFS_O_READ));
    ASSERT_EQ(NFS_ERR_INVALID, nfs_open(&f, "/t/a.txt", 0));
    ASSERT_EQ(NFS_ERR_INVALID, nfs_open(&f, "/t/a.txt", NFS_O_READ | NFS_O_TRUNC));
    ASSERT_EQ(NFS_ERR_INVALID, nfs_open(&f, "/t/a.txt", NFS_O_WRITE | NFS_O_EXCL));
    ASSERT_EQ(NFS_ERR_EXISTS, nfs_open(&f, "/t/a.txt", NFS_O_WRITE | NFS_O_CREATE | NFS_O_EXCL));
    ASSERT_EQ(NFS_ERR_IS_DIR, nfs_open(&f, "/t", NFS_O_READ));
    nfs_dir_t *dd = NULL;
    ASSERT_EQ(NFS_ERR_NOT_DIR, nfs_opendir(&dd, "/t/a.txt"));
    ASSERT_EQ(NFS_ERR_INVALID, nfs_close(NULL));

    // Handle exhaustion.
    nfs_file_t *h[NFS_MAX_OPEN_FILES + 1];
    for (unsigned i = 0; i < NFS_MAX_OPEN_FILES; i++) {
        ASSERT_EQ(NFS_OK, nfs_open(&h[i], "/t/a.txt", NFS_O_READ));
    }
    ASSERT_EQ(NFS_ERR_TOO_MANY, nfs_open(&h[NFS_MAX_OPEN_FILES], "/t/a.txt", NFS_O_READ));
    for (unsigned i = 0; i < NFS_MAX_OPEN_FILES; i++) {
        ASSERT_EQ(NFS_OK, nfs_close(h[i]));
    }
}

TEST(test_unlink_rename_stat) {
    write_str("/t/r.txt", "abc");
    ASSERT_EQ(NFS_OK, nfs_rename("/t/r.txt", "/t/s.txt"));
    nfs_stat_t st;
    ASSERT_EQ(NFS_ERR_NOT_FOUND, nfs_stat("/t/r.txt", &st));
    ASSERT_EQ(NFS_OK, nfs_stat("/t/s.txt", &st));
    ASSERT_EQ(3, st.size);
    ASSERT_FALSE(st.is_dir);
    ASSERT_TRUE(st.mtime > 1500000000u);
    write_str("/t/other.txt", "x");
    ASSERT_EQ(NFS_ERR_EXISTS, nfs_rename("/t/other.txt", "/t/s.txt")); // never replaces
    ASSERT_EQ(NFS_ERR_NOT_FOUND, nfs_rename("/t/ghost", "/t/ghost2"));
    ASSERT_EQ(NFS_OK, nfs_stat("/t", &st));
    ASSERT_TRUE(st.is_dir);
    ASSERT_EQ(NFS_OK, nfs_stat("/", &st));
    ASSERT_TRUE(st.is_dir);

    ASSERT_EQ(NFS_ERR_NOT_EMPTY, nfs_unlink("/t")); // directory with files
    ASSERT_EQ(NFS_OK, nfs_unlink("/t/s.txt"));
    ASSERT_EQ(NFS_ERR_NOT_FOUND, nfs_stat("/t/s.txt", &st));
    ASSERT_EQ(NFS_ERR_INVALID, nfs_unlink("/"));
    ASSERT_EQ(NFS_ERR_INVALID, nfs_unlink("/t/../")); // normalises to the root
}

TEST(test_symlink_cannot_escape) {
    char outside[] = "neupod_nfs_outside_XXXXXX";
    ASSERT_TRUE(mkdtemp(outside) != NULL);
    char real_outside[1024];
    ASSERT_TRUE(realpath(outside, real_outside) != NULL);
    char link_path[1024];
    snprintf(link_path, sizeof link_path, "%s/escape", g_root);
    ASSERT_EQ(0, symlink(real_outside, link_path));
    ASSERT_EQ(0, symlink("t", (snprintf(link_path, sizeof link_path, "%s/inside", g_root), link_path)));

    nfs_file_t *f = NULL;
    nfs_stat_t st;
    ASSERT_EQ(NFS_ERR_BAD_PATH, nfs_stat("/escape", &st));
    ASSERT_EQ(NFS_ERR_BAD_PATH, nfs_open(&f, "/escape/new.txt", NFS_O_WRITE | NFS_O_CREATE));
    ASSERT_EQ(NFS_ERR_BAD_PATH, nfs_mkdir("/escape/sub"));
    ASSERT_EQ(NFS_ERR_BAD_PATH, nfs_rename("/t/a.txt", "/escape/a.txt"));
    ASSERT_EQ(NFS_OK, nfs_stat("/inside", &st)); // a link that stays inside the root is fine
    ASSERT_TRUE(st.is_dir);
    char cmd[2100];
    snprintf(cmd, sizeof cmd, "rm -rf %s", real_outside);
    ASSERT_EQ(0, system(cmd));
}

TEST(test_dir_iteration) {
    ASSERT_EQ(NFS_OK, nfs_mkdir("/tree"));
    ASSERT_EQ(NFS_OK, nfs_mkdir("/tree/sub"));
    write_str("/tree/one.txt", "1");
    write_str("/tree/two.txt", "22");
    write_str("/tree/sub/deep.txt", "333");

    nfs_dir_t *d = NULL;
    ASSERT_EQ(NFS_OK, nfs_opendir(&d, "/tree"));
    int files = 0, dirs = 0;
    uint64_t bytes = 0;
    bool done = false;
    nfs_dirent_t e;
    for (int guard = 0; guard < 20; guard++) {
        ASSERT_EQ(NFS_OK, nfs_readdir(d, &e, &done));
        if (done) {
            break;
        }
        ASSERT_TRUE(strcmp(e.name, ".") != 0 && strcmp(e.name, "..") != 0);
        if (e.is_dir) {
            dirs++;
            ASSERT_EQ(0, strcmp(e.name, "sub"));
        } else {
            files++;
            bytes += e.size;
        }
    }
    ASSERT_TRUE(done);
    ASSERT_EQ(2, files);
    ASSERT_EQ(1, dirs);
    ASSERT_EQ(3, bytes);
    ASSERT_EQ(NFS_OK, nfs_closedir(d));
    ASSERT_EQ(NFS_ERR_INVALID, nfs_closedir(d)); // double close

    ASSERT_EQ(NFS_OK, nfs_opendir(&d, "/tree/sub/"));
    ASSERT_EQ(NFS_OK, nfs_readdir(d, &e, &done));
    ASSERT_FALSE(done);
    ASSERT_EQ(0, strcmp(e.name, "deep.txt"));
    ASSERT_EQ(3, e.size);
    ASSERT_EQ(NFS_OK, nfs_readdir(d, &e, &done));
    ASSERT_TRUE(done);
    ASSERT_EQ(NFS_OK, nfs_closedir(d));

    ASSERT_EQ(NFS_ERR_NOT_FOUND, nfs_opendir(&d, "/missing"));

    uint64_t total = 0, free_b = 0;
    ASSERT_EQ(NFS_OK, nfs_space(&total, &free_b));
    ASSERT_TRUE(total > 0 && free_b <= total);
}

TEST(test_hidden_entries) {
    nfs_dirent_t e;
    memset(&e, 0, sizeof e);
    snprintf(e.name, sizeof e.name, "System Volume Information");
    ASSERT_FALSE(nfs_dirent_hidden(&e)); // no attribute: listed
    e.hidden = true; // what nfs_fatfs.c sets for AM_HID / AM_SYS
    ASSERT_TRUE(nfs_dirent_hidden(&e));
    e.hidden = false;
    snprintf(e.name, sizeof e.name, ".Trashes"); // the dot rule stays
    ASSERT_TRUE(nfs_dirent_hidden(&e));
    snprintf(e.name, sizeof e.name, "song.flac");
    ASSERT_FALSE(nfs_dirent_hidden(&e));
    // The host backend has no attributes: it never reports an entry as hidden.
    ASSERT_EQ(NFS_OK, nfs_mkdir("/hid"));
    nfs_file_t *f;
    ASSERT_EQ(NFS_OK, nfs_open(&f, "/hid/a.txt", NFS_O_WRITE | NFS_O_CREATE));
    nfs_close(f);
    nfs_dir_t *d;
    bool done = false;
    ASSERT_EQ(NFS_OK, nfs_opendir(&d, "/hid"));
    ASSERT_EQ(NFS_OK, nfs_readdir(d, &e, &done));
    ASSERT_FALSE(done);
    ASSERT_FALSE(e.hidden);
    nfs_closedir(d);
}

int main(void) {
    if (mkdtemp(g_root) == NULL || setenv("NEUPOD_FS_ROOT", g_root, 1) != 0) {
        printf("cannot create temp root\n");
        return 1;
    }
    RUN(test_path_normalize);
    RUN(test_err_strings);
    RUN(test_roundtrip_seek_size);
    RUN(test_errors_and_flags);
    RUN(test_unlink_rename_stat);
    RUN(test_symlink_cannot_escape);
    RUN(test_dir_iteration);
    RUN(test_hidden_entries);
    int rc = RESULT();
    char cmd[128];
    snprintf(cmd, sizeof cmd, "rm -rf %s", g_root);
    if (system(cmd) != 0) {
        printf("warning: could not remove %s\n", g_root);
    }
    return rc;
}
