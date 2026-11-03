// SPDX-License-Identifier: MPL-2.0
/**
 * @file nfs.h
 * @brief Portable file API ("neuPod file system") for the player and the storage tests.
 *
 * Paths are UTF-8, '/'-separated and absolute from the volume root ("/music/a.flac"). They are
 * normalised first (nfs_path_normalize). Backends: RP2350 = FatFs on the microSD (the volume must
 * be mounted with hal_storage_mount() first), host = POSIX files under $NEUPOD_FS_ROOT (default
 * build/host/fsroot). No heap is used; handles come from small static pools.
 *
 * WRITE SAFETY: on the card, writes also need hal_storage_set_write_enabled(true); until then
 * every mutating call fails with NFS_ERR_IO (the card refuses the write).
 */
#ifndef NEUPOD_NFS_H
#define NEUPOD_NFS_H

#include "hal_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    NFS_OK = 0,
    NFS_ERR_NOT_FOUND,   /**< Path (or a parent directory) does not exist. */
    NFS_ERR_EXISTS,      /**< Already exists (NFS_O_EXCL, mkdir, rename target). */
    NFS_ERR_NOT_DIR,     /**< A path component or the target is not a directory. */
    NFS_ERR_IS_DIR,      /**< Target is a directory but a file was required. */
    NFS_ERR_NOT_EMPTY,   /**< Directory not empty. */
    NFS_ERR_NO_SPACE,    /**< Volume full. */
    NFS_ERR_IO,          /**< Device error, or writes are locked. */
    NFS_ERR_INVALID,     /**< Bad argument or flag combination. */
    NFS_ERR_BAD_PATH,    /**< Malformed path, or it escapes above the root. */
    NFS_ERR_TOO_MANY,    /**< Out of file/directory handles. */
    NFS_ERR_NOT_MOUNTED, /**< No volume mounted. */
    NFS_ERR_UNSUPPORTED, /**< Not available on this backend. */
    NFS_ERR_NO_MEMORY,   /**< A fixed-size internal table is too small (e.g. fast-seek map). */
} nfs_err_t;

/** Short name of an error, e.g. "NOT_FOUND". Never NULL. */
const char *nfs_err_str(nfs_err_t e);

/* Open flags. NFS_O_READ and/or NFS_O_WRITE are required. */
#define NFS_O_READ 0x01u
#define NFS_O_WRITE 0x02u
#define NFS_O_CREATE 0x04u /**< Create if missing (needs NFS_O_WRITE). */
#define NFS_O_EXCL 0x08u   /**< With NFS_O_CREATE: fail with NFS_ERR_EXISTS if it exists. */
#define NFS_O_TRUNC 0x10u  /**< Discard existing contents (needs NFS_O_WRITE). */
#define NFS_O_APPEND 0x20u /**< Position at the end on open (needs NFS_O_WRITE). */

#define NFS_PATH_MAX 256u /**< Longest normalised path, including the NUL. */
#define NFS_NAME_MAX 255u /**< Longest entry name in bytes (UTF-8). */
#define NFS_MAX_OPEN_FILES 4u
#define NFS_MAX_OPEN_DIRS 2u

typedef struct nfs_file nfs_file_t;
typedef struct nfs_dir nfs_dir_t;

typedef struct {
    uint64_t size; /**< Bytes (0 for directories). */
    bool is_dir;
    uint32_t mtime; /**< Seconds since 1970-01-01 UTC; 0 if the volume has no real clock. */
} nfs_stat_t;

typedef struct {
    char name[NFS_NAME_MAX + 1];
    uint64_t size;
    bool is_dir;
} nfs_dirent_t;

/** Contiguous region of sectors holding a file's data (see nfs_file_extent). */
typedef struct {
    uint32_t first_lba;
    uint32_t sectors;
} nfs_extent_t;

/**
 * Normalise @p in into @p out (capacity @p cap, at least NFS_PATH_MAX recommended): collapses
 * repeated '/', drops ".", resolves "..", makes the path absolute, strips a trailing '/'. The root
 * is "/". NFS_ERR_BAD_PATH if ".." would climb above the root, the path is empty/NULL, contains
 * control characters or a backslash, a component exceeds NFS_NAME_MAX, or it does not fit.
 */
nfs_err_t nfs_path_normalize(const char *in, char *out, size_t cap);

nfs_err_t nfs_open(nfs_file_t **out, const char *path, unsigned flags);
nfs_err_t nfs_close(nfs_file_t *f);
/** Read up to @p n bytes; *nread (may be NULL) gets the count, 0 at end of file. */
nfs_err_t nfs_read(nfs_file_t *f, void *buf, size_t n, size_t *nread);
/** Write @p n bytes at the current position. */
nfs_err_t nfs_write(nfs_file_t *f, const void *buf, size_t n, size_t *nwritten);
/**
 * Set the position. Past the end of a writable handle is allowed (the gap reads as unspecified
 * bytes once written beyond); on a read-only handle the position is clamped to the size.
 */
nfs_err_t nfs_seek(nfs_file_t *f, uint64_t pos);
nfs_err_t nfs_tell(nfs_file_t *f, uint64_t *pos);
nfs_err_t nfs_size(nfs_file_t *f, uint64_t *size);
/** Flush file data and directory entry to the medium. */
nfs_err_t nfs_sync(nfs_file_t *f);
/**
 * Reserve @p size bytes for an EMPTY file opened for writing and leave the position at 0, so later
 * writes need no allocation. @p contiguous = true asks for one unfragmented run (FatFs f_expand;
 * fails with NFS_ERR_NO_SPACE if none is free); false lets the filesystem fragment it. The file's
 * size becomes @p size.
 */
nfs_err_t nfs_preallocate(nfs_file_t *f, uint64_t size, bool contiguous);
/**
 * Enable fast seek (cluster link map) for a read-only handle: later seeks no longer walk the FAT.
 * NFS_ERR_NO_MEMORY if the file is too fragmented for the fixed-size map, NFS_ERR_UNSUPPORTED on
 * backends without it.
 */
nfs_err_t nfs_fast_seek(nfs_file_t *f);
/**
 * Where the file's data sits on the medium, for raw-sector tests. Only succeeds for a file that
 * is one contiguous run and lies entirely inside the volume's data area; otherwise
 * NFS_ERR_UNSUPPORTED. The file must be synced (no pending data) and nothing else may touch it.
 */
nfs_err_t nfs_file_extent(nfs_file_t *f, nfs_extent_t *out);

nfs_err_t nfs_mkdir(const char *path);
/** Remove a file or an EMPTY directory. */
nfs_err_t nfs_unlink(const char *path);
nfs_err_t nfs_rename(const char *from, const char *to);
nfs_err_t nfs_stat(const char *path, nfs_stat_t *out);

nfs_err_t nfs_opendir(nfs_dir_t **out, const char *path);
/** Next entry (never "." or ".."); NFS_OK with *done = true when exhausted. */
nfs_err_t nfs_readdir(nfs_dir_t *d, nfs_dirent_t *out, bool *done);
nfs_err_t nfs_closedir(nfs_dir_t *d);

/** Total and free bytes of the volume. Slow on a big FAT32 volume (scans the FAT). */
nfs_err_t nfs_space(uint64_t *total, uint64_t *free_bytes);

/*
 * WRITE SCOPE. While a scope is active, every mutating call (open for write/create/truncate, mkdir, unlink,
 * rename: both paths) whose path is not @p dir itself or below it fails with NFS_ERR_IO, whatever the hardware
 * write lock says. It narrows an unlock: the one caller that must write (app/settings, /.neupod) enters a scope,
 * then unlocks the card (hal_storage_set_write_enabled) for the duration and restores the lock afterwards.
 * The scope does not stop FatFs updating the FAT and the directory entries that belong to the files inside it
 * (those sectors are shared), and it does not cover raw hal_storage_write(); nothing else in the firmware calls that.
 * Not recursive; one scope at a time; the storage task is the only caller (no locking here).
 */
nfs_err_t nfs_write_scope_enter(const char *dir);
void nfs_write_scope_leave(void);
bool nfs_write_scope_active(void);
/** True if no scope is active, or the (normalised, absolute) @p path is the scope directory or below it. */
bool nfs_write_scope_allows(const char *path);

#ifdef __cplusplus
}
#endif

#endif // NEUPOD_NFS_H
