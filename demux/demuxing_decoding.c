#define _GNU_SOURCE
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <pthread.h>
#include <stdlib.h>

#include <libavutil/opt.h>
#include <libavutil/imgutils.h>
#include <libavutil/samplefmt.h>
#include <libavutil/timestamp.h>
#include <libavformat/avformat.h>
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>

#include "log.h"
#include "decode.h"
#include "audio_player.h"
#include "video_player.h"
#include "timeutils.h"
#include "msleep.h"
#include "queue.h"

#define SAMPLE_PER_BUFFER 4096

typedef struct {
    struct SwrContext *swr;
    struct AVCodecContext *codec;
    AVStream *st;

    msleep_h empty_buff;
    msleep_h full_buff;

    queue_h free_buff;
    queue_h fill_buff;

    int buff_size;
    int frame_count;
    int stream_idx;
    int audio_streams;
    int drop_packets;

    /* Destination format after resampling */
    enum AVSampleFormat dst_fmt;
} app_audio_ctx_t;

#ifdef CONFIG_VIDEO
typedef struct {
#ifndef CONFIG_VIDEO_HW_DECODE
    AVCodecContext *codec;
    struct SwsContext *sws;
#endif
    AVStream *st;
    enum AVCodecID codec_id;

    msleep_h empty_buff;
    msleep_h full_buff;

    int width;
    int height;
    int fps_rate;
    int fps_scale;
    uint8_t *codec_ext_data;
    int codec_ext_data_size;
    enum AVPixelFormat pix_fmt;

    queue_h free_buff;
    queue_h fill_buff;

    int subtitle_stream_idx;
    int stream_idx;
    int frame_count;
} app_video_ctx_t;
#endif

typedef struct {
    struct AVFormatContext *fmt_ctx;

    app_audio_ctx_t *audio_ctx;
#ifdef CONFIG_VIDEO
    app_video_ctx_t *video_ctx;
#endif

    msleep_h pause;

    pthread_t task;
    int stop_decode;
} demux_ctx_t;

/* Prototypes */
static enum AVSampleFormat planar_sample_to_same_packed(enum AVSampleFormat fmt);
static void *read_demux_data(void *ctx);
static ret_code_t open_codec_context(int *stream_idx, AVFormatContext *fmt_ctx, enum AVMediaType type);
static ret_code_t resampling_config(app_audio_ctx_t *ctx);
static void uninit_audio_buffers(app_audio_ctx_t *ctx);

static ret_code_t timedwait_buffer(msleep_h h, int timeout)
{
    ret_code_t rc;

    switch (msleep_wait(h, timeout))
    {
    case MSLEEP_INTERRUPT:
        rc = L_OK;
        break;
    case MSLEEP_TIMEOUT:
        rc = L_TIMEOUT;
        break;
    case MSLEEP_ERROR:
    default:
        rc = L_FAILED;
        break;
    }

    return rc;
}

static void signal_buffer(msleep_h h)
{
    msleep_wakeup(h);
}

static int get_audio_streams_count(AVFormatContext *fmt)
{
    int i, count = 0;
    AVStream *st;

    for (i = 0; i < fmt->nb_streams; i++)
    {
        st = fmt->streams[i];
        if (st->codec->codec_type == AVMEDIA_TYPE_AUDIO)
            count++;
    }

    return count;
}

ret_code_t decode_next_audio_stream(demux_ctx_h h)
{
    demux_ctx_t *ctx = (demux_ctx_t *)h;
    int index;
    AVStream *st;
    AVCodecContext *dec_ctx = NULL;
    AVCodec *dec = NULL;
    AVDictionary *opts = NULL;

    if (ctx->audio_ctx->audio_streams < 2)
        return L_FAILED;

    index = ctx->audio_ctx->stream_idx;
    while (1)
    {
        index++;
        if (index >= ctx->fmt_ctx->nb_streams)
            index = 0;

        st = ctx->fmt_ctx->streams[index];
        if (st->codec->codec_type == AVMEDIA_TYPE_AUDIO)
            break;
    }
    ctx->audio_ctx->st = st;

    DBG_I("Next audio stream index is %d\n", index);

    /* TODO Here we need real synchronization */
    ctx->audio_ctx->drop_packets = 1;
    usleep(50000);

    uninit_audio_buffers(ctx->audio_ctx);
    if (ctx->audio_ctx->swr)
        swr_free(&ctx->audio_ctx->swr);
    ctx->audio_ctx->swr = NULL;
    if (ctx->audio_ctx->codec)
        avcodec_close(ctx->audio_ctx->codec);

    dec_ctx = st->codec;
    dec = avcodec_find_decoder(dec_ctx->codec_id);
    if (!dec)
    {
        DBG_E("Failed to find %s codec\n", av_get_media_type_string(AVMEDIA_TYPE_AUDIO));
        return L_FAILED;
    }

    /* Init the decoders, with or without reference counting */
    if (avcodec_open2(dec_ctx, dec, &opts) < 0)
    {
        DBG_E("Failed to open %s codec\n", av_get_media_type_string(AVMEDIA_TYPE_AUDIO));
        return L_FAILED;
    }

    ctx->audio_ctx->stream_idx = index;
    ctx->audio_ctx->codec = st->codec;

    if (resampling_config(ctx->audio_ctx))
        return L_FAILED;

    ctx->audio_ctx->drop_packets = 0;

    return L_OK;
}

ret_code_t decode_init(demux_ctx_h *h, char *src_file)
{
    demux_ctx_t *ctx;
    int streams = 0;
    int stream_index;

    /* register all formats and codecs */
    av_register_all();

    ctx = (demux_ctx_t *)malloc(sizeof(demux_ctx_t));
    if (!ctx)
    {
        DBG_E("Memory allocation failed\n");
        return L_FAILED;
    }
    msleep_init(&ctx->pause);
    /* open input file, and allocate format context */
    if (avformat_open_input(&ctx->fmt_ctx, src_file, NULL, NULL) < 0)
    {
        DBG_E("Could not open source file %s\n", src_file);
        return L_FAILED;
    }
    /* retrieve stream information */
    if (avformat_find_stream_info(ctx->fmt_ctx, NULL) < 0)
    {
        DBG_F("Could not find stream information\n");
        return L_FAILED;
    }
#ifdef CONFIG_VIDEO
    if (!open_codec_context(&stream_index, ctx->fmt_ctx, AVMEDIA_TYPE_VIDEO))
    {
        app_video_ctx_t *vctx;
        AVStream *video_stream = NULL;
        int i;
#ifndef CONFIG_VIDEO_HW_DECODE
        int rc;
#endif

        streams++;

        vctx = (app_video_ctx_t *)malloc(sizeof(app_video_ctx_t));
        if (!vctx)
        {
            DBG_E("Can not alloc demuxer video context\n");
            return L_FAILED;
        }
        ctx->video_ctx = vctx;

        memset(vctx, 0, sizeof(app_video_ctx_t));
        vctx->stream_idx = stream_index;
        queue_init(&vctx->free_buff);
        queue_init(&vctx->fill_buff);
        vctx->subtitle_stream_idx = -1;

        video_stream = ctx->fmt_ctx->streams[stream_index];
#ifndef CONFIG_VIDEO_HW_DECODE
        vctx->codec = video_stream->codec;
#endif
        vctx->st = video_stream;
        vctx->width = video_stream->codec->width;
        vctx->height = video_stream->codec->height;
        vctx->pix_fmt = video_stream->codec->pix_fmt;
        vctx->codec_id = video_stream->codec->codec_id;
       
        DBG_I("Codec extradata size is: %d\n", video_stream->codec->extradata_size);
        vctx->codec_ext_data = (uint8_t *)malloc(video_stream->codec->extradata_size);
        memcpy(vctx->codec_ext_data, video_stream->codec->extradata, video_stream->codec->extradata_size);
        vctx->codec_ext_data_size = video_stream->codec->extradata_size;
 
        if (video_stream->avg_frame_rate.den && video_stream->avg_frame_rate.num)
        {
            vctx->fps_rate = video_stream->avg_frame_rate.num;
            vctx->fps_scale = video_stream->avg_frame_rate.den;
        }
        else if (video_stream->r_frame_rate.num && video_stream->r_frame_rate.den)
        {
            vctx->fps_rate = video_stream->r_frame_rate.num;
            vctx->fps_scale = video_stream->r_frame_rate.den;
        }

        DBG_I("SRC: Video size %dx%d. pix_fmt=%s\n", vctx->width, vctx->height, av_get_pix_fmt_name(vctx->pix_fmt));
#ifdef CONFIG_VIDEO_HW_DECODE
        for (i = 0; i < VIDEO_BUFFERS; i++)
        {
            video_buffer_t *vbuff;        

            vbuff = (video_buffer_t *)malloc(sizeof(video_buffer_t));

            if (posix_memalign ((void **)&vbuff->data, 16, 200 * 1024))
            {
                DBG_E("Could not allocate destination video buffer\n");
                return L_FAILED;
            }
            vbuff->buff_size = 200 * 1024;
            vbuff->number = i;
            DBG_I("Video buffer %p\n", vbuff->data);

            queue_push(vctx->free_buff, (queue_node_t *)vbuff);
        }
#else
        /* Allocate destination image with same resolution and RGBA pixel format */
        for (i = 0; i < VIDEO_BUFFERS; i++)
        {
            video_buffer_t *vbuff;        

            vbuff = (video_buffer_t *)malloc(sizeof(video_buffer_t));

            rc = av_image_alloc(vbuff->buffer, vbuff->linesize, vctx->codec->width, vctx->codec->height,
                AV_PIX_FMT_RGBA, 1);
            if (rc < 0)
            {
                DBG_E("Could not allocate destination video buffer\n");
                return L_FAILED;
            }
            vbuff->size = rc;
            vbuff->number = i;

            queue_push(vctx->free_buff, (queue_node_t *)vbuff);
        }
        /* Create scale context */
        vctx->sws = sws_getContext(vctx->codec->width, vctx->codec->height, vctx->codec->pix_fmt, vctx->codec->width,
            vctx->codec->height, AV_PIX_FMT_RGBA, SWS_BILINEAR, NULL, NULL, NULL);
        if (!vctx->sws)
        {
            DBG_E("Can not allocate scale format\n");
            return -1;
        }
#endif
        msleep_init(&vctx->empty_buff);
        msleep_init(&vctx->full_buff);

        if (!open_codec_context(&stream_index, ctx->fmt_ctx, AVMEDIA_TYPE_SUBTITLE))
        {
            DBG_I("Subtitles stream was found\n");

            vctx->subtitle_stream_idx = stream_index;
        }
    }
#endif
    if (!open_codec_context(&stream_index, ctx->fmt_ctx, AVMEDIA_TYPE_AUDIO))
    {
        app_audio_ctx_t *actx;
        AVStream *audio_stream = NULL;

        streams++;

        actx = (app_audio_ctx_t *)malloc(sizeof(app_audio_ctx_t));
        if (!actx)
        {
            DBG_E("Can not alloc demuxer audio context\n");
            return L_FAILED;
        }
        ctx->audio_ctx = actx;

        memset(actx, 0, sizeof(app_audio_ctx_t));
        actx->stream_idx = stream_index;

        queue_init(&actx->free_buff);
        queue_init(&actx->fill_buff);

        audio_stream = ctx->fmt_ctx->streams[stream_index];
        actx->codec = audio_stream->codec;
        actx->st = audio_stream;

        actx->audio_streams = get_audio_streams_count(ctx->fmt_ctx);
        DBG_I("Audio stream was found. Index = %d total %d\n", stream_index, actx->audio_streams);

        msleep_init(&actx->empty_buff);
        msleep_init(&actx->full_buff);

        if (resampling_config(actx))
            return L_FAILED;

    }

    *h = ctx;

    if (!streams)
    {
        DBG_E("Any streams were found\n");
        return L_FAILED;
    }

    /* dump input information to stderr */
    av_dump_format(ctx->fmt_ctx, 0, src_file, 0);

    return L_OK;
}

void decode_uninit(demux_ctx_h h)
{
    demux_ctx_t *ctx = (demux_ctx_t *)h;

    if (!ctx)
        return;

    if (ctx->audio_ctx)
    {
        app_audio_ctx_t *actx = ctx->audio_ctx;

        msleep_uninit(actx->full_buff);
        msleep_uninit(actx->empty_buff);
        /* Release allocated buffers */
        uninit_audio_buffers(actx);
        /* Release resampling context */
        if (actx->swr)
            swr_free(&actx->swr);
        if (actx->codec)
            avcodec_close(actx->codec);

        queue_uninit(actx->free_buff);
        queue_uninit(actx->fill_buff);

        free(actx);
    }
#ifdef CONFIG_VIDEO
    if (ctx->video_ctx)
    {
        video_buffer_t *buff;
        app_video_ctx_t *vctx = ctx->video_ctx;

        msleep_uninit(vctx->full_buff);
        msleep_uninit(vctx->empty_buff);
#ifdef CONFIG_VIDEO_HW_DECODE
        while ((buff = (video_buffer_t *)queue_pop(vctx->free_buff)) != NULL)
        {
            free(buff->data);
            free(buff);
        }
        while ((buff = (video_buffer_t *)queue_pop(vctx->fill_buff)) != NULL)
        {
            free(buff->data);
            free(buff);
        }
#else
        if (vctx->codec)
            avcodec_close(vctx->codec);
        if (vctx->sws)
            sws_freeContext(vctx->sws);

        while ((buff = (video_buffer_t *)queue_pop(vctx->free_buff)) != NULL)
        {
            av_freep(&buff->buffer[0]);
            free(buff);
        }
        while ((buff = (video_buffer_t *)queue_pop(vctx->fill_buff)) != NULL)
        {
            av_freep(&buff->buffer[0]);
            free(buff);
        }
#endif
        queue_uninit(vctx->free_buff);
        queue_uninit(vctx->fill_buff);

        if (vctx->codec_ext_data)
            free(vctx->codec_ext_data);

        free(vctx);
    }
#endif

    if (ctx->fmt_ctx)
        avformat_close_input(&ctx->fmt_ctx);
    msleep_uninit(ctx->pause);

    free(ctx);
}

void decode_start_read(demux_ctx_h h)
{
    demux_ctx_t *ctx = (demux_ctx_t *)h;

    DBG_I("Wakeup\n");

    if (!ctx)
        return;

    msleep_wakeup(ctx->pause);
}

void decode_release_audio_buffer(demux_ctx_h h, audio_buffer_t *buff)
{
    demux_ctx_t *ctx = (demux_ctx_t *)h;

    if (!ctx->audio_ctx)
    {
        DBG_E("Audio context not allocated\n");
        return;
    }

    queue_push(ctx->audio_ctx->free_buff, (queue_node_t *)buff);
    signal_buffer(ctx->audio_ctx->empty_buff);
}

#ifdef CONFIG_VIDEO
void decode_release_video_buffer(demux_ctx_h h, video_buffer_t *buff)
{
    demux_ctx_t *ctx = (demux_ctx_t *)h;

    if (!ctx->video_ctx)
    {
        DBG_E("Video context not allocated\n");
        return;
    }

    queue_push(ctx->video_ctx->free_buff, (queue_node_t *)buff);
    signal_buffer(ctx->video_ctx->empty_buff);
}
#endif

enum AVSampleFormat decode_get_sample_format(demux_ctx_h h)
{
    demux_ctx_t *ctx = (demux_ctx_t *)h;

    if (!ctx->audio_ctx)
        DBG_E("Audio context not allocated\n");

    return ctx->audio_ctx->dst_fmt;
}

int decode_get_sample_rate(demux_ctx_h h)
{
    demux_ctx_t *ctx = (demux_ctx_t *)h;

    if (!ctx->audio_ctx)
        DBG_E("Audio context not allocated\n");

    return ctx->audio_ctx->codec->sample_rate;
}

int decode_get_channels(demux_ctx_h h)
{
    /* Still only stereo support */
    return 2;
}

ret_code_t decode_get_audio_buffs_info(demux_ctx_h h, int *size, int *count)
{
    demux_ctx_t *ctx = (demux_ctx_t *)h;

    if (!ctx->audio_ctx)
        return L_FAILED;

    *size = ctx->audio_ctx->buff_size;
    *count = AUDIO_BUFFERS;

    return L_OK;
}

audio_buffer_t *decode_get_next_audio_buffer(demux_ctx_h h, ret_code_t *rc)
{
    demux_ctx_t *ctx = (demux_ctx_t *)h;
    audio_buffer_t *abuf;

    if (!ctx->audio_ctx)
    {
        if (rc)
            *rc = L_FAILED;
        DBG_E("Audio context not allocated\n");
        return NULL;
    }

    if (ctx->stop_decode)
    {
        if (rc)
            *rc = L_STOPPING;
        return NULL;
    }
    abuf = (audio_buffer_t *)queue_pop_timed(ctx->audio_ctx->fill_buff, 500);
    if (!abuf)
    {
        if (rc)
            *rc = L_TIMEOUT;
        return NULL;
    }

    if (rc)
        *rc = L_OK;

    return abuf;    
}

#ifdef CONFIG_VIDEO
video_buffer_t *decode_get_free_buffer(demux_ctx_h h)
{
    demux_ctx_t *ctx = (demux_ctx_t *)h;
    video_buffer_t *vbuff;

    if (!ctx || !ctx->video_ctx)
    {
        DBG_E("Video context not allocated\n");
        return NULL;
    }

    if (ctx->stop_decode)
        return NULL;

    vbuff = (video_buffer_t *)queue_pop(ctx->video_ctx->free_buff);

    return vbuff;
}

video_buffer_t *decode_get_next_video_buffer(demux_ctx_h h, ret_code_t *rc)
{
    demux_ctx_t *ctx = (demux_ctx_t *)h;
    video_buffer_t *vbuff = NULL;

    if (!ctx->video_ctx)
    {
        if (rc)
            *rc = L_FAILED;
        DBG_E("Video context not allocated\n");
        return NULL;
    }
    if (ctx->stop_decode)
    {
        if (rc)
            *rc = L_STOPPING;
        return NULL;
    }

    vbuff = (video_buffer_t *)queue_pop_timed(ctx->video_ctx->fill_buff, 500);
    if (!vbuff)
    {
        if (rc)
            *rc = L_TIMEOUT;
        return NULL;
    }

    if (rc)
        *rc = L_OK;

    return vbuff;
}

int decode_is_video(demux_ctx_h h)
{
    demux_ctx_t *ctx = (demux_ctx_t *)h;

    return (ctx->video_ctx != NULL);
}

int devode_get_video_size(demux_ctx_h hd, int *w, int *h)
{
    demux_ctx_t *ctx = (demux_ctx_t *)hd;

    if (!ctx || !ctx->video_ctx)
        return -1;

    *w = ctx->video_ctx->width;
    *h = ctx->video_ctx->height;

    return 0;
}
#endif

int decode_is_audio(demux_ctx_h h)
{
    demux_ctx_t *ctx = (demux_ctx_t *)h;

    return (ctx->audio_ctx != NULL);
}

ret_code_t decode_start(demux_ctx_h h)
{
    ret_code_t rc = L_OK;
    pthread_attr_t attr;
    struct sched_param param;
    demux_ctx_t *ctx = (demux_ctx_t *)h;

    /* Use default scheduler. Set SCHED_RR or SCHED_FIFO request root access */
    pthread_attr_init(&attr);
    param.sched_priority = 5;
    pthread_attr_setschedparam(&attr, &param);
    if (pthread_create(&ctx->task, NULL, read_demux_data, h) < 0)
        rc = L_FAILED;
    pthread_attr_destroy(&attr);
    return rc;
}

int decode_is_task_running(demux_ctx_h h)
{
    demux_ctx_t *ctx = (demux_ctx_t *)h;

    /* Return 1 if thread is running */
    return (pthread_tryjoin_np(ctx->task, NULL) == EBUSY);
}

void decode_stop(demux_ctx_h h)
{
    demux_ctx_t *ctx = (demux_ctx_t *)h;

    ctx->stop_decode = 1;
    if (ctx->audio_ctx)
        signal_buffer(ctx->audio_ctx->full_buff);
#ifdef CONFIG_VIDEO
    if (ctx->video_ctx)
        signal_buffer(ctx->video_ctx->full_buff);
#endif
}

static ret_code_t realloc_audio_buffer(audio_buffer_t *buffer, enum AVSampleFormat dst_fmt)
{
    int dst_linesize;

    av_freep(&buffer->data[0]);
    if (av_samples_alloc(buffer->data, &dst_linesize, 2, buffer->nb_samples, dst_fmt, 16) < 0)
    {
        DBG_E("av_samples_alloc failed\n");
        return L_FAILED;
    }
    buffer->max_nb_samples = buffer->nb_samples;
    buffer->size = dst_linesize;

    DBG_I("Reallocation audio buffer. Maxinum sample: %d size=%d\n", buffer->max_nb_samples, dst_linesize);

    return L_OK;
}

static ret_code_t init_audio_buffers(app_audio_ctx_t *ctx)
{
    int i;
    ret_code_t rc = L_OK;
    int dst_nb_chs;
    int dst_linesize;
    enum AVSampleFormat dst_fmt;
    audio_buffer_t *buff;

    dst_fmt = ctx->codec->sample_fmt;
    if (av_sample_fmt_is_planar(dst_fmt)) 
        dst_fmt = planar_sample_to_same_packed(dst_fmt);

    for (i = 0; i < AUDIO_BUFFERS; i++)
    {
        buff = (audio_buffer_t *)malloc(sizeof(audio_buffer_t));
        if (!buff)
        {
            DBG_E("Memory allocation failed\n");
            rc = L_FAILED;
            break;
        }
        memset(buff, 0, sizeof(audio_buffer_t));
        buff->max_nb_samples = buff->nb_samples = av_rescale_rnd(SAMPLE_PER_BUFFER, ctx->codec->sample_rate,
            ctx->codec->sample_rate, AV_ROUND_UP);
        DBG_V("max_nb_samples = %d(%d)\n", buff->max_nb_samples, av_get_bytes_per_sample(dst_fmt));

        dst_nb_chs = av_get_channel_layout_nb_channels(AV_CH_LAYOUT_STEREO);
        if (av_samples_alloc_array_and_samples(&buff->data, &dst_linesize, dst_nb_chs, buff->nb_samples,
            dst_fmt, 16) < 0)
        {
            rc = L_FAILED;
            DBG_E("Could not allocate destination samples\n");
            break;
        }
        ctx->buff_size = buff->buff_size = dst_linesize;
        DBG_V("Buffer address is %p size=%d\n", buff->data[0], dst_linesize);

        queue_push(ctx->free_buff, (queue_node_t *)buff);
    }

    return rc;
}

static void uninit_audio_buffers(app_audio_ctx_t *ctx)
{
    audio_buffer_t *buff;

    while ((buff = (audio_buffer_t *)queue_pop(ctx->free_buff)) != NULL)
    {
        if (buff->data)
            av_freep(&buff->data[0]);

        free(buff);
    }

    while ((buff = (audio_buffer_t *)queue_pop(ctx->fill_buff)) != NULL)
    {
        if (buff->data)
            av_freep(&buff->data[0]);

        free(buff);
    }
}

static enum AVSampleFormat planar_sample_to_same_packed(enum AVSampleFormat fmt)
{
    enum AVSampleFormat dst_fmt;

#ifdef CONFIG_PC
    switch (fmt)
    {
    case AV_SAMPLE_FMT_U8P:
        dst_fmt = AV_SAMPLE_FMT_U8;
        break;
    case AV_SAMPLE_FMT_S16P:
        dst_fmt = AV_SAMPLE_FMT_S16;
        break;
    case AV_SAMPLE_FMT_S32P:
        dst_fmt = AV_SAMPLE_FMT_S32;
        break;
    case AV_SAMPLE_FMT_FLTP:
        dst_fmt = AV_SAMPLE_FMT_FLT;
        break;
    case AV_SAMPLE_FMT_DBLP:
        dst_fmt = AV_SAMPLE_FMT_DBL;
        break;
    default:
        dst_fmt = fmt;
        break;
    }
#elif CONFIG_RASPBERRY_PI
    dst_fmt = AV_SAMPLE_FMT_S16;    
#endif
    return dst_fmt;
}

static int64_t ts2ms(AVRational *time_base, int64_t ts)
{
    if (ts == AV_NOPTS_VALUE)
        return AV_NOPTS_VALUE;

    return ts * time_base->num * 1000 / time_base->den;
}

static int decode_audio_packet(int *got_frame, int cached, app_audio_ctx_t *ctx, AVFrame *frame, AVPacket *pkt)
{
    int ret;
    int decoded;
    enum AVSampleFormat dst_fmt;
    int dst_linesize;
    size_t unpadded_linesize;
    audio_buffer_t *buff;

    if (ctx->drop_packets)
        return 0;

    if (!ctx->swr)
        return -1;

    /* decode audio frame */
    ret = avcodec_decode_audio4(ctx->codec, frame, got_frame, pkt);
    if (ret < 0)
    {
        DBG_E("Error decoding audio frame (%s)\n", av_err2str(ret));
        return ret;
    }
    /* Some audio decoders decode only part of the packet, and have to be
     * called again with the remainder of the packet data.
     * Sample: fate-suite/lossless-audio/luckynight-partial.shn
     * Also, some decoders might over-read the packet. */
    decoded = FFMIN(ret, pkt->size);

    if (!(*got_frame))
        return decoded;

    DBG_V("audio_frame%s n:%d nb_samples:%d pts:%"PRId64" channels:%d\n", cached ? "(cached)" : "", ctx->frame_count++,
        frame->nb_samples, ts2ms(&ctx->st->time_base, av_frame_get_best_effort_timestamp(frame)),
        av_frame_get_channels(frame));

    buff = (audio_buffer_t *)queue_pop(ctx->free_buff);
    if (!buff)
    {
        timedwait_buffer(ctx->empty_buff, INFINITE_WAIT);
        buff = (audio_buffer_t *)queue_pop(ctx->free_buff);
    }

    unpadded_linesize = frame->nb_samples * av_get_bytes_per_sample(frame->format);

    if(pkt->dts != AV_NOPTS_VALUE)
        buff->pts_ms = ts2ms(&ctx->st->time_base, av_frame_get_best_effort_timestamp(frame));
    else
        buff->pts_ms = AV_NOPTS_VALUE;

    dst_fmt = planar_sample_to_same_packed(ctx->codec->sample_fmt);
    /* compute destination number of samples */
    buff->nb_samples = av_rescale_rnd(swr_get_delay(ctx->swr, ctx->codec->sample_rate) + frame->nb_samples,
        ctx->codec->sample_rate, ctx->codec->sample_rate, AV_ROUND_UP);    
    if (buff->nb_samples > buff->max_nb_samples)
    {
        if (realloc_audio_buffer(buff, dst_fmt))
            return -1;
    }
    ret = swr_convert(ctx->swr, buff->data, buff->nb_samples, (const uint8_t **)frame->extended_data,
        frame->nb_samples);
    if (ret < 0) 
    {
           DBG_E("Error while converting\n");
        return -1;
    }
    unpadded_linesize = av_samples_get_buffer_size(&dst_linesize, 2, ret, dst_fmt, 1);
    if (unpadded_linesize < 0)
    {
           DBG_E("Could not get sample buffer size\n");
           return -1;
    }
    buff->size = (size_t)unpadded_linesize;

    queue_push(ctx->fill_buff, (queue_node_t *)buff);
    signal_buffer(ctx->full_buff);

    return decoded;
}

#ifdef CONFIG_VIDEO
ret_code_t decode_get_pixel_format(demux_ctx_h h, enum AVPixelFormat *pix_fmt)
{
    demux_ctx_t *ctx = (demux_ctx_t *)h;

    if (!ctx || !ctx->video_ctx)
        return L_FAILED;

    *pix_fmt = ctx->video_ctx->pix_fmt;

    return L_OK;
}

ret_code_t decode_get_codec_id(demux_ctx_h h, enum AVCodecID *codec_id)
{
    demux_ctx_t *ctx = (demux_ctx_t *)h;

    if (!ctx || !ctx->video_ctx)
        return L_FAILED;

    *codec_id = ctx->video_ctx->codec_id;

    return L_OK;
}

ret_code_t decode_get_frame_rate(demux_ctx_h h, int *rate, int *scale)
{
    demux_ctx_t *ctx = (demux_ctx_t *)h;

    if (!ctx || !ctx->video_ctx)
        return L_FAILED;

    *rate = ctx->video_ctx->fps_rate;
    *scale = ctx->video_ctx->fps_scale;

    return L_OK;
}

uint8_t *decode_get_codec_extra_data(demux_ctx_h h, int *size)
{
    demux_ctx_t *ctx = (demux_ctx_t *)h;

    if (!ctx || !ctx->video_ctx)
    {
        *size = 0;
        return NULL;
    }
    *size = ctx->video_ctx->codec_ext_data_size;

    return ctx->video_ctx->codec_ext_data;
}

#ifdef CONFIG_VIDEO_HW_DECODE
static int decode_video_packet(int *got_frame, int cached, app_video_ctx_t *ctx, AVFrame *frame, AVPacket *pkt)
{
    video_buffer_t *buff;

    if (!pkt->data || !pkt->size)
    {
        *got_frame = 0;
        return 0;
    }

    *got_frame = 1;
    buff = (video_buffer_t *)queue_pop(ctx->free_buff);
    if (!buff)
    {
        timedwait_buffer(ctx->empty_buff, INFINITE_WAIT);
        buff = (video_buffer_t *)queue_pop(ctx->free_buff);
    }
    if (pkt->size <= buff->buff_size)
    {
        /* TODO. Redesign with out memcpy */
        memcpy(buff->data, pkt->data, pkt->size);
        buff->size = pkt->size;
    }
    else
    {
        /* TODO */
        DBG_F("Packet is too large. Size = %d Need buffer reallocation\n", pkt->size);
    }

    if(pkt->dts != AV_NOPTS_VALUE)
        buff->pts_ms = ts2ms(&ctx->st->time_base, pkt->pts);
    else
        buff->pts_ms = AV_NOPTS_VALUE;

    queue_push(ctx->fill_buff, (queue_node_t *)buff);
    signal_buffer(ctx->full_buff);

    return 0;
}
#else
static int decode_video_packet(int *got_frame, int cached, app_video_ctx_t *ctx, AVFrame *frame, AVPacket *pkt)
{
    int rc;
    video_buffer_t *buff;

    rc = avcodec_decode_video2(ctx->codec, frame, got_frame, pkt);
    if (rc < 0)
    {
        DBG_E("Error decoding video frame (%s)\n", av_err2str(rc));
        return rc;
    }

    if (!(*got_frame))
        return 0;

    if (frame->width != ctx->codec->width || frame->height != ctx->codec->height
        || frame->format != ctx->codec->pix_fmt)
    {
        DBG_E("Error: Width, height and pixel format have to be constant in a rawvideo file, but the width, height or "
            "pixel format of the input video changed:\nold: width = %d, height = %d, format = %s\nnew: width = %d, "
            "height = %d, format = %s\n", ctx->codec->width,ctx->codec-> height,
            av_get_pix_fmt_name(ctx->codec->pix_fmt), frame->width, frame->height, av_get_pix_fmt_name(frame->format));
        return -1;
    }

    DBG_V("video_frame%s n:%d coded_n:%d display_n:%d pts:%s(%"PRId64")\n", cached ? "(cached)" : "",
        ctx->frame_count++, frame->coded_picture_number, frame->display_picture_number,
        av_ts2timestr(av_frame_get_best_effort_timestamp(frame), &ctx->st->time_base),
        ts2ms(&ctx->codec->time_base, av_frame_get_best_effort_timestamp(frame)));

    buff = (video_buffer_t *)queue_pop(ctx->free_buff);
    if (!buff)
    {
        timedwait_buffer(ctx->empty_buff, INFINITE_WAIT);
        buff = (video_buffer_t *)queue_pop(ctx->free_buff);
    }

    rc = sws_scale(ctx->sws, (const uint8_t * const*)frame->data, frame->linesize, 0, ctx->codec->height,
        buff->buffer, buff->linesize);
    if (rc < 0)
    {
        DBG_E("sws_scale failed\n");
        return rc;
    }
    
    if(pkt->dts != AV_NOPTS_VALUE)
        buff->pts_ms = ts2ms(&ctx->st->time_base, av_frame_get_best_effort_timestamp(frame));
    else
        buff->pts_ms = AV_NOPTS_VALUE;

    queue_push(ctx->fill_buff, (queue_node_t *)buff);
    signal_buffer(ctx->full_buff);

    return 0;
}
#endif

static int decode_subtitle_packet(app_video_ctx_t *ctx, AVPacket *pkt)
{
    fprintf(stderr, "--- %s\n", pkt->data);
    return 0;
}
#endif

static int decode_packet(int *got_frame, int cached, demux_ctx_t *ctx, AVFrame *frame, AVPacket *pkt)
{
    int decoded = pkt->size;

    *got_frame = 0;

#ifdef CONFIG_VIDEO
    if (ctx->video_ctx && pkt->stream_index == ctx->video_ctx->stream_idx)
    {
        if (decode_video_packet(got_frame, cached, ctx->video_ctx, frame, pkt) < 0)
            return -1;
    }
    else if (ctx->video_ctx && pkt->stream_index == ctx->video_ctx->subtitle_stream_idx)
    {
        *got_frame = 1;
        if (decode_subtitle_packet(ctx->video_ctx, pkt) < 0)
            return -1;
    }
    else
#endif
    if (ctx->audio_ctx && pkt->stream_index == ctx->audio_ctx->stream_idx)
    {
        decoded = decode_audio_packet(got_frame, cached, ctx->audio_ctx, frame, pkt);
        if (decoded < 0)
            return -1;
    }

    return decoded;
}

static ret_code_t open_codec_context(int *stream_idx, AVFormatContext *fmt_ctx, enum AVMediaType type)
{
    int stream_index;
    AVStream *st;
    AVCodecContext *dec_ctx = NULL;
    AVCodec *dec = NULL;
    AVDictionary *opts = NULL;

    if ((stream_index = av_find_best_stream(fmt_ctx, type, -1, -1, NULL, 0)) < 0)
    {
        if (type == AVMEDIA_TYPE_SUBTITLE)
            DBG_I("Could not find %s stream in input file\n", av_get_media_type_string(type));
        else
            DBG_E("Could not find %s stream in input file\n", av_get_media_type_string(type));
        return L_FAILED;
    }
        
    st = fmt_ctx->streams[stream_index];

    /* find decoder for the stream */
    dec_ctx = st->codec;
    dec = avcodec_find_decoder(dec_ctx->codec_id);
    if (!dec)
    {
        DBG_E("Failed to find %s codec\n", av_get_media_type_string(type));
        return L_FAILED;
    }

    /* Init the decoders, with or without reference counting */
    if (avcodec_open2(dec_ctx, dec, &opts) < 0)
    {
        DBG_E("Failed to open %s codec\n", av_get_media_type_string(type));
        return L_FAILED;
    }
    *stream_idx = stream_index;

    return L_OK;
}

static ret_code_t resampling_config(app_audio_ctx_t *ctx)
{
    ret_code_t ret;
    enum AVSampleFormat dst_fmt;
    char chan_layout_str[32];

    if ((ret = init_audio_buffers(ctx)) != L_OK)
        return ret;    

    /* create resampler context */
    ctx->swr = swr_alloc();
    if (!ctx->swr)
    {
        DBG_E("Could not allocate resampler context\n");
        return AVERROR(ENOMEM);
    }
    av_get_channel_layout_string(chan_layout_str, sizeof(chan_layout_str), ctx->codec->channels,
        ctx->codec->channel_layout);
    DBG_I("Source channel layout is: %s format: %s sample rate:%d\n",
        chan_layout_str, av_get_sample_fmt_name(ctx->codec->sample_fmt),
        ctx->codec->sample_rate);
    /* Source layout */
    av_opt_set_int(ctx->swr, "in_channel_layout",ctx->codec->channel_layout, 0);
    av_opt_set_int(ctx->swr, "in_sample_rate", ctx->codec->sample_rate, 0);
    av_opt_set_sample_fmt(ctx->swr, "in_sample_fmt", ctx->codec->sample_fmt, 0);
    /* Destination layout */
    /* Still only stereo output */
    av_opt_set_int(ctx->swr, "out_channel_layout", AV_CH_LAYOUT_STEREO, 0);
    av_opt_set_int(ctx->swr, "out_sample_rate", ctx->codec->sample_rate, 0);
    dst_fmt = planar_sample_to_same_packed(ctx->codec->sample_fmt);
    av_opt_set_sample_fmt(ctx->swr, "out_sample_fmt", dst_fmt, 0);

    /* initialize the resampling context */
    if (swr_init(ctx->swr) < 0)
    {
        DBG_E("Failed to initialize the resampling context\n");
        return L_FAILED;
    }

    ctx->dst_fmt = dst_fmt;

    return ret;
}

static void *read_demux_data(void *args)
{
    AVPacket pkt;
    int ret;
    int got_frame;
    AVFrame *frame = NULL;
    demux_ctx_t *ctx = (demux_ctx_t *)args;

    if (decode_is_video(ctx))
    {
        DBG_I("Waiting\n");
        msleep_wait(ctx->pause, INFINITE_WAIT);
    }
    DBG_I("Start demux task\n");

    frame = av_frame_alloc();
    if (!frame)
    {
        DBG_E("Could not allocate frame\n");
        return NULL;
    }

    /* initialize packet, set data to NULL, let the demuxer fill it */
    av_init_packet(&pkt);
    pkt.data = NULL;
    pkt.size = 0;

    /* read frames from the file */
    while (av_read_frame(ctx->fmt_ctx, &pkt) >= 0 && !ctx->stop_decode)
    {
        AVPacket orig_pkt = pkt;
        do
        {
            ret = decode_packet(&got_frame, 0, ctx, frame, &pkt);
            if (ret < 0)
                break;
            pkt.data += ret;
            pkt.size -= ret;
        }
        while (pkt.size > 0);
        av_free_packet(&orig_pkt);
    }

    /* flush cached frames */
    pkt.data = NULL;
    pkt.size = 0;
    do
    {
        decode_packet(&got_frame, 1, ctx, frame, &pkt);
    }
    while (got_frame);

    printf("\n");

    av_frame_free(&frame);

    DBG_I("Stop demux task\n");

    return NULL;
}

