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
#include <unistd.h>
#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include <libavutil/avutil.h>
#include <SDL2/SDL.h>
#include <SDL2/SDL_opengl.h>

#include "log.h"
#include "decode.h"
#include "video_player.h"
#include "timeutils.h"
#include "control.h"

typedef struct {
    video_player_common_ctx_t common;

    int width, height;
    enum AVPixelFormat pix_fmt;

    SDL_Window *window;
    SDL_Renderer *renderer;
    SDL_Texture *texture;

    SDL_Rect vp_rect;
} player_ctx_t;

static event_code_t get_event_callback(control_ctx_h h, uint32_t *data)
{
    player_ctx_t *ctx = (player_ctx_t *)control_get_user_data(h);
    user_event_t *event;
    event_code_t code = L_EVENT_NONE;
    
    event = (user_event_t *)queue_pop(ctx->common.event_queue);
    if (event)
    {
        *data = event->data;
        code = event->code;
        free(event);
    }
    return code;
}

static void uninit_sdl(video_player_h h)
{
    player_ctx_t *ctx = (player_ctx_t *)h;

    msleep_uninit(ctx->common.sched);

    if (ctx->texture)
        SDL_DestroyTexture(ctx->texture);
    if (ctx->renderer)
        SDL_DestroyRenderer(ctx->renderer);
    if (ctx->window)
        SDL_DestroyWindow(ctx->window);

    SDL_Quit();
}

static int init_sdl(video_player_h h)
{
    player_ctx_t *ctx = (player_ctx_t *)h;

    if (SDL_Init(SDL_INIT_EVERYTHING) < 0)
    {
        DBG_E("Unable SDL initialization\n");
        return -1;
    }

    ctx->vp_rect.x = 0;
    ctx->vp_rect.y = 0;
    ctx->vp_rect.w = ctx->width;
    ctx->vp_rect.h = ctx->height;

    control_register_callback(ctx->common.ctrl_ctx, get_event_callback, ctx);

    DBG_I("Create window. size %dx%d\n", ctx->width, ctx->height);
    ctx->window = SDL_CreateWindow("SDL player", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, ctx->width,
        ctx->height, SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE | SDL_WINDOW_OPENGL);
    if (!ctx->window)
    {
        DBG_E("Unable create SDL window\n");
        return -1;
    }
    /* Set "best" scale quality */
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "2");

    ctx->renderer = SDL_CreateRenderer(ctx->window, -1, SDL_RENDERER_ACCELERATED);
    if (!ctx->renderer)
    {
        DBG_E("Unable to create renderer\n");
        return -1;
    }
    SDL_SetRenderDrawColor(ctx->renderer, 0, 0, 0, 0xff);

    ctx->texture = SDL_CreateTexture(ctx->renderer, SDL_PIXELFORMAT_ABGR8888, SDL_TEXTUREACCESS_STREAMING,  ctx->width,
        ctx->height);
    if (!ctx->texture)
    {
        DBG_E("Unable to create texture\n");
        return -1;
    }

    if (decode_setup_video_buffers(ctx->common.demux_ctx, VIDEO_BUFFERS, 1, 80 * 1024) != L_OK)
        return L_FAILED;

    ctx->common.first_pkt = 1;
    msleep_init(&ctx->common.sched);

    decode_start_read(ctx->common.demux_ctx);

    return 0;
}

static void event_sdl(player_ctx_t *ctx, int *is_min)
{
    SDL_Event event;
    double x_scale, y_scale;
    int w, h;
    event_code_t code = L_EVENT_NONE;
    uint32_t data = 0;

    while (SDL_PollEvent(&event))
    {
        switch (event.type)
        {
        case SDL_WINDOWEVENT:
            switch (event.window.event)
            {
            case SDL_WINDOWEVENT_MINIMIZED:
                *is_min = 1;
                break;
            case SDL_WINDOWEVENT_MAXIMIZED:
            case SDL_WINDOWEVENT_RESTORED:
                *is_min = 0;
                break;
            case SDL_WINDOWEVENT_SIZE_CHANGED:
                w = event.window.data1;
                h = event.window.data2;
                x_scale = (double)w / (double)ctx->width;
                y_scale = (double)h / (double)ctx->height;
                if (x_scale > y_scale)
                {
                    ctx->vp_rect.w = y_scale * ctx->width;
                    ctx->vp_rect.h = y_scale * ctx->height;
                    ctx->vp_rect.x = (w - ctx->vp_rect.w) / 2;
                    ctx->vp_rect.y = 0;
                }
                else
                {
                    ctx->vp_rect.w = x_scale * ctx->width;
                    ctx->vp_rect.h = x_scale * ctx->height;
                    ctx->vp_rect.x = 0;
                    ctx->vp_rect.y = (h - ctx->vp_rect.h) / 2;
                }
                break;
            default:
                break;
            }
            break;
        case SDL_KEYDOWN:
            break;
        case SDL_KEYUP:
            switch (event.key.keysym.scancode)
            {
            case SDL_SCANCODE_Q:
                code = L_EVENT_QUIT;
                break;
            case SDL_SCANCODE_SPACE:
                code = L_EVENT_PAUSE;
                break;
            case SDL_SCANCODE_RIGHT:
                code = L_EVENT_SEEK_RIGHT;
                data = 60;
                break;
            case SDL_SCANCODE_LEFT:
                code = L_EVENT_SEEK_LEFT;
                data = 60;
                break;
            case SDL_SCANCODE_A:
                code = L_EVENT_AUDIO_STREEM;
                break;
            case SDL_SCANCODE_M:
                code = L_EVENT_MUTE;
                break;
            case SDL_SCANCODE_I:
                code = L_EVENT_INFO;
                break;
            default:
                break;
            }
            if (code != L_EVENT_NONE)
            {
                user_event_t *event;

                event = (user_event_t *)malloc(sizeof(user_event_t));
                if (event)
                {
                    event->code = code;
                    event->data = data;
                    queue_push(ctx->common.event_queue, (queue_node_t *)event);
                }
                break;
            }
            if (event.key.keysym.scancode == SDL_SCANCODE_F)
                SDL_SetWindowFullscreen(ctx->window, SDL_WINDOW_FULLSCREEN_DESKTOP);
            else if (event.key.keysym.scancode == SDL_SCANCODE_ESCAPE)
                SDL_SetWindowFullscreen(ctx->window, 0);
            break;
        default:
            break;
        }
    }
}

static int draw_frame_sdl(video_player_h h, media_buffer_t *buf)
{
    int win_minimized = 0;
    player_ctx_t *ctx = (player_ctx_t *)h;

    event_sdl(ctx, &win_minimized);
    if (win_minimized)
        return 0;

    SDL_UpdateTexture(ctx->texture, NULL, buf->s.video.buffer[0], ctx->width * 4);
    SDL_RenderClear(ctx->renderer);
    SDL_RenderCopy(ctx->renderer, ctx->texture, NULL, &ctx->vp_rect);
    SDL_RenderPresent(ctx->renderer);

    decode_release_video_buffer(ctx->common.demux_ctx, buf);

    return 0;
}

static ret_code_t seek_sdl(video_player_h h, seek_direction_t dir, int32_t seek)
{
    player_ctx_t *ctx = (player_ctx_t *)h;

    if (dir == L_SEEK_FORWARD)
        util_time_sub(&ctx->common.base_time, seek);
    else if (dir == L_SEEK_BACKWARD)
        util_time_add(&ctx->common.base_time, seek);

    return L_OK;
}

static ret_code_t schedule_sdl(video_player_h h, media_buffer_t *buf)
{
    player_ctx_t *ctx = (player_ctx_t *)h;

    if (ctx->common.first_pkt)
    {
        clock_gettime(CLOCK_MONOTONIC, &ctx->common.base_time);
        ctx->common.first_pkt = 0;
    }
    else if (buf->pts_ms != AV_NOPTS_VALUE)
    {
        struct timespec curr_time;
        int diff;

        clock_gettime(CLOCK_MONOTONIC, &curr_time);
        diff = util_time_diff(&curr_time, &ctx->common.base_time);
        DBG_V("Current PTS=%lld time diff=%d\n", buf->pts_ms, diff);
        if (diff > 0 && buf->pts_ms > diff)
        {
            diff = buf->pts_ms - diff;
            if (diff > 5000)
            {
                DBG_W("The frame requests %d msec wait. Drop it and continue\n", diff);
                decode_release_video_buffer(ctx->common.demux_ctx, buf);
                return L_FAILED;
            }
            DBG_V("Going to sleep for %d ms\n", diff);
            msleep_wait(ctx->common.sched, diff);
        }
    }
    return L_OK;
}

static int pause_toggle_sdl(video_player_h h)
{
    player_ctx_t *ctx = (player_ctx_t *)h;

    if (!ctx)
        return 0;

    if (ctx->common.state == PLAYER_PAUSE)
    {
        struct timespec end_pause;
        uint32_t diff;

        ctx->common.state = PLAYER_PLAY;
        clock_gettime(CLOCK_MONOTONIC, &end_pause);
        diff = util_time_diff(&end_pause, &ctx->common.start_pause);
        util_time_add(&ctx->common.base_time, diff);
    }
    else
    {
        ctx->common.state = PLAYER_PAUSE;
        clock_gettime(CLOCK_MONOTONIC, &ctx->common.start_pause);
    }

    return (ctx->common.state == PLAYER_PAUSE);
}

static void idle_sdl(video_player_h h)
{
    int is_min;
    player_ctx_t *ctx = (player_ctx_t *)h;

    event_sdl(ctx, &is_min);

    SDL_RenderClear(ctx->renderer);
    SDL_RenderCopy(ctx->renderer, ctx->texture, NULL, &ctx->vp_rect);
    SDL_RenderPresent(ctx->renderer);
}

ret_code_t video_player_start(video_player_h *player_ctx, demux_ctx_h h, void *clock)
{
    player_ctx_t *ctx;
    ret_code_t rc = L_OK;
    pthread_attr_t attr;
    struct sched_param param;

    ctx = (player_ctx_t *)malloc(sizeof(player_ctx_t));
    if (!ctx)
    {
        DBG_E("Memory allocation failed\n");
        return L_FAILED;
    }
    memset(ctx, 0, sizeof(player_ctx_t));
    ctx->common.demux_ctx = h;

    if (devode_get_video_size(h, &ctx->width, &ctx->height))
    {
        DBG_E("Can not get video size\n");
        return L_FAILED;
    }

    if (decode_get_pixel_format(h, &ctx->pix_fmt))
    {
        DBG_E("Could not get frame pixel format\n");
        return L_FAILED;
    }

    ctx->common.init = init_sdl;
    ctx->common.uninit = uninit_sdl;
    ctx->common.draw_frame = draw_frame_sdl;
    ctx->common.pause = pause_toggle_sdl;
    ctx->common.seek = seek_sdl;
    ctx->common.schedule = schedule_sdl;
    ctx->common.idle = idle_sdl;

    /* Use default scheduler. Set SCHED_RR or SCHED_FIFO request root access */
    pthread_attr_init(&attr);
    param.sched_priority = 2;
    pthread_attr_setschedparam(&attr, &param);
    if (pthread_create(&ctx->common.task, &attr, player_main_routine, ctx))
    {
        DBG_E("Create thread falled\n");
        rc = L_FAILED;
    }
    pthread_attr_destroy(&attr);

    *player_ctx = ctx;

    return rc;
}
