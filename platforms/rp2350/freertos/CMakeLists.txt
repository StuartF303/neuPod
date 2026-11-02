# FreeRTOS for the RP2350, SINGLE-CORE on core 0 (core 1 is the bare-metal audio core, ADR-0011): the
# Raspberry Pi FreeRTOS-Kernel fork (submodule lib/FreeRTOS-Kernel, ARM_CM33_NTZ port), heap_4, and the SDK's FreeRTOS flavour of the CYW43 driver.
#
# Result: INTERFACE target `neupod_freertos`. Anything that links it is compiled with the SDK's
# FreeRTOS adapter (pico_sync / pico_time block cooperatively under the scheduler) and sees
# FreeRTOS.h and our FreeRTOSConfig.h. Bare-metal test executables must NOT link it.
# Override with -DFREERTOS_KERNEL_PATH=... to try another kernel tree (see docs/runtime.md).
if(NOT FREERTOS_KERNEL_PATH)
  set(FREERTOS_KERNEL_PATH ${CMAKE_SOURCE_DIR}/lib/FreeRTOS-Kernel)
endif()
if(NOT EXISTS ${FREERTOS_KERNEL_PATH}/tasks.c)
  message(FATAL_ERROR "FreeRTOS-Kernel submodule missing. Run: git submodule update --init lib/FreeRTOS-Kernel")
endif()
set(FREERTOS_CONFIG_FILE_DIRECTORY ${CMAKE_CURRENT_LIST_DIR})
include(${FREERTOS_KERNEL_PATH}/portable/ThirdParty/GCC/RP2350_ARM_NTZ/FreeRTOS_Kernel_import.cmake)

add_library(neupod_freertos INTERFACE)
target_include_directories(neupod_freertos INTERFACE ${CMAKE_SOURCE_DIR}/app/os ${CMAKE_SOURCE_DIR}/app/include)
target_link_libraries(neupod_freertos INTERFACE
  FreeRTOS-Kernel-Heap4
  # cyw43 flavour: pico_cyw43_arch_sys_freertos (no lwIP yet). The driver's worker task is created
  # on core 0, where the kernel runs (see docs/runtime.md).
  pico_cyw43_arch_sys_freertos
)
# SDK knobs for the cyw43 worker task: low-ish priority (RT_PRIO_RADIO), 4 KB of stack (words).
target_compile_definitions(neupod_freertos INTERFACE
  CYW43_TASK_PRIORITY=3
  CYW43_TASK_STACK_SIZE=1024
  CYW43_LWIP=0  # the SDK's none-variant sets this; the non-lwIP sys_freertos variant does not
)
