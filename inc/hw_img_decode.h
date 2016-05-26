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
#ifndef __LBMC_DEBUG_H__
#define __LBMC_DEBUG_H__

#include <VG/openvg.h>
#include "errors.h"

typedef void* img_h;

typedef enum {
    PNG_IMG_FILE,
    JPEG_IMG_FILE,
    UNKNOWN_IMG_FILE
} image_type_t;

ret_code_t img_init(img_h *h, image_type_t type, char *file);
void img_uninit(img_h h);

ret_code_t img_decode(img_h h, int width, int height);
uint8_t *img_get_raw_buffer(img_h hctx, int *w, int *h, VGImageFormat *rgb);

#endif
