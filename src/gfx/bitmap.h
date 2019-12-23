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

#ifndef BITMAP_H
#define BITMAP_H

#include <SDL.h>
#include <stdlib.h>
#include <stdint.h>

class Bitmap {
    unsigned char *image_data;
    const char *name;           /* for debugging only, not always valid */
    int16_t width, height;
    int external_image_data;    // boolean: is image_data owned by this instance
    int hastransparency;
    int id;                     // a unique ID for this Bitmap
    int data_sent;              // has image_data been sent to the network?

  public:
      Bitmap(const char *image_name, int transparent = 1);
      Bitmap(int xl, int yl,
             unsigned char *image_data,
             const char *name = "unknown",
             int hastransparency = 1,
             int copy_image_data = 0);
      Bitmap(int x1, int y1, int xl, int yl, Bitmap * source_image);
      Bitmap(int x, int y, int w, int h);
     ~Bitmap();

    void blit(int xx, int yy, int rx = 0, int ry = 0, int rx2 = 319, int ry2 = 199);
    void blit_fullscreen(void);
    void blit_to_bitmap(Bitmap * to, int xx, int yy);
    void recolor(unsigned char oldcolor, unsigned char newcolor);
    void outline(unsigned char outlinecolor);

    unsigned char *info(int *width = NULL, int *height = NULL);
    void clear_data_sent();
    void send_bitmapdata();
    void resend_bitmapdata();

 private:
    void blit_data(int tox, int toy,
                   const unsigned char *data, int w, int h,
                   int mask_color=-1);
};

void all_bitmaps_resend_if_sent(void);
void all_bitmaps_send_now(void);
Bitmap *rotate_bitmap(Bitmap * picture, int degrees);
int bitmap_exists(const char *name);

#endif
