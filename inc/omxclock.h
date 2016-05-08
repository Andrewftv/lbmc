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

