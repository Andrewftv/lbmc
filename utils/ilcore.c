#include "ilcore.h"
#include "msleep.h"
#include "log.h"

#define IL_WAIT_TIMEOUT 100 /*ms*/

typedef struct {
    OMX_HANDLETYPE handle;
    char *name;

    list_h event_list;
    msleep_h event_sleep;
    void *app_data;
} ilcore_comp_ctx_t;

typedef struct {
    ilcore_comp_h src_comp;
    ilcore_comp_h dst_comp;
    uint32_t src_port;  
    uint32_t dst_port;
} omx_tunnel_t;

static char *omx_cmdstrings[] = {
    "OMX_CommandStateSet",
    "OMX_CommandFlush",
    "OMX_CommandPortDisable",
    "OMX_CommandPortEnable",
    "OMX_CommandMarkBuffer",
    "OMX_CommandKhronosExtensions",
    "OMX_CommandVendorStartUnused",
    "OMX_CommandMax"
};
static char *omx_eventstrings[] = {
    "OMX_EventCmdComplete",
    "OMX_EventError",
    "OMX_EventMark",
    "OMX_EventPortSettingsChanged",
    "OMX_EventBufferFlag",
    "OMX_EventResourcesAcquired",
    "OMX_EventComponentResumed",
    "OMX_EventDynamicResourcesAvailable",
    "OMX_EventPortFormatDetected",
    "OMX_EventKhronosExtensions", 
    "OMX_EventVendorStartUnused",
    "OMX_EventParamOrConfigChanged",
    "OMX_EventMax"
};

static char *omx_cmd2str(OMX_COMMANDTYPE cmd)
{
    switch (cmd)
    {
    case OMX_CommandStateSet:
        return omx_cmdstrings[0];
    case OMX_CommandFlush:
        return omx_cmdstrings[1];
    case OMX_CommandPortDisable:
        return omx_cmdstrings[2];
    case OMX_CommandPortEnable:
        return omx_cmdstrings[3];
    case OMX_CommandMarkBuffer:
        return omx_cmdstrings[4];
    case OMX_CommandKhronosExtensions:
        return omx_cmdstrings[5];
    case OMX_CommandVendorStartUnused:
        return omx_cmdstrings[6];
    case OMX_CommandMax:
        return omx_cmdstrings[7];
    }

    return "Unknown";
}

static char *omx_event2str(OMX_EVENTTYPE event)
{
    switch (event)
    {
    case OMX_EventCmdComplete:
        return omx_eventstrings[0];
    case OMX_EventError:
        return omx_eventstrings[1];
    case OMX_EventMark:
        return omx_eventstrings[2];
    case OMX_EventPortSettingsChanged:
        return omx_eventstrings[3];
    case OMX_EventBufferFlag:
        return omx_eventstrings[4];
    case OMX_EventResourcesAcquired:
        return omx_eventstrings[5];
    case OMX_EventComponentResumed:
        return omx_eventstrings[6];
    case OMX_EventDynamicResourcesAvailable:
        return omx_eventstrings[7];
    case OMX_EventPortFormatDetected:
        return omx_eventstrings[8];
    case OMX_EventKhronosExtensions:
        return omx_eventstrings[9];
    case OMX_EventVendorStartUnused:
        return omx_eventstrings[10];
    case OMX_EventParamOrConfigChanged:
        return omx_eventstrings[11];
    case OMX_EventMax:
        return omx_eventstrings[12];
    }

    return "Unknown";
}

static int find_func_cb(list_node_t *node, void *user_data)
{
    omx_event_t *s1 = (omx_event_t *)node;
    omx_event_t *s2 = (omx_event_t *)user_data;

    if (s1->eEvent == s2->eEvent && s1->nData1 == s2->nData1 && s1->nData2 == s2->nData2)
        return 1;

    return 0;
}

static int search_event_cb(list_node_t *node, void *user_data)
{
	omx_event_t *event = (omx_event_t *)node;
	int event_type = *(int *)user_data;

	if (event->eEvent == OMX_EventError || event->eEvent == event_type)
		return 1;

	return 0;
}

OMX_ERRORTYPE il_event_handler(OMX_HANDLETYPE hComponent, OMX_PTR pAppData, OMX_EVENTTYPE eEvent, 
    OMX_U32 nData1, OMX_U32 nData2, OMX_PTR pEventData)
{
    ilcore_add_comp_event(pAppData, eEvent, nData1, nData2);

    return OMX_ErrorNone;
}

OMX_ERRORTYPE il_fill_buffer_done(OMX_HANDLETYPE hComponent, OMX_PTR pAppData, OMX_BUFFERHEADERTYPE* pBuffer)
{
    DBG_I("Fill buffer done\n");

    return OMX_ErrorNone;
}

OMX_ERRORTYPE il_empty_buffer_done(OMX_HANDLETYPE hComponent, OMX_PTR pAppData, OMX_BUFFERHEADERTYPE* pBuffer)
{
    DBG_I("Empty buffer done\n");

    return OMX_ErrorNone;
}

OMX_ERRORTYPE omx_core_comp_wait_event(ilcore_comp_h h, OMX_EVENTTYPE eventType, long timeout)
{
    omx_event_t *event;
    OMX_ERRORTYPE rc = OMX_ErrorNone;
    ilcore_comp_ctx_t *ctx = (ilcore_comp_ctx_t *)h;

    while (1)
    {
        event = (omx_event_t *)slist_find_remove(ctx->event_list, search_event_cb, &eventType);
        if (event)
        {
            if(event->eEvent == OMX_EventError && event->nData1 == (OMX_U32)OMX_ErrorSameState && event->nData2 == 1)
            {
                DBG_I("Component received event: same state\n");
                rc = OMX_ErrorNone;
            }
            else if(event->eEvent == OMX_EventError) 
			{
				rc = (OMX_ERRORTYPE)event->nData1;
                DBG_E("Component received error: %d\n", rc);
			}
            else if(event->eEvent == eventType)
            {
                DBG_I("Got event: '%s', %d events remain in the queue\n", omx_event2str(event->eEvent),
                    slist_get_count(ctx->event_list));
                rc = OMX_ErrorNone;
            }
            else
            {
                DBG_E("Unknown event\n");
                rc = OMX_ErrorMax;
            }
            free(event);

            return rc;
        }

        if (!timeout)
            return OMX_ErrorMax;

        if (msleep_wait(ctx->event_sleep, timeout) == MSLEEP_TIMEOUT)
        {
            DBG_E("Wait event timeout\n");
            return OMX_ErrorMax;
        }
    }

    return OMX_ErrorNone;
}

OMX_ERRORTYPE omx_core_comp_wait_command(ilcore_comp_h h, OMX_U32 command, OMX_U32 nData2, long timeout)
{
    omx_event_t *event, cmp_event;
    OMX_ERRORTYPE rc = OMX_ErrorNone;
    ilcore_comp_ctx_t *ctx = (ilcore_comp_ctx_t *)h;

    cmp_event.eEvent = OMX_EventCmdComplete;
    cmp_event.nData1 = command;
    cmp_event.nData2 = nData2;

    while(1)
    {
        event = (omx_event_t *)slist_find_remove(ctx->event_list, find_func_cb, &cmp_event);
        if (event)
        {
            if(event->eEvent == OMX_EventError && event->nData1 == (OMX_U32)OMX_ErrorSameState && event->nData2 == 1)
            {
                DBG_I("Component received event: same state\n");
                rc = OMX_ErrorNone;
            }
            else if(event->eEvent == OMX_EventError)
            {
                rc = (OMX_ERRORTYPE)event->nData1;
                DBG_E("Component received error: %d\n", rc);
            }
            else if(event->eEvent == OMX_EventCmdComplete && event->nData1 == command && event->nData2 == nData2)
            {
                DBG_I("Command  %s completed\n", omx_cmd2str(command));
                rc = OMX_ErrorNone;
            }
            else
            {
                DBG_E("Unknown event: %s\n", omx_event2str(event->eEvent));
                rc = OMX_ErrorMax;
            }
            free(event);

            return rc;
        }

        if (!timeout)
            return OMX_ErrorMax;

        if (msleep_wait(ctx->event_sleep, timeout) == MSLEEP_TIMEOUT)
        {
            DBG_E("Wait event timeout\n");
            return OMX_ErrorMax;
        }
    }

    return OMX_ErrorNone;
}

static void omx_core_comp_remove(ilcore_comp_ctx_t *comp, OMX_EVENTTYPE eEvent, OMX_U32 nData1, OMX_U32 nData2)
{
    omx_event_t cmp_event;
    list_node_t *node;

    cmp_event.eEvent = eEvent;
    cmp_event.nData1 = nData1;
    cmp_event.nData2 = nData2;

    while ((node = slist_find_remove(comp->event_list, find_func_cb, &cmp_event)) != NULL)
        free(node);
}

ret_code_t ilcore_add_comp_event(ilcore_comp_h h, OMX_EVENTTYPE eEvent, OMX_U32 nData1, OMX_U32 nData2)
{
    omx_event_t *event;
    ilcore_comp_ctx_t *ctx = (ilcore_comp_ctx_t *)h;

    DBG_I("Add event: '%s' '%s' command: '%s' data: %d\n", ctx->name, omx_event2str(eEvent), omx_cmd2str(nData1),
        nData2);

    event = (omx_event_t *)malloc(sizeof(omx_event_t));
    if (!event)
        return OMX_ErrorInsufficientResources;

    event->eEvent = eEvent;
    event->nData1 = nData1;
    event->nData2 = nData2;

    omx_core_comp_remove(ctx, eEvent, nData1, nData2);
    if (slist_add_tail(ctx->event_list, (list_node_t *)event))
    {
        free(event);
        DBG_E("Function slist_add_tail failed\n");
        return L_FAILED;
}
    msleep_wakeup_broadcast(ctx->event_sleep);

    return L_OK;
}

ret_code_t ilcore_disable_all_ports(ilcore_comp_h h)
{
    OMX_PORT_PARAM_TYPE ports;
    OMX_ERRORTYPE err;
    int i, j;
    OMX_INDEXTYPE types[] = {OMX_IndexParamAudioInit, OMX_IndexParamVideoInit, OMX_IndexParamImageInit,
        OMX_IndexParamOtherInit};
    ilcore_comp_ctx_t  *ctx = (ilcore_comp_ctx_t *)h;

    ports.nSize = sizeof(OMX_PORT_PARAM_TYPE);
    ports.nVersion.nVersion = OMX_VERSION;

    for (j = 0; j < 4; j++)
    {
        err = OMX_GetParameter(ctx->handle, types[j], &ports);
        if (err != OMX_ErrorNone)
        {
            DBG_E("Get port paraneters failed. err=%d\n");
            return L_FAILED;
        }
        for(i = 0; i < ports.nPorts; i++)
        {
            DBG_I("Disabling port #%d\n", ports.nStartPortNumber + i);
            err = OMX_SendCommand(ctx->handle, OMX_CommandPortDisable, ports.nStartPortNumber + i, NULL);
            if (err != OMX_ErrorNone)
            {
                DBG_E("Port disable failed. err=%d\n");
                return L_FAILED;
            }
            err = omx_core_comp_wait_command(ctx, OMX_CommandPortDisable, ports.nStartPortNumber + i, IL_WAIT_TIMEOUT);
            if (err != OMX_ErrorNone)
            {
                DBG_E("Wait event failed. err=%d\n");
                return L_FAILED;
            }
        }
    }

    return L_OK;
}

OMX_HANDLETYPE ilcore_get_handle(ilcore_comp_h h)
{
    ilcore_comp_ctx_t *ctx = (ilcore_comp_ctx_t *)h;

    return ctx->handle;
}

ret_code_t ilcore_set_state(ilcore_comp_h h, OMX_STATETYPE state)
{
    OMX_ERRORTYPE err;
    ilcore_comp_ctx_t *ctx = (ilcore_comp_ctx_t *)h;

    err = OMX_SendCommand(ctx->handle, OMX_CommandStateSet, state, NULL);
    if (err != OMX_ErrorNone)
    {
        DBG_E("Change state to idle failed. err=%d\n");
        return L_FAILED;
    }
    err = omx_core_comp_wait_command(ctx, OMX_CommandStateSet, state, IL_WAIT_TIMEOUT);
    if (err != OMX_ErrorNone)
    {
        DBG_E("Wait event failed. err=%d\n");
        return L_FAILED;
    }

    return L_OK;
}

ret_code_t ilcore_get_state(ilcore_comp_h h, OMX_STATETYPE *state)
{
    OMX_ERRORTYPE err;
    ilcore_comp_ctx_t *ctx = (ilcore_comp_ctx_t *)h;

    err = OMX_GetState(ctx->handle, state);
    if (err != OMX_ErrorNone)
    {
        DBG_E("OMX_GetState failed\n");
        return L_FAILED;
    }

    return L_OK;
}

ret_code_t ilcore_set_port_buffers_param(ilcore_comp_h h, int size, int count)
{
    OMX_ERRORTYPE err;
    OMX_PARAM_PORTDEFINITIONTYPE param;
    ilcore_comp_ctx_t *ctx = (ilcore_comp_ctx_t *)h;

    OMX_INIT_STRUCT(param);
    param.nPortIndex = IL_AUDIO_RENDER_IN_PORT;
 
    err = OMX_GetParameter(ctx->handle, OMX_IndexParamPortDefinition, &param);
    if (err != OMX_ErrorNone)
    {
        DBG_E("OMX_IndexParamPortDefinition failed. err=%d\n");
        return L_FAILED;
    }
    param.nBufferSize = size;
    param.nBufferCountActual = count;

    err = OMX_SetParameter(ctx->handle, OMX_IndexParamPortDefinition, &param);
    if (err != OMX_ErrorNone)
    {
        DBG_E("OMX_IndexParamPortDefinition failed. err=%d\n");
        return L_FAILED;
    }
    return L_OK;
}

char *ilcore_get_comp_name(ilcore_comp_h h)
{
    ilcore_comp_ctx_t  *ctx = (ilcore_comp_ctx_t *)h;

    return ctx->name;
}

ret_code_t ilcore_disable_port(ilcore_comp_h h, uint32_t port, int wait)
{
    ilcore_comp_ctx_t  *ctx = (ilcore_comp_ctx_t *)h;
    OMX_PARAM_PORTDEFINITIONTYPE param;
    OMX_ERRORTYPE omx_err;

    DBG_I("%s: disable port %d on comp %s\n", __FUNCTION__, port, ilcore_get_comp_name(h));

    OMX_INIT_STRUCT(param);
    param.nPortIndex = port;

    omx_err = OMX_GetParameter(ctx->handle, OMX_IndexParamPortDefinition, &param);
    if(omx_err != OMX_ErrorNone)
    {
        DBG_E("%s: Error get port %d status on component %s err = 0x%08x\n", __FUNCTION__, port,
            ilcore_get_comp_name(h), omx_err);
        return L_FAILED;
    }

    if(param.bEnabled == OMX_FALSE)
        return L_OK; /* Port already disable */

    omx_err = OMX_SendCommand(ctx->handle, OMX_CommandPortDisable, port, NULL);
    if(omx_err != OMX_ErrorNone)
    {
        DBG_E("%s: Error disable port %d on component %s err = 0x%08x\n", __FUNCTION__, port, ilcore_get_comp_name(h),
            omx_err);
        return L_FAILED;
    }

    if (wait)
    {
        omx_err = omx_core_comp_wait_command(h, OMX_CommandPortDisable, port, 2000);
        if(omx_err != OMX_ErrorNone)
            return L_FAILED;
    }
    return L_OK;
}

ret_code_t ilcore_enable_port(ilcore_comp_h h, uint32_t port, int wait)
{
    ilcore_comp_ctx_t  *ctx = (ilcore_comp_ctx_t *)h;
    OMX_PARAM_PORTDEFINITIONTYPE param;
    OMX_ERRORTYPE omx_err;

    DBG_I("%s: enable port %d on comp %s\n", __FUNCTION__, port, ilcore_get_comp_name(h));

    OMX_INIT_STRUCT(param);
    param.nPortIndex = port;

    omx_err = OMX_GetParameter(ctx->handle, OMX_IndexParamPortDefinition, &param);
    if(omx_err != OMX_ErrorNone)
    {
        DBG_E("%s: Error get port %d status on component %s err = 0x%08x\n", __FUNCTION__, port,
            ilcore_get_comp_name(h), omx_err);
        return L_FAILED;
    }

    if(param.bEnabled == OMX_TRUE)
        return L_OK; /* Port already enable */

    omx_err = OMX_SendCommand(ctx->handle, OMX_CommandPortEnable, port, NULL);
    if(omx_err != OMX_ErrorNone)
    {
        DBG_E("%s: Error disable port %d on component %s err = 0x%08x\n", __FUNCTION__, port, ilcore_get_comp_name(h),
            omx_err);
        return L_FAILED;
    }

    if (wait)
    {
        omx_err = omx_core_comp_wait_command(h, OMX_CommandPortEnable, port, 2000);
        if(omx_err != OMX_ErrorNone)
            return L_FAILED;
    }
    return L_OK;
}

ret_code_t ilcore_set_param(ilcore_comp_h h, OMX_INDEXTYPE index, OMX_PTR data)
{
    OMX_ERRORTYPE err;
    ilcore_comp_ctx_t  *ctx = (ilcore_comp_ctx_t *)h;

    err = OMX_SetParameter(ctx->handle, index, data);
    if(err != OMX_ErrorNone) 
	{
		DBG_E("%s: %s failed with err = 0x%x\n", __FUNCTION__, ctx->name, err);
        return L_FAILED;
	}

    return L_OK;
}

ret_code_t ilcore_get_param(ilcore_comp_h h, OMX_INDEXTYPE index, OMX_PTR data)
{
    OMX_ERRORTYPE err;
    ilcore_comp_ctx_t  *ctx = (ilcore_comp_ctx_t *)h;

    err = OMX_GetParameter(ctx->handle, index, data);
    if(err != OMX_ErrorNone) 
	{
		DBG_E("%s: %s failed with err = 0x%x\n", __FUNCTION__, ctx->name, err);
        return L_FAILED;
	}

    return L_OK;
}

ret_code_t ilcore_set_config(ilcore_comp_h h, OMX_INDEXTYPE index, OMX_PTR data)
{
    OMX_ERRORTYPE err;
    ilcore_comp_ctx_t  *ctx = (ilcore_comp_ctx_t *)h;

    err = OMX_SetConfig(ctx->handle, index, data);
    if(err != OMX_ErrorNone) 
	{
		DBG_E("%s: %s failed with err = 0x%x\n", __FUNCTION__, ctx->name, err);
        return L_FAILED;
	}

    return L_OK;
}

ret_code_t ilcore_get_config(ilcore_comp_h h, OMX_INDEXTYPE index, OMX_PTR data)
{
    OMX_ERRORTYPE err;
    ilcore_comp_ctx_t  *ctx = (ilcore_comp_ctx_t *)h;

    err = OMX_GetConfig(ctx->handle, index, data);
    if(err != OMX_ErrorNone) 
	{
		DBG_E("%s: %s failed with err = 0x%x\n", __FUNCTION__, ctx->name, err);
        return L_FAILED;
	}

    return L_OK;
}

void ilcore_set_app_data(ilcore_comp_h h, void *app_data)
{
    ilcore_comp_ctx_t  *ctx = (ilcore_comp_ctx_t *)h;

    ctx->app_data = app_data;
}

void *ilcore_get_app_data(ilcore_comp_h h)
{
    ilcore_comp_ctx_t  *ctx = (ilcore_comp_ctx_t *)h;

    return ctx->app_data;
}

ret_code_t ilcore_init_comp(ilcore_comp_h *h, OMX_CALLBACKTYPE *cb, char *name)
{
    OMX_ERRORTYPE err;
    ilcore_comp_ctx_t *ctx;

    ctx = (ilcore_comp_ctx_t *)malloc(sizeof(ilcore_comp_ctx_t));
    if (!ctx)
    {
        DBG_E("Memory allocation failed\n");
        return L_FAILED;
    }
    memset(ctx, 0, sizeof(ilcore_comp_ctx_t));

    slist_init(&ctx->event_list);
    msleep_init(&ctx->event_sleep);
    
    ctx->name = strdup(name);
    if (!ctx->name)
    {
        DBG_E("strdup failed\n");
        return L_FAILED;
    }

    err = OMX_GetHandle(&ctx->handle, name, ctx, cb);
    if (err != OMX_ErrorNone)
    {
        DBG_E("OMX_GetHandle failed. err=%d\n", err);
        return L_FAILED;
    }

    *h = ctx;

    return L_OK;
}

void ilcore_uninit_comp(ilcore_comp_h h)
{
    OMX_ERRORTYPE err;
    ilcore_comp_ctx_t  *ctx = (ilcore_comp_ctx_t *)h;

    if (!ctx)
        return;

    if (ctx->name)
        free(ctx->name);

    err = OMX_FreeHandle(ctx->handle);
    if (err != OMX_ErrorNone)
        DBG_E("OMX_FreeHandle failed. err=%d\n", err);

    msleep_uninit(ctx->event_sleep);
    slist_uninit(ctx->event_list);

    free(ctx);
}

/* Tunneling */
ret_code_t ilcore_create_tunnel(ilcore_tunnel_h *h, ilcore_comp_h src_comp, uint32_t src_port, ilcore_comp_h dst_comp,
    uint32_t dst_port)
{
    omx_tunnel_t *tunnel;

    DBG_I("%s: src comp %s srs port %d dst comp %s dst port %d\n", __FUNCTION__, ilcore_get_comp_name(src_comp),
        src_port, ilcore_get_comp_name(dst_comp), dst_port);

    tunnel = (omx_tunnel_t *)malloc(sizeof(omx_tunnel_t));
    if (!tunnel)
    {
        DBG_E("Memory allocation failed\n");
        return L_FAILED;
    }

    memset(tunnel, 0, sizeof(omx_tunnel_t));

    tunnel->src_comp = src_comp;
    tunnel->src_port = src_port;
    tunnel->dst_comp = dst_comp;
    tunnel->dst_port = dst_port;

    *h = tunnel;

    return L_OK;
}

void ilcore_destroy_tunnel(ilcore_tunnel_h h)
{
    if (h)
        free(h);
}

ret_code_t ilcore_flush_tunnel(ilcore_tunnel_h h)
{
    OMX_ERRORTYPE omx_err;
    omx_tunnel_t *tunnel = (omx_tunnel_t *)h;

    if (!tunnel || !tunnel->src_comp || !tunnel->dst_comp)
    {
        DBG_E("%s: Incorrect parameters\n", __FUNCTION__);
        return L_FAILED;
    }

    omx_err = OMX_SendCommand(ilcore_get_handle(tunnel->src_comp), OMX_CommandFlush, tunnel->src_port, NULL);
    if(omx_err != OMX_ErrorNone && omx_err != OMX_ErrorSameState)
    {
        DBG_E("%s: Error flush port %d on component %s err = 0x%08x\n", __FUNCTION__, tunnel->src_port,
            ilcore_get_comp_name(tunnel->src_comp), omx_err);
    }

    omx_err = OMX_SendCommand(ilcore_get_handle(tunnel->dst_comp), OMX_CommandFlush, tunnel->dst_port, NULL);
    if(omx_err != OMX_ErrorNone && omx_err != OMX_ErrorSameState)
    {
        DBG_E("%s: Error flush port %d on component %s err = 0x%08x\n", __FUNCTION__, tunnel->dst_port,
            ilcore_get_comp_name(tunnel->dst_comp), omx_err);
    }

    omx_core_comp_wait_command(tunnel->src_comp, OMX_CommandFlush, tunnel->src_port, 2000);
    omx_core_comp_wait_command(tunnel->dst_comp, OMX_CommandFlush, tunnel->dst_port, 2000);

    return L_OK;
}

ret_code_t ilcore_setup_tunnel(ilcore_tunnel_h h)
{
    omx_tunnel_t *tunnel = (omx_tunnel_t *)h;
    OMX_STATETYPE state;
    OMX_ERRORTYPE omx_err;

    if (!tunnel || !tunnel->src_comp || !tunnel->dst_comp)
    {
        DBG_E("%s: Incorrect parameters\n", __FUNCTION__);
        return L_FAILED;
    }

    if (ilcore_get_state(tunnel->src_comp, &state) != L_OK)
        return L_FAILED;

    if (state == OMX_StateLoaded)
    {
        if (ilcore_set_state(tunnel->src_comp, OMX_StateIdle))
            return L_FAILED;
    }

    if (ilcore_disable_port(tunnel->src_comp, tunnel->src_port, 0) != L_OK)
        return L_FAILED;
    if (ilcore_disable_port(tunnel->dst_comp, tunnel->dst_port, 0) != L_OK)
        return L_FAILED;

    omx_err = OMX_SetupTunnel(ilcore_get_handle(tunnel->src_comp), tunnel->src_port,
        ilcore_get_handle(tunnel->dst_comp), tunnel->dst_port);
    if(omx_err != OMX_ErrorNone) 
    {
        DBG_E("%s: could not setup tunnel src %s port %d dst %s port %d err = 0x%08x\n", __FUNCTION__, 
            ilcore_get_comp_name(tunnel->src_comp), tunnel->src_port, ilcore_get_comp_name(tunnel->dst_comp),
            tunnel->dst_port, omx_err);
        return L_FAILED;
    }

    if (ilcore_enable_port(tunnel->src_comp, tunnel->src_port, 0) != L_OK)
        return L_FAILED;
    if (ilcore_enable_port(tunnel->dst_comp, tunnel->dst_port, 0) != L_OK)
        return L_FAILED;

    if (ilcore_get_state(tunnel->dst_comp, &state) != L_OK)
        return L_FAILED;
    
    omx_err = omx_core_comp_wait_command(tunnel->dst_comp, OMX_CommandPortEnable, tunnel->dst_port, 2000);
    if(omx_err != OMX_ErrorNone)
        return L_FAILED;
    
    if (state == OMX_StateLoaded)
    {
        if (ilcore_set_state(tunnel->dst_comp, OMX_StateIdle))
            return L_FAILED;
    }
    omx_err = omx_core_comp_wait_command(tunnel->src_comp, OMX_CommandPortEnable, tunnel->src_port, 2000);
    if(omx_err != OMX_ErrorNone)
        return L_FAILED;

    return L_OK;
}

ret_code_t ilcore_clean_tunnel(ilcore_tunnel_h h)
{
    omx_tunnel_t *tunnel = (omx_tunnel_t *)h;
    OMX_ERRORTYPE omx_err;

    if (!tunnel || !tunnel->src_comp || !tunnel->dst_comp)
    {
        DBG_E("%s: Incorrect parameters\n", __FUNCTION__);
        return L_FAILED;
    }

    ilcore_disable_port(tunnel->src_comp, tunnel->src_port, 0);
    ilcore_disable_port(tunnel->dst_comp, tunnel->dst_port, 0);

    omx_err = OMX_SetupTunnel(ilcore_get_handle(tunnel->src_comp), tunnel->src_port, NULL, 0);
    if(omx_err != OMX_ErrorNone) 
    {
        DBG_E("%s: could not unset tunnel on comp src %s port %d err = 0x%08x\n", __FUNCTION__, 
            ilcore_get_comp_name(tunnel->src_comp), tunnel->src_port, omx_err);
    }
    omx_err = OMX_SetupTunnel(ilcore_get_handle(tunnel->dst_comp), tunnel->dst_port, NULL, 0);
    if(omx_err != OMX_ErrorNone) 
    {
        DBG_E("%s: could not unset tunnel on comp dst %s port %d err = 0x%08x\n", __FUNCTION__, 
            ilcore_get_comp_name(tunnel->dst_comp), tunnel->dst_port, omx_err);
    }

    return L_OK;
}

