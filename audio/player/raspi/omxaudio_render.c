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
#include "omxaudio_render.h"

ret_code_t omxaudio_render_setup_buffers(ilcore_comp_h render, demux_ctx_h demuxer)
{
    OMX_ERRORTYPE err;
    OMX_STATETYPE state;
    OMX_PARAM_PORTDEFINITIONTYPE portdef;
    media_buffer_t *buf[2 * AUDIO_BUFFERS];
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
            portdef.nBufferSize, buf[i]->s.audio.data[0]);
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

ret_code_t omxaudio_render_release_buffers(ilcore_comp_h render, demux_ctx_h demuxer)
{
    media_buffer_t *buf[2 * AUDIO_BUFFERS];
    int i, size, count, align;

    decode_get_audio_buffs_info(demuxer, &size, &count, &align);

    ilcore_disable_port(render, IL_AUDIO_RENDER_IN_PORT, 0);

    for (i = 0; i < count; i++)
    {
        OMX_BUFFERHEADERTYPE *hdr;

        buf[i] = decode_get_free_audio_buffer(demuxer);
        if (!buf[i])
        {
            DBG_E("Can not get demuxer buffer #%d\n", i);
            continue;
        }
        hdr = buf[i]->app_data;
        OMX_FreeBuffer(ilcore_get_handle(render), IL_AUDIO_RENDER_IN_PORT, hdr);
    }

    omx_core_comp_wait_command(render, OMX_CommandPortDisable, IL_AUDIO_RENDER_IN_PORT, 2000);

    for (i = 0; i < count; i++)
        if (buf[i])
            decode_release_audio_buffer(demuxer, buf[i]);

    return L_OK;
}

