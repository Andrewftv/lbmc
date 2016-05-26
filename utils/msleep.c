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
#include <stdio.h>
#include <pthread.h>
#include <time.h>
#include <sys/types.h>
#include <sys/time.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include "msleep.h"
#include "timeutils.h"
 
typedef struct {
    pthread_mutex_t mutex;
    pthread_cond_t cond;
} msleep_ctx_t;
 
int msleep_init(msleep_h *h)
{
    msleep_ctx_t *ctx = (msleep_ctx_t *)malloc(sizeof(msleep_ctx_t));
    if (!ctx)
        return -1;
 
    memset(ctx, 0, sizeof(msleep_ctx_t));

    if (pthread_mutex_init(&ctx->mutex, NULL))
        goto Error;
 
    if (pthread_cond_init(&ctx->cond, NULL))
        goto Error;

    *h = ctx;
 
    return 0;
 
Error:
    msleep_uninit(ctx);
    return -1;
}
 
void msleep_uninit(msleep_h h)
{
    msleep_ctx_t *ctx = (msleep_ctx_t *)h;

    pthread_cond_destroy(&ctx->cond); 
    pthread_mutex_destroy(&ctx->mutex);
    free(ctx);
}
 
int msleep_wait(msleep_h h, int timeout)
{
    struct timespec endtime;
    int rc = MSLEEP_INTERRUPT;
 
    msleep_ctx_t *ctx = (msleep_ctx_t *)h;
 
    if (timeout == INFINITE_WAIT)
    {
        pthread_mutex_lock(&ctx->mutex);
        pthread_cond_wait(&ctx->cond, &ctx->mutex);
        pthread_mutex_unlock(&ctx->mutex);
        
        return MSLEEP_INTERRUPT;
    }
 
    clock_gettime(CLOCK_REALTIME, &endtime);
    util_time_add(&endtime, timeout);
 
    pthread_mutex_lock(&ctx->mutex);
    if (pthread_cond_timedwait(&ctx->cond, &ctx->mutex, &endtime))
        rc = MSLEEP_TIMEOUT;
    pthread_mutex_unlock(&ctx->mutex);
    
    return rc;
}
 
int msleep_wakeup(msleep_h h)
{
    msleep_ctx_t *ctx = (msleep_ctx_t *)h;

    pthread_mutex_lock(&ctx->mutex);
    pthread_cond_signal(&ctx->cond);
    pthread_mutex_unlock(&ctx->mutex);

    return 0;
}

int msleep_wakeup_broadcast(msleep_h h)
{
    msleep_ctx_t *ctx = (msleep_ctx_t *)h;

    pthread_mutex_lock(&ctx->mutex);
    pthread_cond_broadcast(&ctx->cond);
    pthread_mutex_unlock(&ctx->mutex);

    return 0;
}

