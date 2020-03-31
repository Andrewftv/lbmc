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

    queue_h free_buff;
    queue_h fill_buff;

    /* Requested buffers parameters */
    int amount;
    int size;
    int align;

    int buff_allocated;
    int buff_align;
    int buff_size;
    int frame_count;
    int stream_idx;
    int audio_streams;

    /* Destination format after resampling */
    enum AVSampleFormat dst_fmt;
    /* Destination sample rate */
    int sample_rate;
} app_audio_ctx_t;

#ifdef CONFIG_VIDEO
typedef struct {
#ifndef CONFIG_VIDEO_HW_DECODE
    AVCodecContext *codec;
    struct SwsContext *sws;
#endif
    AVStream *st;
    enum AVCodecID codec_id;

    int width;
    int height;
    int fps_rate;
    int fps_scale;
    uint8_t *codec_ext_data;
    int codec_ext_data_size;
    enum AVPixelFormat pix_fmt;

    queue_h free_buff;
    queue_h fill_buff;

    /* Requested buffers parameters */
    int amount;
    int size;
    int align;

    int buff_allocated;
    int buff_align;
    int buff_size;
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
    pthread_mutex_t lock;

    pthread_t task;
    int stop_decode;
    /* Current playing PTS in ms */
    int64_t curr_pts;
    int show_info;
} demux_ctx_t;

/* Prototypes */
static enum AVSampleFormat planar_sample_to_same_packed(enum AVSampleFormat fmt);
static void *read_demux_data(void *ctx);
static ret_code_t open_codec_context(int *stream_idx, AVFormatContext *fmt_ctx, enum AVMediaType type);
static ret_code_t resampling_config(app_audio_ctx_t *ctx, int reinit);
static void uninit_audio_buffers(app_audio_ctx_t *ctx);

static int64_t ts2ms(AVRational *time_base, int64_t ts)
{
    if (ts == AV_NOPTS_VALUE)
        return AV_NOPTS_VALUE;

    return ts * time_base->num * 1000 / time_base->den;
}

void decode_lock(demux_ctx_h h)
{
    demux_ctx_t *ctx = (demux_ctx_t *)h;

    if (!ctx)
    {
        DBG_E("Can not lock decoder\n");
        return;
    }
    pthread_mutex_lock(&ctx->lock);
}

void decode_unlock(demux_ctx_h h)
{
    demux_ctx_t *ctx = (demux_ctx_t *)h;

    if (!ctx)
    {
        DBG_E("Can not unlock decoder\n");
        return;
    }
    pthread_mutex_unlock(&ctx->lock);
}

static int get_stream_duration(demux_ctx_t *ctx)
{
    if (!ctx || !ctx->fmt_ctx)
        return 0;

    return ctx->fmt_ctx->duration / 1000;
}

ret_code_t decode_seek(demux_ctx_h h, seek_direction_t dir, int seek_time_ms, int64_t *next_pts)
{
    demux_ctx_t *ctx = (demux_ctx_t *)h;
    int64_t pts, pts_ms;
    int duration;

    if (!ctx)
    {
        DBG_E("Incorrect context\n");
        return L_FAILED;
    }

    duration = get_stream_duration(ctx);
    if (!duration)
    {
        DBG_E("Seek on zero length stream\n");
        return L_FAILED;
    }

    pts_ms = decode_get_current_playing_pts(ctx);
    switch (dir)
    {
    case L_SEEK_FORWARD:
        if ((pts_ms + seek_time_ms) < duration)
            pts_ms += seek_time_ms;
        else
            pts_ms = duration;
        break;
    case L_SEEK_BACKWARD:
        pts_ms -= seek_time_ms;
        if (pts_ms < 0)
            return L_FAILED;
        break;
    default:
        DBG_E("Incorect seek direction\n");
        return L_FAILED;
    }

    pts = pts_ms * (AV_TIME_BASE / 1000);
    DBG_I("Seek for PTS=%lld(%lld)\n", pts, pts_ms);

    if (avformat_seek_file(ctx->fmt_ctx, -1, INT64_MIN, pts, INT64_MAX,
        (dir == L_SEEK_BACKWARD) ? AVSEEK_FLAG_BACKWARD : 0) < 0)
    {
        DBG_E("av_seek_frame failed\n");
        return L_FAILED;
    }
    release_all_buffers(ctx);

    if (next_pts)
        *next_pts = pts_ms;

    return L_OK;
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

static int get_first_audio_stream(AVFormatContext *fmt)
{
    int index = -1, i;
    AVStream *st;

    for (i = 0; i < fmt->nb_streams; i++)
    {
        st = fmt->streams[i];
        if (st->codec->codec_type == AVMEDIA_TYPE_AUDIO)
        {
            index = i;
            break;
        }
    }

    return index;
}

static ret_code_t reopen_audio_stream(demux_ctx_t *ctx, int cur_inx, int new_inx)
{
    AVCodec *dec;
    AVStream *st;

    if (!ctx || !ctx->fmt_ctx)
    {
        DBG_E("Invalid input parameters\n");
        return L_FAILED;
    }

    if (cur_inx == new_inx)
        return L_OK; /* Nothing to do. Already opened. */

    /* Close current audio stream */
    st = ctx->fmt_ctx->streams[cur_inx];
    avcodec_close(st->codec);

    st = ctx->fmt_ctx->streams[new_inx];
    if (!st)
    {
        DBG_E("Stream for index %d not fount\n", new_inx);
        return L_FAILED;
    }
    dec = avcodec_find_decoder(st->codec->codec_id);
    if (!dec)
    {
        DBG_E("Unable to find decoder %d\n", st->codec->codec_id);
        return L_FAILED;
    }
    if (avcodec_open2(st->codec, dec, NULL) < 0)
    {
        DBG_E("Unable to open codec\n");
        return L_FAILED;
    }

    return L_OK;
}

void decode_set_requested_buffers_param(demux_ctx_h h, media_buffer_type_t type, int amount, int size, int align)
{
    demux_ctx_t *ctx = (demux_ctx_t *)h;

    if (!ctx)
        return;

    switch (type)
    {
    case MB_AUDIO_TYPE:
        if (!decode_is_audio(h))
            break;
        ctx->audio_ctx->amount = amount;
        ctx->audio_ctx->size = size;
        ctx->audio_ctx->align = align;
        break;
#ifdef CONFIG_VIDEO
    case MB_VIDEO_TYPE:
        if (!decode_is_video(h))
            break;
        ctx->video_ctx->amount = amount;
        ctx->video_ctx->size = size;
        ctx->video_ctx->align = align;
        break;
#endif
    default:
        break;
    }
}

int64_t decode_get_current_playing_pts(demux_ctx_h h)
{
    demux_ctx_t *ctx = (demux_ctx_t *)h;

    if (!ctx)
    {
        DBG_E("Incorrect context\n");
        return - 1;
    }

    return ctx->curr_pts;
}

void decode_set_current_playing_pts(demux_ctx_h h, int64_t pts)
{
    demux_ctx_t *ctx = (demux_ctx_t *)h;

    if (!ctx)
    {
        DBG_E("Incorrect context\n");
        return;
    }

    ctx->curr_pts = pts;
}

ret_code_t decode_next_audio_stream(demux_ctx_h h)
{
    demux_ctx_t *ctx = (demux_ctx_t *)h;
    int index;
    AVStream *st;
    AVCodecContext *dec_ctx = NULL;
    AVCodec *dec = NULL;
    AVDictionary *opts = NULL;
    ret_code_t rc = L_OK;

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

    decode_lock(ctx);

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
        rc = L_FAILED;
        goto Exit;
    }

    /* Init the decoders, with or without reference counting */
    if (avcodec_open2(dec_ctx, dec, &opts) < 0)
    {
        DBG_E("Failed to open %s codec\n", av_get_media_type_string(AVMEDIA_TYPE_AUDIO));
        rc = L_FAILED;
        goto Exit;
    }

    ctx->audio_ctx->stream_idx = index;
    ctx->audio_ctx->codec = st->codec;

    if (resampling_config(ctx->audio_ctx, 1))
        rc = L_FAILED;

Exit:
    decode_unlock(ctx);

    return rc;
}

#ifdef CONFIG_VIDEO
ret_code_t decode_setup_video_buffers(demux_ctx_h h, int amount, int align, int len)
{
    demux_ctx_t *ctx = (demux_ctx_t *)h;
    app_video_ctx_t *vctx;
    media_buffer_t *vbuff;
    int i;

    if (ctx->video_ctx->amount != -1)
        amount = ctx->video_ctx->amount;

    vctx = ctx->video_ctx;
#ifdef CONFIG_VIDEO_HW_DECODE
    for (i = 0; i < amount; i++)
    {
        vbuff = (media_buffer_t *)malloc(sizeof(media_buffer_t));
        memset(vbuff, 0, sizeof(media_buffer_t));
        vbuff->type = MB_VIDEO_TYPE;
        if (posix_memalign((void **)&vbuff->s.video.data, align, len))
        {
            DBG_E("Memory allocation failed\n");
            return L_FAILED;
        }
        vbuff->s.video.buff_size = len;
        DBG_V("Video buffer %p\n", vbuff->s.video.data);

        queue_push(vctx->free_buff, (queue_node_t *)vbuff);
    }
#else
    /* Allocate destination image with same resolution and RGBA pixel format */
    for (i = 0; i < amount; i++)
    {
        int rc;

        vbuff = (media_buffer_t *)malloc(sizeof(media_buffer_t));
        memset(vbuff, 0, sizeof(media_buffer_t));
        vbuff->type = MB_VIDEO_TYPE;

        rc = av_image_alloc(vbuff->s.video.buffer, vbuff->s.video.linesize, vctx->codec->width, vctx->codec->height,
                AV_PIX_FMT_RGBA, align);
        if (rc < 0)
        {
            DBG_E("Could not allocate destination video buffer\n");
            return L_FAILED;
        }
        vbuff->size = len = rc;

        queue_push(vctx->free_buff, (queue_node_t *)vbuff);
    }
    /* Create scale context */
    vctx->sws = sws_getContext(vctx->codec->width, vctx->codec->height, vctx->codec->pix_fmt, vctx->codec->width,
            vctx->codec->height, AV_PIX_FMT_RGBA, SWS_BILINEAR, NULL, NULL, NULL);
    if (!vctx->sws)
    {
        DBG_E("Can not allocate scale format\n");
        return L_FAILED;
    }
#endif
    vctx->buff_allocated = amount;
    vctx->buff_align = align;
    vctx->buff_size = len;

    DBG_I("Allocated %d video buffers, size=%d alignment=%d\n", vctx->buff_allocated, vctx->buff_size,
        vctx->buff_align);

    return L_OK;
}
#endif

ret_code_t decode_init(demux_ctx_h *h, char *src_file, int show_info)
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
    memset(ctx, 0, sizeof(demux_ctx_t));
    ctx->show_info = show_info;
    msleep_init(&ctx->pause);
    pthread_mutex_init(&ctx->lock, NULL);
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
    DBG_I("Format name: %s\n", ctx->fmt_ctx->iformat->name);
    if (!open_codec_context(&stream_index, ctx->fmt_ctx, AVMEDIA_TYPE_VIDEO))
    {
        app_video_ctx_t *vctx;
        AVStream *video_stream = NULL;

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
       
        DBG_I("Video stream was found. index=%d\n", stream_index); 

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
        int first_index;

        streams++;

        actx = (app_audio_ctx_t *)malloc(sizeof(app_audio_ctx_t));
        if (!actx)
        {
            DBG_E("Can not alloc demuxer audio context\n");
            return L_FAILED;
        }
        ctx->audio_ctx = actx;

        memset(actx, 0, sizeof(app_audio_ctx_t));
        /* Get first stream instead of "best" */
        first_index = get_first_audio_stream(ctx->fmt_ctx);
        if (reopen_audio_stream(ctx, stream_index, first_index) != L_OK)
            return L_FAILED;

        stream_index = first_index;
        actx->stream_idx = stream_index;

        queue_init(&actx->free_buff);
        queue_init(&actx->fill_buff);

        audio_stream = ctx->fmt_ctx->streams[stream_index];
        actx->codec = audio_stream->codec;
        actx->st = audio_stream;

        actx->audio_streams = get_audio_streams_count(ctx->fmt_ctx);
        DBG_I("Audio stream was found. Index = %d total %d\n", stream_index, actx->audio_streams);

        if (resampling_config(actx, 0))
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
        media_buffer_t *buff;
        app_video_ctx_t *vctx = ctx->video_ctx;

#ifdef CONFIG_VIDEO_HW_DECODE
        while ((buff = (media_buffer_t *)queue_pop(vctx->free_buff)) != NULL)
        {
            free(buff->s.video.data);
            free(buff);
        }
        while ((buff = (media_buffer_t *)queue_pop(vctx->fill_buff)) != NULL)
        {
            free(buff->s.video.data);
            free(buff);
        }
#else
        if (vctx->codec)
            avcodec_close(vctx->codec);
        if (vctx->sws)
            sws_freeContext(vctx->sws);

        while ((buff = (media_buffer_t *)queue_pop(vctx->free_buff)) != NULL)
        {
            av_freep(&buff->s.video.buffer[0]);
            free(buff);
        }
        while ((buff = (media_buffer_t *)queue_pop(vctx->fill_buff)) != NULL)
        {
            av_freep(&buff->s.video.buffer[0]);
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
    pthread_mutex_destroy(&ctx->lock);
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

void decode_release_audio_buffer(demux_ctx_h h, media_buffer_t *buff)
{
    demux_ctx_t *ctx = (demux_ctx_t *)h;

    if (!ctx->audio_ctx)
    {
        DBG_E("Audio context not allocated\n");
        return;
    }

    queue_push(ctx->audio_ctx->free_buff, (queue_node_t *)buff);
}

#ifdef CONFIG_VIDEO
void decode_release_video_buffer(demux_ctx_h h, media_buffer_t *buff)
{
    demux_ctx_t *ctx = (demux_ctx_t *)h;

    if (!ctx->video_ctx)
    {
        DBG_E("Video context not allocated\n");
        return;
    }

    queue_push(ctx->video_ctx->free_buff, (queue_node_t *)buff);
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

ret_code_t decode_get_audio_buffs_info(demux_ctx_h h, int *size, int *count, int *align)
{
    demux_ctx_t *ctx = (demux_ctx_t *)h;

    if (!ctx->audio_ctx)
        return L_FAILED;

    *size = ctx->audio_ctx->buff_size;
    *count = ctx->audio_ctx->buff_allocated;
    *align = ctx->audio_ctx->buff_align;

    return L_OK;
}

media_buffer_t *decode_get_free_audio_buffer(demux_ctx_h h)
{
    demux_ctx_t *ctx = (demux_ctx_t *)h;
    media_buffer_t *abuff;

    if (!ctx || !ctx->audio_ctx)
    {
        DBG_E("Video context not allocated\n");
        return NULL;
    }

    abuff = (media_buffer_t *)queue_pop(ctx->audio_ctx->free_buff);

    return abuff;
}

media_buffer_t *decode_get_next_audio_buffer(demux_ctx_h h, ret_code_t *rc)
{
    demux_ctx_t *ctx = (demux_ctx_t *)h;
    media_buffer_t *abuf;

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
    abuf = (media_buffer_t *)queue_pop_timed(ctx->audio_ctx->fill_buff, 500);
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
media_buffer_t *decode_get_free_video_buffer(demux_ctx_h h)
{
    demux_ctx_t *ctx = (demux_ctx_t *)h;
    media_buffer_t *vbuff;

    if (!ctx || !ctx->video_ctx)
    {
        DBG_E("Video context not allocated\n");
        return NULL;
    }

    vbuff = (media_buffer_t *)queue_pop(ctx->video_ctx->free_buff);

    return vbuff;
}

media_buffer_t *decode_get_next_video_buffer(demux_ctx_h h, ret_code_t *rc)
{
    demux_ctx_t *ctx = (demux_ctx_t *)h;
    media_buffer_t *vbuff = NULL;

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

    vbuff = (media_buffer_t *)queue_pop_timed(ctx->video_ctx->fill_buff, 500);
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
#else
int decode_is_video(demux_ctx_h h)
{
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
    demux_ctx_t *ctx = (demux_ctx_t *)h;

    /* Use default scheduler. Set SCHED_RR or SCHED_FIFO request root access */
    if (pthread_create(&ctx->task, NULL, read_demux_data, h) < 0)
        rc = L_FAILED;
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
}

static ret_code_t realloc_audio_buffer(media_buffer_t *buffer, enum AVSampleFormat dst_fmt)
{
    int dst_linesize;

    av_freep(&buffer->s.audio.data[0]);
    if (av_samples_alloc(buffer->s.audio.data, &dst_linesize, 2, buffer->s.audio.nb_samples, dst_fmt, 16) < 0)
    {
        DBG_E("av_samples_alloc failed\n");
        return L_FAILED;
    }
    buffer->s.audio.max_nb_samples = buffer->s.audio.nb_samples;
    buffer->size = dst_linesize;

    DBG_I("Reallocation audio buffer. Maxinum sample: %d size=%d\n", buffer->s.audio.max_nb_samples, dst_linesize);

    return L_OK;
}

ret_code_t decode_setup_audio_buffers(demux_ctx_h h, int amount, int align, int len)
{
    demux_ctx_t *ctx = (demux_ctx_t *)h;
    int i;
    ret_code_t rc = L_OK;
    int dst_nb_chs;
    int dst_linesize;
    enum AVSampleFormat dst_fmt;
    media_buffer_t *buff;

    if (ctx->audio_ctx->amount != -1)
        amount = ctx->audio_ctx->amount;

    dst_fmt = ctx->audio_ctx->codec->sample_fmt;
    if (av_sample_fmt_is_planar(dst_fmt)) 
        dst_fmt = planar_sample_to_same_packed(dst_fmt);

    for (i = 0; i < amount; i++)
    {
        buff = (media_buffer_t *)malloc(sizeof(media_buffer_t));
        if (!buff)
        {
            DBG_E("Memory allocation failed\n");
            rc = L_FAILED;
            break;
        }
        memset(buff, 0, sizeof(media_buffer_t));
        buff->type = MB_AUDIO_TYPE;
        buff->s.audio.max_nb_samples = buff->s.audio.nb_samples =
            av_rescale_rnd(SAMPLE_PER_BUFFER, ctx->audio_ctx->codec->sample_rate, ctx->audio_ctx->codec->sample_rate,
            AV_ROUND_UP);
        DBG_V("max_nb_samples = %d(%d)\n", buff->s.audio.max_nb_samples, av_get_bytes_per_sample(dst_fmt));

        dst_nb_chs = av_get_channel_layout_nb_channels(AV_CH_LAYOUT_STEREO);
        if (av_samples_alloc_array_and_samples(&buff->s.audio.data, &dst_linesize, dst_nb_chs, buff->s.audio.nb_samples,
            dst_fmt, align) < 0)
        {
            rc = L_FAILED;
            DBG_E("Could not allocate destination samples\n");
            break;
        }
        ctx->audio_ctx->buff_size = buff->s.audio.buff_size = len = dst_linesize;
        DBG_V("Buffer address is %p size=%d\n", buff->s.audio.data[0], dst_linesize);

        queue_push(ctx->audio_ctx->free_buff, (queue_node_t *)buff);
    }

    ctx->audio_ctx->buff_allocated = amount;
    ctx->audio_ctx->buff_align = align;
    ctx->audio_ctx->buff_size = len;

    DBG_I("Allocated %d audio buffers, size=%d alignment=%d\n", ctx->audio_ctx->buff_allocated,
        ctx->audio_ctx->buff_size, ctx->audio_ctx->buff_align);

    return rc;
}

void release_all_buffers(demux_ctx_h h)
{
    demux_ctx_t *ctx = (demux_ctx_t *)h;
    media_buffer_t *buff;

    if (decode_is_audio(ctx))
        while ((buff = (media_buffer_t *)queue_pop(ctx->audio_ctx->fill_buff)) != NULL)
            queue_push(ctx->audio_ctx->free_buff, (queue_node_t *)buff);

#ifdef CONFIG_VIDEO
    if (decode_is_video(ctx))
        while ((buff = (media_buffer_t *)queue_pop(ctx->video_ctx->fill_buff)) != NULL)
            queue_push(ctx->video_ctx->free_buff, (queue_node_t *)buff);
#endif
}

static void uninit_audio_buffers(app_audio_ctx_t *ctx)
{
    media_buffer_t *buff;

    while ((buff = (media_buffer_t *)queue_pop(ctx->free_buff)) != NULL)
    {
        if (buff->s.audio.data)
            av_freep(&buff->s.audio.data[0]);

        free(buff);
    }

    while ((buff = (media_buffer_t *)queue_pop(ctx->fill_buff)) != NULL)
    {
        if (buff->s.audio.data)
            av_freep(&buff->s.audio.data[0]);

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

static int decode_audio_packet(int *got_frame, int cached, app_audio_ctx_t *ctx, AVFrame *frame, AVPacket *pkt)
{
    int ret;
    int decoded;
    enum AVSampleFormat dst_fmt;
    int dst_linesize;
    size_t unpadded_linesize;
    media_buffer_t *buff;

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

    buff = (media_buffer_t *)queue_pop_timed(ctx->free_buff, QUEUE_INFINITE_WAIT);
    unpadded_linesize = frame->nb_samples * av_get_bytes_per_sample(frame->format);

    if(pkt->pts != AV_NOPTS_VALUE)
        buff->pts_ms = ts2ms(&ctx->st->time_base, pkt->pts);
    else
        buff->pts_ms = AV_NOPTS_VALUE;

    dst_fmt = planar_sample_to_same_packed(ctx->codec->sample_fmt);
    /* compute destination number of samples */
    buff->s.audio.nb_samples = av_rescale_rnd(swr_get_delay(ctx->swr, ctx->codec->sample_rate) + frame->nb_samples,
        ctx->codec->sample_rate, ctx->codec->sample_rate, AV_ROUND_UP);    
    if (buff->s.audio.nb_samples > buff->s.audio.max_nb_samples)
    {
        if (realloc_audio_buffer(buff, dst_fmt))
            return -1;
    }
    ret = swr_convert(ctx->swr, buff->s.audio.data, buff->s.audio.nb_samples, (const uint8_t **)frame->extended_data,
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

    return decoded;
}

#ifdef CONFIG_VIDEO
ret_code_t decode_get_video_buffs_info(demux_ctx_h h, int *size, int *count, int *align)
{
    demux_ctx_t *ctx = (demux_ctx_t *)h;

    if (!ctx->video_ctx)
        return L_FAILED;

    *size = ctx->video_ctx->buff_size;
    *count = ctx->video_ctx->buff_allocated;
    *align = ctx->video_ctx->buff_align;

    return L_OK;
}

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
    media_buffer_t *buff;

    if (!pkt->data || !pkt->size)
    {
        *got_frame = 0;
        return 0;
    }

    *got_frame = 1;
    buff = (media_buffer_t *)queue_pop_timed(ctx->free_buff, INFINITE_WAIT);
    if (pkt->size <= buff->s.video.buff_size)
    {
        /* TODO. Redesign with out memcpy */
        memcpy(buff->s.video.data, pkt->data, pkt->size);
        buff->size = pkt->size;
    }
    else
    {
        int saved = 0, size = pkt->size, to_copy;

        DBG_V("Packet is too large. Size = %d\n", pkt->size);
        do
        {
            to_copy = (size > buff->s.video.buff_size) ? buff->s.video.buff_size : size;
            DBG_V("Part of buffer: %d bytes size=%d\n", to_copy, size);
            memcpy(buff->s.video.data, &pkt->data[saved], to_copy);
            buff->size = to_copy;
            saved += to_copy;
            size -= to_copy;
            if (size)
            {
                buff->status = MB_CONTINUE_STATUS;
                if(pkt->pts != AV_NOPTS_VALUE)
                    buff->pts_ms = ts2ms(&ctx->st->time_base, pkt->pts);
                else
                    buff->pts_ms = AV_NOPTS_VALUE;

                if(pkt->dts != AV_NOPTS_VALUE)
                    buff->dts_ms = ts2ms(&ctx->st->time_base, pkt->dts);
                else
                    buff->dts_ms = AV_NOPTS_VALUE;
                if (buff->dts_ms == -1)
                    buff->dts_ms = AV_NOPTS_VALUE;

                queue_push(ctx->fill_buff, (queue_node_t *)buff);

                buff = (media_buffer_t *)queue_pop_timed(ctx->free_buff, INFINITE_WAIT);
            }
        } while (size);
    }

    buff->status = MB_FULL_STATUS;
    if(pkt->pts != AV_NOPTS_VALUE)
        buff->pts_ms = ts2ms(&ctx->st->time_base, pkt->pts);
    else
        buff->pts_ms = AV_NOPTS_VALUE;

    if(pkt->dts != AV_NOPTS_VALUE)
        buff->dts_ms = ts2ms(&ctx->st->time_base, pkt->dts);
    else
        buff->dts_ms = AV_NOPTS_VALUE;
    if (buff->dts_ms == -1)
        buff->dts_ms = AV_NOPTS_VALUE;

    queue_push(ctx->fill_buff, (queue_node_t *)buff);

    return 0;
}
#else
static int decode_video_packet(int *got_frame, int cached, app_video_ctx_t *ctx, AVFrame *frame, AVPacket *pkt)
{
    int rc;
    media_buffer_t *buff;

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

    buff = (media_buffer_t *)queue_pop_timed(ctx->free_buff, QUEUE_INFINITE_WAIT);

    rc = sws_scale(ctx->sws, (const uint8_t * const*)frame->data, frame->linesize, 0, ctx->codec->height,
        buff->s.video.buffer, buff->s.video.linesize);
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

static ret_code_t resampling_config(app_audio_ctx_t *ctx, int reinit)
{
    ret_code_t ret = L_OK;
    enum AVSampleFormat dst_fmt;
    char chan_layout_str[32];

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
    if (!reinit)
    {
        av_opt_set_int(ctx->swr, "out_sample_rate", ctx->codec->sample_rate, 0);
        dst_fmt = planar_sample_to_same_packed(ctx->codec->sample_fmt);
        av_opt_set_sample_fmt(ctx->swr, "out_sample_fmt", dst_fmt, 0);

        ctx->dst_fmt = dst_fmt;
        ctx->sample_rate = ctx->codec->sample_rate;
    }
    else
    {
        av_opt_set_int(ctx->swr, "out_sample_rate", ctx->sample_rate, 0);
        av_opt_set_sample_fmt(ctx->swr, "out_sample_fmt", ctx->dst_fmt, 0);
    }
    /* initialize the resampling context */
    if (swr_init(ctx->swr) < 0)
    {
        DBG_E("Failed to initialize the resampling context\n");
        return L_FAILED;
    }

    return ret;
}

void print_stream_info(demux_ctx_h h)
{
    demux_ctx_t *ctx = (demux_ctx_t *)h;
    int count, i, duration = 0;
    int hour, min, sec, curr_hour, curr_min, curr_sec;
    int temp;

    if (!ctx || !ctx->show_info)
        return;

    duration = get_stream_duration(ctx);
    if (duration)
    {
        /* Print the progress */
        count = ctx->curr_pts * 10 / duration;
        fprintf(stderr, "[");
        for (i = 0; i < count; i++)
            fprintf(stderr, "=");
        fprintf(stderr, ">");
        for (i = 0; i < 10 - count -1; i++)
            fprintf(stderr, " ");
        fprintf(stderr, "] ");
    }

    temp = ctx->curr_pts / 1000;
    curr_sec = temp % 60;
    temp /= 60;
    curr_min = temp % 60;
    temp /= 60;
    curr_hour = temp;

    temp = duration / 1000;
    sec = temp % 60;
    temp /= 60;
    min = temp % 60;
    temp /= 60;
    hour = temp;

#ifdef CONFIG_VIDEO
    if (ctx->video_ctx && ctx->audio_ctx)
    {
        fprintf(stderr, "V-%02d:%02d  A-%02d:%02d TS-%02d:%02d:%02d/%02d:%02d:%02d          \r",
            queue_count(ctx->video_ctx->fill_buff), ctx->video_ctx->buff_allocated,
            queue_count(ctx->audio_ctx->fill_buff), ctx->audio_ctx->buff_allocated, curr_hour, curr_min, curr_sec, hour,
            min, sec);
    }
    else if (ctx->video_ctx)
    {
        fprintf(stderr, "V-%02d:%02d TS-%02d:%02d:%02d/%02d:%02d:%02d          \r",
            queue_count(ctx->video_ctx->fill_buff), ctx->video_ctx->buff_allocated, curr_hour, curr_min, curr_sec, hour,
            min, sec);
    }
    else
#endif
    if (ctx->audio_ctx)
    {
        fprintf(stderr, "A-%02d:%02d TS-%02d:%02d:%02d/%02d:%02d:%02d          \r",
            queue_count(ctx->audio_ctx->fill_buff), ctx->audio_ctx->buff_allocated, curr_hour, curr_min, curr_sec, hour,
            min, sec);
    }
}

static void *read_demux_data(void *args)
{
    AVPacket pkt;
    int ret;
    int got_frame;
    AVFrame *frame = NULL;
    demux_ctx_t *ctx = (demux_ctx_t *)args;

    DBG_I("Waiting\n");
    msleep_wait(ctx->pause, MSLEEP_INFINITE_WAIT);
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
    while (!ctx->stop_decode)
    {
        AVPacket orig_pkt;

        decode_lock(ctx);
        if (av_read_frame(ctx->fmt_ctx, &pkt) < 0)
        {
            decode_unlock(ctx);
            break;
        }
        decode_unlock(ctx);
        orig_pkt = pkt;
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

    printf("\n");

    av_frame_free(&frame);

    DBG_I("Stop demux task\n");

    return NULL;
}
