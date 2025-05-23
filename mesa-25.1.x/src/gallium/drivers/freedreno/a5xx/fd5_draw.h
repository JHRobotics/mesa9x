/*
 * Copyright © 2016 Rob Clark <robclark@freedesktop.org>
 * SPDX-License-Identifier: MIT
 *
 * Authors:
 *    Rob Clark <robclark@freedesktop.org>
 */

#ifndef FD5_DRAW_H_
#define FD5_DRAW_H_

#include "pipe/p_context.h"

#include "freedreno_draw.h"

#include "fd5_context.h"
#include "fd5_screen.h"

/* some bits in common w/ a4xx: */
#include "a4xx/fd4_draw.h"

void fd5_draw_init(struct pipe_context *pctx);

static inline void
fd5_draw(struct fd_batch *batch, struct fd_ringbuffer *ring,
         enum pc_di_primtype primtype, enum pc_di_vis_cull_mode vismode,
         enum pc_di_src_sel src_sel, uint32_t count, uint32_t instances,
         enum a4xx_index_size idx_type, uint32_t max_indices,
         uint32_t idx_offset, struct pipe_resource *idx_buffer)
{
   /* for debug after a lock up, write a unique counter value
    * to scratch7 for each draw, to make it easier to match up
    * register dumps to cmdstream.  The combination of IB
    * (scratch6) and DRAW is enough to "triangulate" the
    * particular draw that caused lockup.
    */
   emit_marker5(ring, 7);

   OUT_PKT7(ring, CP_DRAW_INDX_OFFSET, idx_buffer ? 7 : 3);
   if (vismode == USE_VISIBILITY) {
      /* leave vis mode blank for now, it will be patched up when
       * we know if we are binning or not
       */
      OUT_RINGP(ring, DRAW4(primtype, src_sel, idx_type, 0),
                &batch->draw_patches);
   } else {
      OUT_RING(ring, DRAW4(primtype, src_sel, idx_type, vismode));
   }
   OUT_RING(ring, instances); /* NumInstances */
   OUT_RING(ring, count);     /* NumIndices */
   if (idx_buffer) {
      OUT_RING(ring, 0x0); /* XXX */
      OUT_RELOC(ring, fd_resource(idx_buffer)->bo, idx_offset, 0, 0);
      OUT_RING(ring, max_indices);
   }

   emit_marker5(ring, 7);

   fd_reset_wfi(batch);
}

static inline void
fd5_draw_emit(struct fd_batch *batch, struct fd_ringbuffer *ring,
              enum pc_di_primtype primtype, enum pc_di_vis_cull_mode vismode,
              const struct pipe_draw_info *info,
              const struct pipe_draw_indirect_info *indirect,
              const struct pipe_draw_start_count_bias *draw, unsigned index_offset)
{
   struct pipe_resource *idx_buffer = NULL;
   enum a4xx_index_size idx_type;
   enum pc_di_src_sel src_sel;
   uint32_t max_indices, idx_offset;

   if (indirect && indirect->buffer) {
      struct fd_resource *ind = fd_resource(indirect->buffer);

      emit_marker5(ring, 7);

      if (info->index_size) {
         struct pipe_resource *idx = info->index.resource;
         max_indices = idx->width0 / info->index_size;

         OUT_PKT7(ring, CP_DRAW_INDX_INDIRECT, 6);
         OUT_RINGP(ring,
                   DRAW4(primtype, DI_SRC_SEL_DMA,
                         fd4_size2indextype(info->index_size), 0),
                   &batch->draw_patches);
         OUT_RELOC(ring, fd_resource(idx)->bo, index_offset, 0, 0);
         OUT_RING(ring, A5XX_CP_DRAW_INDX_INDIRECT_3_MAX_INDICES(max_indices));
         OUT_RELOC(ring, ind->bo, indirect->offset, 0, 0);
      } else {
         OUT_PKT7(ring, CP_DRAW_INDIRECT, 3);
         OUT_RINGP(ring, DRAW4(primtype, DI_SRC_SEL_AUTO_INDEX, 0, 0),
                   &batch->draw_patches);
         OUT_RELOC(ring, ind->bo, indirect->offset, 0, 0);
      }

      emit_marker5(ring, 7);
      fd_reset_wfi(batch);

      return;
   }

   if (info->index_size) {
      assert(!info->has_user_indices);

      idx_buffer = info->index.resource;
      idx_type = fd4_size2indextype(info->index_size);
      max_indices = idx_buffer->width0 / info->index_size;
      idx_offset = index_offset + draw->start * info->index_size;
      src_sel = DI_SRC_SEL_DMA;
   } else {
      idx_buffer = NULL;
      idx_type = INDEX4_SIZE_32_BIT;
      max_indices = 0;
      idx_offset = 0;
      src_sel = DI_SRC_SEL_AUTO_INDEX;
   }

   fd5_draw(batch, ring, primtype, vismode, src_sel, draw->count,
            info->instance_count, idx_type, max_indices, idx_offset,
            idx_buffer);
}

#endif /* FD5_DRAW_H_ */
