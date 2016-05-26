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
#include "log.h"
#include "msleep.h"
#include "ilcore.h"
#include "omxclock.h"

#ifdef OMX_SKIP64BIT
OMX_TICKS to_omx_time(int64_t pts)
{
	OMX_TICKS ticks;
	ticks.nLowPart = pts;
	ticks.nHighPart = pts >> 32;
	return ticks;
}
uint64_t from_omx_time(OMX_TICKS ticks)
{
	uint64_t pts = ticks.nLowPart | ((uint64_t)ticks.nHighPart << 32);
	return pts;
}
#else
#define to_omx_time(x) (x)
#define from_omx_time(x) (x)
#endif

ilcore_comp_h create_omx_clock(void)
{
    OMX_TIME_CONFIG_ACTIVEREFCLOCKTYPE ref_clock;
    OMX_CALLBACKTYPE cb;
    OMX_ERRORTYPE err;
    ilcore_comp_h clock;

    cb.EventHandler = il_event_handler;
    cb.EmptyBufferDone = il_empty_buffer_done;
    cb.FillBufferDone = il_fill_buffer_done;

    if (ilcore_init_comp(&clock, &cb, "OMX.broadcom.clock"))
        return NULL;

    if (ilcore_disable_all_ports(clock))
        goto Error;

    OMX_INIT_STRUCT(ref_clock);
    ref_clock.eClock = OMX_TIME_RefClockAudio;

    err = OMX_SetConfig(ilcore_get_handle(clock), OMX_IndexConfigTimeActiveRefClock, &ref_clock);
    if (err != OMX_ErrorNone)
    {
        DBG_E("Clock reference config failed. err = 0x%08x\n");
        goto Error;
    }

    return clock;

Error:
    destroy_omx_clock(clock);
    
    return NULL;
}

void destroy_omx_clock(ilcore_comp_h clock)
{
    if (clock)
        ilcore_uninit_comp(clock);
}

ret_code_t omx_clock_start(ilcore_comp_h clock, uint64_t pts)
{
    OMX_TIME_CONFIG_CLOCKSTATETYPE clock_state;
    OMX_ERRORTYPE err;

    OMX_INIT_STRUCT(clock_state);
    clock_state.eState = OMX_TIME_ClockStateRunning;
  	clock_state.nStartTime = to_omx_time(1000 * pts);

    err = OMX_SetConfig(ilcore_get_handle(clock), OMX_IndexConfigTimeClockState, &clock_state);
  	if(err != OMX_ErrorNone)
    {
        DBG_E("%s: error setting OMX_IndexConfigTimeClockState\n", __FUNCTION__);
        return L_FAILED;
    }

    return L_OK;
}

ret_code_t omx_clock_stop(ilcore_comp_h clock)
{
    OMX_ERRORTYPE err;
    OMX_TIME_CONFIG_CLOCKSTATETYPE clock_state;

    OMX_INIT_STRUCT(clock_state);
    clock_state.eState = OMX_TIME_ClockStateStopped;

    err = OMX_SetConfig(ilcore_get_handle(clock), OMX_IndexConfigTimeClockState, &clock_state);
  	if(err != OMX_ErrorNone)
    {
        DBG_E("%s: error setting OMX_IndexConfigTimeClockState\n", __FUNCTION__);
        return L_FAILED;
    }

    return L_OK;
}

ret_code_t omx_clock_set_speed(ilcore_comp_h clock, omxclock_playback_speed_t speed)
{
    OMX_ERRORTYPE err;
    OMX_TIME_CONFIG_SCALETYPE scale_type;

    OMX_INIT_STRUCT(scale_type);
    scale_type.xScale = speed;

    err = OMX_SetConfig(ilcore_get_handle(clock), OMX_IndexConfigTimeScale, &scale_type);
  	if(err != OMX_ErrorNone)
    {
        DBG_E("%s: error setting OMX_IndexConfigTimeClockState\n", __FUNCTION__);
        return L_FAILED;
    }

    return L_OK;
}

ret_code_t omx_clock_state_execute(ilcore_comp_h clock)
{
    OMX_STATETYPE state;

    if (ilcore_get_state(clock, &state))
        return L_FAILED;

    if (state != OMX_StateExecuting)
    {
        if (ilcore_set_state(clock, OMX_StateExecuting))
            return L_FAILED;
    }

    return L_OK;
}

ret_code_t omx_clock_reset(ilcore_comp_h clock)
{
    OMX_ERRORTYPE err;
    OMX_TIME_CONFIG_CLOCKSTATETYPE clock_state;

    OMX_INIT_STRUCT(clock_state);

    omx_clock_stop(clock);

    clock_state.eState = OMX_TIME_ClockStateWaitingForStartTime;
    clock_state.nOffset = to_omx_time(-1000LL * 200 /*OMX_PRE_ROLL*/);

    clock_state.nWaitMask |= OMX_CLOCKPORT0;
    clock_state.nWaitMask |= OMX_CLOCKPORT1;
    clock_state.nWaitMask |= OMX_CLOCKPORT2;

    err = OMX_SetConfig(ilcore_get_handle(clock), OMX_IndexConfigTimeClockState, &clock_state);
  	if(err != OMX_ErrorNone)
    {
        DBG_E("%s: error setting OMX_IndexConfigTimeClockState\n", __FUNCTION__);
        return L_FAILED;
    }

    omx_clock_start(clock, 0);

    return L_OK;
}

ret_code_t omx_clock_hdmi_clock_sync(ilcore_comp_h clock)
{
    OMX_CONFIG_LATENCYTARGETTYPE latency;

    OMX_INIT_STRUCT(latency);

    latency.nPortIndex = OMX_ALL;
    latency.bEnabled = OMX_TRUE;
    latency.nFilter = 10;
    latency.nTarget = 0;
    latency.nShift = 3;
    latency.nSpeedFactor = -200;
    latency.nInterFactor = 100;
    latency.nAdjCap = 100;

    return ilcore_set_config(clock, OMX_IndexConfigLatencyTarget, &latency);
}
