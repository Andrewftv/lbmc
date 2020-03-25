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

#define INFINITE_WAIT   		(-1)

#define MSLEEP_OK               0
#define MSLEEP_TIMEOUT          1
#define MSLEEP_INTERRUPT        2
#define MSLEEP_ERROR            (-1)

#ifdef __cplusplus
extern "C" {
#endif

int msleep_init(msleep_h *h);
void msleep_uninit(msleep_h h);
int msleep_wait(msleep_h h, int timeout);
int msleep_wakeup(msleep_h h);
#ifndef CONFIG_FUTEX
int msleep_wakeup_broadcast(msleep_h h);
#endif

#ifdef __cplusplus
}
#endif

#endif

