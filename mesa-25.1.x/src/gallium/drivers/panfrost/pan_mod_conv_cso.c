/*
 * Copyright (C) 2023 Amazon.com, Inc. or its affiliates
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
 */

#include "pan_mod_conv_cso.h"
#include "nir/pipe_nir.h"
#include "nir_builder.h"
#include "pan_context.h"
#include "pan_resource.h"
#include "pan_screen.h"
#include "pan_shader.h"

#define panfrost_afbc_add_info_ubo(name, b)                                    \
   nir_variable *info_ubo = nir_variable_create(                               \
      b.shader, nir_var_mem_ubo,                                               \
      glsl_array_type(glsl_uint_type(),                                        \
                      sizeof(struct panfrost_afbc_##name##_info) / 4, 0),      \
      "info_ubo");                                                             \
   info_ubo->data.driver_location = 0;

#define panfrost_afbc_get_info_field(name, b, field)                           \
   nir_load_ubo(                                                               \
      (b), 1, sizeof(((struct panfrost_afbc_##name##_info *)0)->field) * 8,    \
      nir_imm_int(b, 0),                                                       \
      nir_imm_int(b, offsetof(struct panfrost_afbc_##name##_info, field)),     \
      .align_mul = 4, .range = ~0)

#define panfrost_mtk_add_info_ubo(name, b)                                     \
   nir_variable *info_ubo = nir_variable_create(                               \
      b.shader, nir_var_mem_ubo,                                               \
      glsl_array_type(glsl_uint_type(),                                        \
                      sizeof(struct panfrost_mtk_##name##_info) / 4, 0),       \
      "info_ubo");                                                             \
   info_ubo->data.driver_location = 0;

#define panfrost_mtk_get_info_field(name, b, field)                            \
   nir_load_ubo(                                                               \
      (b), 1, sizeof(((struct panfrost_mtk_##name##_info *)0)->field) * 8,     \
      nir_imm_int(b, 0),                                                       \
      nir_imm_int(b, offsetof(struct panfrost_mtk_##name##_info, field)),      \
      .align_mul = 4, .range = ~0)

static nir_def *
read_afbc_header(nir_builder *b, nir_def *buf, nir_def *idx)
{
   nir_def *offset = nir_imul_imm(b, idx, AFBC_HEADER_BYTES_PER_TILE);
   return nir_load_global(b, nir_iadd(b, buf, nir_u2u64(b, offset)), 16,
                          AFBC_HEADER_BYTES_PER_TILE / 4, 32);
}

static void
write_afbc_header(nir_builder *b, nir_def *buf, nir_def *idx, nir_def *hdr)
{
   nir_def *offset = nir_imul_imm(b, idx, AFBC_HEADER_BYTES_PER_TILE);
   nir_store_global(b, nir_iadd(b, buf, nir_u2u64(b, offset)), 16, hdr, 0xF);
}

static nir_def *
get_morton_index(nir_builder *b, nir_def *idx, nir_def *src_stride,
                 nir_def *dst_stride)
{
   nir_def *x = nir_umod(b, idx, dst_stride);
   nir_def *y = nir_udiv(b, idx, dst_stride);

   nir_def *offset = nir_imul(b, nir_iand_imm(b, y, ~0x7), src_stride);
   offset = nir_iadd(b, offset, nir_ishl_imm(b, nir_ushr_imm(b, x, 3), 6));

   x = nir_iand_imm(b, x, 0x7);
   x = nir_iand_imm(b, nir_ior(b, x, nir_ishl_imm(b, x, 2)), 0x13);
   x = nir_iand_imm(b, nir_ior(b, x, nir_ishl_imm(b, x, 1)), 0x15);
   y = nir_iand_imm(b, y, 0x7);
   y = nir_iand_imm(b, nir_ior(b, y, nir_ishl_imm(b, y, 2)), 0x13);
   y = nir_iand_imm(b, nir_ior(b, y, nir_ishl_imm(b, y, 1)), 0x15);
   nir_def *tile_idx = nir_ior(b, x, nir_ishl_imm(b, y, 1));

   return nir_iadd(b, offset, tile_idx);
}

static nir_def *
get_superblock_size(nir_builder *b, unsigned arch, nir_def *hdr,
                    nir_def *uncompressed_size)
{
   nir_def *size = nir_imm_int(b, 0);

   unsigned body_base_ptr_len = 32;
   unsigned nr_subblocks = 16;
   unsigned sz_len = 6; /* bits */
   nir_def *words[4];
   nir_def *mask = nir_imm_int(b, (1 << sz_len) - 1);
   nir_def *is_solid_color = nir_imm_bool(b, false);

   for (int i = 0; i < 4; i++)
      words[i] = nir_channel(b, hdr, i);

   /* Sum up all of the subblock sizes */
   for (int i = 0; i < nr_subblocks; i++) {
      nir_def *subblock_size;
      unsigned bitoffset = body_base_ptr_len + (i * sz_len);
      unsigned start = bitoffset / 32;
      unsigned end = (bitoffset + (sz_len - 1)) / 32;
      unsigned offset = bitoffset % 32;

      /* Handle differently if the size field is split between two words
       * of the header */
      if (start != end) {
         subblock_size = nir_ior(b, nir_ushr_imm(b, words[start], offset),
                                 nir_ishl_imm(b, words[end], 32 - offset));
         subblock_size = nir_iand(b, subblock_size, mask);
      } else {
         subblock_size =
            nir_ubitfield_extract_imm(b, words[start], offset, sz_len);
      }
      subblock_size = nir_bcsel(b, nir_ieq_imm(b, subblock_size, 1),
                                uncompressed_size, subblock_size);
      size = nir_iadd(b, size, subblock_size);

      /* When the first subblock size is set to zero, the whole superblock is
       * filled with a solid color specified in the header */
      if (arch >= 7 && i == 0)
         is_solid_color = nir_ieq_imm(b, size, 0);
   }

   return (arch >= 7)
             ? nir_bcsel(b, is_solid_color, nir_imm_zero(b, 1, 32), size)
             : size;
}

static nir_def *
get_packed_offset(nir_builder *b, nir_def *metadata, nir_def *idx,
                  nir_def **out_size)
{
   nir_def *metadata_offset =
      nir_u2u64(b, nir_imul_imm(b, idx, sizeof(struct pan_afbc_block_info)));
   nir_def *range_ptr = nir_iadd(b, metadata, metadata_offset);
   nir_def *entry = nir_load_global(b, range_ptr, 4,
                                    sizeof(struct pan_afbc_block_info) / 4, 32);
   nir_def *offset =
      nir_channel(b, entry, offsetof(struct pan_afbc_block_info, offset) / 4);

   if (out_size)
      *out_size =
         nir_channel(b, entry, offsetof(struct pan_afbc_block_info, size) / 4);

   return nir_u2u64(b, offset);
}

#define MAX_LINE_SIZE 16

static void
copy_superblock(nir_builder *b, nir_def *dst, nir_def *dst_idx, nir_def *hdr_sz,
                nir_def *src, nir_def *src_idx, nir_def *metadata,
                nir_def *meta_idx, unsigned align)
{
   nir_def *hdr = read_afbc_header(b, src, src_idx);
   nir_def *src_body_base_ptr = nir_u2u64(b, nir_channel(b, hdr, 0));
   nir_def *src_bodyptr = nir_iadd(b, src, src_body_base_ptr);

   nir_def *size;
   nir_def *dst_offset = get_packed_offset(b, metadata, meta_idx, &size);
   nir_def *dst_body_base_ptr = nir_iadd(b, dst_offset, hdr_sz);
   nir_def *dst_bodyptr = nir_iadd(b, dst, dst_body_base_ptr);

   /* Replace the `base_body_ptr` field if not zero (solid color) */
   nir_def *hdr2 =
      nir_vector_insert_imm(b, hdr, nir_u2u32(b, dst_body_base_ptr), 0);
   hdr = nir_bcsel(b, nir_ieq_imm(b, src_body_base_ptr, 0), hdr, hdr2);
   write_afbc_header(b, dst, dst_idx, hdr);

   nir_variable *offset_var =
      nir_local_variable_create(b->impl, glsl_uint_type(), "offset");
   nir_store_var(b, offset_var, nir_imm_int(b, 0), 1);
   nir_loop *loop = nir_push_loop(b);
   {
      nir_def *offset = nir_load_var(b, offset_var);
      nir_if *loop_check = nir_push_if(b, nir_uge(b, offset, size));
      nir_jump(b, nir_jump_break);
      nir_push_else(b, loop_check);
      unsigned line_sz = align <= MAX_LINE_SIZE ? align : MAX_LINE_SIZE;
      for (unsigned i = 0; i < align / line_sz; ++i) {
         nir_def *src_line = nir_iadd(b, src_bodyptr, nir_u2u64(b, offset));
         nir_def *dst_line = nir_iadd(b, dst_bodyptr, nir_u2u64(b, offset));
         nir_store_global(
            b, dst_line, line_sz,
            nir_load_global(b, src_line, line_sz, line_sz / 4, 32), ~0);
         offset = nir_iadd_imm(b, offset, line_sz);
      }
      nir_store_var(b, offset_var, offset, 0x1);
      nir_pop_if(b, loop_check);
   }
   nir_pop_loop(b, loop);
}

#define panfrost_afbc_size_get_info_field(b, field)                            \
   panfrost_afbc_get_info_field(size, b, field)

static nir_shader *
panfrost_create_afbc_size_shader(struct panfrost_screen *screen, unsigned bpp,
                                 unsigned align)
{
   struct panfrost_device *dev = pan_device(&screen->base);

   nir_builder b = nir_builder_init_simple_shader(
      MESA_SHADER_COMPUTE, screen->vtbl.get_compiler_options(),
      "panfrost_afbc_size(bpp=%d)", bpp);

   panfrost_afbc_add_info_ubo(size, b);

   nir_def *coord = nir_load_global_invocation_id(&b, 32);
   nir_def *block_idx = nir_channel(&b, coord, 0);
   nir_def *src = panfrost_afbc_size_get_info_field(&b, src);
   nir_def *metadata = panfrost_afbc_size_get_info_field(&b, metadata);
   nir_def *uncompressed_size = nir_imm_int(&b, 4 * 4 * bpp / 8); /* bytes */

   nir_def *hdr = read_afbc_header(&b, src, block_idx);
   nir_def *size = get_superblock_size(&b, dev->arch, hdr, uncompressed_size);
   size = nir_iand(&b, nir_iadd(&b, size, nir_imm_int(&b, align - 1)),
                   nir_inot(&b, nir_imm_int(&b, align - 1)));

   nir_def *offset = nir_u2u64(
      &b,
      nir_iadd(&b,
               nir_imul_imm(&b, block_idx, sizeof(struct pan_afbc_block_info)),
               nir_imm_int(&b, offsetof(struct pan_afbc_block_info, size))));
   nir_store_global(&b, nir_iadd(&b, metadata, offset), 4, size, 0x1);

   return b.shader;
}

#define panfrost_afbc_pack_get_info_field(b, field)                            \
   panfrost_afbc_get_info_field(pack, b, field)

static nir_shader *
panfrost_create_afbc_pack_shader(struct panfrost_screen *screen, unsigned align,
                                 bool tiled)
{
   nir_builder b = nir_builder_init_simple_shader(
      MESA_SHADER_COMPUTE, screen->vtbl.get_compiler_options(),
      "panfrost_afbc_pack");

   panfrost_afbc_add_info_ubo(pack, b);

   nir_def *coord = nir_load_global_invocation_id(&b, 32);
   nir_def *src_stride = panfrost_afbc_pack_get_info_field(&b, src_stride);
   nir_def *dst_stride = panfrost_afbc_pack_get_info_field(&b, dst_stride);
   nir_def *dst_idx = nir_channel(&b, coord, 0);
   nir_def *src_idx =
      tiled ? get_morton_index(&b, dst_idx, src_stride, dst_stride) : dst_idx;
   nir_def *src = panfrost_afbc_pack_get_info_field(&b, src);
   nir_def *dst = panfrost_afbc_pack_get_info_field(&b, dst);
   nir_def *header_size =
      nir_u2u64(&b, panfrost_afbc_pack_get_info_field(&b, header_size));
   nir_def *metadata = panfrost_afbc_pack_get_info_field(&b, metadata);

   copy_superblock(&b, dst, dst_idx, header_size, src, src_idx, metadata,
                   src_idx, align);

   return b.shader;
}

#define panfrost_mtk_detile_get_info_field(b, field)                            \
   panfrost_mtk_get_info_field(detile, b, field)

static nir_def *
pan_mtk_tiled_from_linear(nir_builder *b, nir_def *linear, nir_def *tiles_per_stride, nir_def *width)
{
   nir_def *tiled;
   /* uvec2 tlc = uvec2(linear) >> uvec2(2u, 5u) */
   nir_def *tlc = nir_ushr(b, linear,
                           nir_imm_ivec2(b, 2, 5));

   /* uvec2 txc = uvec2(linear) & uvec2(3u, 31u) */
   nir_def *txc = nir_iand(b, linear,
                           nir_imm_ivec2(b, 3, 31));

   /* uint tlo = tlc.y * tiles_per_stride + tlc.x */
   nir_def *tlo = nir_iadd(b,
                           nir_imul(b,
                                    nir_channel(b, tlc, 1),
                                    tiles_per_stride),
                           nir_channel(b, tlc, 0));
   nir_def *txcx = nir_channel(b, txc, 0);
   nir_def *txcy = nir_channel(b, txc, 1);
   nir_def *txcytmp = nir_vec2(b, txcy,
                               nir_ushr_imm(b, txcy, 1));

   /* txo = (uvec2(txc.y, txc.y >> 1) << uvec2(2u)) | txc.xx */
   nir_def *txo = nir_ior(b,
                          nir_ishl_imm(b, txcytmp, 2),
                          nir_vec2(b, txcx, txcx));

   /* uvec2 off = (uvec2(tlo) << uvec2(7u, 6u)) | txo */
   nir_def *off = nir_ior(b,
                          nir_ishl(b,
                                   nir_vec2(b, tlo, tlo),
                                   nir_imm_ivec2(b, 7, 6)),
                          txo);

   /* convert to 2D coord
    * tiled.xy = off % (width / 4, width / 4)
    * tiled.zw = off / (width / 4, width / 4) */
   nir_def *width4 = nir_ishl_imm(b, tiles_per_stride, 2);
   width4 = nir_vec2(b, width4, width4);
   nir_def *tiled_xy = nir_umod(b, off, width4);
   nir_def *tiled_zw = nir_udiv(b, off, width4);
   tiled = nir_vec4(b,
                    nir_channel(b, tiled_xy, 0),
                    nir_channel(b, tiled_xy, 1),
                    nir_channel(b, tiled_zw, 0),
                    nir_channel(b, tiled_zw, 1));

   return tiled;
}

static nir_shader *
panfrost_create_mtk_detile_shader(struct panfrost_screen *screen, unsigned align,
                                  bool is_tiled)
{
   const struct panfrost_device *device = &screen->dev;
   bool tint_yuv = (device->debug & PAN_DBG_YUV) != 0;
   nir_builder b = nir_builder_init_simple_shader(
      MESA_SHADER_COMPUTE, screen->vtbl.get_compiler_options(),
      "panfrost_mtk_detile");
   b.shader->info.workgroup_size[0] = 4;
   b.shader->info.workgroup_size[1] = 16;
   b.shader->info.workgroup_size[2] = 1;

   const struct glsl_type *image_type =
      glsl_image_type(GLSL_SAMPLER_DIM_2D, /*is_array*/ false, GLSL_TYPE_UINT);

   panfrost_mtk_add_info_ubo(detile, b);

   nir_variable *y_tiled =
      nir_variable_create(b.shader, nir_var_image, image_type, "y_tiled");
   y_tiled->data.binding = 0;
   y_tiled->data.image.format = PIPE_FORMAT_R8G8B8A8_UINT;
   BITSET_SET(b.shader->info.images_used, 0);
   nir_variable *uv_tiled =
      nir_variable_create(b.shader, nir_var_image, image_type, "uv_tiled");
   uv_tiled->data.binding = 1;
   uv_tiled->data.image.format = PIPE_FORMAT_R8G8B8A8_UINT;
   BITSET_SET(b.shader->info.images_used, 1);

   nir_variable *y_linear =
      nir_variable_create(b.shader, nir_var_image, image_type, "y_linear");
   y_linear->data.binding = 2;
   y_linear->data.image.format = PIPE_FORMAT_R8G8B8A8_UINT;
   BITSET_SET(b.shader->info.images_used, 2);

   nir_variable *uv_linear =
      nir_variable_create(b.shader, nir_var_image, image_type, "uv_linear");
   uv_linear->data.binding = 3;
   uv_linear->data.image.format = PIPE_FORMAT_R8G8B8A8_UINT;
   BITSET_SET(b.shader->info.images_used, 3);

   nir_def *linear = nir_load_global_invocation_id(&b, 32);
   nir_def *tiles_per_stride =
      panfrost_mtk_detile_get_info_field(&b, tiles_per_stride);
   nir_def *src_width = panfrost_mtk_detile_get_info_field(&b, src_width);

   nir_def *zero = nir_imm_int(&b, 0);

   nir_def *coord = nir_vec2(&b,
                             nir_channel(&b, linear, 0),
                             nir_channel(&b, linear, 1));

   nir_def *tiled = pan_mtk_tiled_from_linear(&b, coord, tiles_per_stride, src_width);

   nir_def *tiled_xz = nir_vec4(&b, nir_channel(&b, tiled, 0),
                                nir_channel(&b, tiled, 2), zero, zero);
   nir_def *tiled_yw = nir_vec4(&b, nir_channel(&b, tiled, 1),
                                nir_channel(&b, tiled, 3), zero, zero);

   nir_def *yval = nir_image_load(&b, 4, 32, zero, tiled_xz,
                                  zero /* sample */, zero /* lod */,
                                  .access = ACCESS_NON_WRITEABLE,
                                  .image_dim = GLSL_SAMPLER_DIM_2D,
                                  .image_array = false,
                                  .dest_type = nir_type_uint32);
   nir_def *uvval;

   nir_def *dst_y_coord = nir_vec4(&b,
                                   nir_channel(&b, coord, 0),
                                   nir_channel(&b, coord, 1),
                                   zero, zero);
   /* store Y data */
   nir_def *img_deref_st_y = nir_imm_int(&b, 2);
   nir_image_store(&b, img_deref_st_y, dst_y_coord, zero /* sample */,
                   yval, zero /* lod */,
                   .access = ACCESS_NON_READABLE,
                   .image_dim = GLSL_SAMPLER_DIM_2D,
                   .image_array = false, .src_type = nir_type_uint32);

   /* store UV data */
   nir_def *odd_even_line = nir_iand_imm(&b,
                                     nir_channel(&b, dst_y_coord, 1),
                                     1);
   nir_push_if(&b, nir_ieq_imm(&b, odd_even_line, 0));
   {
      if (tint_yuv) {
         /* use just blue for chroma */
         uvval = nir_imm_ivec4(&b, 0xc0, 0x80, 0xc0, 0x80);
      } else {
         nir_def *img_deref_uv = nir_imm_int(&b, 1);
         uvval = nir_image_load(&b, 4, 32, img_deref_uv, tiled_yw,
                                zero /* sample */, zero /* lod */,
                                .access = ACCESS_NON_WRITEABLE,
                                .image_dim = GLSL_SAMPLER_DIM_2D,
                                .image_array = false,
                                .dest_type = nir_type_uint32);
      }
      nir_def *dst_uv_coord = nir_ishr(&b, dst_y_coord,
                                       nir_imm_ivec4(&b, 0, 1, 0, 0));
      nir_def *img_deref_st_uv = nir_imm_int(&b, 3);
      nir_image_store(&b, img_deref_st_uv, dst_uv_coord, zero /* sample */,
                      uvval, zero /* lod */,
                      .access = ACCESS_NON_READABLE,
                      .image_dim = GLSL_SAMPLER_DIM_2D,
                      .image_array = false, .src_type = nir_type_uint32);
   }
   nir_pop_if(&b, NULL);

   return b.shader;
}

struct pan_mod_convert_shader_data *
panfrost_get_mod_convert_shaders(struct panfrost_context *ctx,
                                 struct panfrost_resource *rsrc, unsigned align)
{
   struct pipe_context *pctx = &ctx->base;
   struct panfrost_screen *screen = pan_screen(ctx->base.screen);
   bool tiled = rsrc->image.layout.modifier & AFBC_FORMAT_MOD_TILED;
   struct pan_mod_convert_shader_key key = {
      .bpp = util_format_get_blocksizebits(rsrc->base.format),
      .align = align,
      .tiled = tiled,
   };

   pthread_mutex_lock(&ctx->mod_convert_shaders.lock);
   struct hash_entry *he =
      _mesa_hash_table_search(ctx->mod_convert_shaders.shaders, &key);
   struct pan_mod_convert_shader_data *shader = he ? he->data : NULL;
   pthread_mutex_unlock(&ctx->mod_convert_shaders.lock);

   if (shader)
      return shader;

   shader = rzalloc(ctx->mod_convert_shaders.shaders, struct pan_mod_convert_shader_data);
   shader->key = key;

#define COMPILE_SHADER(name, ...)                                              \
   {                                                                           \
      nir_shader *nir =                                                        \
         panfrost_create_##name##_shader(screen, __VA_ARGS__);            \
      nir->info.num_ubos = 1;                                                  \
      /* "default" UBO is maybe not correct here, but in panfrost we're */     \
      /* using this as an indicator for whether UBO0 is a user UBO */          \
      nir->info.first_ubo_is_default_ubo = true;                               \
      shader->name##_cso = pipe_shader_from_nir(pctx, nir);                    \
   }

   COMPILE_SHADER(afbc_size, key.bpp, key.align);
   COMPILE_SHADER(afbc_pack, key.align, key.tiled);
   COMPILE_SHADER(mtk_detile, key.bpp, key.align);

#undef COMPILE_SHADER

   pthread_mutex_lock(&ctx->mod_convert_shaders.lock);
   _mesa_hash_table_insert(ctx->mod_convert_shaders.shaders, &shader->key, shader);
   pthread_mutex_unlock(&ctx->mod_convert_shaders.lock);

   return shader;
}

DERIVE_HASH_TABLE(pan_mod_convert_shader_key);

void
panfrost_afbc_context_init(struct panfrost_context *ctx)
{
   ctx->mod_convert_shaders.shaders = pan_mod_convert_shader_key_table_create(NULL);
   pthread_mutex_init(&ctx->mod_convert_shaders.lock, NULL);
}

void
panfrost_afbc_context_destroy(struct panfrost_context *ctx)
{
   hash_table_foreach(ctx->mod_convert_shaders.shaders, he) {
      assert(he->data);
      struct pan_mod_convert_shader_data *shader = he->data;
      ctx->base.delete_compute_state(&ctx->base, shader->afbc_size_cso);
      ctx->base.delete_compute_state(&ctx->base, shader->afbc_pack_cso);
      ctx->base.delete_compute_state(&ctx->base, shader->mtk_detile_cso);
   }

   _mesa_hash_table_destroy(ctx->mod_convert_shaders.shaders, NULL);
   pthread_mutex_destroy(&ctx->mod_convert_shaders.lock);
}
