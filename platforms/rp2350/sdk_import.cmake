# Included by the top-level CMakeLists.txt BEFORE project() when NEUPOD_PLATFORM=rp2350.
#
# Board: -DNEUPOD_BOARD=pico2w (default: the Pico 2 W bench rig, ST7789 + Audio Pack + click wheel + CYW43) or
# amoled241 (Waveshare RP2350-Touch-AMOLED-2.41 dev board, docs/rp2350-amoled241.md). It has to be known here, before
# pico_sdk_init(), because it selects the SDK's board header. Use a separate build directory per board.
set(NEUPOD_BOARD pico2w CACHE STRING "RP2350 board: pico2w or amoled241")
set_property(CACHE NEUPOD_BOARD PROPERTY STRINGS pico2w amoled241)
if(NEUPOD_BOARD STREQUAL "pico2w")
  set(PICO_BOARD pico2_w CACHE STRING "Board type")
elseif(NEUPOD_BOARD STREQUAL "amoled241")
  # The board header (an SDK-style header, BSD-3-Clause, see LICENSE.md) lives with the board's sources.
  set(PICO_BOARD_HEADER_DIRS ${CMAKE_CURRENT_LIST_DIR}/boards/amoled241)
  set(PICO_BOARD waveshare_rp2350_touch_amoled_2.41 CACHE STRING "Board type" FORCE)
else()
  message(FATAL_ERROR "NEUPOD_BOARD must be pico2w or amoled241 (got '${NEUPOD_BOARD}')")
endif()
set(PICO_PLATFORM rp2350-arm-s CACHE STRING "Platform")
include(${CMAKE_CURRENT_LIST_DIR}/pico_sdk_import.cmake)
