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
        player_ctx->state = PLAYER_PLAY;
        clock_gettime(CLOCK_MONOTONIC, &player_ctx->base_time);
    }
    else
    {
        player_ctx->state = PLAYER_PAUSE;
        player_ctx->corrected_pts = player_ctx->last_pts;
    }

    return (player_ctx->state == PLAYER_PAUSE);
}

void video_player_stop(video_player_context *player_ctx)
{
    if (!player_ctx)
        return;

    player_ctx->running = 0;
    /* Waiting for player task */
    if (player_ctx->task)
        pthread_join(player_ctx->task, NULL);

    if (player_ctx->priv)
        free(player_ctx->priv);
}

void *player_main_routine(void *args)
{
    media_buffer_t *buf;
#ifndef CONFIG_RASPBERRY_PI
    int first_pkt = 1;
#endif
    ret_code_t rc;
    video_player_context *player_ctx = (video_player_context *)args;
    //struct timespec start, end;

    DBG_I("Video player task started.\n");

    if (player_ctx->init(player_ctx->priv))
        return NULL;

    player_ctx->running = 1;

    while(player_ctx->running)
    {
        if (!player_ctx->running)
            break;

        if (player_ctx->state == PLAYER_PAUSE)
        {
            usleep(100000);
            continue;
        }

        //clock_gettime(CLOCK_MONOTONIC, &start);
        buf = decode_get_next_video_buffer(player_ctx->demux_ctx, &rc);
        //clock_gettime(CLOCK_MONOTONIC, &end);
        //if (buf)
        //    DBG_I("--- new buff #%d timeout %d\n", buf->number, util_time_sub(&end, &start));
        //else
        //    DBG_I("--- no packet. timeout %d\n", util_time_sub(&end, &start));
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
        if (first_pkt)
        {
            clock_gettime(CLOCK_MONOTONIC, &player_ctx->base_time);
            first_pkt = 0;
        }
        else if (buf->pts_ms != AV_NOPTS_VALUE)
        {
            struct timespec curr_time;
            int diff, pts_ms = (int)buf->pts_ms - player_ctx->corrected_pts;

            clock_gettime(CLOCK_MONOTONIC, &curr_time);
            diff = util_time_sub(&curr_time, &player_ctx->base_time);
            DBG_V("Current PTS=%d time diff=%d\n", pts_ms, diff);
            if (pts_ms > diff)
            {
                diff = pts_ms - diff;
                DBG_V("Going to sleep for %d ms\n", diff);
                usleep(diff * 1000);
            }
            player_ctx->last_pts = buf->pts_ms;
        }
#endif
        //if (buf->pts_ms != AV_NOPTS_VALUE)
        //    fprintf(stderr, "--- new frame pts=%lld\n", buf->pts_ms);
        //else
        //    fprintf(stderr, "--- new frame pts=NOPTS\n");
        player_ctx->draw_frame(player_ctx->priv, buf);

#ifndef CONFIG_RASPBERRY_PI
        decode_release_video_buffer(player_ctx->demux_ctx, buf);
#endif
    }

    player_ctx->running = 0;

    player_ctx->uninit(player_ctx->priv);

    DBG_I("Video player task finished.\n");
    return NULL;
}

