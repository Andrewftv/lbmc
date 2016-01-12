#ifndef __ILCORE_H__
#define __ILCORE_H__

#include <bcm_host.h>
#include <IL/OMX_Broadcom.h>

#include "errors.h"
#include "list.h"

#define IL_AUDIO_RENDER_IN_PORT  100

typedef void* ilcore_comp_h;

typedef struct omx_event {
	list_node_t node;
	OMX_EVENTTYPE eEvent;
	OMX_U32 nData1;
	OMX_U32 nData2;
} omx_event_t;

ret_code_t ilcore_init_comp(ilcore_comp_h *h, OMX_CALLBACKTYPE *cb, char *name);
void ilcore_uninit_comp(ilcore_comp_h h);

ret_code_t ilcore_add_comp_event(ilcore_comp_h comp, OMX_EVENTTYPE eEvent, OMX_U32 nData1, OMX_U32 nData2);
ret_code_t ilcore_disable_all_ports(ilcore_comp_h comp);
ret_code_t ilcore_set_state(ilcore_comp_h h, OMX_STATETYPE state);
OMX_HANDLETYPE ilcore_get_handle(ilcore_comp_h h);
ret_code_t ilcore_get_state(ilcore_comp_h h, OMX_STATETYPE *state);
OMX_ERRORTYPE omx_core_comp_wait_command(ilcore_comp_h h, OMX_U32 command, OMX_U32 nData2, long timeout);
ret_code_t ilcore_set_port_buffers_param(ilcore_comp_h h, int size, int count);

#endif

