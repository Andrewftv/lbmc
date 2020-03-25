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

#include <errno.h>
#include <linux/futex.h>
#include <syscall.h>
 
typedef struct {
#ifdef CONFIG_FUTEX
    uint32_t lock;
    int valid;
#else
    pthread_mutex_t mutex;
    pthread_cond_t cond;
#endif
} msleep_ctx_t;

#ifdef CONFIG_FUTEX
static int _futex(int *uaddr, int futex_op, int val,
    const struct timespec *timeout, int *uaddr2, int val3)
{
    return syscall(SYS_futex, uaddr, futex_op, val, timeout, uaddr, val3);
}

static int _futex_wait(uint32_t *ft, int timeout)
{
    int rc = MSLEEP_INTERRUPT;
    struct timespec wait_time;
    struct timespec *pwait_time;

    if (timeout != INFINITE_WAIT)
    {
        wait_time.tv_sec = timeout / 1000;
        wait_time.tv_nsec = timeout % 1000 * 1000000;

        pwait_time = &wait_time;
    }
    else
    {
        pwait_time = NULL;
    }

    while (1)
    {
        if (__sync_bool_compare_and_swap(ft, 1, 0))
            break;

        if (_futex((int *)ft, FUTEX_WAIT, 0, pwait_time, NULL, 0) == -1)
        {
            if (errno == ETIMEDOUT)
            {
                __sync_bool_compare_and_swap(ft, 0, 1);
                rc = MSLEEP_TIMEOUT;
            }
            else if (errno != EAGAIN)
            {
                rc = MSLEEP_ERROR;
            }
        }
        else
        {
            rc = MSLEEP_INTERRUPT;
        }
    }

    return rc;
}

static int _futex_wake(uint32_t *ft)
{
    int rc = MSLEEP_OK;
    
    if (__sync_bool_compare_and_swap(ft, 0, 1))
        if( _futex((int *)ft, FUTEX_WAKE, 1, NULL, NULL, 0) == -1)
            rc = MSLEEP_ERROR;

    return rc;
}
#endif

int msleep_init(msleep_h *h)
{
    msleep_ctx_t *ctx = (msleep_ctx_t *)malloc(sizeof(msleep_ctx_t));
    if (!ctx)
        return MSLEEP_ERROR;
 
    memset(ctx, 0, sizeof(msleep_ctx_t));

#ifdef CONFIG_FUTEX
    ctx->lock = 0;
    ctx->valid = 1;
#else
    if (pthread_mutex_init(&ctx->mutex, NULL))
        goto Error;
 
    if (pthread_cond_init(&ctx->cond, NULL))
        goto Error;
#endif

    *h = ctx;
 
    return MSLEEP_OK;

#ifndef CONFIG_FUTEX
Error:
    msleep_uninit(ctx);
    return MSLEEP_ERROR;
#endif
}
 
void msleep_uninit(msleep_h h)
{
    msleep_ctx_t *ctx = (msleep_ctx_t *)h;

#ifndef CONFIG_FUTEX
    pthread_cond_destroy(&ctx->cond); 
    pthread_mutex_destroy(&ctx->mutex);
#endif
    if (ctx)
        free(ctx);
}
 
int msleep_wait(msleep_h h, int timeout)
{
    int rc = MSLEEP_INTERRUPT;
    msleep_ctx_t *ctx = (msleep_ctx_t *)h;

#ifdef CONFIG_FUTEX
    if (!ctx || !ctx->valid)
        return MSLEEP_ERROR;

    rc = _futex_wait(&ctx->lock, timeout);
#else
    struct timespec endtime;
 
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
#endif
   
    return rc;
}
 
int msleep_wakeup(msleep_h h)
{
    msleep_ctx_t *ctx = (msleep_ctx_t *)h;
    int rc = MSLEEP_OK;

#ifdef CONFIG_FUTEX
    if (!ctx || !ctx->valid)
        return MSLEEP_ERROR;

    rc = _futex_wake(&ctx->lock);
#else
    pthread_mutex_lock(&ctx->mutex);
    pthread_cond_signal(&ctx->cond);
    pthread_mutex_unlock(&ctx->mutex);
#endif

    return rc;
}

#ifndef CONFIG_FUTEX
int msleep_wakeup_broadcast(msleep_h h)
{
    msleep_ctx_t *ctx = (msleep_ctx_t *)h;

    pthread_mutex_lock(&ctx->mutex);
    pthread_cond_broadcast(&ctx->cond);
    pthread_mutex_unlock(&ctx->mutex);

    return MSLEEP_OK;
}
#endif

