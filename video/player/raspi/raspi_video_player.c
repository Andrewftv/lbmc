#include <string.h>
#include <IL/OMX_Video.h>

#include "log.h"
#include "decode.h"
#include "video_player.h"
#include "ilcore.h"
#include "msleep.h"

typedef struct {
    demux_ctx_h demux;
    ilcore_comp_h decoder;
    ilcore_comp_h render;
    ilcore_comp_h scheduler;
    ilcore_comp_h clock;

    ilcore_tunnel_h tunnel_decoder;
    ilcore_tunnel_h tunnel_sched;
    ilcore_tunnel_h tunnel_clock;

    video_player_context *parent;

    int buff_done;
    msleep_h buffer_done;
} player_ctx_t;

static int nalu_format_start_codes(uint8_t *extradata, int extrasize)
{
    /* Numbers from OMX player */
    if (!extradata || extrasize < 7)
        return 1;

    if (*extradata != 1)
        return 1;

    return 0;
}
#if 0
static ret_code_t hdmi_clock_sync(ilcore_comp_h render)
{
    OMX_CONFIG_LATENCYTARGETTYPE latency;

    memset(&latency, 0, sizeof(OMX_CONFIG_LATENCYTARGETTYPE));
    latency.nSize = sizeof(OMX_CONFIG_LATENCYTARGETTYPE);
    latency.nVersion.nVersion = OMX_VERSION;

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
#endif
static OMX_ERRORTYPE play_buffer_done(OMX_HANDLETYPE hComponent, OMX_PTR pAppData, OMX_BUFFERHEADERTYPE* pBuffer)
{
    player_ctx_t *ctx = (player_ctx_t *)pBuffer->pAppPrivate;

    if (!ctx)
        return OMX_ErrorNone;

    ctx->buff_done = 1;
    msleep_wakeup(ctx->buffer_done);

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
        return L_FAILED;
    }

    return L_OK;
}

static void raspi_uninit(video_player_h h)
{
    player_ctx_t *ctx = (player_ctx_t *)h;

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
    if (ctx->scheduler)
        ilcore_uninit_comp(ctx->scheduler);
    if (ctx->render)
        ilcore_uninit_comp(ctx->render);
    ilcore_uninit_comp(ctx->decoder);

    msleep_uninit(ctx->buffer_done);
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
    video_buffer_t *buffers[VIDEO_BUFFERS];

    video_player_pause(ctx->parent);

    msleep_init(&ctx->buffer_done);

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

    memset(&format, 0, sizeof(OMX_VIDEO_PARAM_PORTFORMATTYPE));
    format.nSize = sizeof(OMX_VIDEO_PARAM_PORTFORMATTYPE);
    format.nVersion.nVersion = OMX_VERSION;
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

    memset(&port_param, 0, sizeof(OMX_PARAM_PORTDEFINITIONTYPE));
    port_param.nSize = sizeof(OMX_PARAM_PORTDEFINITIONTYPE);
    port_param.nVersion.nVersion = OMX_VERSION;
    port_param.nPortIndex = IL_VIDEO_DECODER_IN_PORT;

    if (ilcore_get_param(ctx->decoder, OMX_IndexParamPortDefinition, &port_param))
        goto Error;

    port_param.nPortIndex = IL_VIDEO_DECODER_IN_PORT;
    port_param.nBufferCountActual = VIDEO_BUFFERS;
    port_param.nBufferSize = (200 * 1024);

    devode_get_video_size(ctx->demux, &width, &height);

    port_param.format.video.nFrameWidth  = width;
    port_param.format.video.nFrameHeight = height;

    if (ilcore_set_param(ctx->decoder, OMX_IndexParamPortDefinition, &port_param))
        goto Error;

    memset(&concan, 0, sizeof(OMX_PARAM_BRCMVIDEODECODEERRORCONCEALMENTTYPE));
    concan.nSize = sizeof(OMX_PARAM_BRCMVIDEODECODEERRORCONCEALMENTTYPE);
    concan.nVersion.nVersion = OMX_VERSION;
    concan.bStartWithValidFrame = OMX_FALSE;

    if (ilcore_set_param(ctx->decoder, OMX_IndexParamBrcmVideoDecodeErrorConcealment, &concan))
        goto Error;

    extradata = decode_get_codec_extra_data(ctx->demux, &extrasize);

    /* TODO. Not shure */
    if (codec_id == AV_CODEC_ID_H264)
    {
        OMX_CONFIG_BOOLEANTYPE time_stamp_mode;

        memset(&time_stamp_mode, 0, sizeof(OMX_CONFIG_BOOLEANTYPE));
        time_stamp_mode.nSize = sizeof(OMX_CONFIG_BOOLEANTYPE);
        time_stamp_mode.nVersion.nVersion = OMX_VERSION;
        time_stamp_mode.bEnabled = OMX_TRUE;

        if (ilcore_set_param(ctx->decoder, OMX_IndexParamBrcmVideoTimestampFifo, &time_stamp_mode))
            goto Error;

        if (nalu_format_start_codes(extradata, extrasize))
        {
            OMX_NALSTREAMFORMATTYPE nal_format;

            DBG_I("Config NAL stream format\n");

            memset(&nal_format, 0, sizeof(OMX_NALSTREAMFORMATTYPE));
            nal_format.nSize = sizeof(OMX_NALSTREAMFORMATTYPE);
            nal_format.nVersion.nVersion = OMX_VERSION;
            nal_format.nPortIndex = IL_VIDEO_DECODER_IN_PORT;
            nal_format.eNaluFormat = OMX_NaluFormatStartCodes;

            if (ilcore_set_param(ctx->decoder, OMX_IndexParamNalStreamFormatSelect, &nal_format))
                goto Error;
        }
    }
#if 0
    if (hdmi_clock_sync(ctx->render))
        goto Error;
#endif
    /* Setup buffers */
    DBG_I("Setup decoder buffers\n");

    memset(&port_param, 0, sizeof(OMX_PARAM_PORTDEFINITIONTYPE));
    port_param.nSize = sizeof(OMX_PARAM_PORTDEFINITIONTYPE);
    port_param.nVersion.nVersion = OMX_VERSION;
    port_param.nPortIndex = IL_VIDEO_DECODER_IN_PORT;

    if (ilcore_get_param(ctx->decoder, OMX_IndexParamPortDefinition, &port_param))
        goto Error;

    ilcore_get_state(ctx->decoder, &state);
    if (state != OMX_StateIdle)
        ilcore_set_state(ctx->decoder, OMX_StateIdle);

    if (ilcore_enable_port(ctx->decoder, IL_VIDEO_DECODER_IN_PORT, 0))
        goto Error;

    for (i = 0; i < port_param.nBufferCountActual; i++)
    {
        OMX_BUFFERHEADERTYPE *hdr;

        buffers[i] = decode_get_free_buffer(ctx->demux);
        if (!buffers[i])
        {
            DBG_E("Can not get demuxer buffer #%d\n", i);
            goto Error;
        }
        err = OMX_UseBuffer(ilcore_get_handle(ctx->decoder), (OMX_BUFFERHEADERTYPE **)&buffers[i]->app_data,
            IL_VIDEO_DECODER_IN_PORT, NULL, port_param.nBufferSize, buffers[i]->data);
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
        DBG_E("Wait event failed. err=%d\n");
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
        video_buffer_t *buff;
        OMX_BUFFERHEADERTYPE *hdr;
        OMX_ERRORTYPE err;

        DBG_I("Config codec\n");

        buff = decode_get_free_buffer(ctx->demux);
        if (!buff)
            goto Error;

        hdr = (OMX_BUFFERHEADERTYPE *)buff->app_data;
        hdr->nOffset = 0;
        hdr->nFilledLen = extrasize;
        memcpy(buff->data, extradata, extrasize);
        hdr->nFlags = OMX_BUFFERFLAG_CODECCONFIG | OMX_BUFFERFLAG_ENDOFFRAME;
        hdr->pAppPrivate = ctx;

        ctx->buff_done = 0;
        err = OMX_EmptyThisBuffer(ilcore_get_handle(ctx->decoder), hdr);
        if (err != OMX_ErrorNone)
        {
            DBG_E("OMX_EmptyThisBuffer failed. err=0x%08x\n", err);
            goto Error;
        
        }
        if (!ctx->buff_done)
        {
            if (msleep_wait(ctx->buffer_done, 1000) == MSLEEP_TIMEOUT)
            {
                DBG_E("Event buffer done not received\n");
                goto Error;
            }
        }
        decode_release_video_buffer(ctx->demux, buff);
        DBG_I("Config codec done\n");
    }

    DBG_I("Video player sucsessfuly initialized !\n");
    decode_start_read(ctx->demux);
    video_player_pause(ctx->parent);

    return L_OK;

Error:
    DBG_E("Video player initialization failed\n");

    raspi_uninit(h);

    return L_FAILED;
}

static int first_pkt = 1;

static ret_code_t raspi_draw_frame(video_player_h h, video_buffer_t *buff)
{
    player_ctx_t *ctx = (player_ctx_t *)h;
    OMX_PARAM_PORTDEFINITIONTYPE port_param;
    OMX_BUFFERHEADERTYPE *hdr;
    OMX_ERRORTYPE err;
    int sent_data = 0;

    while (buff->size > 0)
    {
        hdr = (OMX_BUFFERHEADERTYPE *)buff->app_data;
        hdr->nFlags = 0;
        hdr->nOffset = 0;
        hdr->nTimeStamp = buff->pts_ms;
        if (first_pkt)
        {
            hdr->nFlags = OMX_BUFFERFLAG_STARTTIME | OMX_BUFFERFLAG_ENDOFFRAME;
            first_pkt = 0;
        }
        else
        {
            if(buff->pts_ms == AV_NOPTS_VALUE)
            {
                hdr->nFlags = OMX_BUFFERFLAG_TIME_UNKNOWN;
                hdr->nTimeStamp = 0;
            }
        }
        hdr->nFilledLen = (hdr->nAllocLen >= buff->size) ? buff->size : hdr->nAllocLen;
        hdr->pAppPrivate = ctx;

        if (sent_data)
            memcpy(buff->data, &buff->data[sent_data], hdr->nFilledLen);

        sent_data += hdr->nFilledLen;
        buff->size -= hdr->nFilledLen;

        if (buff->size > 0)
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
            continue;

        DBG_I("Got OMX_EventPortSettingsChanged\n");
    
        memset(&port_param, 0, sizeof(OMX_PARAM_PORTDEFINITIONTYPE));
        port_param.nSize = sizeof(OMX_PARAM_PORTDEFINITIONTYPE);
        port_param.nVersion.nVersion = OMX_VERSION;
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
    }
    return L_OK;

Error:
    DBG_E("raspi_draw_frame failed\n");
    
    return L_FAILED;
}

static void raspi_idle(video_player_h ctx)
{
    usleep(100000);
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

