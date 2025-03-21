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


#include "vmw_screen.h"
#include "vmw_fence.h"
#include "vmw_context.h"

#include "util/u_memory.h"
#if MESA_MAJOR >= 24
#include "util/compiler.h"
#else
#include "pipe/p_compiler.h"
#endif

#include "../wddm_screen.h"

DEBUG_GET_ONCE_BOOL_OPTION(cache_maps_disabled, "SVGA_FORCE_KERNEL_UNMAPS", TRUE)

/* Called from vmw_drm_create_screen(), creates and initializes the
 * vmw_winsys_screen structure, which is the main entity in this
 * module.
 * First, check whether a vmw_winsys_screen object already exists for
 * this device, and in that case return that one, making sure that we
 * have our own file descriptor open to DRM.
 */

struct vmw_winsys_screen_wddm *
vmw_winsys_create_wddm(const WDDMGalliumDriverEnv *pEnv)
{
	struct vmw_winsys_screen_wddm *vws_wddm;
	struct vmw_winsys_screen *vws;

	if (pEnv->pHWInfo == NULL || pEnv->pHWInfo->u32HwType != VBOX_GA_HW_TYPE_VMSVGA)
		return NULL;

	vws_wddm = CALLOC_STRUCT(vmw_winsys_screen_wddm);
	vws = &vws_wddm->base;
	if (!vws)
		goto out_no_vws;

	vws_wddm->pEnv = pEnv;
	vws_wddm->HwInfo = pEnv->pHWInfo->u.svga;

	vws->device = 0; /* not used */
	vws->open_count = 1;
	vws->ioctl.drm_fd = -1; /* not used */
	vws->force_coherent = FALSE;
	
	if (!vmw_ioctl_init(vws))
	{
		goto out_no_ioctl;
	}
	
   vws->base.have_gb_dma = !vws->force_coherent;
   vws->base.need_to_rebind_resources = FALSE;
   vws->base.have_transfer_from_buffer_cmd = vws->base.have_vgpu10;
   vws->base.have_constant_buffer_offset_cmd = FALSE;
   vws->cache_maps = vws->base.have_vgpu10 && !debug_get_option_cache_maps_disabled();
#if MESA_MAJOR >= 23
   vws->base.have_constant_buffer_offset_cmd =
      vws->ioctl.have_drm_2_20 && vws->base.have_sm5;
   vws->base.have_index_vertex_buffer_offset_cmd = FALSE;
   vws->base.have_rasterizer_state_v2_cmd =
      vws->ioctl.have_drm_2_20 && vws->base.have_sm5;
#endif
   vws->fence_ops = vmw_fence_ops_create(vws);
   
	if (!vws->fence_ops){
		goto out_no_fence_ops;
	}

	if(!vmw_pools_init(vws))
	{
		goto out_no_pools;
	}

	if (!vmw_winsys_screen_init_svga(vws))
	{
		goto out_no_svga;
	}

	cnd_init(&vws->cs_cond);
	mtx_init(&vws->cs_mutex, mtx_plain);

	return vws_wddm;

out_no_svga:
	vmw_pools_cleanup(vws);
out_no_pools:
	vws->fence_ops->destroy(vws->fence_ops);
out_no_fence_ops:
	vmw_ioctl_cleanup(vws);
out_no_ioctl:
	FREE(vws);
out_no_vws:
	return NULL;
}

void
vmw_winsys_destroy(struct vmw_winsys_screen *vws)
{
	if (--vws->open_count == 0) {
#if MESA_MAJOR >= 25
      if (vws->swc)
         vmw_swc_unref(vws->swc);
#endif
		vmw_pools_cleanup(vws);
		vws->fence_ops->destroy(vws->fence_ops);
		vmw_ioctl_cleanup(vws);
		mtx_destroy(&vws->cs_mutex);
		cnd_destroy(&vws->cs_cond);
		FREE(vws);
	}
}

void vmw_svga_winsys_host_log(struct svga_winsys_screen *sws, const char *log)
{
	
}

