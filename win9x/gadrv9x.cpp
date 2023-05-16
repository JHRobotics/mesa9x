/** @file
 * VirtualBox Windows Guest Mesa3D - Gallium driver interface to the 9x miniport driver.
 */

#include <windows.h>

#include <stdio.h>

#include "util/u_debug.h"
#include "stw_winsys.h"
#include "stw_device.h"
#include "stw_tls.h"
#include "gdi/gdi_sw_winsys.h"

#include "softpipe/sp_texture.h"
#include "softpipe/sp_screen.h"
#include "softpipe/sp_public.h"

extern "C" {
#include "svga/svga_public.h"
#include "svga/svga_winsys.h"
}
#include "svga/svga_screen.h"


#ifdef MESA_NEW
#include "frontend/drm_driver.h"
#else
#include "state_tracker/drm_driver.h"
#endif
#include "pipe/p_context.h"

#define uint8  uint8_t
#define uint16 uint16_t
#define uint32 uint32_t
#define uint64 uint64_t

#include "wddm_screen.h"
#include "svgadrv.h"

extern "C" {
struct svga_winsys_screen *
svga_wddm_winsys_screen_create(const WDDMGalliumDriverEnv *pEnv);
}

struct pipe_screen * WINAPI
GaDrvScreenCreate(const WDDMGalliumDriverEnv *pEnv)
{
    struct svga_winsys_screen *sws = svga_wddm_winsys_screen_create(pEnv);
    
    if (sws)
        return svga_screen_create(sws);
    return NULL;
}

void WINAPI
GaDrvScreenDestroy(struct pipe_screen *s)
{
    if (s)
        s->destroy(s);
}

uint32_t WINAPI
GaDrvGetSurfaceId(struct pipe_screen *pScreen, struct pipe_resource *pResource)
{
    uint32_t u32Sid = 0;

    if (   pScreen
        && pResource)
    {
        /* Get the sid (surface id). */
        struct winsys_handle whandle;
        memset(&whandle, 0, sizeof(whandle));
#ifndef MESA_NEW
        whandle.type = DRM_API_HANDLE_TYPE_SHARED;
#else
        whandle.type = WINSYS_HANDLE_TYPE_SHARED;
#endif

        if (pScreen->resource_get_handle(pScreen, NULL, pResource, &whandle, 
#ifndef MESA_NEW
        	PIPE_HANDLE_USAGE_READ
#else
          0
#endif
        ))
        {
            u32Sid = (uint32_t)whandle.handle;
        }

    }

    return u32Sid;
}

const WDDMGalliumDriverEnv *WINAPI
GaDrvGetWDDMEnv(struct pipe_screen *pScreen)
{
    WDDMGalliumDriverEnv *hAdapter = NULL;

    if (pScreen)
    {
        struct svga_screen *pSvgaScreen = svga_screen(pScreen);
        struct vmw_winsys_screen_wddm *vws_wddm = (struct vmw_winsys_screen_wddm *)pSvgaScreen->sws;

        return vws_wddm->pEnv;
    }

    return hAdapter;
}

uint32_t WINAPI
GaDrvGetContextId(struct pipe_context *pPipeContext)
{
    uint32 u32Cid = ~0;

    if (pPipeContext)
    {
        struct svga_winsys_context *pSWC = svga_winsys_context(pPipeContext);
        u32Cid = pSWC->cid;
    }

    return u32Cid;
}

void WINAPI
GaDrvContextFlush(struct pipe_context *pPipeContext)
{
    if (pPipeContext)
        pPipeContext->flush(pPipeContext, NULL, PIPE_FLUSH_END_OF_FRAME);
}

#ifdef DEBUG
struct svga_screen *
svga_screen(struct pipe_screen *screen)
{
   assert(screen);
   return (struct svga_screen *)screen;
}
#endif
