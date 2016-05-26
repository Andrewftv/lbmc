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
#ifndef __FT_TEXT_H__
#define __FT_TEXT_H__

#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_STROKER_H

#include "errors.h"

typedef void* ft_text_h;

ret_code_t ft_text_init(ft_text_h *h);
void ft_text_uninit(ft_text_h h);

ret_code_t ft_text_set_size(ft_text_h *h, int size);
ret_code_t ft_text_get_size(ft_text_h *h, int *size);

ret_code_t ft_load_char(ft_text_h *h, char ch);
FT_GlyphSlot ft_text_get_glyph(ft_text_h *h);
ret_code_t ft_load_stroker(ft_text_h *h, char ch, FT_Bitmap *bitmap);
void ft_done_stroker(ft_text_h *h);

#endif
