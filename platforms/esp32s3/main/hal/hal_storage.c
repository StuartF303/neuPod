// SPDX-License-Identifier: MPL-2.0
/*
 * Block storage: the microSD in SPI mode (SPI3: CS 2, SCK 4, MOSI 5, MISO 6) through ESP-IDF's sdspi host and sdmmc protocol code,
 * under neuPod's own FatFs (app/third_party/fatfs, app/storage/diskio.c -> hal_storage_read/write), the same layering as the Pico
 * and the Y2. IDF's fatfs component is not part of this build (platforms/esp32s3/CMakeLists.txt).
 *
 * Write safety as on every board: writes are locked out until hal_storage_set_write_enabled(true); the boot check only reads.
 * A failed write latches hal_storage_write_fault() until the next hal_storage_init().
 */
#include <stdio.h>
#include <string.h>

#include "board.h"
#include "driver/sdspi_host.h"
#include "driver/spi_master.h"
#include "ff.h"
#include "hal_storage.h"
#include "hal_storage_decode.h"
#include "sd_protocol_defs.h"
#include "sdmmc_cmd.h"

#define SD_HOST SPI3_HOST
#define SD_MAX_FREQ_KHZ 20000 /* the probe ran 20 MHz; the SPI-mode default-speed ceiling is 25 MHz */

static sdmmc_card_t g_card;
static sdspi_dev_handle_t g_slot;
static bool g_bus_up, g_host_up, g_slot_up;
static bool g_ready;
static bool g_write_enabled;
static bool g_write_fault;
static hal_card_info_t g_info;
static uint32_t g_sectors;

static FATFS g_fatfs;
static bool g_mounted;

// Release the device (not the SPI bus or the sdspi host: re-initialising a card is then cheap and safe to repeat).
static void drop_card(void) {
    if (g_slot_up) {
        sdspi_host_remove_device(g_slot);
        g_slot_up = false;
    }
    g_ready = false;
    g_sectors = 0;
}

static hal_storage_status_t map_err(esp_err_t e) {
    switch (e) {
    case ESP_OK: return HAL_STORAGE_OK;
    case ESP_ERR_TIMEOUT: return HAL_STORAGE_TIMEOUT;
    case ESP_ERR_INVALID_CRC: return HAL_STORAGE_CRC;
    default: return HAL_STORAGE_IO;
    }
}

static void fill_info(void) {
    memset(&g_info, 0, sizeof g_info);
    g_sectors = (uint32_t)g_card.csd.capacity;
    g_info.sectors = g_sectors;
    g_info.capacity_bytes = (uint64_t)g_card.csd.capacity * g_card.csd.sector_size;
    g_info.type = (g_card.ocr & SD_OCR_SDHC_CAP) ? (g_info.capacity_bytes > 34359738368ull ? HAL_CARD_SDXC : HAL_CARD_SDHC) : HAL_CARD_SDSC;
    g_info.mid = (uint8_t)g_card.cid.mfg_id;
    g_info.oid[0] = (char)(g_card.cid.oem_id >> 8);
    g_info.oid[1] = (char)(g_card.cid.oem_id & 0xFF);
    memcpy(g_info.pnm, g_card.cid.name, 5);
    g_info.prv_major = (uint8_t)(g_card.cid.revision >> 4);
    g_info.prv_minor = (uint8_t)(g_card.cid.revision & 0x0F);
    g_info.psn = (uint32_t)g_card.cid.serial;
    g_info.mdt_year = (uint16_t)(2000u + ((g_card.cid.date >> 4) & 0xFFu));
    g_info.mdt_month = (uint8_t)(g_card.cid.date & 0x0Fu);
    g_info.csd_version = (uint8_t)(g_card.csd.csd_ver != 0 ? 1 : 0);
    g_info.max_bus_hz = (uint32_t)g_card.csd.tr_speed;
    g_info.bus_width = 1; /* SPI mode */
    g_info.bus_clock_hz = (uint32_t)g_card.real_freq_khz * 1000u;
    g_info.high_speed = g_card.real_freq_khz > 25000;
}

hal_storage_status_t hal_storage_init(void) {
    hal_storage_unmount();
    drop_card();
    g_write_fault = false; // the reset clears the latch (hal_storage.h)

    if (!g_bus_up) {
        spi_bus_config_t bus = {
            .mosi_io_num = PIN_SD_MOSI,
            .miso_io_num = PIN_SD_MISO,
            .sclk_io_num = PIN_SD_SCK,
            .quadwp_io_num = -1,
            .quadhd_io_num = -1,
            .max_transfer_sz = 4096,
        };
        if (spi_bus_initialize(SD_HOST, &bus, SPI_DMA_CH_AUTO) != ESP_OK) {
            return HAL_STORAGE_IO;
        }
        g_bus_up = true;
    }
    if (!g_host_up) {
        if (sdspi_host_init() != ESP_OK) {
            return HAL_STORAGE_IO;
        }
        g_host_up = true;
    }
    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    host.max_freq_khz = SD_MAX_FREQ_KHZ;
    sdspi_device_config_t slot = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot.gpio_cs = PIN_SD_CS;
    slot.host_id = SD_HOST;
    if (sdspi_host_init_device(&slot, &g_slot) != ESP_OK) {
        return HAL_STORAGE_IO;
    }
    g_slot_up = true;
    host.slot = g_slot;
    memset(&g_card, 0, sizeof g_card);
    esp_err_t e = sdmmc_card_init(&host, &g_card); // identification and the register reads: nothing is written to the card
    if (e != ESP_OK) {
        drop_card();
        // A missing card answers nothing at all (timeout); anything else is a card or bus fault.
        return e == ESP_ERR_TIMEOUT || e == ESP_ERR_NOT_FOUND ? HAL_STORAGE_NO_CARD : map_err(e);
    }
    fill_info();
    g_ready = true;
    return HAL_STORAGE_OK;
}

void hal_storage_deinit(void) {
    hal_storage_unmount();
    drop_card();
}

bool hal_storage_ready(void) { return g_ready; }

bool hal_storage_card_present(void) { return g_ready && sdmmc_get_status(&g_card) == ESP_OK; }

hal_storage_status_t hal_storage_get_info(hal_card_info_t *out) {
    if (out == NULL) {
        return HAL_STORAGE_BAD_PARAM;
    }
    if (!g_ready) {
        return HAL_STORAGE_NOT_INIT;
    }
    *out = g_info;
    return HAL_STORAGE_OK;
}

uint32_t hal_storage_sector_count(void) { return g_ready ? g_sectors : 0; }

hal_storage_status_t hal_storage_read(uint32_t lba, void *buf, uint32_t count) {
    if (buf == NULL || count == 0) {
        return HAL_STORAGE_BAD_PARAM;
    }
    if (!g_ready) {
        return HAL_STORAGE_NOT_INIT;
    }
    if ((uint64_t)lba + count > g_sectors) {
        return HAL_STORAGE_BAD_PARAM;
    }
    return map_err(sdmmc_read_sectors(&g_card, buf, lba, count));
}

void hal_storage_set_write_enabled(bool enabled) { g_write_enabled = enabled; }
bool hal_storage_get_write_enabled(void) { return g_write_enabled; }

hal_storage_status_t hal_storage_write(uint32_t lba, const void *buf, uint32_t count) {
    if (!g_write_enabled) {
        return HAL_STORAGE_WRITE_PROTECT; // checked first: nothing below may run while locked
    }
    if (buf == NULL || count == 0) {
        return HAL_STORAGE_BAD_PARAM;
    }
    if (g_write_fault) {
        return HAL_STORAGE_IO;
    }
    if (!g_ready) {
        return HAL_STORAGE_NOT_INIT;
    }
    if ((uint64_t)lba + count > g_sectors) {
        return HAL_STORAGE_BAD_PARAM;
    }
    esp_err_t e = sdmmc_write_sectors(&g_card, buf, lba, count);
    if (e != ESP_OK) {
        g_write_fault = true;
    }
    return map_err(e);
}

hal_storage_status_t hal_storage_sync(void) { return g_ready ? HAL_STORAGE_OK : HAL_STORAGE_NOT_INIT; } // SPI writes complete before they return

hal_storage_status_t hal_storage_set_bus_clock(uint32_t hz) {
    (void)hz;
    return HAL_STORAGE_UNSUPPORTED;
}

hal_storage_status_t hal_storage_enable_high_speed(void) { return HAL_STORAGE_UNSUPPORTED; }

uint32_t hal_storage_get_bus_clock(void) { return g_ready ? (uint32_t)g_card.real_freq_khz * 1000u : 0; }

hal_storage_status_t hal_storage_card_status(uint32_t *r1) {
    if (!g_ready) {
        return HAL_STORAGE_NOT_INIT;
    }
    esp_err_t e = sdmmc_get_status(&g_card); // CMD13
    if (r1 != NULL) {
        *r1 = e == ESP_OK ? 0x900u : 0xFFFFFFFFu; // transfer state, no error bits; the sdmmc layer does not expose the raw word
    }
    return map_err(e);
}

bool hal_storage_write_fault(void) { return g_write_fault; }

hal_storage_status_t hal_storage_mount(void) {
    if (!g_ready) {
        return HAL_STORAGE_NOT_INIT;
    }
    switch (f_mount(&g_fatfs, "", 1)) { // reads only
    case FR_OK:
        g_mounted = true;
        return HAL_STORAGE_OK;
    case FR_NO_FILESYSTEM: return HAL_STORAGE_NO_FILESYSTEM;
    case FR_NOT_READY: return HAL_STORAGE_NO_CARD;
    default: return HAL_STORAGE_IO;
    }
}

void hal_storage_unmount(void) {
    if (g_mounted) {
        f_mount(NULL, "", 0);
        g_mounted = false;
    }
}

bool hal_storage_is_mounted(void) { return g_mounted && g_ready; }

hal_fs_type_t hal_storage_fs_type(void) {
    if (!hal_storage_is_mounted()) {
        return HAL_FS_NONE;
    }
    switch (g_fatfs.fs_type) {
    case FS_FAT12: return HAL_FS_FAT12;
    case FS_FAT16: return HAL_FS_FAT16;
    case FS_FAT32: return HAL_FS_FAT32;
    case FS_EXFAT: return HAL_FS_EXFAT;
    default: return HAL_FS_NONE;
    }
}

hal_storage_status_t hal_storage_volume_info(hal_volume_info_t *out) {
    if (out == NULL) {
        return HAL_STORAGE_BAD_PARAM;
    }
    if (!hal_storage_is_mounted()) {
        return HAL_STORAGE_NOT_MOUNTED;
    }
    memset(out, 0, sizeof *out);
    out->fs = hal_storage_fs_type();
    out->sectors_per_cluster = g_fatfs.csize;
    out->cluster_bytes = out->sectors_per_cluster * HAL_STORAGE_SECTOR_SIZE;
    TCHAR label[36] = {0}; // FatFs needs at least 34 bytes
    DWORD vsn = 0;
    if (f_getlabel("", label, &vsn) != FR_OK) {
        return HAL_STORAGE_IO;
    }
    snprintf(out->label, sizeof out->label, "%s", label);
    out->serial = vsn;
    return HAL_STORAGE_OK;
}

// Formatting a card from this firmware is not offered yet (as on the Y2): prepare the card on a PC.
hal_storage_status_t hal_storage_format(const char *label, const hal_format_opts_t *opts) {
    (void)label;
    (void)opts;
    return HAL_STORAGE_UNSUPPORTED;
}
