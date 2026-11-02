# Enforces the core-1 rules (docs/adr/0011-bare-metal-audio-core.md): the bare-metal audio core must not
# use the RTOS, stdio, the heap, or SDK primitives that FreeRTOS interop reroutes to the kernel.
# Usage: cmake -DSRC_DIR=<repo> -P check_core1_rules.cmake   (ctest "audio_core_rules")
set(FILES
  app/audio/audio_core.c
  app/audio/audio_shared.c
  app/audio/spsc_ring.c
  app/include/spsc_ring.h
  app/include/seqlock.h
  app/include/audio_engine.h
  app/audio/audio_dsp.c
  app/include/audio_dsp.h
  platforms/rp2350/hal/hal_audio.c
  platforms/rp2350/hal/hal_audio_null.c
  hal/host/hal_audio.c)
# Tokens are matched on code only (comments stripped first), at an identifier boundary on the left
# (so "free(" does not match "spsc_ring_free(").
set(FORBIDDEN
  "os\\.h" "FreeRTOS" "task\\.h" "semphr\\.h" "queue\\.h" "os_[a-z_]+\\(" "xTask" "vTask" "x[A-Z][a-zA-Z]+FromISR"
  "stdio\\.h" "printf" "puts\\(" "putchar" "malloc" "calloc" "realloc" "free\\("
  "mutex_" "sem_" "critical_section" "queue_" "lock_core" "sleep_ms" "sleep_us" "sleep_until" "busy_wait"
  "alarm_pool_" "add_alarm_" "add_repeating_timer" "multicore_lockout" "multicore_fifo"
  "pico/sync\\.h" "pico/stdlib\\.h" "pico/time\\.h" "hal_delay_ms" "hal_status_led" "hal_panic" "assert" "panic")
set(BAD 0)
foreach(F ${FILES})
  file(READ ${SRC_DIR}/${F} TEXT)
  string(REGEX REPLACE "/\\*([^*]|\\*+[^*/])*\\*+/" "" TEXT "${TEXT}")   # block comments
  string(REGEX REPLACE "//[^\n]*" "" TEXT "${TEXT}")                    # line comments
  foreach(PAT ${FORBIDDEN})
    if(TEXT MATCHES "(^|[^A-Za-z0-9_])(${PAT})")
      message(SEND_ERROR "core-1 rule violation in ${F}: matches '${PAT}'")
      set(BAD 1)
    endif()
  endforeach()
endforeach()
if(NOT BAD)
  message(STATUS "core-1 rules: ok")
endif()
