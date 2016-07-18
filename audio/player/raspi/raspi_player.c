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
    int eos;
    int stop;

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
    OMX_ERRORTYPE err;
    OMX_CALLBACKTYPE cb;
    OMX_AUDIO_PARAM_PCMMODETYPE pcm;
    int buff_size, buff_count, buff_align;

    cb.EventHandler = il_event_handler;
    cb.EmptyBufferDone = audio_play_buffer_done;
    cb.FillBufferDone = il_fill_buffer_done;

    if (ilcore_init_comp(&ctx->render, &cb, "OMX.broadcom.audio_render"))
        return L_FAILED;

    if (ilcore_disable_all_ports(ctx->render))
        return L_FAILED;

    if (decode_setup_audio_buffers(ctx->demuxer, AUDIO_BUFFERS, AUDIO_BUFF_ALIGN, AUDIO_BUFF_SIZE))
        return L_FAILED;

    if (decode_get_audio_buffs_info(ctx->demuxer, &buff_size, &buff_count, &buff_align))
    {
        DBG_E("Can not get audio buffers info\n");
        return L_FAILED;
    }
    DBG_I("Buffers count: %d buffers size: %d alignment: %d\n", buff_count, buff_size, buff_align);

    memset(&pcm, 0, sizeof(OMX_AUDIO_PARAM_PCMMODETYPE));
    pcm.nSize = sizeof(OMX_AUDIO_PARAM_PCMMODETYPE);
    pcm.nVersion.nVersion = OMX_VERSION;
    pcm.nPortIndex = IL_AUDIO_RENDER_IN_PORT;
    pcm.nChannels = decode_get_channels(ctx->demuxer);
    pcm.eNumData = OMX_NumericalDataSigned;
    pcm.eEndian = OMX_EndianLittle;
    pcm.nSamplingRate = decode_get_sample_rate(ctx->demuxer);
    pcm.bInterleaved = OMX_TRUE;
    pcm.nBitPerSample = 16;
    pcm.ePCMMode = OMX_AUDIO_PCMModeLinear;

    pcm.eChannelMapping[1] = OMX_AUDIO_ChannelRF;
    pcm.eChannelMapping[0] = OMX_AUDIO_ChannelLF;

    err = OMX_SetParameter(ilcore_get_handle(ctx->render), OMX_IndexParamAudioPcm, &pcm);
    if (err != OMX_ErrorNone)
    {
        DBG_E("OMX_IndexParamAudioPcm failed. err=%d\n");
        return L_FAILED;
    }
    ilcore_set_app_data(ctx->render, ctx);

    if (ilcore_set_port_buffers_param(ctx->render, buff_size, buff_count, buff_align))
        return L_FAILED;

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

    if (!decode_is_video(ctx->demuxer))
        decode_start_read(ctx->demuxer);

    DBG_I("Audio player successfuly initialized !\n");

    return L_OK;
}

static ret_code_t audio_player_uninit(player_ctx_t *ctx)
{
    if (ctx->render)
        omxaudio_render_release_buffers(ctx->render, ctx->demuxer);

    ilcore_flush_tunnel(ctx->clock_tunnel);
    ilcore_clean_tunnel(ctx->clock_tunnel);
    ilcore_destroy_tunnel(ctx->clock_tunnel);

    if (ctx->render)
        ilcore_uninit_comp(ctx->render);

    return L_OK;
}

static void eof_callback(void *context)
{
    player_ctx_t *ctx = (player_ctx_t *)context;

    ctx->eos = 1;
}

static void wait_complition(player_ctx_t *ctx)
{
    media_buffer_t *buf;
    OMX_BUFFERHEADERTYPE *hdr;
    OMX_ERRORTYPE err;

    if (ctx->stop)
        return; /* Force stop player by user */

    buf = decode_get_free_audio_buffer(ctx->demuxer);
    if (!buf)
    {
        DBG_E("Unable to get free audio buffer\n");
        return;
    }
    hdr = (OMX_BUFFERHEADERTYPE *)buf->app_data;
    hdr->pAppPrivate = buf;
    hdr->nOffset = 0;
    hdr->nFilledLen = 0;
    hdr->nTimeStamp = to_omx_time(0);
    hdr->nFlags = OMX_BUFFERFLAG_ENDOFFRAME | OMX_BUFFERFLAG_EOS | OMX_BUFFERFLAG_TIME_UNKNOWN;

    err = OMX_EmptyThisBuffer(ilcore_get_handle(ctx->render), hdr);
    if (err != OMX_ErrorNone)
    {
        DBG_E("OMX_EmptyThisBuffer failed. err=0x%08x\n", err);
        return;
    }
    DBG_I("Waiting for end of stream\n");
    while (!ctx->eos)
    {
        usleep(100000);
    }
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

    ilcore_set_eos_callback(ctx->render, eof_callback, ctx);

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

        decode_set_current_playing_pts(ctx->demuxer, buf->pts_ms);

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

    wait_complition(ctx);

    audio_player_uninit(ctx);
    ctx->running = 0;

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

void audio_player_stop(audio_player_h h, int stop)
{
    player_ctx_t *ctx = (player_ctx_t *)h;

    if (!ctx)
        return;

    ctx->running = 0;
    ctx->stop = stop;
    /* Waiting for player task */
    pthread_join(ctx->task, NULL);
    
    free(ctx);
}

int audio_player_pause_toggle(audio_player_h h)
{
    player_ctx_t *ctx = (player_ctx_t *)h;
    
    ctx->pause = !ctx->pause;
    if (ctx->pause)
        omx_clock_set_speed(ctx->clock, OMX_CLOCK_PAUSE_SPEED);
    else
        omx_clock_set_speed(ctx->clock, OMX_CLOCK_NORMAL_SPEED);

    return ctx->pause;
}

ret_code_t audio_player_mute_toggle(audio_player_h h, int *is_muted)
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
    if (is_muted)
        *is_muted = ctx->mute;

    return rc;
}

int audio_player_is_runnung(audio_player_h h)
{
    player_ctx_t *ctx = (player_ctx_t *)h;

    return ctx->running;
}
