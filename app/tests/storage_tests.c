// SPDX-License-Identifier: MPL-2.0
/*
 * microSD bring-up test runner (see docs/storage.md).
 *
 * SAFETY RULES, enforced in this file:
 *  - Writes are locked (hal_storage_set_write_enabled(false)) except inside a "write session"
 *    (session_begin .. session_end) or the confirmed format. session_end ALWAYS re-locks writes
 *    and deletes the test's files, including after a failure or 'q'; run_test() re-locks again.
 *  - Every file written lives under TEST_DIR ("/neupod-test"); ses_open() refuses any writable
 *    open elsewhere. The directory is created only if missing and removed again (if empty) only
 *    if this session created it.
 *  - Raw sector writes (test 4) go only to the LBA range of a file this session just allocated,
 *    checked by raw_write() before each call.
 *  - Write sessions first check free space >= need + 64 MB.
 *  - Test 9 (format) needs a typed YES, and is never part of run-all.
 * Tests 1-3 are read-only.
 */
#include "storage_tests.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "hal_console.h"
#include "hal_storage.h"
#include "hal_system.h"
#include "nfs.h"

typedef enum { RES_PASS, RES_FAIL, RES_SKIP, RES_QUIT } result_t;

#define TEST_DIR "/neupod-test"
#define SPACE_MARGIN (64ull * 1024 * 1024)
#define SPEED_FILE_BYTES (32u * 1024u * 1024u)

// --- helpers -------------------------------------------------------------------------------

// Widely published (unofficial) manufacturer ID table, e.g. from the Linux kernel / mmc-utils
// lists. Informational only: the SD Association does not publish a complete authoritative list.
static const char *manufacturer_name(uint8_t mid) {
    switch (mid) {
    case 0x01: return "Panasonic";
    case 0x02: return "Toshiba / Kioxia";
    case 0x03: return "SanDisk";
    case 0x1B: return "Samsung";
    case 0x1D: return "ADATA";
    case 0x27: return "Phison";
    case 0x28: return "Lexar";
    case 0x31: return "Silicon Power";
    case 0x41: return "Kingston";
    case 0x6F: return "STMicroelectronics";
    case 0x74: return "Transcend";
    case 0x76: return "Patriot";
    case 0x82: return "Sony";
    default: return NULL;
    }
}

static void print_size(uint64_t bytes) {
    uint64_t gb100 = bytes / 10000000u;                // hundredths of GB (1e9)
    uint64_t gib100 = bytes * 100u / (1024u * 1024u * 1024u); // hundredths of GiB
    printf("%u.%02u GB (%u.%02u GiB)", (unsigned)(gb100 / 100u), (unsigned)(gb100 % 100u),
           (unsigned)(gib100 / 100u), (unsigned)(gib100 % 100u));
}

static void print_mhz(uint32_t hz) {
    printf("%u.%03u MHz", (unsigned)(hz / 1000000u), (unsigned)(hz / 1000u % 1000u));
}

// Decode the first sector without mounting anything.
static void describe_sector0(const uint8_t *s) {
    if (s[510] != 0x55 || s[511] != 0xAA) {
        if (memcmp(&s[3], "EXFAT   ", 8) == 0) {
            printf("  sector 0: exFAT boot sector (no 0x55AA signature)\n");
        } else {
            printf("  sector 0: no 0x55AA boot signature (blank or unformatted?)\n");
        }
        return;
    }
    if (memcmp(&s[3], "EXFAT   ", 8) == 0) {
        printf("  sector 0: exFAT volume boot record (no partition table)\n");
    } else if (memcmp(&s[82], "FAT32   ", 8) == 0) {
        printf("  sector 0: FAT32 volume boot record (no partition table)\n");
    } else if (memcmp(&s[54], "FAT", 3) == 0) {
        printf("  sector 0: FAT12/16 volume boot record (no partition table)\n");
    } else {
        unsigned t = s[450];
        const char *name = t == 0xEE ? "GPT protective MBR" : t == 0x07 ? "NTFS/exFAT"
                           : t == 0x0B || t == 0x0C ? "FAT32" : t == 0x0E || t == 0x06 ? "FAT16"
                           : t == 0x00 ? "empty" : "other";
        printf("  sector 0: MBR, partition 1 type 0x%02X (%s)\n", t, name);
    }
}

// --- test 1: detect (read-only) --------------------------------------------------------------

static result_t test_detect(void) {
    static uint32_t sector0[HAL_STORAGE_SECTOR_SIZE / 4]; // word aligned for DMA
    printf("\n== Test 1: card detect (READ-ONLY: nothing is written) ==\n");

    uint32_t t0 = hal_millis();
    hal_storage_status_t st = hal_storage_init();
    uint32_t init_ms = hal_millis() - t0;
    printf("init: %s in %u ms\n", hal_storage_status_str(st), (unsigned)init_ms);
    if (st != HAL_STORAGE_OK) {
        printf("FAIL: card did not initialise (%s).\n", hal_storage_status_str(st));
        if (st == HAL_STORAGE_NO_CARD || st == HAL_STORAGE_TIMEOUT) {
            printf("  No card answered. Check: card fully inserted, 3.3 V present, CLK GP2, "
                   "CMD GP3, DAT0-3 GP4-7 wiring, and CMD/DAT pull-ups.\n");
        } else if (st == HAL_STORAGE_CRC) {
            printf("  Bus errors: wiring or clock too fast (try a lower clock).\n");
        }
        return RES_FAIL;
    }

    hal_card_info_t ci;
    st = hal_storage_get_info(&ci);
    if (st != HAL_STORAGE_OK) {
        printf("FAIL: could not read card info (%s)\n", hal_storage_status_str(st));
        return RES_FAIL;
    }

    printf("Card:\n");
    printf("  type: %s, %s, %u-bit bus @ ", hal_storage_card_type_str(ci.type),
           ci.write_protected ? "WRITE-PROTECTED" : "writable", (unsigned)ci.bus_width);
    print_mhz(ci.bus_clock_hz);
    printf("\n  capacity: %u sectors = ", (unsigned)ci.sectors);
    print_size(ci.capacity_bytes);
    printf("\nCID:\n");
    const char *maker = manufacturer_name(ci.mid);
    printf("  MID 0x%02X (%s; informational)\n", ci.mid, maker ? maker : "not in table");
    printf("  OID \"%s\"  PNM \"%s\"  PRV %u.%u  PSN 0x%08X  date %04u-%02u\n", ci.oid, ci.pnm,
           (unsigned)ci.prv_major, (unsigned)ci.prv_minor, (unsigned)ci.psn, (unsigned)ci.mdt_year,
           (unsigned)ci.mdt_month);
    printf("CSD: v%u, TRAN_SPEED 0x%02X = max ", ci.csd_version + 1u, ci.tran_speed_raw);
    print_mhz(ci.max_bus_hz);
    printf("\n");
    if (ci.ssr_valid) {
        printf("SSR: speed class %s%u, UHS grade U%u, video class %u, AU %u KiB\n",
               ci.speed_class ? "C" : "none ", (unsigned)ci.speed_class,
               (unsigned)ci.uhs_speed_grade, (unsigned)ci.video_speed_class,
               (unsigned)(ci.au_size_bytes / 1024u));
        if (ci.au_size_bytes == 0) {
            printf("     (AU size not reported by this card)\n");
        }
    } else {
        printf("SSR: not available on this card/interface\n");
    }

    // One read-only sector read proves the data lines; decode what is there without mounting.
    t0 = hal_millis();
    st = hal_storage_read(0, sector0, 1);
    uint32_t rd_ms = hal_millis() - t0;
    printf("read sector 0: %s in %u ms\n", hal_storage_status_str(st), (unsigned)rd_ms);
    if (st != HAL_STORAGE_OK) {
        printf("FAIL: sector read failed (%s)\n", hal_storage_status_str(st));
        return RES_FAIL;
    }
    describe_sector0((const uint8_t *)sector0);

    bool sane = ci.sectors > 0 && ci.capacity_bytes == (uint64_t)ci.sectors * HAL_STORAGE_SECTOR_SIZE;
    if (!sane) {
        printf("FAIL: card reported zero or inconsistent capacity\n");
        return RES_FAIL;
    }
    printf("PASS: card identified and readable (init %u ms, sector read %u ms)\n",
           (unsigned)init_ms, (unsigned)rd_ms);
    return RES_PASS;
}

// --- common helpers -------------------------------------------------------------------------

// 128 KB I/O buffer shared by the tests (word aligned: the SD DMA needs it).
static uint32_t g_buf32[32768];
#define BUF ((uint8_t *)g_buf32)
#define BUF_BYTES (sizeof g_buf32)

static bool quit_requested(void) {
    int c = hal_console_getc_timeout(0);
    return c == 'q' || c == 'Q';
}

// xorshift32 (shifts 13, 17, 5): a full-period (2^32 - 1) linear-feedback shift generator. All
// data patterns come from it with a fixed seed so every run writes and expects the same bytes.
static uint32_t xs_next(uint32_t *st) {
    uint32_t x = *st;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    return *st = x;
}

static uint32_t crc32_update(uint32_t crc, const uint8_t *p, size_t n) {
    static uint32_t table[256];
    static bool ready;
    if (!ready) {
        for (uint32_t i = 0; i < 256; i++) {
            uint32_t c = i;
            for (int k = 0; k < 8; k++) {
                c = (c & 1u) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            }
            table[i] = c;
        }
        ready = true;
    }
    crc = ~crc;
    while (n--) {
        crc = table[(crc ^ *p++) & 0xFFu] ^ (crc >> 8);
    }
    return ~crc;
}

static uint32_t mbs100(uint64_t bytes, uint64_t us) { // MB/s * 100 (MB = 1,000,000 bytes)
    return us == 0 ? 0 : (uint32_t)(bytes * 100u / us);
}

static void print_mbs(uint32_t v100) { printf("%u.%02u", (unsigned)(v100 / 100u), (unsigned)(v100 % 100u)); }

static const char *fs_name(hal_fs_type_t t) {
    switch (t) {
    case HAL_FS_FAT12: return "FAT12";
    case HAL_FS_FAT16: return "FAT16";
    case HAL_FS_FAT32: return "FAT32";
    case HAL_FS_EXFAT: return "exFAT";
    default: return "none";
    }
}

// The host has no mount step (files are plain POSIX files); treat UNSUPPORTED as "ready".
static bool ensure_mounted(void) {
    if (!hal_storage_ready()) {
        hal_storage_status_t st = hal_storage_init();
        if (st != HAL_STORAGE_OK) {
            printf("FAIL: card init: %s\n", hal_storage_status_str(st));
            return false;
        }
    }
    if (hal_storage_is_mounted()) {
        return true;
    }
    hal_storage_status_t st = hal_storage_mount();
    if (st != HAL_STORAGE_OK && st != HAL_STORAGE_UNSUPPORTED) {
        printf("FAIL: mount: %s\n", hal_storage_status_str(st));
        return false;
    }
    return true;
}

// --- write session -----------------------------------------------------------------------------

typedef struct {
    bool active;
    bool dir_created;
    const char *files[4];
    size_t nfiles;
    nfs_file_t *open_file;
} session_t;
static session_t g_ses;

static bool path_in_test_dir(const char *p) {
    char n[NFS_PATH_MAX];
    return nfs_path_normalize(p, n, sizeof n) == NFS_OK && strncmp(n, TEST_DIR "/", sizeof TEST_DIR) == 0;
}

// Open an EXISTING file (read, or write without creating). Writable opens must be inside TEST_DIR
// and may never create or truncate: only ses_create() makes files.
static nfs_err_t ses_open(nfs_file_t **f, const char *path, unsigned flags) {
    if (flags & (NFS_O_CREATE | NFS_O_TRUNC | NFS_O_EXCL)) {
        return NFS_ERR_INVALID;
    }
    if ((flags & (NFS_O_WRITE | NFS_O_APPEND)) && (!g_ses.active || !path_in_test_dir(path))) {
        return NFS_ERR_INVALID; // refuse: would write outside the test directory
    }
    nfs_err_t e = nfs_open(f, path, flags);
    if (e == NFS_OK) {
        g_ses.open_file = *f;
    }
    return e;
}

static void ses_track(const char *path);

// Create a NEW test file (exclusive: an existing file is never truncated or reused). On success
// the file is tracked for deletion; if it already exists we stop and leave it alone.
static nfs_err_t ses_create(nfs_file_t **f, const char *path) {
    if (!g_ses.active || !path_in_test_dir(path)) {
        return NFS_ERR_INVALID;
    }
    nfs_err_t e = nfs_open(f, path, NFS_O_WRITE | NFS_O_CREATE | NFS_O_EXCL);
    if (e == NFS_ERR_EXISTS) {
        printf("FAIL: %s already exists (left over from an earlier run?). It was NOT touched; "
               "delete it manually (e.g. on a PC) and run the test again.\n", path);
    } else if (e == NFS_OK) {
        ses_track(path);
        g_ses.open_file = *f;
    }
    return e;
}

static nfs_err_t ses_close(nfs_file_t *f) {
    if (g_ses.open_file == f) {
        g_ses.open_file = NULL;
    }
    return nfs_close(f);
}

static void ses_track(const char *path) {
    if (g_ses.nfiles < sizeof g_ses.files / sizeof g_ses.files[0]) {
        g_ses.files[g_ses.nfiles++] = path;
    }
}

// Read-only soak: read the first 16 MB of the card twice at the current clock and compare. Any
// read error or difference means the bus is not trustworthy, so no write test may start.
#define SOAK_SECTORS 32768u // 16 MB
static bool read_soak(void) {
    uint32_t n = hal_storage_sector_count();
    n = n < SOAK_SECTORS ? n : SOAK_SECTORS;
    n &= ~127u; // whole 64 KB chunks
    if (n == 0) {
        return true; // tiny image (host): nothing meaningful to read
    }
    uint32_t crc[2] = {0, 0};
    for (int pass = 0; pass < 2; pass++) {
        for (uint32_t s0 = 0; s0 < n; s0 += 128) {
            hal_storage_status_t hs = hal_storage_read(s0, BUF, 128);
            if (hs != HAL_STORAGE_OK) {
                printf("FAIL: soak read error at sector %u: %s\n", (unsigned)s0, hal_storage_status_str(hs));
                return false;
            }
            crc[pass] = crc32_update(crc[pass], BUF, 128u * HAL_STORAGE_SECTOR_SIZE);
        }
    }
    if (crc[0] != crc[1]) {
        printf("FAIL: soak: two reads of the same 16 MB differ (CRC 0x%08X vs 0x%08X)\n",
               (unsigned)crc[0], (unsigned)crc[1]);
        return false;
    }
    printf("read soak: 2 x %u MB identical (CRC 0x%08X), no errors\n", (unsigned)(n / 2048u),
           (unsigned)crc[0]);
    return true;
}

// Mount, check free space, soak-read, create TEST_DIR if missing, THEN unlock writes.
// RES_PASS = go ahead.
static result_t session_begin(uint64_t need_bytes) {
    memset(&g_ses, 0, sizeof g_ses);
    if (!ensure_mounted()) {
        return RES_FAIL;
    }
    uint32_t t0 = hal_millis();
    uint64_t total = 0, freeb = 0;
    nfs_err_t e = nfs_space(&total, &freeb);
    if (e != NFS_OK) {
        printf("FAIL: cannot read free space (%s)\n", nfs_err_str(e));
        return RES_FAIL;
    }
    printf("free space: %u MB of %u MB (read in %u ms)\n", (unsigned)(freeb / 1000000u),
           (unsigned)(total / 1000000u), (unsigned)(hal_millis() - t0));
    if (freeb < need_bytes + SPACE_MARGIN) {
        printf("FAIL: need %u MB free plus a 64 MB margin; only %u MB free. Nothing was written.\n",
               (unsigned)(need_bytes / 1000000u), (unsigned)(freeb / 1000000u));
        return RES_FAIL;
    }
    if (hal_storage_write_fault()) { // stale latch from an earlier failure: reset the card first
        printf("write fault latched earlier: re-initialising the card\n");
        hal_storage_unmount();
        if (hal_storage_init() != HAL_STORAGE_OK || !ensure_mounted()) {
            printf("FAIL: card did not recover\n");
            return RES_FAIL;
        }
    }
    if (!read_soak()) {
        return RES_FAIL; // nothing was written
    }
    g_ses.active = true;
    hal_storage_set_write_enabled(true);
    nfs_stat_t st;
    e = nfs_stat(TEST_DIR, &st);
    if (e == NFS_ERR_NOT_FOUND) {
        e = nfs_mkdir(TEST_DIR);
        g_ses.dir_created = e == NFS_OK;
    } else if (e == NFS_OK && !st.is_dir) {
        e = NFS_ERR_NOT_DIR;
    }
    if (e != NFS_OK) {
        printf("FAIL: cannot prepare " TEST_DIR " (%s)\n", nfs_err_str(e));
        g_ses.dir_created = false;
        hal_storage_set_write_enabled(false);
        g_ses.active = false;
        return RES_FAIL;
    }
    return RES_PASS;
}

// Always called after a session_begin that returned RES_PASS: close, delete, re-lock.
// If a write failed (fault latch) or the card dropped out, the card is re-initialised and the
// volume remounted BEFORE anything is deleted, so cleanup never writes through a stale state. If
// that is impossible, nothing is written and the leftovers are listed for manual deletion.
static void session_end(void) {
    if (!g_ses.active) {
        hal_storage_set_write_enabled(false);
        return;
    }
    if (g_ses.open_file != NULL) {
        nfs_close(g_ses.open_file); // writes are refused while a fault is latched
        g_ses.open_file = NULL;
    }
    bool recovered = false;
    for (int attempt = 0; attempt < 2 && !recovered; attempt++) { // one retry before giving up
        if (attempt > 0) {
            printf("cleanup: retrying card recovery\n");
            hal_delay_ms(300);
        }
        if (hal_storage_write_fault() || !hal_storage_ready() || attempt > 0) {
            printf("cleanup: card needs a reset first (write fault or lost card); re-initialising\n");
            hal_storage_unmount();
            if (hal_storage_init() != HAL_STORAGE_OK) {
                printf("cleanup: card did not come back.\n");
                continue;
            }
        }
        recovered = ensure_mounted() && !hal_storage_write_fault();
    }
    if (!recovered) {
        hal_storage_set_write_enabled(false);
        printf("WARNING: could not clean up. Writes stay locked. Delete these manually:\n");
        for (size_t i = 0; i < g_ses.nfiles; i++) {
            printf("  %s\n", g_ses.files[i]);
        }
        if (g_ses.dir_created) {
            printf("  %s (directory)\n", TEST_DIR);
        }
        g_ses.active = false;
        return;
    }
    hal_storage_set_write_enabled(true); // only for the deletes below
    for (size_t i = 0; i < g_ses.nfiles; i++) {
        nfs_err_t e = nfs_unlink(g_ses.files[i]);
        if (e != NFS_OK && e != NFS_ERR_NOT_FOUND) {
            printf("WARNING: could not delete %s (%s)\n", g_ses.files[i], nfs_err_str(e));
        }
    }
    if (g_ses.dir_created) {
        nfs_err_t e = nfs_unlink(TEST_DIR); // only succeeds if empty
        if (e != NFS_OK) {
            printf("note: left " TEST_DIR " in place (%s)\n", nfs_err_str(e));
        }
    }
    hal_storage_set_write_enabled(false);
    g_ses.active = false;
    printf("cleanup done: test files deleted, writes locked\n");
}

// --- test 2: mount ---------------------------------------------------------------------------

static result_t test_mount(void) {
    printf("\n== Test 2: mount (read-only) ==\n");
    if (!hal_storage_ready()) {
        hal_storage_status_t st = hal_storage_init();
        if (st != HAL_STORAGE_OK) {
            printf("FAIL: card init: %s\n", hal_storage_status_str(st));
            return RES_FAIL;
        }
    }
    hal_storage_unmount(); // fresh mount so the volume is re-read from the card
    uint32_t t0 = hal_millis();
    hal_storage_status_t st = hal_storage_mount();
    uint32_t mount_ms = hal_millis() - t0;
    if (st == HAL_STORAGE_UNSUPPORTED) {
        printf("SKIP: no mount step on this platform (host files are plain POSIX files)\n");
        return RES_SKIP;
    }
    printf("mount: %s in %u ms\n", hal_storage_status_str(st), (unsigned)mount_ms);
    if (st != HAL_STORAGE_OK) {
        printf("FAIL: could not mount (%s)%s\n", hal_storage_status_str(st),
               st == HAL_STORAGE_NO_FILESYSTEM
                   ? ": no FAT/exFAT volume at sector 0 or in the partition table"
                   : "");
        return RES_FAIL;
    }
    hal_volume_info_t vi;
    st = hal_storage_volume_info(&vi);
    if (st != HAL_STORAGE_OK) {
        printf("FAIL: volume info: %s\n", hal_storage_status_str(st));
        return RES_FAIL;
    }
    printf("filesystem: %s, cluster %u bytes (%u sectors), label \"%s\", serial 0x%08X\n",
           fs_name(vi.fs), (unsigned)vi.cluster_bytes, (unsigned)vi.sectors_per_cluster, vi.label,
           (unsigned)vi.serial);
    uint64_t total = 0, freeb = 0;
    t0 = hal_millis();
    nfs_err_t e = nfs_space(&total, &freeb);
    uint32_t space_ms = hal_millis() - t0;
    if (e != NFS_OK) {
        printf("FAIL: free space: %s\n", nfs_err_str(e));
        return RES_FAIL;
    }
    printf("space: total %u.%02u GB, free %u.%02u GB (full FAT scan took %u ms; slow on a big FAT32)\n",
           (unsigned)(total / 1000000000u), (unsigned)(total / 10000000u % 100u),
           (unsigned)(freeb / 1000000000u), (unsigned)(freeb / 10000000u % 100u),
           (unsigned)space_ms);
    printf("PASS: %s volume mounted (%u ms mount, %u ms free-space scan)\n", fs_name(vi.fs),
           (unsigned)mount_ms, (unsigned)space_ms);
    return RES_PASS;
}

// --- test 3: list ----------------------------------------------------------------------------

#define LIST_CAP 200u
static unsigned g_list_seen, g_list_printed;

// List one directory (no recursion). Output is capped; the rest is only counted.
static nfs_err_t list_dir(const char *path, unsigned indent) {
    nfs_dir_t *d = NULL;
    nfs_err_t e = nfs_opendir(&d, path);
    if (e != NFS_OK) {
        return e;
    }
    bool done = false;
    nfs_dirent_t ent;
    while ((e = nfs_readdir(d, &ent, &done)) == NFS_OK && !done) {
        g_list_seen++;
        if (g_list_printed < LIST_CAP) {
            g_list_printed++;
            printf("%*s%s%s", (int)indent * 2, "", ent.name, ent.is_dir ? "/" : "");
            if (!ent.is_dir) {
                printf("  %llu bytes", (unsigned long long)ent.size);
            }
            printf("\n");
        }
    }
    nfs_closedir(d);
    return e;
}

// Depth 2 in two passes so every root entry (e.g. /neupod-test) is shown before the output cap
// can cut anything off: first the root, then the contents of each root directory.
static nfs_err_t list_subdirs(void) {
    static char child[NFS_PATH_MAX + NFS_NAME_MAX + 2];
    nfs_dir_t *d = NULL;
    nfs_err_t e = nfs_opendir(&d, "/");
    if (e != NFS_OK) {
        return e;
    }
    bool done = false;
    nfs_dirent_t ent;
    while ((e = nfs_readdir(d, &ent, &done)) == NFS_OK && !done) {
        if (!ent.is_dir) {
            continue;
        }
        snprintf(child, sizeof child, "/%s", ent.name);
        if (g_list_printed < LIST_CAP) {
            printf("%s/:\n", ent.name);
        }
        nfs_err_t ce = strlen(child) < NFS_PATH_MAX ? list_dir(child, 1) : NFS_ERR_BAD_PATH;
        if (ce != NFS_OK && g_list_printed < LIST_CAP) {
            printf("  (cannot read: %s)\n", nfs_err_str(ce));
        }
    }
    nfs_closedir(d);
    return e;
}

static result_t test_list(void) {
    printf("\n== Test 3: list root, depth 2 (read-only) ==\n");
    if (!ensure_mounted()) {
        return RES_FAIL;
    }
    g_list_seen = g_list_printed = 0;
    uint32_t t0 = hal_millis();
    printf("-- root --\n");
    nfs_err_t e = list_dir("/", 0);
    unsigned root_entries = g_list_seen;
    if (e == NFS_OK) {
        if (g_list_seen > LIST_CAP) {
            printf("... %u more root entries\n", g_list_seen - LIST_CAP);
        }
        g_list_printed = 0;
        printf("-- inside root directories --\n");
        e = list_subdirs();
    }
    uint32_t ms = hal_millis() - t0;
    if (g_list_seen - root_entries > g_list_printed) {
        printf("... %u more\n", g_list_seen - root_entries - g_list_printed);
    }
    if (e != NFS_OK) {
        printf("FAIL: listing error %s after %u entries\n", nfs_err_str(e), g_list_seen);
        return RES_FAIL;
    }
    printf("PASS: %u entries listed in %u ms\n", g_list_seen, (unsigned)ms);
    return RES_PASS;
}

// --- test 4: integrity ---------------------------------------------------------------------

#define PATTERN_FILE TEST_DIR "/pattern.bin"
#define RAW_FILE TEST_DIR "/raw.bin"
#define PATTERN_BYTES (16u * 1024u * 1024u)
#define PATTERN_CHUNK (64u * 1024u)
#define PATTERN_SEED 0xC0FFEE11u
#define RAW_SEED 0x5EED1234u
#define RAW_BYTES (2u * 1024u * 1024u)
#define MAX_REPORTED 8u

static void fill_pattern(uint8_t *p, size_t n, uint32_t *st) {
    uint32_t *w = (uint32_t *)p;
    for (size_t i = 0; i < n / 4; i++) {
        w[i] = xs_next(st);
    }
}

// Compare a buffer with the expected stream, reporting the first few mismatches by offset.
static uint32_t compare_pattern(const uint8_t *got, size_t n, uint32_t *st, uint64_t base,
                                uint32_t *reported) {
    const uint32_t *w = (const uint32_t *)got;
    uint32_t bad = 0;
    for (size_t i = 0; i < n / 4; i++) {
        uint32_t want = xs_next(st);
        if (w[i] != want) {
            for (int b = 0; b < 4; b++) {
                uint8_t g = (uint8_t)(w[i] >> (8 * b));
                uint8_t x = (uint8_t)(want >> (8 * b));
                if (g != x) {
                    bad++;
                    if (*reported < MAX_REPORTED) {
                        (*reported)++;
                        printf("  mismatch at offset %llu: expected 0x%02X got 0x%02X\n",
                               (unsigned long long)(base + i * 4 + (size_t)b), x, g);
                    }
                }
            }
        }
    }
    return bad;
}

// Raw sector access restricted to the extent of a file we just allocated.
static hal_storage_status_t raw_write(const nfs_extent_t *ex, uint32_t sector_off, const void *buf,
                                      uint32_t count) {
    if (!g_ses.active || (uint64_t)sector_off + count > ex->sectors) {
        return HAL_STORAGE_BAD_PARAM;
    }
    return hal_storage_write(ex->first_lba + sector_off, buf, count);
}

static result_t integrity_files(void) {
    nfs_file_t *f;
    nfs_err_t e = ses_create(&f, PATTERN_FILE);
    if (e != NFS_OK) {
        printf("FAIL: create %s: %s\n", PATTERN_FILE, nfs_err_str(e));
        return RES_FAIL;
    }
    uint32_t st = PATTERN_SEED;
    uint32_t t0 = hal_millis();
    for (uint32_t off = 0; off < PATTERN_BYTES; off += PATTERN_CHUNK) {
        fill_pattern(BUF, PATTERN_CHUNK, &st);
        size_t put = 0;
        e = nfs_write(f, BUF, PATTERN_CHUNK, &put);
        if (e != NFS_OK || put != PATTERN_CHUNK) {
            printf("FAIL: write at offset %u: %s\n", (unsigned)off, nfs_err_str(e));
            return RES_FAIL;
        }
        if (quit_requested()) {
            return RES_QUIT;
        }
    }
    e = nfs_sync(f);
    nfs_err_t ce = ses_close(f);
    if (e != NFS_OK || ce != NFS_OK) {
        printf("FAIL: sync/close: %s/%s\n", nfs_err_str(e), nfs_err_str(ce));
        return RES_FAIL;
    }
    uint32_t wr_ms = hal_millis() - t0;
    printf("wrote %u MB in %u ms (pattern: xorshift32, seed 0x%08X)\n",
           (unsigned)(PATTERN_BYTES >> 20), (unsigned)wr_ms, (unsigned)PATTERN_SEED);

    // Unmount and mount again so the read-back comes from the card, not from FatFs caches.
    hal_storage_unmount();
    hal_storage_status_t ms = hal_storage_mount();
    if (ms != HAL_STORAGE_OK && ms != HAL_STORAGE_UNSUPPORTED) {
        printf("FAIL: remount: %s\n", hal_storage_status_str(ms));
        return RES_FAIL;
    }
    e = ses_open(&f, PATTERN_FILE, NFS_O_READ);
    if (e != NFS_OK) {
        printf("FAIL: reopen: %s\n", nfs_err_str(e));
        return RES_FAIL;
    }
    st = PATTERN_SEED;
    uint32_t bad = 0, reported = 0;
    t0 = hal_millis();
    for (uint32_t off = 0; off < PATTERN_BYTES; off += PATTERN_CHUNK) {
        size_t got = 0;
        e = nfs_read(f, BUF, PATTERN_CHUNK, &got);
        if (e != NFS_OK || got != PATTERN_CHUNK) {
            printf("FAIL: read at offset %u: %s (%u bytes)\n", (unsigned)off, nfs_err_str(e),
                   (unsigned)got);
            ses_close(f);
            return RES_FAIL;
        }
        bad += compare_pattern(BUF, PATTERN_CHUNK, &st, off, &reported);
        if (quit_requested()) {
            ses_close(f);
            return RES_QUIT;
        }
    }
    ses_close(f);
    printf("read back %u MB in %u ms: %u mismatching bytes\n", (unsigned)(PATTERN_BYTES >> 20),
           (unsigned)(hal_millis() - t0), (unsigned)bad);
    if (bad != 0) {
        printf("FAIL: data read back differs from what was written\n");
        return RES_FAIL;
    }
    return RES_PASS;
}

// Returns RES_PASS, RES_FAIL or RES_SKIP (with the reason printed).
static result_t integrity_raw(void) {
    printf("-- raw sector part: " RAW_FILE ", 2 MB contiguous --\n");
    nfs_file_t *f;
    nfs_err_t e = ses_create(&f, RAW_FILE);
    if (e != NFS_OK) {
        printf("FAIL: create raw file: %s\n", nfs_err_str(e));
        return RES_FAIL;
    }
    e = nfs_preallocate(f, RAW_BYTES, true);
    if (e != NFS_OK) {
        printf("SKIP raw part: cannot allocate a contiguous 2 MB file (%s)\n", nfs_err_str(e));
        return RES_SKIP;
    }
    nfs_extent_t ex;
    e = nfs_file_extent(f, &ex);
    if (e != NFS_OK) {
        printf("SKIP raw part: file extent not usable (%s): not contiguous, outside the data area, "
               "or not available on this platform\n", nfs_err_str(e));
        return RES_SKIP;
    }
    if (ex.sectors != RAW_BYTES / HAL_STORAGE_SECTOR_SIZE || ex.first_lba == 0) {
        printf("SKIP raw part: unexpected extent (lba %u, %u sectors)\n", (unsigned)ex.first_lba,
               (unsigned)ex.sectors);
        return RES_SKIP;
    }
    printf("file occupies sectors %u..%u (contiguous, inside the volume's data area)\n",
           (unsigned)ex.first_lba, (unsigned)(ex.first_lba + ex.sectors - 1));

    const uint32_t chunk = 128; // sectors per transfer = 64 KB
    uint32_t st = RAW_SEED;
    uint32_t t0 = hal_millis();
    for (uint32_t s = 0; s < ex.sectors; s += chunk) {
        fill_pattern(BUF, chunk * HAL_STORAGE_SECTOR_SIZE, &st);
        hal_storage_status_t hs = raw_write(&ex, s, BUF, chunk);
        if (hs != HAL_STORAGE_OK) {
            printf("FAIL: raw write at sector +%u: %s\n", (unsigned)s, hal_storage_status_str(hs));
            return RES_FAIL;
        }
        if (quit_requested()) {
            return RES_QUIT;
        }
    }
    uint32_t wr_ms = hal_millis() - t0;
    st = RAW_SEED;
    uint32_t bad = 0, reported = 0;
    t0 = hal_millis();
    for (uint32_t s = 0; s < ex.sectors; s += chunk) {
        hal_storage_status_t hs = hal_storage_read(ex.first_lba + s, BUF, chunk);
        if (hs != HAL_STORAGE_OK) {
            printf("FAIL: raw read at sector +%u: %s\n", (unsigned)s, hal_storage_status_str(hs));
            return RES_FAIL;
        }
        bad += compare_pattern(BUF, chunk * HAL_STORAGE_SECTOR_SIZE, &st,
                               (uint64_t)s * HAL_STORAGE_SECTOR_SIZE, &reported);
    }
    printf("raw: wrote %u ms, read+compare %u ms, %u mismatching bytes\n", (unsigned)wr_ms,
           (unsigned)(hal_millis() - t0), (unsigned)bad);
    if (bad != 0) {
        printf("FAIL: raw read-back mismatch\n");
        return RES_FAIL;
    }
    return RES_PASS;
}

static result_t test_integrity(void) {
    printf("\n== Test 4: data integrity (writes ONLY under " TEST_DIR ") ==\n");
    result_t r = session_begin(PATTERN_BYTES + RAW_BYTES);
    if (r != RES_PASS) {
        return r;
    }
    r = integrity_files();
    result_t raw = RES_SKIP;
    if (r == RES_PASS) {
        raw = integrity_raw();
        if (raw == RES_FAIL || raw == RES_QUIT) {
            r = raw;
        }
    }
    session_end();
    if (r == RES_PASS) {
        printf("PASS: 16 MB file round trip across remount%s\n",
               raw == RES_PASS ? " and 2 MB raw-sector round trip" : " (raw part skipped, see above)");
    }
    return r;
}

// --- test 5-7: speed -------------------------------------------------------------------------

#define SPEED_FILE TEST_DIR "/speed.bin"
static uint32_t g_best_write100, g_best_read100; // from test 5, used by test 7

// Create the 32 MB speed file (allocated, not written; contents are whatever the clusters held).
static result_t make_speed_file(void) {
    nfs_file_t *f;
    nfs_err_t e = ses_create(&f, SPEED_FILE);
    if (e != NFS_OK) {
        printf("FAIL: create speed file: %s\n", nfs_err_str(e));
        return RES_FAIL;
    }
    uint32_t t0 = hal_millis();
    e = nfs_preallocate(f, SPEED_FILE_BYTES, false);
    nfs_err_t ce = ses_close(f);
    if (e != NFS_OK || ce != NFS_OK) {
        printf("FAIL: preallocate 32 MB: %s/%s\n", nfs_err_str(e), nfs_err_str(ce));
        return RES_FAIL;
    }
    printf("pre-allocated 32 MB in %u ms (not counted in the speeds)\n", (unsigned)(hal_millis() - t0));
    return RES_PASS;
}

static result_t speed_sequential(void) {
    static const struct {
        uint32_t block;
        uint32_t total;
    } runs[] = {{512, 2u << 20}, {4096, 8u << 20}, {32768, 32u << 20}, {131072, 32u << 20}};
    uint32_t st = 0xBEEF0001u;
    fill_pattern(BUF, BUF_BYTES, &st);
    printf("block      bytes      write MB/s   read MB/s\n");
    g_best_write100 = g_best_read100 = 0;
    for (size_t k = 0; k < sizeof runs / sizeof runs[0]; k++) {
        nfs_file_t *f;
        nfs_err_t e = ses_open(&f, SPEED_FILE, NFS_O_WRITE);
        if (e != NFS_OK) {
            printf("FAIL: open for write: %s\n", nfs_err_str(e));
            return RES_FAIL;
        }
        uint64_t t0 = hal_micros();
        for (uint32_t off = 0; off < runs[k].total; off += runs[k].block) {
            size_t put = 0;
            e = nfs_write(f, BUF, runs[k].block, &put);
            if (e != NFS_OK || put != runs[k].block) {
                printf("FAIL: write block %u: %s\n", (unsigned)(off / runs[k].block), nfs_err_str(e));
                return RES_FAIL;
            }
            if ((off & 0xFFFFFu) == 0 && quit_requested()) {
                return RES_QUIT;
            }
        }
        e = nfs_sync(f);
        uint64_t wr_us = hal_micros() - t0;
        ses_close(f);
        if (e != NFS_OK) {
            printf("FAIL: sync: %s\n", nfs_err_str(e));
            return RES_FAIL;
        }
        e = ses_open(&f, SPEED_FILE, NFS_O_READ);
        if (e != NFS_OK) {
            printf("FAIL: open for read: %s\n", nfs_err_str(e));
            return RES_FAIL;
        }
        t0 = hal_micros();
        for (uint32_t off = 0; off < runs[k].total; off += runs[k].block) {
            size_t got = 0;
            e = nfs_read(f, BUF, runs[k].block, &got);
            if (e != NFS_OK || got != runs[k].block) {
                printf("FAIL: read block %u: %s\n", (unsigned)(off / runs[k].block), nfs_err_str(e));
                ses_close(f);
                return RES_FAIL;
            }
            if ((off & 0xFFFFFu) == 0 && quit_requested()) {
                ses_close(f);
                return RES_QUIT;
            }
        }
        uint64_t rd_us = hal_micros() - t0;
        ses_close(f);
        uint32_t w = mbs100(runs[k].total, wr_us);
        uint32_t r = mbs100(runs[k].total, rd_us);
        printf("%6u B  %6u KB   ", (unsigned)runs[k].block, (unsigned)(runs[k].total >> 10));
        print_mbs(w);
        printf("        ");
        print_mbs(r);
        printf("\n");
        g_best_write100 = w > g_best_write100 ? w : g_best_write100;
        g_best_read100 = r > g_best_read100 ? r : g_best_read100;
    }
    return RES_PASS;
}

static result_t test_speed_seq(void) {
    printf("\n== Test 5: sequential speed (writes ONLY " SPEED_FILE ") ==\n");
    printf("MB = 1,000,000 bytes. Write timing includes the final f_sync; the file is\n"
           "pre-allocated first so FAT allocation is excluded. 512 B / 4 KB runs move less data\n"
           "(2 / 8 MB) to bound the time. Clock: ");
    print_mbs(hal_storage_get_bus_clock() / 10000u);
    printf(" MHz, 4-bit.\n");
    result_t r = session_begin(SPEED_FILE_BYTES);
    if (r != RES_PASS) {
        return r;
    }
    r = make_speed_file();
    if (r == RES_PASS) {
        r = speed_sequential();
    }
    session_end();
    if (r == RES_PASS) {
        printf("DMA vs CPU split: not exposed by the SD library (no async transfer API), not measured.\n");
        printf("PASS: best write ");
        print_mbs(g_best_write100);
        printf(" MB/s, best read ");
        print_mbs(g_best_read100);
        printf(" MB/s\n");
    }
    return r;
}

#define RANDOM_READS 2000u
static uint32_t g_lat[RANDOM_READS];

static int cmp_u32(const void *a, const void *b) {
    uint32_t x = *(const uint32_t *)a, y = *(const uint32_t *)b;
    return x < y ? -1 : x > y;
}

static result_t random_run(bool fast, bool *skipped) {
    nfs_file_t *f;
    nfs_err_t e = ses_open(&f, SPEED_FILE, NFS_O_READ);
    if (e != NFS_OK) {
        printf("FAIL: open: %s\n", nfs_err_str(e));
        return RES_FAIL;
    }
    if (fast) {
        e = nfs_fast_seek(f);
        if (e != NFS_OK) {
            printf("fast seek (link map): not available (%s)\n", nfs_err_str(e));
            ses_close(f);
            *skipped = true;
            return RES_PASS;
        }
    }
    uint32_t rng = 0x13572468u;
    uint32_t blocks = SPEED_FILE_BYTES / 4096u;
    for (uint32_t i = 0; i < RANDOM_READS; i++) {
        uint64_t off = (uint64_t)(xs_next(&rng) % blocks) * 4096u;
        uint64_t t0 = hal_micros();
        size_t got = 0;
        e = nfs_seek(f, off);
        if (e == NFS_OK) {
            e = nfs_read(f, BUF, 4096, &got);
        }
        g_lat[i] = (uint32_t)(hal_micros() - t0);
        if (e != NFS_OK || got != 4096) {
            printf("FAIL: random read %u at %llu: %s\n", (unsigned)i, (unsigned long long)off,
                   nfs_err_str(e));
            ses_close(f);
            return RES_FAIL;
        }
        if (i % 100 == 99 && quit_requested()) {
            ses_close(f);
            return RES_QUIT;
        }
    }
    ses_close(f);
    uint64_t sum = 0;
    uint32_t mn = g_lat[0], mx = g_lat[0];
    for (uint32_t i = 0; i < RANDOM_READS; i++) {
        sum += g_lat[i];
        mn = g_lat[i] < mn ? g_lat[i] : mn;
        mx = g_lat[i] > mx ? g_lat[i] : mx;
    }
    qsort(g_lat, RANDOM_READS, sizeof g_lat[0], cmp_u32);
    uint32_t avg = (uint32_t)(sum / RANDOM_READS);
    printf("%-10s avg %u us, min %u us, max %u us, p99 %u us  (%u IOPS)\n",
           fast ? "fast seek" : "plain seek", (unsigned)avg, (unsigned)mn, (unsigned)mx,
           (unsigned)g_lat[(RANDOM_READS * 99 + 99) / 100 - 1], (unsigned)(1000000u / (avg ? avg : 1)));
    return RES_PASS;
}

static result_t test_speed_random(void) {
    printf("\n== Test 6: random 4 KB reads (writes ONLY " SPEED_FILE ", allocation only) ==\n");
    result_t r = session_begin(SPEED_FILE_BYTES);
    if (r != RES_PASS) {
        return r;
    }
    r = make_speed_file();
    bool skipped = false;
    if (r == RES_PASS) {
        printf("%u reads, 4 KB aligned, seeded PRNG, same offsets for both runs\n", RANDOM_READS);
        r = random_run(false, &skipped);
    }
    if (r == RES_PASS) {
        r = random_run(true, &skipped);
    }
    session_end();
    if (r == RES_PASS) {
        printf("PASS: random read latency measured%s\n", skipped ? " (fast seek unavailable)" : "");
    }
    return r;
}

#define STREAM_MS 10000u
#define STREAM_PERIOD_US 20000u
#define STREAM_CHUNK 32768u

static result_t test_stream(void) {
    printf("\n== Test 7: streaming 32 KB every 20 ms for 10 s (writes ONLY " SPEED_FILE ") ==\n");
    result_t r = session_begin(SPEED_FILE_BYTES);
    if (r != RES_PASS) {
        return r;
    }
    r = make_speed_file();
    uint32_t reads = 0, misses = 0;
    uint32_t worst = 0;
    uint64_t sum = 0;
    if (r == RES_PASS) {
        nfs_file_t *f;
        nfs_err_t e = ses_open(&f, SPEED_FILE, NFS_O_READ);
        if (e != NFS_OK) {
            printf("FAIL: open: %s\n", nfs_err_str(e));
            r = RES_FAIL;
        } else {
            uint64_t start = hal_micros();
            uint64_t next = start + STREAM_PERIOD_US;
            while (hal_micros() - start < (uint64_t)STREAM_MS * 1000u) {
                uint64_t t0 = hal_micros();
                size_t got = 0;
                e = nfs_read(f, BUF, STREAM_CHUNK, &got);
                uint64_t t1 = hal_micros();
                if (e != NFS_OK) {
                    printf("FAIL: read: %s\n", nfs_err_str(e));
                    r = RES_FAIL;
                    break;
                }
                if (got < STREAM_CHUNK) {
                    nfs_seek(f, 0); // wrap at end of file
                }
                uint32_t dt = (uint32_t)(t1 - t0);
                reads++;
                sum += dt;
                worst = dt > worst ? dt : worst;
                if (t1 > next) {
                    misses++;
                    next = t1; // late: start the next period now instead of piling up
                }
                // Burn the rest of the period (a decoder would be using it).
                while (hal_micros() < next) {
                    if (quit_requested()) {
                        r = RES_QUIT;
                        break;
                    }
                }
                if (r == RES_QUIT) {
                    break;
                }
                next += STREAM_PERIOD_US;
            }
            ses_close(f);
        }
    }
    session_end();
    if (r != RES_PASS && r != RES_FAIL) {
        return r;
    }
    if (reads == 0) {
        return r;
    }
    uint32_t avg = (uint32_t)(sum / reads);
    int headroom = (int)(100 - (int)((uint64_t)worst * 100u / STREAM_PERIOD_US));
    printf("%u reads: deadline misses %u, worst %u us, average %u us, headroom %d%% of 20 ms\n",
           (unsigned)reads, (unsigned)misses, (unsigned)worst, (unsigned)avg, headroom);
    printf("demand 32 KB / 20 ms = 1.64 MB/s (FLAC 24/96 needs about 0.6 MB/s sustained, so this "
           "is ~2.7x harder); read rate while reading: ");
    print_mbs(mbs100(STREAM_CHUNK, avg));
    printf(" MB/s\n");
    if (g_best_write100 == 0) {
        printf("USB mass-storage target (>= 10 MB/s): run test 5 first to compare.\n");
    } else {
        printf("USB mass-storage target >= 10 MB/s: best read %s, best write %s (test 5, at the "
               "current clock)\n", g_best_read100 >= 1000 ? "MET" : "NOT met",
               g_best_write100 >= 1000 ? "MET" : "NOT met");
    }
    if (misses != 0) {
        printf("FAIL: %u deadline misses\n", (unsigned)misses);
        return RES_FAIL;
    }
    if (r == RES_FAIL) {
        return r;
    }
    printf("PASS: no deadline misses in 10 s, headroom %d%%\n", headroom);
    return RES_PASS;
}

// --- test 8: clock sweep -------------------------------------------------------------------

/*
 * Confinement: this test raises the bus clock until it fails, so a bad bus is EXPECTED. To keep a
 * failure from corrupting the file system:
 *  - the sweep file is allocated contiguously at the safe clock and its sector range is found
 *    first; nothing FatFs-related (allocation, sync, close, delete) happens at a test clock;
 *  - at a test clock the only writes are raw writes inside that range (raw_write() checks it);
 *  - before writing at a new clock the bus must pass a read-only soak at that clock;
 *  - the clock is restored (and the card re-initialised if it misbehaved) before any cleanup.
 * It also requires that this session created /neupod-test itself, and is never part of run-all.
 */
#define SWEEP_FILE TEST_DIR "/sweep.bin"
#define SWEEP_BYTES (4u * 1024u * 1024u)
#define SWEEP_CHUNK_SECTORS 64u // 32 KB

// Read-only soak at the current clock: read the file's range twice (must be identical) and query
// the card status ~100 times (CMD13, no error bits, transfer state).
static bool sweep_soak(const nfs_extent_t *ex, const char **why) {
    uint32_t crc[2] = {0, 0};
    for (int pass = 0; pass < 2; pass++) {
        for (uint32_t s0 = 0; s0 < ex->sectors; s0 += SWEEP_CHUNK_SECTORS) {
            if (hal_storage_read(ex->first_lba + s0, BUF, SWEEP_CHUNK_SECTORS) != HAL_STORAGE_OK) {
                *why = "soak: read error";
                return false;
            }
            crc[pass] = crc32_update(crc[pass], BUF, SWEEP_CHUNK_SECTORS * HAL_STORAGE_SECTOR_SIZE);
        }
    }
    if (crc[0] != crc[1]) {
        *why = "soak: reads differ";
        return false;
    }
    for (int i = 0; i < 100; i++) {
        if (hal_storage_card_status(NULL) != HAL_STORAGE_OK) {
            *why = "soak: card status error";
            return false;
        }
    }
    return true;
}

// One step: raw-write 4 MB at 32 KB (CRC of what was generated), read back (CRC of what came back).
static bool sweep_step(const nfs_extent_t *ex, uint32_t seed, uint32_t *wr100, uint32_t *rd100,
                       const char **why) {
    uint32_t st = seed, crc_w = 0, crc_r = 0;
    uint64_t t0 = hal_micros();
    for (uint32_t s0 = 0; s0 < ex->sectors; s0 += SWEEP_CHUNK_SECTORS) {
        fill_pattern(BUF, SWEEP_CHUNK_SECTORS * HAL_STORAGE_SECTOR_SIZE, &st);
        crc_w = crc32_update(crc_w, BUF, SWEEP_CHUNK_SECTORS * HAL_STORAGE_SECTOR_SIZE);
        if (raw_write(ex, s0, BUF, SWEEP_CHUNK_SECTORS) != HAL_STORAGE_OK) {
            *why = "write error";
            return false;
        }
    }
    uint64_t wr_us = hal_micros() - t0;
    t0 = hal_micros();
    for (uint32_t s0 = 0; s0 < ex->sectors; s0 += SWEEP_CHUNK_SECTORS) {
        if (hal_storage_read(ex->first_lba + s0, BUF, SWEEP_CHUNK_SECTORS) != HAL_STORAGE_OK) {
            *why = "read error";
            return false;
        }
        crc_r = crc32_update(crc_r, BUF, SWEEP_CHUNK_SECTORS * HAL_STORAGE_SECTOR_SIZE);
    }
    uint64_t rd_us = hal_micros() - t0;
    *wr100 = mbs100(SWEEP_BYTES, wr_us);
    *rd100 = mbs100(SWEEP_BYTES, rd_us);
    if (crc_r != crc_w) {
        *why = "data CRC mismatch";
        return false;
    }
    return true;
}

// Put the bus back to @p hz and prove the card is usable again (status clean, a sector reads, the
// volume mounts). If the card is in High Speed mode or the quick path fails, do a full re-init
// (which first quiesces the card at the current clock, then resets it) with up to 3 tries.
// Returns false if the card could not be brought back; the caller must then FAIL.
static bool restore_clock(uint32_t hz) {
    static uint32_t s0[HAL_STORAGE_SECTOR_SIZE / 4];
    hal_card_info_t ci;
    bool hs = hal_storage_get_info(&ci) == HAL_STORAGE_OK && ci.high_speed;
    if (!hs && hal_storage_set_bus_clock(hz) == HAL_STORAGE_OK && !hal_storage_write_fault() &&
        hal_storage_read(0, s0, 1) == HAL_STORAGE_OK && hal_storage_card_status(NULL) == HAL_STORAGE_OK) {
        return true;
    }
    printf(hs ? "leaving High Speed mode: re-initialising the card (expected)\n"
              : "bus did not recover cleanly; re-initialising the card\n");
    for (int attempt = 0; attempt < 3; attempt++) {
        if (attempt > 0) {
            printf("restore: retry %d\n", attempt);
            hal_delay_ms(300);
        }
        hal_storage_unmount();
        if (hal_storage_init() != HAL_STORAGE_OK) {
            continue;
        }
        if (hal_storage_set_bus_clock(hz) != HAL_STORAGE_OK) {
            continue;
        }
        hal_storage_status_t ms = hal_storage_mount();
        if ((ms == HAL_STORAGE_OK || ms == HAL_STORAGE_UNSUPPORTED) &&
            hal_storage_read(0, s0, 1) == HAL_STORAGE_OK && hal_storage_card_status(NULL) == HAL_STORAGE_OK) {
            return true;
        }
    }
    return false;
}

static result_t test_sweep(void) {
    // Integer PIO dividers only (125 MHz / (4 * n)): fractional dividers mis-sample the last
    // response CRC bit on this PIO program (see sdio_config.h), which would hide the real limit.
    static const uint32_t steps_hz[] = {7812500u, 10416667u, 15625000u, 31250000u};
    printf("\n== Test 8: SDIO clock sweep (writes ONLY inside " SWEEP_FILE ") ==\n");
    hal_card_info_t ci;
    if (!hal_storage_ready() && hal_storage_init() != HAL_STORAGE_OK) {
        printf("FAIL: no card\n");
        return RES_FAIL;
    }
    hal_storage_get_info(&ci);
    if (ci.type == HAL_CARD_IMAGE) {
        printf("SKIP: the bus clock cannot be changed on the host\n");
        return RES_SKIP;
    }
    uint32_t orig_hz = hal_storage_get_bus_clock();
    printf("CSD TRAN_SPEED 0x%02X: card is rated %u MHz in default-speed mode; steps above 25 MHz need "
           "High Speed mode (CMD6). The SD library never sends CMD6; the HAL does it via the "
           "library's low-level calls.\n", ci.tran_speed_raw, (unsigned)(ci.max_bus_hz / 1000000u));
    result_t r = session_begin(SWEEP_BYTES);
    if (r != RES_PASS) {
        return r;
    }
    if (!g_ses.dir_created) {
        printf("FAIL: refusing to run: " TEST_DIR " already existed (not created by this test). Remove "
               "it on a PC first so the test owns everything it writes.\n");
        session_end();
        return RES_FAIL;
    }

    // At the safe clock: allocate one contiguous file with FatFs and find where it is.
    nfs_file_t *f;
    nfs_extent_t ex = {0, 0};
    nfs_err_t e = ses_create(&f, SWEEP_FILE);
    if (e == NFS_OK) {
        e = nfs_preallocate(f, SWEEP_BYTES, true);
        if (e == NFS_OK) {
            e = nfs_file_extent(f, &ex); // also syncs the file
        }
        ses_close(f);
    }
    if (e != NFS_OK || ex.sectors != SWEEP_BYTES / HAL_STORAGE_SECTOR_SIZE) {
        printf("FAIL: cannot allocate a contiguous, locatable sweep file (%s). The sweep needs one so "
               "that no file system write happens at a test clock.\n", nfs_err_str(e));
        session_end();
        return RES_FAIL;
    }
    printf("sweep file at sectors %u..%u; only raw writes inside it happen at test clocks\n",
           (unsigned)ex.first_lba, (unsigned)(ex.first_lba + ex.sectors - 1));

    int last_good = -1;
    const char *stop_reason = "reached the last step";
    printf("clock MHz   write MB/s  read MB/s  result\n");
    for (size_t k = 0; k < sizeof steps_hz / sizeof steps_hz[0]; k++) {
        if (steps_hz[k] > 25000000u) {
            hal_card_info_t cur;
            if (hal_storage_get_info(&cur) == HAL_STORAGE_OK && !cur.high_speed) {
                hal_storage_status_t hs = hal_storage_enable_high_speed();
                if (hs != HAL_STORAGE_OK) {
                    printf("capped at 25 MHz: High Speed switch failed (%s): %s\n",
                           hal_storage_status_str(hs),
                           hs == HAL_STORAGE_UNSUPPORTED ? "card does not offer High Speed"
                                                         : "CMD6 error");
                    stop_reason = "High Speed mode unavailable";
                    break;
                }
                printf("card switched to High Speed (CMD6): max clock now 50 MHz\n");
            }
        }
        hal_storage_status_t cs = hal_storage_set_bus_clock(steps_hz[k]);
        if (cs != HAL_STORAGE_OK) {
            printf("capped: cannot set %u Hz (%s)\n", (unsigned)steps_hz[k], hal_storage_status_str(cs));
            stop_reason = "clock change refused";
            break;
        }
        uint32_t actual = hal_storage_get_bus_clock();
        printf("%2u.%03u      ", (unsigned)(actual / 1000000u), (unsigned)(actual / 1000u % 1000u));
        uint32_t w = 0, rd = 0;
        const char *why = "";
        if (!sweep_soak(&ex, &why)) { // read-only first: no write until the bus has proven itself
            printf("--          --         STOP (%s; no write attempted)\n", why);
            stop_reason = why;
            break;
        }
        if (sweep_step(&ex, 0x1234ABCDu + (uint32_t)k, &w, &rd, &why)) {
            print_mbs(w);
            printf("        ");
            print_mbs(rd);
            printf("       OK\n");
            last_good = (int)k;
        } else {
            printf("--          --         FAIL (%s)\n", why);
            stop_reason = why;
            break;
        }
        if (quit_requested()) {
            r = RES_QUIT;
            stop_reason = "stopped by user";
            break;
        }
    }
    // Back to the original clock before ANY file system operation (cleanup deletes the file).
    bool restored = restore_clock(orig_hz);
    session_end(); // retries recovery itself and lists leftovers if it cannot clean up
    if (!restored) {
        printf("FAIL: the card could not be brought back to %u Hz after the sweep. No setting is "
               "suggested. Power-cycle the board, then use key c to remove any leftover test "
               "files.\n", (unsigned)orig_hz);
        return RES_FAIL;
    }
    if (r == RES_QUIT) {
        return r;
    }
    if (last_good < 0) {
        printf("FAIL: even %u Hz was not reliable (%s)\n", (unsigned)steps_hz[0], stop_reason);
        return RES_FAIL;
    }
    int pick = last_good > 0 ? last_good - 1 : 0; // last good minus one step, as margin
    printf("last good step: %u Hz (stopped: %s). Suggested setting, one step of margin:\n",
           (unsigned)steps_hz[last_good], stop_reason);
    printf("  #define NEUPOD_SDIO_BAUD_HZ %uu   /* in platforms/rp2350/hal/sdio_config.h */\n",
           (unsigned)steps_hz[pick]);
    printf("clock restored to %u Hz\nPASS: sweep complete\n", (unsigned)hal_storage_get_bus_clock());
    return RES_PASS;
}

// --- cleanup of leftovers ('c') ---------------------------------------------------------------

static const char *const k_known_files[] = {"pattern.bin", "raw.bin", "speed.bin", "sweep.bin"};

static bool is_known_file(const char *name) {
    for (size_t i = 0; i < sizeof k_known_files / sizeof k_known_files[0]; i++) {
        if (strcmp(name, k_known_files[i]) == 0) {
            return true;
        }
    }
    return false;
}

// Remove leftovers of an interrupted test: lists TEST_DIR first (read-only), asks y/N, then
// deletes ONLY the known test file names and the directory if that leaves it empty.
static void cleanup_leftovers(void) {
    printf("\n== Clean up leftover test files ==\n");
    if (!ensure_mounted()) {
        return;
    }
    nfs_dir_t *d = NULL;
    nfs_err_t e = nfs_opendir(&d, TEST_DIR);
    if (e == NFS_ERR_NOT_FOUND) {
        printf(TEST_DIR " does not exist: nothing to clean.\n");
        return;
    }
    if (e != NFS_OK) {
        printf("cannot read " TEST_DIR " (%s)\n", nfs_err_str(e));
        return;
    }
    unsigned known = 0, other = 0;
    bool done = false;
    nfs_dirent_t ent;
    printf("Contents of " TEST_DIR ":\n");
    while (nfs_readdir(d, &ent, &done) == NFS_OK && !done) {
        bool del = !ent.is_dir && is_known_file(ent.name);
        printf("  %s%s  %llu bytes  -> %s\n", ent.name, ent.is_dir ? "/" : "",
               (unsigned long long)ent.size, del ? "will be deleted" : "KEPT (not a test file)");
        known += del;
        other += !del;
    }
    nfs_closedir(d);
    if (known == 0 && other != 0) {
        printf("No known test files here; nothing will be deleted.\n");
        return;
    }
    printf("Delete %u file(s)%s? [y/N] ", known,
           other == 0 ? " and remove the then-empty directory " TEST_DIR : "");
    fflush(stdout);
    while (hal_console_getc_timeout(0) >= 0) {
        // drop earlier keys
    }
    int c = hal_console_getc_timeout(30000);
    printf("%c\n", c > 31 ? c : '-');
    if (c != 'y' && c != 'Y') {
        printf("Cancelled. Nothing was written.\n");
        return;
    }
    if (hal_storage_write_fault()) {
        hal_storage_unmount();
        if (hal_storage_init() != HAL_STORAGE_OK || !ensure_mounted()) {
            printf("card did not recover; nothing was written\n");
            return;
        }
    }
    if (!read_soak()) {
        return;
    }
    hal_storage_set_write_enabled(true); // only for the deletes below
    char path[NFS_PATH_MAX];
    for (size_t i = 0; i < sizeof k_known_files / sizeof k_known_files[0]; i++) {
        snprintf(path, sizeof path, TEST_DIR "/%s", k_known_files[i]);
        e = nfs_unlink(path);
        if (e == NFS_OK) {
            printf("deleted %s\n", path);
        } else if (e != NFS_ERR_NOT_FOUND) {
            printf("could not delete %s (%s)\n", path, nfs_err_str(e));
        }
    }
    e = nfs_unlink(TEST_DIR); // fails with NOT_EMPTY if anything else is in there: kept
    printf(e == NFS_OK ? "removed " TEST_DIR "\n" : "left " TEST_DIR " in place (%s)\n", nfs_err_str(e));
    hal_storage_set_write_enabled(false);
}

// --- test 9: format (destructive, confirmed) ---------------------------------------------------

// Read a line with echo; returns false on timeout (60 s idle). Backspace edits.
static bool read_line(char *out, size_t cap) {
    size_t n = 0;
    uint32_t idle = 0;
    for (;;) {
        int c = hal_console_getc_timeout(100);
        if (c < 0) {
            if (++idle > 600) {
                return false;
            }
            continue;
        }
        idle = 0;
        if (c == '\r' || c == '\n') {
            out[n] = '\0';
            printf("\n");
            return true;
        }
        if ((c == 8 || c == 127) && n > 0) {
            n--;
            printf("\b \b");
        } else if (c >= 32 && c < 127 && n + 1 < cap) {
            out[n++] = (char)c;
            printf("%c", c);
        }
        fflush(stdout);
    }
}

static result_t test_format(void) {
    printf("\n== Test 9: FORMAT the card (MBR + one exFAT partition) ==\n");
    hal_card_info_t ci;
    if (!hal_storage_ready() && hal_storage_init() != HAL_STORAGE_OK) {
        printf("FAIL: no card\n");
        return RES_FAIL;
    }
    hal_storage_get_info(&ci);
    if (ci.type == HAL_CARD_IMAGE) {
        printf("SKIP: formatting is not supported on the host\n");
        return RES_SKIP;
    }
    uint32_t align = ci.au_size_bytes ? ci.au_size_bytes / HAL_STORAGE_SECTOR_SIZE : 8192u;
    printf("Card: %s \"%s\" %s, %u.%02u GB, serial 0x%08X\n", hal_storage_card_type_str(ci.type),
           ci.pnm, ci.oid, (unsigned)(ci.capacity_bytes / 1000000000u),
           (unsigned)(ci.capacity_bytes / 10000000u % 100u), (unsigned)ci.psn);
    // Show what is there now (read-only), as far as FatFs can see it.
    hal_volume_info_t vi;
    uint64_t total = 0, freeb = 0;
    if (hal_storage_mount() == HAL_STORAGE_OK && hal_storage_volume_info(&vi) == HAL_STORAGE_OK &&
        nfs_space(&total, &freeb) == NFS_OK) {
        printf("Current volume: %s, label \"%s\", %u MB used of %u MB\n", fs_name(vi.fs), vi.label,
               (unsigned)((total - freeb) / 1000000u), (unsigned)(total / 1000000u));
    } else {
        printf("Current volume: none that FatFs can read\n");
    }
    hal_storage_unmount();
    printf("!!! ALL DATA ON THIS CARD WILL BE ERASED, including other partitions such as a Linux root\n"
           "    file system that FatFs cannot even see !!!\n"
           "New layout: MBR with one exFAT partition starting at sector %u (%s), 128 KB clusters,\n"
           "label NEUPOD. Type YES and press Enter to continue; anything else cancels: ",
           (unsigned)align, ci.au_size_bytes ? "card AU size" : "4 MiB fallback");
    fflush(stdout);
    while (hal_console_getc_timeout(0) >= 0) {
        // drop any keys typed earlier so a stray 'Y' cannot count
    }
    char line[16];
    if (!read_line(line, sizeof line) || strcmp(line, "YES") != 0) {
        printf("Cancelled. Nothing was written.\n");
        return RES_SKIP;
    }
    printf("Formatting... (this can take a minute)\n");
    hal_format_opts_t opts = {.cluster_bytes = 131072u, .align_sectors = align, .erase_all_confirmed = true};
    uint32_t t0 = hal_millis();
    hal_storage_status_t st = hal_storage_format("NEUPOD", &opts); // locks writes again on return
    uint32_t ms = hal_millis() - t0;
    hal_storage_set_write_enabled(false);
    printf("format: %s in %u ms\n", hal_storage_status_str(st), (unsigned)ms);
    if (st != HAL_STORAGE_OK) {
        printf("FAIL: format failed (%s); the card may be unusable until formatted again\n",
               hal_storage_status_str(st));
        return RES_FAIL;
    }
    printf("-- remount check (test 2) --\n");
    result_t r = test_mount();
    if (r != RES_PASS) {
        printf("FAIL: formatted but could not mount\n");
        return RES_FAIL;
    }
    printf("PASS: card formatted (MBR + exFAT partition) and mounted (format took %u ms)\n", (unsigned)ms);
    return RES_PASS;
}

// --- menu ----------------------------------------------------------------------------------

static const struct {
    const char *name;
    result_t (*run)(void);
    bool in_run_all;
    const char *skip_note;
} k_tests[] = {
    {"detect (read-only)", test_detect, true, ""},
    {"mount (read-only)", test_mount, true, ""},
    {"list (read-only)", test_list, true, ""},
    {"integrity (writes)", test_integrity, true, ""},
    {"speed sequential", test_speed_seq, true, ""},
    {"speed random", test_speed_random, true, ""},
    {"streaming", test_stream, true, ""},
    {"clock sweep (alone)", test_sweep, false, "  (run individually)"},
    {"FORMAT (needs YES)", test_format, false, "  (not run: needs YES)"}, // never part of run-all
};
#define NUM_TESTS (sizeof k_tests / sizeof k_tests[0])

static void print_menu(void) {
    printf("\n=== neuPod microSD tests ===\n");
    for (size_t k = 0; k < NUM_TESTS; k++) {
        printf("  %u  %s\n", (unsigned)(k + 1), k_tests[k].name);
    }
    printf("  a  run tests 1-7 with summary (never 8 or 9)   q  stop / back to menu\n"
           "  c  clean up leftover test files (asks first)\n"
           "  h  or Enter: this menu\n"
           "Tests 4-8 write only under " TEST_DIR " and delete it; 9 erases the card.\nkey: ");
    fflush(stdout);
}

static const char *result_str(result_t r) {
    return r == RES_PASS ? "PASS" : r == RES_FAIL ? "FAIL" : r == RES_SKIP ? "SKIP" : "STOP";
}

static result_t run_test(size_t idx) {
    result_t r = k_tests[idx].run();
    hal_storage_set_write_enabled(false); // defence in depth: never leave a test with writes on
    size_t stack_free = hal_stack_free_min();
    if (stack_free != 0) {
        printf("[stack: at least %u bytes were always free]\n", (unsigned)stack_free);
    } else if (strcmp(hal_platform_name(), "rp2350") == 0) { // 0 = unknown on other platforms
        printf("[STACK EXHAUSTED: the whole painted stack was used; results are not trustworthy]\n");
    }
    return r;
}

void storage_tests_run_menu(void) {
    hal_storage_set_write_enabled(false);
    print_menu();
    for (;;) {
        int c = hal_console_getc_timeout(100);
        if (c < 0) {
            continue;
        }
        if (c >= '1' && c < '1' + (int)NUM_TESTS) {
            printf("%c\n", c);
            uint32_t t0 = hal_millis();
            result_t r = run_test((size_t)(c - '1'));
            printf("-> %s (%u ms)\n", result_str(r), (unsigned)(hal_millis() - t0));
            print_menu();
        } else if (c == 'a' || c == 'A') {
            printf("a\n");
            result_t res[NUM_TESTS];
            uint32_t ms[NUM_TESTS];
            size_t done = 0;
            for (; done < NUM_TESTS; done++) {
                if (!k_tests[done].in_run_all) {
                    res[done] = RES_SKIP; // destructive: only ever run by choosing it directly
                    ms[done] = 0;
                    continue;
                }
                uint32_t t0 = hal_millis();
                res[done] = run_test(done);
                ms[done] = hal_millis() - t0;
                if (res[done] == RES_QUIT) {
                    done++;
                    break;
                }
            }
            printf("\n=== Summary ===\n #  %-22s result   ms\n", "test");
            for (size_t k = 0; k < done; k++) {
                printf(" %u  %-22s %-6s %7u%s\n", (unsigned)(k + 1), k_tests[k].name,
                       result_str(res[k]), (unsigned)ms[k],
                       k_tests[k].skip_note);
            }
            print_menu();
        } else if (c == 'c' || c == 'C') {
            printf("c\n");
            cleanup_leftovers();
            hal_storage_set_write_enabled(false);
            print_menu();
        } else if (c == 'h' || c == '?' || c == '\r' || c == '\n') {
            print_menu();
        }
    }
}
