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

#include "wrapper/wrapper_sw_winsys.h"

extern struct stw_winsys stw_winsys;


BOOL WINAPI MesaScreenCreate(HDC hdc, struct pipe_screen **pScreen)
{
	struct pipe_screen *screen = stw_winsys.create_screen(hdc);
	if(screen)
	{
		*pScreen = screen;
		return TRUE;
	}
	
	return FALSE;
}

BOOL WINAPI MesaPresent(struct pipe_screen *screen, struct pipe_context *ctx, struct pipe_resource *res,
	HDC dc, const RECT *pSrcRest, const RECT *pDstRect)
{
	stw_winsys.present(screen, ctx, res, dc);
	
	return TRUE;
}
