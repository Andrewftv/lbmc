#ifndef __ILCORE_H__
#define __ILCORE_H__

#include <bcm_host.h>
#include <IL/OMX_Broadcom.h>

#include "errors.h"
#include "list.h"

/* OMX clock ports */
#define IL_CLOCK_PORT1              80
#define IL_CLOCK_PORT2              81

/* OMX video decoder ports */
#define IL_VIDEO_DECODER_IN_PORT    130
#define IL_VIDEO_DECODER_OUT_PORT   131

/* OMX video scheduler ports */
#define IL_SCHED_VIDEO_IN_PORT      10
#define IL_SCHED_VIDEO_OUT_PORT     11
#define IL_SCHED_CLOCK_PORT         12

/* OMX video render port */
#define IL_VIDEO_RENDER_IN_PORT     90

/* OMX audio renderer ports */
#define IL_AUDIO_RENDER_IN_PORT     100
#define IL_AUDIO_RENDER_CLOCK_PORT  101

#define IL_IMAGE_DECODER_IN_PORT    320
#define IL_IMAGE_DECODER_OUT_PORT   321
#define IL_IMAGE_RESIZE_IN_PORT     60
#define IL_IMAGE_RESIZE_OUT_PORT    61

#define OMX_INIT_STRUCT(a) do { \
    memset(&(a), 0, sizeof(a)); \
    (a).nSize = sizeof(a); \
    (a).nVersion.nVersion = OMX_VERSION; \
} while(0)

typedef void* ilcore_comp_h;
typedef void* ilcore_tunnel_h;

typedef struct omx_event {
    list_node_t node;
    OMX_EVENTTYPE eEvent;
    OMX_U32 nData1;
    OMX_U32 nData2;
} omx_event_t;

/* Event handle callback */
OMX_ERRORTYPE il_event_handler(OMX_HANDLETYPE hComponent, OMX_PTR pAppData, OMX_EVENTTYPE eEvent, 
    OMX_U32 nData1, OMX_U32 nData2, OMX_PTR pEventData);
/* Buffer callbacks */
OMX_ERRORTYPE il_empty_buffer_done(OMX_HANDLETYPE hComponent, OMX_PTR pAppData, OMX_BUFFERHEADERTYPE* pBuffer);
OMX_ERRORTYPE il_fill_buffer_done(OMX_HANDLETYPE hComponent, OMX_PTR pAppData, OMX_BUFFERHEADERTYPE* pBuffer);

/* Initialization */
ret_code_t ilcore_init_comp(ilcore_comp_h *h, OMX_CALLBACKTYPE *cb, char *name);
void ilcore_uninit_comp(ilcore_comp_h h);

/* Tunneling */
ret_code_t ilcore_create_tunnel(ilcore_tunnel_h *h, ilcore_comp_h src_comp, uint32_t src_port, ilcore_comp_h dst_comp,
    uint32_t dst_port);
void ilcore_destroy_tunnel(ilcore_tunnel_h h);

ret_code_t ilcore_flush_tunnel(ilcore_tunnel_h h);
ret_code_t ilcore_setup_tunnel(ilcore_tunnel_h h);
ret_code_t ilcore_clean_tunnel(ilcore_tunnel_h h);

/**/
ret_code_t ilcore_add_comp_event(ilcore_comp_h comp, OMX_EVENTTYPE eEvent, OMX_U32 nData1, OMX_U32 nData2);
ret_code_t ilcore_disable_port(ilcore_comp_h comp, uint32_t port, int wait);
ret_code_t ilcore_enable_port(ilcore_comp_h comp, uint32_t port, int wait);
ret_code_t ilcore_disable_all_ports(ilcore_comp_h comp);
ret_code_t ilcore_set_state(ilcore_comp_h h, OMX_STATETYPE state);
OMX_HANDLETYPE ilcore_get_handle(ilcore_comp_h h);
ret_code_t ilcore_get_state(ilcore_comp_h h, OMX_STATETYPE *state);
OMX_ERRORTYPE omx_core_comp_wait_event(ilcore_comp_h h, OMX_EVENTTYPE eventType, long timeout);
OMX_ERRORTYPE omx_core_comp_wait_command(ilcore_comp_h h, OMX_U32 command, OMX_U32 nData2, long timeout);
ret_code_t ilcore_set_port_buffers_param(ilcore_comp_h h, int size, int count);
char *ilcore_get_comp_name(ilcore_comp_h h);
ret_code_t ilcore_set_param(ilcore_comp_h h, OMX_INDEXTYPE index, OMX_PTR data);
ret_code_t ilcore_get_param(ilcore_comp_h h, OMX_INDEXTYPE index, OMX_PTR data);
ret_code_t ilcore_set_config(ilcore_comp_h h, OMX_INDEXTYPE index, OMX_PTR data);
ret_code_t ilcore_get_config(ilcore_comp_h h, OMX_INDEXTYPE index, OMX_PTR data);

void ilcore_set_app_data(ilcore_comp_h h, void *app_data);
void *ilcore_get_app_data(ilcore_comp_h h);

#endif

