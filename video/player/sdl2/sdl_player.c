#include <unistd.h>
#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include <libavutil/avutil.h>
#include <SDL2/SDL.h>

#include "log.h"
#include "decode.h"
#include "video_player.h"
#include "timeutils.h"

typedef struct {
    int width, height;
    enum AVPixelFormat pix_fmt;

    SDL_Window *window;
    SDL_Renderer *renderer;
    SDL_Texture *texture;

    demux_ctx_h demux;
} player_ctx_t;

static void uninit_sdl(video_player_h h)
{
    player_ctx_t *ctx = (player_ctx_t *)h;

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
    DBG_I("Create window. size %dx%d\n", ctx->width, ctx->height);
    ctx->window = SDL_CreateWindow("SDL player", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, ctx->width,
        ctx->height, SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE);
    if (!ctx->window)
    {
        DBG_E("Unable create SDL window\n");
        return -1;
    }

    ctx->renderer = SDL_CreateRenderer(ctx->window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC );
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

    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "linear");

    decode_start_read(ctx->demux);

    return 0;
}

static void event_sdl(player_ctx_t *ctx, SDL_Rect *rect, int *is_min)
{
    SDL_Event event;

    while (SDL_PollEvent(&event))
    {
        double x_scale, y_scale;

        if(event.type != SDL_WINDOWEVENT)
            continue;

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
            x_scale = (double)event.window.data1 / (double)ctx->width;
            y_scale = (double)event.window.data2 / (double)ctx->height;
            if (x_scale > y_scale)
            {
                rect->w = y_scale * ctx->width;
                rect->h = y_scale * ctx->height;
                rect->x = (event.window.data1 - rect->w) / 2;
                rect->y = 0;
            }
            else
            {
                rect->w = x_scale * ctx->width;
                rect->h = x_scale * ctx->height;
                rect->x = 0;
                rect->y = (event.window.data2 - rect->h) / 2;
            }
            break;
        default:
            break;
        }
    }
}

static int draw_frame_sdl(video_player_h h, video_buffer_t *buf)
{
    SDL_Rect dst_rect;
    int win_minimized = 0;
    player_ctx_t *ctx = (player_ctx_t *)h;

    dst_rect.x = 0;
    dst_rect.y = 0;
    dst_rect.w = ctx->width;
    dst_rect.h = ctx->height;

    event_sdl(ctx, &dst_rect, &win_minimized);
    if (win_minimized)
        return 0;

    SDL_UpdateTexture(ctx->texture, NULL, buf->buffer[0], ctx->width * 4);
    SDL_RenderClear(ctx->renderer);
    SDL_RenderCopy(ctx->renderer, ctx->texture, NULL, &dst_rect);
    SDL_RenderPresent(ctx->renderer);

    return 0;
}

ret_code_t video_player_start(video_player_context *player_ctx, demux_ctx_h h, void *clock)
{
    player_ctx_t *ctx;
    ret_code_t rc = L_OK;
    pthread_attr_t attr;
    struct sched_param param;

    memset(player_ctx, 0, sizeof(video_player_context));
    player_ctx->demux_ctx = h;

    ctx = (player_ctx_t *)malloc(sizeof(player_ctx_t));
    if (!ctx)
    {
        DBG_E("Memory allocation failed\n");
        return L_FAILED;
    }
    memset(ctx, 0, sizeof(player_ctx_t));
    ctx->demux = h;
    player_ctx->priv = ctx;

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

    player_ctx->init = init_sdl;
    player_ctx->uninit = uninit_sdl;
    player_ctx->draw_frame = draw_frame_sdl;

    /* Use default scheduler. Set SCHED_RR or SCHED_FIFO request root access */
    pthread_attr_init(&attr);
    param.sched_priority = 2;
    pthread_attr_setschedparam(&attr, &param);
    if (pthread_create(&player_ctx->task, &attr, player_main_routine, player_ctx))
    {
        DBG_E("Create thread falled\n");
        rc = L_FAILED;
    }
    pthread_attr_destroy(&attr);

    return rc;
}

