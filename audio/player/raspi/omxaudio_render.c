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
#include "omxaudio_render.h"

void audio_player_set_buffer_done(audio_player_h h);
demux_ctx_h audio_player_get_demuxer(audio_player_h h);
msleep_h audio_player_get_msleep(audio_player_h h);

static OMX_ERRORTYPE play_buffer_done(OMX_HANDLETYPE hComponent, OMX_PTR pAppData, OMX_BUFFERHEADERTYPE* pBuffer)
{
    audio_player_h h = (audio_player_h)pBuffer->pAppPrivate;

    audio_player_set_buffer_done(h);
    msleep_wakeup(audio_player_get_msleep(h));

    return OMX_ErrorNone;
}

ilcore_comp_h create_omxaudio_render(int buff_size, int buff_count, int sample_rate, int channels)
{
    OMX_ERRORTYPE err;
    OMX_CALLBACKTYPE cb;
    OMX_AUDIO_PARAM_PCMMODETYPE pcm;
    ilcore_comp_h render;

    cb.EventHandler = il_event_handler;
    cb.EmptyBufferDone = play_buffer_done;
    cb.FillBufferDone = il_fill_buffer_done;

    if (ilcore_init_comp(&render, &cb, "OMX.broadcom.audio_render"))
        return NULL;

    if (ilcore_disable_all_ports(render))
        goto Error;

    if (ilcore_set_port_buffers_param(render, buff_size, buff_count))
        goto Error;

    memset(&pcm, 0, sizeof(OMX_AUDIO_PARAM_PCMMODETYPE));
    pcm.nSize = sizeof(OMX_AUDIO_PARAM_PCMMODETYPE);
    pcm.nVersion.nVersion = OMX_VERSION;
    pcm.nPortIndex = IL_AUDIO_RENDER_IN_PORT;
    pcm.nChannels = channels;
    pcm.eNumData = OMX_NumericalDataSigned;
    pcm.eEndian = OMX_EndianLittle;
    pcm.nSamplingRate = sample_rate;
    pcm.bInterleaved = OMX_TRUE;
    pcm.nBitPerSample = 16;
    pcm.ePCMMode = OMX_AUDIO_PCMModeLinear;

    pcm.eChannelMapping[1] = OMX_AUDIO_ChannelRF;
    pcm.eChannelMapping[0] = OMX_AUDIO_ChannelLF;

    err = OMX_SetParameter(ilcore_get_handle(render), OMX_IndexParamAudioPcm, &pcm);
    if (err != OMX_ErrorNone)
    {
        DBG_E("OMX_IndexParamAudioPcm failed. err=%d\n");
        goto Error;
    }

    return render;

Error:
    destroy_omxaudio_render(render);

    return NULL;
}

void destroy_omxaudio_render(ilcore_comp_h render)
{
    if (render)
        ilcore_uninit_comp(render);
}

ret_code_t omxaudio_render_setup_buffers(ilcore_comp_h render, demux_ctx_h demuxer)
{
    OMX_ERRORTYPE err;
    OMX_STATETYPE state;
    OMX_PARAM_PORTDEFINITIONTYPE portdef;
    audio_buffer_t *buf[AUDIO_BUFFERS];
    int i;

    memset(&portdef, 0, sizeof(OMX_PARAM_PORTDEFINITIONTYPE));
    portdef.nSize = sizeof(OMX_PARAM_PORTDEFINITIONTYPE);
    portdef.nVersion.nVersion = OMX_VERSION;
    portdef.nPortIndex = IL_AUDIO_RENDER_IN_PORT;

    err = OMX_GetParameter(ilcore_get_handle(render), OMX_IndexParamPortDefinition, &portdef);
    if(err != OMX_ErrorNone || portdef.bEnabled != OMX_FALSE || portdef.nBufferCountActual == 0 ||
        portdef.nBufferSize == 0)
    {
        DBG_E("Incorrect port defenitions: err=%d enabled=%d buffers count=%d buffers size=%d",
            err, portdef.bEnabled, portdef.nBufferCountActual, portdef.nBufferSize);
        return L_FAILED;
    }
    DBG_I("buffers count=%d buffers size=%d alligment=%d\n", portdef.nBufferCountActual, portdef.nBufferSize,
        portdef.nBufferAlignment);

    if (ilcore_get_state(render, &state))
        return L_FAILED;

    if (state != OMX_StateIdle)
    {
        DBG_E("Incorrect state: err=%d state=%d\n", err, state);
        return L_FAILED;
    }

    err = OMX_SendCommand(ilcore_get_handle(render), OMX_CommandPortEnable, IL_AUDIO_RENDER_IN_PORT, NULL);
    if (err != OMX_ErrorNone)
    {
        DBG_E("Port enabling is failed. err=%d\n");
        return L_FAILED;
    }

    for (i = 0; i < portdef.nBufferCountActual; i++)
    {
        OMX_BUFFERHEADERTYPE *hdr;

        buf[i] = decode_get_free_audio_buffer(demuxer);
        if (!buf[i])
        {
            DBG_E("Can not get demuxer buffer #%d\n", i);
            return L_FAILED;
        }

        err = OMX_UseBuffer(ilcore_get_handle(render), &hdr, IL_AUDIO_RENDER_IN_PORT, NULL,
            portdef.nBufferSize, buf[i]->data[0]);
        if (err != OMX_ErrorNone)
        {
            DBG_E("OMX_UseBuffer failed. err=%d index=%d\n", err, i);
            return L_FAILED;
        }
        buf[i]->app_data = hdr;
    }

    err = omx_core_comp_wait_command(render, OMX_CommandPortEnable, IL_AUDIO_RENDER_IN_PORT, 100);
    if (err != OMX_ErrorNone)
    {
        DBG_E("Wait event failed. err=%d\n");
        return L_FAILED;
    }

    for (i = 0; i < portdef.nBufferCountActual; i++)
        decode_release_audio_buffer(demuxer, buf[i]);

    return L_OK;
}

