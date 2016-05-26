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
#ifndef __OMX_CLOCK_H__
#define __OMX_CLOCK_H__

OMX_TICKS to_omx_time(int64_t pts);
uint64_t from_omx_time(OMX_TICKS ticks);

typedef enum {
    OMX_CLOCK_PAUSE_SPEED = 0,
    OMX_CLOCK_NORMAL_SPEED = (1<<16)
} omxclock_playback_speed_t;

ilcore_comp_h create_omx_clock(void);
void destroy_omx_clock(ilcore_comp_h clock);

ret_code_t omx_clock_start(ilcore_comp_h clock, uint64_t pts);
ret_code_t omx_clock_set_speed(ilcore_comp_h clock, omxclock_playback_speed_t speed);
ret_code_t omx_clock_state_execute(ilcore_comp_h clock);
ret_code_t omx_clock_hdmi_clock_sync(ilcore_comp_h clock);
ret_code_t omx_clock_stop(ilcore_comp_h clock);
ret_code_t omx_clock_reset(ilcore_comp_h clock);

#endif
