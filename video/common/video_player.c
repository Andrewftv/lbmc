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

int video_player_pause_toggle(video_player_context *player_ctx)
{
    if (!player_ctx)
        return 0;

    if (player_ctx->state == PLAYER_PAUSE)
    {
        struct timespec end_pause;
        uint32_t diff;

        player_ctx->state = PLAYER_PLAY;
        clock_gettime(CLOCK_MONOTONIC, &end_pause);
        diff = util_time_diff(&end_pause, &player_ctx->start_pause);
        util_time_add(&player_ctx->base_time, diff);
    }
    else
    {
        player_ctx->state = PLAYER_PAUSE;
        clock_gettime(CLOCK_MONOTONIC, &player_ctx->start_pause);
    }

    return (player_ctx->state == PLAYER_PAUSE);
}

void video_player_lock(video_player_context *ctx)
{
    if (!ctx)
    {
        DBG_E("Can not lock video player\n");
        return;
    }
    pthread_mutex_lock(&ctx->lock);
}

void video_player_unlock(video_player_context *ctx)
{
    if (!ctx)
    {
        DBG_E("Can not unlock video player\n");
        return;
    }
    pthread_mutex_unlock(&ctx->lock);
}

ret_code_t video_player_seek(video_player_context *ctx, seek_direction_t dir, int32_t seek)
{
    if (dir == L_SEEK_FORWARD)
        util_time_sub(&ctx->base_time, seek);
    else if (dir == L_SEEK_BACKWARD)
        util_time_add(&ctx->base_time, seek);

    return L_OK;
}

void video_player_stop(video_player_context *player_ctx, int stop)
{
    if (!player_ctx)
        return;

    player_ctx->stop = stop;
    player_ctx->running = 0;
#ifndef CONFIG_RASPBERRY_PI
    msleep_wakeup(player_ctx->sched);
#endif
    
    /* Waiting for player task */
    if (player_ctx->task)
        pthread_join(player_ctx->task, NULL);

#ifndef CONFIG_RASPBERRY_PI
    msleep_uninit(player_ctx->sched);
#endif

    if (player_ctx->priv)
        free(player_ctx->priv);
}

void *player_main_routine(void *args)
{
    media_buffer_t *buf;
    ret_code_t rc;
    video_player_context *player_ctx = (video_player_context *)args;

    DBG_I("Video player task started.\n");

    if (player_ctx->init(player_ctx->priv))
        return NULL;

    pthread_mutex_init(&player_ctx->lock, NULL);
    player_ctx->running = 1;
#ifndef CONFIG_RASPBERRY_PI
    player_ctx->first_pkt = 1;
    msleep_init(&player_ctx->sched);
#endif

    while(player_ctx->running)
    {
        if (!player_ctx->running)
            break;

        if (player_ctx->state == PLAYER_PAUSE)
        {
            usleep(100000);
            continue;
        }

        video_player_lock(player_ctx);
        buf = decode_get_next_video_buffer(player_ctx->demux_ctx, &rc);
        video_player_unlock(player_ctx);
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
                if (player_ctx->idle)
                    player_ctx->idle(player_ctx->priv);
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
#ifndef CONFIG_RASPBERRY_PI
        if (player_ctx->first_pkt)
        {
            clock_gettime(CLOCK_MONOTONIC, &player_ctx->base_time);
            player_ctx->first_pkt = 0;
        }
        else if (buf->pts_ms != AV_NOPTS_VALUE)
        {
            struct timespec curr_time;
            int diff;

            clock_gettime(CLOCK_MONOTONIC, &curr_time);
            diff = util_time_diff(&curr_time, &player_ctx->base_time);
            DBG_V("Current PTS=%lld time diff=%d\n", buf->pts_ms, diff);
            if (diff > 0 && buf->pts_ms > diff)
            {
                diff = buf->pts_ms - diff;
                if (diff > 5000)
                {
                    DBG_E("The frame requests %d msec wait. Drop it and continue\n", diff);
                    decode_release_video_buffer(player_ctx->demux_ctx, buf);
                    continue;
                }
                DBG_V("Going to sleep for %d ms\n", diff);
                msleep_wait(player_ctx->sched, diff);
            }
        }
#endif
        player_ctx->draw_frame(player_ctx->priv, buf);

#ifndef CONFIG_RASPBERRY_PI
        decode_release_video_buffer(player_ctx->demux_ctx, buf);
#endif
    }

    player_ctx->running = 0;

    pthread_mutex_destroy(&player_ctx->lock);
    player_ctx->uninit(player_ctx->priv);

    DBG_I("Video player task finished.\n");
    return NULL;
}

