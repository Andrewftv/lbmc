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
#include <VG/openvg.h>
#include <VG/vgu.h>
#include <interface/vmcs_host/vc_dispmanx_types.h>

#include "guiapi.h"
#include "log.h"

int gui_draw_ellipse(win_h hwin, int x, int y, int w, int h, int filled,
	uint32_t color)
{
	VGPath path;
	VGPaint paint = 0;
	int rc = 0;
	VGPaintMode paint_mode = VG_FILL_PATH;

	if (!filled)
		paint_mode = VG_STROKE_PATH;

	gui_activate_window(hwin);
	path = vgCreatePath(VG_PATH_FORMAT_STANDARD, VG_PATH_DATATYPE_F, 1.0, 0.0,
    	0, 0, VG_PATH_CAPABILITY_ALL);
	if (!path)
	{
		gui_release_window(hwin);
		DBG_E("%s: Can not create path\n", __FUNCTION__);
		return -1;
	}

	paint = vgCreatePaint(); 
	if (!paint)
	{
		DBG_E("%s: Can not create paint\n", __FUNCTION__);
		rc = -1;
		goto Exit;		
	}

	vgSetColor(paint, color);
	vguEllipse(path, x, y, w, h);
	vgSetPaint(paint, paint_mode);
	vgDrawPath(path, paint_mode);

Exit:
	if (paint)
		vgDestroyPaint(paint);
	vgDestroyPath(path);
	gui_release_window(hwin);
	return rc;
}

int gui_draw_polygon(win_h hwin, float *points, int count, int closed,
	int filled, uint32_t color)
{
	VGPath path;
	VGPaint paint = 0;
	int rc = 0;
	VGPaintMode paint_mode = VG_STROKE_PATH;

	if (filled && closed)
		paint_mode = VG_FILL_PATH;

	gui_activate_window(hwin);
	path = vgCreatePath(VG_PATH_FORMAT_STANDARD, VG_PATH_DATATYPE_F, 1.0, 0.0,
    	0, 0, VG_PATH_CAPABILITY_ALL);
	if (!path)
	{
		gui_release_window(hwin);
		DBG_E("%s: Can not create path\n", __FUNCTION__);
		return -1;
	}

	paint = vgCreatePaint(); 
	if (!paint)
	{
		DBG_E("%s: Can not create paint\n", __FUNCTION__);
		rc = -1;
		goto Exit;		
	}

	vgSetColor(paint, color);
	vguPolygon(path, points, count, closed);
	vgSetPaint(paint, paint_mode);
	vgDrawPath(path, paint_mode);

Exit:
	if (paint)
		vgDestroyPaint(paint);
	vgDestroyPath(path);
	gui_release_window(hwin);
	return rc;
}

int gui_draw_line(win_h hwin, int x1, int y1, int x2, int y2, uint32_t color)
{
	VGPath path;
	VGPaint paint = 0;
	int rc = 0;

	gui_activate_window(hwin);
	path = vgCreatePath(VG_PATH_FORMAT_STANDARD, VG_PATH_DATATYPE_F, 1.0, 0.0,
    	0, 0, VG_PATH_CAPABILITY_ALL);
	if (!path)
	{
		gui_release_window(hwin);
		DBG_E("%s: Can not create path\n", __FUNCTION__);
		return -1;
	}

	paint = vgCreatePaint(); 
	if (!paint)
	{
		DBG_E("%s: Can not create paint\n", __FUNCTION__);
		rc = -1;
		goto Exit;		
	}

	vgSetColor(paint, color);
	vguLine(path, x1, y1, x2, y2);
	vgSetPaint(paint, VG_STROKE_PATH);
	vgDrawPath(path, VG_STROKE_PATH);

Exit:
	if (paint)
		vgDestroyPaint(paint);
	vgDestroyPath(path);
	gui_release_window(hwin);
	return rc;
} 

int gui_draw_round_rect(win_h hwin, l_rect_t *rect, int arcw, int arch,
	int filled, uint32_t color)
{
	VGPath path;
	VGPaint paint = 0;
	int rc = 0;
	VGPaintMode paint_mode = VG_FILL_PATH;

	if (!filled)
		paint_mode = VG_STROKE_PATH;

	gui_activate_window(hwin);	
	path = vgCreatePath(VG_PATH_FORMAT_STANDARD, VG_PATH_DATATYPE_F, 1.0, 0.0,
    	0, 0, VG_PATH_CAPABILITY_ALL);
	if (!path)
	{
		gui_release_window(hwin);
		DBG_E("%s: Can not create path\n", __FUNCTION__);
		return -1;
	}

	paint = vgCreatePaint(); 
	if (!paint)
	{
		DBG_E("%s: Can not create paint\n", __FUNCTION__);
		rc = -1;
		goto Exit;		
	}

	vgSetColor(paint, color);
	vguRoundRect(path, rect->x, rect->y, rect->width, rect->height, arcw, arch);
	vgSetPaint(paint, paint_mode);
	vgDrawPath(path, paint_mode);

Exit:
	if (paint)
		vgDestroyPaint(paint);
	vgDestroyPath(path);
	gui_release_window(hwin);
	return rc;
}

int gui_draw_rect(win_h hwin, l_rect_t *rect, uint32_t color, int filled)
{
	VGPath path;
	VGPaint paint = 0;
	int rc = 0;
	VGPaintMode paint_mode = VG_FILL_PATH;

	if (!filled)
		paint_mode = VG_STROKE_PATH;

	gui_activate_window(hwin);
	path = vgCreatePath(VG_PATH_FORMAT_STANDARD, VG_PATH_DATATYPE_F, 1.0, 0.0,
    	0, 0, VG_PATH_CAPABILITY_ALL);
	if (!path)
	{
		gui_release_window(hwin);
		DBG_E("%s: Can not create path\n", __FUNCTION__);
		return -1;
	}

	paint = vgCreatePaint(); 
	if (!paint)
	{
		DBG_E("%s: Can not create paint\n", __FUNCTION__);
		rc = -1;
		goto Exit;		
	}

	vgSetColor(paint, color);
	vguRect(path, rect->x, rect->y, rect->width, rect->height);
	vgSetPaint(paint, paint_mode);
	vgDrawPath(path, paint_mode);

Exit:
	if (paint)
		vgDestroyPaint(paint);
	vgDestroyPath(path);
	gui_release_window(hwin);
	return rc;
}

int gui_draw_get_line_width(win_h hwin)
{
	float ret;

	gui_activate_window(hwin);
	ret = vgGetf(VG_STROKE_LINE_WIDTH);
	gui_release_window(hwin);
	return ret;
}

void gui_draw_set_line_width(win_h hwin, int width)
{
	gui_activate_window(hwin);
	vgSetf(VG_STROKE_LINE_WIDTH, width);
	gui_release_window(hwin);
}

line_cap_style_t gui_draw_get_cap_style(win_h hwin)
{
	line_cap_style_t rc = DRAW_LINE_CAP_UNKNOWN;

	gui_activate_window(hwin);
	switch(vgGeti(VG_STROKE_CAP_STYLE))
	{
	case VG_CAP_BUTT:
		rc = DRAW_LINE_CAP_BUTT;
		break;
	case VG_CAP_ROUND:
		rc = DRAW_LINE_CAP_ROUND;
		break;
	case VG_CAP_SQUARE:
		rc = DRAW_LINE_CAP_SQUARE;
		break;
	}
	gui_release_window(hwin);

	return rc;
}

void gui_draw_set_cap_style(win_h hwin, line_cap_style_t style)
{
	VGCapStyle vg_style;

	switch (style)
	{
	case DRAW_LINE_CAP_BUTT:
		vg_style = VG_CAP_BUTT;
		break;
	case DRAW_LINE_CAP_ROUND:
		vg_style = VG_CAP_ROUND;
		break;
	case DRAW_LINE_CAP_SQUARE:
		vg_style = VG_CAP_SQUARE;
		break;
	default:
		return;
	}

	gui_activate_window(hwin);
	vgSeti(VG_STROKE_CAP_STYLE, vg_style);
	gui_release_window(hwin);
}

line_join_style_t gui_draw_get_join_style(win_h hwin)
{
	line_join_style_t rc = DRAW_LINE_JOIN_UNKNOWN;

	gui_activate_window(hwin);
	switch (vgGeti(VG_STROKE_JOIN_STYLE))
	{
	case VG_JOIN_MITER:
		rc = DRAW_LINE_JOIN_MITER;
		break;
	case VG_JOIN_ROUND:
		rc = DRAW_LINE_JOIN_ROUND;
		break;
	case VG_JOIN_BEVEL:
		rc = DRAW_LINE_JOIN_BEVEL;
		break;
	}
	gui_release_window(hwin);

	return rc;
}

void gui_draw_set_join_style(win_h hwin, line_join_style_t style)
{
	VGJoinStyle join_style;

	switch (style)
	{
	case DRAW_LINE_JOIN_MITER:
		join_style = VG_JOIN_MITER;
		break;
	case DRAW_LINE_JOIN_ROUND:
		join_style = VG_JOIN_ROUND;
		break;
	case DRAW_LINE_JOIN_BEVEL:
		join_style = VG_JOIN_BEVEL;
		break;
	default:
		return;
	}

	gui_activate_window(hwin);
	vgSeti(VG_STROKE_JOIN_STYLE, join_style);
	gui_release_window(hwin);
}

float gui_draw_get_miter_limit(win_h hwin)
{
	float ret;

	gui_activate_window(hwin);
	ret = vgGetf(VG_STROKE_MITER_LIMIT);
	gui_release_window(hwin);

	return ret;
}

void gui_draw_set_miter_limit(win_h hwin, float limit)
{
	gui_activate_window(hwin);
	vgSetf(VG_STROKE_MITER_LIMIT, limit);
	gui_release_window(hwin);
}

