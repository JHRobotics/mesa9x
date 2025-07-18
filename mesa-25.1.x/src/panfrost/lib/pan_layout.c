/*
 * Copyright (C) 2019-2022 Collabora, Ltd.
 * Copyright (C) 2018-2019 Alyssa Rosenzweig
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 */

#include "util/log.h"
#include "util/macros.h"
#include "util/u_math.h"
#include "pan_props.h"
#include "pan_texture.h"

/*
 * List of supported modifiers, in descending order of preference. AFBC is
 * faster than u-interleaved tiling which is faster than linear. Within AFBC,
 * enabling the YUV-like transform is typically a win where possible.
 * AFRC is only used if explicitly asked for (only for RGB formats).
 * Similarly MTK 16L32 is only used if explicitly asked for.
 */
uint64_t pan_best_modifiers[PAN_MODIFIER_COUNT] = {
   DRM_FORMAT_MOD_ARM_AFBC(AFBC_FORMAT_MOD_BLOCK_SIZE_32x8 |
                           AFBC_FORMAT_MOD_SPARSE | AFBC_FORMAT_MOD_SPLIT),
   DRM_FORMAT_MOD_ARM_AFBC(AFBC_FORMAT_MOD_BLOCK_SIZE_32x8 |
                           AFBC_FORMAT_MOD_SPARSE | AFBC_FORMAT_MOD_SPLIT |
                           AFBC_FORMAT_MOD_YTR),

   DRM_FORMAT_MOD_ARM_AFBC(AFBC_FORMAT_MOD_BLOCK_SIZE_16x16 |
                           AFBC_FORMAT_MOD_TILED | AFBC_FORMAT_MOD_SC |
                           AFBC_FORMAT_MOD_SPARSE | AFBC_FORMAT_MOD_YTR),

   DRM_FORMAT_MOD_ARM_AFBC(AFBC_FORMAT_MOD_BLOCK_SIZE_16x16 |
                           AFBC_FORMAT_MOD_TILED | AFBC_FORMAT_MOD_SC |
                           AFBC_FORMAT_MOD_SPARSE),

   DRM_FORMAT_MOD_ARM_AFBC(AFBC_FORMAT_MOD_BLOCK_SIZE_16x16 |
                           AFBC_FORMAT_MOD_SPARSE | AFBC_FORMAT_MOD_YTR),

   DRM_FORMAT_MOD_ARM_AFBC(AFBC_FORMAT_MOD_BLOCK_SIZE_16x16 |
                           AFBC_FORMAT_MOD_SPARSE),

   DRM_FORMAT_MOD_ARM_16X16_BLOCK_U_INTERLEAVED,
   DRM_FORMAT_MOD_LINEAR,

   DRM_FORMAT_MOD_ARM_AFRC(
      AFRC_FORMAT_MOD_CU_SIZE_P0(AFRC_FORMAT_MOD_CU_SIZE_16)),
   DRM_FORMAT_MOD_ARM_AFRC(
      AFRC_FORMAT_MOD_CU_SIZE_P0(AFRC_FORMAT_MOD_CU_SIZE_24)),
   DRM_FORMAT_MOD_ARM_AFRC(
      AFRC_FORMAT_MOD_CU_SIZE_P0(AFRC_FORMAT_MOD_CU_SIZE_32)),
   DRM_FORMAT_MOD_ARM_AFRC(
      AFRC_FORMAT_MOD_CU_SIZE_P0(AFRC_FORMAT_MOD_CU_SIZE_16) |
      AFRC_FORMAT_MOD_LAYOUT_SCAN),
   DRM_FORMAT_MOD_ARM_AFRC(
      AFRC_FORMAT_MOD_CU_SIZE_P0(AFRC_FORMAT_MOD_CU_SIZE_24) |
      AFRC_FORMAT_MOD_LAYOUT_SCAN),
   DRM_FORMAT_MOD_ARM_AFRC(
      AFRC_FORMAT_MOD_CU_SIZE_P0(AFRC_FORMAT_MOD_CU_SIZE_32) |
      AFRC_FORMAT_MOD_LAYOUT_SCAN),

   DRM_FORMAT_MOD_MTK_16L_32S_TILE,
};

/* Table of AFBC superblock sizes */
static const struct pan_block_size afbc_superblock_sizes[] = {
   [AFBC_FORMAT_MOD_BLOCK_SIZE_16x16] = {16, 16},
   [AFBC_FORMAT_MOD_BLOCK_SIZE_32x8] = {32, 8},
   [AFBC_FORMAT_MOD_BLOCK_SIZE_64x4] = {64, 4},
};

/*
 * Given an AFBC modifier, return the superblock size.
 *
 * We do not yet have any use cases for multiplanar YCBCr formats with different
 * superblock sizes on the luma and chroma planes. These formats are unsupported
 * for now.
 */
struct pan_block_size
panfrost_afbc_superblock_size(uint64_t modifier)
{
   unsigned index = (modifier & AFBC_FORMAT_MOD_BLOCK_SIZE_MASK);

   assert(drm_is_afbc(modifier));
   assert(index < ARRAY_SIZE(afbc_superblock_sizes));

   return afbc_superblock_sizes[index];
}

/*
 * Given an AFBC modifier, return the render size.
 */
struct pan_block_size
panfrost_afbc_renderblock_size(uint64_t modifier)
{
   unsigned index = (modifier & AFBC_FORMAT_MOD_BLOCK_SIZE_MASK);

   assert(drm_is_afbc(modifier));
   assert(index < ARRAY_SIZE(afbc_superblock_sizes));

   struct pan_block_size blk_size = afbc_superblock_sizes[index];

  /* The GPU needs to render 16x16 tiles. For wide tiles, that means we
   * have to extend the render region to have a height of 16 pixels.
   */
   blk_size.height = ALIGN_POT(blk_size.height, 16);
   return blk_size;
}

/*
 * Given an AFBC modifier, return the width of the superblock.
 */
unsigned
panfrost_afbc_superblock_width(uint64_t modifier)
{
   return panfrost_afbc_superblock_size(modifier).width;
}

/*
 * Given an AFBC modifier, return the height of the superblock.
 */
unsigned
panfrost_afbc_superblock_height(uint64_t modifier)
{
   return panfrost_afbc_superblock_size(modifier).height;
}

/*
 * Given an AFBC modifier, return if "wide blocks" are used. Wide blocks are
 * defined as superblocks wider than 16 pixels, the minimum (and default) super
 * block width.
 */
bool
panfrost_afbc_is_wide(uint64_t modifier)
{
   return panfrost_afbc_superblock_width(modifier) > 16;
}

/*
 * Given an AFBC modifier, return the subblock size (subdivision of a
 * superblock). This is always 4x4 for now as we only support one AFBC
 * superblock layout.
 */
struct pan_block_size
panfrost_afbc_subblock_size(uint64_t modifier)
{
   return (struct pan_block_size){4, 4};
}

/*
 * Given an AFRC modifier, return whether the layout is optimized for scan
 * order (vs rotation order).
 */
bool
panfrost_afrc_is_scan(uint64_t modifier)
{
   return modifier & AFRC_FORMAT_MOD_LAYOUT_SCAN;
}

struct pan_block_size
panfrost_afrc_clump_size(enum pipe_format format, bool scan)
{
   struct pan_afrc_format_info finfo = panfrost_afrc_get_format_info(format);

   switch (finfo.num_comps) {
   case 1:
      return scan ? (struct pan_block_size){16, 4}
                  : (struct pan_block_size){8, 8};
   case 2:
      return (struct pan_block_size){8, 4};
   case 3:
   case 4:
      return (struct pan_block_size){4, 4};
   default:
      assert(0);
      return (struct pan_block_size){0, 0};
   }
}

static struct pan_block_size
panfrost_afrc_layout_size(uint64_t modifier)
{
   if (panfrost_afrc_is_scan(modifier))
      return (struct pan_block_size){16, 4};
   else
      return (struct pan_block_size){8, 8};
}

struct pan_block_size
panfrost_afrc_tile_size(enum pipe_format format, uint64_t modifier)
{
   bool scan = panfrost_afrc_is_scan(modifier);
   struct pan_block_size clump_sz = panfrost_afrc_clump_size(format, scan);
   struct pan_block_size layout_sz = panfrost_afrc_layout_size(modifier);

   return (struct pan_block_size){clump_sz.width * layout_sz.width,
                                  clump_sz.height * layout_sz.height};
}

unsigned
panfrost_afrc_block_size_from_modifier(uint64_t modifier)
{
   switch (modifier & AFRC_FORMAT_MOD_CU_SIZE_MASK) {
   case AFRC_FORMAT_MOD_CU_SIZE_16:
      return 16;
   case AFRC_FORMAT_MOD_CU_SIZE_24:
      return 24;
   case AFRC_FORMAT_MOD_CU_SIZE_32:
      return 32;
   default:
      unreachable("invalid coding unit size flag in modifier");
   };
}

static unsigned
panfrost_afrc_buffer_alignment_from_modifier(uint64_t modifier)
{
   switch (modifier & AFRC_FORMAT_MOD_CU_SIZE_MASK) {
   case AFRC_FORMAT_MOD_CU_SIZE_16:
      return 1024;
   case AFRC_FORMAT_MOD_CU_SIZE_24:
      return 512;
   case AFRC_FORMAT_MOD_CU_SIZE_32:
      return 2048;
   default:
      unreachable("invalid coding unit size flag in modifier");
   };
}

/*
 * Determine the number of bytes between rows of paging tiles in an AFRC image
 */
uint32_t
pan_afrc_row_stride(enum pipe_format format, uint64_t modifier, uint32_t width)
{
   struct pan_block_size tile_size = panfrost_afrc_tile_size(format, modifier);
   unsigned block_size = panfrost_afrc_block_size_from_modifier(modifier);

   return (width / tile_size.width) * block_size * AFRC_CLUMPS_PER_TILE;
}

/*
 * Given a format, determine the tile size used for u-interleaving. For formats
 * that are already block compressed, this is 4x4. For all other formats, this
 * is 16x16, hence the modifier name.
 */
static inline struct pan_block_size
panfrost_u_interleaved_tile_size(enum pipe_format format)
{
   if (util_format_is_compressed(format))
      return (struct pan_block_size){4, 4};
   else
      return (struct pan_block_size){16, 16};
}

/*
 * Determine the block size used for interleaving. For u-interleaving, this is
 * the tile size. For AFBC, this is the superblock size. For AFRC, this is the
 * paging tile size. For linear textures, this is trivially 1x1.
 */
struct pan_block_size
panfrost_block_size(uint64_t modifier, enum pipe_format format)
{
   if (modifier == DRM_FORMAT_MOD_ARM_16X16_BLOCK_U_INTERLEAVED)
      return panfrost_u_interleaved_tile_size(format);
   else if (drm_is_afbc(modifier))
      return panfrost_afbc_superblock_size(modifier);
   else if (drm_is_afrc(modifier))
      return panfrost_afrc_tile_size(format, modifier);
   else
      return (struct pan_block_size){1, 1};
}

/* For non-AFBC and non-wide AFBC, the render block size matches
 * the block size, but for wide AFBC, the GPU wants the block height
 * to be 16 pixels high.
 */
struct pan_block_size
panfrost_renderblock_size(uint64_t modifier, enum pipe_format format)
{
   if (!drm_is_afbc(modifier))
      return panfrost_block_size(modifier, format);

   return panfrost_afbc_renderblock_size(modifier);
}

/*
 * Determine the tile size used by AFBC. This tiles superblocks themselves.
 * Current GPUs support either 8x8 tiling or no tiling (1x1)
 */
static inline unsigned
pan_afbc_tile_size(uint64_t modifier)
{
   return (modifier & AFBC_FORMAT_MOD_TILED) ? 8 : 1;
}

/*
 * Determine the number of bytes between header rows for an AFBC image. For an
 * image with linear headers, this is simply the number of header blocks
 * (=superblocks) per row times the numbers of bytes per header block. For an
 * image with tiled headers, this is multipled by the number of rows of
 * header blocks are in a tile together.
 */
uint32_t
pan_afbc_row_stride(uint64_t modifier, uint32_t width)
{
   unsigned block_width = panfrost_afbc_superblock_width(modifier);

   return (width / block_width) * pan_afbc_tile_size(modifier) *
          AFBC_HEADER_BYTES_PER_TILE;
}

/*
 * Determine the number of header blocks between header rows. This is equal to
 * the number of bytes between header rows divided by the bytes per blocks of a
 * header tile. This is also divided by the tile size to give a "line stride" in
 * blocks, rather than a real row stride. This is required by Bifrost.
 */
uint32_t
pan_afbc_stride_blocks(uint64_t modifier, uint32_t row_stride_bytes)
{
   return row_stride_bytes /
          (AFBC_HEADER_BYTES_PER_TILE * pan_afbc_tile_size(modifier));
}

/*
 * Determine the required alignment for the slice offset of an image. For
 * now, this is always aligned on 64-byte boundaries. */
uint32_t
pan_slice_align(uint64_t modifier)
{
   return 64;
}

/*
 * Determine the required alignment for the body offset of an AFBC image. For
 * now, this depends only on whether tiling is in use. These minimum alignments
 * are required on all current GPUs.
 */
uint32_t
pan_afbc_body_align(unsigned arch, uint64_t modifier)
{
   if (modifier & AFBC_FORMAT_MOD_TILED)
      return 4096;

   if (arch >= 6)
      return 128;

   return 64;
}

static inline unsigned
format_minimum_alignment(unsigned arch, enum pipe_format format, uint64_t mod)
{
   if (drm_is_afbc(mod))
      return 16;

   if (drm_is_afrc(mod))
      return panfrost_afrc_buffer_alignment_from_modifier(mod);

   if (arch < 7)
      return 64;

   switch (format) {
   /* For v7+, NV12/NV21/I420 have a looser alignment requirement of 16 bytes */
   case PIPE_FORMAT_R8_G8B8_420_UNORM:
   case PIPE_FORMAT_G8_B8R8_420_UNORM:
   case PIPE_FORMAT_R8_G8_B8_420_UNORM:
   case PIPE_FORMAT_R8_B8_G8_420_UNORM:
   case PIPE_FORMAT_R8_G8B8_422_UNORM:
   case PIPE_FORMAT_R8_B8G8_422_UNORM:
      return 16;
   /* the 10 bit formats have even looser alignment */
   case PIPE_FORMAT_R10_G10B10_420_UNORM:
   case PIPE_FORMAT_R10_G10B10_422_UNORM:
      return 1;
   default:
      return 64;
   }
}

/*
 * Computes sizes for checksumming, which is 8 bytes per 16x16 tile.
 * Checksumming is believed to be a CRC variant (CRC64 based on the size?).
 * This feature is also known as "transaction elimination".
 * CRC values are prefetched by 32x32 (64x64 on v12+) regions so size needs to
 * be aligned.
 */

#define CHECKSUM_TILE_WIDTH     16
#define CHECKSUM_TILE_HEIGHT    16
#define CHECKSUM_BYTES_PER_TILE 8

unsigned
panfrost_compute_checksum_size(unsigned arch,
                               struct pan_image_slice_layout *slice,
                               unsigned width, unsigned height)
{
   unsigned checksum_region_size = panfrost_meta_tile_size(arch);
   unsigned checksum_x_tile_per_region =
      (checksum_region_size / CHECKSUM_TILE_WIDTH);
   unsigned checksum_y_tile_per_region =
      (checksum_region_size / CHECKSUM_TILE_HEIGHT);

   unsigned tile_count_x =
      checksum_x_tile_per_region * DIV_ROUND_UP(width, checksum_region_size);
   unsigned tile_count_y =
      checksum_y_tile_per_region * DIV_ROUND_UP(height, checksum_region_size);

   slice->crc.stride = tile_count_x * CHECKSUM_BYTES_PER_TILE;

   return slice->crc.stride * tile_count_y;
}

unsigned
panfrost_get_layer_stride(const struct pan_image_layout *layout, unsigned level)
{
   if (layout->dim != MALI_TEXTURE_DIMENSION_3D)
      return layout->array_stride;
   else if (drm_is_afbc(layout->modifier))
      return layout->slices[level].afbc.surface_stride;
   else
      return layout->slices[level].surface_stride;
}

unsigned
panfrost_get_legacy_stride(const struct pan_image_layout *layout,
                           unsigned level)
{
   unsigned row_stride = layout->slices[level].row_stride;
   struct pan_block_size block_size =
      panfrost_renderblock_size(layout->modifier, layout->format);

   if (drm_is_afbc(layout->modifier)) {
      unsigned width = u_minify(layout->width, level);
      unsigned alignment =
         block_size.width * pan_afbc_tile_size(layout->modifier);

      width = ALIGN_POT(width, alignment);
      return width * util_format_get_blocksize(layout->format);
   } else if (drm_is_afrc(layout->modifier)) {
      struct pan_block_size tile_size =
         panfrost_afrc_tile_size(layout->format, layout->modifier);

      return row_stride / tile_size.height;
   } else {
      return row_stride / block_size.height;
   }
}

unsigned
panfrost_from_legacy_stride(unsigned legacy_stride, enum pipe_format format,
                            uint64_t modifier)
{
   struct pan_block_size block_size =
      panfrost_renderblock_size(modifier, format);

   if (drm_is_afbc(modifier)) {
      unsigned width = legacy_stride / util_format_get_blocksize(format);

      return pan_afbc_row_stride(modifier, width);
   } else if (drm_is_afrc(modifier)) {
      struct pan_block_size tile_size =
         panfrost_afrc_tile_size(format, modifier);

      return legacy_stride * tile_size.height;
   } else {
      return legacy_stride * block_size.height;
   }
}

/* Computes the offset into a texture at a particular level/face. Add to
 * the base address of a texture to get the address to that level/face */

unsigned
panfrost_texture_offset(const struct pan_image_layout *layout, unsigned level,
                        unsigned array_idx, unsigned surface_idx)
{
   return layout->slices[level].offset + (array_idx * layout->array_stride) +
          (surface_idx * layout->slices[level].surface_stride);
}

bool
pan_image_layout_init(unsigned arch, struct pan_image_layout *layout,
                      const struct pan_image_explicit_layout *explicit_layout)
{
   /* Explicit stride only work with non-mipmap, non-array, single-sample
    * 2D image without CRC.
    */
   if (explicit_layout &&
       (layout->depth > 1 || layout->nr_samples > 1 || layout->array_size > 1 ||
        layout->dim != MALI_TEXTURE_DIMENSION_2D || layout->nr_slices > 1 ||
        layout->crc))
      return false;

   bool afbc = drm_is_afbc(layout->modifier);
   bool afrc = drm_is_afrc(layout->modifier);
   int align_req =
      format_minimum_alignment(arch, layout->format, layout->modifier);

   /* Mandate alignment */
   if (explicit_layout) {
      bool rejected = false;

      int align_mask = align_req - 1;

      if (arch >= 7) {
         rejected = ((explicit_layout->offset & align_mask) ||
                     (explicit_layout->row_stride & align_mask));
      } else {
         rejected = (explicit_layout->offset & align_mask);
      }

      if (rejected) {
         mesa_loge(
            "panfrost: rejecting image due to unsupported offset or stride "
            "alignment.\n");
         return false;
      }
   }

   unsigned fmt_blocksize = util_format_get_blocksize(layout->format);

   /* MSAA is implemented as a 3D texture with z corresponding to the
    * sample #, horrifyingly enough */

   assert(layout->depth == 1 || layout->nr_samples == 1);

   bool linear = layout->modifier == DRM_FORMAT_MOD_LINEAR;
   bool is_3d = layout->dim == MALI_TEXTURE_DIMENSION_3D;

   uint64_t offset = explicit_layout ? explicit_layout->offset : 0;
   struct pan_block_size renderblk_size =
      panfrost_renderblock_size(layout->modifier, layout->format);
   struct pan_block_size block_size =
      panfrost_block_size(layout->modifier, layout->format);

   unsigned width = layout->width;
   unsigned height = layout->height;
   unsigned depth = layout->depth;

   unsigned align_w = renderblk_size.width;
   unsigned align_h = renderblk_size.height;

   /* For tiled AFBC, align to tiles of superblocks (this can be large) */
   if (afbc) {
      align_w *= pan_afbc_tile_size(layout->modifier);
      align_h *= pan_afbc_tile_size(layout->modifier);
   }

   for (unsigned l = 0; l < layout->nr_slices; ++l) {
      struct pan_image_slice_layout *slice = &layout->slices[l];

      unsigned effective_width =
         ALIGN_POT(util_format_get_nblocksx(layout->format, width), align_w);
      unsigned effective_height =
         ALIGN_POT(util_format_get_nblocksy(layout->format, height), align_h);
      unsigned row_stride;

      /* Align levels to cache-line as a performance improvement for
       * linear/tiled and as a requirement for AFBC */

      offset = ALIGN_POT(offset, pan_slice_align(layout->modifier));

      slice->offset = offset;

      if (afrc) {
         row_stride = pan_afrc_row_stride(layout->format, layout->modifier,
                                          effective_width);
      } else {
         row_stride = fmt_blocksize * effective_width * block_size.height;
      }

      /* On v7+ row_stride and offset alignment requirement are equal */
      if (arch >= 7) {
         row_stride = ALIGN_POT(row_stride, align_req);
      }

      if (explicit_layout && !afbc && !afrc) {
         /* Make sure the explicit stride is valid */
         if (explicit_layout->row_stride < row_stride) {
            mesa_loge("panfrost: rejecting image due to invalid row stride.\n");
            return false;
         }

         row_stride = explicit_layout->row_stride;
      } else if (linear) {
         /* Keep lines alignment on 64 byte for performance */
         row_stride = ALIGN_POT(row_stride, 64);
      }

      uint64_t slice_one_size =
         (uint64_t)row_stride * (effective_height / block_size.height);

      /* Compute AFBC sizes if necessary */
      if (afbc) {
         slice->row_stride =
            pan_afbc_row_stride(layout->modifier, effective_width);
         slice->afbc.stride = effective_width / block_size.width;
         slice->afbc.nr_blocks =
            slice->afbc.stride * (effective_height / block_size.height);
         slice->afbc.header_size =
            ALIGN_POT(slice->afbc.nr_blocks * AFBC_HEADER_BYTES_PER_TILE,
                      pan_afbc_body_align(arch, layout->modifier));

         if (explicit_layout &&
             explicit_layout->row_stride < slice->row_stride) {
            mesa_loge("panfrost: rejecting image due to invalid row stride.\n");
            return false;
         }

         /* AFBC body size */
         slice->afbc.body_size = slice_one_size;

         /* 3D AFBC resources have all headers placed at the
          * beginning instead of having them split per depth
          * level
          */
         if (is_3d) {
            slice->afbc.surface_stride = slice->afbc.header_size;
            slice->afbc.header_size *= depth;
            slice->afbc.body_size *= depth;
            offset += slice->afbc.header_size;
         } else {
            slice_one_size += slice->afbc.header_size;
            slice->afbc.surface_stride = slice_one_size;
         }
      } else {
         slice->row_stride = row_stride;
      }

      uint64_t slice_full_size = slice_one_size * depth * layout->nr_samples;

      slice->surface_stride = slice_one_size;

      /* Compute AFBC sizes if necessary */

      offset += slice_full_size;

      /* We can't use slice_full_size_B for AFBC(3D), otherwise the headers are
       * not counted. */
      if (afbc)
         slice->size = slice->afbc.body_size + slice->afbc.header_size;
      else
         slice->size = slice_full_size;

      /* Add a checksum region if necessary */
      if (layout->crc) {
         slice->crc.size =
            panfrost_compute_checksum_size(arch, slice, width, height);

         slice->crc.offset = offset;
         offset += slice->crc.size;
         slice->size += slice->crc.size;
      }

      width = u_minify(width, 1);
      height = u_minify(height, 1);
      depth = u_minify(depth, 1);
   }

   /* Arrays and cubemaps have the entire miptree duplicated */
   layout->array_stride = ALIGN_POT(offset, 64);
   if (explicit_layout)
      layout->data_size = offset;
   else
      layout->data_size = ALIGN_POT(
         (uint64_t)layout->array_stride * (uint64_t)layout->array_size, 4096);

   return true;
}

void
pan_iview_get_surface(const struct pan_image_view *iview, unsigned level,
                      unsigned layer, unsigned sample, struct pan_surface *surf)
{
   const struct util_format_description *fdesc =
      util_format_description(iview->format);


   /* In case of multiplanar depth/stencil, the stencil is always on
    * plane 1. Combined depth/stencil only has one plane, so depth
    * will be on plane 0 in either case.
    */
   const struct pan_image *image = util_format_has_stencil(fdesc)
      ? pan_image_view_get_s_plane(iview)
      : pan_image_view_get_plane(iview, 0);

   level += iview->first_level;
   assert(level < image->layout.nr_slices);

   layer += iview->first_layer;

   bool is_3d = image->layout.dim == MALI_TEXTURE_DIMENSION_3D;
   const struct pan_image_slice_layout *slice = &image->layout.slices[level];
   uint64_t base = image->data.base + image->data.offset;

   if (drm_is_afbc(image->layout.modifier)) {
      assert(!sample);

      if (is_3d) {
         ASSERTED unsigned depth = u_minify(image->layout.depth, level);
         assert(layer < depth);
         surf->afbc.header =
            base + slice->offset + (layer * slice->afbc.surface_stride);
         surf->afbc.body = base + slice->offset + slice->afbc.header_size +
                           (slice->surface_stride * layer);
      } else {
         assert(layer < image->layout.array_size);
         surf->afbc.header =
            base + panfrost_texture_offset(&image->layout, level, layer, 0);
         surf->afbc.body = surf->afbc.header + slice->afbc.header_size;
      }
   } else {
      unsigned array_idx = is_3d ? 0 : layer;
      unsigned surface_idx = is_3d ? layer : sample;

      surf->data = base + panfrost_texture_offset(&image->layout, level,
                                                  array_idx, surface_idx);
   }
}
