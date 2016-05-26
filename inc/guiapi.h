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
#ifndef __GUI_API_H__
#define __GUI_API_H__

#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_STROKER_H

#include <interface/vmcs_host/vc_dispmanx_types.h>
#include <EGL/eglplatform.h>
#include <EGL/egl.h>

typedef void* gui_h;
typedef void* win_h;
typedef void* font_h;
typedef void* image_h;

typedef enum {
	DRAW_LINE_CAP_BUTT = 0,
	DRAW_LINE_CAP_ROUND,
	DRAW_LINE_CAP_SQUARE,
	DRAW_LINE_CAP_UNKNOWN = 100
} line_cap_style_t;

typedef enum {
	DRAW_LINE_JOIN_MITER = 0,
	DRAW_LINE_JOIN_ROUND,
	DRAW_LINE_JOIN_BEVEL,
	DRAW_LINE_JOIN_UNKNOWN = 100
} line_join_style_t;

#define FONT_STYLE_STROKER	(1 << 0)

#ifdef __cplusplus
extern "C" {
#endif

int gui_lock(void);
void gui_unlock(void);

int gui_activate_window(win_h hwin);
void gui_release_window(win_h hwin);

void gui_uninit(gui_h hgui);
int gui_init(gui_h *hgui);

void window_uninit(win_h hwin);
int window_init(gui_h hgui, win_h *hwin, int x, int y, int w, int h);

FT_Library gui_get_ft_library(gui_h hgui);
int gui_get_window_rect(win_h hwin, VC_RECT_T *rect);
void gui_window_flush(win_h hwin);
void gui_win_clear_window(win_h hwin);
gui_h gui_win_get_ui_handler(win_h hwin);
EGLDisplay gui_get_display(gui_h hgui);
EGLContext gui_get_context(gui_h hgui);
EGLSurface gui_win_get_context(win_h hwin);

int gui_load_font(win_h hwin, char *font_path, int font_size, int border,
	font_h *hfont);
void gui_unload_font(win_h hwin, font_h hfont);
int gui_set_font_size(font_h hfont, int size);
int gui_font_draw_text(win_h hwin, font_h hfont, char *text, int x, int y);
void gui_font_set_font_color(font_h hfont, uint32_t rgba);
int gui_font_get_string_len(font_h hfont, char *text);

void gui_draw_set_line_width(win_h hwin, int width);
int gui_draw_get_line_width(win_h hwin);
line_cap_style_t gui_draw_get_cap_style(win_h hwin);
void gui_draw_set_cap_style(win_h hwin, line_cap_style_t style);
void gui_draw_set_join_style(win_h hwin, line_join_style_t style);
line_join_style_t gui_draw_get_join_style(win_h hwin);
void gui_draw_set_miter_limit(win_h hwin, float limit);
float gui_draw_get_miter_limit(win_h hwin);
int gui_draw_rect(win_h hwin, VC_RECT_T *rect, uint32_t color, int filled);
int gui_draw_line(win_h hwin, int x1, int y1, int x2, int y2, uint32_t color);
int gui_draw_polygon(win_h hwin, float *points, int count, int closed,
	int filled, uint32_t color);
int gui_draw_ellipse(win_h hwin, int x, int y, int w, int h, int filled,
	uint32_t color);
int gui_draw_round_rect(win_h hwin, VC_RECT_T *rect, int arcw, int arch,
	int filled, uint32_t color);

int gui_image_load(win_h hwin, char *image_path, image_h *h);
void gui_image_unload(win_h hwin, image_h h);
int gui_image_draw(win_h hwin, image_h h, int x, int y, uint8_t alpha);
int gui_image_get_width(image_h h);
int gui_image_get_height(image_h h);
int gui_image_get_depth(image_h h);

#ifdef __cplusplus
}
#endif

#endif

