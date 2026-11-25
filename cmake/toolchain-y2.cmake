# Cross toolchain for the Innioasis Y2 (MT6582, armv7 hard-float, Linux 3.4 kernel, glibc rootfs).
#
# Selected automatically by the top-level CMakeLists.txt for -DNEUPOD_PLATFORM=y2 (or pass it with
# -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-y2.cmake). Where the compiler lives:
#   -DNEUPOD_Y2_TOOLCHAIN_DIR=<dir>   or env NEUPOD_Y2_TOOLCHAIN   default: ~/y2-build/out-rootfs/host (the Buildroot
#                                     host tree that built the rootfs, so the libc matches the device exactly)
#   -DNEUPOD_Y2_TRIPLE=<triple>       or env NEUPOD_Y2_TRIPLE       default: arm-buildroot-linux-gnueabihf
# CI uses the Ubuntu cross compiler instead: NEUPOD_Y2_TOOLCHAIN_DIR=/usr NEUPOD_Y2_TRIPLE=arm-linux-gnueabihf
# (the binary then runs on the device only if the device's glibc is at least as new as the one it was linked against;
# only the Buildroot toolchain is meant for the binaries that are deployed).
set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR arm)

if(NOT NEUPOD_Y2_TOOLCHAIN_DIR)
  if(DEFINED ENV{NEUPOD_Y2_TOOLCHAIN})
    set(NEUPOD_Y2_TOOLCHAIN_DIR "$ENV{NEUPOD_Y2_TOOLCHAIN}")
  else()
    set(NEUPOD_Y2_TOOLCHAIN_DIR "$ENV{HOME}/y2-build/out-rootfs/host")
  endif()
endif()
if(NOT NEUPOD_Y2_TRIPLE)
  if(DEFINED ENV{NEUPOD_Y2_TRIPLE})
    set(NEUPOD_Y2_TRIPLE "$ENV{NEUPOD_Y2_TRIPLE}")
  else()
    set(NEUPOD_Y2_TRIPLE "arm-buildroot-linux-gnueabihf")
  endif()
endif()

set(_y2_gcc "${NEUPOD_Y2_TOOLCHAIN_DIR}/bin/${NEUPOD_Y2_TRIPLE}-gcc")
if(NOT EXISTS "${_y2_gcc}")
  message(FATAL_ERROR
    "Y2 cross compiler not found: ${_y2_gcc}\n"
    "Build the rootfs toolchain first (testbed/y2/rootfs), or point -DNEUPOD_Y2_TOOLCHAIN_DIR / "
    "-DNEUPOD_Y2_TRIPLE (env NEUPOD_Y2_TOOLCHAIN / NEUPOD_Y2_TRIPLE) at another armhf gcc.")
endif()
set(CMAKE_C_COMPILER "${_y2_gcc}")
set(CMAKE_STRIP "${NEUPOD_Y2_TOOLCHAIN_DIR}/bin/${NEUPOD_Y2_TRIPLE}-strip")

# Host tools are found on the build machine; libraries and headers only in the toolchain's sysroot (compiler default).
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
