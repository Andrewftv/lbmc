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

