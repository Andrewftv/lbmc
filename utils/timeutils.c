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
#include "timeutils.h"

int util_time_compare(struct timespec *t1, struct timespec *t2)
{
    if (t1->tv_sec > t2->tv_sec)
        return 1;
    else if (t1->tv_sec < t2->tv_sec)
        return -1;
    else if (t1->tv_nsec > t2->tv_nsec)
        return 1;
    else if (t1->tv_nsec < t2->tv_nsec)
        return -1;

    return 0;
}

int util_time_diff(struct timespec *t1, struct timespec *t2)
{
    int diff;

    diff = ((t1->tv_sec - t2->tv_sec) * 1000);
    diff += ((t1->tv_nsec - t2->tv_nsec) / 1000000);

    return diff;
}

ret_code_t util_time_add(struct timespec *t, uint32_t ms)
{
    int32_t ms_only = ms % 1000;

    t->tv_sec += ms / 1000;
    t->tv_nsec += (ms_only * 1000000);
    if (t->tv_nsec > 999999999)
    {
        t->tv_nsec -= 1000000000;
        t->tv_sec++;
    }
    return L_OK;
}

ret_code_t util_time_sub(struct timespec *t, uint32_t ms)
{
    int32_t ms_only = ms % 1000;

    t->tv_sec -= ms / 1000;
    if (t->tv_nsec < ms_only * 1000000)
    {
        t->tv_sec--;
        t->tv_nsec = 1000000000 + t->tv_nsec - ms_only;
    }
    else
    {
        t->tv_nsec -= (ms_only * 1000000);
    }
    return L_OK;
}

