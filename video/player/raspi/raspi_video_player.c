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
#include <string.h>
#include <IL/OMX_Video.h>

#include "log.h"
#include "decode.h"
#include "audio_player.h"
#include "video_player.h"
#include "ilcore.h"
#include "msleep.h"
#include "omxclock.h"

typedef struct {
    demux_ctx_h demux;
    ilcore_comp_h decoder;
    ilcore_comp_h render;
    ilcore_comp_h scheduler;
    ilcore_comp_h clock;

    ilcore_tunnel_h tunnel_decoder;
    ilcore_tunnel_h tunnel_sched;
    ilcore_tunnel_h tunnel_clock;

    int eos;

    video_player_context *parent;
} player_ctx_t;

static int nalu_format_start_codes(enum AVCodecID codec_id, uint8_t *extradata, int extrasize)
{
    if (codec_id != CODEC_ID_H264)
        return 0;

    /* Numbers from OMXPlayer */
    if (!extradata || extrasize < 7)
        return 1;

    if (*extradata != 1)
        return 1;

    return 0;
}

static ret_code_t hdmi_clock_sync(ilcore_comp_h render)
{
    OMX_CONFIG_LATENCYTARGETTYPE latency;

    OMX_INIT_STRUCT(latency);

    latency.nPortIndex = IL_VIDEO_RENDER_IN_PORT;
    latency.bEnabled = OMX_TRUE;
    latency.nFilter = 2;
    latency.nTarget = 4000;
    latency.nShift = 3;
    latency.nSpeedFactor = -135;
    latency.nInterFactor = 500;
    latency.nAdjCap = 20;

    return ilcore_set_config(render, OMX_IndexConfigLatencyTarget, &latency);
}

static OMX_ERRORTYPE play_buffer_done(OMX_HANDLETYPE hComponent, OMX_PTR pAppData, OMX_BUFFERHEADERTYPE* pBuffer)
{
    media_buffer_t *buf = (media_buffer_t *)pBuffer->pAppPrivate;
    player_ctx_t *ctx = (player_ctx_t *)ilcore_get_app_data(pAppData);

    if (!ctx || !buf)
    {
        DBG_E("Incorrect parameters: ctx - %p, buff - %p\n", ctx, buf);
        return OMX_ErrorBadParameter;
    }

    decode_release_video_buffer(ctx->demux, buf);

    return OMX_ErrorNone;
}

static ret_code_t av_codecid2max_type(enum AVCodecID codec_id, OMX_VIDEO_CODINGTYPE *coding_type)
{
    switch (codec_id)
    {
    case AV_CODEC_ID_H264:
        DBG_I("Use codec: H264\n");
        *coding_type = OMX_VIDEO_CodingAVC;
        break;
    case AV_CODEC_ID_MPEG4:
        DBG_I("Use codec: MPEG4\n");
        *coding_type = OMX_VIDEO_CodingMPEG4;
        break;
    case AV_CODEC_ID_MPEG1VIDEO:
    case AV_CODEC_ID_MPEG2VIDEO:
        DBG_I("Use codec: MPEG2\n");
        *coding_type = OMX_VIDEO_CodingMPEG2;
        break;
    case AV_CODEC_ID_H263:
        DBG_I("Use codec: H263\n");
        *coding_type = OMX_VIDEO_CodingMPEG4;
        break;
    case AV_CODEC_ID_VP6:
    case AV_CODEC_ID_VP6F:
    case AV_CODEC_ID_VP6A:
        DBG_I("Use codec: VP6\n");
        *coding_type = OMX_VIDEO_CodingVP6;
        break;
    case AV_CODEC_ID_VP8:
        DBG_I("Use codec: VP8\n");
        *coding_type = OMX_VIDEO_CodingVP8;
        break;
    case AV_CODEC_ID_THEORA:
        DBG_I("Use codec: THEORA\n");
        *coding_type = OMX_VIDEO_CodingTheora;
        break;
    case AV_CODEC_ID_MJPEG:
    case AV_CODEC_ID_MJPEGB:
        DBG_I("Use codec: MJPEG\n");
        *coding_type = OMX_VIDEO_CodingMJPEG;
        break;
    case AV_CODEC_ID_VC1:
    case AV_CODEC_ID_WMV3:
        DBG_I("Use codec: WMV\n");
        *coding_type = OMX_VIDEO_CodingWMV;
        break;
    default:
        DBG_E("Unknown codec ID: %d\n", codec_id);
        *coding_type = OMX_VIDEO_CodingAutoDetect;
        break;
    }

    return L_OK;
}

static void raspi_release_buffers(player_ctx_t *ctx)
{
    media_buffer_t *buffers[VIDEO_BUFFERS];
    OMX_ERRORTYPE err;
    int i;

    if (ilcore_disable_port(ctx->decoder, IL_VIDEO_DECODER_IN_PORT, 0) != L_OK)
        return;

    for (i = 0; i < VIDEO_BUFFERS; i++)
    {
        OMX_BUFFERHEADERTYPE *hdr;

        buffers[i] = decode_get_free_video_buffer(ctx->demux);
        if (!buffers[i])
        {
            DBG_E("Can not get demuxer buffer #%d\n", i);
            continue;
        }
        hdr = buffers[i]->app_data;
        err = OMX_FreeBuffer(ilcore_get_handle(ctx->decoder), IL_VIDEO_DECODER_IN_PORT, hdr);
        if (err != OMX_ErrorNone)
        {
            DBG_E("OMX_UseBuffer failed. err=%d index=%d\n", err, i);
            return;
        }
    }
    err = omx_core_comp_wait_command(ctx->decoder, OMX_CommandPortDisable, IL_VIDEO_DECODER_IN_PORT, 2000);
    if (err != OMX_ErrorNone)
    {
        DBG_E("Wait event failed. err=0x%x\n", err);
        return;
    }

    for (i = 0; i < VIDEO_BUFFERS; i++)
        if (buffers[i])
            decode_release_video_buffer(ctx->demux, buffers[i]);
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

    if (ctx->parent->stop)
        return; /* Force stop player by user */

    buf = decode_get_free_video_buffer(ctx->demux);
    if (!buf)
    {
        DBG_E("Unable to get free video buffer\n");
        return;
    }
    hdr = (OMX_BUFFERHEADERTYPE *)buf->app_data;
    hdr->pAppPrivate = buf;
    hdr->nOffset = 0;
    hdr->nFilledLen = 0;
    hdr->nTimeStamp = to_omx_time(0);
    hdr->nFlags = OMX_BUFFERFLAG_ENDOFFRAME | OMX_BUFFERFLAG_EOS | OMX_BUFFERFLAG_TIME_UNKNOWN;

    err = OMX_EmptyThisBuffer(ilcore_get_handle(ctx->decoder), hdr);
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

static void raspi_uninit(video_player_h h)
{
    player_ctx_t *ctx = (player_ctx_t *)h;

    wait_complition(ctx);

    if (ctx->tunnel_decoder)
    {
        ilcore_flush_tunnel(ctx->tunnel_decoder);
        ilcore_clean_tunnel(ctx->tunnel_decoder);
        ilcore_destroy_tunnel(ctx->tunnel_decoder);
    }
    if (ctx->tunnel_sched)
    {
        ilcore_flush_tunnel(ctx->tunnel_sched);
        ilcore_clean_tunnel(ctx->tunnel_sched);
        ilcore_destroy_tunnel(ctx->tunnel_sched);
    }
    if (ctx->tunnel_clock)
    {
        ilcore_flush_tunnel(ctx->tunnel_clock);
        ilcore_clean_tunnel(ctx->tunnel_clock);
        ilcore_destroy_tunnel(ctx->tunnel_clock);
    }

    raspi_release_buffers(ctx);

    if (ctx->scheduler)
        ilcore_uninit_comp(ctx->scheduler);
    if (ctx->render)
        ilcore_uninit_comp(ctx->render);
    ilcore_uninit_comp(ctx->decoder);
}

static ret_code_t raspi_init(video_player_h h)
{
    player_ctx_t *ctx = (player_ctx_t *)h;
    enum AVCodecID codec_id;
    OMX_VIDEO_CODINGTYPE coding_type;
    OMX_VIDEO_PARAM_PORTFORMATTYPE format;
    OMX_PARAM_PORTDEFINITIONTYPE port_param;
    OMX_PARAM_BRCMVIDEODECODEERRORCONCEALMENTTYPE concan;
    OMX_CALLBACKTYPE cb;
    int fps_rate, fps_scale, width, height;
    uint8_t *extradata;
    int extrasize, i;
    OMX_STATETYPE state;
    OMX_ERRORTYPE err;
    media_buffer_t *buffers[VIDEO_BUFFERS];

    video_player_pause_toggle(ctx->parent);

    if (decode_get_codec_id(ctx->demux, &codec_id) != L_OK)
    {   
        DBG_E("Uneble get video stream codec ID\n");
        return L_FAILED;
    }
    
    if (av_codecid2max_type(codec_id, &coding_type))
        return L_FAILED;

    cb.EventHandler = il_event_handler;
    cb.EmptyBufferDone = play_buffer_done;
    cb.FillBufferDone = il_fill_buffer_done;

    if (ilcore_init_comp(&ctx->decoder, &cb, "OMX.broadcom.video_decode"))
        return L_FAILED;

    ilcore_set_app_data(ctx->decoder, ctx);

    if (ilcore_disable_all_ports(ctx->decoder))
        goto Error;

    cb.EmptyBufferDone = il_empty_buffer_done;

    if (ilcore_init_comp(&ctx->render, &cb, "OMX.broadcom.video_render"))
        goto Error;

    if (ilcore_disable_all_ports(ctx->render))
        goto Error;

    if (ilcore_init_comp(&ctx->scheduler, &cb, "OMX.broadcom.video_scheduler"))
        goto Error;

    if (ilcore_disable_all_ports(ctx->scheduler))
        goto Error;

    if (ilcore_create_tunnel(&ctx->tunnel_decoder, ctx->decoder, IL_VIDEO_DECODER_OUT_PORT, ctx->scheduler,
        IL_SCHED_VIDEO_IN_PORT))
    {
        goto Error;
    }
    if (ilcore_create_tunnel(&ctx->tunnel_sched, ctx->scheduler, IL_SCHED_VIDEO_OUT_PORT, ctx->render,
        IL_VIDEO_RENDER_IN_PORT))
    {
        goto Error;
    }
    if (ilcore_create_tunnel(&ctx->tunnel_clock, ctx->clock, IL_CLOCK_PORT2, ctx->scheduler, IL_SCHED_CLOCK_PORT))
        goto Error;

    if (ilcore_setup_tunnel(ctx->tunnel_clock))
        goto Error;

    if (ilcore_set_state(ctx->decoder, OMX_StateIdle))
        goto Error;

    OMX_INIT_STRUCT(format);
    format.nPortIndex = IL_VIDEO_DECODER_IN_PORT;
    format.eCompressionFormat = coding_type;
    
    decode_get_frame_rate(ctx->demux, &fps_rate, &fps_scale);
    DBG_I("FPS: rate = %d scale = %d\n", fps_rate, fps_scale);

    if (fps_rate > 0 && fps_scale > 0)
        format.xFramerate = (long long)(1 << 16) * fps_rate / fps_scale;
    else
        format.xFramerate = 25 * (1 << 16);

    if (ilcore_set_param(ctx->decoder, OMX_IndexParamVideoPortFormat, &format))
        goto Error;

    OMX_INIT_STRUCT(port_param);
    port_param.nPortIndex = IL_VIDEO_DECODER_IN_PORT;

    if (ilcore_get_param(ctx->decoder, OMX_IndexParamPortDefinition, &port_param))
        goto Error;

    DBG_I("Video buffers: size=%d amount=%d alligment=%d\n",port_param.nBufferSize, port_param.nBufferCountActual,
        port_param.nBufferAlignment);

    if (decode_setup_video_buffers(ctx->demux, port_param.nBufferCountActual, port_param.nBufferAlignment,
        port_param.nBufferSize) != L_OK)
    {
        DBG_E("Unable to allocate video buffers\n");
        goto Error;
    }

    port_param.nPortIndex = IL_VIDEO_DECODER_IN_PORT;

    devode_get_video_size(ctx->demux, &width, &height);

    DBG_I("Image size = %dx%d\n", width, height);

    port_param.format.video.nFrameWidth  = width;
    port_param.format.video.nFrameHeight = height;

    if (ilcore_set_param(ctx->decoder, OMX_IndexParamPortDefinition, &port_param))
        goto Error;

    OMX_INIT_STRUCT(concan);
    concan.bStartWithValidFrame = OMX_FALSE;

    if (ilcore_set_param(ctx->decoder, OMX_IndexParamBrcmVideoDecodeErrorConcealment, &concan))
        goto Error;

    extradata = decode_get_codec_extra_data(ctx->demux, &extrasize);

    /* TODO. Not shure */
    //if (codec_id == AV_CODEC_ID_H264)
    {
        OMX_CONFIG_BOOLEANTYPE time_stamp_mode;

        OMX_INIT_STRUCT(time_stamp_mode);
        time_stamp_mode.bEnabled = OMX_TRUE;

        if (ilcore_set_param(ctx->decoder, OMX_IndexParamBrcmVideoTimestampFifo, &time_stamp_mode))
            goto Error;

        if (nalu_format_start_codes(codec_id, extradata, extrasize))
        {
            OMX_NALSTREAMFORMATTYPE nal_format;

            DBG_I("Config NAL stream format\n");

            OMX_INIT_STRUCT(nal_format);
            nal_format.nPortIndex = IL_VIDEO_DECODER_IN_PORT;
            nal_format.eNaluFormat = OMX_NaluFormatStartCodes;

            if (ilcore_set_param(ctx->decoder, OMX_IndexParamNalStreamFormatSelect, &nal_format))
                goto Error;
        }
    }

    if (hdmi_clock_sync(ctx->render))
        goto Error;

    /* Setup buffers */
    DBG_I("Setup decoder buffers\n");

    OMX_INIT_STRUCT(port_param);
    port_param.nPortIndex = IL_VIDEO_DECODER_IN_PORT;

    if (ilcore_get_param(ctx->decoder, OMX_IndexParamPortDefinition, &port_param))
        goto Error;

    ilcore_get_state(ctx->decoder, &state);
    if (state != OMX_StateIdle)
    {
        if (state != OMX_StateLoaded)
            ilcore_set_state(ctx->decoder, OMX_StateLoaded);
        ilcore_set_state(ctx->decoder, OMX_StateIdle);
    }
    if (ilcore_enable_port(ctx->decoder, IL_VIDEO_DECODER_IN_PORT, 0))
        goto Error;

    for (i = 0; i < port_param.nBufferCountActual; i++)
    {
        OMX_BUFFERHEADERTYPE *hdr;

        buffers[i] = decode_get_free_video_buffer(ctx->demux);
        if (!buffers[i])
        {
            DBG_E("Can not get demuxer buffer #%d\n", i);
            goto Error;
        }
        err = OMX_UseBuffer(ilcore_get_handle(ctx->decoder), (OMX_BUFFERHEADERTYPE **)&buffers[i]->app_data,
            IL_VIDEO_DECODER_IN_PORT, NULL, port_param.nBufferSize, buffers[i]->s.video.data);
        if (err != OMX_ErrorNone)
        {
            DBG_E("OMX_UseBuffer failed. err=%d index=%d\n", err, i);
            goto Error;
        }
        hdr = (OMX_BUFFERHEADERTYPE *)buffers[i]->app_data;
        hdr->nInputPortIndex = IL_VIDEO_DECODER_IN_PORT;
    }
    err = omx_core_comp_wait_command(ctx->decoder, OMX_CommandPortEnable, IL_VIDEO_DECODER_IN_PORT, 100);
    if (err != OMX_ErrorNone)
    {
        DBG_E("Wait event failed. err=%x\n", err);
        goto Error;
    }

    for (i = 0; i < port_param.nBufferCountActual; i++)
        decode_release_video_buffer(ctx->demux, buffers[i]);

    /*****************/
    DBG_I("Buffers setup done\n");

    if (ilcore_setup_tunnel(ctx->tunnel_decoder))
        goto Error;

    ilcore_set_state(ctx->decoder, OMX_StateExecuting);

    if (ilcore_setup_tunnel(ctx->tunnel_sched))
        goto Error;

    ilcore_set_state(ctx->scheduler, OMX_StateExecuting);
    ilcore_set_state(ctx->render, OMX_StateExecuting);

    if (extradata && extrasize > 0)
    {
        media_buffer_t *buff;
        OMX_BUFFERHEADERTYPE *hdr;
        OMX_ERRORTYPE err;

        DBG_I("Config codec\n");

        buff = decode_get_free_video_buffer(ctx->demux);
        if (!buff)
            goto Error;

        hdr = (OMX_BUFFERHEADERTYPE *)buff->app_data;
        hdr->nOffset = 0;
        hdr->nFilledLen = extrasize;
        memcpy(buff->s.video.data, extradata, extrasize);
        hdr->nFlags = OMX_BUFFERFLAG_CODECCONFIG | OMX_BUFFERFLAG_ENDOFFRAME;
        hdr->pAppPrivate = buff;

        err = OMX_EmptyThisBuffer(ilcore_get_handle(ctx->decoder), hdr);
        if (err != OMX_ErrorNone)
        {
            DBG_E("OMX_EmptyThisBuffer failed. err=0x%08x\n", err);
            goto Error;
        
        }
        DBG_I("Config codec done\n");
    }

    //omx_clock_start(ctx->clock, 0);
    //omx_clock_set_speed(ctx->clock, OMX_CLOCK_PAUSE_SPEED);
    //omx_clock_state_execute(ctx->clock);

    DBG_I("Video player sucsessfuly initialized !\n");
    decode_start_read(ctx->demux);
    video_player_pause_toggle(ctx->parent);

    ilcore_set_eos_callback(ctx->render, eof_callback, ctx);

    return L_OK;

Error:
    DBG_E("Video player initialization failed\n");

    raspi_uninit(h);

    return L_FAILED;
}

static int first_pkt = 1;

static ret_code_t raspi_draw_frame(video_player_h h, media_buffer_t *buff)
{
    player_ctx_t *ctx = (player_ctx_t *)h;
    OMX_PARAM_PORTDEFINITIONTYPE port_param;
    OMX_BUFFERHEADERTYPE *hdr;
    OMX_ERRORTYPE err;
    int no_ts = 0;

    hdr = (OMX_BUFFERHEADERTYPE *)buff->app_data;
    hdr->nFlags = 0;
    hdr->nOffset = 0;
        
    if (buff->pts_ms == AV_NOPTS_VALUE && buff->dts_ms == AV_NOPTS_VALUE)
    {
        hdr->nTimeStamp = to_omx_time(0);
        no_ts = 1;
    }
    else if (buff->dts_ms != AV_NOPTS_VALUE)
    {
        hdr->nTimeStamp = to_omx_time(1000 * buff->dts_ms);
    }
    else
    {
        hdr->nTimeStamp = to_omx_time(1000 * buff->pts_ms);
    }
    DBG_V("Video packet. size: %d pts=%lld dts=%lld\n", buff->size, buff->pts_ms, buff->dts_ms);

    if (first_pkt)
    {
        omx_clock_set_speed(ctx->clock, OMX_CLOCK_NORMAL_SPEED);
        hdr->nFlags = OMX_BUFFERFLAG_STARTTIME;
        first_pkt = 0;
    }
    else
    {
        if(no_ts)
            hdr->nFlags |= OMX_BUFFERFLAG_TIME_UNKNOWN;
    }
    hdr->nFilledLen = (hdr->nAllocLen >= buff->size) ? buff->size : hdr->nAllocLen;
    hdr->pAppPrivate = buff;

    buff->size -= hdr->nFilledLen;

    if (!buff->size && buff->status == MB_FULL_STATUS)
        hdr->nFlags |= OMX_BUFFERFLAG_ENDOFFRAME;

    err = OMX_EmptyThisBuffer(ilcore_get_handle(ctx->decoder), hdr);
    if (err != OMX_ErrorNone)
    {
        DBG_E("OMX_EmptyThisBuffer failed. err=0x%08x size=%d len=%d pts=0x%llu\n", err, buff->size, hdr->nAllocLen,
                (uint64_t)buff->pts_ms);
        goto Error;
    }

    err = omx_core_comp_wait_event(ctx->decoder, OMX_EventPortSettingsChanged, 0);
    if (err != OMX_ErrorNone)
        return L_OK;

    DBG_I("Got OMX_EventPortSettingsChanged\n");

    OMX_INIT_STRUCT(port_param);
    port_param.nPortIndex = IL_VIDEO_DECODER_OUT_PORT;

    if (ilcore_get_param(ctx->decoder, OMX_IndexParamPortDefinition, &port_param))
        goto Error;

    if (ilcore_disable_port(ctx->decoder, IL_VIDEO_DECODER_OUT_PORT, 1))
        goto Error;

    if (ilcore_disable_port(ctx->scheduler, IL_SCHED_VIDEO_IN_PORT, 1))
        goto Error;

    port_param.nPortIndex = IL_SCHED_VIDEO_IN_PORT;
    if (ilcore_set_param(ctx->scheduler, OMX_IndexParamPortDefinition, &port_param))
        goto Error;

    err = omx_core_comp_wait_event(ctx->scheduler, OMX_EventPortSettingsChanged, 0);
    if(err != OMX_ErrorNone)
    {
        DBG_E("Wait for OMX_EventPortSettingsChanged failed\n");
    }

    if (ilcore_enable_port(ctx->decoder, IL_VIDEO_DECODER_OUT_PORT, 1))
        goto Error;

    if (ilcore_enable_port(ctx->scheduler, IL_SCHED_VIDEO_IN_PORT, 1))
        goto Error;

    return L_OK;

Error:
    DBG_E("raspi_draw_frame failed\n");
    
    return L_FAILED;
}

static void raspi_idle(video_player_h ctx)
{
    usleep(1000);
}

ret_code_t video_player_start(video_player_context *player_ctx, demux_ctx_h h, ilcore_comp_h clock)
{
    player_ctx_t *ctx;
    pthread_attr_t attr;
    struct sched_param param;
    ret_code_t rc = L_OK;

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
    ctx->clock = clock;
    ctx->parent = player_ctx;
    player_ctx->priv = ctx;

    player_ctx->init = raspi_init;
    player_ctx->uninit = raspi_uninit;
    player_ctx->draw_frame = raspi_draw_frame;
    player_ctx->idle = raspi_idle;

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
