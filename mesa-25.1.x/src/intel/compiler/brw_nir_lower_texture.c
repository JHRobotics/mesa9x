/*
 * Copyright © 2024 Intel Corporation
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
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#include "compiler/nir/nir_builder.h"
#include "compiler/nir/nir_format_convert.h"
#include "brw_nir.h"

/**
 * Pack either the explicit LOD or LOD bias and the array index together.
 */
static bool
pack_lod_and_array_index(nir_builder *b, nir_tex_instr *tex)
{
   /* If 32-bit texture coordinates are used, pack either the explicit LOD or
    * LOD bias and the array index into a single (32-bit) value.
    */
   int lod_index = nir_tex_instr_src_index(tex, nir_tex_src_lod);
   if (lod_index < 0) {
      lod_index = nir_tex_instr_src_index(tex, nir_tex_src_bias);

      /* The explicit LOD or LOD bias may not be found if this lowering has
       * already occured.  The explicit LOD may also not be found in some
       * cases where it is zero.
       */
      if (lod_index < 0)
         return false;
   }

   assert(nir_tex_instr_src_type(tex, lod_index) == nir_type_float);

   /* Also do not perform this packing if the explicit LOD is zero. */
   if (tex->op == nir_texop_txl &&
       nir_src_is_const(tex->src[lod_index].src) &&
       nir_src_as_float(tex->src[lod_index].src) == 0.0) {
      return false;
   }

   const int coord_index = nir_tex_instr_src_index(tex, nir_tex_src_coord);
   assert(coord_index >= 0);

   nir_def *lod = tex->src[lod_index].src.ssa;
   nir_def *coord = tex->src[coord_index].src.ssa;

   assert(nir_tex_instr_src_type(tex, coord_index) == nir_type_float);

   if (coord->bit_size < 32)
      return false;

   b->cursor = nir_before_instr(&tex->instr);

   /* First, combine the two values.  The packing format is a little weird.
    * The explicit LOD / LOD bias is stored as float, as normal.  However, the
    * array index is converted to an integer and smashed into the low 9 bits.
    */
   const unsigned array_index = tex->coord_components - 1;

   nir_def *clamped_ai =
      nir_umin(b,
               nir_f2u32(b, nir_fround_even(b, nir_channel(b, coord,
                                                           array_index))),
               nir_imm_int(b, 511));

   nir_def *lod_ai = nir_ior(b, nir_iand_imm(b, lod, 0xfffffe00), clamped_ai);

   /* Second, replace the coordinate with a new value that has one fewer
    * component (i.e., drop the array index).
    */
   nir_def *reduced_coord = nir_trim_vector(b, coord,
                                            tex->coord_components - 1);
   tex->coord_components--;

   /* Finally, remove the old sources and add the new. */
   nir_src_rewrite(&tex->src[coord_index].src, reduced_coord);

   nir_tex_instr_remove_src(tex, lod_index);
   nir_tex_instr_add_src(tex, nir_tex_src_backend1, lod_ai);

   return true;
}

/**
 * Pack either the explicit LOD/Bias and the offset together.
 */
static bool
pack_lod_or_bias_and_offset(nir_builder *b, nir_tex_instr *tex)
{
   /* If there is no backend2, it means there was no offset to pack so just
    * bail.
    */
   int backend2_index = nir_tex_instr_src_index(tex, nir_tex_src_backend2);
   if (backend2_index < 0)
      return false;

   /* If 32-bit texture coordinates are used, pack either the explicit LOD or
    * LOD bias and the array index into a single (32-bit) value.
    */
   int lod_index = nir_tex_instr_src_index(tex, nir_tex_src_lod);
   if (lod_index < 0) {
      lod_index = nir_tex_instr_src_index(tex, nir_tex_src_bias);

      /* The explicit LOD or LOD bias may not be found if this lowering has
       * already occured.  The explicit LOD may also not be found in some
       * cases where it is zero.
       */
      if (lod_index < 0)
         return false;
   }

   assert(nir_tex_instr_src_type(tex, lod_index) == nir_type_float);

   /* Also do not perform this packing if the explicit LOD is zero. */
   if (nir_src_is_const(tex->src[lod_index].src) &&
       nir_src_as_float(tex->src[lod_index].src) == 0.0) {
      return false;
   }

   /* When using the programmable offsets instruction gather4_po_l_c with
    * SIMD16 or SIMD32 the U, V offsets are combined with LOD/bias parameters
    * on the 12 LSBs. For the offset parameters on gather instructions the 6
    * least significant bits are honored as signed value with a range
    * [-32..31].
    *
    * Offsets should already have been packed in pack_const_offset().
    *
    *    ------------------------------------------
    *    |Bits     | [31:12]  | [11:6]  | [5:0]   |
    *    ------------------------------------------
    *    |OffsetUV | LOD/Bias | OffsetV | OffsetU |
    *    ------------------------------------------
    */
   nir_def *lod = tex->src[lod_index].src.ssa;
   nir_def *backend2 = tex->src[backend2_index].src.ssa;

   b->cursor = nir_before_instr(&tex->instr);

   nir_def *lod_offsetUV = nir_ior(b, backend2,
                                   nir_iand_imm(b, lod, 0xFFFFF000));

   nir_src_rewrite(&tex->src[backend2_index].src, lod_offsetUV);

   return true;
}

static bool
pack_offset(nir_builder *b, nir_tex_instr *tex, bool pack_6bits_offsets)
{
   /* No offset, nothing to do */
   int offset_index = nir_tex_instr_src_index(tex, nir_tex_src_offset);
   if (offset_index < 0)
      return false;

   b->cursor = nir_before_instr(&tex->instr);

   nir_def *offset = tex->src[offset_index].src.ssa;

   /* Combine all three offsets into a single unsigned dword:
    *
    *    bits 11:8 - U Offset (X component)
    *    bits  7:4 - V Offset (Y component)
    *    bits  3:0 - R Offset (Z component)
    *
    * Or for TG4 messages with pack_6bits_offsets=true, do the bottom packing
    * of :
    *
    *    ------------------------------------------
    *    |Bits     | [31:12]  | [11:6]  | [5:0]   |
    *    ------------------------------------------
    *    |OffsetUV | LOD/Bias | OffsetV | OffsetU |
    *    ------------------------------------------
    */
   const unsigned num_components =
      nir_tex_instr_src_size(tex, offset_index);

   static const unsigned bits4_bits[] = { 4, 4, 4, };
   static const unsigned bits6_bits[] = { 6, 6, 0, };

   offset = nir_pad_vector_imm_int(b, offset, 0, num_components);
   offset = nir_format_clamp_sint(
      b, offset, pack_6bits_offsets ? bits6_bits : bits4_bits);

   static const unsigned bits4_offsets[] = { 8, 4, 0, };
   static const unsigned bits6_offsets[] = { 0, 6, 0, };
   const unsigned *comp_bits_offsets = pack_6bits_offsets ?
      bits6_offsets : bits4_offsets;
   const unsigned value_mask = pack_6bits_offsets ? 0x3f : 0xf;

   nir_def *packed_offset = NULL;
   for (unsigned c = 0; c < num_components; c++) {
      nir_def *c_shifted = nir_ishl_imm(
         b,
         nir_iand_imm(b, nir_channel(b, offset, c), value_mask),
         comp_bits_offsets[c]);
      packed_offset = packed_offset == NULL ? c_shifted : nir_ior(b, packed_offset, c_shifted);
   }

   nir_tex_instr_remove_src(tex, offset_index);
   nir_tex_instr_add_src(tex, nir_tex_src_backend2, packed_offset);

   return true;
}

static bool
intel_nir_lower_texture_instr(nir_builder *b, nir_tex_instr *tex, void *cb_data)
{
   const struct intel_device_info *devinfo = cb_data;

   const bool has_lod =
      nir_tex_instr_src_index(tex, nir_tex_src_lod) != -1 ||
      nir_tex_instr_src_index(tex, nir_tex_src_bias) != -1;
   /* On Gfx20+, when we have a LOD, we need to pack the offsets with it. When
    * there is no LOD, the offsets are lowered in the coordinates (see
    * lower_xehp_tg4_offset_filter).
    */
   const bool needs_tg4_load_bias_offset_packing =
      tex->op == nir_texop_tg4 && has_lod &&
      devinfo->ver >= 20;
   const bool needs_tg4_offset_packing = devinfo->verx10 >= 125;

   bool progress = false;

   if (tex->op != nir_texop_txf &&
       (tex->op != nir_texop_tg4 || needs_tg4_offset_packing)) {
      progress |= pack_offset(b, tex, needs_tg4_load_bias_offset_packing);
   }

   switch (tex->op) {
   case nir_texop_txl:
   case nir_texop_txb:
   case nir_texop_tg4: {
      if (tex->is_array &&
          tex->sampler_dim == GLSL_SAMPLER_DIM_CUBE &&
          devinfo->ver >= 20) {
         progress |= pack_lod_and_array_index(b, tex);
      }

      if (needs_tg4_load_bias_offset_packing)
         progress |= pack_lod_or_bias_and_offset(b, tex);

      break;
   }
   default:
      break;
   }

   return progress;
}

bool
brw_nir_lower_texture(nir_shader *shader,
                      const struct intel_device_info *devinfo)
{
   return nir_shader_tex_pass(shader,
                              intel_nir_lower_texture_instr,
                              nir_metadata_none,
                              (void *)devinfo);
}
