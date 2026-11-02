// SPDX-License-Identifier: MPL-2.0
// Host storage_test: the same menu as the device, over a disk-image file ($NEUPOD_DISK_IMAGE,
// default build/host/disk.img; create one with e.g. `truncate -s 64M build/host/disk.img`).
// The image is opened read-write but this stage never writes to it.
#include <stdio.h>

#include "hal_system.h"
#include "storage_tests.h"

int main(void) {
    if (hal_system_init() != HAL_OK) {
        fprintf(stderr, "storage_test: init failed\n");
        return 1;
    }
    storage_tests_run_menu(); // exits on stdin EOF or Ctrl-C
    return 0;
}
