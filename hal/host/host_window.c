// SPDX-License-Identifier: MPL-2.0
/*
 * Optional SDL2 front-end. Everything SDL is behind NEUPOD_HOST_SDL; the #else branch is the
 * headless stub. NOTE: the SDL branch was written without SDL2 headers available on the
 * authoring machine and has not been compiled yet.
 */
#include "host_internal.h"

#ifdef NEUPOD_HOST_SDL

#define SDL_MAIN_HANDLED
#include <SDL.h>
#include <stdio.h>
#include <pthread.h>
#include <stdlib.h>

#include "hal_input.h"

#define WINDOW_SCALE 2

static SDL_Window *g_win;
static SDL_Renderer *g_ren;
static SDL_Texture *g_tex;
static pthread_t g_owner; // SDL is single-threaded: only the thread that opened the window uses it

static void window_close(void) {
    if (g_tex) {
        SDL_DestroyTexture(g_tex);
        g_tex = NULL;
    }
    if (g_ren) {
        SDL_DestroyRenderer(g_ren);
        g_ren = NULL;
    }
    if (g_win) {
        SDL_DestroyWindow(g_win);
        g_win = NULL;
    }
    SDL_Quit();
}

bool host_window_open(int w, int h) {
    if (g_win) {
        return true;
    }
    g_owner = pthread_self();
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        fprintf(stderr, "neuPod host: SDL_Init failed (%s); running headless\n", SDL_GetError());
        return false;
    }
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0"); // nearest: keep pixels crisp at 2x
    g_win = SDL_CreateWindow("neuPod", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                             w * WINDOW_SCALE, h * WINDOW_SCALE, SDL_WINDOW_SHOWN);
    if (g_win) {
        g_ren = SDL_CreateRenderer(g_win, -1, 0);
    }
    if (g_ren) {
        SDL_RenderSetLogicalSize(g_ren, w, h);
        g_tex = SDL_CreateTexture(g_ren, SDL_PIXELFORMAT_RGB565, SDL_TEXTUREACCESS_STREAMING, w, h);
    }
    if (!g_tex) {
        fprintf(stderr, "neuPod host: SDL window setup failed (%s); running headless\n",
                SDL_GetError());
        window_close();
        return false;
    }
    atexit(window_close);
    return true;
}

void host_window_present(const uint16_t *fb, int w, int h, uint8_t backlight) {
    (void)h;
    if (!g_tex) {
        return;
    }
    SDL_UpdateTexture(g_tex, NULL, fb, w * (int)sizeof(uint16_t));
    SDL_SetTextureColorMod(g_tex, backlight, backlight, backlight);
    SDL_RenderClear(g_ren);
    SDL_RenderCopy(g_ren, g_tex, NULL, NULL);
    SDL_RenderPresent(g_ren);
}

static uint8_t key_to_button(SDL_Keycode k) {
    switch (k) {
    case SDLK_RETURN:
    case SDLK_KP_ENTER:
        return HAL_BTN_SELECT;
    case SDLK_RIGHT:
        return HAL_BTN_RIGHT;
    case SDLK_LEFT:
        return HAL_BTN_LEFT;
    case SDLK_SPACE:
    case SDLK_p:
        return HAL_BTN_PLAY;
    case SDLK_ESCAPE:
    case SDLK_BACKSPACE:
    case SDLK_m:
        return HAL_BTN_MENU;
    default:
        return 0;
    }
}

void host_pump_events(void) {
    if (!g_win || !pthread_equal(pthread_self(), g_owner)) {
        return;
    }
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        switch (e.type) {
        case SDL_QUIT:
            exit(0); // atexit handler tears the window down; stdio is flushed
        case SDL_KEYDOWN:
        case SDL_KEYUP: {
            if (e.key.repeat) {
                break;
            }
            bool down = e.type == SDL_KEYDOWN;
            uint8_t btn = key_to_button(e.key.keysym.sym);
            if (btn) {
                host_input_button(btn, down);
            } else if (down && e.key.keysym.sym == SDLK_UP) {
                host_input_scroll(-1); // Up = counter-clockwise
            } else if (down && e.key.keysym.sym == SDLK_DOWN) {
                host_input_scroll(1);
            }
            break;
        }
        case SDL_MOUSEWHEEL:
            if (e.wheel.y != 0) {
                // Wheel up = Up key = counter-clockwise (negative), honouring natural-scroll inversion.
                host_input_scroll(e.wheel.direction == SDL_MOUSEWHEEL_FLIPPED ? e.wheel.y
                                                                              : -e.wheel.y);
            }
            break;
        default:
            break;
        }
    }
}

#else // headless

bool host_window_open(int w, int h) {
    (void)w;
    (void)h;
    return false;
}

void host_window_present(const uint16_t *fb, int w, int h, uint8_t backlight) {
    (void)fb;
    (void)w;
    (void)h;
    (void)backlight;
}

void host_pump_events(void) {}

#endif
