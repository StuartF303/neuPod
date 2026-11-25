// SPDX-License-Identifier: MPL-2.0
/**
 * @file lib_ff_posix.h
 * @brief The slice of the FatFs API the music library uses, over POSIX files: for the host simulator only (NEUPOD_LIB_POSIX).
 *
 * The device (RP2350, Y2) runs the library on FatFs. The host simulator's card is a folder (NEUPOD_FS_ROOT, nfs_posix.c), so the library is built
 * there against this header instead of ff.h: the same library sources, with f_open / f_read / ... renamed to lpx_f_* so nothing can clash with a FatFs
 * linked into the same program. Not a general FatFs replacement: exactly what library.h and the library's .c files call. Paths are mapped into the
 * card folder (and kept inside it) by hal_host_resolve_path(), as nfs_posix.c does for the file API. Not used by the unit tests, which run the real FatFs
 * on a disk image.
 */
#ifndef NEUPOD_LIB_FF_POSIX_H
#define NEUPOD_LIB_FF_POSIX_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef unsigned int UINT;
typedef unsigned char BYTE;
typedef unsigned short WORD;
typedef uint32_t DWORD;
typedef uint64_t FSIZE_t;

#define FF_USE_FASTSEEK 0

typedef enum {
    FR_OK = 0,
    FR_DISK_ERR,
    FR_INT_ERR,
    FR_NOT_READY,
    FR_NO_FILE,
    FR_NO_PATH,
    FR_INVALID_NAME,
    FR_DENIED,
    FR_EXIST,
    FR_INVALID_OBJECT,
    FR_WRITE_PROTECTED,
    FR_NOT_ENOUGH_CORE,
} FRESULT;

#define FA_READ 0x01
#define FA_WRITE 0x02
#define FA_OPEN_EXISTING 0x00
#define FA_CREATE_NEW 0x04
#define FA_CREATE_ALWAYS 0x08
#define FA_OPEN_ALWAYS 0x10
#define FA_OPEN_APPEND 0x30

#define AM_HID 0x02
#define AM_SYS 0x04
#define AM_DIR 0x10

/** Like FatFs' FIL: a small read cache (512 B) so the library's record reads are not a system call each. */
typedef struct {
    int fd;
    FSIZE_t fptr;     /**< position */
    FSIZE_t obj_size; /**< length */
    FSIZE_t buf_off;  /**< file offset of buf[0] */
    UINT buf_len;     /**< valid bytes in buf (0 = none) */
    BYTE buf[512];
} FIL;
#define f_size(fp) ((fp)->obj_size)

typedef struct {
    FSIZE_t fsize;
    WORD fdate, ftime;
    BYTE fattrib;
    char fname[256];
} FILINFO;

typedef struct {
    void *dir; /* POSIX DIR* */
    char path[1024];
} DIR;

#define f_open lpx_f_open
#define f_close lpx_f_close
#define f_read lpx_f_read
#define f_write lpx_f_write
#define f_lseek lpx_f_lseek
#define f_sync lpx_f_sync
#define f_stat lpx_f_stat
#define f_opendir lpx_f_opendir
#define f_readdir lpx_f_readdir
#define f_closedir lpx_f_closedir

FRESULT f_open(FIL *fp, const char *path, BYTE mode);
FRESULT f_close(FIL *fp);
FRESULT f_read(FIL *fp, void *buf, UINT btr, UINT *br);
FRESULT f_write(FIL *fp, const void *buf, UINT btw, UINT *bw);
FRESULT f_lseek(FIL *fp, FSIZE_t ofs);
FRESULT f_sync(FIL *fp);
FRESULT f_stat(const char *path, FILINFO *fno);
FRESULT f_opendir(DIR *dp, const char *path);
FRESULT f_readdir(DIR *dp, FILINFO *fno); /* end of the directory: fno->fname[0] == '\0' */
FRESULT f_closedir(DIR *dp);

#ifdef __cplusplus
}
#endif

#endif
