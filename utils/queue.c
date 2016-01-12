#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include "queue.h"

typedef struct omx_node_s {
    struct omx_node_s *next;
	void *data;
} omx_node_t;

typedef struct {
    omx_node_t *first_node;
    omx_node_t *last_node;
    pthread_mutex_t mutex;
    int count;
} omx_queue_t;

static void destroy_queue(omx_queue_t *q)
{
    omx_node_t *node, *tmp;	

    pthread_mutex_lock(&q->mutex);
    node = q->first_node;
    while (node)
    {
		tmp = node;
		node = node->next;
		free(tmp);
    }
    pthread_mutex_unlock(&q->mutex);
}

int omx_queue_init(omx_queue_h *h)
{
    omx_queue_t *queue;

    queue = (omx_queue_t *)malloc(sizeof(omx_queue_t));
    if (!queue)
		return -1;

    memset(queue, 0, sizeof(omx_queue_t));
    if (pthread_mutex_init(&queue->mutex, NULL))
		goto Error;

    *h = queue;

    return 0;

Error:
    omx_queue_uninit(queue);
    return -1;
}

void omx_queue_uninit(omx_queue_h h)
{
    omx_queue_t *queue = (omx_queue_t *)h;

    if (!queue)
		return;
	
    destroy_queue(queue);

    pthread_mutex_destroy(&queue->mutex);
    free(queue);
}

int omx_queue_push(omx_queue_h h, void *data)
{
    omx_queue_t *q = (omx_queue_t *)h;
	omx_node_t *node;

	node = (omx_node_t *)malloc(sizeof(omx_node_t));
	if (!node)
		return -1;

    node->next = NULL;
	node->data = data;

    pthread_mutex_lock(&q->mutex);
    if (!q->first_node)
    {
		q->first_node = node;
		q->last_node = node;
    }
    else
    {
		q->last_node->next = node;
		q->last_node = node;
    }
    q->count++;
    pthread_mutex_unlock(&q->mutex);

    return 0;
}

void *omx_queue_pop(omx_queue_h h)
{
    omx_node_t *node;
    omx_queue_t *q = (omx_queue_t *)h;
	void *data;

    pthread_mutex_lock(&q->mutex);
    if (!q->first_node)
    {
		pthread_mutex_unlock(&q->mutex);
    	return NULL;
    }
    node = q->first_node;
    q->first_node = node->next;
    if (!q->first_node)
		q->last_node = NULL;
    q->count--;
    pthread_mutex_unlock(&q->mutex);

    data = node->data;
	free(node);

    return data;
}

int omx_queue_count(omx_queue_h h)
{
    int count;
    omx_queue_t *q = (omx_queue_t *)h;

    pthread_mutex_lock(&q->mutex);
    count = q->count;
    pthread_mutex_unlock(&q->mutex);

    return count;
}

