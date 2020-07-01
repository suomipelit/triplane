/* 
 * Triplane Classic - a side-scrolling dogfighting game.
 * Copyright (C) 1996,1997,2009  Dodekaedron Software Creations Oy
 * 
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * tjt@users.sourceforge.net
 */

#include "io/video.h"
#include "io/dksfile.h"
#include "util/wutil.h"
#include <SDL.h>
#include <signal.h>
#if !defined(_MSC_VER)
#include <unistd.h>
#endif
#include <assert.h>
#include <string.h>

struct video_state_t video_state = { NULL, 0, 0 };

struct naytto ruutu;

int current_mode = VGA_MODE;
unsigned char *vircr;
unsigned int window_multiplier_vga = 2, window_multiplier_svga = 1;
int wantfullscreen = 0;
SDL_Rect render_dest_rect;

SDL_Color curpal[256];

/**
 * Sets palette entries firstcolor to firstcolor+n-1
 * from pal[0] to pal[n-1].
 * @param pal the palette, specify NULL to set all colors to black=(0,0,0)
 * @param reverse = 1 to read colors in reverse order (pal[n-1] to pal[0])
 */
void setpal_range(const char pal[][3], int firstcolor, int n, int reverse) {
    SDL_Color *cc = (SDL_Color *) walloc(n * sizeof(SDL_Color));
    int i, from = (reverse ? n - 1 : 0);

    for (i = 0; i < n; i++) {
        if (pal == NULL) {
            cc[i].r = cc[i].g = cc[i].b = 0;
        } else {
            cc[i].r = 4 * pal[from][0];
            cc[i].g = 4 * pal[from][1];
            cc[i].b = 4 * pal[from][2];
        }
        if (reverse)
            from--;
        else
            from++;
    }

    SDL_SetPaletteColors(video_state.surface->format->palette, cc, firstcolor, n);

    memcpy(&curpal[firstcolor], cc, n * sizeof(SDL_Color));
    wfree(cc);
}

void fillrect(int x, int y, int w, int h, int c) {
    SDL_Rect r;
    r.x = x;
    r.y = y;
    r.w = w;
    r.h = h;
    SDL_FillRect(video_state.surface, &r, c);
}

int get_screen_width(void) {
    return (current_mode == SVGA_MODE) ? 800 : 320;
}

int get_screen_height(void) {
    return (current_mode == SVGA_MODE) ? 600 : 200;
}

void refresh_rendering() {
    int window_w, window_h;
    const int scr_x_size = get_screen_width();
    const int scr_y_size = get_screen_height();
    const double aspect = static_cast<float>(scr_x_size) / scr_y_size;

    SDL_GetRendererOutputSize(video_state.renderer, &window_w, &window_h);

    if (1.0 * window_w / window_h <= aspect) {
        const int h = static_cast<int>(window_w / aspect);
        render_dest_rect.x = 0;
        render_dest_rect.y = (window_h - h) / 2;
        render_dest_rect.w = window_w;
        render_dest_rect.h = h;
    } else {
        const int w = static_cast<int>(window_h * aspect);
        render_dest_rect.x = (window_w - w) / 2;
        render_dest_rect.y = 0;
        render_dest_rect.w = w;
        render_dest_rect.h = window_h;
    }
}

unsigned int get_window_multiplier(void) {
    if (wantfullscreen) {
        int window_w, window_h;
        SDL_GetRendererOutputSize(video_state.renderer, &window_w, &window_h);

        return window_h / get_screen_height();
    }

    return (current_mode == SVGA_MODE) ?
        window_multiplier_svga : window_multiplier_vga;
}

static void reset_window_size(void) {
    const int scr_x_size = get_screen_width();
    const int scr_y_size = get_screen_height();
    const unsigned int windowMultiplier = get_window_multiplier();

    SDL_SetWindowSize(video_state.window,
        scr_x_size * windowMultiplier, scr_y_size * windowMultiplier);

    refresh_rendering();
}

void increase_scaling(void) {
    if (current_mode == SVGA_MODE) {
        if (window_multiplier_svga < 2) {
            ++window_multiplier_svga;
            reset_window_size();
        }
    } else {
        if (window_multiplier_vga < 4) {
            ++window_multiplier_vga;
            reset_window_size();
        }
    }
}

void decrease_scaling(void) {
    if (current_mode == SVGA_MODE) {
        if (window_multiplier_svga > 1) {
            --window_multiplier_svga;
            reset_window_size();
        }
    } else {
        if (window_multiplier_vga > 1) {
            --window_multiplier_vga;
            reset_window_size();
        }
    }
}

void do_all(int do_retrace) {
    /* Blit 8-bit surface to 32-bit surface */
    SDL_BlitSurface(video_state.surface, NULL, video_state.displaySurface, NULL);

    void *pixels;
    int pitch;

    SDL_LockTexture(video_state.texture, NULL, &pixels, &pitch);

    /* Convert 8-bit data to renderable 32-bit data */
    SDL_ConvertPixels(video_state.displaySurface->w, video_state.displaySurface->h,
        video_state.displaySurface->format->format,
        video_state.displaySurface->pixels, video_state.displaySurface->pitch,
        SDL_PIXELFORMAT_RGBA8888,
        pixels, pitch);

    SDL_UnlockTexture(video_state.texture);

    /* Render texture to display */
    SDL_RenderCopy(video_state.renderer, video_state.texture, NULL, &render_dest_rect);
    SDL_RenderPresent(video_state.renderer);
}

static void sigint_handler(int dummy) {
    _exit(1);
}

void init_video(void) {
    int ret;

    if (!video_state.init_done) {
        ret = SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER | SDL_INIT_JOYSTICK | SDL_INIT_NOPARACHUTE);
        if (ret) {
            fprintf(stderr, "SDL_Init failed with %d. Is your DISPLAY environment variable set?\n", ret);
            exit(1);
        }
        signal(SIGINT, sigint_handler);
        atexit(SDL_Quit);
        video_state.init_done = 1;

        SDL_ShowCursor(SDL_DISABLE);
    }
}

static void deinit() {
    if (video_state.texture) {
        SDL_DestroyTexture(video_state.texture);
        video_state.texture = NULL;
    }
    if (video_state.displaySurface) {
        SDL_FreeSurface(video_state.displaySurface);
        video_state.displaySurface = NULL;
    }
    if (video_state.renderer) {
        SDL_DestroyRenderer(video_state.renderer);
        video_state.renderer = NULL;
    }
    if (video_state.window) {
        SDL_DestroyWindow(video_state.window);
        video_state.window = NULL;
    }
    if (video_state.surface) {
        SDL_FreeSurface(video_state.surface);
        video_state.surface = NULL;
    }
}

void deinit_video(void) {
    if (video_state.init_done) {
        deinit();
        SDL_Quit();
    }
}

static int init_mode(int new_mode, const char *paletname) {
    Uint32 mode_flags;
    int las, las2;
    int w = (new_mode == SVGA_MODE) ? 800 : 320;
    int h = (new_mode == SVGA_MODE) ? 600 : 200;

    init_video();

    mode_flags = SDL_WINDOW_OPENGL;

    if (wantfullscreen)
        mode_flags |= SDL_WINDOW_FULLSCREEN_DESKTOP;

    if (video_state.window) {
        deinit();
    }

    const unsigned int window_multiplier = new_mode == VGA_MODE ?
        window_multiplier_vga :
        window_multiplier_svga;

    video_state.window = SDL_CreateWindow("Triplane Classic",
        SDL_WINDOWPOS_UNDEFINED,
        SDL_WINDOWPOS_UNDEFINED,
        w * window_multiplier, h * window_multiplier,
        mode_flags);

    assert(video_state.window);

    video_state.surface = SDL_CreateRGBSurface(0,
        w, h,
        8, 0, 0, 0, 0);

    assert(video_state.surface);

    video_state.renderer = SDL_CreateRenderer(video_state.window, -1, 0);

    assert(video_state.surface);

    video_state.displaySurface = SDL_CreateRGBSurface(0,
        w, h,
        32, 0, 0, 0, 0);

    assert(video_state.displaySurface);

    video_state.texture = SDL_CreateTexture(video_state.renderer,
        SDL_PIXELFORMAT_RGBA8888,
        SDL_TEXTUREACCESS_STREAMING,
        w,
        h);

    assert(video_state.texture);

    vircr = (uint8_t *) video_state.surface->pixels;

    dksopen(paletname);

    dksread(ruutu.normaalipaletti, sizeof(ruutu.normaalipaletti));
    for (las = 0; las < 256; las++)
        for (las2 = 0; las2 < 3; las2++)
            ruutu.paletti[las][las2] = ruutu.normaalipaletti[las][las2];

    dksclose();

    setpal_range(ruutu.paletti, 0, 256);

    current_mode = new_mode;

    refresh_rendering();

    return 1;
}

int init_vesa(const char *paletname) {
    return init_mode(SVGA_MODE, paletname);
}

void init_vga(const char *paletname) {
    init_mode(VGA_MODE, paletname);
}
