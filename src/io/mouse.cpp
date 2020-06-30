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

/*******************************************************************************

   Purpose: 
   	Mouse handling part of Wsystem 2.0 for DJGPP v.2.0á5

*******************************************************************************/

#include "io/mouse.h"
#include "io/video.h"
#include <SDL.h>

extern int current_mode;
extern unsigned int window_multiplier_vga, window_multiplier_svga;

void hiiri_to(int x, int y) {
    SDL_WarpMouseInWindow(video_state.window, x, y);
}

namespace
{
void limit(int *x, int *y) {
    const int x_max = (current_mode == SVGA_MODE) ? 800 : 320;
    const int y_max = (current_mode == SVGA_MODE) ? 600 : 200;

    if (*x > x_max) {
        hiiri_to(x_max, *y);
        *x = x_max;
    } else if (*y > y_max) {
        hiiri_to(*x, y_max);
        *y = y_max;
    }
}
}

void koords(int *x, int *y, int *n1, int *n2) {
    Uint8 ret;

    SDL_PumpEvents();
    ret = SDL_GetMouseState(x, y);

    const unsigned int multiplier =
        (current_mode == SVGA_MODE) ? window_multiplier_svga : window_multiplier_vga;
    *x /= multiplier;
    *y /= multiplier;

    if (wantfullscreen && !ret) limit(x, y);

    *n1 = !!(ret & SDL_BUTTON(1));
    *n2 = !!(ret & SDL_BUTTON(3));
}
