/*
 *      Copyright (C) 2016  Andrew Fateyev
 *      andrew.ftv@gmail.com
 * 
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */
#ifndef __LBMC_TYPES_H__
#define __LBMC_TYPES_H__

#ifdef CONFIG_RASPBERRY_PI
typedef VC_RECT_T l_rect_t;
#else
typedef struct {
    int x;
    int y;
    int width;
    int height;
} l_rect_t;
#endif

#ifdef CONFIG_RASPBERRY_PI
typedef VG_INVALID_HANDLE L_INVALID_HANDLE;
#else
#define L_INVALID_HANDLE NULL
#endif

#endif
