#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include "queue.h"
#include "msleep.h"

typedef struct {
    queue_node_t *first_node;
    queue_node_t *last_node;
    pthread_mutex_t mutex;
    int count;

    msleep_h wait;
} queue_t;

static void destroy_queue(queue_t *q)
{
    queue_node_t *node, *tmp;    

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

queue_err_t queue_init(queue_h *h)
{
    queue_t *queue;

    queue = (queue_t *)malloc(sizeof(queue_t));
    if (!queue)
        return QUE_FAILED;

    memset(queue, 0, sizeof(queue_t));
    if (pthread_mutex_init(&queue->mutex, NULL))
        goto Error;

    msleep_init(&queue->wait);

    *h = queue;

    return QUE_OK;

Error:
    queue_uninit(queue);
    return QUE_FAILED;
}

void queue_uninit(queue_h h)
{
    queue_t *queue = (queue_t *)h;

    if (!queue)
        return;
    
    msleep_uninit(queue->wait);
    destroy_queue(queue);

    pthread_mutex_destroy(&queue->mutex);
    free(queue);
}

queue_err_t queue_push(queue_h h, queue_node_t *node)
{
    queue_t *q = (queue_t *)h;

    node->next = NULL;

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

    msleep_wakeup(q->wait);

    return QUE_OK;
}

/* FIXME. Incorrect implementation */
queue_node_t *queue_pop_timed(queue_h h, int timeout)
{
    queue_node_t *node;
    queue_t *q = (queue_t *)h;

    node = queue_pop(h);
    if (!node)
        msleep_wait(q->wait, timeout);

    return queue_pop(h);
}

queue_node_t *queue_pop(queue_h h)
{
    queue_node_t *node;
    queue_t *q = (queue_t *)h;

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

    return node;
}

int queue_count(queue_h h)
{
    int count;
    queue_t *q = (queue_t *)h;

    pthread_mutex_lock(&q->mutex);
    count = q->count;
    pthread_mutex_unlock(&q->mutex);

    return count;
}

