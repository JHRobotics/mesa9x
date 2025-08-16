#if MESA_MAJOR >= 23
# include "pipe/p_defines.h"
# include "state_tracker/st_format.h"
# include "state_tracker/st_context.h"
#endif

#include <windows.h>

#include "util/u_debug.h"
#include "stw_winsys.h"
#include "stw_device.h"
#include "stw_tls.h"
#include "stw_context.h"
#include "gdi/gdi_sw_winsys.h"

BOOL WINAPI MesaDimensions(struct pipe_screen *screen, struct pipe_context *ctx, struct pipe_resource *res, 
	int *pWidth, int *pHeight, int *pBpp, int *pPitch)
{
	return FALSE;
}

struct pipe_screen *virgl_screen_create_9x(HDC hDC);

//static void virgl_present(struct pipe_screen *screen, struct pipe_context *pipe, struct pipe_resource *res, HDC hDC);
//static boolean virgl_get_adapter_luid(struct pipe_screen *screen, LUID *pAdapterLuid);

static const char *virgl_get_name(void)
{
   return "VirGL";
}

#if MESA_MAJOR >= 21 && MESA_MAJOR < 24
static unsigned
virgl_get_pfd_flags(struct pipe_screen *screen)
{
   return stw_pfd_gdi_support;
}
#endif

struct stw_winsys stw_winsys = {
   &virgl_screen_create_9x,
   NULL, //&virgl_present,
#if WINVER >= 0xA00
   NULL, //&wddm_get_adapter_luid,
#else
   NULL, /* get_adapter_luid */
#endif
   NULL,
   NULL,
   NULL,
#if MESA_MAJOR < 24
   NULL, //&virgl_get_pfd_flags,
#endif
   NULL,  //&wddm_create_framebuffer,
   &virgl_get_name,
};
