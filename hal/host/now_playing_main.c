// SPDX-License-Identifier: MPL-2.0
// Host viewer for the portable Now Playing screen. Usage: now_playing [8|16] [light]
//   SDL build: shows a window until closed.  Headless: set NEUPOD_DISPLAY_PPM=out.ppm to capture.
#include <stdio.h>
#include <string.h>

#include "hal_display.h"
#include "hal_system.h"
#include "now_playing.h"

int main(int argc, char **argv) {
    bool small = false;
    bool light = false;
    for (int i = 1; i < argc; i++) {
        small |= strcmp(argv[i], "8") == 0;
        light |= strcmp(argv[i], "light") == 0;
    }
    if (hal_system_init() != HAL_OK || hal_display_init() != HAL_OK) {
        fprintf(stderr, "now_playing: init failed\n");
        return 1;
    }
    now_playing_t np = {.title = "Bohemian Rhapsody",
                        .artist = "Queen",
                        .album = "A Night at the Opera",
                        .elapsed_s = 83,
                        .total_s = 355,
                        .font = small ? &gfx_font_8x8 : &gfx_font_16,
                        .fg = light ? HAL_RGB565(0, 0, 0) : HAL_RGB565(255, 255, 255),
                        .bg = light ? HAL_RGB565(255, 255, 255) : HAL_RGB565(0, 0, 0)};
    if (gfx_present_frame(now_playing_draw, &np) != HAL_OK) {
        fprintf(stderr, "now_playing: draw failed\n");
        return 1;
    }
    printf("now_playing: drawn\n");
#ifdef NEUPOD_HOST_SDL
    for (;;) {
        hal_display_present();
        hal_delay_ms(50); // pumps events; exits when the window is closed
    }
#endif
    return 0;
}
