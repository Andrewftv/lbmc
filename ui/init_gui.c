#include <bcm_host.h>
#include <interface/vmcs_host/vc_dispmanx_types.h>
#include <EGL/eglplatform.h>
#include <EGL/egl.h>
#include <VG/openvg.h>

#include "guiapi.h"
#include "log.h"
//#include "Unicode.h"

static pthread_mutex_t gui_mutex = PTHREAD_MUTEX_INITIALIZER;

typedef struct {
	DISPMANX_ELEMENT_HANDLE_T dispman_element;
	DISPMANX_DISPLAY_HANDLE_T dispman_display;
	EGL_DISPMANX_WINDOW_T nativewindow;
	EGLSurface surface;

	gui_h hgui;	

	VC_RECT_T win_rect;
} player_win_ctx_t;

typedef struct {
   	uint32_t screen_width;
   	uint32_t screen_height;
   	EGLDisplay display;
	EGLConfig config;
	EGLContext context;

	FT_Library ft_library;
} omx_player_gui_ctx_t;

int gui_lock(void)
{
	return pthread_mutex_lock(&gui_mutex);
}

void gui_unlock(void)
{
	pthread_mutex_unlock(&gui_mutex);
}

EGLDisplay gui_get_display(gui_h hgui)
{
	omx_player_gui_ctx_t *ctx = (omx_player_gui_ctx_t *)hgui;

	return ctx->display;
}

EGLContext gui_get_context(gui_h hgui)
{
	omx_player_gui_ctx_t *ctx = (omx_player_gui_ctx_t *)hgui;
	
	return ctx->context;
}

EGLSurface gui_win_get_context(win_h hwin)
{
	player_win_ctx_t *ctx = (player_win_ctx_t *)hwin;

	return ctx->surface;
}

int gui_activate_window(win_h hwin)
{
	gui_h hgui;
	EGLDisplay display;
	EGLContext context;
	EGLSurface surface;

	if (!hwin)
		return -1;

	hgui = gui_win_get_ui_handler(hwin);
	if (!hgui)
		return -1;

	display = gui_get_display(hgui);
	context = gui_get_context(hgui);
	surface = gui_win_get_context(hwin);

	gui_lock();

	if (!eglBindAPI(EGL_OPENVG_API))
	{
		DBG_E("%s: eglBindAPI failed\n", __FUNCTION__);
		return -1;
	}
	if (!eglMakeCurrent(display, surface, surface, context))
	{
		DBG_E("%s: eglMakeCurrent failed - 0x%x\n",
			__FUNCTION__, vgGetError());
		return -1;
	}

	return 0;
}

void gui_release_window(win_h hwin)
{
	gui_h hgui;
	EGLDisplay display;

	if (!hwin)
		return;

	hgui = gui_win_get_ui_handler(hwin);
	if (!hgui)
		return;

	display = gui_get_display(hgui);
	if (display)
		eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);

	gui_unlock();
}

void gui_uninit(gui_h hgui)
{
	omx_player_gui_ctx_t *gui_ctx = (omx_player_gui_ctx_t *)hgui;

	if (!gui_ctx)
		return;

	if (gui_ctx->display != EGL_NO_DISPLAY)
	{
		if (gui_ctx->context != EGL_NO_CONTEXT)
   			eglDestroyContext(gui_ctx->display, gui_ctx->context);
   		eglTerminate(gui_ctx->display);
	}

	if (gui_ctx->ft_library)
		FT_Done_FreeType(gui_ctx->ft_library);

	free(gui_ctx);
}

int gui_init(gui_h *hgui)
{
	EGLint num_config;
	omx_player_gui_ctx_t *gui_ctx;
	VGfloat color[4] = { 0.0f, 0.0f, 0.0f, 0.0f }; /* XXX Why float? */

	static const EGLint attribute_list_rgb32a[] =
	{
		EGL_RED_SIZE, 8,
		EGL_GREEN_SIZE, 8,
		EGL_BLUE_SIZE, 8,
		EGL_ALPHA_SIZE, 8,
		EGL_RENDERABLE_TYPE, EGL_OPENVG_BIT,
		EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
		EGL_NONE
	};
#if 0
	static const EGLint attribute_list_rgb888[] =
	{
		EGL_RED_SIZE, 8,
		EGL_GREEN_SIZE, 8,
		EGL_BLUE_SIZE, 8,
		EGL_ALPHA_SIZE, 0,
		EGL_RENDERABLE_TYPE, EGL_OPENVG_BIT,
		EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
		EGL_NONE
	};
#endif
	gui_ctx = (omx_player_gui_ctx_t *)malloc(sizeof(omx_player_gui_ctx_t));
	if (!gui_ctx)
	{
		DBG_E("%s: Memory allocation failed\n", __FUNCTION__);
		return -1;
	}

	memset(gui_ctx, 0, sizeof(omx_player_gui_ctx_t));
   
	if (FT_Init_FreeType(&gui_ctx->ft_library))
	{
		DBG_E("%s: FreeType initialization failed\n", __FUNCTION__);
		goto Error;
	}

	gui_ctx->display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
	if (gui_ctx->display == EGL_NO_DISPLAY)
	{
		DBG_E("%s: eglGetDisplay failed\n", __FUNCTION__);
		goto Error;
	}

	if (eglInitialize(gui_ctx->display, NULL, NULL) == EGL_FALSE)
	{
		DBG_E("%s: eglInitialize failed\n", __FUNCTION__);
		goto Error;
	}

	eglBindAPI(EGL_OPENVG_API);
#if 0
	if (eglChooseConfig(gui_ctx->display, attribute_list_rgb888, &gui_ctx->config, 1,
		&num_config) == EGL_FALSE)
	{
		DBG_E("%s: eglChooseConfig failed\n", __FUNCTION__);
		goto Error;
	}

	gui_ctx->context = eglCreateContext(gui_ctx->display, gui_ctx->config,
		EGL_NO_CONTEXT, NULL);
	if (gui_ctx->context == EGL_NO_CONTEXT)
	{
		DBG_E("%s: eglCreateContext failed\n", __FUNCTION__);
		goto Error;
	}
#endif
    if (eglChooseConfig(gui_ctx->display, attribute_list_rgb32a, &gui_ctx->config, 1, &num_config) == EGL_FALSE)
	{
		DBG_E("%s: eglChooseConfig failed\n", __FUNCTION__);
		goto Error;
	}

	gui_ctx->context = eglCreateContext(gui_ctx->display, gui_ctx->config,
		EGL_NO_CONTEXT, NULL);
	if (gui_ctx->context == EGL_NO_CONTEXT)
	{
		DBG_E("%s: eglCreateContext failed\n", __FUNCTION__);
		goto Error;
	}
	if (graphics_get_display_size(0 /* LCD */, &gui_ctx->screen_width,
		&gui_ctx->screen_height) < 0)
	{
		DBG_E("%s: graphics_get_display_size failed\n", __FUNCTION__);
		goto Error;
	}

    vgSetfv(VG_CLEAR_COLOR, 4, color);

	*hgui = gui_ctx;

	return 0;

Error:
	*hgui = NULL;
	gui_uninit(gui_ctx);

	return -1;
}

void window_uninit(win_h hwin)
{
	DISPMANX_UPDATE_HANDLE_T dispman_update;
	omx_player_gui_ctx_t *gui_ctx = (omx_player_gui_ctx_t *)gui_win_get_ui_handler(hwin);
	player_win_ctx_t *win_ctx = (player_win_ctx_t *)hwin;

	if (!gui_ctx || !win_ctx)
		return;

	gui_lock();
	if (win_ctx->surface != EGL_NO_SURFACE)
	{
   		eglDestroySurface(gui_ctx->display, win_ctx->surface);
		eglWaitClient();
	}
	dispman_update = vc_dispmanx_update_start(0);
	if (dispman_update)
	{
		vc_dispmanx_element_remove(dispman_update, win_ctx->dispman_element);
		vc_dispmanx_update_submit_sync(dispman_update);
	}
	if (win_ctx->dispman_display)
		vc_dispmanx_display_close(win_ctx->dispman_display);
	gui_unlock();

	free(win_ctx);
}

int window_init(gui_h hgui, win_h *hwin,
	int x, int y, int w, int h)
{
	DISPMANX_UPDATE_HANDLE_T dispman_update;
	VC_RECT_T src_rect;
	omx_player_gui_ctx_t *gui_ctx = (omx_player_gui_ctx_t *)hgui;
	player_win_ctx_t *win_ctx;

	win_ctx = (player_win_ctx_t *)malloc(sizeof(player_win_ctx_t));
	if (!win_ctx)
	{
		DBG_E("%s: Memory allocation failed\n", __FUNCTION__);
		goto Error;
	}
	memset(win_ctx, 0, sizeof(player_win_ctx_t));

	gui_lock();
	win_ctx->hgui = hgui;
	win_ctx->dispman_display = vc_dispmanx_display_open(0 /* LCD */);

	win_ctx->win_rect.x = x;
	win_ctx->win_rect.y = y;
	win_ctx->win_rect.width = w;
	win_ctx->win_rect.height = h;

	src_rect.x = 0;
	src_rect.y = 0;
	src_rect.width = win_ctx->win_rect.width << 16;
	src_rect.height = win_ctx->win_rect.height << 16;        

	dispman_update = vc_dispmanx_update_start(0);

	win_ctx->dispman_element = vc_dispmanx_element_add(dispman_update, win_ctx->dispman_display, 1/*layer*/,
        &win_ctx->win_rect, 0/*src*/, &src_rect, DISPMANX_PROTECTION_NONE, 0/*alpha*/, 0/*clamp*/,
		(DISPMANX_TRANSFORM_T) 0/*transform*/);

	win_ctx->nativewindow.element = win_ctx->dispman_element;
	win_ctx->nativewindow.width = w;
	win_ctx->nativewindow.height = h;

	vc_dispmanx_update_submit_sync(dispman_update);

	win_ctx->surface = eglCreateWindowSurface(gui_ctx->display, gui_ctx->config, &win_ctx->nativewindow, NULL);
	if (win_ctx->surface == EGL_NO_SURFACE)
	{
		DBG_E("%s: Can not create surface\n", __FUNCTION__);
		goto Error;
	}
	gui_unlock();

	*hwin = win_ctx;

	return 0;

Error:
	gui_unlock();
	window_uninit(win_ctx);
	*hwin = NULL;
	return -1;
}

FT_Library gui_get_ft_library(gui_h hgui)
{
	omx_player_gui_ctx_t *gui_ctx = (omx_player_gui_ctx_t *)hgui;

	return gui_ctx->ft_library;
}

int gui_get_window_rect(win_h hwin, VC_RECT_T *rect)
{
	player_win_ctx_t *win_ctx = (player_win_ctx_t *)hwin;

	if (!hwin || !rect)
		return -1;

	memcpy(rect, &win_ctx->win_rect, sizeof(VC_RECT_T));

	return 0;
}

gui_h gui_win_get_ui_handler(win_h hwin)
{
	player_win_ctx_t *win_ctx = (player_win_ctx_t *)hwin;

	return win_ctx->hgui;
}

void gui_win_clear_window(win_h hwin)
{
	VC_RECT_T win_rect;

	gui_get_window_rect(hwin, &win_rect);

	gui_activate_window(hwin);
	vgClear(0, 0, win_rect.width, win_rect.height);	
	gui_release_window(hwin);
}

void gui_window_flush(win_h hwin)
{
	player_win_ctx_t *win_ctx = (player_win_ctx_t *)hwin;
	omx_player_gui_ctx_t *gui_ctx = (omx_player_gui_ctx_t*)
		gui_win_get_ui_handler(hwin);

	gui_activate_window(hwin);
	eglSwapBuffers(gui_ctx->display, win_ctx->surface);
	gui_release_window(hwin);
}

