// SPDX-License-Identifier: MPL-2.0
/*
 * Host backend of the portable file API (nfs.h): POSIX files under a root directory,
 * $NEUPOD_FS_ROOT or NEUPOD_DEFAULT_FS_ROOT (build/host/fsroot, set by CMake), created on first
 * use. Paths are normalised before use, so they cannot leave the root.
 */
#define _XOPEN_SOURCE 700
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <unistd.h>

#include "nfs.h"

#ifndef NEUPOD_DEFAULT_FS_ROOT
#define NEUPOD_DEFAULT_FS_ROOT "fsroot"
#endif

struct nfs_file {
    int fd;
    bool used;
    bool writable;
};

struct nfs_dir {
    DIR *dir;
    int dirfd;
    bool used;
};

static struct nfs_file g_files[NFS_MAX_OPEN_FILES];
static struct nfs_dir g_dirs[NFS_MAX_OPEN_DIRS];
static char g_root[1024];
static char g_root_real[PATH_MAX];
static bool g_root_ready;

static nfs_err_t map_errno(int e) {
    switch (e) {
    case ENOENT: return NFS_ERR_NOT_FOUND;
    case EEXIST: return NFS_ERR_EXISTS;
    case ENOTDIR: return NFS_ERR_NOT_DIR;
    case EISDIR: return NFS_ERR_IS_DIR;
    case ENOTEMPTY: return NFS_ERR_NOT_EMPTY;
    case ENOSPC:
    case EDQUOT:
    case EFBIG: return NFS_ERR_NO_SPACE;
    case EMFILE:
    case ENFILE: return NFS_ERR_TOO_MANY;
    case EINVAL: return NFS_ERR_INVALID;
    case ENAMETOOLONG: return NFS_ERR_BAD_PATH;
    default: return NFS_ERR_IO;
    }
}

static void mkdir_p(const char *path) {
    char tmp[1024];
    snprintf(tmp, sizeof tmp, "%s", path);
    for (char *p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            mkdir(tmp, 0777);
            *p = '/';
        }
    }
    mkdir(tmp, 0777);
}

// True if @p full (which may not exist yet) really lies inside the root once symbolic links are
// resolved. A symlink under the root that points outside must not be a way out.
static bool inside_root(const char *full) {
    char rp[PATH_MAX];
    if (realpath(full, rp) == NULL) {
        if (errno != ENOENT) {
            return false; // ELOOP, EACCES, ...
        }
        char parent[1024];
        snprintf(parent, sizeof parent, "%s", full);
        char *slash = strrchr(parent, '/');
        if (slash == NULL) {
            return false;
        }
        if (slash == parent) {
            slash[1] = '\0';
        } else {
            *slash = '\0';
        }
        if (realpath(parent, rp) == NULL) {
            return errno == ENOENT; // nothing exists up there, so nothing can redirect it
        }
    }
    size_t n = strlen(g_root_real);
    return strncmp(rp, g_root_real, n) == 0 && (rp[n] == '\0' || rp[n] == '/' || strcmp(g_root_real, "/") == 0);
}

// Normalise @p path and map it into the root. Creates the root on first use.
static nfs_err_t resolve(const char *path, char *full, size_t cap, char *norm) {
    if (!g_root_ready) {
        const char *env = getenv("NEUPOD_FS_ROOT");
        snprintf(g_root, sizeof g_root, "%s", (env && env[0]) ? env : NEUPOD_DEFAULT_FS_ROOT);
        size_t n = strlen(g_root);
        while (n > 1 && g_root[n - 1] == '/') {
            g_root[--n] = '\0';
        }
        mkdir_p(g_root);
        if (realpath(g_root, g_root_real) == NULL) {
            return NFS_ERR_IO;
        }
        g_root_ready = true;
    }
    nfs_err_t e = nfs_path_normalize(path, norm, NFS_PATH_MAX);
    if (e != NFS_OK) {
        return e;
    }
    int n = snprintf(full, cap, "%s%s", g_root, strcmp(norm, "/") == 0 ? "" : norm);
    if (n < 0 || (size_t)n >= cap) {
        return NFS_ERR_BAD_PATH;
    }
    if (full[0] == '\0') {
        snprintf(full, cap, "/");
    }
    return inside_root(full) ? NFS_OK : NFS_ERR_BAD_PATH;
}

#define RESOLVE(path)                                                                              \
    char full[1024];                                                                      \
    char norm[NFS_PATH_MAX];                                                                       \
    do {                                                                                           \
        nfs_err_t re_ = resolve((path), full, sizeof full, norm);                                  \
        if (re_ != NFS_OK) {                                                                       \
            return re_;                                                                            \
        }                                                                                          \
    } while (0)

nfs_err_t nfs_open(nfs_file_t **out, const char *path, unsigned flags) {
    RESOLVE(path);
    bool rd = flags & NFS_O_READ;
    bool wr = flags & NFS_O_WRITE;
    if (out == NULL || (!rd && !wr) || (!wr && (flags & (NFS_O_CREATE | NFS_O_TRUNC | NFS_O_APPEND))) ||
        ((flags & NFS_O_EXCL) && !(flags & NFS_O_CREATE))) {
        return NFS_ERR_INVALID;
    }
    if (wr && !nfs_write_scope_allows(norm)) {
        return NFS_ERR_IO; // outside the active write scope (nfs.h)
    }
    struct stat st;
    if (stat(full, &st) == 0 && S_ISDIR(st.st_mode)) {
        return NFS_ERR_IS_DIR;
    }
    int oflags = rd && wr ? O_RDWR : wr ? O_WRONLY : O_RDONLY;
    oflags |= (flags & NFS_O_CREATE) ? O_CREAT : 0;
    oflags |= (flags & NFS_O_EXCL) ? O_EXCL : 0;
    oflags |= (flags & NFS_O_TRUNC) ? O_TRUNC : 0;
    struct nfs_file *f = NULL;
    for (size_t i = 0; i < NFS_MAX_OPEN_FILES && f == NULL; i++) {
        f = g_files[i].used ? NULL : &g_files[i];
    }
    if (f == NULL) {
        return NFS_ERR_TOO_MANY;
    }
    int fd = open(full, oflags, 0666);
    if (fd < 0) {
        return map_errno(errno);
    }
    if ((flags & NFS_O_APPEND) && lseek(fd, 0, SEEK_END) < 0) {
        int err = errno;
        close(fd);
        return map_errno(err);
    }
    f->fd = fd;
    f->used = true;
    f->writable = wr;
    *out = f;
    return NFS_OK;
}

nfs_err_t nfs_close(nfs_file_t *f) {
    if (f == NULL || !f->used) {
        return NFS_ERR_INVALID;
    }
    f->used = false;
    return close(f->fd) == 0 ? NFS_OK : map_errno(errno);
}

nfs_err_t nfs_read(nfs_file_t *f, void *buf, size_t n, size_t *nread) {
    if (f == NULL || !f->used || (buf == NULL && n > 0)) {
        return NFS_ERR_INVALID;
    }
    size_t total = 0;
    while (total < n) {
        ssize_t r = read(f->fd, (char *)buf + total, n - total);
        if (r < 0) {
            if (errno == EINTR) {
                continue;
            }
            if (nread) {
                *nread = total;
            }
            return map_errno(errno);
        }
        if (r == 0) {
            break;
        }
        total += (size_t)r;
    }
    if (nread) {
        *nread = total;
    }
    return NFS_OK;
}

nfs_err_t nfs_write(nfs_file_t *f, const void *buf, size_t n, size_t *nwritten) {
    if (f == NULL || !f->used || !f->writable || (buf == NULL && n > 0)) {
        return NFS_ERR_INVALID;
    }
    size_t total = 0;
    nfs_err_t e = NFS_OK;
    while (total < n) {
        ssize_t w = write(f->fd, (const char *)buf + total, n - total);
        if (w < 0) {
            if (errno == EINTR) {
                continue;
            }
            e = map_errno(errno);
            break;
        }
        total += (size_t)w;
    }
    if (nwritten) {
        *nwritten = total;
    }
    return e;
}

nfs_err_t nfs_seek(nfs_file_t *f, uint64_t pos) {
    if (f == NULL || !f->used) {
        return NFS_ERR_INVALID;
    }
    if (!f->writable) { // read-only handles clamp to the size (same as the FatFs backend)
        struct stat st;
        if (fstat(f->fd, &st) != 0) {
            return map_errno(errno);
        }
        if (pos > (uint64_t)st.st_size) {
            pos = (uint64_t)st.st_size;
        }
    }
    return lseek(f->fd, (off_t)pos, SEEK_SET) >= 0 ? NFS_OK : map_errno(errno);
}

nfs_err_t nfs_tell(nfs_file_t *f, uint64_t *pos) {
    if (f == NULL || !f->used || pos == NULL) {
        return NFS_ERR_INVALID;
    }
    off_t p = lseek(f->fd, 0, SEEK_CUR);
    if (p < 0) {
        return map_errno(errno);
    }
    *pos = (uint64_t)p;
    return NFS_OK;
}

nfs_err_t nfs_size(nfs_file_t *f, uint64_t *size) {
    if (f == NULL || !f->used || size == NULL) {
        return NFS_ERR_INVALID;
    }
    struct stat st;
    if (fstat(f->fd, &st) != 0) {
        return map_errno(errno);
    }
    *size = (uint64_t)st.st_size;
    return NFS_OK;
}

nfs_err_t nfs_sync(nfs_file_t *f) {
    if (f == NULL || !f->used) {
        return NFS_ERR_INVALID;
    }
    return fsync(f->fd) == 0 ? NFS_OK : map_errno(errno);
}

nfs_err_t nfs_preallocate(nfs_file_t *f, uint64_t size, bool contiguous) {
    (void)contiguous; // a host file system makes no promise either way
    if (f == NULL || !f->used || !f->writable || size == 0) {
        return NFS_ERR_INVALID;
    }
    struct stat st;
    if (fstat(f->fd, &st) != 0 || st.st_size != 0) {
        return NFS_ERR_INVALID;
    }
    if (ftruncate(f->fd, (off_t)size) != 0) {
        return map_errno(errno);
    }
    return lseek(f->fd, 0, SEEK_SET) >= 0 ? NFS_OK : map_errno(errno);
}

nfs_err_t nfs_fast_seek(nfs_file_t *f) {
    (void)f;
    return NFS_ERR_UNSUPPORTED;
}

nfs_err_t nfs_file_extent(nfs_file_t *f, nfs_extent_t *out) {
    (void)f;
    (void)out;
    return NFS_ERR_UNSUPPORTED;
}

nfs_err_t nfs_mkdir(const char *path) {
    RESOLVE(path);
    if (strcmp(norm, "/") == 0) {
        return NFS_ERR_EXISTS;
    }
    if (!nfs_write_scope_allows(norm)) {
        return NFS_ERR_IO;
    }
    return mkdir(full, 0777) == 0 ? NFS_OK : map_errno(errno);
}

nfs_err_t nfs_unlink(const char *path) {
    RESOLVE(path);
    if (strcmp(norm, "/") == 0) {
        return NFS_ERR_INVALID;
    }
    if (!nfs_write_scope_allows(norm)) {
        return NFS_ERR_IO;
    }
    struct stat st;
    if (stat(full, &st) != 0) {
        return map_errno(errno);
    }
    int r = S_ISDIR(st.st_mode) ? rmdir(full) : unlink(full);
    return r == 0 ? NFS_OK : map_errno(errno);
}

nfs_err_t nfs_rename(const char *from, const char *to) {
    char a[1024];
    char b[1024];
    char na[NFS_PATH_MAX];
    char nb[NFS_PATH_MAX];
    nfs_err_t e = resolve(from, a, sizeof a, na);
    if (e == NFS_OK) {
        e = resolve(to, b, sizeof b, nb);
    }
    if (e != NFS_OK) {
        return e;
    }
    if (strcmp(na, "/") == 0 || strcmp(nb, "/") == 0) {
        return NFS_ERR_INVALID;
    }
    if (!nfs_write_scope_allows(na) || !nfs_write_scope_allows(nb)) {
        return NFS_ERR_IO;
    }
    struct stat st;
    if (stat(b, &st) == 0) {
        return NFS_ERR_EXISTS; // match FatFs: never replace
    }
    return rename(a, b) == 0 ? NFS_OK : map_errno(errno);
}

nfs_err_t nfs_stat(const char *path, nfs_stat_t *out) {
    RESOLVE(path);
    struct stat st;
    if (out == NULL) {
        return NFS_ERR_INVALID;
    }
    if (stat(full, &st) != 0) {
        return map_errno(errno);
    }
    out->is_dir = S_ISDIR(st.st_mode);
    out->size = out->is_dir ? 0 : (uint64_t)st.st_size;
    out->mtime = (uint32_t)st.st_mtime;
    return NFS_OK;
}

nfs_err_t nfs_opendir(nfs_dir_t **out, const char *path) {
    RESOLVE(path);
    if (out == NULL) {
        return NFS_ERR_INVALID;
    }
    struct stat st;
    if (stat(full, &st) != 0) {
        return map_errno(errno);
    }
    if (!S_ISDIR(st.st_mode)) {
        return NFS_ERR_NOT_DIR;
    }
    for (size_t i = 0; i < NFS_MAX_OPEN_DIRS; i++) {
        if (!g_dirs[i].used) {
            g_dirs[i].dir = opendir(full);
            if (g_dirs[i].dir == NULL) {
                return map_errno(errno);
            }
            g_dirs[i].dirfd = dirfd(g_dirs[i].dir);
            g_dirs[i].used = true;
            *out = &g_dirs[i];
            return NFS_OK;
        }
    }
    return NFS_ERR_TOO_MANY;
}

nfs_err_t nfs_readdir(nfs_dir_t *d, nfs_dirent_t *out, bool *done) {
    if (d == NULL || !d->used || out == NULL || done == NULL) {
        return NFS_ERR_INVALID;
    }
    for (;;) {
        errno = 0;
        struct dirent *de = readdir(d->dir);
        if (de == NULL) {
            if (errno != 0) {
                return map_errno(errno);
            }
            *done = true;
            return NFS_OK;
        }
        if (strcmp(de->d_name, ".") == 0 || strcmp(de->d_name, "..") == 0) {
            continue;
        }
        struct stat st;
        if (fstatat(d->dirfd, de->d_name, &st, 0) != 0) {
            continue; // vanished
        }
        *done = false;
        strncpy(out->name, de->d_name, NFS_NAME_MAX);
        out->name[NFS_NAME_MAX] = '\0';
        out->is_dir = S_ISDIR(st.st_mode);
        out->hidden = false; /* POSIX has no such attribute: the dot rule in the file browser is all there is */
        out->size = out->is_dir ? 0 : (uint64_t)st.st_size;
        return NFS_OK;
    }
}

nfs_err_t nfs_closedir(nfs_dir_t *d) {
    if (d == NULL || !d->used) {
        return NFS_ERR_INVALID;
    }
    d->used = false;
    return closedir(d->dir) == 0 ? NFS_OK : map_errno(errno);
}

nfs_err_t nfs_space(uint64_t *total, uint64_t *free_bytes) {
    RESOLVE("/");
    struct statvfs sv;
    if (total == NULL || free_bytes == NULL) {
        return NFS_ERR_INVALID;
    }
    if (statvfs(full, &sv) != 0) {
        return map_errno(errno);
    }
    *total = (uint64_t)sv.f_blocks * sv.f_frsize;
    *free_bytes = (uint64_t)sv.f_bavail * sv.f_frsize;
    return NFS_OK;
}
