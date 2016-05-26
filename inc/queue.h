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

