// SPDX-License-Identifier: MPL-2.0
/*
 * The slice of the FatFs API the music library uses, over POSIX files (app/include/lib_ff_posix.h): the host simulator's library build.
 * Paths go through hal_host_resolve_path(), which keeps them inside the card folder like the file API (nfs_posix.c).
 */
#define _XOPEN_SOURCE 700
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#define DIR lpx_ffdir_t /* the header's own DIR type: not POSIX's */
#include "lib_ff_posix.h"
#undef DIR

#include "hal_host.h"

static FRESULT map_errno(int e) {
    switch (e) {
    case ENOENT: return FR_NO_FILE;
    case ENOTDIR: return FR_NO_PATH;
    case EEXIST: return FR_EXIST;
    case ENOSPC:
    case EDQUOT:
    case EFBIG: return FR_DENIED; /* the library maps this to "card full" */
    case EACCES:
    case EROFS:
    case EPERM: return FR_WRITE_PROTECTED;
    default: return FR_DISK_ERR;
    }
}

static bool resolve(const char *path, char *out, size_t n) { return hal_host_resolve_path(path, out, n); }

FRESULT f_open(FIL *fp, const char *path, BYTE mode) {
    char full[1024];
    if (!resolve(path, full, sizeof full)) {
        return FR_INVALID_NAME;
    }
    int fl = (mode & FA_READ) && (mode & FA_WRITE) ? O_RDWR : (mode & FA_WRITE) ? O_WRONLY : O_RDONLY;
    if (mode & FA_CREATE_NEW) fl |= O_CREAT | O_EXCL;
    else if (mode & FA_CREATE_ALWAYS) fl |= O_CREAT | O_TRUNC;
    else if (mode & FA_OPEN_ALWAYS) fl |= O_CREAT;
    struct stat st;
    if (stat(full, &st) == 0 && S_ISDIR(st.st_mode)) {
        return FR_NO_FILE;
    }
    int fd = open(full, fl | O_CLOEXEC, 0666);
    if (fd < 0) {
        return map_errno(errno);
    }
    memset(fp, 0, sizeof *fp);
    fp->fd = fd;
    struct stat s2;
    fp->obj_size = fstat(fd, &s2) == 0 ? (FSIZE_t)s2.st_size : 0;
    if ((mode & FA_OPEN_APPEND) == FA_OPEN_APPEND) {
        fp->fptr = fp->obj_size;
    }
    return FR_OK;
}

FRESULT f_close(FIL *fp) {
    int r = close(fp->fd);
    fp->fd = -1;
    return r == 0 ? FR_OK : FR_DISK_ERR;
}

FRESULT f_sync(FIL *fp) { return fsync(fp->fd) == 0 ? FR_OK : FR_DISK_ERR; }

FRESULT f_lseek(FIL *fp, FSIZE_t ofs) {
    fp->fptr = ofs; /* like FatFs in a file opened for writing, a seek past the end is allowed: the next write extends */
    return FR_OK;
}

FRESULT f_read(FIL *fp, void *buf, UINT btr, UINT *br) {
    uint8_t *d = buf;
    UINT done = 0;
    while (done < btr && fp->fptr < fp->obj_size) {
        if (!(fp->buf_len != 0 && fp->fptr >= fp->buf_off && fp->fptr < fp->buf_off + fp->buf_len)) {
            FSIZE_t at = fp->fptr & ~(FSIZE_t)(sizeof fp->buf - 1u);
            ssize_t got = pread(fp->fd, fp->buf, sizeof fp->buf, (off_t)at);
            if (got < 0) {
                *br = done;
                return FR_DISK_ERR;
            }
            fp->buf_off = at;
            fp->buf_len = (UINT)got;
            if (got == 0 || fp->fptr >= at + (FSIZE_t)got) {
                break;
            }
        }
        UINT in = (UINT)(fp->fptr - fp->buf_off), k = fp->buf_len - in;
        if (k > btr - done) k = btr - done;
        if (fp->fptr + k > fp->obj_size) k = (UINT)(fp->obj_size - fp->fptr);
        memcpy(d + done, fp->buf + in, k);
        done += k;
        fp->fptr += k;
    }
    *br = done;
    return FR_OK;
}

FRESULT f_write(FIL *fp, const void *buf, UINT btw, UINT *bw) {
    const uint8_t *s = buf;
    UINT done = 0;
    fp->buf_len = 0; /* the read cache may overlap what is written */
    while (done < btw) {
        ssize_t w = pwrite(fp->fd, s + done, btw - done, (off_t)fp->fptr);
        if (w <= 0) {
            *bw = done;
            return w < 0 ? map_errno(errno) : FR_DENIED;
        }
        done += (UINT)w;
        fp->fptr += (FSIZE_t)w;
        if (fp->fptr > fp->obj_size) fp->obj_size = fp->fptr;
    }
    *bw = done;
    return FR_OK;
}

// FatFs' date and time words from a Unix time (read back as UTC by the library, which takes them as such).
static void dos_time(time_t t, FILINFO *fno) {
    struct tm tm;
    gmtime_r(&t, &tm);
    int y = tm.tm_year + 1900;
    if (y < 1980) {
        fno->fdate = fno->ftime = 0;
        return;
    }
    fno->fdate = (WORD)(((y - 1980) << 9) | ((tm.tm_mon + 1) << 5) | tm.tm_mday);
    fno->ftime = (WORD)((tm.tm_hour << 11) | (tm.tm_min << 5) | (tm.tm_sec / 2));
}

static void fill(FILINFO *fno, const char *name, const struct stat *st) {
    memset(fno, 0, sizeof *fno);
    snprintf(fno->fname, sizeof fno->fname, "%s", name);
    fno->fsize = S_ISDIR(st->st_mode) ? 0 : (FSIZE_t)st->st_size;
    fno->fattrib = S_ISDIR(st->st_mode) ? AM_DIR : 0;
    dos_time(st->st_mtime, fno);
}

FRESULT f_stat(const char *path, FILINFO *fno) {
    char full[1024];
    if (!resolve(path, full, sizeof full)) {
        return FR_INVALID_NAME;
    }
    struct stat st;
    if (stat(full, &st) != 0) {
        return map_errno(errno);
    }
    if (fno != NULL) {
        const char *base = strrchr(path, '/');
        fill(fno, base != NULL ? base + 1 : path, &st);
    }
    return FR_OK;
}

FRESULT f_opendir(lpx_ffdir_t *dp, const char *path) {
    if (!resolve(path, dp->path, sizeof dp->path)) {
        return FR_INVALID_NAME;
    }
    dp->dir = opendir(dp->path);
    return dp->dir != NULL ? FR_OK : map_errno(errno);
}

FRESULT f_readdir(lpx_ffdir_t *dp, FILINFO *fno) {
    for (;;) {
        errno = 0;
        struct dirent *de = readdir((DIR *)dp->dir);
        if (de == NULL) {
            fno->fname[0] = '\0';
            return errno == 0 ? FR_OK : FR_DISK_ERR;
        }
        if (strcmp(de->d_name, ".") == 0 || strcmp(de->d_name, "..") == 0) {
            continue;
        }
        char full[1280];
        struct stat st;
        snprintf(full, sizeof full, "%s/%s", dp->path, de->d_name);
        if (stat(full, &st) != 0) {
            continue; /* vanished */
        }
        fill(fno, de->d_name, &st);
        return FR_OK;
    }
}

FRESULT f_closedir(lpx_ffdir_t *dp) {
    int r = dp->dir != NULL ? closedir((DIR *)dp->dir) : 0;
    dp->dir = NULL;
    return r == 0 ? FR_OK : FR_DISK_ERR;
}
