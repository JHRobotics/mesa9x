#include <windows.h>

#define WGL_WGLEXT_PROTOTYPES

#include <GL/gl.h>
#include <GL/wglext.h>

#include "util/u_debug.h"
#include "stw_winsys.h"
#include "stw_device.h"
#include "gdi/gdi_sw_winsys.h"
#include "pipe/p_screen.h"
#include "pipe/p_context.h"

#include "stw_context.h"

#ifdef GALLIUM_SOFTPIPE
#include "softpipe/sp_texture.h"
#include "softpipe/sp_screen.h"
#include "softpipe/sp_public.h"
#endif

#ifdef WIN9X
#include "vramcpy.h"
#endif

#include "wrapper/wrapper_sw_winsys.h"

extern struct stw_winsys stw_winsys;

EXTERN_C struct stw_device * WINAPI MesaGetSTW();
struct stw_device * WINAPI MesaGetSTW(HDC hdc)
{
	if(!stw_dev->screen)
	{
		DrvSetPixelFormat(hdc, 1);
  }
  
	return stw_dev;
}

EXTERN_C BOOL WINAPI MesaNineWinsys(HDC hdc, struct sw_winsys **pWinsys, struct pipe_screen **pScreen);
BOOL WINAPI MesaNineWinsys(HDC hdc, struct sw_winsys **pWinsys, struct pipe_screen **pScreen)
{
	struct pipe_screen *screen = stw_winsys.create_screen(hdc);
	struct sw_winsys *ws = NULL;
	
	if(screen)
	{
		//ws = wrapper_sw_winsys_wrap_pipe_screen(screen);
		
		*pWinsys = ws;
		*pScreen = screen;
		
		return TRUE;
	}
	
	return FALSE;
}

EXTERN_C struct stw_context * WINAPI MesaNineContext(HDC hdc, struct pipe_screen *screen);
struct stw_context * WINAPI MesaNineContext(HDC hdc, struct pipe_screen *screen)
{
	if(!stw_dev)
		return NULL;
	
  stw_dev->fscreen->screen = screen;
  stw_dev->screen = screen;
  
  stw_dev->max_2d_length = screen->get_param(screen, PIPE_CAP_MAX_TEXTURE_2D_SIZE);
  
	const struct stw_pixelformat_info *pfi = stw_pixelformat_get_info_from_hdc(hdc);
#if 0
	if (!pfi)
	{
		PIXELFORMATDESCRIPTOR pfd =
		{
			sizeof(PIXELFORMATDESCRIPTOR),
			1,
			PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER,    //Flags
			PFD_TYPE_RGBA,        // The kind of framebuffer. RGBA or palette.
			32,                   // Colordepth of the framebuffer.
			0, 0, 0, 0, 0, 0,
			0,
			0,
			0,
			0, 0, 0, 0,
			24,                   // Number of bits for the depthbuffer
			8,                    // Number of bits for the stencilbuffer
			0,                    // Number of Aux buffers in the framebuffer.
			PFD_MAIN_PLANE,
			0,
			0, 0, 0
		};
		
		int formatId = stw_pixelformat_choose(hdc, &pfd);
		pfi = stw_pixelformat_get_info(formatId);
		if(!pfi)
		{
			printf("pfi NULL");
			return NULL;
		}
	}
#endif
	
	printf("pfi: %p\n", pfi);

	return stw_create_context_attribs(hdc, 0, NULL, stw_dev->fscreen, 1, 0, 0, WGL_CONTEXT_COMPATIBILITY_PROFILE_BIT_ARB, pfi, WGL_NO_RESET_NOTIFICATION_ARB);
}
