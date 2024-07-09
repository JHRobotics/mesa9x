/*
 * Copyright (C) 2016-2018 Oracle Corporation
 *
 * This file is part of VirtualBox Open Source Edition (OSE), as
 * available from http://www.virtualbox.org. This file is free software;
 * you can redistribute it and/or modify it under the terms of the GNU
 * General Public License (GPL) as published by the Free Software
 * Foundation, in version 2 as it comes in the "COPYING" file of the
 * VirtualBox OSE distribution. VirtualBox OSE is distributed in the
 * hope that it will be useful, but WITHOUT ANY WARRANTY of any kind.
 */

/**********************************************************
 * Copyright 2009-2015 VMware, Inc.  All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation
 * files (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use, copy,
 * modify, merge, publish, distribute, sublicense, and/or sell copies
 * of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 **********************************************************/

#include "svga_cmd.h"
#include "util/u_memory.h"
#include "util/u_math.h"
#include "svgadump/svga_dump.h"
#if MESA_MAJOR >= 21
#include "frontend/drm_driver.h"
#else
#include "state_tracker/drm_driver.h"
#endif
#include "vmw_screen.h"
#include "vmw_context.h"
#include "vmw_fence.h"
#include "vmwgfx_drm.h"
#include "svga3d_caps.h"
#include "svga3d_reg.h"
#include "svga3d_surfacedefs.h"

#include "../wddm_screen.h"
#include "../svgadrv.h"

//#include <iprt/asm.h>

#if MESA_MAJOR >= 24
#define boolean bool
#define FALSE false
#define TRUE true
#endif

#define VMW_MAX_DEFAULT_TEXTURE_SIZE   (128 * 1024 * 1024)
#define VMW_FENCE_TIMEOUT_SECONDS 60

struct vmw_region
{
   uint32_t handle;
   uint64_t map_handle;
   void *data;
   uint32_t map_count;
   uint32_t size;
   struct vmw_winsys_screen_wddm *vws_wddm;
};

uint32_t
vmw_region_size(struct vmw_region *region)
{
   return region->size;
}

uint32
vmw_ioctl_context_create(struct vmw_winsys_screen *vws)
{
   struct vmw_winsys_screen_wddm *vws_wddm = (struct vmw_winsys_screen_wddm *)vws;
   return vws_wddm->pEnv->pfnContextCreate(vws_wddm->pEnv->pvEnv, false, false);
}

uint32
vmw_ioctl_extended_context_create(struct vmw_winsys_screen *vws,
                                  boolean vgpu10)
{
   struct vmw_winsys_screen_wddm *vws_wddm = (struct vmw_winsys_screen_wddm *)vws;
   return vws_wddm->pEnv->pfnContextCreate(vws_wddm->pEnv->pvEnv, true, vgpu10);
}

void
vmw_ioctl_context_destroy(struct vmw_winsys_screen *vws, uint32 cid)
{
    struct vmw_winsys_screen_wddm *vws_wddm = (struct vmw_winsys_screen_wddm *)vws;
    vws_wddm->pEnv->pfnContextDestroy(vws_wddm->pEnv->pvEnv, cid);
    return;
}

/*
 * Allocates a device unique surface id, and queues a create surface command
 * for the host. Does not wait for host completion. The surface ID can be
 * used directly in the command stream and shows up as the same surface
 * ID on the host.
 */
uint32
vmw_ioctl_surface_create(struct vmw_winsys_screen *vws,
                         SVGA3dSurface1Flags flags,
                         SVGA3dSurfaceFormat format,
                         unsigned usage,
                         SVGA3dSize size,
                         uint32_t numFaces, uint32_t numMipLevels,
                         unsigned sampleCount)
{
    struct vmw_winsys_screen_wddm *vws_wddm = (struct vmw_winsys_screen_wddm *)vws;

    GASURFCREATE createParms;
    GASURFSIZE sizes[DRM_VMW_MAX_SURFACE_FACES*
                     DRM_VMW_MAX_MIP_LEVELS];
    GASURFSIZE *cur_size;
    uint32_t iFace;
    uint32_t iMipLevel;
    uint32_t u32Sid;
    int ret;

    //RT_NOREF(sampleCount);

    memset(&createParms, 0, sizeof(createParms));
    createParms.flags = (uint32_t) flags;
    createParms.format = (uint32_t) format;
    createParms.usage = (uint32_t) usage;

    if (numFaces * numMipLevels >= GA_MAX_SURFACE_FACES*GA_MAX_MIP_LEVELS) {
        return (uint32_t)-1;
    }
    cur_size = sizes;
    for (iFace = 0; iFace < numFaces; ++iFace) {
       SVGA3dSize mipSize = size;

       createParms.mip_levels[iFace] = numMipLevels;
       for (iMipLevel = 0; iMipLevel < numMipLevels; ++iMipLevel) {
          cur_size->cWidth = mipSize.width;
          cur_size->cHeight = mipSize.height;
          cur_size->cDepth = mipSize.depth;
          cur_size->u32Reserved = 0;
          mipSize.width = MAX2(mipSize.width >> 1, 1);
          mipSize.height = MAX2(mipSize.height >> 1, 1);
          mipSize.depth = MAX2(mipSize.depth >> 1, 1);
          cur_size++;
       }
    }
    for (iFace = numFaces; iFace < SVGA3D_MAX_SURFACE_FACES; ++iFace) {
       createParms.mip_levels[iFace] = 0;
    }
    
    createParms.size = svga3dsurface_get_serialized_size(format, size, numMipLevels, numFaces);
    
    ret = vws_wddm->pEnv->pfnSurfaceDefine(vws_wddm->pEnv->pvEnv, &createParms, &sizes[0], numFaces * numMipLevels, &u32Sid);
    if (ret) {
        return SVGA3D_INVALID_ID;
    }

    return u32Sid;
}

#if MESA_MAJOR < 21
uint32
vmw_ioctl_gb_surface_create(struct vmw_winsys_screen *vws,
			    SVGA3dSurfaceFlags flags,
			    SVGA3dSurfaceFormat format,
                            unsigned usage,
			    SVGA3dSize size,
			    uint32_t numFaces,
			    uint32_t numMipLevels,
                            unsigned sampleCount,
                            uint32_t buffer_handle,
			    struct vmw_region **p_region)
#else
uint32
vmw_ioctl_gb_surface_create(struct vmw_winsys_screen *vws,
                            SVGA3dSurfaceAllFlags flags,
                            SVGA3dSurfaceFormat format,
                            unsigned usage,
                            SVGA3dSize size,
                            uint32_t numFaces,
                            uint32_t numMipLevels,
                            unsigned sampleCount,
                            uint32_t buffer_handle,
                            SVGA3dMSPattern multisamplePattern,
                            SVGA3dMSQualityLevel qualityLevel,
                            struct vmw_region **p_region)
#endif
{
    struct vmw_winsys_screen_wddm *vws_wddm = (struct vmw_winsys_screen_wddm *)vws;

    struct vmw_region *region = NULL;
    if (p_region)
    {
       region = calloc(1, sizeof(struct vmw_region));
       if (!region)
          return SVGA3D_INVALID_ID;
    }

    SVGAGBSURFCREATE createParms;
    createParms.s.flags = flags;
    createParms.s.format = format;
    createParms.s.usage = usage;
    createParms.s.size = size;
    createParms.s.numFaces = numFaces;
    createParms.s.numMipLevels = numMipLevels;
    createParms.s.sampleCount = sampleCount;
    createParms.s.multisamplePattern = multisamplePattern;
    createParms.s.qualityLevel = qualityLevel;
    if (buffer_handle)
        createParms.gmrid = buffer_handle;
    else
        createParms.gmrid = SVGA3D_INVALID_ID;
    createParms.userAddress = 0; /* out */
    createParms.u32Sid = 0; /* out */
    createParms.GMRreturn = FALSE;
    if(p_region || createParms.gmrid != SVGA3D_INVALID_ID)
    {
    	createParms.GMRreturn = TRUE;
    }

    createParms.cbGB = svga3dsurface_get_serialized_size_extended(format, size, numMipLevels, numFaces, sampleCount);

    int ret = vws_wddm->pEnv->pfnGBSurfaceDefine(vws_wddm->pEnv->pvEnv, &createParms);
    if (ret)
    {
        free(region);
        return SVGA3D_INVALID_ID;
    }

    if (p_region)
    {
        region->handle      = createParms.gmrid;
        region->map_handle  = 0;
        region->data        = (void *)createParms.userAddress;
        region->map_count   = 0;
        region->size        = createParms.cbGB;
        region->vws_wddm    = vws_wddm;
        *p_region = region;
    }
    return createParms.u32Sid;
}

/**
 * vmw_ioctl_surface_req - Fill in a struct surface_req
 *
 * @vws: Winsys screen
 * @whandle: Surface handle
 * @req: The struct surface req to fill in
 * @needs_unref: This call takes a kernel surface reference that needs to
 * be unreferenced.
 *
 * Returns 0 on success, negative error type otherwise.
 * Fills in the surface_req structure according to handle type and kernel
 * capabilities.
 */
static int
vmw_ioctl_surface_req(const struct vmw_winsys_screen *vws,
                      const struct winsys_handle *whandle,
                      struct drm_vmw_surface_arg *req,
                      boolean *needs_unref)
{
	switch(whandle->type)
	{
		case WINSYS_HANDLE_TYPE_SHARED:
		case WINSYS_HANDLE_TYPE_KMS:
		case WINSYS_HANDLE_TYPE_FD:
			*needs_unref = FALSE;
			req->handle_type = DRM_VMW_HANDLE_LEGACY;
			req->sid = (uint32_t)whandle->handle;
			break;
		default:
			return -EINVAL;
	}

	return 0;
}

/**
 * vmw_ioctl_gb_surface_ref - Put a reference on a guest-backed surface and
 * get surface information
 *
 * @vws: Screen to register the reference on
 * @handle: Kernel handle of the guest-backed surface
 * @flags: flags used when the surface was created
 * @format: Format used when the surface was created
 * @numMipLevels: Number of mipmap levels of the surface
 * @p_region: On successful return points to a newly allocated
 * struct vmw_region holding a reference to the surface backup buffer.
 *
 * Returns 0 on success, a system error on failure.
 */
#if MESA_MAJOR < 21
int
vmw_ioctl_gb_surface_ref(struct vmw_winsys_screen *vws,
                         const struct winsys_handle *whandle,
                         SVGA3dSurfaceFlags *flags,
                         SVGA3dSurfaceFormat *format,
                         uint32_t *numMipLevels,
                         uint32_t *handle,
                         struct vmw_region **p_region)
#else
int
vmw_ioctl_gb_surface_ref(struct vmw_winsys_screen *vws,
                         const struct winsys_handle *whandle,
                         SVGA3dSurfaceAllFlags *flags,
                         SVGA3dSurfaceFormat *format,
                         uint32_t *numMipLevels,
                         uint32_t *handle,
                         struct vmw_region **p_region)
#endif
{
    //RT_NOREF7(vws, whandle, flags, format, numMipLevels, handle, p_region);
    // ??? DeviceCallbacks.pfnLockCb(pDevice->hDevice, );
    return -1;
}

void
vmw_ioctl_surface_destroy(struct vmw_winsys_screen *vws, uint32 sid)
{
    struct vmw_winsys_screen_wddm *vws_wddm = (struct vmw_winsys_screen_wddm *)vws;
    /// @todo ? take into account surface references, referencing should be probably implemented here in user mode.
    vws_wddm->pEnv->pfnSurfaceDestroy(vws_wddm->pEnv->pvEnv, sid);
}

void
vmw_ioctl_command(struct vmw_winsys_screen *vws, int32_t cid,
                  uint32_t throttle_us, void *commands, uint32_t size,
                  struct pipe_fence_handle **pfence, int32_t imported_fence_fd,
                  uint32_t flags)
{
    struct vmw_winsys_screen_wddm *vws_wddm = (struct vmw_winsys_screen_wddm *)vws;
    GAFENCEQUERY FenceQuery;
    //RT_NOREF3(throttle_us, imported_fence_fd, flags);
#ifdef DEBUG
   // svga_dump_commands(commands, size);
#endif
    memset(&FenceQuery, 0, sizeof(FenceQuery));
    FenceQuery.u32FenceStatus = GA_FENCE_STATUS_NULL;

    vws_wddm->pEnv->pfnRender(vws_wddm->pEnv->pvEnv, cid, commands, size, pfence? &FenceQuery: NULL);
    if (FenceQuery.u32FenceStatus == GA_FENCE_STATUS_NULL)
    {
       /*
        * Kernel has already synced, or caller requested no fence.
        */
       if (pfence)
          *pfence = NULL;
    }
    else
    {
       if (pfence)
       {
          vmw_fences_signal(vws->fence_ops, FenceQuery.u32ProcessedSeqNo, FenceQuery.u32SubmittedSeqNo, TRUE);

          *pfence = vmw_fence_create(vws->fence_ops, FenceQuery.u32FenceHandle,
                                     FenceQuery.u32SubmittedSeqNo, /* mask */ 0, -1);
          if (*pfence == NULL)
          {
             /*
              * Fence creation failed. Need to sync.
              */
             (void) vmw_ioctl_fence_finish(vws, FenceQuery.u32FenceHandle, /* mask */ 0);
             vmw_ioctl_fence_unref(vws, FenceQuery.u32FenceHandle);
          }
       }
    }
}

struct vmw_region *
vmw_ioctl_region_create(struct vmw_winsys_screen *vws, uint32_t size)
{
    /* 'region' is a buffer visible both for host and guest */
    struct vmw_region *region;
    struct vmw_winsys_screen_wddm *vws_wddm = (struct vmw_winsys_screen_wddm *)vws;
    uint32_t u32GmrId = 0;
    void *pvMap = NULL;
    int ret;

    region = calloc(1, sizeof(struct vmw_region));
    if (!region)
       goto out_err1;

    ret = vws_wddm->pEnv->pfnRegionCreate(vws_wddm->pEnv->pvEnv, size, &u32GmrId, &pvMap);

    if (ret) {
       vmw_error("IOCTL failed %d: %s\n", ret, strerror(-ret));
       goto out_err1;
    }

    region->handle      = u32GmrId;
    region->map_handle  = 0;
    region->data        = pvMap;
    region->map_count   = 0;
    region->size        = size;
    region->vws_wddm    = vws_wddm;

    return region;

out_err1:
    free(region);
    return NULL;
}

void
vmw_ioctl_region_destroy(struct vmw_region *region)
{
    struct vmw_winsys_screen_wddm *vws_wddm = region->vws_wddm;

    vws_wddm->pEnv->pfnRegionDestroy(vws_wddm->pEnv->pvEnv, region->handle, region->data);

    free(region);
}

SVGAGuestPtr
vmw_ioctl_region_ptr(struct vmw_region *region)
{
   SVGAGuestPtr ptr;
   ptr.gmrId = region->handle;
   ptr.offset = 0;
   return ptr;
}

void *
vmw_ioctl_region_map(struct vmw_region *region)
{
    debug_printf("%s: gmrId = %u\n", __FUNCTION__, region->handle);

    if (region->data == NULL)
    {
       /* Should not get here. */
       return NULL;
    }

    ++region->map_count;

    return region->data;
}

void
vmw_ioctl_region_unmap(struct vmw_region *region)
{
   /// @todo
   --region->map_count;
}

/**
 * vmw_ioctl_syncforcpu - Synchronize a buffer object for CPU usage
 *
 * @region: Pointer to a struct vmw_region representing the buffer object.
 * @dont_block: Dont wait for GPU idle, but rather return -EBUSY if the
 * GPU is busy with the buffer object.
 * @readonly: Hint that the CPU access is read-only.
 * @allow_cs: Allow concurrent command submission while the buffer is
 * synchronized for CPU. If FALSE command submissions referencing the
 * buffer will block until a corresponding call to vmw_ioctl_releasefromcpu.
 *
 * This function idles any GPU activities touching the buffer and blocks
 * command submission of commands referencing the buffer, even from
 * other processes.
 */
int
vmw_ioctl_syncforcpu(struct vmw_region *region,
                     boolean dont_block,
                     boolean readonly,
                     boolean allow_cs)
{
    //RT_NOREF4(region, dont_block, readonly, allow_cs);
    // ???
    return -1;
}

/**
 * vmw_ioctl_releasefromcpu - Undo a previous syncforcpu.
 *
 * @region: Pointer to a struct vmw_region representing the buffer object.
 * @readonly: Should hold the same value as the matching syncforcpu call.
 * @allow_cs: Should hold the same value as the matching syncforcpu call.
 */
void
vmw_ioctl_releasefromcpu(struct vmw_region *region,
                         boolean readonly,
                         boolean allow_cs)
{
    //RT_NOREF3(region, readonly, allow_cs);
    // ???
}

void
vmw_ioctl_fence_unref(struct vmw_winsys_screen *vws,
		      uint32_t handle)
{
    struct vmw_winsys_screen_wddm *vws_wddm = (struct vmw_winsys_screen_wddm *)vws;
    vws_wddm->pEnv->pfnFenceUnref(vws_wddm->pEnv->pvEnv, handle);
}

static inline uint32_t
vmw_drm_fence_flags(uint32_t flags)
{
    uint32_t dflags = 0;

    if (flags & SVGA_FENCE_FLAG_EXEC)
	dflags |= DRM_VMW_FENCE_FLAG_EXEC;
    if (flags & SVGA_FENCE_FLAG_QUERY)
	dflags |= DRM_VMW_FENCE_FLAG_QUERY;

    return dflags;
}


int
vmw_ioctl_fence_signalled(struct vmw_winsys_screen *vws,
			  uint32_t handle,
			  uint32_t flags)
{
    //RT_NOREF(flags);
    struct vmw_winsys_screen_wddm *vws_wddm = (struct vmw_winsys_screen_wddm *)vws;
    GAFENCEQUERY FenceQuery;
    int ret;

    memset(&FenceQuery, 0, sizeof(FenceQuery));
    FenceQuery.u32FenceStatus = GA_FENCE_STATUS_NULL;

    ret = vws_wddm->pEnv->pfnFenceQuery(vws_wddm->pEnv->pvEnv, handle, &FenceQuery);

    if (ret != 0)
        return ret;

    if (FenceQuery.u32FenceStatus == GA_FENCE_STATUS_NULL)
        return 0; /* Treat as signalled. */

    vmw_fences_signal(vws->fence_ops, FenceQuery.u32ProcessedSeqNo, FenceQuery.u32SubmittedSeqNo, TRUE);

    return FenceQuery.u32FenceStatus == GA_FENCE_STATUS_SIGNALED ? 0 : -1;
}


int
vmw_ioctl_fence_finish(struct vmw_winsys_screen *vws,
                       uint32_t handle,
		       uint32_t flags)
{
    //RT_NOREF(flags);
    struct vmw_winsys_screen_wddm *vws_wddm = (struct vmw_winsys_screen_wddm *)vws;

    vws_wddm->pEnv->pfnFenceWait(vws_wddm->pEnv->pvEnv, handle, VMW_FENCE_TIMEOUT_SECONDS*1000000);

    return 0; /* Regardless. */
}

uint32
vmw_ioctl_shader_create(struct vmw_winsys_screen *vws,
			SVGA3dShaderType type,
			uint32 code_len)
{
    //RT_NOREF3(vws, type, code_len);
    // DeviceCallbacks.pfnAllocateCb(pDevice->hDevice, pDdiAllocate);
    return 0;
}

void
vmw_ioctl_shader_destroy(struct vmw_winsys_screen *vws, uint32 shid)
{
    //RT_NOREF2(vws, shid);
    // ??? DeviceCallbacks.pfnDeallocateCb(pDevice->hDevice, pDdiAllocate);
    return;
}

static int
vmw_ioctl_parse_caps(struct vmw_winsys_screen *vws,
		     const uint32_t *cap_buffer)
{
   unsigned i;

   if (vws->base.have_gb_objects) {
      for (i = 0; i < vws->ioctl.num_cap_3d; ++i) {
				vws->ioctl.cap_3d[i].has_cap = TRUE;
				vws->ioctl.cap_3d[i].result.u = cap_buffer[i];
      }
      return 0;
   } else {
	   unsigned caps_max = vws->ioctl.num_cap_3d;
	   if(caps_max > GA_HWINFO_CAPS)
	   {
	      caps_max = GA_HWINFO_CAPS;
	   }
	   
	   for (i = 0; i < caps_max; ++i) {
	   	   if(cap_buffer[i])
	   	   {
	           vws->ioctl.cap_3d[i].has_cap = TRUE;
	           vws->ioctl.cap_3d[i].result.u = cap_buffer[i];
	       }
	   }
   }
   return 0;
}

#define SVGA_CAP2_DX2 0x00000004
#define SVGA_CAP2_DX3 0x00000400

enum SVGASHADERMODEL
{
   SVGA_SM_LEGACY = 0,
   SVGA_SM_4,
   SVGA_SM_4_1,
   SVGA_SM_5,
   SVGA_SM_MAX
};

static enum SVGASHADERMODEL vboxGetShaderModel(struct vmw_winsys_screen_wddm *vws_wddm)
{
   enum SVGASHADERMODEL enmResult = SVGA_SM_LEGACY;

   if (   (vws_wddm->HwInfo.au32Regs[SVGA_REG_CAPABILITIES] & SVGA_CAP_GBOBJECTS)
       && (vws_wddm->HwInfo.au32Regs[SVGA_REG_CAPABILITIES] & SVGA_CAP_CMD_BUFFERS_3 /*=SVGA_CAP_DX*/)
       && vws_wddm->HwInfo.au32Caps[SVGA3D_DEVCAP_DXCONTEXT])
   {
      enmResult = SVGA_SM_4;

      if (vws_wddm->HwInfo.au32Regs[SVGA_REG_CAPABILITIES] & SVGA_CAP_CAP2_REGISTER)
      {
         if (   (vws_wddm->HwInfo.au32Regs[SVGA_REG_CAP2] & SVGA_CAP2_DX2)
             && vws_wddm->HwInfo.au32Caps[SVGA3D_DEVCAP_SM41])
         {
            enmResult = SVGA_SM_4_1;

            if (   (vws_wddm->HwInfo.au32Regs[SVGA_REG_CAP2] & SVGA_CAP2_DX3)
                && vws_wddm->HwInfo.au32Caps[SVGA3D_DEVCAP_SM5])
            {
               enmResult = SVGA_SM_5;
            }
         }
      }
   }

   return enmResult;
}

static int
vboxGetParam(struct vmw_winsys_screen_wddm *vws_wddm, struct drm_vmw_getparam_arg *gp_arg)
{
    /* DRM_VMW_GET_PARAM */
    switch (gp_arg->param)
    {
        case DRM_VMW_PARAM_NUM_STREAMS:
            gp_arg->value = 1; /* const */
            break;
        case DRM_VMW_PARAM_NUM_FREE_STREAMS:
            gp_arg->value = 1; /* const */
            break;
        case DRM_VMW_PARAM_3D:
            gp_arg->value = (vws_wddm->HwInfo.au32Regs[SVGA_REG_CAPABILITIES] & SVGA_CAP_3D) != 0;
            break;
        case DRM_VMW_PARAM_HW_CAPS:
            gp_arg->value = vws_wddm->HwInfo.au32Regs[SVGA_REG_CAPABILITIES];
            break;
        case DRM_VMW_PARAM_FIFO_CAPS:
            gp_arg->value = vws_wddm->HwInfo.au32Fifo[SVGA_FIFO_CAPABILITIES];
            break;
        case DRM_VMW_PARAM_MAX_FB_SIZE:
            gp_arg->value = vws_wddm->HwInfo.au32Regs[SVGA_REG_MAX_PRIMARY_BOUNDING_BOX_MEM];
            break;
        case DRM_VMW_PARAM_FIFO_HW_VERSION:
            if (vws_wddm->HwInfo.au32Fifo[SVGA_FIFO_CAPABILITIES] & SVGA_FIFO_CAP_3D_HWVERSION_REVISED)
            {
                gp_arg->value =
                    vws_wddm->HwInfo.au32Fifo[SVGA_FIFO_3D_HWVERSION_REVISED];
            }
            else
            {
                gp_arg->value =
                    vws_wddm->HwInfo.au32Fifo[SVGA_FIFO_3D_HWVERSION];
            }
            break;
        case DRM_VMW_PARAM_MAX_SURF_MEMORY:
            if (vws_wddm->HwInfo.au32Regs[SVGA_REG_CAPABILITIES] & SVGA_CAP_GBOBJECTS)
                gp_arg->value = vws_wddm->HwInfo.au32Regs[SVGA_REG_SUGGESTED_GBOBJECT_MEM_SIZE_KB] * 1024 / 2;
            else
                gp_arg->value = vws_wddm->HwInfo.au32Regs[SVGA_REG_MEMORY_SIZE];
            break;
        case DRM_VMW_PARAM_3D_CAPS_SIZE:
        		if (vws_wddm->HwInfo.au32Regs[SVGA_REG_CAPABILITIES] & SVGA_CAP_GBOBJECTS)
        			gp_arg->value = SVGA3D_DEVCAP_MAX * sizeof(uint32_t);
        		else
            	gp_arg->value = (SVGA_FIFO_3D_CAPS_LAST - SVGA_FIFO_3D_CAPS + 1) * sizeof(uint32_t);
            break;
        case DRM_VMW_PARAM_MAX_MOB_MEMORY:
            gp_arg->value = vws_wddm->HwInfo.au32Regs[SVGA_REG_SUGGESTED_GBOBJECT_MEM_SIZE_KB] * 1024;
            break;
        case DRM_VMW_PARAM_MAX_MOB_SIZE:
            gp_arg->value = vws_wddm->HwInfo.au32Regs[SVGA_REG_MOB_MAX_SIZE];
            break;
        case DRM_VMW_PARAM_SCREEN_TARGET:
            gp_arg->value = 1;
            break;
#if MESA_MAJOR < 21
        case DRM_VMW_PARAM_VGPU10:
            gp_arg->value = 1;
            break;
#else
        case DRM_VMW_PARAM_DX:
            gp_arg->value = (vboxGetShaderModel(vws_wddm) >= SVGA_SM_4);
            break;
        case DRM_VMW_PARAM_HW_CAPS2:
            if (vws_wddm->HwInfo.au32Regs[SVGA_REG_CAPABILITIES] & SVGA_CAP_CAP2_REGISTER)
                gp_arg->value = vws_wddm->HwInfo.au32Regs[SVGA_REG_CAP2];
            else
                gp_arg->value = 0;
            break;
        case DRM_VMW_PARAM_SM4_1:
            gp_arg->value = (vboxGetShaderModel(vws_wddm) >= SVGA_SM_4_1);
            break;
        case DRM_VMW_PARAM_SM5:
            gp_arg->value = (vboxGetShaderModel(vws_wddm) >= SVGA_SM_5);
            break;
#if MESA_MAJOR >= 24
        case DRM_VMW_PARAM_GL43:
            gp_arg->value = vws_wddm->HwInfo.au32Caps[SVGA3D_DEVCAP_GL43];
            break;
#endif
#endif /* MESA_MAJOR >= 21 */
        default: return -1;
    }
    return 0;
}

static int
vboxGet3DCap(struct vmw_winsys_screen_wddm *vws_wddm, void *pvCap, size_t cbCap)
{
#if 0
    /* DRM_VMW_GET_3D_CAP */
    memcpy(pvCap, &vws_wddm->HwInfo.au32Fifo[SVGA_FIFO_3D_CAPS], cbCap);
#else
    /* use driver-parsed caps */
    memcpy(pvCap, vws_wddm->HwInfo.au32Caps, cbCap);
#endif
    return 0;
}

DEBUG_GET_ONCE_BOOL_OPTION(buffer_coherent, "SVGA_BUFFER_COHERENT", FALSE);
DEBUG_GET_ONCE_BOOL_OPTION(force_coherent, "SVGA_FORCE_COHERENT", FALSE);

boolean
vmw_ioctl_init(struct vmw_winsys_screen *vws)
{
   struct drm_vmw_getparam_arg gp_arg;
   struct drm_vmw_get_3d_cap_arg cap_arg;
   unsigned int size;
   int ret;
   uint32_t *cap_buffer;
   boolean drm_gb_capable;
   boolean have_drm_2_5 = 1; /* unused */

   struct vmw_winsys_screen_wddm *vws_wddm = (struct vmw_winsys_screen_wddm *)vws;

   VMW_FUNC;

   vws->ioctl.have_drm_2_6 = 1; /* unused */
   vws->ioctl.have_drm_2_9 = 1;
#if MESA_MAJOR >= 21
   vws->ioctl.have_drm_2_15 = 1;
   /* PIPE_CAP_BUFFER_MAP_PERSISTENT_COHERENT is broken in VirtualBox (<= 7.0.14) */
   vws->ioctl.have_drm_2_16 = debug_get_option_buffer_coherent() ? 1 : 0;
 
 /*  vws->ioctl.have_drm_2_17 = 1; DRM_VMW_MSG */
   vws->ioctl.have_drm_2_18 = 1; /* DRM_VMW_PARAM_SM5 */
 /*  vws->ioctl.have_drm_2_19 = 1; VMX86_STATS: vmw_svga_winsys_stats_inc */
#endif

#if MESA_MAJOR >= 23
   vws->ioctl.have_drm_2_17 = 1;
   vws->ioctl.have_drm_2_19 = 1;
   vws->ioctl.have_drm_2_20 = 1;
#endif

   vws->ioctl.drm_execbuf_version = vws->ioctl.have_drm_2_9 ? 2 : 1;

#if MESA_MAJOR < 21
   drm_gb_capable = 1;
#else
   drm_gb_capable = have_drm_2_5;
#endif

   memset(&gp_arg, 0, sizeof(gp_arg));
   gp_arg.param = DRM_VMW_PARAM_3D;
   ret = vboxGetParam(vws_wddm, &gp_arg);
   if (ret || gp_arg.value == 0) {
      vmw_error("No 3D enabled (%i, %s).\n", ret, strerror(-ret));
      goto out_no_3d;
   }

   memset(&gp_arg, 0, sizeof(gp_arg));
   gp_arg.param = DRM_VMW_PARAM_FIFO_HW_VERSION;
   ret = vboxGetParam(vws_wddm, &gp_arg);
   if (ret) {
      vmw_error("Failed to get fifo hw version (%i, %s).\n",
                ret, strerror(-ret));
      goto out_no_3d;
   }
   vws->ioctl.hwversion = gp_arg.value;

#if MESA_MAJOR < 21
   /* The driver does not support this feature. */
   vws->base.have_gb_objects = FALSE;
   /* commented code begin */
   memset(&gp_arg, 0, sizeof(gp_arg));
   gp_arg.param = DRM_VMW_PARAM_HW_CAPS;
   ret = vboxGetParam(vws_wddm, &gp_arg);
   if (ret)
      vws->base.have_gb_objects = FALSE;
   else
   	  vws->base.have_gb_objects = !!(gp_arg.value & SVGA_CAP_GBOBJECTS);
   
   debug_printf("vws->base.have_gb_objects: %d (%X)\n", vws->base.have_gb_objects, gp_arg.value);
      //vws->base.have_gb_objects = !!(gp_arg.value & (uint64_t) SVGA_CAP_GBOBJECTS);
   /* commented code end */
   
   if (vws->base.have_gb_objects && !drm_gb_capable)
      goto out_no_3d;

   vws->base.have_vgpu10 = FALSE;
   if (vws->base.have_gb_objects) {
      memset(&gp_arg, 0, sizeof(gp_arg));
      gp_arg.param = DRM_VMW_PARAM_3D_CAPS_SIZE;
      ret = vboxGetParam(vws_wddm, &gp_arg);
      if (ret)
         size = SVGA_FIFO_3D_CAPS_SIZE * sizeof(uint32_t);
      else
         size = gp_arg.value;
   
      if (vws->base.have_gb_objects)
         vws->ioctl.num_cap_3d = size / sizeof(uint32_t);
      else
         vws->ioctl.num_cap_3d = SVGA3D_DEVCAP_MAX;

      memset(&gp_arg, 0, sizeof(gp_arg));
      gp_arg.param = DRM_VMW_PARAM_MAX_MOB_MEMORY;
      ret = vboxGetParam(vws_wddm, &gp_arg);
      if (ret) {
         /* Just guess a large enough value. */
         vws->ioctl.max_mob_memory = 256*1024*1024;
      } else {
         vws->ioctl.max_mob_memory = gp_arg.value;
      }

      memset(&gp_arg, 0, sizeof(gp_arg));
      gp_arg.param = DRM_VMW_PARAM_MAX_MOB_SIZE;
      ret = vboxGetParam(vws_wddm, &gp_arg);

      if (ret || gp_arg.value == 0) {
           vws->ioctl.max_texture_size = VMW_MAX_DEFAULT_TEXTURE_SIZE;
      } else {
           vws->ioctl.max_texture_size = gp_arg.value;
      }

      /* Never early flush surfaces, mobs do accounting. */
      vws->ioctl.max_surface_memory = 128*1024*1024;

      if (vws->ioctl.have_drm_2_9) {

         memset(&gp_arg, 0, sizeof(gp_arg));
         gp_arg.param = DRM_VMW_PARAM_VGPU10;
         ret = vboxGetParam(vws_wddm, &gp_arg);
         if (ret == 0 && gp_arg.value != 0) {
            BOOL vgpu10_val;

            debug_printf("Have VGPU10 interface and hardware.\n");
            vws->base.have_vgpu10 = TRUE;
            vgpu10_val = debug_get_bool_option("SVGA_VGPU10", TRUE);
            if (!vgpu10_val) {
               debug_printf("Disabling VGPU10 interface.\n");
               vws->base.have_vgpu10 = FALSE;
            } else {
               debug_printf("Enabling VGPU10 interface.\n");
               vws_wddm->HwInfo.svga->dx = TRUE;
            }
         }
      }
   } else { /* !gb_objects */
      vws->ioctl.num_cap_3d = SVGA3D_DEVCAP_MAX;

      memset(&gp_arg, 0, sizeof(gp_arg));
      gp_arg.param = DRM_VMW_PARAM_MAX_SURF_MEMORY;
      if (have_drm_2_5)
         ret = vboxGetParam(vws_wddm, &gp_arg);
      if (!have_drm_2_5 || ret) {
         /* Just guess a large enough value, around 800mb. */
         vws->ioctl.max_surface_memory = 0x30000000;
      } else {
         vws->ioctl.max_surface_memory = gp_arg.value;
      }

      vws->ioctl.max_texture_size = VMW_MAX_DEFAULT_TEXTURE_SIZE;

      //size = SVGA_FIFO_3D_CAPS_SIZE * sizeof(uint32_t);
      size = GA_HWINFO_CAPS * sizeof(uint32_t);
   }

   debug_printf("VGPU10 interface is %s.\n",
                vws->base.have_vgpu10 ? "on" : "off");
   
   cap_buffer = calloc(1, size);
   if (!cap_buffer) {
      debug_printf("Failed alloc fifo 3D caps buffer.\n");
      goto out_no_3d;
   }
   
   vws->ioctl.cap_3d = calloc(vws->ioctl.num_cap_3d, 
			      sizeof(*vws->ioctl.cap_3d));
   if (!vws->ioctl.cap_3d) {
      debug_printf("Failed alloc fifo 3D caps buffer.\n");
      goto out_no_caparray;
   }

   ret = vboxGet3DCap(vws_wddm, cap_buffer, size);

   if (ret) {
      debug_printf("Failed to get 3D capabilities"
		   " (%i, %s).\n", ret, strerror(-ret));
      goto out_no_caps;
   }

   ret = vmw_ioctl_parse_caps(vws, cap_buffer);
   if (ret) {
      debug_printf("Failed to parse 3D capabilities"
		   " (%i, %s).\n", ret, strerror(-ret));
      goto out_no_caps;
   }
#else /* mesa21 */
   memset(&gp_arg, 0, sizeof(gp_arg));
   gp_arg.param = DRM_VMW_PARAM_HW_CAPS;
   ret = vboxGetParam(vws_wddm, &gp_arg);
   if (ret)
      vws->base.have_gb_objects = FALSE;
   else
      vws->base.have_gb_objects = !!(gp_arg.value & (uint64_t) SVGA_CAP_GBOBJECTS);

   if (vws->base.have_gb_objects && !drm_gb_capable)
      goto out_no_3d;

   vws->base.have_vgpu10 = FALSE;
   vws->base.have_sm4_1 = FALSE;
   vws->base.have_intra_surface_copy = FALSE;
#if MESA_MAJOR >= 24
   vws->base.device_id = 0x0405; /* assume SVGA II */
#endif

   if (vws->base.have_gb_objects) {
#if 0
      memset(&gp_arg, 0, sizeof(gp_arg));
      gp_arg.param = DRM_VMW_PARAM_MAX_MOB_MEMORY;
      ret = vboxGetParam(vws_wddm, &gp_arg);
      if (ret) {
         /* Just guess a large enough value. */
         vws->ioctl.max_mob_memory = 256*1024*1024;
      } else {
         vws->ioctl.max_mob_memory = gp_arg.value;
      }
#endif
      vws->ioctl.max_mob_memory = 256*1024*1024;

      memset(&gp_arg, 0, sizeof(gp_arg));
      gp_arg.param = DRM_VMW_PARAM_MAX_MOB_SIZE;
      ret = vboxGetParam(vws_wddm, &gp_arg);

      if (ret || gp_arg.value == 0) {
           vws->ioctl.max_texture_size = VMW_MAX_DEFAULT_TEXTURE_SIZE;
      } else {
           vws->ioctl.max_texture_size = gp_arg.value;
      }

#if 0
      /* Never early flush surfaces, mobs do accounting. */
      vws->ioctl.max_surface_memory = -1;
#endif
      vws->ioctl.max_surface_memory = 256*1024*1024;

      if (vws->ioctl.have_drm_2_9) {
         memset(&gp_arg, 0, sizeof(gp_arg));
         gp_arg.param = DRM_VMW_PARAM_DX;
         ret = vboxGetParam(vws_wddm, &gp_arg);
         if (ret == 0 && gp_arg.value != 0) {
            BOOL vgpu10_val;

            debug_printf("Have VGPU10 interface and hardware.\n");
            vws->base.have_vgpu10 = TRUE;
            vgpu10_val = debug_get_bool_option("SVGA_VGPU10", TRUE);
            if (!vgpu10_val) {
               debug_printf("Disabling VGPU10 interface.\n");
               vws->base.have_vgpu10 = FALSE;
            } else {
               debug_printf("Enabling VGPU10 interface.\n");
               vws_wddm->HwInfo.svga->dx = TRUE;
            }
         }
      }

      if (vws->ioctl.have_drm_2_15 && vws->base.have_vgpu10) {
         memset(&gp_arg, 0, sizeof(gp_arg));
         gp_arg.param = DRM_VMW_PARAM_HW_CAPS2;
         ret = vboxGetParam(vws_wddm, &gp_arg);
         if (ret == 0 && gp_arg.value != 0) {
            vws->base.have_intra_surface_copy = TRUE;
         }

         memset(&gp_arg, 0, sizeof(gp_arg));
         gp_arg.param = DRM_VMW_PARAM_SM4_1;
         ret = vboxGetParam(vws_wddm, &gp_arg);
         if (ret == 0 && gp_arg.value != 0) {
            vws->base.have_sm4_1 = TRUE;
         }
      }

      if (vws->ioctl.have_drm_2_18 && vws->base.have_sm4_1) {
         memset(&gp_arg, 0, sizeof(gp_arg));
         gp_arg.param = DRM_VMW_PARAM_SM5;
         ret = vboxGetParam(vws_wddm, &gp_arg);
         if (ret == 0 && gp_arg.value != 0) {
            vws->base.have_sm5 = TRUE;
         }
      }
      
#if MESA_MAJOR >= 24
      if (vws->ioctl.have_drm_2_20 && vws->base.have_sm5) {
         memset(&gp_arg, 0, sizeof(gp_arg));
         gp_arg.param = DRM_VMW_PARAM_GL43;
         ret = vboxGetParam(vws_wddm, &gp_arg);
         if (ret == 0 && gp_arg.value != 0) {
            vws->base.have_gl43 = true;
         }
      }
#endif

      memset(&gp_arg, 0, sizeof(gp_arg));
      gp_arg.param = DRM_VMW_PARAM_3D_CAPS_SIZE;
      ret = vboxGetParam(vws_wddm, &gp_arg);
      if (ret)
         size = SVGA_FIFO_3D_CAPS_SIZE * sizeof(uint32_t);
      else
         size = gp_arg.value;

      if (vws->base.have_gb_objects)
         vws->ioctl.num_cap_3d = size / sizeof(uint32_t);
      else
         vws->ioctl.num_cap_3d = SVGA3D_DEVCAP_MAX;

      if (vws->ioctl.have_drm_2_16) {
         vws->base.have_coherent = TRUE;
         vws->force_coherent = debug_get_option_force_coherent();
      }
   } else {
      vws->ioctl.num_cap_3d = SVGA3D_DEVCAP_MAX;

      memset(&gp_arg, 0, sizeof(gp_arg));
      gp_arg.param = DRM_VMW_PARAM_MAX_SURF_MEMORY;
      if (have_drm_2_5)
         ret = vboxGetParam(vws_wddm, &gp_arg);
      if (!have_drm_2_5 || ret) {
         /* Just guess a large enough value, around 800mb. */
         vws->ioctl.max_surface_memory = 0x30000000;
      } else {
         vws->ioctl.max_surface_memory = gp_arg.value;
      }

      vws->ioctl.max_texture_size = VMW_MAX_DEFAULT_TEXTURE_SIZE;

      //size = SVGA_FIFO_3D_CAPS_SIZE * sizeof(uint32_t);
      size = GA_HWINFO_CAPS * sizeof(uint32_t);
   }

   debug_printf("VGPU10 interface is %s.\n",
                vws->base.have_vgpu10 ? "on" : "off");

   cap_buffer = calloc(1, size);
   if (!cap_buffer) {
      debug_printf("Failed alloc fifo 3D caps buffer.\n");
      goto out_no_3d;
   }

   vws->ioctl.cap_3d = calloc(vws->ioctl.num_cap_3d, 
			      sizeof(*vws->ioctl.cap_3d));
   if (!vws->ioctl.cap_3d) {
      debug_printf("Failed alloc fifo 3D caps buffer.\n");
      goto out_no_caparray;
   }

   /*
    * This call must always be after DRM_VMW_PARAM_MAX_MOB_MEMORY and
    * DRM_VMW_PARAM_SM4_1. This is because, based on these calls, kernel
    * driver sends the supported cap.
    */
   ret = vboxGet3DCap(vws_wddm, cap_buffer, size);

   if (ret) {
      debug_printf("Failed to get 3D capabilities"
		   " (%i, %s).\n", ret, strerror(-ret));
      goto out_no_caps;
   }

   ret = vmw_ioctl_parse_caps(vws, cap_buffer);
   if (ret) {
      debug_printf("Failed to parse 3D capabilities"
		   " (%i, %s).\n", ret, strerror(-ret));
      goto out_no_caps;
   }

   if (vws->ioctl.have_drm_2_15 && vws->base.have_vgpu10) {

     /* support for these commands didn't make it into vmwgfx kernel
      * modules before 2.10.
      */
      vws->base.have_generate_mipmap_cmd = TRUE;
      vws->base.have_set_predication_cmd = TRUE;
   }

   if (vws->ioctl.have_drm_2_15) {
      vws->base.have_fence_fd = TRUE;
   }
#endif

   free(cap_buffer);
   return TRUE;

	out_no_caps:
   	free(vws->ioctl.cap_3d);
  out_no_caparray:
		free(cap_buffer);
	out_no_3d:
		vws->ioctl.num_cap_3d = 0;

 		debug_printf("%s Failed\n", __FUNCTION__);
		return FALSE;
}



void
vmw_ioctl_cleanup(struct vmw_winsys_screen *vws)
{
   VMW_FUNC;
   
   free(vws->ioctl.cap_3d);
}
