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
#ifndef __OMX_AUDIO_RENDER_H__
#define __OMX_AUDIO_RENDER_H__

ilcore_comp_h create_omxaudio_render(int buff_size, int buff_count, int sample_rate, int channels);
void destroy_omxaudio_render(ilcore_comp_h render);

ret_code_t omxaudio_render_setup_buffers(ilcore_comp_h render, demux_ctx_h demux);
ret_code_t omxaudio_render_release_buffers(ilcore_comp_h render, demux_ctx_h demuxer);

#endif
