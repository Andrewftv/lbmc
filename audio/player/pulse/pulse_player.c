/*
 * Implementation of audio player over pulseaudio.
 */

#include <stdio.h>
#include <pthread.h>
#include <stdlib.h>
#include <unistd.h>

#include <libavutil/samplefmt.h>

#include <pulse/simple.h>
#include <pulse/error.h>

#include "log.h"
#include "decode.h"
#include "audio_player.h"

typedef struct {
	pthread_t task;

	int pause;
    int running;

	demux_ctx_h audio_ctx;
} pulse_player_ctx_t;

static int init_context(pulse_player_ctx_t *ctx)
{
	memset(ctx, 0, sizeof(pulse_player_ctx_t));

	return 0;
}

static void uninit_context(pulse_player_ctx_t *ctx)
{
}

static pa_sample_format_t av2pa(enum AVSampleFormat src_fmt)
{
	pa_sample_format_t dst_fmt = PA_SAMPLE_INVALID;

	switch(src_fmt)
	{
	case AV_SAMPLE_FMT_U8:
		dst_fmt = PA_SAMPLE_U8;
		break;
	case AV_SAMPLE_FMT_S16:
		dst_fmt = PA_SAMPLE_S16LE;
		break;
	case AV_SAMPLE_FMT_S32:
		dst_fmt = PA_SAMPLE_S32LE;
		break;
	case AV_SAMPLE_FMT_FLT:
		dst_fmt = PA_SAMPLE_FLOAT32LE;
		break;
	case AV_SAMPLE_FMT_DBL:
		dst_fmt = PA_SAMPLE_FLOAT32LE; /* ? */
		break;
	default:
		break;
	}

	return dst_fmt;
}

static void *player_routine(void *args)
{
	pulse_player_ctx_t *ctx = (pulse_player_ctx_t *)args;
    pa_sample_spec ss;
	pa_simple *s = NULL;
	int error;
	size_t size;
	uint8_t *buf;
	enum AVSampleFormat fmt;
    ret_code_t rc;

	ss.rate = decode_get_sample_rate(ctx->audio_ctx);
	ss.channels = decode_get_channels(ctx->audio_ctx);
	fmt = decode_get_sample_format(ctx->audio_ctx);
	ss.format = av2pa(fmt);

	DBG_I("Player task started\n");
	ctx->running = 1;

	DBG_I("Open pulse audio. format: %s rate: %d channels: %d\n",
		pa_sample_format_to_string(ss.format), ss.rate, ss.channels);

	if (!(s = pa_simple_new(NULL, "LBMC", PA_STREAM_PLAYBACK, NULL, "playback",
		&ss, NULL, NULL, &error)))
	{
        DBG_E("pa_simple_new() failed: %s\n", pa_strerror(error));
        goto finish;
    }

	while(ctx->running)
	{
		if (!ctx->running)
			break;

		if (ctx->pause)
		{
			usleep(100000);
			continue;
		}

		buf = decode_get_next_audio_buffer(ctx->audio_ctx, &size, NULL, &rc);
        if (!buf)
        {
            if (rc != L_STOPPING)
                DBG_E("Nothing to play\n");
            usleep(10000);
            continue;
        }

		if (pa_simple_write(s, buf, size, &error) < 0) 
		{
            DBG_E("pa_simple_write() failed: %s\n", pa_strerror(error));
            break;
        }

		decode_release_audio_buffer(ctx->audio_ctx);
	}

finish:

	if (pa_simple_drain(s, &error) < 0)
        DBG_E("pa_simple_drain() failed: %s\n", pa_strerror(error));
	pa_simple_free(s);

	ctx->running = 0;
	DBG_I("Player task finished\n");
	return NULL;
}

int audio_player_is_runnung(audio_player_h h)
{
    pulse_player_ctx_t *ctx = (pulse_player_ctx_t *)h;

    return ctx->running;
}

void audio_player_pause(audio_player_h player_ctx)
{
	pulse_player_ctx_t *ctx = (pulse_player_ctx_t *)player_ctx;

	ctx->pause = !ctx->pause;
}

ret_code_t audio_player_start(audio_player_h *player_ctx, demux_ctx_h h)
{
	pulse_player_ctx_t *ctx;
    ret_code_t rc = L_OK;

	ctx = (pulse_player_ctx_t *)malloc(sizeof(pulse_player_ctx_t));
	if (!ctx)
	{
		DBG_E("Mamory allocation failed\n");
		return L_FAILED;
	}

	init_context(ctx);
	ctx->audio_ctx = h;

	*player_ctx = ctx;
	/* Use default scheduler. Set SCHED_RR or SCHED_FIFO request root access */
	if (pthread_create(&ctx->task, NULL, player_routine, ctx) < 0)
        rc = L_FAILED;

    return rc;
}

void audio_player_stop(audio_player_h player_ctx)
{
	pulse_player_ctx_t *ctx = (pulse_player_ctx_t *)player_ctx;

	if (!ctx)
		return;

	ctx->running = 0;
	/* Waiting for player task */
	pthread_join(ctx->task, NULL);

	uninit_context(ctx);

	free(ctx);
}

