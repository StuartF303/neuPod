// SPDX-License-Identifier: MPL-2.0
/*
 * Portable file API (nfs.h) on FatFs. Compiled only into RP2350 storage targets, against the SD
 * library's own ff.h (see platforms/rp2350/CMakeLists.txt); the volume is mounted by
 * hal_storage_mount(). No SDK headers here: only FatFs and the HAL.
 */
#include <string.h>

#include "ff.h"
#include "hal_storage.h"
#include "nfs.h"

#define CLMT_ENTRIES 256u // fast-seek link map size in DWORDs (fits ~127 fragments)

struct nfs_file {
    FIL fil;
    DWORD clmt[CLMT_ENTRIES];
    char path[NFS_PATH_MAX];
    bool used;
    bool writable;
};

struct nfs_dir {
    DIR dir;
    bool used;
};

static struct nfs_file g_files[NFS_MAX_OPEN_FILES];
static struct nfs_dir g_dirs[NFS_MAX_OPEN_DIRS];
static uint32_t g_bounce[HAL_STORAGE_SECTOR_SIZE / 4]; // aligned staging for unaligned callers

static nfs_err_t map_fr(FRESULT fr) {
    switch (fr) {
    case FR_OK: return NFS_OK;
    case FR_NO_FILE:
    case FR_NO_PATH: return NFS_ERR_NOT_FOUND;
    case FR_EXIST: return NFS_ERR_EXISTS;
    case FR_INVALID_NAME: return NFS_ERR_BAD_PATH;
    case FR_DENIED: return NFS_ERR_NO_SPACE; // full volume / full directory / read-only target
    case FR_TOO_MANY_OPEN_FILES: return NFS_ERR_TOO_MANY;
    case FR_NOT_ENABLED:
    case FR_NO_FILESYSTEM: return NFS_ERR_NOT_MOUNTED;
    case FR_INVALID_OBJECT:
    case FR_INVALID_PARAMETER:
    case FR_INVALID_DRIVE: return NFS_ERR_INVALID;
    case FR_NOT_ENOUGH_CORE: return NFS_ERR_NO_MEMORY;
    default: return NFS_ERR_IO; // DISK_ERR, INT_ERR, NOT_READY, WRITE_PROTECTED, TIMEOUT, LOCKED
    }
}

static nfs_err_t prepare(const char *path, char *out) {
    if (!hal_storage_is_mounted()) {
        return NFS_ERR_NOT_MOUNTED;
    }
    return nfs_path_normalize(path, out, NFS_PATH_MAX);
}

// Mutating calls: refused outside an active write scope (nfs.h). The hardware lock still applies on top.
static nfs_err_t gate(const char *normalised) { return nfs_write_scope_allows(normalised) ? NFS_OK : NFS_ERR_IO; }

static struct nfs_file *alloc_file(void) {
    for (size_t i = 0; i < NFS_MAX_OPEN_FILES; i++) {
        if (!g_files[i].used) {
            memset(&g_files[i], 0, sizeof g_files[i]);
            g_files[i].used = true;
            return &g_files[i];
        }
    }
    return NULL;
}

// Days since 1970-01-01 for a proleptic Gregorian date (Howard Hinnant's algorithm).
static int32_t days_from_civil(int y, unsigned m, unsigned d) {
    y -= m <= 2;
    int era = (y >= 0 ? y : y - 399) / 400;
    unsigned yoe = (unsigned)(y - era * 400);
    unsigned doy = (153u * (m + (m > 2 ? -3 : 9)) + 2u) / 5u + d - 1u;
    unsigned doe = yoe * 365u + yoe / 4u - yoe / 100u + doy;
    return era * 146097 + (int32_t)doe - 719468;
}

static uint32_t fat_to_unix(WORD fdate, WORD ftime) {
    if (fdate == 0) {
        return 0;
    }
    int32_t days = days_from_civil(1980 + (fdate >> 9), (fdate >> 5) & 15u, fdate & 31u);
    return (uint32_t)days * 86400u + (uint32_t)(ftime >> 11) * 3600u +
           (uint32_t)((ftime >> 5) & 63u) * 60u + (uint32_t)(ftime & 31u) * 2u;
}

nfs_err_t nfs_open(nfs_file_t **out, const char *path, unsigned flags) {
    char p[NFS_PATH_MAX];
    nfs_err_t e = prepare(path, p);
    if (e != NFS_OK) {
        return e;
    }
    bool rd = flags & NFS_O_READ;
    bool wr = flags & NFS_O_WRITE;
    if (out == NULL || (!rd && !wr) || (!wr && (flags & (NFS_O_CREATE | NFS_O_TRUNC | NFS_O_APPEND))) ||
        ((flags & NFS_O_EXCL) && !(flags & NFS_O_CREATE))) {
        return NFS_ERR_INVALID;
    }
    if (wr && (e = gate(p)) != NFS_OK) {
        return e;
    }
    FILINFO fno;
    FRESULT fr = f_stat(p, &fno);
    if (fr == FR_OK && (fno.fattrib & AM_DIR)) {
        return NFS_ERR_IS_DIR;
    }
    if (strcmp(p, "/") == 0) {
        return NFS_ERR_IS_DIR;
    }
    BYTE mode = (BYTE)((rd ? FA_READ : 0) | (wr ? FA_WRITE : 0));
    if (flags & NFS_O_CREATE) {
        mode |= (flags & NFS_O_EXCL) ? FA_CREATE_NEW : (flags & NFS_O_TRUNC) ? FA_CREATE_ALWAYS
                                                                             : FA_OPEN_ALWAYS;
    } else {
        mode |= FA_OPEN_EXISTING;
    }
    struct nfs_file *f = alloc_file();
    if (f == NULL) {
        return NFS_ERR_TOO_MANY;
    }
    fr = f_open(&f->fil, p, mode);
    if (fr != FR_OK) {
        f->used = false;
        return map_fr(fr);
    }
    if ((flags & NFS_O_TRUNC) && !(flags & NFS_O_CREATE)) {
        fr = f_truncate(&f->fil); // position is 0, so this empties the file
    }
    if (fr == FR_OK && (flags & NFS_O_APPEND)) {
        fr = f_lseek(&f->fil, f_size(&f->fil));
    }
    if (fr != FR_OK) {
        f_close(&f->fil);
        f->used = false;
        return map_fr(fr);
    }
    f->writable = wr;
    memcpy(f->path, p, strlen(p) + 1);
    *out = f;
    return NFS_OK;
}

nfs_err_t nfs_close(nfs_file_t *f) {
    if (f == NULL || !f->used) {
        return NFS_ERR_INVALID;
    }
    FRESULT fr = f_close(&f->fil);
    f->used = false;
    return map_fr(fr);
}

nfs_err_t nfs_read(nfs_file_t *f, void *buf, size_t n, size_t *nread) {
    if (f == NULL || !f->used || (buf == NULL && n > 0)) {
        return NFS_ERR_INVALID;
    }
    uint8_t *dst = buf;
    size_t total = 0;
    bool aligned = ((uintptr_t)dst & 3u) == 0;
    while (n > 0) {
        UINT want = (UINT)(n > 0x40000000u ? 0x40000000u : n);
        UINT got = 0;
        FRESULT fr;
        if (aligned) {
            fr = f_read(&f->fil, dst, want, &got);
        } else { // DMA needs word-aligned destinations: stage through an aligned sector buffer
            want = want < HAL_STORAGE_SECTOR_SIZE ? want : HAL_STORAGE_SECTOR_SIZE;
            fr = f_read(&f->fil, g_bounce, want, &got);
            memcpy(dst, g_bounce, got);
        }
        if (fr != FR_OK) {
            if (nread) {
                *nread = total;
            }
            return map_fr(fr);
        }
        total += got;
        dst += got;
        n -= got;
        if (got < want) {
            break; // end of file
        }
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
    const uint8_t *src = buf;
    size_t total = 0;
    bool aligned = ((uintptr_t)src & 3u) == 0;
    nfs_err_t e = NFS_OK;
    while (n > 0 && e == NFS_OK) {
        UINT want = (UINT)(n > 0x40000000u ? 0x40000000u : n);
        UINT put = 0;
        FRESULT fr;
        if (aligned) {
            fr = f_write(&f->fil, src, want, &put);
        } else {
            want = want < HAL_STORAGE_SECTOR_SIZE ? want : HAL_STORAGE_SECTOR_SIZE;
            memcpy(g_bounce, src, want);
            fr = f_write(&f->fil, g_bounce, want, &put);
        }
        total += put;
        src += put;
        n -= put;
        if (fr != FR_OK) {
            e = map_fr(fr);
        } else if (put < want) {
            e = NFS_ERR_NO_SPACE; // volume full
        }
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
    FRESULT fr = f_lseek(&f->fil, (FSIZE_t)pos);
    if (fr != FR_OK) {
        return map_fr(fr);
    }
    if (f->writable && f_tell(&f->fil) != (FSIZE_t)pos) {
        return NFS_ERR_NO_SPACE; // could not extend the file that far
    }
    return NFS_OK;
}

nfs_err_t nfs_tell(nfs_file_t *f, uint64_t *pos) {
    if (f == NULL || !f->used || pos == NULL) {
        return NFS_ERR_INVALID;
    }
    *pos = f_tell(&f->fil);
    return NFS_OK;
}

nfs_err_t nfs_size(nfs_file_t *f, uint64_t *size) {
    if (f == NULL || !f->used || size == NULL) {
        return NFS_ERR_INVALID;
    }
    *size = f_size(&f->fil);
    return NFS_OK;
}

nfs_err_t nfs_sync(nfs_file_t *f) {
    if (f == NULL || !f->used) {
        return NFS_ERR_INVALID;
    }
    return map_fr(f_sync(&f->fil));
}

nfs_err_t nfs_preallocate(nfs_file_t *f, uint64_t size, bool contiguous) {
    if (f == NULL || !f->used || !f->writable || size == 0 || f_size(&f->fil) != 0) {
        return NFS_ERR_INVALID;
    }
    FRESULT fr;
    if (contiguous) {
        fr = f_expand(&f->fil, (FSIZE_t)size, 1); // allocate now, one unfragmented run
        if (fr == FR_DENIED) {
            return NFS_ERR_NO_SPACE;
        }
    } else {
        // Seeking past the end of a writable file allocates the clusters (any layout).
        fr = f_lseek(&f->fil, (FSIZE_t)size);
        if (fr == FR_OK && f_tell(&f->fil) != (FSIZE_t)size) {
            return NFS_ERR_NO_SPACE;
        }
        if (fr == FR_OK) {
            fr = f_lseek(&f->fil, 0);
        }
    }
    return map_fr(fr);
}

nfs_err_t nfs_fast_seek(nfs_file_t *f) {
    if (f == NULL || !f->used || f->writable) {
        return NFS_ERR_INVALID;
    }
    f->fil.cltbl = f->clmt;
    f->clmt[0] = CLMT_ENTRIES;
    FRESULT fr = f_lseek(&f->fil, CREATE_LINKMAP);
    if (fr != FR_OK) {
        f->fil.cltbl = NULL; // table too small (FR_NOT_ENOUGH_CORE) or error: plain seeks
    }
    return map_fr(fr);
}

nfs_err_t nfs_file_extent(nfs_file_t *f, nfs_extent_t *out) {
    if (f == NULL || !f->used || out == NULL) {
        return NFS_ERR_INVALID;
    }
    if (f_sync(&f->fil) != FR_OK) {
        return NFS_ERR_IO;
    }
    FSIZE_t size = f_size(&f->fil);
    FATFS *fs = f->fil.obj.fs;
    if (fs == NULL || fs->fs_type == 0 || size == 0 || f->fil.obj.sclust < 2) {
        return NFS_ERR_UNSUPPORTED;
    }
    // Prove the file is ONE contiguous run: a link map of a single fragment has 4 entries.
    FIL probe;
    DWORD tbl[4];
    if (f_open(&probe, f->path, FA_READ) != FR_OK) {
        return NFS_ERR_UNSUPPORTED;
    }
    probe.cltbl = tbl;
    tbl[0] = 4;
    FRESULT fr = f_lseek(&probe, CREATE_LINKMAP);
    DWORD sclust = probe.obj.sclust;
    f_close(&probe);
    if (fr != FR_OK || tbl[0] != 4 || tbl[3] != 0) {
        return NFS_ERR_UNSUPPORTED; // fragmented (or map failed)
    }
    uint64_t csize = fs->csize;
    uint64_t sectors = ((uint64_t)size + HAL_STORAGE_SECTOR_SIZE - 1) / HAL_STORAGE_SECTOR_SIZE;
    uint64_t nclust = (sectors + csize - 1) / csize;
    if (sclust < 2 || (uint64_t)sclust + nclust > fs->n_fatent) {
        return NFS_ERR_UNSUPPORTED;
    }
    uint64_t lba = (uint64_t)fs->database + (uint64_t)(sclust - 2) * csize;
    uint64_t data_end = (uint64_t)fs->database + (uint64_t)(fs->n_fatent - 2) * csize;
    if (lba < fs->database || lba + sectors > data_end || lba + sectors > hal_storage_sector_count()) {
        return NFS_ERR_UNSUPPORTED;
    }
    out->first_lba = (uint32_t)lba;
    out->sectors = (uint32_t)sectors;
    return NFS_OK;
}

nfs_err_t nfs_mkdir(const char *path) {
    char p[NFS_PATH_MAX];
    nfs_err_t e = prepare(path, p);
    if (e != NFS_OK) {
        return e;
    }
    if (strcmp(p, "/") == 0) {
        return NFS_ERR_EXISTS;
    }
    if ((e = gate(p)) != NFS_OK) {
        return e;
    }
    return map_fr(f_mkdir(p));
}

nfs_err_t nfs_unlink(const char *path) {
    char p[NFS_PATH_MAX];
    nfs_err_t e = prepare(path, p);
    if (e != NFS_OK) {
        return e;
    }
    if (strcmp(p, "/") == 0) {
        return NFS_ERR_INVALID;
    }
    if ((e = gate(p)) != NFS_OK) {
        return e;
    }
    FILINFO fno;
    if (f_stat(p, &fno) == FR_OK && (fno.fattrib & AM_DIR)) {
        DIR d;
        if (f_opendir(&d, p) == FR_OK) {
            FRESULT fr = f_readdir(&d, &fno);
            f_closedir(&d);
            if (fr == FR_OK && fno.fname[0] != '\0') {
                return NFS_ERR_NOT_EMPTY;
            }
        }
    }
    return map_fr(f_unlink(p));
}

nfs_err_t nfs_rename(const char *from, const char *to) {
    char a[NFS_PATH_MAX];
    char b[NFS_PATH_MAX];
    nfs_err_t e = prepare(from, a);
    if (e == NFS_OK) {
        e = nfs_path_normalize(to, b, sizeof b);
    }
    if (e != NFS_OK) {
        return e;
    }
    if (strcmp(a, "/") == 0 || strcmp(b, "/") == 0) {
        return NFS_ERR_INVALID;
    }
    if ((e = gate(a)) != NFS_OK || (e = gate(b)) != NFS_OK) {
        return e;
    }
    return map_fr(f_rename(a, b));
}

nfs_err_t nfs_stat(const char *path, nfs_stat_t *out) {
    char p[NFS_PATH_MAX];
    nfs_err_t e = prepare(path, p);
    if (e != NFS_OK || out == NULL) {
        return e != NFS_OK ? e : NFS_ERR_INVALID;
    }
    if (strcmp(p, "/") == 0) {
        out->size = 0;
        out->is_dir = true;
        out->mtime = 0;
        return NFS_OK;
    }
    FILINFO fno;
    FRESULT fr = f_stat(p, &fno);
    if (fr != FR_OK) {
        return map_fr(fr);
    }
    out->is_dir = (fno.fattrib & AM_DIR) != 0;
    out->size = out->is_dir ? 0 : fno.fsize;
    out->mtime = fat_to_unix(fno.fdate, fno.ftime);
    return NFS_OK;
}

nfs_err_t nfs_opendir(nfs_dir_t **out, const char *path) {
    char p[NFS_PATH_MAX];
    nfs_err_t e = prepare(path, p);
    if (e != NFS_OK || out == NULL) {
        return e != NFS_OK ? e : NFS_ERR_INVALID;
    }
    if (strcmp(p, "/") != 0) {
        FILINFO fno;
        FRESULT fr = f_stat(p, &fno);
        if (fr != FR_OK) {
            return map_fr(fr);
        }
        if (!(fno.fattrib & AM_DIR)) {
            return NFS_ERR_NOT_DIR;
        }
    }
    for (size_t i = 0; i < NFS_MAX_OPEN_DIRS; i++) {
        if (!g_dirs[i].used) {
            FRESULT fr = f_opendir(&g_dirs[i].dir, p);
            if (fr != FR_OK) {
                return map_fr(fr);
            }
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
    FILINFO fno;
    FRESULT fr = f_readdir(&d->dir, &fno);
    if (fr != FR_OK) {
        return map_fr(fr);
    }
    if (fno.fname[0] == '\0') {
        *done = true;
        return NFS_OK;
    }
    *done = false;
    strncpy(out->name, fno.fname, NFS_NAME_MAX);
    out->name[NFS_NAME_MAX] = '\0';
    out->is_dir = (fno.fattrib & AM_DIR) != 0;
    out->hidden = (fno.fattrib & (AM_HID | AM_SYS)) != 0;
    out->size = out->is_dir ? 0 : fno.fsize;
    return NFS_OK;
}

nfs_err_t nfs_closedir(nfs_dir_t *d) {
    if (d == NULL || !d->used) {
        return NFS_ERR_INVALID;
    }
    FRESULT fr = f_closedir(&d->dir);
    d->used = false;
    return map_fr(fr);
}

nfs_err_t nfs_space(uint64_t *total, uint64_t *free_bytes) {
    if (!hal_storage_is_mounted()) {
        return NFS_ERR_NOT_MOUNTED;
    }
    if (total == NULL || free_bytes == NULL) {
        return NFS_ERR_INVALID;
    }
    DWORD nclst = 0;
    FATFS *fs = NULL;
    FRESULT fr = f_getfree("", &nclst, &fs); // "" = the (only) current drive
    if (fr != FR_OK) {
        return map_fr(fr);
    }
    uint64_t cl_bytes = (uint64_t)fs->csize * HAL_STORAGE_SECTOR_SIZE;
    *total = (uint64_t)(fs->n_fatent - 2) * cl_bytes;
    *free_bytes = (uint64_t)nclst * cl_bytes;
    return NFS_OK;
}
