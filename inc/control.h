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

#ifndef __CONTROL_H__
#define __CONTROL_H__

#include "errors.h"
#include "queue.h"

typedef void* control_ctx_h;

typedef enum {
    L_EVENT_NONE,
    L_EVENT_QUIT,
    L_EVENT_PAUSE,
    L_EVENT_PLAY,
    L_EVENT_STOP,
    L_EVENT_SEEK_RIGHT,
    L_EVENT_SEEK_LEFT,
    L_EVENT_AUDIO_STREEM,
    L_EVENT_MUTE,
    L_EVENT_INFO
} event_code_t;

typedef struct {
    queue_node_t node;
    event_code_t code;
    uint32_t data;
} user_event_t;

typedef event_code_t (*get_event_cb)(control_ctx_h h, uint32_t *data);

ret_code_t control_init(control_ctx_h *h);
void control_uninit(control_ctx_h h);

ret_code_t control_register_callback(control_ctx_h h, get_event_cb cb, void *user_data);
event_code_t control_get_event(control_ctx_h h, uint32_t *data);
void *control_get_user_data(control_ctx_h h);

#endif
