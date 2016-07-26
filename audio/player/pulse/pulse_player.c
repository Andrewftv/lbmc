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
/*
 * Implementation of audio player over pulseaudio.
 */

#include <stdio.h>
#include <pthread.h>
#include <stdlib.h>
#include <unistd.h>

#include <libavutil/samplefmt.h>

#include <pulse/simple.h>
#include <pulse/error.h>

#include "log.h"
#include "decode.h"
#include "audio_player.h"
#include "timeutils.h"
#include "msleep.h"

typedef struct {
    pthread_t task;

    int pause;
    int running;
    int first_pkt;

    msleep_h sched;
    pthread_mutex_t lock;

    struct timespec base_time;
    struct timespec start_pause;

    demux_ctx_h audio_ctx;
} pulse_player_ctx_t;

static int init_context(pulse_player_ctx_t *ctx)
{
    memset(ctx, 0, sizeof(pulse_player_ctx_t));

    pthread_mutex_init(&ctx->lock, NULL);
    ctx->first_pkt = 1;
    msleep_init(&ctx->sched);

    return 0;
}

static void uninit_context(pulse_player_ctx_t *ctx)
{
    msleep_uninit(ctx->sched);
    pthread_mutex_destroy(&ctx->lock);
}

static pa_sample_format_t av2pa(enum AVSampleFormat src_fmt)
{
    pa_sample_format_t dst_fmt = PA_SAMPLE_INVALID;

    switch(src_fmt)
    {
    case AV_SAMPLE_FMT_U8:
        dst_fmt = PA_SAMPLE_U8;
        break;
    case AV_SAMPLE_FMT_S16:
        dst_fmt = PA_SAMPLE_S16LE;
        break;
    case AV_SAMPLE_FMT_S32:
        dst_fmt = PA_SAMPLE_S32LE;
        break;
    case AV_SAMPLE_FMT_FLT:
        dst_fmt = PA_SAMPLE_FLOAT32LE;
        break;
    case AV_SAMPLE_FMT_DBL:
        dst_fmt = PA_SAMPLE_FLOAT32LE; /* ? */
        break;
    default:
        break;
    }

    return dst_fmt;
}

static void *player_routine(void *args)
{
    pulse_player_ctx_t *ctx = (pulse_player_ctx_t *)args;
    pa_sample_spec ss;
    pa_simple *s = NULL;
    int error;
    media_buffer_t *buf;
    enum AVSampleFormat fmt;
    ret_code_t rc;

    ss.rate = decode_get_sample_rate(ctx->audio_ctx);
    ss.channels = decode_get_channels(ctx->audio_ctx);
    fmt = decode_get_sample_format(ctx->audio_ctx);
    ss.format = av2pa(fmt);

    if (decode_setup_audio_buffers(ctx->audio_ctx, AUDIO_BUFFERS, AUDIO_BUFF_ALIGN, AUDIO_BUFF_SIZE))
        return NULL;

    DBG_I("Open pulse audio. format: %s rate: %d channels: %d\n", pa_sample_format_to_string(ss.format), ss.rate,
        ss.channels);

    if (!(s = pa_simple_new(NULL, "LBMC", PA_STREAM_PLAYBACK, NULL, "playback", &ss, NULL, NULL, &error)))
    {
        DBG_E("pa_simple_new() failed: %s\n", pa_strerror(error));
        goto finish;
    }

    if (!decode_is_video(ctx->audio_ctx))
        decode_start_read(ctx->audio_ctx);

    DBG_I("Audio player task started\n");
    ctx->running = 1;

    while(ctx->running)
    {
        if (!ctx->running)
            break;

        if (ctx->pause)
        {
            usleep(100000);
            continue;
        }

        audio_player_lock(ctx);
        buf = decode_get_next_audio_buffer(ctx->audio_ctx, &rc);
        audio_player_unlock(ctx);
        if (!buf)
        {
            if (rc != L_STOPPING)
                DBG_E("Nothing to play\n");
            usleep(10000);
            continue;
        }

        decode_set_current_playing_pts(ctx->audio_ctx, buf->pts_ms);

        if (ctx->first_pkt)
        {
            clock_gettime(CLOCK_MONOTONIC, &ctx->base_time);
            ctx->first_pkt = 0;
        }
        else if (buf->pts_ms != AV_NOPTS_VALUE)
        {
            struct timespec curr_time;
            int diff;

            clock_gettime(CLOCK_MONOTONIC, &curr_time);
            diff = util_time_diff(&curr_time, &ctx->base_time);
            DBG_V("Current PTS=%lld time diff=%d\n", buf->pts_ms, diff);
            if (diff > 0 && buf->pts_ms > diff)
            {
                diff = buf->pts_ms - diff;
                if (diff > 5000)
                {
                    DBG_E("The frame requests %d msec wait. Drop it and continue\n", diff);
                    decode_release_audio_buffer(ctx->audio_ctx, buf);
                    continue;
                }
                DBG_V("Going to sleep for %d ms\n", diff);
                msleep_wait(ctx->sched, diff);
            }
            else if (diff > buf->pts_ms + 30)
            {
                DBG_V("Drop this packet\n");
                goto Drop;
            }
        }

        if (pa_simple_write(s, buf->s.audio.data[0], buf->size, &error) < 0) 
        {
            DBG_E("pa_simple_write() failed: %s\n", pa_strerror(error));
            break;
        }
Drop:
        decode_release_audio_buffer(ctx->audio_ctx, buf);
    }

finish:

    if (pa_simple_drain(s, &error) < 0)
        DBG_E("pa_simple_drain() failed: %s\n", pa_strerror(error));
    pa_simple_free(s);

    ctx->running = 0;
    DBG_I("Player task finished\n");
    return NULL;
}

void audio_player_lock(audio_player_h h)
{
    pulse_player_ctx_t *ctx = (pulse_player_ctx_t *)h;

    if (!ctx)
    {
        DBG_E("Can not lock audio player\n");
        return;
    }
    pthread_mutex_lock(&ctx->lock);
}

void audio_player_unlock(audio_player_h h)
{
    pulse_player_ctx_t *ctx = (pulse_player_ctx_t *)h;

    if (!ctx)
    {
        DBG_E("Can not unlock audio player\n");
        return;
    }
    pthread_mutex_unlock(&ctx->lock);
}

ret_code_t audio_player_seek(audio_player_h h, seek_direction_t dir, int32_t seek)
{
    pulse_player_ctx_t *ctx = (pulse_player_ctx_t *)h;

    if (dir == L_SEEK_FORWARD)
        util_time_sub(&ctx->base_time, seek);
    else if (dir == L_SEEK_BACKWARD)
        util_time_add(&ctx->base_time, seek);

    return L_OK;
}

int audio_player_is_runnung(audio_player_h h)
{
    pulse_player_ctx_t *ctx = (pulse_player_ctx_t *)h;

    return ctx->running;
}

int audio_player_pause_toggle(audio_player_h player_ctx)
{
    pulse_player_ctx_t *ctx = (pulse_player_ctx_t *)player_ctx;

    if (ctx->pause)
    {
        struct timespec end_pause;
        uint32_t diff;

        clock_gettime(CLOCK_MONOTONIC, &end_pause);
        diff = util_time_diff(&end_pause, &ctx->start_pause);
        util_time_add(&ctx->base_time, diff);
    }
    else
    {
        clock_gettime(CLOCK_MONOTONIC, &ctx->start_pause);
    }
    ctx->pause = !ctx->pause;

    return ctx->pause;
}

ret_code_t audio_player_mute_toggle(audio_player_h player_ctx, int *is_muted)
{
    *is_muted = 0;

    return L_OK;
}

ret_code_t audio_player_start(audio_player_h *player_ctx, demux_ctx_h h, void *clock)
{
    pulse_player_ctx_t *ctx;
    ret_code_t rc = L_OK;

    ctx = (pulse_player_ctx_t *)malloc(sizeof(pulse_player_ctx_t));
    if (!ctx)
    {
        DBG_E("Memory allocation failed\n");
        return L_FAILED;
    }

    init_context(ctx);
    ctx->audio_ctx = h;

    *player_ctx = ctx;
    /* Use default scheduler. Set SCHED_RR or SCHED_FIFO request root access */
    if (pthread_create(&ctx->task, NULL, player_routine, ctx) < 0)
        rc = L_FAILED;

    return rc;
}

void audio_player_stop(audio_player_h player_ctx, int stop)
{
    pulse_player_ctx_t *ctx = (pulse_player_ctx_t *)player_ctx;

    if (!ctx)
        return;

    ctx->running = 0;
    msleep_wakeup(ctx->sched);
    /* Waiting for player task */
    pthread_join(ctx->task, NULL);

    uninit_context(ctx);

    free(ctx);
}
