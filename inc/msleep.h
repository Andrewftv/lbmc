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
#ifndef __MSLEEP_H__
#define __MSLEEP_H__

#include <stdint.h>

typedef void* msleep_h;

#define MSLEEP_INFINITE_WAIT    (-1)

typedef enum {
    MSLEEP_OK = 0,
    MSLEEP_TIMEOUT = 1,
    MSLEEP_INTERRUPT = 2,
    MSLEEP_ERROR = -1
} msleep_err_t;

#ifdef __cplusplus
extern "C" {
#endif

msleep_err_t msleep_init(msleep_h *h);
void msleep_uninit(msleep_h h);
msleep_err_t msleep_wait(msleep_h h, int timeout);
msleep_err_t msleep_wakeup(msleep_h h);
#ifndef CONFIG_FUTEX
msleep_err_t msleep_wakeup_broadcast(msleep_h h);
#endif

#ifdef __cplusplus
}
#endif

#endif

