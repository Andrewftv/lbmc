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

ret_code_t img_decode(img_h h);
uint8_t *img_get_raw_buffer(img_h hctx, int *w, int *h, VGImageFormat *rgb);

#endif

