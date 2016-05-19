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

#define PAUSE_SLEEP_US          (100 * 1000)
#define BUFF_DONE_TIMEOUT_MS    1000
#define VOLUME_MINIMUM          -6000  /* -60dB */

typedef struct {
    pthread_t task;

    int pause;
    int running;
    long volume;
    int mute;

    demux_ctx_h demuxer;
    ilcore_comp_h render;
    ilcore_comp_h clock;
    ilcore_tunnel_h clock_tunnel;
} player_ctx_t;

OMX_ERRORTYPE audio_play_buffer_done(OMX_HANDLETYPE hComponent, OMX_PTR pAppData, OMX_BUFFERHEADERTYPE* pBuffer)
{
    media_buffer_t *buf = (media_buffer_t *)pBuffer->pAppPrivate;
    player_ctx_t *ctx = (player_ctx_t *)ilcore_get_app_data(pAppData);
    
    decode_release_audio_buffer(ctx->demuxer, buf);

    return OMX_ErrorNone;
}

static ret_code_t audio_player_init(player_ctx_t *ctx)
{
    int buff_size, buff_count;

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

    ilcore_set_app_data(ctx->render, ctx);

    if (ilcore_set_state(ctx->render, OMX_StateIdle) != L_OK)
        return L_FAILED;

    if (omxaudio_render_setup_buffers(ctx->render, ctx->demuxer) != L_OK)
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

    destroy_omxaudio_render(ctx->render);

    return L_OK;
}

static void *player_routine(void *args)
{
    media_buffer_t *buf;
    ret_code_t rc;
    int first_frame = 1;
    OMX_ERRORTYPE err;
    OMX_CONFIG_BRCMAUDIODESTINATIONTYPE ar_dest;
    OMX_BUFFERHEADERTYPE *hdr;
    player_ctx_t *ctx = (player_ctx_t *)args;

    DBG_I("Start audio player task\n");

    if (audio_player_init(ctx) < 0)
        return NULL;

    OMX_INIT_STRUCT(ar_dest);
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
            usleep(PAUSE_SLEEP_US);
            continue;
        }

        buf = decode_get_next_audio_buffer(ctx->demuxer, &rc);
        if (!buf)
        {
            if (rc != L_STOPPING)
                DBG_I("Nothing to play\n");
            usleep(10000);
            continue;
        }

        hdr = (OMX_BUFFERHEADERTYPE *)buf->app_data;
        hdr->nFlags = 0;
        if (first_frame)
        {
            omx_clock_set_speed(ctx->clock, OMX_CLOCK_NORMAL_SPEED);
            first_frame = 0;
            hdr->nFlags = OMX_BUFFERFLAG_STARTTIME;
        }

        DBG_V("Audio packet. size=%d pts=%lld dts=%lld\n", buf->size, buf->pts_ms, buf->dts_ms);
        hdr->pAppPrivate = buf;
        hdr->nOffset = 0;
        hdr->nFilledLen = buf->size;
        if (buf->pts_ms == AV_NOPTS_VALUE && buf->dts_ms == AV_NOPTS_VALUE)
        {
            hdr->nFlags |= OMX_BUFFERFLAG_TIME_UNKNOWN;
            hdr->nTimeStamp = to_omx_time(0);
        }
        else if (buf->pts_ms != AV_NOPTS_VALUE)
        {
            hdr->nTimeStamp = to_omx_time(1000 * buf->pts_ms);
        }
        else
        {
            hdr->nTimeStamp = to_omx_time(1000 * buf->dts_ms);
        }
        hdr->nFlags |= OMX_BUFFERFLAG_ENDOFFRAME;

        err = OMX_EmptyThisBuffer(ilcore_get_handle(ctx->render), hdr);
        if (err != OMX_ErrorNone)
        {
            DBG_E("OMX_EmptyThisBuffer failed. err=0x%08x\n", err);
            break;
        
        }
    }

    ctx->running = 0;
    audio_player_uninit(ctx);

    DBG_I("Audio player task finished\n");

    return NULL;
}

ret_code_t audio_player_start(audio_player_h *player_ctx, demux_ctx_h h, ilcore_comp_h clock)
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
    ctx->clock = clock;
    ctx->volume = -1; /* Uninited */

    *player_ctx = ctx;

    /* Use default scheduler. Set SCHED_RR or SCHED_FIFO request root access */
    if (pthread_create(&ctx->task, NULL, player_routine, ctx) < 0)
        rc = L_FAILED;

    return rc;
}

demux_ctx_h audio_player_get_demuxer(audio_player_h h)
{
    player_ctx_t *ctx = (player_ctx_t *)h;

    return ctx->demuxer;
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

ret_code_t audio_player_mute(audio_player_h h)
{
    player_ctx_t *ctx = (player_ctx_t *)h;
    long set_volume;
    OMX_AUDIO_CONFIG_VOLUMETYPE volume;
    ret_code_t rc;

    OMX_INIT_STRUCT(volume);
    volume.nPortIndex = IL_AUDIO_RENDER_IN_PORT;

    if (!ctx->mute)
    {
        rc = ilcore_get_config(ctx->render, OMX_IndexConfigAudioVolume, &volume);
        if (rc != L_OK)
        {
            DBG_E("Unable to get current volume\n");
            return rc;
        }
        ctx->volume = volume.sVolume.nValue;
        set_volume = VOLUME_MINIMUM;
        
        DBG_I("Mute audio. Prevision volume is %d dB\n", ctx->volume / 100);
    }
    else
    {
        set_volume = ctx->volume;

        DBG_I("Restore audio. Current volume is %d dB\n", ctx->volume / 100);
    }

    volume.sVolume.nValue = set_volume;
    rc = ilcore_set_config(ctx->render, OMX_IndexConfigAudioVolume, &volume);
    if (rc != L_OK)
    {
        DBG_E("Unable to audio volume\n");
        return rc;
    }

    ctx->mute = !ctx->mute;

    return rc;
}

int audio_player_is_runnung(audio_player_h h)
{
    player_ctx_t *ctx = (player_ctx_t *)h;

    return ctx->running;
}

