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

#ifdef GALLIUM_LLVMPIPE
#include "llvmpipe/lp_texture.h"
#include "llvmpipe/lp_screen.h"
#include "llvmpipe/lp_public.h"
#endif

#include "gdi/gdi_sw_winsys.h"

#ifdef WIN9X
#include "vramcpy.h"
#endif

BOOL WINAPI MesaDimensions(struct pipe_screen *screen, struct pipe_context *ctx, struct pipe_resource *res, 
	int *pWidth, int *pHeight, int *pBpp, int *pPitch)
{
	const char *type_name = screen->get_name(screen);
	struct sw_winsys *winsys = NULL;
	struct gdi_sw_displaytarget *dt = NULL;

#ifdef GALLIUM_LLVMPIPE
	if(strncmp(type_name, "llvmpipe", 8) == 0)
	{
		winsys = llvmpipe_screen(screen)->winsys;
		dt = (struct gdi_sw_displaytarget *)(llvmpipe_resource(res)->dt);
	}
#endif

#ifdef GALLIUM_SOFTPIPE
	if(strcmp(type_name, "softpipe") == 0)
	{
		winsys = softpipe_screen(screen)->winsys;
		dt = (struct gdi_sw_displaytarget *)(softpipe_resource(res)->dt);
	}
#endif
	
	if(dt)
	{
		if(pWidth != NULL)
		{
			*pWidth = dt->width;
		}
		
		if(pHeight != NULL)
		{
			*pHeight = dt->height;
		}
		
		if(pBpp != NULL)
		{
			*pBpp = dt->bmi.bmiHeader.biBitCount;
		}
		
		if(pPitch != NULL)
		{
			*pPitch = dt->stride;
		}
		
		return TRUE;
	}
	
	return FALSE;
}
