#ifndef __LBMC_DEBUG_H__
#define __LBMC_DEBUG_H__

#include "errors.h"

typedef void* jpeg_h;

ret_code_t jpeg_init(jpeg_h *h, char *file);
void jpeg_uninit(jpeg_h h);

ret_code_t jpeg_decode(jpeg_h h);

#endif

