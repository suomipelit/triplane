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

#ifndef MENUSUPPORT_H
#define MENUSUPPORT_H

/* Support functions for menus */

#include <stdlib.h>

struct menu_position {
  int x, y;
  int active;
};

void menu_keys(int *exit_flag, int *help_flag);
void menu_mouse(int *x, int *y, int *n1, int *n2,
                const menu_position *positions = NULL);
void wait_mouse_relase(int nokb = 0);
void wait_press_and_release(void);

#endif
