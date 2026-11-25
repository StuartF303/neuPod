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
target_include_directories(neupod_freertos INTERFACE ${CMAKE_SOURCE_DIR}/app/os)
target_link_libraries(neupod_freertos INTERFACE FreeRTOS-Kernel-Heap4)
if(NOT NEUPOD_BOARD STREQUAL "amoled241") # the amoled241 board has no radio: no CYW43 driver, no lwIP, no BTstack
  target_link_libraries(neupod_freertos INTERFACE
    # cyw43 flavour: pico_cyw43_arch_sys_freertos (no lwIP yet). The driver's worker task is created
    # on core 0, where the kernel runs (see docs/runtime.md).
    pico_cyw43_arch_lwip_sys_freertos
  )
endif()
target_include_directories(neupod_freertos INTERFACE ${CMAKE_SOURCE_DIR}/platforms/rp2350/lwip)
if(NOT NEUPOD_BOARD STREQUAL "amoled241")
  # SDK knobs for the cyw43 worker task: priority 6 (RT_PRIO_RADIO, above storage at 5), 6 KB of stack (words). This task is also
  # BTstack's thread (docs/bluetooth.md): every BTstack callback, and our event handlers, run on it.
  # Why above storage: the A2DP link has a ~40 ms deadline, while storage only feeds a deep buffer and bursts to 32-40 % of
  # core 0 during FLAC. Bench 19 Nov 2026 (Pico 2 W, AirPods, Eels FLAC): worker 3 < storage 5 gave 36 stalls per 10 s,
  # catch-up to 160 ms, bitpool forced 53 to 45, a 0x14 disconnect; worker above storage gave 0 stalls, 25-34 ms, bitpool 53.
  # Keep equal to RT_PRIO_RADIO (app/include/runtime_config.h; a static assert checks it). Shares 6 with the timer service.
  target_compile_definitions(neupod_freertos INTERFACE
    CYW43_TASK_PRIORITY=6
    CYW43_TASK_STACK_SIZE=1536
    # Bluetooth. The SDK sets this through pico_btstack_hci_transport_cyw43, which only the targets that link it see;
    # but cyw43_arch_init() is compiled into several of the static libraries that link neupod_freertos, and every copy must
    # agree (it decides whether cyw43_arch_init() starts BTstack). The BTstack sources themselves are linked once, in hal_rp2350_rtos.
    CYW43_ENABLE_BLUETOOTH=1
    PICO_CYW43_ARCH_DEFAULT_COUNTRY_CODE=CYW43_COUNTRY_UK  # channels 1-13
  )
endif()
