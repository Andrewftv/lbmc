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
#ifndef __LBMC_AUDIO_PLAYER_H__
#define __LBMC_AUDIO_PLAYER_H__

#include "errors.h"
#ifdef CONFIG_RASPBERRY_PI
#include "IL/OMX_Core.h"
#endif
/*
 * Audio player interface.
 */

typedef void* audio_player_h;

#ifdef CONFIG_RASPBERRY_PI
OMX_ERRORTYPE audio_play_buffer_done(OMX_HANDLETYPE hComponent, OMX_PTR pAppData, OMX_BUFFERHEADERTYPE* pBuffer);
#endif

ret_code_t audio_player_start(audio_player_h *player_ctx, demux_ctx_h h, void *clock);
void audio_player_stop(audio_player_h player_ctx, int stop);
int audio_player_pause_toggle(audio_player_h player_ctx);
ret_code_t audio_player_mute_toggle(audio_player_h player_ctx, int *is_muded);
ret_code_t audio_player_seek(audio_player_h h, seek_direction_t dir, int32_t seek);

void audio_player_lock(audio_player_h h);
void audio_player_unlock(audio_player_h h);

int audio_player_is_runnung(audio_player_h h);

#endif
