# y2-a2dp: A2DP source testbed tool for the Innioasis Y2 (BTstack over /dev/stpbt). See README.md.
#
#   Y2 binary (static, Buildroot toolchain):
#     cmake -S testbed/y2/tools/y2-a2dp -B build/y2-a2dp -DY2A2DP_TARGET=y2 && cmake --build build/y2-a2dp
#   PC build (compiles everything, cannot open the device) plus unit tests:
#     cmake -S testbed/y2/tools/y2-a2dp -B build/y2-a2dp-host && cmake --build build/y2-a2dp-host && ctest --test-dir build/y2-a2dp-host
#
# BTstack is used in place, not vendored: -DBTSTACK_ROOT=<dir> or the Pico SDK's copy ($PICO_SDK_PATH/lib/btstack).
cmake_minimum_required(VERSION 3.13)

set(Y2A2DP_TARGET "host" CACHE STRING "host or y2 (cross-compile with cmake/toolchain-y2.cmake)")
set_property(CACHE Y2A2DP_TARGET PROPERTY STRINGS host y2)
if(Y2A2DP_TARGET STREQUAL "y2" AND NOT CMAKE_TOOLCHAIN_FILE)
  set(CMAKE_TOOLCHAIN_FILE ${CMAKE_CURRENT_LIST_DIR}/../../../../cmake/toolchain-y2.cmake)
endif()

project(y2_a2dp C)
set(CMAKE_C_STANDARD 11)

if(NOT BTSTACK_ROOT)
  if(DEFINED ENV{BTSTACK_ROOT})
    set(BTSTACK_ROOT "$ENV{BTSTACK_ROOT}")
  elseif(DEFINED ENV{PICO_SDK_PATH})
    set(BTSTACK_ROOT "$ENV{PICO_SDK_PATH}/lib/btstack")
  endif()
endif()
if(NOT BTSTACK_ROOT OR NOT EXISTS "${BTSTACK_ROOT}/src/hci.c")
  message(FATAL_ERROR "BTstack not found. Pass -DBTSTACK_ROOT=<btstack checkout> (v1.6.x) or set PICO_SDK_PATH.")
endif()

# BTstack itself: classic + A2DP/AVRCP + the bluedroid SBC codec (only the encoder is used) + the POSIX run loop, TLV and HCI dump. A static library, so
# the linker drops what is not used.
file(GLOB BTSTACK_CORE    "${BTSTACK_ROOT}/src/*.c")
file(GLOB BTSTACK_CLASSIC "${BTSTACK_ROOT}/src/classic/*.c")
file(GLOB BTSTACK_SBC     "${BTSTACK_ROOT}/3rd-party/bluedroid/encoder/srce/*.c" "${BTSTACK_ROOT}/3rd-party/bluedroid/decoder/srce/*.c")
list(FILTER BTSTACK_CORE EXCLUDE REGEX "lc3") # LC3 codecs need external libraries and are not used
add_library(btstack STATIC
  ${BTSTACK_CORE} ${BTSTACK_CLASSIC} ${BTSTACK_SBC}
  ${BTSTACK_ROOT}/platform/posix/btstack_run_loop_posix.c
  ${BTSTACK_ROOT}/platform/posix/btstack_tlv_posix.c
  ${BTSTACK_ROOT}/platform/posix/hci_dump_posix_fs.c
  ${BTSTACK_ROOT}/3rd-party/yxml/yxml.c ${BTSTACK_ROOT}/3rd-party/md5/md5.c) # needed by the classic profiles that btstack.h pulls in
target_include_directories(btstack PUBLIC
  ${CMAKE_CURRENT_LIST_DIR}          # btstack_config.h (first)
  ${BTSTACK_ROOT}/src
  ${BTSTACK_ROOT}/src/classic
  ${BTSTACK_ROOT}/platform/posix
  ${BTSTACK_ROOT}/3rd-party/bluedroid/encoder/include
  ${BTSTACK_ROOT}/3rd-party/yxml
  ${BTSTACK_ROOT}/3rd-party/md5
  ${BTSTACK_ROOT}/3rd-party/bluedroid/decoder/include) # headers only (btstack.h pulls in the decoder API)
target_compile_options(btstack PRIVATE -w) # third-party code: not ours to keep warning-free

set(Y2A2DP_WARN -Wall -Wextra -Wno-unused-parameter)

# The two pieces that make BTstack talk to the MediaTek chip.
add_library(y2a2dp_transport STATIC btstack_uart_stpbt.c btstack_chipset_mtk_consys.c)
target_link_libraries(y2a2dp_transport PUBLIC btstack)
target_compile_options(y2a2dp_transport PRIVATE ${Y2A2DP_WARN})

add_executable(y2-a2dp main.c)
target_link_libraries(y2-a2dp PRIVATE y2a2dp_transport btstack m)
target_compile_options(y2-a2dp PRIVATE ${Y2A2DP_WARN})

if(Y2A2DP_TARGET STREQUAL "y2")
  # Static like the other Y2 testbed tools: runs on the device regardless of which glibc the rootfs carries.
  target_link_options(y2-a2dp PRIVATE -static)
  add_custom_command(TARGET y2-a2dp POST_BUILD
    COMMAND ${CMAKE_STRIP} -o $<TARGET_FILE:y2-a2dp>.stripped $<TARGET_FILE:y2-a2dp>
    COMMENT "Stripped copy: y2-a2dp.stripped")
else()
  enable_testing()
  add_executable(test_uart_stpbt test/test_uart_stpbt.c)
  target_link_libraries(test_uart_stpbt PRIVATE y2a2dp_transport btstack)
  target_compile_options(test_uart_stpbt PRIVATE ${Y2A2DP_WARN})
  add_test(NAME uart_stpbt COMMAND test_uart_stpbt)

  add_executable(test_chipset_mtk test/test_chipset_mtk.c)
  target_link_libraries(test_chipset_mtk PRIVATE y2a2dp_transport btstack)
  target_compile_options(test_chipset_mtk PRIVATE ${Y2A2DP_WARN})
  add_test(NAME chipset_mtk COMMAND test_chipset_mtk)

  add_executable(test_hci_preinit test/test_hci_preinit.c)
  target_link_libraries(test_hci_preinit PRIVATE y2a2dp_transport btstack)
  target_compile_options(test_hci_preinit PRIVATE ${Y2A2DP_WARN})
  add_test(NAME hci_preinit COMMAND test_hci_preinit)
endif()
