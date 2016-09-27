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
#ifndef __LBMC_VIDEO_PLAYER_H__
#define __LBMC_VIDEO_PLAYER_H__

#include <pthread.h>
#include <stdint.h>
#ifdef CONFIG_RASPBERRY_PI
#include <bcm_host.h>
#endif

#include "errors.h"
#include "decode.h"
#include "msleep.h"
#include "control.h"

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
    pthread_t task;
    player_state_t state;
    int running;
    int stop;
#ifndef CONFIG_RASPBERRY_PI
    int first_pkt;
    msleep_h sched;
#endif

    struct timespec base_time;
    struct timespec start_pause;

    pthread_mutex_t lock;
    queue_h event_queue;

    demux_ctx_h demux_ctx;
    control_ctx_h ctrl_ctx;

    ret_code_t (*init)(video_player_h ctx);
    void (*uninit)(video_player_h ctx);
    ret_code_t (*draw_frame)(video_player_h ctx, media_buffer_t *buff);
    void (*idle)(video_player_h ctx);
    int (*pause)(video_player_h ctx);
    ret_code_t (*seek)(video_player_h h, seek_direction_t dir, int32_t seek);
    ret_code_t (*schedule)(video_player_h h, media_buffer_t *buf);
} video_player_common_ctx_t;

void *player_main_routine(void *args);

ret_code_t video_player_start(video_player_h *player_ctx, demux_ctx_h h, void *clock);
void video_player_stop(video_player_h player_ctx, int stop);
ret_code_t video_player_seek(video_player_h ctx, seek_direction_t dir, int32_t seek);
int video_player_pause_toggle(video_player_h ctx);
ret_code_t video_player_set_control(video_player_h h, control_ctx_h ctrl);

#ifdef CONFIG_RASPBERRY_PI
ret_code_t hdmi_init_display(TV_DISPLAY_STATE_T *tv_state);
void hdmi_uninit_display(TV_DISPLAY_STATE_T *tv_state);
#endif

void video_player_lock(video_player_h ctx);
void video_player_unlock(video_player_h ctx);

#endif
