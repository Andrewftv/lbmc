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
#ifndef __LBMC_DECODE_H__
#define __LBMC_DECODE_H__

#define AUDIO_BUFFERS 64
#define VIDEO_BUFFERS 20

#include <libavcodec/avcodec.h>
#include <libavutil/pixfmt.h>
#include "errors.h"
#include "queue.h"

typedef void* demux_ctx_h;

typedef enum {
    MB_UNKNOUN_TYPE = 0,
    MB_AUDIO_TYPE = 1,
    MB_VIDEO_TYPE = 2,
    MB_SUBS_TYPE = 3
} media_buffer_type_t;

typedef enum {
    MB_FULL_STATUS = 0,
    MB_CONTINUE_STATUS
} media_buffer_status_t;

typedef struct {
    uint8_t **data;
    size_t buff_size;
    int nb_samples;
    int max_nb_samples;
} audio_part_t;

typedef struct {
#ifdef CONFIG_VIDEO_HW_DECODE
    uint8_t *data;
    int buff_size;  /* Allocation size */
#else
    uint8_t *buffer[4];
    int linesize[4];
#endif
} video_part_t;

typedef struct {
    queue_node_t node;
    media_buffer_type_t type;
    union {
        audio_part_t audio;
        video_part_t video;
    } s;

    /* Common part */
    int size;
    int64_t pts_ms; /* PTS in ms from a stream begin */
    int64_t dts_ms; /* DTS in ms from a stream begin */
    media_buffer_status_t status;
    void *app_data;
} media_buffer_t;

ret_code_t decode_init(demux_ctx_h *h, char *src_file);
void decode_uninit(demux_ctx_h h);
void decode_start_read(demux_ctx_h h);

/* Access to buffers by player */
media_buffer_t *decode_get_free_audio_buffer(demux_ctx_h h);
media_buffer_t *decode_get_next_audio_buffer(demux_ctx_h h, ret_code_t *rc);
void decode_release_audio_buffer(demux_ctx_h h, media_buffer_t *buff);
void release_all_buffers(demux_ctx_h h);

int decode_is_audio(demux_ctx_h h);
ret_code_t decode_next_audio_stream(demux_ctx_h h);

#ifdef CONFIG_VIDEO
media_buffer_t *decode_get_free_video_buffer(demux_ctx_h h);
media_buffer_t *decode_get_next_video_buffer(demux_ctx_h h, ret_code_t *rc);
void decode_release_video_buffer(demux_ctx_h h, media_buffer_t *buff);
int decode_is_video(demux_ctx_h h);
int devode_get_video_size(demux_ctx_h hd, int *w, int *h);
ret_code_t decode_get_pixel_format(demux_ctx_h h, enum AVPixelFormat *pix_fmt);
ret_code_t decode_get_codec_id(demux_ctx_h h, enum AVCodecID *codec_id);
ret_code_t decode_get_frame_rate(demux_ctx_h h, int *rate, int *scale);  
uint8_t *decode_get_codec_extra_data(demux_ctx_h h, int *size);
ret_code_t decode_setup_video_buffers(demux_ctx_h h, int amount, int align, int len);
#endif

/* Output audio format. Used for a player configuration */
enum AVSampleFormat decode_get_sample_format(demux_ctx_h h);
int decode_get_sample_rate(demux_ctx_h h);
int decode_get_channels(demux_ctx_h h);
ret_code_t decode_get_audio_buffs_info(demux_ctx_h h, int *size, int *cont);

/* Demux/decode task */
ret_code_t decode_start(demux_ctx_h h);
int decode_is_task_running(demux_ctx_h h);
void decode_stop(demux_ctx_h h);

#endif
