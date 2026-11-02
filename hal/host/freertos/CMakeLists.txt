# FreeRTOS for the host build: the kernel's POSIX port (portable/ThirdParty/GCC/Posix), heap_4.
# One pthread per task, a single scheduling "core"; lets app/ run unchanged on the PC.
# Result: INTERFACE target `neupod_freertos` (same name as on the RP2350).
find_package(Threads REQUIRED)
set(FREERTOS_KERNEL_PATH ${CMAKE_SOURCE_DIR}/lib/FreeRTOS-Kernel)
if(NOT EXISTS ${FREERTOS_KERNEL_PATH}/tasks.c)
  message(FATAL_ERROR "FreeRTOS-Kernel submodule missing. Run: git submodule update --init lib/FreeRTOS-Kernel")
endif()

add_library(freertos_config INTERFACE)
target_include_directories(freertos_config SYSTEM INTERFACE
  ${CMAKE_CURRENT_LIST_DIR}
  ${CMAKE_SOURCE_DIR}/app/os)
target_compile_definitions(freertos_config INTERFACE projCOVERAGE_TEST=0)

set(FREERTOS_PORT GCC_POSIX CACHE STRING "FreeRTOS port" FORCE)
set(FREERTOS_HEAP 4 CACHE STRING "FreeRTOS heap implementation" FORCE)
add_subdirectory(${FREERTOS_KERNEL_PATH} ${CMAKE_BINARY_DIR}/freertos_kernel EXCLUDE_FROM_ALL)

add_library(neupod_freertos INTERFACE)
target_link_libraries(neupod_freertos INTERFACE freertos_kernel Threads::Threads)
