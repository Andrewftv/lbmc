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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <semaphore.h>
#include <errno.h>

#include "queue.h"
#include "log.h"
#include "timeutils.h"

typedef struct {
    queue_node_t *first_node;
    queue_node_t *last_node;
    pthread_mutex_t mutex;
    sem_t sem_count;
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

    if (sem_init(&queue->sem_count, 0, 0))
        goto Error;

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
    
    destroy_queue(queue);

    sem_destroy(&queue->sem_count);
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
    sem_post(&q->sem_count);
    pthread_mutex_unlock(&q->mutex);

    return QUE_OK;
}

queue_node_t *queue_pop_timed(queue_h h, int timeout)
{
    queue_t *q = (queue_t *)h;
    queue_node_t *node;
    struct timespec wait_time;

    if (!timeout)
        return queue_pop(h);

    if (timeout != QUEUE_INFINITE_WAIT)
    {
        clock_gettime(CLOCK_REALTIME, &wait_time);
        util_time_add(&wait_time, timeout);

        if (sem_timedwait(&q->sem_count, &wait_time) == -1)
        {
            if (errno != ETIMEDOUT)
            {
                DBG_E("Function sem_timedwait failed\n");
            }
            return NULL;
        }
    }
    else
    {
        sem_wait(&q->sem_count);
    }

    pthread_mutex_lock(&q->mutex);
    if (!q->first_node)
    {
        pthread_mutex_unlock(&q->mutex);

        DBG_E("Oops!!! Incorrect situation\n");

        return NULL;
    }
    node = q->first_node;
    q->first_node = node->next;
    if (!q->first_node)
        q->last_node = NULL;

    pthread_mutex_unlock(&q->mutex);

    return node;
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
    /* Item found. Decrement the counter */
    sem_wait(&q->sem_count); 

    q->first_node = node->next;
    if (!q->first_node)
        q->last_node = NULL;
    pthread_mutex_unlock(&q->mutex);

    return node;
}

int queue_count(queue_h h)
{
    int count;
    queue_t *q = (queue_t *)h;

    pthread_mutex_lock(&q->mutex);
    sem_getvalue(&q->sem_count, &count);
    pthread_mutex_unlock(&q->mutex);

    return count;
}

