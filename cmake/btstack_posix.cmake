# BTstack for a POSIX target (the Y2 firmware and the y2-a2dp tool): classic + A2DP/AVRCP + the bluedroid SBC codec + the POSIX run loop.
# BTstack is used in place, not vendored: -DBTSTACK_ROOT=<dir>, env BTSTACK_ROOT, or the Pico SDK's copy ($PICO_SDK_PATH/lib/btstack).
#
#   neupod_btstack_posix(<target> <config dir holding btstack_config.h>)
# Static library, so the linker drops what is not used. Include directories are PUBLIC.
function(neupod_btstack_posix target config_dir)
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
  file(GLOB BTSTACK_CORE    "${BTSTACK_ROOT}/src/*.c")
  file(GLOB BTSTACK_CLASSIC "${BTSTACK_ROOT}/src/classic/*.c")
  file(GLOB BTSTACK_SBC     "${BTSTACK_ROOT}/3rd-party/bluedroid/encoder/srce/*.c" "${BTSTACK_ROOT}/3rd-party/bluedroid/decoder/srce/*.c")
  list(FILTER BTSTACK_CORE EXCLUDE REGEX "lc3") # LC3 codecs need external libraries and are not used
  add_library(${target} STATIC
    ${BTSTACK_CORE} ${BTSTACK_CLASSIC} ${BTSTACK_SBC}
    ${BTSTACK_ROOT}/platform/posix/btstack_run_loop_posix.c
    ${BTSTACK_ROOT}/platform/posix/btstack_tlv_posix.c
    ${BTSTACK_ROOT}/platform/posix/hci_dump_posix_fs.c
    ${BTSTACK_ROOT}/3rd-party/yxml/yxml.c ${BTSTACK_ROOT}/3rd-party/md5/md5.c) # needed by the classic profiles that btstack.h pulls in
  target_include_directories(${target} PUBLIC
    ${config_dir}                      # btstack_config.h (first)
    ${BTSTACK_ROOT}/src
    ${BTSTACK_ROOT}/src/classic
    ${BTSTACK_ROOT}/platform/posix
    ${BTSTACK_ROOT}/3rd-party/bluedroid/encoder/include
    ${BTSTACK_ROOT}/3rd-party/yxml
    ${BTSTACK_ROOT}/3rd-party/md5
    ${BTSTACK_ROOT}/3rd-party/bluedroid/decoder/include) # headers only (btstack.h pulls in the decoder API)
  target_compile_options(${target} PRIVATE -w) # third-party code: not ours to keep warning-free
endfunction()
