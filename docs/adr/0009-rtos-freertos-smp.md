# ADR-0009: RTOS — FreeRTOS SMP, audio pinned to core 1

## Status

Accepted (decided by the project owner). **Amended by [ADR-0011](0011-bare-metal-audio-core.md):** the SMP
configuration below did not work on the board; the kernel is now single-core on core 0 and core 1 is a
bare-metal audio core. The FreeRTOS choice, heap, `os.h`, cyw43 variant and host POSIX port stand.

## Context

Playback must not glitch while the UI renders, the SD card is read and the radio is serviced.
The RP2350 has two Cortex-M33 cores. The portable `app/` code must run unchanged on the
desktop build, and the ESP32-S3 comparison platform ships FreeRTOS natively.

## Decision

- **FreeRTOS SMP**, from the Raspberry Pi fork of the kernel (`lib/FreeRTOS-Kernel`,
  submodule, pinned at commit `4f7299d`, kernel V11.1.0+; the fork has no release tags),
  ARM_CM33_NTZ / RP2350 port, both cores, 1 kHz tick, `heap_4` (112 KB), stack-overflow check 2,
  malloc-failed hook, run-time statistics.
- **Audio** is one task pinned to core 1 at the highest application priority (7). Only the
  **decoder** may share core 1, at priority 6: it is the one large, bursty CPU consumer, and keeping
  it beside the audio task leaves core 0 free for UI, SD, input and the radio. The decoder is
  always pre-empted by audio and talks to it through a lock-free ring buffer, so audio never
  waits on a lower-priority task. Every other task, the timer service task and the CYW43 worker
  are pinned to core 0.
- **Audio timing** comes from the sink's block clock on the 1 MHz hardware timer, not the 1 kHz tick.
  Today the audio task sleeps then spins to the deadline; waking a task from an interrupt on core 1
  deadlocked the SMP kernel on the board and is not used until proven (docs/runtime.md, bench findings).
- **cyw43 arch: `pico_cyw43_arch_sys_freertos`** (no lwIP yet). `threadsafe_background` services
  the driver from timer interrupts and its own locks, outside the scheduler's view; `sys_freertos` is
  the SDK's FreeRTOS-native variant: the driver runs in one worker task, created pinned to the
  core that calls `cyw43_arch_init()` (core 0), so its interrupts and work stay off the audio core,
  and BTstack (later) attaches to the same worker. (Chosen from the SDK sources; not yet proven on
  hardware.) Add the `lwip_sys_freertos` variant when Wi-Fi networking starts.
- **`app/include/os.h`** wraps tasks (core affinity is a mandatory argument), delays, queues,
  mutexes, event flags and task statistics, so app code never calls FreeRTOS directly and times
  are milliseconds. One implementation (`app/os/os_freertos.c`) serves RP2350, host and, later,
  ESP-IDF.
- **Host** uses the kernel's POSIX port (one pthread per task, one scheduling "core"): the same
  tasks, UI and boot checks run on the PC. Core affinity is ignored there and timing is not
  representative.

## Consequences

- Core 1 isolation depends on discipline: a new task must pick its core in `runtime_config.h`;
  creating a task without a core is impossible through `os.h`.
- Two builds of the HAL/SD glue exist on RP2350 (bare metal for the bring-up test executables,
  RTOS for `neupod`) because the SDK's FreeRTOS adapter changes the layout of its mutex type.
- Panic path: stack overflow, malloc failure and assertions print the task name over the UART
  (lock-free) and halt with an LED code; on the Pico 2 W the LED sits behind the CYW43 chip and
  only blinks when the failure occurs in ordinary task context.
- Tickless idle is untested in this port; power work later may need revisiting.
