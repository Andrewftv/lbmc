#ifndef __LBMC_DECODE_H__
#define __LBMC_DECODE_H__

#define AUDIO_BUFFERS 64
#define VIDEO_BUFFERS 2

#include <libavutil/pixfmt.h>
#include "errors.h"

typedef void* demux_ctx_h;

ret_code_t decode_init(demux_ctx_h *h, char *src_file);
void decode_uninit(demux_ctx_h h);

/* Access to buffers by player */
int decode_get_audio_buffers_count(demux_ctx_h h);
uint8_t *decode_get_next_audio_buffer(demux_ctx_h h, size_t *size, void **app_data, int64_t *pts, ret_code_t *rc);
void decode_release_audio_buffer(demux_ctx_h h);

int decode_is_audio(demux_ctx_h h);
ret_code_t decode_next_audio_stream(demux_ctx_h h);

#ifdef CONFIG_VIDEO
int decode_get_video_buffers_count(demux_ctx_h h);
uint8_t *decode_get_next_video_buffer(demux_ctx_h h, size_t *size, int64_t *pts, ret_code_t *rc);
void decode_release_video_buffer(demux_ctx_h h);
int decode_is_video(demux_ctx_h h);
int devode_get_video_size(demux_ctx_h hd, int *w, int *h);
ret_code_t decode_get_pixel_format(demux_ctx_h h, enum AVPixelFormat *pix_fmt);
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

