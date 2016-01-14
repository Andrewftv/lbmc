#ifndef __OMX_AUDIO_RENDER_H__
#define __OMX_AUDIO_RENDER_H__

ilcore_comp_h create_omxaudio_render(int buff_size, int buff_count, int sample_rate, int channels);
void destroy_omxaudio_render(ilcore_comp_h render);

ret_code_t omxaudio_render_setup_buffers(ilcore_comp_h render, demux_ctx_h demux);

#endif

