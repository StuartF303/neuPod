// SPDX-License-Identifier: MPL-2.0
/**
 * @file memory_budget.h
 * @brief The big static-RAM consumers of the RP2350 build, as named constants (docs/runtime.md, docs/bluetooth.md).
 *
 * Plain macros only, so FreeRTOSConfig.h and lv_conf.h can include it. The RP2350 has 512 KB of main SRAM;
 * these three plus the audio core's rings and the BTstack pools are most of it.
 *
 * History (the sizes were trimmed to make room for the BTstack classic stack, stage 1 of docs/bluetooth.md):
 *   FreeRTOS heap  112 KB -> 80 KB   (bench minimum free was ~58 KB of 112 KB: ~54 KB used before the haptic/storage growth)
 *   LVGL pool       96 KB -> 72 KB   (bench peak ~38 KB after navigating every menu; the host tour passes at 72 KB)
 *   codec arena    116 KB -> 99 KB   (app/include/codec.h, CODEC_ARENA_BYTES; 32-bit ALAC rejected)
 *   PCM ring        16 KB -> 32 KB   (NEUPOD_PCM_RING_FRAMES below; 2048 frames underran on ALAC 24/96)
 */
#ifndef NEUPOD_MEMORY_BUDGET_H
#define NEUPOD_MEMORY_BUDGET_H

/**
 * FreeRTOS heap_4 (task stacks, queues, mutexes, the CYW43 worker task) and, through os_alloc(), the transient scratch of
 * album art (art.c: 4096 B decoder pool + at most 3008 B strip/averaging buffers = about 7.1 KB on the 64 px build) and FatFs
 * (ff_alloc.c: at most NEUPOD_FF_ALLOC_MAX_BYTES = 4 KB during a directory creation). Both run in the storage task and never
 * overlap (art_decode creates no directories mid-decode), so the peak is the art figure, about 7.1 KB of free heap. The newlib
 * heap is only ~2 KB, which is why these do not use malloc (docs/storage.md).
 */
#define NEUPOD_RTOS_HEAP_BYTES (80u * 1024u)

/**
 * Free heap that os_alloc() leaves alone: it returns NULL rather than take a block that would drop the RTOS heap's free space
 * below this. Task, queue and mutex creation panic when the heap is dry, so the optional scratch (album art, FatFs) must never be
 * the allocation that starves them. Owner-chosen default: the bench showed about 10 KB minimum-ever free with Bluetooth A2DP up,
 * and the art transient is about 7.1 KB, so 6 KB keeps kernel creation safe with the art decode in flight. On the ESP32-S3
 * the free size counts PSRAM too (megabytes), so the reserve does not bind there.
 */
#ifndef NEUPOD_OS_ALLOC_RESERVE
#define NEUPOD_OS_ALLOC_RESERVE 6144u
#endif

/**
 * LVGL's built-in allocator pool (screens, widgets, styles). The host build (64-bit pointers) needs about 1.6 times what the
 * 32-bit target does for the same screens: Files plus Now Playing on top does not fit in 72 KB there, so the host gets 128 KB.
 * The target figure is the one that counts.
 */
#if defined(NEUPOD_LVGL_POOL_BYTES) /* set by the build: the ESP32-S3 keeps the pool in its 8 MB of PSRAM (platforms/esp32s3/main/CMakeLists.txt) */
#elif defined(NEUPOD_PSRAM_SECTION) /* RP2350 AMOLED: the pool is in PSRAM (lv_conf.h), sized like the ESP32-S3's: Cover Flow at 600x450 (UI-25) */
#define NEUPOD_LVGL_POOL_BYTES (192u * 1024u)
#elif defined(NEUPOD_UI_HIRES) /* hosted build (host simulator, Y2 testbed): carries the 480x360 profile, plenty of RAM */
#define NEUPOD_LVGL_POOL_BYTES (192u * 1024u)
#elif defined(__SIZEOF_POINTER__) && __SIZEOF_POINTER__ == 8 /* 64-bit host build only; any 32-bit target (ARM or RISC-V) gets 72 KB */
#define NEUPOD_LVGL_POOL_BYTES (128u * 1024u)
#else
#define NEUPOD_LVGL_POOL_BYTES (72u * 1024u)
#endif

/**
 * Audio core PCM ring, in stereo frames (8 bytes each, int32 L/R): the buffer between the decoder (core 1 loop)
 * and the block interrupt that feeds the DAC. Its depth is the decoder's slack: a decode call longer than
 * ring-frames / sample-rate starves the sink (4096 frames = 93 ms at 44.1 kHz, 43 ms at 96 kHz). RAM cost = frames * 8 bytes
 * (32 KB at 4096). Must be a power of two and a multiple of HAL_AUDIO_BLOCK_FRAMES (256).
 */
#define NEUPOD_PCM_RING_FRAMES 4096u

#endif // NEUPOD_MEMORY_BUDGET_H
