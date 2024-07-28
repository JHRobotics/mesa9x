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
	
	if(SVGASurfaceCreate(svga, pCreateParms, paSizes, cSizes, pu32Sid))
	{
		return S_OK;
	}
	
	return E_FAIL;
}

static void vboxVxdSurfaceDestroy(void *pvEnv, uint32_t u32Sid)
{
	SVGA_ENV;
	
	uint32_t s = 0;
	
  SVGASurfaceDestroy(svga, u32Sid, &s);
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

typedef struct _cmd_define_item_id
{
	SVGA3dCmdHeader header;
	uint32_t itemId;
} cmd_define_item_id_t;

static int vboxVxdRender(void *pvEnv, uint32_t u32Cid, void *pvCommands, uint32_t cbCommands, GAFENCEQUERY *pFenceQuery)
{
	SVGA_ENV;
	HRESULT hr = S_OK;
	const uint8_t *next = (const uint8_t *)pvCommands;
	const uint8_t *last = next + cbCommands;
  uint32_t cid_dx = 0;
  DWORD flags = SVGA_CB_RENDER;
  
  if(cbCommands == 0 && pFenceQuery == NULL)
  	return S_OK;
  
  if(svga->dx)
  {
  	flags |= SVGA_CB_DX_FLAG_DX_CONTEXT;
  	cid_dx = u32Cid;
  }
    
  uint32_t cnt_cmds = 0;
  uint32_t cnt_cb   = 0;
  
  //SVGAWaitAll(svga);
  
  SVGAStart(svga);
  
  while(next < last)
 	{
		const uint32_t cmd_id = *(const uint32_t *)next;

 		if(cmd_id >= SVGA_3D_CMD_BASE && cmd_id < SVGA_3D_CMD_MAX)
 		{
			const SVGA3dCmdHeader *header = (const SVGA3dCmdHeader *)next;
			const size_t cmd_bytes = header->size + sizeof(SVGA3dCmdHeader);
			//debug_printf("%s: %d, %d\n", __FUNCTION__, cmd_id, real_size);
			const cmd_define_item_id_t *id_test = (cmd_define_item_id_t*)header;

			//svga_dump_command(cmd_id, body, header->size);
			
			switch(cmd_id)
			{
				case SVGA_3D_CMD_DX_DEFINE_RENDERTARGET_VIEW:
					SVGAContextCotableUpdate(svga, cid_dx, SVGA_COTABLE_RTVIEW, id_test->itemId);
					break;
				case SVGA_3D_CMD_DX_DEFINE_SHADERRESOURCE_VIEW:
					SVGAContextCotableUpdate(svga, cid_dx, SVGA_COTABLE_SRVIEW, id_test->itemId);
					break;
				case SVGA_3D_CMD_DX_DEFINE_DEPTHSTENCIL_VIEW:
					SVGAContextCotableUpdate(svga, cid_dx, SVGA_COTABLE_DSVIEW, id_test->itemId);
					break;
				case SVGA_3D_CMD_DX_DEFINE_ELEMENTLAYOUT:
					SVGAContextCotableUpdate(svga, cid_dx, SVGA_COTABLE_ELEMENTLAYOUT, id_test->itemId);
					break;
				case SVGA_3D_CMD_DX_DEFINE_BLEND_STATE:
					SVGAContextCotableUpdate(svga, cid_dx, SVGA_COTABLE_BLENDSTATE, id_test->itemId);
					break;
				case SVGA_3D_CMD_DX_DEFINE_DEPTHSTENCIL_STATE:
					SVGAContextCotableUpdate(svga, cid_dx, SVGA_COTABLE_DEPTHSTENCIL, id_test->itemId);
					break;
				case SVGA_3D_CMD_DX_DEFINE_RASTERIZER_STATE:
					SVGAContextCotableUpdate(svga, cid_dx, SVGA_COTABLE_RASTERIZERSTATE, id_test->itemId);
					break;
				case SVGA_3D_CMD_DX_DEFINE_SAMPLER_STATE:
					SVGAContextCotableUpdate(svga, cid_dx, SVGA_COTABLE_SAMPLER, id_test->itemId);
					break;
				case SVGA_3D_CMD_DX_DEFINE_STREAMOUTPUT:
					SVGAContextCotableUpdate(svga, cid_dx, SVGA_COTABLE_STREAMOUTPUT, id_test->itemId);
					break;
				case SVGA_3D_CMD_DX_DEFINE_QUERY:
					SVGAContextCotableUpdate(svga, cid_dx, SVGA_COTABLE_DXQUERY, id_test->itemId);
					break;
				case SVGA_3D_CMD_DX_DEFINE_SHADER:
					SVGAContextCotableUpdate(svga, cid_dx, SVGA_COTABLE_DXSHADER, id_test->itemId);
					break;
				case SVGA_3D_CMD_DX_DEFINE_UA_VIEW:
					SVGAContextCotableUpdate(svga, cid_dx, SVGA_COTABLE_UAVIEW, id_test->itemId);
					break;
			}
			
			if(cnt_cmds >= SVGA_CB_MAX_QUEUED_PER_CONTEXT-3 ||
				(cnt_cb + cmd_bytes) >= SVGA_CB_MAX_SIZE - (sizeof(DWORD)*2 + sizeof(SVGACBHeader))
				)
			{
				SVGAFinish(svga, flags, cid_dx);
				SVGAStart(svga);
				
				cnt_cmds = 0;
				cnt_cb   = cmd_bytes;
			}
			
			SVGAPush(svga, header, cmd_bytes);
			
			cnt_cmds++;
			cnt_cb += cmd_bytes;
			
			next = ((uint8_t*)header) + cmd_bytes;
			if(next > last)
			{
				break;
			}
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
	
	if(pFenceQuery)
	{
		flags |= SVGA_CB_FORCE_FENCE;
	}
	
	SVGAFinish(svga, flags, cid_dx);
	
  if(pFenceQuery)
  {
  	vboxVxdFenceQuery(pvEnv, svga->cmd_fence, pFenceQuery);
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

 	if(!SVGACanAllocate(svga, u32RegionSize, SVGA_ALLOC_CREATE_REGION))
 	{
 		return E_FAIL;
 	}
 	
 	if(u32RegionSize == 0)
 	{
 		return E_FAIL;
 	}
 	
	region = SVGARegionCreate(svga, u32RegionSize, &user_address);

	if(region > 0)
	{
		void *ptr = (void*)user_address;
		if(ptr != NULL)
		{
			//memset(ptr, 0, u32RegionSize);
			
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
