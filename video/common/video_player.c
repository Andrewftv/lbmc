#include <stdint.h>
#include <pthread.h>
#include <unistd.h>
#include <stdlib.h>

#include "log.h"
#include "decode.h"
#include "video_player.h"
#include "timeutils.h"

#include <libavutil/avutil.h>

void video_player_pause(video_player_context *player_ctx)
{
	if (!player_ctx)
		return;

    if (player_ctx->state == PLAYER_PAUSE)
    {
        player_ctx->state = PLAYER_PLAY;
        clock_gettime(CLOCK_MONOTONIC, &player_ctx->base_time);
    }
    else
    {
        player_ctx->state = PLAYER_PAUSE;
        player_ctx->corrected_pts = player_ctx->last_pts;
    }
}

void video_player_stop(video_player_context *player_ctx)
{
	if (!player_ctx)
		return;

	player_ctx->running = 0;
	/* Waiting for player task */
	pthread_join(player_ctx->task, NULL);

	free(player_ctx->priv);
}

void *player_main_routine(void *args)
{
	size_t size;
	uint8_t *buf;
    int64_t pts;
    int first_pkt = 1;
	video_player_context *player_ctx = (video_player_context *)args;

	DBG_I("Video player task started.\n");

	if (player_ctx->init(player_ctx->priv))
		return NULL;

#ifdef TEXT_RENDERER
	if (ft_text_init(&ctx->ft_lib))
		ctx->ft_lib = NULL;
#endif

	player_ctx->running = 1;

	while(player_ctx->running)
	{
        ret_code_t rc;

		if (!player_ctx->running)
			break;

        if (player_ctx->state == PLAYER_PAUSE)
        {
            usleep(100000);
            continue;
        }

		buf = decode_get_next_video_buffer(player_ctx->demux_ctx, &size, &pts, &rc);
        if (!buf)
        {
            if (rc == L_FAILED)
            {
                DBG_E("Can not get next a video buffer\n");
                break;
            }
            else if (rc == L_TIMEOUT)
            {
                if (player_ctx->idle)
					player_ctx->idle(player_ctx->priv);
                continue;
            }
            else if (rc == L_OK)
            {
                DBG_E("Incorrect state\n");
                break;
            }
        }

        if (first_pkt)
        {
            clock_gettime(CLOCK_MONOTONIC, &player_ctx->base_time);
            first_pkt = 0;
        }
        else if (pts != AV_NOPTS_VALUE)
        {
            struct timespec curr_time;
            int diff, pts_ms = (int)pts - player_ctx->corrected_pts;

            clock_gettime(CLOCK_MONOTONIC, &curr_time);
            diff = util_time_sub(&curr_time, &player_ctx->base_time);
            DBG_V("Current PTS=%d time diff=%d\n", pts_ms, diff);
            if (pts_ms > diff)
            {
                diff = pts_ms - diff;
                DBG_V("Going to sleep for %d ms\n", diff);
                usleep(diff * 1000);
            }
            player_ctx->last_pts = pts;
        }

		player_ctx->draw_frame(player_ctx->priv, buf);

		decode_release_video_buffer(player_ctx->demux_ctx);
	}

	player_ctx->running = 0;
#ifdef TEXT_RENDERER
	if (ctx->ft_lib)
		ft_text_uninit(ctx->ft_lib);
#endif

	player_ctx->uninit(player_ctx->priv);

	DBG_I("Video player task finished.\n");
	return NULL;
}

