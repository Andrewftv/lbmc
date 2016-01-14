#include "log.h"
#include "msleep.h"
#include "ilcore.h"
#include "omxclock.h"

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

    memset(&ref_clock, 0, sizeof(OMX_TIME_CONFIG_ACTIVEREFCLOCKTYPE));
    ref_clock.nSize = sizeof(OMX_TIME_CONFIG_ACTIVEREFCLOCKTYPE);
    ref_clock.eClock = OMX_TIME_RefClockAudio;
    ref_clock.nVersion.nVersion = OMX_VERSION;

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

    clock_state.nSize = sizeof(OMX_TIME_CONFIG_CLOCKSTATETYPE);
    clock_state.nVersion.nVersion = OMX_VERSION;
    clock_state.eState = OMX_TIME_ClockStateRunning;
  	clock_state.nStartTime = pts;

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

    scale_type.nSize = sizeof(OMX_TIME_CONFIG_CLOCKSTATETYPE);
    scale_type.nVersion.nVersion = OMX_VERSION;
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
