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

#include <stdlib.h>
#include "stdint.h"
#include "unistd.h"

#include "log.h"
#include "control.h"

typedef struct control_ctx_s {
    get_event_cb get_event;
    void *user_data;
} control_ctx_t;

static int kbhit()
{
    struct timeval tv = { 0L, 0L };
    fd_set fds;

    FD_ZERO(&fds);
    FD_SET(0, &fds);
    return select(1, &fds, NULL, NULL, &tv);
}

static int getch()
{
    int rc;
    uint16_t ch = 0;
    
    if ((rc = read(0, &ch, sizeof(ch))) < 0) 
        return rc;
    else
        return ch;
}

static event_code_t get_console_event(control_ctx_h h, uint32_t *data)
{
    event_code_t rc = L_EVENT_NONE;

    *data = 0;
    if (kbhit())
    {
        uint16_t ch;

        ch = getch();
        switch(ch & 0xff)
        {
        case 'q':
            rc = L_EVENT_QUIT;
            break;
        case ' ':
            rc = L_EVENT_PAUSE;
            break;
        case 0x43:
            rc = L_EVENT_SEEK_RIGHT;
            *data = 60;
            break;
        case 0x44:
            rc = L_EVENT_SEEK_LEFT;
            *data = 60;
            break;
        case 'a':
            rc = L_EVENT_AUDIO_STREEM;
            break;
        case 'm':
            rc = L_EVENT_MUTE;
            break;
        case 'i':
            rc = L_EVENT_INFO;
            break;
        default:
            break;
        }
    }

    return rc;
}

ret_code_t control_init(control_ctx_h *h)
{
    control_ctx_t *ctx;

    ctx = (control_ctx_t *)malloc(sizeof(control_ctx_t));
    if (!ctx)
    {
        DBG_E("Unable to allocate memory\n");
        return L_MEMORY;
    }

    control_register_callback(ctx, get_console_event, NULL);

    *h = ctx;

    return L_OK;
}

void control_uninit(control_ctx_h h)
{
    control_ctx_t *ctx = (control_ctx_t *)h;

    if (ctx)
        free(ctx);
}

ret_code_t control_register_callback(control_ctx_h h, get_event_cb cb, void *user_data)
{
    control_ctx_t *ctx = (control_ctx_t *)h;

    if (!ctx)
    {
        DBG_E("Incorrect context\n");
        return L_FAILED;
    }
    if (!cb)
    {
        DBG_E("Incorrect callback pointer\n");
        return L_FAILED;
    }

    ctx->get_event = cb;
    ctx->user_data = user_data;

    return L_OK;
}

void *control_get_user_data(control_ctx_h h)
{
    control_ctx_t *ctx = (control_ctx_t *)h;

    return ctx->user_data;
}

event_code_t control_get_event(control_ctx_h h, uint32_t *data)
{
    control_ctx_t *ctx = (control_ctx_t *)h;

    return ctx->get_event(h, data);
}

