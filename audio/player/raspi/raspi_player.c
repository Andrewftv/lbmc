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

typedef struct {
    pthread_t task;

    int pause;
    int running;

    msleep_h buffer_done;
    demux_ctx_h demuxer;
    ilcore_comp_h render;
} player_ctx_t;

static int buff_done = 0;

static OMX_ERRORTYPE il_event_handler(OMX_HANDLETYPE hComponent, OMX_PTR pAppData, OMX_EVENTTYPE eEvent, 
    OMX_U32 nData1, OMX_U32 nData2, OMX_PTR pEventData)
{
    ilcore_add_comp_event(pAppData, eEvent, nData1, nData2);

    return OMX_ErrorNone;
}

static OMX_ERRORTYPE il_empty_buffer_done(OMX_HANDLETYPE hComponent, OMX_PTR pAppData, 
    OMX_BUFFERHEADERTYPE* pBuffer)
{
    player_ctx_t *ctx = (player_ctx_t *)pBuffer->pAppPrivate;

    buff_done = 1;
    decode_release_audio_buffer(ctx->demuxer);
    msleep_wakeup(ctx->buffer_done);

    return OMX_ErrorNone;
}

static OMX_ERRORTYPE il_fill_buffer_done(OMX_HANDLETYPE hComponent, OMX_PTR pAppData,
      OMX_BUFFERHEADERTYPE* pBuffer)
{
    DBG_I("Fill buffer done\n");

    return OMX_ErrorNone;
}

static int audio_player_init(player_ctx_t *ctx)
{
    OMX_ERRORTYPE err;
    int buff_size, buff_count;
    OMX_PARAM_PORTDEFINITIONTYPE portdef;
    OMX_STATETYPE state;
    OMX_AUDIO_PARAM_PCMMODETYPE pcm;
    OMX_CALLBACKTYPE cb;
    int i;

    msleep_init(&ctx->buffer_done);
 
    cb.EventHandler = il_event_handler;
    cb.EmptyBufferDone = il_empty_buffer_done;
    cb.FillBufferDone = il_fill_buffer_done;
 
    if (ilcore_init_comp(&ctx->render, &cb, "OMX.broadcom.audio_render"))
        return -1;
 
    if (ilcore_disable_all_ports(ctx->render))
        return -1;

    if (decode_get_audio_buffs_info(ctx->demuxer, &buff_size, &buff_count))
    {
        DBG_E("Can not get audio buffers info\n");
        return -1;
    }
    DBG_I("Buffers count: %d buffers size: %d\n", buff_count, buff_size);

    if (ilcore_set_port_buffers_param(ctx->render, buff_size, buff_count))
        return -1;

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
        return -1;
    }

    if (ilcore_set_state(ctx->render, OMX_StateIdle))
        return -1;

    memset(&portdef, 0, sizeof(OMX_PARAM_PORTDEFINITIONTYPE));
    portdef.nSize = sizeof(OMX_PARAM_PORTDEFINITIONTYPE);
    portdef.nVersion.nVersion = OMX_VERSION;
    portdef.nPortIndex = IL_AUDIO_RENDER_IN_PORT;
   
    err = OMX_GetParameter(ilcore_get_handle(ctx->render), OMX_IndexParamPortDefinition, &portdef);
    if(err != OMX_ErrorNone || portdef.bEnabled != OMX_FALSE || portdef.nBufferCountActual == 0 ||
        portdef.nBufferSize == 0)
    {
        DBG_E("Incorrect port defenitions: err=%d enabled=%d buffers count=%d buffers size=%d",
            err, portdef.bEnabled, portdef.nBufferCountActual, portdef.nBufferSize);
        return -1;
    }
    DBG_I("buffers count=%d buffers size=%d alligment=%d\n", portdef.nBufferCountActual, portdef.nBufferSize,
        portdef.nBufferAlignment);

    if (ilcore_get_state(ctx->render, &state))
        return -1;

    if (state != OMX_StateIdle)
    {
        DBG_E("Incorrect state: err=%d state=%d\n", err, state);
        return -1;
    }

    err = OMX_SendCommand(ilcore_get_handle(ctx->render), OMX_CommandPortEnable, IL_AUDIO_RENDER_IN_PORT, NULL);
    if (err != OMX_ErrorNone)
    {
        DBG_E("Port enabling is failed. err=%d\n");
        return -1;
    }

    for (i = 0; i < portdef.nBufferCountActual; i++)
    {
        uint8_t *buf;
        OMX_BUFFERHEADERTYPE *buf_hdr;

        buf = decode_get_audio_buffer_by_index(ctx->demuxer, i);
        if (!buf)
        {
            DBG_E("Can not get demuxer buffer #%d\n", i);
            return -1;
        }

        err = OMX_UseBuffer(ilcore_get_handle(ctx->render), &buf_hdr, IL_AUDIO_RENDER_IN_PORT, NULL,
            portdef.nBufferSize, buf);
        if (err != OMX_ErrorNone)
        {
            DBG_E("OMX_UseBuffer failed. err=%d index=%d\n", err, i);
            return -1;
        }

        decode_set_audio_buffer_priv_data(ctx->demuxer, i, buf_hdr);
    }

    err = omx_core_comp_wait_command(ctx->render, OMX_CommandPortEnable, IL_AUDIO_RENDER_IN_PORT, 100);
    if (err != OMX_ErrorNone)
    {
        DBG_E("Wait event failed. err=%d\n");
        return -1;
    }

    if (ilcore_set_state(ctx->render, OMX_StateExecuting))
        return -1;

    DBG_I("Audio player successfuly initialized !\n");

    return 0;
}

static int audio_player_uninit(player_ctx_t *ctx)
{
    msleep_uninit(ctx->buffer_done);

    return 0;
}

static void *player_routine(void *args)
{
    size_t size;
	uint8_t *buf;
    ret_code_t rc;
    OMX_ERRORTYPE err;
    OMX_CONFIG_BRCMAUDIODESTINATIONTYPE ar_dest;
    OMX_BUFFERHEADERTYPE *hdr;
    player_ctx_t *ctx = (player_ctx_t *)args;

    DBG_I("Start audio player task\n");

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

    while(ctx->running)
    {
        if (!ctx->running)
			break;

		if (ctx->pause)
		{
			usleep(100000);
			continue;
		}

		buf = decode_get_next_audio_buffer(ctx->demuxer, &size, (void *)&hdr, &rc);
        if (!buf)
        {
            if (rc != L_STOPPING)
                DBG_E("Nothing to play\n");
            usleep(10000);
            continue;
        }

        hdr->pAppPrivate = ctx;
        hdr->nOffset = 0;
        hdr->nFilledLen = size;

        buff_done = 0;
        err = OMX_EmptyThisBuffer(ilcore_get_handle(ctx->render), hdr);
        if (err != OMX_ErrorNone)
        {
            DBG_E("OMX_EmptyThisBuffer failed\n");
            break;
        
        }
        if (buff_done)
            continue;

        if (msleep_wait(ctx->buffer_done, 1000) != MSLEEP_INTERRUPT)
        {
            DBG_E("Event buffer done not reseived\n");
            break;
        }
    }

    ctx->running = 0;

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

    if (audio_player_init(ctx) < 0)
        return L_FAILED;

    /* Use default scheduler. Set SCHED_RR or SCHED_FIFO request root access */
	if (pthread_create(&ctx->task, NULL, player_routine, ctx) < 0)
        rc = L_FAILED;

    return rc;
}

void audio_player_stop(audio_player_h h)
{
    player_ctx_t *ctx = (player_ctx_t *)h;

    if (!ctx)
        return;

    ctx->running = 0;
    /* Waiting for player task */
	pthread_join(ctx->task, NULL);
    
    audio_player_uninit(ctx);

    free(ctx);
}

void audio_player_pause(audio_player_h h)
{
    player_ctx_t *ctx = (player_ctx_t *)h;
    
    ctx->pause = !ctx->pause;
}

int audio_player_is_runnung(audio_player_h h)
{
    player_ctx_t *ctx = (player_ctx_t *)h;

    return ctx->running;
}

