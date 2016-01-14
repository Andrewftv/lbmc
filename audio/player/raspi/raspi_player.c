#include <stdio.h>
#include <pthread.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdint.h>
#include <string.h>

#include "log.h"
#include "decode.h"
#include "audio_player.h"
#include "msleep.h"
#include "ilcore.h"
#include "omxclock.h"
#include "omxaudio_render.h"

typedef struct {
    pthread_t task;

    int pause;
    int running;
    int buff_done;

    msleep_h buffer_done;
    demux_ctx_h demuxer;
    ilcore_comp_h render;
    ilcore_comp_h clock;
    ilcore_tunnel_h clock_tunnel;
} player_ctx_t;

static ret_code_t audio_player_init(player_ctx_t *ctx)
{
    int buff_size, buff_count;

    msleep_init(&ctx->buffer_done);
 
    if (decode_get_audio_buffs_info(ctx->demuxer, &buff_size, &buff_count))
    {
        DBG_E("Can not get audio buffers info\n");
        return L_FAILED;
    }
    DBG_I("Buffers count: %d buffers size: %d\n", buff_count, buff_size);

    ctx->render = create_omxaudio_render(buff_size, buff_count, decode_get_sample_rate(ctx->demuxer),
        decode_get_channels(ctx->demuxer));
    if (!ctx->render)
        return L_FAILED;

    if (ilcore_set_state(ctx->render, OMX_StateIdle) != L_OK)
        return L_FAILED;

    if (omxaudio_render_setup_buffers(ctx->render, ctx->demuxer) != L_OK)
        return L_FAILED;

    ctx->clock = create_omx_clock();
    if (!ctx->clock)
        return L_FAILED;

    /* Create tunnel */
    if (ilcore_create_tunnel(&ctx->clock_tunnel, ctx->clock, IL_CLOCK_PORT1, ctx->render,
        IL_AUDIO_RENDER_CLOCK_PORT) != L_OK)
    {
        return L_FAILED;
    }
    if (ilcore_setup_tunnel(ctx->clock_tunnel) != L_OK)
        return L_FAILED;

    if (ilcore_set_state(ctx->render, OMX_StateExecuting) != L_OK)
        return L_FAILED;

    DBG_I("Audio player successfuly initialized !\n");

    return L_OK;
}

static ret_code_t audio_player_uninit(player_ctx_t *ctx)
{
    ilcore_flush_tunnel(ctx->clock_tunnel);
    ilcore_clean_tunnel(ctx->clock_tunnel);
    ilcore_destroy_tunnel(ctx->clock_tunnel);

    destroy_omx_clock(ctx->clock);
    destroy_omxaudio_render(ctx->render);

    msleep_uninit(ctx->buffer_done);

    return L_OK;
}

static void *player_routine(void *args)
{
    size_t size;
	uint8_t *buf;
    ret_code_t rc;
    int64_t pts;
    int first_frame = 1;
    OMX_ERRORTYPE err;
    OMX_CONFIG_BRCMAUDIODESTINATIONTYPE ar_dest;
    OMX_BUFFERHEADERTYPE *hdr;
    player_ctx_t *ctx = (player_ctx_t *)args;

    DBG_I("Start audio player task\n");

    if (audio_player_init(ctx) < 0)
        return NULL;

    memset(&ar_dest, 0, sizeof(ar_dest));
    ar_dest.nSize = sizeof(OMX_CONFIG_BRCMAUDIODESTINATIONTYPE);
    ar_dest.nVersion.nVersion = OMX_VERSION;
    strcpy((char *)ar_dest.sName, "local");

    err = OMX_SetConfig(ilcore_get_handle(ctx->render), OMX_IndexConfigBrcmAudioDestination, &ar_dest);
    if (err != OMX_ErrorNone)
    {
        DBG_E("OMX_IndexConfigBrcmAudioDestination failed. err=%d\n");
        return NULL;
    }
    DBG_I("Set alalogue audio output\n");

    ctx->running = 1;

    omx_clock_start(ctx->clock, 0);
    omx_clock_set_speed(ctx->clock, OMX_CLOCK_PAUSE_SPEED);
    omx_clock_state_execute(ctx->clock);

    while(ctx->running)
    {
        if (!ctx->running)
			break;

		if (ctx->pause)
		{
			usleep(100000);
			continue;
		}

		buf = decode_get_next_audio_buffer(ctx->demuxer, &size, (void *)&hdr, &pts, &rc);
        if (!buf)
        {
            if (rc != L_STOPPING)
                DBG_E("Nothing to play\n");
            usleep(10000);
            continue;
        }

        if (first_frame)
        {
            omx_clock_set_speed(ctx->clock, OMX_CLOCK_NORMAL_SPEED);
            first_frame = 0;
            hdr->nFlags = OMX_BUFFERFLAG_STARTTIME;
        }

        hdr->pAppPrivate = ctx;
        hdr->nOffset = 0;
        hdr->nFilledLen = size;
        hdr->nTimeStamp = pts;
        hdr->nFlags = OMX_BUFFERFLAG_TIME_UNKNOWN;

        ctx->buff_done = 0;
        err = OMX_EmptyThisBuffer(ilcore_get_handle(ctx->render), hdr);
        if (err != OMX_ErrorNone)
        {
            DBG_E("OMX_EmptyThisBuffer failed\n");
            break;
        
        }
        if (ctx->buff_done)
            continue;

        if (msleep_wait(ctx->buffer_done, 1000) != MSLEEP_INTERRUPT)
        {
            DBG_E("Event buffer done not reseived\n");
            break;
        }
    }

    ctx->running = 0;
    audio_player_uninit(ctx);

    DBG_I("Audio player task finished\n");

    return NULL;
}

ret_code_t audio_player_start(audio_player_h *player_ctx, demux_ctx_h h)
{
    player_ctx_t *ctx;
    ret_code_t rc = L_OK;

    ctx = (player_ctx_t *)malloc(sizeof(player_ctx_t));
    if (!ctx)
    {
        DBG_E("Mamory allocation failed\n");
        return L_FAILED;
    }

    memset(ctx, 0, sizeof(player_ctx_t));
    ctx->demuxer =  h;

    *player_ctx = ctx;

    /* Use default scheduler. Set SCHED_RR or SCHED_FIFO request root access */
	if (pthread_create(&ctx->task, NULL, player_routine, ctx) < 0)
        rc = L_FAILED;

    return rc;
}

void audio_player_set_buffer_done(audio_player_h h)
{
    player_ctx_t *ctx = (player_ctx_t *)h;

    if (ctx)
        ctx->buff_done = 1;
}

demux_ctx_h audio_player_get_demuxer(audio_player_h h)
{
    player_ctx_t *ctx = (player_ctx_t *)h;

    return ctx->demuxer;
}

msleep_h audio_player_get_msleep(audio_player_h h)
{
    player_ctx_t *ctx = (player_ctx_t *)h;

    return ctx->buffer_done;
}

void audio_player_stop(audio_player_h h)
{
    player_ctx_t *ctx = (player_ctx_t *)h;

    if (!ctx)
        return;

    ctx->running = 0;
    /* Waiting for player task */
	pthread_join(ctx->task, NULL);
    
    free(ctx);
}

void audio_player_pause(audio_player_h h)
{
    player_ctx_t *ctx = (player_ctx_t *)h;
    
    ctx->pause = !ctx->pause;
    if (ctx->pause)
        omx_clock_set_speed(ctx->clock, OMX_CLOCK_PAUSE_SPEED);
    else
        omx_clock_set_speed(ctx->clock, OMX_CLOCK_NORMAL_SPEED);
}

int audio_player_is_runnung(audio_player_h h)
{
    player_ctx_t *ctx = (player_ctx_t *)h;

    return ctx->running;
}

