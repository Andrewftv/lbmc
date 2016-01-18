#include <string.h>
#include <IL/OMX_Video.h>

#include "log.h"
#include "decode.h"
#include "video_player.h"
#include "ilcore.h"

typedef struct {
    demux_ctx_h demux;
    ilcore_comp_h decoder;
    ilcore_comp_h render;
    ilcore_comp_h scheduler;
    ilcore_comp_h clock;

    ilcore_tunnel_h tunnel_decoder;
    ilcore_tunnel_h tunnel_sched;
    ilcore_tunnel_h tunnel_clock;
} player_ctx_t;

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

    if (decode_get_codec_id(ctx->demux, &codec_id) != L_OK)
    {   
        DBG_E("Uneble get video stream codec ID\n");
        return L_FAILED;
    }
    
    switch (codec_id)
    {
    case AV_CODEC_ID_H264:
        DBG_I("Use codec: H264\n");
        coding_type = OMX_VIDEO_CodingAVC;
        break;
    case AV_CODEC_ID_MPEG4:
        DBG_I("Use codec: MPEG4\n");
        coding_type = OMX_VIDEO_CodingMPEG4;
        break;
    case AV_CODEC_ID_MPEG1VIDEO:
    case AV_CODEC_ID_MPEG2VIDEO:
        DBG_I("Use codec: MPEG2\n");
        coding_type = OMX_VIDEO_CodingMPEG2;
        break;
    case AV_CODEC_ID_H263:
        DBG_I("Use codec: H263\n");
        coding_type = OMX_VIDEO_CodingMPEG4;
        break;
    case AV_CODEC_ID_VP6:
    case AV_CODEC_ID_VP6F:
    case AV_CODEC_ID_VP6A:
        DBG_I("Use codec: VP6\n");
        coding_type = OMX_VIDEO_CodingVP6;
        break;
    case AV_CODEC_ID_VP8:
        DBG_I("Use codec: VP8\n");
        coding_type = OMX_VIDEO_CodingVP8;
        break;
    case AV_CODEC_ID_THEORA:
        DBG_I("Use codec: THEORA\n");
        coding_type = OMX_VIDEO_CodingTheora;
        break;
    case AV_CODEC_ID_MJPEG:
    case AV_CODEC_ID_MJPEGB:
        DBG_I("Use codec: MJPEG\n");
        coding_type = OMX_VIDEO_CodingMJPEG;
        break;
    case AV_CODEC_ID_VC1:
    case AV_CODEC_ID_WMV3:
        DBG_I("Use codec: WMV\n");
        coding_type = OMX_VIDEO_CodingWMV;
        break;
    default:
        DBG_E("Unknown codec ID: %d\n", codec_id);
        return L_FAILED;
    }

    cb.EventHandler = il_event_handler;
    cb.EmptyBufferDone = il_empty_buffer_done;
    cb.FillBufferDone = il_fill_buffer_done;

    if (ilcore_init_comp(&ctx->decoder, &cb, "OMX.broadcom.video_decode"))
        return L_FAILED;

    if (ilcore_disable_all_ports(ctx->decoder))
        goto Error;

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
    port_param.nBufferCountActual = 2;
    port_param.nBufferSize = (100 * 1024);

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
    }

    return L_OK;

Error:
    DBG_E("Video player initialization failed\n");

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

    return L_FAILED;
}

static void raspi_uninit(video_player_h h)
{
    player_ctx_t *ctx = (player_ctx_t *)h;

    if (ctx->scheduler)
        ilcore_uninit_comp(ctx->scheduler);
    if (ctx->render)
        ilcore_uninit_comp(ctx->render);
    ilcore_uninit_comp(ctx->decoder);
}

static ret_code_t raspi_draw_frame(video_player_h ctx, uint8_t *buf)
{
    return L_OK;
}

static void raspi_idle(video_player_h ctx)
{
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

