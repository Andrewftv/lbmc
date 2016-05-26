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

#include "errors.h"
#include "guiapi.h"
#include "log.h"
#include "hw_img_decode.h"

typedef struct {
    img_h img;

	VGImage image;
	int width;
	int height;
	uint32_t depth;
    VGImageFormat rgb_format;
} image_ctx_t;

static image_type_t get_image_type(char *image_path)
{
	char *ext;
	image_type_t rc = UNKNOWN_IMG_FILE;

	ext = strchr(image_path, '.');
	if (!ext)
		return UNKNOWN_IMG_FILE;

	ext++;
	if (!strcasecmp(ext, "jpg"))
		rc = JPEG_IMG_FILE;
	else if (!strcasecmp(ext, "jpeg"))
		rc = JPEG_IMG_FILE;
    else if (!strcasecmp(ext, "png"))
        rc = PNG_IMG_FILE;

	return rc;
}

static VGImageFormat get_image_format(image_ctx_t *ctx)
{
	VGImageFormat rgba_format = VG_IMAGE_FORMAT_FORCE_SIZE;

	switch (ctx->depth)
	{
	case 2:
		rgba_format = VG_sBGR_565;
		break;
	case 3:
		rgba_format = VG_sXBGR_8888;
		break;
	case 4:
		rgba_format = VG_sABGR_8888;
		break;
	}

	return rgba_format;
}

static uint8_t *get_uncompressed_buffer(image_ctx_t *ctx, char *image_path, int width, int height)
{
	uint8_t *buffer = NULL;
	image_type_t img_type;

	if ((img_type = get_image_type(image_path)) == UNKNOWN_IMG_FILE)
	{
		DBG_E("%s - Unsupported image type\n", image_path);
		return NULL;
	}

    if (img_init(&ctx->img, img_type, image_path) != L_OK)
        return NULL;

    if (img_decode(ctx->img, width, height) != L_OK)
        return NULL;

    buffer = img_get_raw_buffer(ctx->img, &ctx->width, &ctx->height, &ctx->rgb_format);

	return buffer;
}

int gui_image_get_width(image_h h)
{
	image_ctx_t *ctx = (image_ctx_t *)h;

	if (!ctx || ctx->image == VG_INVALID_HANDLE)
		return -1;

	return ctx->width;
}

int gui_image_get_height(image_h h)
{
	image_ctx_t *ctx = (image_ctx_t *)h;

	if (!ctx || ctx->image == VG_INVALID_HANDLE)
		return -1;

	return ctx->height;
}

int gui_image_get_depth(image_h h)
{
	image_ctx_t *ctx = (image_ctx_t *)h;
	
	if (!ctx || ctx->image == VG_INVALID_HANDLE)
		return -1;

	return ctx->depth;
}

int gui_image_draw(win_h hwin, image_h h, int x, int y, uint8_t alpha)
{
	image_ctx_t *ctx = (image_ctx_t *)h;
	VGfloat old_matrix[9];
	VGPaint paint = VG_INVALID_HANDLE;
	VC_RECT_T rect;
	int rc = 0, err;
    VGfloat color[4] = { 1.0, 1.0, 1.0, (VGfloat)((VGfloat)alpha / (VGfloat)0xff) };

	if (!ctx || ctx->image == VG_INVALID_HANDLE)
	{
		DBG_E("Image has invalid handler\n");
		return -1;
	}
	gui_activate_window(hwin);

	gui_get_window_rect(hwin, &rect);
	vgSeti(VG_MATRIX_MODE, VG_MATRIX_IMAGE_USER_TO_SURFACE);
	vgGetMatrix(old_matrix);
	vgLoadIdentity();
	vgTranslate(x, rect.height - y);
	vgScale(1.0, -1.0);

	paint = vgCreatePaint();
	vgSetParameterfv(paint, VG_PAINT_COLOR, 4, color);
	vgSeti(VG_IMAGE_MODE, VG_DRAW_IMAGE_MULTIPLY);
	vgSetPaint(paint, VG_STROKE_PATH | VG_FILL_PATH);

	if ((err = vgGetError()))
	{
		DBG_E("Something go wrong. Error=0x%x\n", err);
		rc = -1;
		goto Exit;
	}

	vgDrawImage(ctx->image);

	if ((err = vgGetError()))
	{
		DBG_E("vgDrawImage failed. Error=0x%x\n", err);
		rc = -1;
		goto Exit;
	}

	vgLoadMatrix(old_matrix);

Exit:
	if (paint != VG_INVALID_HANDLE)
		vgDestroyPaint(paint);

	gui_release_window(hwin);

	return rc;
}

int gui_image_load(win_h hwin, char *image_path, int width, int height, image_h *h)
{
	image_ctx_t *ctx;
	uint8_t *buffer;
	uint32_t pitch;
	VGImageFormat rgba_format;
	int err;

	ctx = (image_ctx_t *)malloc(sizeof(image_ctx_t));
	if (!ctx)
	{
		DBG_E("%s: Memory allocation failed\n", __FUNCTION__);
		return -1;
	}
	memset(ctx, 0, sizeof(image_ctx_t));
	ctx->image = VG_INVALID_HANDLE;
    ctx->depth = 4; /* TODO */

	buffer = get_uncompressed_buffer(ctx, image_path, width, height);
	if (!buffer)
    {
        DBG_E("Unable get image raw buffer\n");
		goto Error;
    }
	rgba_format = get_image_format(ctx);
	pitch = ctx->width * ctx->depth;
	
	gui_activate_window(hwin);

	ctx->image = vgCreateImage(rgba_format, ctx->width, ctx->height, VG_IMAGE_QUALITY_BETTER);
	if (ctx->image != VG_INVALID_HANDLE)
	{
		vgImageSubData(ctx->image, buffer, pitch, rgba_format, 0, 0, ctx->width, ctx->height);
	}
	else
	{
		DBG_E("vgCreateImage failed. Error=%d\n", vgGetError());
	}

	if ((err = vgGetError()))
	{
		DBG_E("After vgCreateImage. Error=0x%x\n", err);
	}

	gui_release_window(hwin);

	*h = ctx;

	return 0;

Error:
	gui_image_unload(hwin, ctx);

	return -1;
}

void gui_image_unload(win_h hwin, image_h h)
{
	image_ctx_t *ctx = (image_ctx_t *)h;

	if (!ctx)
		return;

    img_uninit(ctx->img);

	if (ctx->image != VG_INVALID_HANDLE)
	{
		gui_activate_window(hwin);
		vgDestroyImage(ctx->image);
		gui_release_window(hwin);
	}
	free(ctx);
}

