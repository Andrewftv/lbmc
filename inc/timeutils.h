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
#ifndef __TIME_UTILS_H__
#define __TIME_UTILS_H__

#include <time.h>
#include <stdint.h>

#include "errors.h"

/*
 * Compare two timespec structures
 * Return -1 if t1 less t2, 1 if t1 above t2 and 0 if equal.
 */
int util_time_compare(struct timespec *t1, struct timespec *t2);
/*
 * Substruct time.
 * Return time difference in miliseconds.
 */
int util_time_diff(struct timespec *t1, struct timespec *t2);
ret_code_t util_time_add(struct timespec *t, uint32_t ms);
ret_code_t util_time_sub(struct timespec *t, uint32_t ms);

#endif
