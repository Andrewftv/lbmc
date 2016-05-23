#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

#include "jpeg.h"
#include "log.h"
#include "ilcore.h"
#include "queue.h"
#include "msleep.h"

typedef struct {
    queue_node_t node;
    OMX_BUFFERHEADERTYPE *buff;
    int size;
} buff_header_t;

typedef struct {
    int fd;

    ilcore_comp_h decoder;
    ilcore_comp_h resize;
    ilcore_tunnel_h tunnel;

    queue_h in_queue;
    OMX_BUFFERHEADERTYPE *out_buffer;

    int done;
    msleep_h fill_done;
} jpeg_ctx_t;

static OMX_ERRORTYPE jpeg_empty_buffer_done(OMX_HANDLETYPE hComponent, OMX_PTR pAppData, OMX_BUFFERHEADERTYPE* pBuffer)
{
    buff_header_t *buf = (buff_header_t *)pBuffer->pAppPrivate;
    jpeg_ctx_t *ctx = (jpeg_ctx_t *)ilcore_get_app_data(pAppData);

    if (!ctx || !buf)
    {
        DBG_E("Incorrect parameters: ctx - %p, buff - %p\n", ctx, buf);
        return OMX_ErrorBadParameter;
    }
    queue_push(ctx->in_queue, (queue_node_t *)buf);

    DBG_I("Empty buffer done\n");

    return OMX_ErrorNone;
}

static OMX_ERRORTYPE jpeg_fill_buffer_done(OMX_HANDLETYPE hComponent, OMX_PTR pAppData, OMX_BUFFERHEADERTYPE* pBuffer)
{
    jpeg_ctx_t *ctx = (jpeg_ctx_t *)pBuffer->pAppPrivate;

    DBG_I("Fill buffer done. Length = %d\n", pBuffer->nFilledLen);

    ctx->done = 1;
    msleep_wakeup(ctx->fill_done);

    return OMX_ErrorNone;
}

static ret_code_t jpeg_comp_init(ilcore_comp_h *comp, char *comp_name)
{
    OMX_CALLBACKTYPE cb;

    cb.EventHandler = il_event_handler;
    cb.EmptyBufferDone = jpeg_empty_buffer_done;
    cb.FillBufferDone = jpeg_fill_buffer_done;

    if (ilcore_init_comp(comp, &cb, comp_name))
        return L_FAILED;

    if (ilcore_disable_all_ports(*comp))
        return L_FAILED;

    return L_OK;
}

static ret_code_t jpeg_setup_decoder(jpeg_ctx_t *ctx)
{
    OMX_IMAGE_PARAM_PORTFORMATTYPE img_format;
    OMX_PARAM_PORTDEFINITIONTYPE portdef;
    int i;
    buff_header_t *hdr;
    OMX_ERRORTYPE err;

    ilcore_set_state(ctx->decoder, OMX_StateIdle);

    OMX_INIT_STRUCT(img_format);
    img_format.nPortIndex = IL_IMAGE_DECODER_IN_PORT;
    img_format.eCompressionFormat = OMX_IMAGE_CodingJPEG;

    if (ilcore_set_param(ctx->decoder, OMX_IndexParamImagePortFormat, &img_format) != L_OK)
        return L_FAILED;

    OMX_INIT_STRUCT(portdef);
    portdef.nPortIndex = IL_IMAGE_DECODER_IN_PORT;

    if (ilcore_get_param(ctx->decoder, OMX_IndexParamPortDefinition, &portdef) != L_OK)
        return L_FAILED;

    DBG_I("Input buffers: amount=%d size=%d\n", portdef.nBufferCountActual, portdef.nBufferSize);

    if (ilcore_enable_port(ctx->decoder, IL_IMAGE_DECODER_IN_PORT, 0) != L_OK)
        return L_FAILED;

    for (i = 0; i < portdef.nBufferCountActual; i++)
    {
        hdr = (buff_header_t *)malloc(sizeof(buff_header_t));
        if (!hdr)
            return L_FAILED;

        if (OMX_AllocateBuffer(ilcore_get_handle(ctx->decoder), &hdr->buff, IL_IMAGE_DECODER_IN_PORT,
            NULL, portdef.nBufferSize) != OMX_ErrorNone)
        {
            return L_FAILED;
        }
        hdr->buff->nInputPortIndex = IL_IMAGE_DECODER_IN_PORT;
        hdr->buff->pAppPrivate = hdr;
        hdr->size = portdef.nBufferSize;

        queue_push(ctx->in_queue, (queue_node_t *)hdr);
    }

    err = omx_core_comp_wait_command(ctx->decoder, OMX_CommandPortEnable, IL_IMAGE_DECODER_IN_PORT, 2000);
    if (err != OMX_ErrorNone)
    {
        DBG_I("Did not get port enable\n");
        return L_FAILED;
    }

    ilcore_set_state(ctx->decoder, OMX_StateExecuting);

    return L_OK;
}

static int get_file_size(int fd)
{
    struct stat s;

    if (fstat(fd, &s) < 0)
        return -1;

    return s.st_size;
}

static ret_code_t jpeg_port_settings_changed(jpeg_ctx_t *ctx)
{
    OMX_PARAM_PORTDEFINITIONTYPE portdef;
    uint32_t w, h;
    OMX_ERRORTYPE err;

    OMX_INIT_STRUCT(portdef);
    portdef.nPortIndex = IL_IMAGE_DECODER_OUT_PORT;

    if (ilcore_get_param(ctx->decoder, OMX_IndexParamPortDefinition, &portdef) != L_OK)
        return L_FAILED;

    w = portdef.format.image.nFrameWidth;
    h = portdef.format.image.nFrameHeight;

    DBG_I("JPEG image size %dx%d\n", w, h);

    portdef.nPortIndex = IL_IMAGE_RESIZE_IN_PORT;
    if (ilcore_set_param(ctx->resize, OMX_IndexParamPortDefinition, &portdef) != L_OK)
        return L_FAILED;

    if (ilcore_create_tunnel(&ctx->tunnel, ctx->decoder, IL_IMAGE_DECODER_OUT_PORT, ctx->resize,
        IL_IMAGE_RESIZE_IN_PORT))
    {
        return L_FAILED;
    }
    if (ilcore_setup_tunnel(ctx->tunnel))
        return L_FAILED;

    omx_core_comp_wait_event(ctx->resize, OMX_EventPortSettingsChanged, 500);

    ilcore_disable_port(ctx->resize, IL_IMAGE_RESIZE_OUT_PORT, 1);

    OMX_INIT_STRUCT(portdef);
    portdef.nPortIndex = IL_IMAGE_RESIZE_OUT_PORT;
    if (ilcore_get_param(ctx->resize, OMX_IndexParamPortDefinition, &portdef) != L_OK)
        return L_FAILED;

    portdef.format.image.eCompressionFormat = OMX_IMAGE_CodingUnused;
    portdef.format.image.eColorFormat = OMX_COLOR_Format32bitABGR8888;
    portdef.format.image.nFrameWidth = w;
    portdef.format.image.nFrameHeight = h;
    portdef.format.image.nStride = 0;
    portdef.format.image.nSliceHeight = 0;
    portdef.format.image.bFlagErrorConcealment = OMX_FALSE;
    if (ilcore_set_param(ctx->resize, OMX_IndexParamPortDefinition, &portdef) != L_OK)
        return L_FAILED;

    if (ilcore_get_param(ctx->resize, OMX_IndexParamPortDefinition, &portdef) != L_OK)
        return L_FAILED;

    ilcore_set_state(ctx->resize, OMX_StateExecuting);

    DBG_I("Width: %u Height: %u Output Color Format: 0x%x Buffer Size: %u\n", portdef.format.image.nFrameWidth,
	    portdef.format.image.nFrameHeight, portdef.format.image.eColorFormat, portdef.nBufferSize);

    ilcore_enable_port(ctx->resize, IL_IMAGE_RESIZE_OUT_PORT, 0);
    err = OMX_AllocateBuffer(ilcore_get_handle(ctx->resize), &ctx->out_buffer, IL_IMAGE_RESIZE_OUT_PORT,
        NULL, portdef.nBufferSize);
    if (err != OMX_ErrorNone)
    {
        DBG_E("Unable to allocate output buffer\n");
        return L_FAILED;
    }

    err = omx_core_comp_wait_command(ctx->resize, OMX_CommandPortEnable, IL_IMAGE_RESIZE_OUT_PORT, 2000);
    if (err != OMX_ErrorNone)
    {
        DBG_I("Did not get port enable\n");
        return L_FAILED;
    }

    return L_OK;
}

ret_code_t jpeg_decode(jpeg_h h)
{
    jpeg_ctx_t *ctx = (jpeg_ctx_t *)h;
    buff_header_t *hdr;
    int img_size, size;
    OMX_ERRORTYPE err;
    
    img_size = get_file_size(ctx->fd);
    if (img_size < 0)
        return L_FAILED;

    DBG_I("Decode JPEG file. size=%d\n", img_size);

    while (img_size > 0)
    {
        hdr = (buff_header_t *)queue_pop(ctx->in_queue);
        if (!hdr)
            return L_FAILED;

        size = read(ctx->fd, hdr->buff->pBuffer, hdr->size);
        if (size < 0)
            return L_FAILED;

        img_size -= size;

        hdr->buff->nFilledLen = size;
        hdr->buff->nOffset = 0;
        hdr->buff->nFlags = 0;
        if (!img_size)
            hdr->buff->nFlags = OMX_BUFFERFLAG_EOS;

        err = OMX_EmptyThisBuffer(ilcore_get_handle(ctx->decoder), hdr->buff);
        if (err != OMX_ErrorNone)
        {
            DBG_E("OMX_EmptyThisBuffer failed\n");
            return L_FAILED;
        }
    }
    err = omx_core_comp_wait_event(ctx->decoder, OMX_EventPortSettingsChanged, 500);
    if (err != OMX_ErrorNone)
    {
        DBG_E("OMX_EventPortSettingsChanged not received. Timeout\n");
    }
    else
    {
        DBG_I("Got OMX_EventPortSettingsChanged\n");
    }
    jpeg_port_settings_changed(ctx);

    ctx->done = 0;
    ctx->out_buffer->pAppPrivate = ctx;
    err = OMX_FillThisBuffer(ilcore_get_handle(ctx->resize), ctx->out_buffer);
    if (err != OMX_ErrorNone)
    {
        DBG_E("OMX_FillThisBuffer failed\n");
        return L_FAILED;
    }

    err = omx_core_comp_wait_event(ctx->decoder, OMX_EventBufferFlag, 500);
    if (err != OMX_ErrorNone)
    {
        DBG_E("Decoder did not receive OMX_EventBufferFlag\n");
    }
    err = omx_core_comp_wait_event(ctx->resize, OMX_EventBufferFlag, 500);
    if (err != OMX_ErrorNone)
    {
        DBG_E("Resizer did not receive OMX_EventBufferFlag\n");
    }

    if (!ctx->done)
    {
        if (msleep_wait(ctx->fill_done, 500) == MSLEEP_TIMEOUT)
            return L_FAILED;
    }

    DBG_I("JPEG image successfuly decoded\n");

    return L_OK;
}

ret_code_t jpeg_init(jpeg_h *h, char *file)
{
    jpeg_ctx_t *ctx;
    ret_code_t rc = L_OK;

    *h = NULL;
    ctx = (jpeg_ctx_t *)malloc(sizeof(jpeg_ctx_t));
    if (!ctx)
    {
        DBG_E("Unable to allocate memory\n");
        return L_MEMORY;
    }
    memset(ctx, 0, sizeof(jpeg_ctx_t));

    msleep_init(&ctx->fill_done);
    queue_init(&ctx->in_queue);

    ctx->fd = open(file, O_RDONLY);
    if (ctx->fd < 0)
    {
        DBG_E("File: %s not found\n", file);
        rc = L_FAILED;
        goto Error;
    }

    if (jpeg_comp_init(&ctx->decoder, "OMX.broadcom.image_decode") != L_OK)
    {
        DBG_E("Unable to create image decoder\n");
        rc = L_FAILED;
        goto Error;
    }
    ilcore_set_app_data(ctx->decoder, ctx);

    if (jpeg_comp_init(&ctx->resize, "OMX.broadcom.resize") != L_OK)
    {
        DBG_E("Unable to create image resizer\n");
        rc = L_FAILED;
        goto Error;
    }

    if (jpeg_setup_decoder(ctx) != L_OK)
    {
        DBG_E("Unable to setup image decoder\n");
        rc = L_FAILED;
        goto Error;
    }

    *h = ctx;

    return L_OK;

Error:
    jpeg_uninit(ctx);
    
    return rc;
}

void jpeg_uninit(jpeg_h h)
{
    jpeg_ctx_t *ctx = (jpeg_ctx_t *)h;
    buff_header_t *hdr;

    if (!ctx)
        return;

    if (ctx->tunnel)
    {
        ilcore_flush_tunnel(ctx->tunnel);
        ilcore_clean_tunnel(ctx->tunnel);
        ilcore_destroy_tunnel(ctx->tunnel);
    }
    ilcore_set_state(ctx->decoder, OMX_StateIdle);
    ilcore_set_state(ctx->resize, OMX_StateIdle);

    ilcore_disable_port(ctx->resize, IL_IMAGE_RESIZE_OUT_PORT, 0);
    OMX_FreeBuffer(ilcore_get_handle(ctx->resize), IL_IMAGE_RESIZE_OUT_PORT, ctx->out_buffer);
    omx_core_comp_wait_command(ctx->resize, OMX_CommandPortDisable, IL_IMAGE_RESIZE_OUT_PORT, 2000);

    ilcore_disable_port(ctx->decoder, IL_IMAGE_DECODER_IN_PORT, 0);
    while ((hdr = (buff_header_t *)queue_pop(ctx->in_queue)) != NULL)
    {
        OMX_FreeBuffer(ilcore_get_handle(ctx->decoder), IL_IMAGE_DECODER_IN_PORT, hdr->buff);
        free(hdr);
    }
    omx_core_comp_wait_command(ctx->decoder, OMX_CommandPortDisable, IL_IMAGE_DECODER_IN_PORT, 2000);

    queue_uninit(ctx->in_queue);
    msleep_uninit(ctx->fill_done);

    if (ctx->fd > 0)
        close(ctx->fd);

    if (ctx->decoder)
        ilcore_uninit_comp(ctx->decoder);
    if (ctx->resize)
        ilcore_uninit_comp(ctx->resize);

    free(ctx);
}

