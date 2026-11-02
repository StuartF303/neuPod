# ADR-0011: Single-core FreeRTOS on core 0, bare-metal audio core on core 1

## Status

Accepted (decided by the project owner after bench evidence). Amends [ADR-0009](0009-rtos-freertos-smp.md).

## Context

ADR-0009 chose FreeRTOS SMP with the audio task pinned to core 1 at the highest priority. On the
board (Raspberry Pi FreeRTOS-Kernel fork, RP2350 SMP port, pinned at its newest commit `4f7299d`) that
did not work, in two independent ways:

1. Waking the audio task from a hardware-alarm interrupt on core 1 (`xSemaphoreGiveFromISR`) corrupted the
   kernel: core 1 spun in `vListInsert` holding the kernel task lock, core 0 spun on that lock in
   PendSV, and the semaphore's wait list contained the audio task's event-list item twice.
2. With the wake-up reduced to ordinary task calls, core 1 froze about 1.5 s after boot waiting in
   `prvCheckForRunStateChange` for a cross-core yield (SIO doorbell) that never arrived. A watchdog that re-sent
   the doorbell recovered it every time, so the cross-core yield requests are being lost. Every audio
   wake-up on core 1 depends on one (the tick runs on core 0). With the watchdog in place the audio
   period was still erratic (2 s outliers).

Upstream FreeRTOS-Kernel (V11.3.3) has no RP2350 port, and its core does not build against the fork's
port, so there is no newer kernel to move to without porting the port.

## Decision

- **FreeRTOS runs single-core on core 0** (`configNUMBER_OF_CORES 1`, the RP2350 port's single-core mode):
  UI, input, storage, monitor, boot checks and the CYW43 worker. No affinity.
- **Core 1 is a bare-metal audio core**, launched by the runtime (`multicore_launch_core1` via
  `hal_core1_launch`) before the scheduler starts. Its main loop handles commands and runs the decoder;
  the output clock is a hardware interrupt on core 1 (the I2S PIO+DMA block interrupt on the RP2350, a timer-driven null sink as the fallback) that moves
  256-frame blocks from a PCM ring to the sink. The interrupt preempts the decoder, so block timing
  does not depend on decode time.
- **Core-1 rules**: no FreeRTOS or `os.h`, no stdio, no malloc, no SDK primitive that the FreeRTOS
  interop reroutes to the kernel (mutex, semaphore, queue, critical section, sleep, busy wait). Core 0 and
  core 1 communicate only through single-producer single-consumer lock-free rings and a seqlock: a command
  ring and a compressed-data ring (core 0 to 1), event rings and a statistics seqlock (core 1 to 0). The
  rules are enforced by a test (`audio_core_rules`) that scans the core-1 files for forbidden tokens, and
  documented in `docs/runtime.md`.
- **Layering**: portable engine in `app/audio/` (core side `audio_core.c`, control side `audio_ctl.c`);
  the block clock and the core launch in the HAL (`hal_audio.h`, `hal_core.h`).
- **Host**: the POSIX FreeRTOS port runs the core-0 tasks; core 1 is a native thread running the same audio
  core loop, the block-clock interrupt is a timer thread, and the rings are the same.

## Consequences

- No cross-core scheduling exists any more, so the failures above cannot occur. Audio timing depends only
  on the core-1 interrupt, which nothing on core 0 can delay.
- There is no kernel on core 1: no RTOS timers, queues, or tasks there. A real codec running in the main loop
  must be written as an interruptible state machine with a bounded stack, and must follow the core-1 rules
  (no heap: all buffers static).
- Core 1 interrupts and the RTOS share one vector table (the SDK points both cores at it); the audio core uses
  its own interrupt numbers only.
- The kernel's SDK interop (pico_sync/pico_time blocking cooperatively) applies to core 0 only; the SDK's
  cross-core wake path for a non-RTOS core is not used.
- A panic halts only the failing core.
- The panic LED limitation of ADR-0009 stays; so does the host-timing caveat.
- If a future kernel release fixes the RP2350 SMP port, moving the decoder back into an RTOS task is possible
  but not needed: the rings and the interrupt-driven sink already isolate it.
