#include "ilcore.h"
#include "msleep.h"
#include "log.h"

#define IL_WAIT_TIMEOUT 100 /*ms*/

typedef struct {
    OMX_HANDLETYPE handle;
    char *name;

    list_h event_list;
    msleep_h event_sleep;
} ilcore_comp_ctx_t;

static char *omx_cmdstrings[] = {"OMX_CommandStateSet", "OMX_CommandFlush", "OMX_CommandPortDisable",
    "OMX_CommandPortEnable", "OMX_CommandMarkBuffer", "OMX_CommandKhronosExtensions", "OMX_CommandVendorStartUnused",
    "OMX_CommandMax"};
static char *omx_eventstrings[] = {"OMX_EventCmdComplete", "OMX_EventError", "OMX_EventMark",
    "OMX_EventPortSettingsChanged", "OMX_EventBufferFlag", "OMX_EventResourcesAcquired", "OMX_EventComponentResumed",
    "OMX_EventDynamicResourcesAvailable", "OMX_EventPortFormatDetected", "OMX_EventKhronosExtensions", 
    "OMX_EventVendorStartUnused", "OMX_EventParamOrConfigChanged", "OMX_EventMax" };

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

	if (s1->eEvent == s2->eEvent && s1->nData1 == s2->nData1 &&
		s1->nData2 == s2->nData2)
	{
		return 1;
	}

	return 0;
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

    DBG_I("Add event: '%s' command: '%s' data: %d\n", omx_event2str(eEvent), omx_cmd2str(nData1), nData2);

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

    memset(&param, 0, sizeof(OMX_PARAM_PORTDEFINITIONTYPE));
    param.nSize = sizeof(OMX_PARAM_PORTDEFINITIONTYPE);
    param.nVersion.nVersion = OMX_VERSION;
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

