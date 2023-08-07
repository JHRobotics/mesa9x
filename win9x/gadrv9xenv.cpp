/** @file
 * VirtualBox Windows Guest Mesa3D - Gallium driver interface to the 9x miniport driver.
 */

#include <windows.h>
#include <stdint.h>

#include <cstdio>

#define uint8  uint8_t
#define uint16 uint16_t
#define uint32 uint32_t
#define uint64 uint64_t

#include "wddm_screen.h"
#include "svgadrv.h"

#define SVGA_ENV svga_inst_t *svga = (svga_inst_t *)pvEnv;assert(svga)

static int vboxVxdSurfaceDefine(void *pvEnv, GASURFCREATE *pCreateParms, GASURFSIZE *paSizes, uint32_t cSizes, uint32_t *pu32Sid)
{
	SVGA_ENV;
	const uint32_t cbSize = sizeof(SVGA3dCmdHeader)
                        + sizeof(SVGA3dCmdDefineSurface_v2)
                        + cSizes * sizeof(SVGA3dSize);
	
	uint8_t *cmd = (uint8_t *)alloca(cbSize);
	
	memset(cmd, 0, cbSize);
	
	((uint32_t*)cmd)[0] = SVGA_3D_CMD_SURFACE_DEFINE_V2;
	((uint32_t*)cmd)[1] = cbSize - sizeof(SVGA3dCmdHeader);
	
	//debug_printf("Define surface\n");
	
	SVGA3dCmdDefineSurface_v2 *surface = (SVGA3dCmdDefineSurface_v2*)(cmd+sizeof(SVGA3dCmdHeader));
	uint32_t sid = SVGASurfaceIDNext(svga);
	
	//printf("creating surface: %d %d\n", sid, pCreateParms->format);

	if(sid)
	{
		surface->sid = sid;
		surface->surfaceFlags = pCreateParms->flags;
	  surface->format = (SVGA3dSurfaceFormat)pCreateParms->format;
	  
	  for(int i = 0; i < SVGA3D_MAX_SURFACE_FACES; i++)
	  {
	  	surface->face[i].numMipLevels = pCreateParms->mip_levels[i];
	  }
	  
	  surface->multisampleCount = 0;
	  surface->autogenFilter    = SVGA3D_TEX_FILTER_NONE;
	  
	  SVGA3dSize *siz = (SVGA3dSize*)(cmd+sizeof(SVGA3dCmdHeader)+sizeof(SVGA3dCmdDefineSurface_v2));
	  
	  for(int i = 0; i < cSizes; i++)
	  {
	    siz->width = paSizes->cWidth;
	    siz->height = paSizes->cHeight;
	    siz->depth = paSizes->cDepth;
	  	
	  	if(i == 0) /* save only face 0 */
	  	{
	  		svga->surfinfo[sid].format = surface->format;
	  		svga->surfinfo[sid].size.width = siz->width;
	  		svga->surfinfo[sid].size.height = siz->height;
	  		svga->surfinfo[sid].size.depth = siz->depth;
	  		svga->surfinfo[sid].gmrId = 0;
	  	}
	  	
	  	paSizes++;
	  	siz++;
	  }
	  
	  if(svga->have_cb_context)
	  {
	  	cb_state_t cbs;
			cb_lock(svga, &cbs);
			cb_push(&cbs, cmd, cbSize);
			cb_submit(svga, &cbs, SVGA3D_INVALID_ID, SVGA_CB_CONTEXT_DEFAULT);
			
			cb_sync(svga);
			
		  *pu32Sid = sid;
		  return 0;
	  }
	  else
	  {
		  if(SVGAFifoWrite(svga, cmd, cbSize))
		  {
		  	*pu32Sid = sid;
		  	return 0;
		  }
		}
	}
	
	return 1;
}

static void vboxVxdSurfaceDestroy(void *pvEnv, uint32_t u32Sid)
{
	SVGA_ENV;
  SVGASurfaceDestroy(svga, u32Sid);
}

static int vboxVxdFenceQuery(void *pvEnv, uint32_t u32FenceHandle, GAFENCEQUERY *pFenceQuery)
{
	SVGA_ENV;
	HRESULT hr = E_FAIL;
	uint32_t fenceStatus;
	uint32_t lastPassed;
	uint32_t lastFence;
	
	if(SVGAFenceQuery(svga, u32FenceHandle, &fenceStatus, &lastPassed, &lastFence))
	{
    pFenceQuery->u32FenceHandle    = u32FenceHandle;
    pFenceQuery->u32SubmittedSeqNo = lastFence;
    pFenceQuery->u32ProcessedSeqNo = lastPassed;
    
    if(u32FenceHandle == 0)
    {
    	pFenceQuery->u32FenceStatus = GA_FENCE_STATUS_NULL;
    }
    else if(u32FenceHandle > lastFence)
    {
    	/*
    	 * JH: We'll do complete sync on fence counter overrun.
    	 *     So at this point we're sure, that all fences before
    	 *     overrun are already passed
    	 */
    	pFenceQuery->u32FenceStatus = GA_FENCE_STATUS_SUBMITTED;
    }
    else if(fenceStatus != 0)
    {
    	pFenceQuery->u32FenceStatus = GA_FENCE_STATUS_SIGNALED;
    }
    else
    {
    	pFenceQuery->u32FenceStatus = GA_FENCE_STATUS_SUBMITTED;
    }
    
    hr = S_OK;
	}
	
  return hr;
}


static int vboxVxdFenceWait(void *pvEnv, uint32_t u32FenceHandle, uint32_t u32TimeoutUS)
{
	SVGA_ENV;
	SVGAFenceSync(svga, u32FenceHandle); // TODO: don't wait here forever
	
  return S_OK;
}


static void vboxVxdFenceUnref(void *pvEnv, uint32_t u32FenceHandle)
{
  // nothing to do
}

static int vboxVxdRender(void *pvEnv, uint32_t u32Cid, void *pvCommands, uint32_t cbCommands, GAFENCEQUERY *pFenceQuery)
{
	SVGA_ENV;
	HRESULT hr = S_OK;
	const uint8_t *next = (const uint8_t *)pvCommands;
	const uint8_t *last = next + cbCommands;
  cb_state_t cbs;
  uint32_t cid_dx = SVGA3D_INVALID_ID;
  
  if(cbCommands == 0 && pFenceQuery == NULL)
  	return S_OK;
  
  if(svga->dx)
  {
  	cid_dx = u32Cid;
  }
  
	if(svga->have_cb_context)
	{
  	cb_lock(svga, &cbs);
  }
  
  while(next < last)
 	{
		const uint32_t cmd_id = *(const uint32_t *)next;

 		if(cmd_id >= SVGA_3D_CMD_BASE && cmd_id < SVGA_3D_CMD_MAX)
 		{
			const SVGA3dCmdHeader *header = (const SVGA3dCmdHeader *)next;
			const size_t cmd_bytes = header->size + sizeof(SVGA3dCmdHeader);
			//debug_printf("%s: %d, %d\n", __FUNCTION__, cmd_id, real_size);

			//svga_dump_command(cmd_id, body, header->size);
			
			if(svga->have_cb_context)
			{
				/* check buffer limits and send on full */
				if(cb_full(&cbs, cmd_bytes))
				{
					cb_submit(svga, &cbs, cid_dx, SVGA_CB_CONTEXT_DEFAULT);
					cb_lock(svga, &cbs);
				}
				
				cb_push(&cbs, (void*)header, cmd_bytes);
			}
			else
			{
				if(!SVGAFifoWrite(svga, (void*)header, cmd_bytes))
				{
					hr = E_FAIL;
					break;
				}
			}
			
			next = ((uint8_t*)header) + cmd_bytes;
			if(next > last)
				break;
		}
		else if(cmd_id == SVGA_CMD_FENCE)
		{
			/* fence ignored */
			debug_printf("fence ignored\n");
		  next += 2*sizeof(uint32_t);
		}
		else
		{
			/* some 2d command without size (or some garbage), not continue */
			debug_printf("Render garbage\n");
			hr = E_FAIL;
			break;
		}
	}
	
  uint32_t fence = 0;  
  if(pFenceQuery)
  {
  	if(svga->have_cb_context)
  	{
  		fence = SVGAFenceInsertCB(svga);
  		uint32_t fence_cmd[2] = {SVGA_CMD_FENCE, fence};
  		
			if(cb_full(&cbs, sizeof(fence_cmd)))
			{
				cb_submit(svga, &cbs, cid_dx, SVGA_CB_CONTEXT_DEFAULT);
				cb_lock(svga, &cbs);
			}
				
			cb_push(&cbs, &fence_cmd, sizeof(fence_cmd));
  	}
  	else
  	{
  		fence = SVGAFenceInsert(svga);
  	}
  }
  
  if(svga->have_cb_context)
  {
  	cb_submit(svga, &cbs, cid_dx, SVGA_CB_CONTEXT_DEFAULT);
  }
  
  if(fence)
  {
  	vboxVxdFenceQuery(pvEnv, fence, pFenceQuery);
  }
  
  return hr;
}

static void vboxVxdContextDestroy(void *pvEnv, uint32_t u32Cid)
{
	SVGA_ENV;
	
	if(svga->dx)
	{
		SVGAContextCotableDestroy(svga, u32Cid);
	}
	
	SVGAContextDestroy(svga, u32Cid);
}

static uint32_t vboxVxdContextCreate(void *pvEnv, boolean extended, boolean vgpu10)
{
	SVGA_ENV;

	uint32_t cid = SVGAContextCreate(svga);
	
	if(svga->dx)
	{
		SVGAContextCotableCreate(svga, cid);
	}
	
	return cid;
}

static int vboxVxdRegionCreate(void *pvEnv, uint32_t u32RegionSize, uint32_t *pu32GmrId, void **ppvMap)
{
	SVGA_ENV;
  uint32_t region;
  uint32_t user_address;
  HRESULT hr = E_FAIL;
  
	region = SVGARegionCreate(svga, u32RegionSize, &user_address);
	
	if(region > 0)
	{
		void *ptr = (void*)user_address;
		if(ptr != NULL)
		{
			memset(ptr, 0, u32RegionSize);
			
			if(ppvMap)
			{
				*ppvMap = (void*)ptr;
			}
			
			*pu32GmrId = region;
			hr = S_OK;
		}
	}
	
	//printf("%s: %d\n", __FUNCTION__, hr);

	return hr;
}

static void vboxVxdRegionDestroy(void *pvEnv, uint32_t u32GmrId, void *pvMap)
{
	SVGA_ENV;
	if(u32GmrId > 0)
	{
		SVGARegionDestroy(svga, u32GmrId);
	}
}


static int vboxVxdGBSurfaceDefine(void *pvEnv, SVGAGBSURFCREATE *pCreateParms)
{
	SVGA_ENV;
	if(SVGASurfaceGBCreate(svga, pCreateParms))
	{
		return S_OK;
	}
	
	return E_FAIL;
}

static WDDMGalliumDriverEnv mEnv = {};
//static VBOXGAHWINFO sHWInfo;

const WDDMGalliumDriverEnv * WINAPI GaDrvCreateEnv(svga_inst_t *svga)
{
  if (mEnv.cb == 0)
  {
      mEnv.cb                = sizeof(WDDMGalliumDriverEnv);
      mEnv.pvEnv             = svga;
      mEnv.pfnContextCreate  = vboxVxdContextCreate;
      mEnv.pfnContextDestroy = vboxVxdContextDestroy;
      mEnv.pfnSurfaceDefine  = vboxVxdSurfaceDefine;
      mEnv.pfnSurfaceDestroy = vboxVxdSurfaceDestroy;
      mEnv.pfnRender         = vboxVxdRender;
      mEnv.pfnFenceUnref     = vboxVxdFenceUnref;
      mEnv.pfnFenceQuery     = vboxVxdFenceQuery;
      mEnv.pfnFenceWait      = vboxVxdFenceWait;
      mEnv.pfnRegionCreate   = vboxVxdRegionCreate;
      mEnv.pfnRegionDestroy  = vboxVxdRegionDestroy;
      //mEnv.pHWInfo           = &mHWInfo;
      /* VGPU10 */
      mEnv.pfnGBSurfaceDefine  = vboxVxdGBSurfaceDefine;
      
      //memset(&sHWInfo, 0, sizeof(sHWInfo));
      //mEnv.pHWInfo = &sHWInfo;
      mEnv.pHWInfo = new VBOXGAHWINFO;
      
      SVGAReadHwInfo(svga, mEnv.pHWInfo);
      mEnv.pHWInfo->u.svga.svga = svga;
      
    }

    return &mEnv;
}
