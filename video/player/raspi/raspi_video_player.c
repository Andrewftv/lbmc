#include <string.h>

#include "video_player.h"

ret_code_t video_player_start(video_player_context *player_ctx, demux_ctx_h h)
{
    memset(player_ctx, 0, sizeof(video_player_context));

    return L_FAILED;
}

