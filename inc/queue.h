#ifndef __OMX_QUEUE_H__
#define __OMX_QUEUE_H__

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    QUE_OK = 0,
    QUE_TIMEOUT = 1,
    QUE_FAILED = -1
} queue_err_t;

typedef void* queue_h;

typedef struct queue_node_s {
    struct  queue_node_s *next;
}  queue_node_t;

queue_err_t queue_init(queue_h *h);
void queue_uninit(queue_h h);
queue_err_t queue_push(queue_h h, queue_node_t *node);
queue_node_t *queue_pop(queue_h h);
queue_node_t *queue_pop_timed(queue_h h, int timeout);
int queue_count(queue_h h);

#ifdef __cplusplus
}
#endif

#endif

