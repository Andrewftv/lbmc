#ifndef __LBMC_DECODE_H__
#define __LBMC_DECODE_H__

#define AUDIO_BUFFERS 64
#define VIDEO_BUFFERS 4

#include <libavcodec/avcodec.h>
#include <libavutil/pixfmt.h>
#include "errors.h"
#include "queue.h"

typedef void* demux_ctx_h;

#ifdef CONFIG_VIDEO
typedef struct {
    queue_node_t node;
#ifdef CONFIG_VIDEO_HW_DECODE
    uint8_t *data;
    int buff_size;  /* Allocation size */
#else
    uint8_t *buffer[4];
    int linesize[4];
#endif
    int size;       /* Data size */
    int64_t pts_ms; /* PTS in ms from a stream begin */
    void *app_data;
    int number;
} video_buffer_t;
#endif

ret_code_t decode_init(demux_ctx_h *h, char *src_file);
void decode_uninit(demux_ctx_h h);
void decode_start_read(demux_ctx_h h);

/* Access to buffers by player */
uint8_t *decode_get_next_audio_buffer(demux_ctx_h h, size_t *size, void **app_data, int64_t *pts, ret_code_t *rc);
void decode_release_audio_buffer(demux_ctx_h h);

int decode_is_audio(demux_ctx_h h);
ret_code_t decode_next_audio_stream(demux_ctx_h h);

#ifdef CONFIG_VIDEO
video_buffer_t *decode_get_free_buffer(demux_ctx_h h);
video_buffer_t *decode_get_next_video_buffer(demux_ctx_h h, ret_code_t *rc);
void decode_release_video_buffer(demux_ctx_h h, video_buffer_t *buff);
int decode_is_video(demux_ctx_h h);
int devode_get_video_size(demux_ctx_h hd, int *w, int *h);
ret_code_t decode_get_pixel_format(demux_ctx_h h, enum AVPixelFormat *pix_fmt);
ret_code_t decode_get_codec_id(demux_ctx_h h, enum AVCodecID *codec_id);
ret_code_t decode_get_frame_rate(demux_ctx_h h, int *rate, int *scale);  
uint8_t *decode_get_codec_extra_data(demux_ctx_h h, int *size);
#endif

/* Output audio format. Used for a player configuration */
enum AVSampleFormat decode_get_sample_format(demux_ctx_h h);
int decode_get_sample_rate(demux_ctx_h h);
int decode_get_channels(demux_ctx_h h);
ret_code_t decode_get_audio_buffs_info(demux_ctx_h h, int *size, int *cont);
ret_code_t decode_set_audio_buffer_priv_data(demux_ctx_h h, int index, void *data);
ret_code_t decode_get_audio_buffer_priv_data(demux_ctx_h h, int index, void **data);
uint8_t *decode_get_audio_buffer_by_index(demux_ctx_h h, int index);

/* Demux/decode task */
ret_code_t decode_start(demux_ctx_h h);
int decode_is_task_running(demux_ctx_h h);
void decode_stop(demux_ctx_h h);

#endif

