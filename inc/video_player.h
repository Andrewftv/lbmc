#ifndef __LBMC_VIDEO_PLAYER_H__
#define __LBMC_VIDEO_PLAYER_H__

#include <pthread.h>
#include <stdint.h>
#ifdef CONFIG_RASPBERRY_PI
#include <bcm_host.h>
#endif

#include "errors.h"
#include "decode.h"

/*
 * Video player interface.
 */

typedef void* video_player_h;

typedef enum {
    PLAYER_INITED = 0,
    PLAYER_PLAY,
    PLAYER_PAUSE
} player_state_t;

typedef struct {
    video_player_h priv;

    pthread_t task;
    player_state_t state;
    int running;

    struct timespec base_time;
    int64_t corrected_pts;
    int64_t last_pts;

    demux_ctx_h demux_ctx;

    ret_code_t (*init)(video_player_h ctx);
    void (*uninit)(video_player_h ctx);
    ret_code_t (*draw_frame)(video_player_h ctx, media_buffer_t *buff);
    void (*idle)(video_player_h ctx);
} video_player_context;

void *player_main_routine(void *args);

ret_code_t video_player_start(video_player_context *player_ctx, demux_ctx_h h, void *clock);
void video_player_stop(video_player_context *player_ctx);
int video_player_pause_toggle(video_player_context *player_ctx);

#ifdef CONFIG_RASPBERRY_PI
ret_code_t hdmi_init_display(TV_DISPLAY_STATE_T *tv_state);
void hdmi_uninit_display(TV_DISPLAY_STATE_T *tv_state);
#endif

#endif

