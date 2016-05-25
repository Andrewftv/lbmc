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
void audio_player_stop(audio_player_h player_ctx);
int audio_player_pause_toggle(audio_player_h player_ctx);
ret_code_t audio_player_mute_toggle(audio_player_h player_ctx, int *is_muded);

int audio_player_is_runnung(audio_player_h h);

#endif

