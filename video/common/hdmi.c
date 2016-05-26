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
#include <bcm_host.h>

#include "log.h"
#include "errors.h"

ret_code_t hdmi_init_display(TV_DISPLAY_STATE_T *tv_state)
{
    TV_SUPPORTED_MODE_NEW_T *modes;
    TV_SUPPORTED_MODE_NEW_T *temp, *tv_found = NULL;
    HDMI_RES_GROUP_T prefer_group;
    uint32_t prefer_mode;
    int max_modes, i;
    uint32_t num_modes;

    memset(tv_state, 0, sizeof(TV_DISPLAY_STATE_T));
    vc_tv_get_display_state(tv_state);

    max_modes = vc_tv_hdmi_get_supported_modes_new(HDMI_RES_GROUP_CEA, NULL, 0, &prefer_group, &prefer_mode);
    if (!max_modes)
    {
        DBG_E("Video not supported\n");
        return L_FAILED;
    }

    modes = (TV_SUPPORTED_MODE_NEW_T *)malloc(sizeof(TV_SUPPORTED_MODE_NEW_T) * max_modes);
    if (!modes)
    {
        DBG_E("Not enoght memory\n");
        return L_FAILED;
    }

    num_modes = vc_tv_hdmi_get_supported_modes_new(HDMI_RES_GROUP_CEA, modes, max_modes, &prefer_group, &prefer_mode);

    DBG_I("Available video modes:\n");
    for (i = 0; i < num_modes; i++)
    {
        temp = modes + i;

        DBG_I("Output mode %d: %dx%d@%d %s%s:%x\n", temp->code, temp->width, temp->height, temp->frame_rate,
			temp->native?"N":"", temp->scan_mode?"I":"", temp->code);

        /* TODO */
        if (temp->width == 1920 && temp->height == 1080)
            tv_found = temp;
    }
    if (!tv_found)
    {
        DBG_E("High definition video mode not found\n");
        goto Exit;
    }
    DBG_I("Chosen mode %d: %dx%d@%d %s%s:%x\n", tv_found->code, tv_found->width, tv_found->height, tv_found->frame_rate,
			tv_found->native?"N":"", tv_found->scan_mode?"I":"", tv_found->code);

    vc_tv_hdmi_power_on_explicit_new(HDMI_MODE_HDMI, HDMI_RES_GROUP_CEA, tv_found->code);

Exit:
    free(modes);

    return L_OK;
}

void hdmi_uninit_display(TV_DISPLAY_STATE_T *tv_state)
{
    if (tv_state->display.hdmi.group && tv_state->display.hdmi.mode)
        vc_tv_hdmi_power_on_explicit_new(HDMI_MODE_HDMI, tv_state->display.hdmi.group, tv_state->display.hdmi.mode);
}
