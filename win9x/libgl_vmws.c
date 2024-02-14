/**************************************************************************
 *
 * Copyright 2009-2010 VMware, Inc.
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sub license, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT. IN NO EVENT SHALL
 * THE COPYRIGHT HOLDERS, AUTHORS AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM,
 * DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
 * OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
 * USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 *
 *
 **************************************************************************/

/**
 * @file
 * Softpipe/LLVMpipe support.
 *
 * @author Jose Fonseca <jfonseca@vmware.com>
 */

#ifdef MESA23
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

#include "softpipe/sp_texture.h"
#include "softpipe/sp_screen.h"
#include "softpipe/sp_public.h"

#include "svga/svga_public.h"
#include "svga/svga_winsys.h"
#include "svga/svga_screen.h"

#include "wrapper/wrapper_sw_winsys.h"

#include "wddm_screen.h"

#define SVGA
#include "3d_accel.h"
#include "svgadrv.h"

#include <stdio.h>

void crt_enable_sse2();
int crt_sse2_is_safe();

void crt_locks_init(int count);
void crt_locks_destroy();
#define LOCK_PROGRAM_OPTIMIZE 0
#define CRT_LOCK_CNT 1

typedef struct WDDMGalliumDriverEnv WDDMGalliumDriverEnv;

struct stw_shared_surface
{
    HANDLE hResource;
    HANDLE hSurface;
    uint32_t u32Sid;
};

static svga_inst_t sSvga;

#if MESA_MAJOR < 21
static struct pipe_screen *wddm_screen_create(void)
#else
static struct pipe_screen *wddm_screen_create(HDC hDC)
#endif
{
	struct pipe_screen *screen = NULL;
	if(!SVGACreate(&sSvga))
	{
		fprintf(stderr, "Failed to init hardware! Wrong driver or virtual gpu type?\n");
		return NULL;
	}
	
	const WDDMGalliumDriverEnv *pEnv = GaDrvCreateEnv(&sSvga);
	
	if(pEnv)
	{
		/// @todo pEnv to include destructor callback, to be called from winsys screen destructor?
		screen = GaDrvScreenCreate(pEnv);
	}

	return screen;
}

/* present direct to window or screen if possible */
#if MESA_MAJOR < 21
static void wddm_present(struct pipe_screen *screen, struct pipe_resource *res, HDC hDC)
{
    struct stw_context *ctx = stw_current_context();
    struct pipe_context *pipe = ctx->st->pipe;
#else
static void wddm_present(struct pipe_screen *screen, struct pipe_context *pipe, struct pipe_resource *res, HDC hDC)
{
#endif
    const WDDMGalliumDriverEnv *pEnv = GaDrvGetWDDMEnv(screen);

    if (pEnv)
    {
    	  svga_inst_t *svga = (svga_inst_t *)(pEnv->pvEnv);
    	  assert(svga);
    
        uint32_t cid = GaDrvGetContextId(pipe);
        uint32_t sid = GaDrvGetSurfaceId(screen, res);
        
        SVGAPresent(svga, hDC, cid, sid);
		}
}

/* present to window */
#if MESA_MAJOR < 21
static void wddm_present_window(struct pipe_screen *screen, struct pipe_resource *res, HDC hDC)
{
	  struct stw_context *ctx = stw_current_context();
    struct pipe_context *pipe = ctx->st->pipe;
#else
static void wddm_present_window(struct pipe_screen *screen, struct pipe_context *pipe, struct pipe_resource *res, HDC hDC)
{
#endif
    const WDDMGalliumDriverEnv *pEnv = GaDrvGetWDDMEnv(screen);

    if (pEnv)
    {
    	  svga_inst_t *svga = (svga_inst_t *)(pEnv->pvEnv);
    	  assert(svga);
    
        uint32_t cid = GaDrvGetContextId(pipe);
        uint32_t sid = GaDrvGetSurfaceId(screen, res);
        
        debug_printf("wddm_present_window (%d x %d)\n", res->width0, res->height0);
        
        SVGAPresentWinBlt(svga, hDC, cid, sid);
		}

}

static boolean
wddm_get_adapter_luid(struct pipe_screen *screen, LUID *pAdapterLuid)
{
    const WDDMGalliumDriverEnv *pEnv = GaDrvGetWDDMEnv(screen);
    if (pEnv)
    {
        //GaDrvEnvKmtAdapterLUID(pEnv, pAdapterLuid);
        return true;
    }

    return false;
}


static struct stw_shared_surface *
wddm_shared_surface_open(struct pipe_screen *screen, HANDLE hSharedSurface)
{
    struct stw_shared_surface *surface = NULL;

    const WDDMGalliumDriverEnv *pEnv = GaDrvGetWDDMEnv(screen);
    if (pEnv)
    {
    		svga_inst_t *svga = (svga_inst_t *)(pEnv->pvEnv);
        surface = (struct stw_shared_surface *)malloc(sizeof(struct stw_shared_surface));
        if (surface)
        {
        	surface->hResource = NULL;
        	surface->hSurface = hSharedSurface;
        	surface->u32Sid = (uint32)hSharedSurface;
        	svga_printf(svga, "Shared surface open");
        	/*
            D3DKMT_HANDLE hDevice = GaDrvEnvKmtDeviceHandle(pEnv);
            NTSTATUS Status = vboxKmtOpenSharedSurface(hDevice, (D3DKMT_HANDLE)hSharedSurface, surface);
            if (Status != STATUS_SUCCESS)
            {
                free(surface);
                surface = NULL;
            }*/
        }
    }
    return surface;
}

static void
wddm_shared_surface_close(struct pipe_screen *screen,
                         struct stw_shared_surface *surface)
{
    const WDDMGalliumDriverEnv *pEnv = GaDrvGetWDDMEnv(screen);
    if (pEnv)
    {
    	svga_inst_t *svga = (svga_inst_t *)(pEnv->pvEnv);
    	svga_printf(svga, "Shared surface close");
        //D3DKMT_HANDLE hDevice = GaDrvEnvKmtDeviceHandle(pEnv);
        //vboxKmtCloseSharedSurface(hDevice, surface);
    }
    free(surface);
}

static void
wddm_compose(struct pipe_screen *screen,
             struct pipe_resource *res,
             struct stw_shared_surface *dest,
             LPCRECT pRect,
             ULONGLONG PresentHistoryToken)
{
    struct stw_context *ctx = stw_current_context();
    struct pipe_context *pipe = ctx->st->pipe;
    /* The ICD asked to present something, make sure that any outstanding commends are submitted. */
    GaDrvContextFlush(pipe);

    uint32_t u32SourceSid = GaDrvGetSurfaceId(screen, res);
    const WDDMGalliumDriverEnv *pEnv = GaDrvGetWDDMEnv(screen);
    uint32_t cid = GaDrvGetContextId(pipe);

    if (pEnv)
    {
        svga_inst_t *svga = (svga_inst_t *)(pEnv->pvEnv);
        assert(svga);
        svga_printf(svga, "wddm_compose\n");
        SVGACompose(svga, cid, u32SourceSid, dest->u32Sid, pRect);
    }
    
    
}

#if MESA_MAJOR >= 21
static unsigned
wddm_get_pfd_flags(struct pipe_screen *screen)
{
   return stw_pfd_gdi_support;
}

#if 0
static struct stw_winsys_framebuffer *
wddm_create_framebuffer(struct pipe_screen *screen, HWND hWnd, int iPixelFormat)
{
	debug_printf("wddm_create_framebuffer\n");
  return NULL;
}
#endif

static const char *
wddm_get_name(void)
{
   return "SVGA3D";
}
#endif

#if MESA_MAJOR >= 21
struct stw_winsys stw_winsys = {
   &wddm_screen_create,
   //&wddm_present_window,
   &wddm_present,
#if WINVER >= 0xA00
   &wddm_get_adapter_luid,
#else
   NULL, /* get_adapter_luid */
#endif
   &wddm_shared_surface_open,
   &wddm_shared_surface_close,
   &wddm_compose,
   &wddm_get_pfd_flags,
   NULL,  //&wddm_create_framebuffer,
   &wddm_get_name,
};
#else
struct stw_winsys stw_winsys = {
   wddm_screen_create,
   //wddm_present_window,
   wddm_present,
   wddm_get_adapter_luid,
   wddm_shared_surface_open,
   wddm_shared_surface_close,
   wddm_compose
};
#endif

EXTERN_C BOOL WINAPI
DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved);


BOOL WINAPI
DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved)
{
   switch (fdwReason)
   {
   case DLL_PROCESS_ATTACH:
#ifdef HAVE_CRTEX
   	  crt_locks_init(CRT_LOCK_CNT);
#endif

   	  {
   	  	/* load self again to protect from unload */
	      char sz[MAX_PATH];
				if(GetModuleFileName(hinstDLL, sz, MAX_PATH))
				{
					LoadLibrary(sz);
				}
   	  }
   	  
   	  /* Init HDA driver */
   	  FBHDA_load();
   	  
   	  /* 
   	   * Because this driver run in userspace as application and if crash, 
   	   * on next run, or another proceess run, we'll need to clean all
   	   * the remain mess
   	   */
   	  SVGAZombieKiller();
   	  
   	  /* DIRECT_VRAM ... enabled now */
   	  if(!debug_get_bool_option("DIRECT_VRAM", TRUE))
   	  {
   	  	stw_winsys.present = wddm_present_window;
   	  }
 
      stw_init(&stw_winsys, hinstDLL);
      stw_init_thread();
      break;

   case DLL_THREAD_ATTACH:
      stw_init_thread();
      break;

   case DLL_THREAD_DETACH:
      stw_cleanup_thread();
      break;

   case DLL_PROCESS_DETACH:
      if (lpvReserved == NULL) {
         // We're being unloaded from the process.
         stw_cleanup_thread();
         stw_cleanup();
#ifdef HAVE_CRTEX
         crt_locks_destroy();
#endif
      } else {
         // Process itself is terminating, and all threads and modules are
         // being detached.
         //
         // The order threads (including llvmpipe rasterizer threads) are
         // destroyed can not be relied up, so it's not safe to cleanup.
         //
         // However global destructors (e.g., LLVM's) will still be called, and
         // if Microsoft OPENGL32.DLL's DllMain is called after us, it will
         // still try to invoke DrvDeleteContext to destroys all outstanding,
         // so set stw_dev to NULL to return immediately if that happens.
         stw_dev = NULL;
         // clean hook in every case
         #if MESA_MAJOR < 21
         stw_tls_clenup_hook();
         #endif
      }
      // JH: clear the garbage
      SVGACleanup(NULL, 0);
      FBHDA_free();
      
      break;
   default:
     break;
   }
   return TRUE;
}

HRESULT WINAPI DllCanUnloadNow()
{
	return S_FALSE;
}

static struct pipe_screen *last_screen = NULL;
static BOOL last_screen_isSVGA3D = FALSE;

BOOL WINAPI MesaDimensions(struct pipe_screen *screen, struct pipe_context *ctx, struct pipe_resource *res, 
	int *pWidth, int *pHeight, int *pBpp, int *pPitch)
{
	BOOL is_svga = FALSE;
	
	if(last_screen != screen)
	{
		const char *type_name = screen->get_name(screen);
		last_screen_isSVGA3D = FALSE;
		
		if(strncmp(type_name, "SVGA3D", sizeof("SVGA3D")-1) == 0)
		{
			last_screen_isSVGA3D = TRUE;
		}
		last_screen = screen;
	}
	
	is_svga = last_screen_isSVGA3D;
	
	if(is_svga)
	{
		const WDDMGalliumDriverEnv *pEnv = GaDrvGetWDDMEnv(screen);
		
		if(pEnv)
		{
			svga_inst_t *svga = (svga_inst_t *)(pEnv->pvEnv);
			assert(svga);
    	  
			uint32_t sid = GaDrvGetSurfaceId(screen, res);
			return SVGASurfaceInfo(svga, sid, pWidth, pHeight, pBpp, pPitch);
		}
	}
	
	return FALSE;
}
