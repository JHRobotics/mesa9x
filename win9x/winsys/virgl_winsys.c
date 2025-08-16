/*
 * Copyright 2014, 2015 Red Hat.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * on the rights to use, copy, modify, merge, publish, distribute, sub
 * license, and/or sell copies of the Software, and to permit persons to whom
 * the Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHOR(S) AND/OR THEIR SUPPLIERS BE LIABLE FOR ANY CLAIM,
 * DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
 * OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
 * USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>

//#include "util/os_mman.h"
#include "util/os_file.h"
#include "util/os_time.h"
#include "util/simple_mtx.h"
#include "util/u_memory.h"
#include "util/format/u_format.h"
#include "util/u_hash_table.h"
#include "util/u_inlines.h"
#include "util/u_pointer.h"
#include "frontend/drm_driver.h"
#include "virgl/virgl_screen.h"
#include "virgl/virgl_public.h"
#include "virtio-gpu/virgl_protocol.h"

#include <windows.h>

struct virgl_drm_winsys
{
   struct virgl_winsys base;
   HDC dc;
//   struct virgl_resource_cache cache;
   mtx_t mutex;

   int32_t blob_id;
   struct hash_table *bo_handles;
   struct hash_table *bo_names;
   mtx_t bo_handles_mutex;
};

static simple_mtx_t virgl_screen_mutex = SIMPLE_MTX_INITIALIZER;

static struct virgl_winsys *
virgl_winsys_create_9x(HDC hDC)
{
   static const unsigned CACHE_TIMEOUT_USEC = 1000000;
   struct virgl_drm_winsys *qdws;
   int drm_version;
   int ret;

#if 0
   for (uint32_t i = 0; i < ARRAY_SIZE(params); i++) {
      struct drm_virtgpu_getparam getparam = { 0 };
      uint64_t value = 0;
      getparam.param = params[i].param;
      getparam.value = (uint64_t)(uintptr_t)&value;
      ret = drmIoctl(drmFD, DRM_IOCTL_VIRTGPU_GETPARAM, &getparam);
      params[i].value = (ret == 0) ? value : 0;
   }

   if (!params[param_3d_features].value)
      return NULL;

   drm_version = virgl_drm_get_version(drmFD);
   if (drm_version < 0)
      return NULL;

   if (params[param_context_init].value) {
      ret = virgl_init_context(drmFD);
      if (ret)
         return NULL;
   }
#endif

   qdws = CALLOC_STRUCT(virgl_drm_winsys);
   if (!qdws)
      return NULL;

#if 0
   qdws->fd = drmFD;
   virgl_resource_cache_init(&qdws->cache, CACHE_TIMEOUT_USEC,
                             virgl_drm_resource_cache_entry_is_busy,
                             virgl_drm_resource_cache_entry_release,
                             qdws);
   (void) mtx_init(&qdws->mutex, mtx_plain);
   (void) mtx_init(&qdws->bo_handles_mutex, mtx_plain);
   p_atomic_set(&qdws->blob_id, 0);

   qdws->bo_handles = util_hash_table_create_ptr_keys();
   qdws->bo_names = util_hash_table_create_ptr_keys();
   qdws->base.destroy = virgl_drm_winsys_destroy;

   qdws->base.transfer_put = virgl_bo_transfer_put;
   qdws->base.transfer_get = virgl_bo_transfer_get;
   qdws->base.resource_create = virgl_drm_winsys_resource_cache_create;
   qdws->base.resource_reference = virgl_drm_resource_reference;
   qdws->base.resource_create_from_handle = virgl_drm_winsys_resource_create_handle;
   qdws->base.resource_set_type = virgl_drm_winsys_resource_set_type;
   qdws->base.resource_get_handle = virgl_drm_winsys_resource_get_handle;
   qdws->base.resource_get_storage_size = virgl_drm_winsys_resource_get_storage_size;
   qdws->base.resource_map = virgl_drm_resource_map;
   qdws->base.resource_wait = virgl_drm_resource_wait;
   qdws->base.resource_is_busy = virgl_drm_resource_is_busy;
   qdws->base.cmd_buf_create = virgl_drm_cmd_buf_create;
   qdws->base.cmd_buf_destroy = virgl_drm_cmd_buf_destroy;
   qdws->base.submit_cmd = virgl_drm_winsys_submit_cmd;
   qdws->base.emit_res = virgl_drm_emit_res;
   qdws->base.res_is_referenced = virgl_drm_res_is_ref;

   qdws->base.cs_create_fence = virgl_cs_create_fence;
   qdws->base.fence_wait = virgl_fence_wait;
   qdws->base.fence_reference = virgl_fence_reference;
   qdws->base.fence_server_sync = virgl_fence_server_sync;
   qdws->base.fence_get_fd = virgl_fence_get_fd;
   qdws->base.get_caps = virgl_drm_get_caps;
   qdws->base.get_fd = virgl_drm_winsys_get_fd;
   qdws->base.supports_fences =  drm_version >= VIRGL_DRM_VERSION_FENCE_FD;
   qdws->base.supports_encoded_transfers = 1;

   qdws->base.supports_coherent = params[param_resource_blob].value &&
                                  params[param_host_visible].value;
#endif
   return &qdws->base;
}

struct pipe_screen *
virgl_screen_create_9x(HDC hDC)
{
	struct virgl_winsys *vws;
	/* int fd, const struct pipe_screen_config *config */
	struct pipe_screen *pscreen = NULL;

	simple_mtx_lock(&virgl_screen_mutex);

	vws = virgl_winsys_create_9x(hDC);

	if(vws)
	{
		pscreen = virgl_create_screen(vws, NULL);
		if (pscreen)
		{
			//virgl_screen(pscreen)->winsys_priv = pscreen->destroy;
			//pscreen->destroy = virgl_screen_destroy_9x;
		}
	}
	
	simple_mtx_unlock(&virgl_screen_mutex);
	return pscreen;
}
