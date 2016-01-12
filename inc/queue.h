#ifndef __OMX_QUEUE_H__
#define __OMX_QUEUE_H__

#ifdef __cplusplus
extern "C" {
#endif

typedef void* omx_queue_h;

int omx_queue_init(omx_queue_h *h);
void omx_queue_uninit(omx_queue_h h);
int omx_queue_push(omx_queue_h h, void *node);
void *omx_queue_pop(omx_queue_h h);
int omx_queue_count(omx_queue_h h);

#ifdef __cplusplus
}
#endif

#endif

