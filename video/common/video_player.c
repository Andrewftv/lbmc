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
#include <stdint.h>
#include <pthread.h>
#include <unistd.h>
#include <stdlib.h>

#include "log.h"
#include "decode.h"
#include "video_player.h"
#include "timeutils.h"

#include <libavutil/avutil.h>

int video_player_pause_toggle(video_player_h h)
{
    video_player_common_ctx_t *ctx = (video_player_common_ctx_t *)h;

    if (!ctx || !ctx->pause)
        return 0;

    return ctx->pause(h);
}

void video_player_lock(video_player_h h)
{
    video_player_common_ctx_t *ctx = (video_player_common_ctx_t *)h;

    if (!ctx)
    {
        DBG_E("Can not lock video player\n");
        return;
    }
    pthread_mutex_lock(&ctx->lock);
}

void video_player_unlock(video_player_h h)
{
    video_player_common_ctx_t *ctx = (video_player_common_ctx_t *)h;

    if (!ctx)
    {
        DBG_E("Can not unlock video player\n");
        return;
    }
    pthread_mutex_unlock(&ctx->lock);
}

ret_code_t video_player_seek(video_player_h h, seek_direction_t dir, int32_t seek)
{
    video_player_common_ctx_t *ctx = (video_player_common_ctx_t *)h;

    if (!ctx || !ctx->seek)
        return L_FAILED;

    return ctx->seek(h, dir, seek);
}

void video_player_stop(video_player_h h, int stop)
{
    video_player_common_ctx_t *ctx = (video_player_common_ctx_t *)h;

    if (!ctx)
        return;

    ctx->stop = stop;
    ctx->running = 0;
#ifndef CONFIG_RASPBERRY_PI
    if (ctx->running)
        msleep_wakeup(ctx->sched);
#endif
    
    /* Waiting for player task */
    if (ctx->task)
        pthread_join(ctx->task, NULL);

    free(ctx);
}

void *player_main_routine(void *args)
{
    media_buffer_t *buf;
    ret_code_t rc;
    video_player_common_ctx_t *ctx = (video_player_common_ctx_t *)args;

    DBG_I("Video player task started.\n");

    if (ctx->init(ctx))
        return NULL;

    pthread_mutex_init(&ctx->lock, NULL);
    ctx->running = 1;

    while(ctx->running)
    {
        if (!ctx->running)
            break;

        if (ctx->state == PLAYER_PAUSE)
        {
            usleep(100000);
            continue;
        }

        video_player_lock(ctx);
        buf = decode_get_next_video_buffer(ctx->demux_ctx, &rc);
        video_player_unlock(ctx);
        if (!buf)
        {
            if (rc == L_FAILED)
            {
                DBG_E("Can not get next a video buffer\n");
                break;
            }
            else if (rc == L_TIMEOUT)
            {
                DBG_I("Nothing to play\n");
                if (ctx->idle)
                    ctx->idle(ctx);
                continue;
            }
            else if (rc == L_STOPPING)
            {
                break;
            }
            else if (rc == L_OK)
            {
                DBG_E("Incorrect state\n");
                break;
            }
        }
        rc = L_OK;
        if (ctx->schedule)
            rc = ctx->schedule(ctx, buf);
        if (rc == L_OK)
            ctx->draw_frame(ctx, buf);
    }

    ctx->running = 0;

    pthread_mutex_destroy(&ctx->lock);
    ctx->uninit(ctx);

    DBG_I("Video player task finished.\n");
    return NULL;
}

