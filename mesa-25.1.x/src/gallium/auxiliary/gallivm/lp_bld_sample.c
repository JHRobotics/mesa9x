/**************************************************************************
 *
 * Copyright 2009 VMware, Inc.
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
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT.
 * IN NO EVENT SHALL VMWARE AND/OR ITS SUPPLIERS BE LIABLE FOR
 * ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 **************************************************************************/

/**
 * @file
 * Texture sampling -- common code.
 *
 * @author Jose Fonseca <jfonseca@vmware.com>
 */

#include "pipe/p_defines.h"
#include "pipe/p_state.h"
#include "util/format/u_format.h"
#include "util/u_math.h"
#include "util/u_cpu_detect.h"
#include "lp_bld_arit.h"
#include "lp_bld_const.h"
#include "lp_bld_debug.h"
#include "lp_bld_printf.h"
#include "lp_bld_flow.h"
#include "lp_bld_sample.h"
#include "lp_bld_swizzle.h"
#include "lp_bld_type.h"
#include "lp_bld_logic.h"
#include "lp_bld_pack.h"
#include "lp_bld_quad.h"
#include "lp_bld_bitarit.h"


/*
 * Bri-linear factor. Should be greater than one.
 */
#define BRILINEAR_FACTOR 2


/**
 * Does the given texture wrap mode allow sampling the texture border color?
 * XXX maybe move this into gallium util code.
 */
bool
lp_sampler_wrap_mode_uses_border_color(enum pipe_tex_wrap mode,
                                       enum pipe_tex_filter min_img_filter,
                                       enum pipe_tex_filter mag_img_filter)
{
   switch (mode) {
   case PIPE_TEX_WRAP_REPEAT:
   case PIPE_TEX_WRAP_CLAMP_TO_EDGE:
   case PIPE_TEX_WRAP_MIRROR_REPEAT:
   case PIPE_TEX_WRAP_MIRROR_CLAMP_TO_EDGE:
      return false;
   case PIPE_TEX_WRAP_CLAMP:
   case PIPE_TEX_WRAP_MIRROR_CLAMP:
      if (min_img_filter == PIPE_TEX_FILTER_NEAREST &&
          mag_img_filter == PIPE_TEX_FILTER_NEAREST) {
         return false;
      } else {
         return true;
      }
   case PIPE_TEX_WRAP_CLAMP_TO_BORDER:
   case PIPE_TEX_WRAP_MIRROR_CLAMP_TO_BORDER:
      return true;
   default:
      assert(0 && "unexpected wrap mode");
      return false;
   }
}


/**
 * Initialize lp_sampler_static_texture_state object with the gallium
 * texture/sampler_view state (this contains the parts which are
 * considered static).
 */
void
lp_sampler_static_texture_state(struct lp_static_texture_state *state,
                                const struct pipe_sampler_view *view)
{
   memset(state, 0, sizeof *state);

   if (!view || !view->texture)
      return;

   const struct pipe_resource *texture = view->texture;

   state->format = view->format;
   state->res_format = texture->format;
   state->swizzle_r = view->swizzle_r;
   state->swizzle_g = view->swizzle_g;
   state->swizzle_b = view->swizzle_b;
   state->swizzle_a = view->swizzle_a;
   assert(state->swizzle_r < PIPE_SWIZZLE_NONE);
   assert(state->swizzle_g < PIPE_SWIZZLE_NONE);
   assert(state->swizzle_b < PIPE_SWIZZLE_NONE);
   assert(state->swizzle_a < PIPE_SWIZZLE_NONE);

   /* check if it is a tex2d created from buf */
   if (view->is_tex2d_from_buf)
      state->target = PIPE_TEXTURE_2D;
   else
      state->target = view->target;

   state->res_target = texture->target;

   state->pot_width = util_is_power_of_two_or_zero(texture->width0);
   state->pot_height = util_is_power_of_two_or_zero(texture->height0);
   state->pot_depth = util_is_power_of_two_or_zero(texture->depth0);
   state->level_zero_only = !view->u.tex.last_level;
   state->tiled = !!(texture->flags & PIPE_RESOURCE_FLAG_SPARSE);
   if (state->tiled)
      state->tiled_samples = texture->nr_samples;

   /*
    * the layer / element / level parameters are all either dynamic
    * state or handled transparently wrt execution.
    */
}


/**
 * Initialize lp_sampler_static_texture_state object with the gallium
 * texture/sampler_view state (this contains the parts which are
 * considered static).
 */
void
lp_sampler_static_texture_state_image(struct lp_static_texture_state *state,
                                      const struct pipe_image_view *view)
{
   memset(state, 0, sizeof *state);

   if (!view || !view->resource)
      return;

   const struct pipe_resource *resource = view->resource;

   state->format = view->format;
   state->res_format = resource->format;
   state->swizzle_r = PIPE_SWIZZLE_X;
   state->swizzle_g = PIPE_SWIZZLE_Y;
   state->swizzle_b = PIPE_SWIZZLE_Z;
   state->swizzle_a = PIPE_SWIZZLE_W;
   assert(state->swizzle_r < PIPE_SWIZZLE_NONE);
   assert(state->swizzle_g < PIPE_SWIZZLE_NONE);
   assert(state->swizzle_b < PIPE_SWIZZLE_NONE);
   assert(state->swizzle_a < PIPE_SWIZZLE_NONE);

   state->target = resource->target;
   state->res_target = resource->target;
   state->pot_width = util_is_power_of_two_or_zero(resource->width0);
   state->pot_height = util_is_power_of_two_or_zero(resource->height0);
   state->pot_depth = util_is_power_of_two_or_zero(resource->depth0);
   state->level_zero_only = view->u.tex.level == 0;
   state->tiled = !!(resource->flags & PIPE_RESOURCE_FLAG_SPARSE);
   if (state->tiled) {
      state->tiled_samples = resource->nr_samples;
      if (view->u.tex.is_2d_view_of_3d)
         state->target = PIPE_TEXTURE_2D;
   }

   /*
    * the layer / element / level parameters are all either dynamic
    * state or handled transparently wrt execution.
    */
}


/**
 * Initialize lp_sampler_static_sampler_state object with the gallium sampler
 * state (this contains the parts which are considered static).
 */
void
lp_sampler_static_sampler_state(struct lp_static_sampler_state *state,
                                const struct pipe_sampler_state *sampler)
{
   memset(state, 0, sizeof *state);

   if (!sampler)
      return;

   /*
    * We don't copy sampler state over unless it is actually enabled, to avoid
    * spurious recompiles, as the sampler static state is part of the shader
    * key.
    *
    * Ideally gallium frontends or cso_cache module would make all state
    * canonical, but until that happens it's better to be safe than sorry here.
    *
    * XXX: Actually there's much more than can be done here, especially
    * regarding 1D/2D/3D/CUBE textures, wrap modes, etc.
    */

   state->wrap_s            = sampler->wrap_s;
   state->wrap_t            = sampler->wrap_t;
   state->wrap_r            = sampler->wrap_r;
   state->min_img_filter    = sampler->min_img_filter;
   state->mag_img_filter    = sampler->mag_img_filter;
   state->min_mip_filter    = sampler->min_mip_filter;
   state->seamless_cube_map = sampler->seamless_cube_map;
   state->reduction_mode    = sampler->reduction_mode;
   if (sampler->max_anisotropy > 1)
      state->aniso = sampler->max_anisotropy;

   if (sampler->max_lod > 0.0f) {
      state->max_lod_pos = 1;
   }

   if (sampler->lod_bias != 0.0f) {
      state->lod_bias_non_zero = 1;
   }

   if (state->min_mip_filter != PIPE_TEX_MIPFILTER_NONE ||
       state->min_img_filter != state->mag_img_filter) {

      /* If min_lod == max_lod we can greatly simplify mipmap selection.
       * This is a case that occurs during automatic mipmap generation.
       */
      if (sampler->min_lod == sampler->max_lod) {
         state->min_max_lod_equal = 1;
      } else {
         if (sampler->min_lod > 0.0f) {
            state->apply_min_lod = 1;
         }

         /*
          * XXX this won't do anything with the mesa state tracker which always
          * sets max_lod to not more than actually present mip maps...
          */
         if (sampler->max_lod < (PIPE_MAX_TEXTURE_LEVELS - 1)) {
            state->apply_max_lod = 1;
         }
      }
   }

   state->compare_mode      = sampler->compare_mode;
   if (sampler->compare_mode != PIPE_TEX_COMPARE_NONE) {
      state->compare_func   = sampler->compare_func;
   }

   state->normalized_coords = !sampler->unnormalized_coords;
}


/* build aniso rho value */
static LLVMValueRef
lp_build_rho_aniso(struct lp_build_sample_context *bld,
                   LLVMValueRef first_level,
                   LLVMValueRef s,
                   LLVMValueRef t,
                   struct lp_aniso_values *aniso_values)
{
   struct gallivm_state *gallivm = bld->gallivm;
   LLVMBuilderRef builder = bld->gallivm->builder;
   struct lp_build_context *coord_bld = &bld->coord_bld;
   struct lp_build_context *int_coord_bld = &bld->int_coord_bld;
   struct lp_build_context *int_size_bld = &bld->int_size_in_bld;
   struct lp_build_context *float_size_bld = &bld->float_size_in_bld;
   struct lp_build_context *rho_bld = &bld->lodf_bld;
   struct lp_build_context *rate_bld = &bld->aniso_rate_bld;
   struct lp_build_context *direction_bld = &bld->aniso_direction_bld;
   LLVMTypeRef i32t = LLVMInt32TypeInContext(bld->gallivm->context);
   LLVMValueRef index0 = LLVMConstInt(i32t, 0, 0);
   LLVMValueRef index1 = LLVMConstInt(i32t, 1, 0);
   LLVMValueRef ddx_ddy = lp_build_packed_ddx_ddy_twocoord(coord_bld, s, t);
   LLVMValueRef int_size, float_size;
   const unsigned length = coord_bld->type.length;
   const unsigned num_quads = length / 4;
   const bool rho_per_quad = rho_bld->type.length != length;

   int_size = lp_build_minify(int_size_bld, bld->int_size, first_level, true);
   float_size = lp_build_int_to_float(float_size_bld, int_size);

   static const unsigned char swizzle01[] = { /* no-op swizzle */
      0, 1,
      LP_BLD_SWIZZLE_DONTCARE, LP_BLD_SWIZZLE_DONTCARE
   };
   static const unsigned char swizzle23[] = {
      2, 3,
      LP_BLD_SWIZZLE_DONTCARE, LP_BLD_SWIZZLE_DONTCARE
   };
   LLVMValueRef ddx_ddys, ddx_ddyt, floatdim, shuffles[LP_MAX_VECTOR_LENGTH / 4];

   for (unsigned i = 0; i < num_quads; i++) {
      shuffles[i*4+0] = shuffles[i*4+1] = index0;
      shuffles[i*4+2] = shuffles[i*4+3] = index1;
   }
   floatdim = LLVMBuildShuffleVector(builder, float_size, float_size,
                                     LLVMConstVector(shuffles, length), "");
   ddx_ddy = lp_build_mul(coord_bld, ddx_ddy, floatdim);

   ddx_ddy = lp_build_mul(coord_bld, ddx_ddy, ddx_ddy);

   ddx_ddys = lp_build_swizzle_aos(coord_bld, ddx_ddy, swizzle01);
   ddx_ddyt = lp_build_swizzle_aos(coord_bld, ddx_ddy, swizzle23);

   LLVMValueRef rho_x2_rho_y2 = lp_build_add(coord_bld, ddx_ddys, ddx_ddyt);

   static const unsigned char swizzle0[] = { /* no-op swizzle */
     0, LP_BLD_SWIZZLE_DONTCARE,
     LP_BLD_SWIZZLE_DONTCARE, LP_BLD_SWIZZLE_DONTCARE
   };
   static const unsigned char swizzle1[] = {
     1, LP_BLD_SWIZZLE_DONTCARE,
     LP_BLD_SWIZZLE_DONTCARE, LP_BLD_SWIZZLE_DONTCARE
   };
   LLVMValueRef rho_x2 = lp_build_swizzle_aos(coord_bld, rho_x2_rho_y2, swizzle0);
   LLVMValueRef rho_y2 = lp_build_swizzle_aos(coord_bld, rho_x2_rho_y2, swizzle1);

   LLVMValueRef rho_max2 = lp_build_max(coord_bld, rho_x2, rho_y2);
   LLVMValueRef rho_min2 = lp_build_min(coord_bld, rho_x2, rho_y2);

   LLVMValueRef min_aniso2 = coord_bld->one;
   LLVMValueRef max_aniso2 = lp_build_const_vec(gallivm, coord_bld->type, bld->static_sampler_state->aniso * bld->static_sampler_state->aniso);
   LLVMValueRef eta2 = lp_build_clamp_nanmin(coord_bld, lp_build_div(coord_bld, rho_max2, rho_min2), min_aniso2, max_aniso2);
   LLVMValueRef N = lp_build_iceil(coord_bld, lp_build_sqrt(coord_bld, eta2));

   LLVMValueRef direction = lp_build_cmp(coord_bld, PIPE_FUNC_GREATER, rho_x2, rho_y2);

   /* If eta2 was clamped this will increase the rho_min2 value,
    * increasing the LOD value (using a lower resolution mip) so
    * that the sampling loop does not skip pixels.
    */
   rho_min2 = lp_build_div(coord_bld, rho_max2, eta2);

   if (rho_per_quad) {
      aniso_values->rate = lp_build_pack_aos_scalars(bld->gallivm, int_coord_bld->type,
         rate_bld->type, N, 0);
      aniso_values->direction = lp_build_pack_aos_scalars(bld->gallivm, int_coord_bld->type,
         direction_bld->type, direction, 0);
      return lp_build_pack_aos_scalars(bld->gallivm, coord_bld->type,
                                        rho_bld->type, rho_min2, 0);
   }

   aniso_values->rate = lp_build_swizzle_scalar_aos(rate_bld, N, 0, 4);
   aniso_values->direction = lp_build_swizzle_scalar_aos(direction_bld, direction, 0, 4);
   return lp_build_swizzle_scalar_aos(rho_bld, rho_min2, 0, 4);
}


/**
 * Generate code to compute coordinate gradient (rho).
 * \param derivs  partial derivatives of (s, t, r, q) with respect to X and Y
 *
 * The resulting rho has bld->levelf format (per quad or per element).
 */
static LLVMValueRef
lp_build_rho(struct lp_build_sample_context *bld,
             LLVMValueRef first_level,
             LLVMValueRef s,
             LLVMValueRef t,
             LLVMValueRef r,
             const struct lp_derivatives *derivs)
{
   struct gallivm_state *gallivm = bld->gallivm;
   struct lp_build_context *int_size_bld = &bld->int_size_in_bld;
   struct lp_build_context *float_size_bld = &bld->float_size_in_bld;
   struct lp_build_context *float_bld = &bld->float_bld;
   struct lp_build_context *coord_bld = &bld->coord_bld;
   struct lp_build_context *rho_bld = &bld->lodf_bld;
   const unsigned dims = bld->dims;
   LLVMValueRef ddx_ddy[2] = {NULL};
   LLVMBuilderRef builder = bld->gallivm->builder;
   LLVMTypeRef i32t = LLVMInt32TypeInContext(bld->gallivm->context);
   LLVMValueRef index0 = LLVMConstInt(i32t, 0, 0);
   LLVMValueRef index1 = LLVMConstInt(i32t, 1, 0);
   LLVMValueRef index2 = LLVMConstInt(i32t, 2, 0);
   LLVMValueRef rho_vec;
   LLVMValueRef rho;
   unsigned length = coord_bld->type.length;
   unsigned num_quads = length / 4;
   bool rho_per_quad = rho_bld->type.length != length;
   bool no_rho_opt = bld->no_rho_approx && (dims > 1);
   LLVMValueRef i32undef = LLVMGetUndef(LLVMInt32TypeInContext(gallivm->context));
   LLVMValueRef rho_xvec, rho_yvec;

   /* Note that all simplified calculations will only work for isotropic
    * filtering
    */

   /*
    * rho calcs are always per quad except for explicit derivs (excluding
    * the messy cube maps for now) when requested.
    */

   LLVMValueRef int_size =
      lp_build_minify(int_size_bld, bld->int_size, first_level, true);
   LLVMValueRef float_size = lp_build_int_to_float(float_size_bld, int_size);

   if (derivs) {
      LLVMValueRef ddmax[3] = { NULL }, ddx[3] = { NULL }, ddy[3] = { NULL };
      for (unsigned i = 0; i < dims; i++) {
         LLVMValueRef indexi = lp_build_const_int32(gallivm, i);

         LLVMValueRef floatdim =
            lp_build_extract_broadcast(gallivm, bld->float_size_in_type,
                                       coord_bld->type, float_size, indexi);

         /*
          * note that for rho_per_quad case could reduce math (at some shuffle
          * cost), but for now use same code to per-pixel lod case.
          */
         if (no_rho_opt) {
            ddx[i] = lp_build_mul(coord_bld, floatdim, derivs->ddx[i]);
            ddy[i] = lp_build_mul(coord_bld, floatdim, derivs->ddy[i]);
            ddx[i] = lp_build_mul(coord_bld, ddx[i], ddx[i]);
            ddy[i] = lp_build_mul(coord_bld, ddy[i], ddy[i]);
         } else {
            LLVMValueRef tmpx = lp_build_abs(coord_bld, derivs->ddx[i]);
            LLVMValueRef tmpy = lp_build_abs(coord_bld, derivs->ddy[i]);
            ddmax[i] = lp_build_max(coord_bld, tmpx, tmpy);
            ddmax[i] = lp_build_mul(coord_bld, floatdim, ddmax[i]);
         }
      }
      if (no_rho_opt) {
         rho_xvec = lp_build_add(coord_bld, ddx[0], ddx[1]);
         rho_yvec = lp_build_add(coord_bld, ddy[0], ddy[1]);
         if (dims > 2) {
            rho_xvec = lp_build_add(coord_bld, rho_xvec, ddx[2]);
            rho_yvec = lp_build_add(coord_bld, rho_yvec, ddy[2]);
         }
         rho = lp_build_max(coord_bld, rho_xvec, rho_yvec);
         /* skipping sqrt hence returning rho squared */
      } else {
         rho = ddmax[0];
         if (dims > 1) {
            rho = lp_build_max(coord_bld, rho, ddmax[1]);
            if (dims > 2) {
               rho = lp_build_max(coord_bld, rho, ddmax[2]);
            }
         }
      }

      LLVMValueRef rho_is_inf = lp_build_is_inf_or_nan(gallivm,
                                                       coord_bld->type, rho);
      rho = lp_build_select(coord_bld, rho_is_inf, coord_bld->zero, rho);

      if (rho_per_quad) {
         /*
          * rho_vec contains per-pixel rho, convert to scalar per quad.
          */
         rho = lp_build_pack_aos_scalars(bld->gallivm, coord_bld->type,
                                         rho_bld->type, rho, 0);
      }
   } else {
      /*
       * This looks all a bit complex, but it's not that bad
       * (the shuffle code makes it look worse than it is).
       * Still, might not be ideal for all cases.
       */
      static const unsigned char swizzle0[] = { /* no-op swizzle */
         0, LP_BLD_SWIZZLE_DONTCARE,
         LP_BLD_SWIZZLE_DONTCARE, LP_BLD_SWIZZLE_DONTCARE
      };
      static const unsigned char swizzle1[] = {
         1, LP_BLD_SWIZZLE_DONTCARE,
         LP_BLD_SWIZZLE_DONTCARE, LP_BLD_SWIZZLE_DONTCARE
      };
      static const unsigned char swizzle2[] = {
         2, LP_BLD_SWIZZLE_DONTCARE,
         LP_BLD_SWIZZLE_DONTCARE, LP_BLD_SWIZZLE_DONTCARE
      };

      if (dims < 2) {
         ddx_ddy[0] = lp_build_packed_ddx_ddy_onecoord(coord_bld, s);
      } else if (dims >= 2) {
         ddx_ddy[0] = lp_build_packed_ddx_ddy_twocoord(coord_bld, s, t);
         if (dims > 2) {
            ddx_ddy[1] = lp_build_packed_ddx_ddy_onecoord(coord_bld, r);
         }
      }

      if (no_rho_opt) {
         static const unsigned char swizzle01[] = { /* no-op swizzle */
            0, 1,
            LP_BLD_SWIZZLE_DONTCARE, LP_BLD_SWIZZLE_DONTCARE
         };
         static const unsigned char swizzle23[] = {
            2, 3,
            LP_BLD_SWIZZLE_DONTCARE, LP_BLD_SWIZZLE_DONTCARE
         };
         LLVMValueRef ddx_ddys, ddx_ddyt, floatdim;
         LLVMValueRef shuffles[LP_MAX_VECTOR_LENGTH / 4];

         for (unsigned i = 0; i < num_quads; i++) {
            shuffles[i*4+0] = shuffles[i*4+1] = index0;
            shuffles[i*4+2] = shuffles[i*4+3] = index1;
         }
         floatdim = LLVMBuildShuffleVector(builder, float_size, float_size,
                                           LLVMConstVector(shuffles, length),
                                           "");
         ddx_ddy[0] = lp_build_mul(coord_bld, ddx_ddy[0], floatdim);
         ddx_ddy[0] = lp_build_mul(coord_bld, ddx_ddy[0], ddx_ddy[0]);
         ddx_ddys = lp_build_swizzle_aos(coord_bld, ddx_ddy[0], swizzle01);
         ddx_ddyt = lp_build_swizzle_aos(coord_bld, ddx_ddy[0], swizzle23);
         rho_vec = lp_build_add(coord_bld, ddx_ddys, ddx_ddyt);

         if (dims > 2) {
            static const unsigned char swizzle02[] = {
               0, 2,
               LP_BLD_SWIZZLE_DONTCARE, LP_BLD_SWIZZLE_DONTCARE
            };
            floatdim = lp_build_extract_broadcast(gallivm, bld->float_size_in_type,
                                                  coord_bld->type, float_size, index2);
            ddx_ddy[1] = lp_build_mul(coord_bld, ddx_ddy[1], floatdim);
            ddx_ddy[1] = lp_build_mul(coord_bld, ddx_ddy[1], ddx_ddy[1]);
            ddx_ddy[1] = lp_build_swizzle_aos(coord_bld, ddx_ddy[1], swizzle02);
            rho_vec = lp_build_add(coord_bld, rho_vec, ddx_ddy[1]);
         }

         rho_xvec = lp_build_swizzle_aos(coord_bld, rho_vec, swizzle0);
         rho_yvec = lp_build_swizzle_aos(coord_bld, rho_vec, swizzle1);
         rho = lp_build_max(coord_bld, rho_xvec, rho_yvec);

         if (rho_per_quad) {
            rho = lp_build_pack_aos_scalars(bld->gallivm, coord_bld->type,
                                            rho_bld->type, rho, 0);
         } else {
            rho = lp_build_swizzle_scalar_aos(coord_bld, rho, 0, 4);
         }
         /* skipping sqrt hence returning rho squared */
      } else {
         ddx_ddy[0] = lp_build_abs(coord_bld, ddx_ddy[0]);
         if (dims > 2) {
            ddx_ddy[1] = lp_build_abs(coord_bld, ddx_ddy[1]);
         } else {
            ddx_ddy[1] = NULL; /* silence compiler warning */
         }

         if (dims < 2) {
            rho_xvec = lp_build_swizzle_aos(coord_bld, ddx_ddy[0], swizzle0);
            rho_yvec = lp_build_swizzle_aos(coord_bld, ddx_ddy[0], swizzle2);
         } else if (dims == 2) {
            static const unsigned char swizzle02[] = {
               0, 2,
               LP_BLD_SWIZZLE_DONTCARE, LP_BLD_SWIZZLE_DONTCARE
            };
            static const unsigned char swizzle13[] = {
               1, 3,
               LP_BLD_SWIZZLE_DONTCARE, LP_BLD_SWIZZLE_DONTCARE
            };
            rho_xvec = lp_build_swizzle_aos(coord_bld, ddx_ddy[0], swizzle02);
            rho_yvec = lp_build_swizzle_aos(coord_bld, ddx_ddy[0], swizzle13);
         } else {
            LLVMValueRef shuffles1[LP_MAX_VECTOR_LENGTH];
            LLVMValueRef shuffles2[LP_MAX_VECTOR_LENGTH];
            assert(dims == 3);
            for (unsigned i = 0; i < num_quads; i++) {
               shuffles1[4*i + 0] = lp_build_const_int32(gallivm, 4*i);
               shuffles1[4*i + 1] = lp_build_const_int32(gallivm, 4*i + 2);
               shuffles1[4*i + 2] = lp_build_const_int32(gallivm, length + 4*i);
               shuffles1[4*i + 3] = i32undef;
               shuffles2[4*i + 0] = lp_build_const_int32(gallivm, 4*i + 1);
               shuffles2[4*i + 1] = lp_build_const_int32(gallivm, 4*i + 3);
               shuffles2[4*i + 2] = lp_build_const_int32(gallivm, length + 4*i + 2);
               shuffles2[4*i + 3] = i32undef;
            }
            rho_xvec = LLVMBuildShuffleVector(builder, ddx_ddy[0], ddx_ddy[1],
                                              LLVMConstVector(shuffles1, length), "");
            rho_yvec = LLVMBuildShuffleVector(builder, ddx_ddy[0], ddx_ddy[1],
                                              LLVMConstVector(shuffles2, length), "");
         }

         rho_vec = lp_build_max(coord_bld, rho_xvec, rho_yvec);

         if (bld->coord_type.length > 4) {
            /* expand size to each quad */
            if (dims > 1) {
               /* could use some broadcast_vector helper for this? */
               LLVMValueRef src[LP_MAX_VECTOR_LENGTH/4];
               for (unsigned i = 0; i < num_quads; i++) {
                  src[i] = float_size;
               }
               float_size = lp_build_concat(bld->gallivm, src,
                                            float_size_bld->type, num_quads);
            } else {
               float_size = lp_build_broadcast_scalar(coord_bld, float_size);
            }
            rho_vec = lp_build_mul(coord_bld, rho_vec, float_size);

            if (dims <= 1) {
               rho = rho_vec;
            } else {
               if (dims >= 2) {
                  LLVMValueRef rho_s, rho_t, rho_r;

                  rho_s = lp_build_swizzle_aos(coord_bld, rho_vec, swizzle0);
                  rho_t = lp_build_swizzle_aos(coord_bld, rho_vec, swizzle1);

                  rho = lp_build_max(coord_bld, rho_s, rho_t);

                  if (dims >= 3) {
                     rho_r = lp_build_swizzle_aos(coord_bld, rho_vec, swizzle2);
                     rho = lp_build_max(coord_bld, rho, rho_r);
                  }
               }
            }
            if (rho_per_quad) {
               rho = lp_build_pack_aos_scalars(bld->gallivm, coord_bld->type,
                                               rho_bld->type, rho, 0);
            } else {
               rho = lp_build_swizzle_scalar_aos(coord_bld, rho, 0, 4);
            }
         } else {
            if (dims <= 1) {
               rho_vec = LLVMBuildExtractElement(builder, rho_vec, index0, "");
            }
            rho_vec = lp_build_mul(float_size_bld, rho_vec, float_size);

            if (dims <= 1) {
               rho = rho_vec;
            } else {
               if (dims >= 2) {
                  LLVMValueRef rho_s, rho_t, rho_r;

                  rho_s = LLVMBuildExtractElement(builder, rho_vec, index0, "");
                  rho_t = LLVMBuildExtractElement(builder, rho_vec, index1, "");

                  rho = lp_build_max(float_bld, rho_s, rho_t);

                  if (dims >= 3) {
                     rho_r = LLVMBuildExtractElement(builder, rho_vec, index2, "");
                     rho = lp_build_max(float_bld, rho, rho_r);
                  }
               }
            }
            if (!rho_per_quad) {
               rho = lp_build_broadcast_scalar(rho_bld, rho);
            }
         }
      }
   }

   return rho;
}


/*
 * Bri-linear lod computation
 *
 * Use a piece-wise linear approximation of log2 such that:
 * - round to nearest, for values in the neighborhood of -1, 0, 1, 2, etc.
 * - linear approximation for values in the neighborhood of 0.5, 1.5., etc,
 *   with the steepness specified in 'factor'
 * - exact result for 0.5, 1.5, etc.
 *
 *
 *   1.0 -              /----*
 *                     /
 *                    /
 *                   /
 *   0.5 -          *
 *                 /
 *                /
 *               /
 *   0.0 - *----/
 *
 *         |                 |
 *        2^0               2^1
 *
 * This is a technique also commonly used in hardware:
 * - http://ixbtlabs.com/articles2/gffx/nv40-rx800-3.html
 *
 * TODO: For correctness, this should only be applied when texture is known to
 * have regular mipmaps, i.e., mipmaps derived from the base level.
 *
 * TODO: This could be done in fixed point, where applicable.
 */
static void
lp_build_brilinear_lod(struct lp_build_context *bld,
                       LLVMValueRef lod,
                       double factor,
                       LLVMValueRef *out_lod_ipart,
                       LLVMValueRef *out_lod_fpart)
{
   LLVMValueRef lod_fpart;
   double pre_offset = (factor - 0.5)/factor - 0.5;
   double post_offset = 1 - factor;

   if (0) {
      lp_build_printf(bld->gallivm, "lod = %f\n", lod);
   }

   lod = lp_build_add(bld, lod,
                      lp_build_const_vec(bld->gallivm, bld->type, pre_offset));

   lp_build_ifloor_fract(bld, lod, out_lod_ipart, &lod_fpart);

   lod_fpart = lp_build_mad(bld, lod_fpart,
                            lp_build_const_vec(bld->gallivm, bld->type, factor),
                            lp_build_const_vec(bld->gallivm, bld->type, post_offset));

   /*
    * It's not necessary to clamp lod_fpart since:
    * - the above expression will never produce numbers greater than one.
    * - the mip filtering branch is only taken if lod_fpart is positive
    */

   *out_lod_fpart = lod_fpart;

   if (0) {
      lp_build_printf(bld->gallivm, "lod_ipart = %i\n", *out_lod_ipart);
      lp_build_printf(bld->gallivm, "lod_fpart = %f\n\n", *out_lod_fpart);
   }
}


/*
 * Combined log2 and brilinear lod computation.
 *
 * It's in all identical to calling lp_build_fast_log2() and
 * lp_build_brilinear_lod() above, but by combining we can compute the integer
 * and fractional part independently.
 */
static void
lp_build_brilinear_rho(struct lp_build_context *bld,
                       LLVMValueRef rho,
                       double factor,
                       LLVMValueRef *out_lod_ipart,
                       LLVMValueRef *out_lod_fpart)
{
   const double pre_factor = (2*factor - 0.5)/(M_SQRT2*factor);
   const double post_offset = 1 - 2*factor;

   assert(bld->type.floating);

   assert(lp_check_value(bld->type, rho));

   /*
    * The pre factor will make the intersections with the exact powers of two
    * happen precisely where we want them to be, which means that the integer
    * part will not need any post adjustments.
    */
   rho = lp_build_mul(bld, rho,
                      lp_build_const_vec(bld->gallivm, bld->type, pre_factor));

   /* ipart = ifloor(log2(rho)) */
   LLVMValueRef lod_ipart = lp_build_extract_exponent(bld, rho, 0);

   /* fpart = rho / 2**ipart */
   LLVMValueRef lod_fpart = lp_build_extract_mantissa(bld, rho);

   lod_fpart =
      lp_build_mad(bld, lod_fpart,
                   lp_build_const_vec(bld->gallivm, bld->type, factor),
                   lp_build_const_vec(bld->gallivm, bld->type, post_offset));

   /*
    * Like lp_build_brilinear_lod, it's not necessary to clamp lod_fpart since:
    * - the above expression will never produce numbers greater than one.
    * - the mip filtering branch is only taken if lod_fpart is positive
    */

   *out_lod_ipart = lod_ipart;
   *out_lod_fpart = lod_fpart;
}


/**
 * Fast implementation of iround(log2(sqrt(x))), based on
 * log2(x^n) == n*log2(x).
 *
 * Gives accurate results all the time.
 * (Could be trivially extended to handle other power-of-two roots.)
 */
static LLVMValueRef
lp_build_ilog2_sqrt(struct lp_build_context *bld,
                    LLVMValueRef x)
{
   LLVMBuilderRef builder = bld->gallivm->builder;
   struct lp_type i_type = lp_int_type(bld->type);
   LLVMValueRef one = lp_build_const_int_vec(bld->gallivm, i_type, 1);

   assert(bld->type.floating);

   assert(lp_check_value(bld->type, x));

   /* ipart = log2(x) + 0.5 = 0.5*(log2(x^2) + 1.0) */
   LLVMValueRef ipart = lp_build_extract_exponent(bld, x, 1);
   ipart = LLVMBuildAShr(builder, ipart, one, "");

   return ipart;
}


/**
 * Generate code to compute texture level of detail (lambda).
 * \param derivs  partial derivatives of (s, t, r, q) with respect to X and Y
 * \param lod_bias  optional float vector with the shader lod bias
 * \param explicit_lod  optional float vector with the explicit lod
 * \param out_lod_ipart  integer part of lod
 * \param out_lod_fpart  float part of lod (never larger than 1 but may be negative)
 * \param out_lod_positive  (mask) if lod is positive (i.e. texture is minified)
 * \param out_aniso_values  aniso sampling values
 *
 * The resulting lod can be scalar per quad or be per element.
 */
void
lp_build_lod_selector(struct lp_build_sample_context *bld,
                      bool is_lodq,
                      unsigned sampler_unit,
                      LLVMValueRef first_level,
                      LLVMValueRef s,
                      LLVMValueRef t,
                      LLVMValueRef r,
                      const struct lp_derivatives *derivs,
                      LLVMValueRef lod_bias, /* optional */
                      LLVMValueRef explicit_lod, /* optional */
                      enum pipe_tex_mipfilter mip_filter,
                      LLVMValueRef *out_lod,
                      LLVMValueRef *out_lod_ipart,
                      LLVMValueRef *out_lod_fpart,
                      LLVMValueRef *out_lod_positive,
                      struct lp_aniso_values *out_aniso_values)

{
   LLVMBuilderRef builder = bld->gallivm->builder;
   struct lp_sampler_dynamic_state *dynamic_state = bld->dynamic_state;
   struct lp_build_context *lodf_bld = &bld->lodf_bld;
   LLVMValueRef lod;

   *out_lod_ipart = bld->lodi_bld.zero;
   *out_lod_positive = bld->lodi_bld.zero;
   *out_lod_fpart = lodf_bld->zero;
   out_aniso_values->rate = bld->aniso_rate_bld.one;
   out_aniso_values->direction = bld->aniso_direction_bld.zero;

   /*
    * For determining min/mag, we follow GL 4.1 spec, 3.9.12 Texture
    * Magnification: "Implementations may either unconditionally assume c = 0
    * for the minification vs. magnification switch-over point, or may choose
    * to make c depend on the combination of minification and magnification
    * modes as follows: if the magnification filter is given by LINEAR and the
    * minification filter is given by NEAREST_MIPMAP_NEAREST or
    * NEAREST_MIPMAP_LINEAR, then c = 0.5. This is done to ensure that a
    * minified texture does not appear "sharper" than a magnified
    * texture. Otherwise c = 0."  And 3.9.11 Texture Minification: "If lod is
    * less than or equal to the constant c (see section 3.9.12) the texture is
    * said to be magnified; if it is greater, the texture is minified."  So,
    * using 0 as switchover point always, and using magnification for lod ==
    * 0.  Note that the always c = 0 behavior is new (first appearing in GL
    * 3.1 spec), old GL versions required 0.5 for the modes listed above.  I
    * have no clue about the (undocumented) wishes of d3d9/d3d10 here!
    */

   LLVMValueRef rho = NULL;
   bool rho_squared;

   /* When anisotropic filtering is enabled, we always compute rho,
    * since it's used to derive the anisotropic sampling rate.
    */
   if (bld->static_sampler_state->aniso) {
      rho = lp_build_rho_aniso(bld, first_level, s, t, out_aniso_values);
      rho_squared = true;
   }

   if (bld->static_sampler_state->min_max_lod_equal && !is_lodq) {
      /* User is forcing sampling from a particular mipmap level.
       * This is hit during mipmap generation.
       */
      LLVMValueRef min_lod =
         dynamic_state->min_lod(bld->gallivm, bld->resources_type,
                                bld->resources_ptr, sampler_unit);

      lod = lp_build_broadcast_scalar(lodf_bld, min_lod);
   } else {
      if (explicit_lod) {
         if (bld->num_lods != bld->coord_type.length) {
            lod = lp_build_pack_aos_scalars(bld->gallivm, bld->coord_bld.type,
                                            lodf_bld->type, explicit_lod, 0);
         } else {
            lod = explicit_lod;
         }
      } else {
         if (!rho) {
            rho = lp_build_rho(bld, first_level, s, t, r, derivs);
            rho_squared = bld->no_rho_approx && (bld->dims > 1);
         }

         /*
          * Compute lod = log2(rho)
          */

         if (!lod_bias && !is_lodq &&
             !bld->static_sampler_state->lod_bias_non_zero &&
             !bld->static_sampler_state->apply_max_lod &&
             !bld->static_sampler_state->apply_min_lod) {
            /*
             * Special case when there are no post-log2 adjustments, which
             * saves instructions but keeping the integer and fractional lod
             * computations separate from the start.
             */

            if (mip_filter == PIPE_TEX_MIPFILTER_NONE ||
                mip_filter == PIPE_TEX_MIPFILTER_NEAREST) {
               /*
                * Don't actually need both values all the time, lod_ipart is
                * needed for nearest mipfilter, lod_positive if min != mag.
                */
               if (rho_squared) {
                  *out_lod_ipart = lp_build_ilog2_sqrt(lodf_bld, rho);
               } else {
                  *out_lod_ipart = lp_build_ilog2(lodf_bld, rho);
               }
               *out_lod_positive = lp_build_cmp(lodf_bld, PIPE_FUNC_GREATER,
                                                rho, lodf_bld->one);
               return;
            }
            if (mip_filter == PIPE_TEX_MIPFILTER_LINEAR &&
                !bld->no_brilinear && !rho_squared) {
               /*
                * This can't work if rho is squared. Not sure if it could be
                * fixed while keeping it worthwile, could also do sqrt here
                * but brilinear and no_rho_opt seems like a combination not
                * making much sense anyway so just use ordinary path below.
                */
               lp_build_brilinear_rho(lodf_bld, rho, BRILINEAR_FACTOR,
                                      out_lod_ipart, out_lod_fpart);
               *out_lod_positive = lp_build_cmp(lodf_bld, PIPE_FUNC_GREATER,
                                                rho, lodf_bld->one);
               return;
            }
         }

         /* get more accurate results if we just sqaure rho always */
         if (!rho_squared)
            rho = lp_build_mul(lodf_bld, rho, rho);

         if (is_lodq)
            lod = lp_build_log2(lodf_bld, rho);
         else
            lod = lp_build_fast_log2(lodf_bld, rho);

         /* log2(x^2) == 0.5*log2(x) */
         lod = lp_build_mul(lodf_bld, lod,
                            lp_build_const_vec(bld->gallivm,
                                               lodf_bld->type, 0.5F));

         /* add shader lod bias */
         if (lod_bias) {
            if (bld->num_lods != bld->coord_type.length)
               lod_bias = lp_build_pack_aos_scalars(bld->gallivm,
                                                    bld->coord_bld.type,
                                                    lodf_bld->type,
                                                    lod_bias, 0);
            lod = LLVMBuildFAdd(builder, lod, lod_bias, "shader_lod_bias");
         }
      }

      /* add sampler lod bias */
      if (bld->static_sampler_state->lod_bias_non_zero) {
         LLVMValueRef sampler_lod_bias =
            dynamic_state->lod_bias(bld->gallivm, bld->resources_type,
                                    bld->resources_ptr, sampler_unit);
         sampler_lod_bias = lp_build_broadcast_scalar(lodf_bld,
                                                      sampler_lod_bias);
         lod = LLVMBuildFAdd(builder, lod, sampler_lod_bias, "sampler_lod_bias");
      }

      if (is_lodq) {
         *out_lod = lod;
      }

      /* clamp lod */
      if (bld->static_sampler_state->apply_max_lod) {
         LLVMValueRef max_lod =
            dynamic_state->max_lod(bld->gallivm, bld->resources_type,
                                   bld->resources_ptr, sampler_unit);
         max_lod = lp_build_broadcast_scalar(lodf_bld, max_lod);

         lod = lp_build_min(lodf_bld, lod, max_lod);
      }
      if (bld->static_sampler_state->apply_min_lod) {
         LLVMValueRef min_lod =
            dynamic_state->min_lod(bld->gallivm, bld->resources_type,
                                   bld->resources_ptr, sampler_unit);
         min_lod = lp_build_broadcast_scalar(lodf_bld, min_lod);

         lod = lp_build_max(lodf_bld, lod, min_lod);
      }

      if (is_lodq) {
         *out_lod_fpart = lod;
         return;
      }
   }

   *out_lod_positive = lp_build_cmp(lodf_bld, PIPE_FUNC_GREATER,
                                    lod, lodf_bld->zero);

   if (mip_filter == PIPE_TEX_MIPFILTER_LINEAR) {
      if (!bld->no_brilinear) {
         lp_build_brilinear_lod(lodf_bld, lod, BRILINEAR_FACTOR,
                                out_lod_ipart, out_lod_fpart);
      } else {
         lp_build_ifloor_fract(lodf_bld, lod, out_lod_ipart, out_lod_fpart);
      }

      lp_build_name(*out_lod_fpart, "lod_fpart");
   } else {
      *out_lod_ipart = lp_build_iround(lodf_bld, lod);
   }

   lp_build_name(*out_lod_ipart, "lod_ipart");

   return;
}


/**
 * For PIPE_TEX_MIPFILTER_NEAREST, convert int part of lod
 * to actual mip level.
 * Note: this is all scalar per quad code.
 * \param lod_ipart  int texture level of detail
 * \param level_out  returns integer
 * \param out_of_bounds returns per coord out_of_bounds mask if provided
 */
void
lp_build_nearest_mip_level(struct lp_build_sample_context *bld,
                           LLVMValueRef first_level,
                           LLVMValueRef last_level,
                           LLVMValueRef lod_ipart,
                           LLVMValueRef *level_out,
                           LLVMValueRef *out_of_bounds)
{
   struct lp_build_context *leveli_bld = &bld->leveli_bld;
   LLVMValueRef level = lp_build_add(leveli_bld, lod_ipart, first_level);

   if (out_of_bounds) {
      LLVMValueRef out, out1;
      out = lp_build_cmp(leveli_bld, PIPE_FUNC_LESS, level, first_level);
      out1 = lp_build_cmp(leveli_bld, PIPE_FUNC_GREATER, level, last_level);
      out = lp_build_or(leveli_bld, out, out1);
      if (bld->num_mips == bld->coord_bld.type.length) {
         *out_of_bounds = out;
      } else if (bld->num_mips == 1) {
         *out_of_bounds = lp_build_broadcast_scalar(&bld->int_coord_bld, out);
      } else {
         assert(bld->num_mips == bld->coord_bld.type.length / 4);
         *out_of_bounds =
            lp_build_unpack_broadcast_aos_scalars(bld->gallivm,
                                                  leveli_bld->type,
                                                  bld->int_coord_bld.type,
                                                  out);
      }
      level = lp_build_andnot(&bld->int_coord_bld, level, *out_of_bounds);
      *level_out = level;
   } else {
      /* clamp level to legal range of levels */
      *level_out = lp_build_clamp(leveli_bld, level, first_level, last_level);

   }
}


/**
 * For PIPE_TEX_MIPFILTER_LINEAR, convert per-quad (or per element) int LOD(s)
 * to two (per-quad) (adjacent) mipmap level indexes, and fix up float lod
 * part accordingly.
 * Later, we'll sample from those two mipmap levels and interpolate between
 * them.
 */
void
lp_build_linear_mip_levels(struct lp_build_sample_context *bld,
                           unsigned texture_unit,
                           LLVMValueRef first_level,
                           LLVMValueRef last_level,
                           LLVMValueRef lod_ipart,
                           LLVMValueRef *lod_fpart_inout,
                           LLVMValueRef *level0_out,
                           LLVMValueRef *level1_out)
{
   LLVMBuilderRef builder = bld->gallivm->builder;
   struct lp_build_context *leveli_bld = &bld->leveli_bld;
   struct lp_build_context *levelf_bld = &bld->levelf_bld;
   LLVMValueRef clamp_min;
   LLVMValueRef clamp_max;

   assert(bld->num_lods == bld->num_mips);

   *level0_out = lp_build_add(leveli_bld, lod_ipart, first_level);
   *level1_out = lp_build_add(leveli_bld, *level0_out, leveli_bld->one);

   /*
    * Clamp both *level0_out and *level1_out to [first_level, last_level],
    * with the minimum number of comparisons, and zeroing lod_fpart in the
    * extreme ends in the process.
    */

   /* *level0_out < first_level */
   clamp_min = LLVMBuildICmp(builder, LLVMIntSLT,
                             *level0_out, first_level,
                             "clamp_lod_to_first");

   *level0_out = LLVMBuildSelect(builder, clamp_min,
                                 first_level, *level0_out, "");

   *level1_out = LLVMBuildSelect(builder, clamp_min,
                                 first_level, *level1_out, "");

   *lod_fpart_inout = LLVMBuildSelect(builder, clamp_min,
                                      levelf_bld->zero, *lod_fpart_inout, "");

   /* *level0_out >= last_level */
   clamp_max = LLVMBuildICmp(builder, LLVMIntSGE,
                             *level0_out, last_level,
                             "clamp_lod_to_last");

   *level0_out = LLVMBuildSelect(builder, clamp_max,
                                 last_level, *level0_out, "");

   *level1_out = LLVMBuildSelect(builder, clamp_max,
                                 last_level, *level1_out, "");

   *lod_fpart_inout = LLVMBuildSelect(builder, clamp_max,
                                      levelf_bld->zero, *lod_fpart_inout, "");

   lp_build_name(*level0_out, "texture%u_miplevel0", texture_unit);
   lp_build_name(*level1_out, "texture%u_miplevel1", texture_unit);
   lp_build_name(*lod_fpart_inout, "texture%u_mipweight", texture_unit);
}


/**
 * A helper function that factorizes this common pattern.
 */
LLVMValueRef
lp_sample_load_mip_value(struct gallivm_state *gallivm,
                         LLVMTypeRef ptr_type,
                         LLVMValueRef offsets,
                         LLVMValueRef index1)
{
   LLVMValueRef zero = lp_build_const_int32(gallivm, 0);
   LLVMValueRef indexes[2] = {zero, index1};
   LLVMValueRef ptr = LLVMBuildGEP2(gallivm->builder, ptr_type, offsets,
                                    indexes, ARRAY_SIZE(indexes), "");
   return LLVMBuildLoad2(gallivm->builder,
                         LLVMInt32TypeInContext(gallivm->context), ptr, "");
}


/**
 * Return pointer to a single mipmap level.
 * \param level  integer mipmap level
 */
LLVMValueRef
lp_build_get_mipmap_level(struct lp_build_sample_context *bld,
                          LLVMValueRef level)
{
   LLVMValueRef mip_offset = lp_sample_load_mip_value(bld->gallivm, bld->mip_offsets_type,
                                                      bld->mip_offsets, level);
   LLVMBuilderRef builder = bld->gallivm->builder;
   LLVMValueRef data_ptr =
      LLVMBuildGEP2(builder,
                    LLVMInt8TypeInContext(bld->gallivm->context),
                    bld->base_ptr, &mip_offset, 1, "");
   return data_ptr;
}


/**
 * Return (per-pixel) offsets to mip levels.
 * \param level  integer mipmap level
 */
LLVMValueRef
lp_build_get_mip_offsets(struct lp_build_sample_context *bld,
                         LLVMValueRef level)
{
   LLVMBuilderRef builder = bld->gallivm->builder;
   LLVMValueRef offsets, offset1;

   if (bld->num_mips == 1) {
      offset1 = lp_sample_load_mip_value(bld->gallivm, bld->mip_offsets_type, bld->mip_offsets, level);
      offsets = lp_build_broadcast_scalar(&bld->int_coord_bld, offset1);
   } else if (bld->num_mips == bld->coord_bld.type.length / 4) {
      offsets = bld->int_coord_bld.undef;
      for (unsigned i = 0; i < bld->num_mips; i++) {
         LLVMValueRef indexi = lp_build_const_int32(bld->gallivm, i);
         offset1 = lp_sample_load_mip_value(bld->gallivm, bld->mip_offsets_type,
                                            bld->mip_offsets,
                                            LLVMBuildExtractElement(builder, level,
                                                                    indexi, ""));
         LLVMValueRef indexo = lp_build_const_int32(bld->gallivm, 4 * i);
         offsets = LLVMBuildInsertElement(builder, offsets, offset1,
                                          indexo, "");
      }
      offsets = lp_build_swizzle_scalar_aos(&bld->int_coord_bld,
                                            offsets, 0, 4);
   } else {
      assert (bld->num_mips == bld->coord_bld.type.length);

      offsets = bld->int_coord_bld.undef;
      for (unsigned i = 0; i < bld->num_mips; i++) {
         LLVMValueRef indexi = lp_build_const_int32(bld->gallivm, i);
         offset1 = lp_sample_load_mip_value(bld->gallivm, bld->mip_offsets_type,
                                            bld->mip_offsets,
                                            LLVMBuildExtractElement(builder, level,
                                                                    indexi, ""));
         offsets = LLVMBuildInsertElement(builder, offsets, offset1,
                                          indexi, "");
      }
   }
   return offsets;
}


/**
 * Codegen equivalent for u_minify().
 * @param lod_scalar  if lod is a (broadcasted) scalar
 * Return max(1, base_size >> level);
 */
LLVMValueRef
lp_build_minify(struct lp_build_context *bld,
                LLVMValueRef base_size,
                LLVMValueRef level,
                bool lod_scalar)
{
   LLVMBuilderRef builder = bld->gallivm->builder;
   assert(lp_check_value(bld->type, base_size));
   assert(lp_check_value(bld->type, level));

   if (level == bld->zero) {
      /* if we're using mipmap level zero, no minification is needed */
      return base_size;
   } else {
      LLVMValueRef size;
      assert(bld->type.sign);
      if (lod_scalar ||
         (util_get_cpu_caps()->has_avx2 || !util_get_cpu_caps()->has_sse)) {
         size = LLVMBuildLShr(builder, base_size, level, "minify");
         size = lp_build_max(bld, size, bld->one);
      } else {
         /*
          * emulate shift with float mul, since intel "forgot" shifts with
          * per-element shift count until avx2, which results in terrible
          * scalar extraction (both count and value), scalar shift,
          * vector reinsertion. Should not be an issue on any non-x86 cpu
          * with a vector instruction set.
          * On cpus with AMD's XOP this should also be unnecessary but I'm
          * not sure if llvm would emit this with current flags.
          */
         LLVMValueRef const127, const23, lf;
         struct lp_type ftype;
         struct lp_build_context fbld;
         ftype = lp_type_float_vec(32, bld->type.length * bld->type.width);
         lp_build_context_init(&fbld, bld->gallivm, ftype);
         const127 = lp_build_const_int_vec(bld->gallivm, bld->type, 127);
         const23 = lp_build_const_int_vec(bld->gallivm, bld->type, 23);

         /* calculate 2^(-level) float */
         lf = lp_build_sub(bld, const127, level);
         lf = lp_build_shl(bld, lf, const23);
         lf = LLVMBuildBitCast(builder, lf, fbld.vec_type, "");

         /* finish shift operation by doing float mul */
         base_size = lp_build_int_to_float(&fbld, base_size);
         size = lp_build_mul(&fbld, base_size, lf);
         /*
          * do the max also with floats because
          * a) non-emulated int max requires sse41
          *    (this is actually a lie as we could cast to 16bit values
          *    as 16bit is sufficient and 16bit int max is sse2)
          * b) with avx we can do int max 4-wide but float max 8-wide
          */
         size = lp_build_max(&fbld, size, fbld.one);
         size = lp_build_itrunc(&fbld, size);
      }
      return size;
   }
}


/*
 * Scale image dimensions with block sizes.
 *
 * tex_blocksize is the resource format blocksize
 * view_blocksize is the view format blocksize
 *
 * This must be applied post-minification, but
 * only when blocksizes are different.
 *
 * ret = (size + (tex_blocksize - 1)) >> log2(tex_blocksize);
 * ret *= blocksize;
 */
LLVMValueRef
lp_build_scale_view_dims(struct lp_build_context *bld, LLVMValueRef size,
                         LLVMValueRef tex_blocksize,
                         LLVMValueRef tex_blocksize_log2,
                         LLVMValueRef view_blocksize)
{
   LLVMBuilderRef builder = bld->gallivm->builder;
   LLVMValueRef ret =
      LLVMBuildAdd(builder, size,
                   LLVMBuildSub(builder, tex_blocksize,
                                lp_build_const_int_vec(bld->gallivm,
                                                       bld->type, 1), ""),
                   "");
   ret = LLVMBuildLShr(builder, ret, tex_blocksize_log2, "");
   ret = LLVMBuildMul(builder, ret, view_blocksize, "");
   return ret;
}


/*
 * Scale a single image dimension.
 *
 * Scale one image between resource and view blocksizes.
 * noop if sizes are the same.
 */
LLVMValueRef
lp_build_scale_view_dim(struct gallivm_state *gallivm, LLVMValueRef size,
                        unsigned tex_blocksize, unsigned view_blocksize)
{
   if (tex_blocksize == view_blocksize)
      return size;

   LLVMBuilderRef builder = gallivm->builder;
   LLVMValueRef ret =
      LLVMBuildAdd(builder, size,
                   lp_build_const_int32(gallivm, tex_blocksize - 1), "");
   ret = LLVMBuildLShr(builder, ret,
                       lp_build_const_int32(gallivm,
                                            util_logbase2(tex_blocksize)), "");
   ret = LLVMBuildMul(builder, ret,
                      lp_build_const_int32(gallivm, view_blocksize), "");
   return ret;
}


/**
 * Dereference stride_array[mipmap_level] array to get a stride.
 * Return stride as a vector.
 */
static LLVMValueRef
lp_build_get_level_stride_vec(struct lp_build_sample_context *bld,
                              LLVMTypeRef stride_type,
                              LLVMValueRef stride_array, LLVMValueRef level)
{
   LLVMBuilderRef builder = bld->gallivm->builder;
   LLVMValueRef stride, stride1;

   if (bld->num_mips == 1) {
      stride1 = lp_sample_load_mip_value(bld->gallivm, stride_type, stride_array, level);
      stride = lp_build_broadcast_scalar(&bld->int_coord_bld, stride1);
   } else if (bld->num_mips == bld->coord_bld.type.length / 4) {
      LLVMValueRef stride1;

      stride = bld->int_coord_bld.undef;
      for (unsigned i = 0; i < bld->num_mips; i++) {
         LLVMValueRef indexi = lp_build_const_int32(bld->gallivm, i);
         stride1 = lp_sample_load_mip_value(bld->gallivm, stride_type, stride_array,
                                            LLVMBuildExtractElement(builder, level,
                                                                    indexi, ""));
         LLVMValueRef indexo = lp_build_const_int32(bld->gallivm, 4 * i);
         stride = LLVMBuildInsertElement(builder, stride, stride1, indexo, "");
      }
      stride = lp_build_swizzle_scalar_aos(&bld->int_coord_bld, stride, 0, 4);
   } else {
      LLVMValueRef stride1;

      assert (bld->num_mips == bld->coord_bld.type.length);

      stride = bld->int_coord_bld.undef;
      for (unsigned i = 0; i < bld->coord_bld.type.length; i++) {
         LLVMValueRef indexi = lp_build_const_int32(bld->gallivm, i);
         stride1 = lp_sample_load_mip_value(bld->gallivm, stride_type, stride_array,
                                            LLVMBuildExtractElement(builder, level,
                                                                    indexi, ""));
         stride = LLVMBuildInsertElement(builder, stride, stride1, indexi, "");
      }
   }
   return stride;
}


/**
 * When sampling a mipmap, we need to compute the width, height, depth
 * of the source levels from the level indexes.  This helper function
 * does that.
 */
void
lp_build_mipmap_level_sizes(struct lp_build_sample_context *bld,
                            LLVMValueRef ilevel,
                            LLVMValueRef *out_size,
                            LLVMValueRef *row_stride_vec,
                            LLVMValueRef *img_stride_vec)
{
   const unsigned dims = bld->dims;
   LLVMValueRef ilevel_vec;

   /*
    * Compute width, height, depth at mipmap level 'ilevel'
    */
   if (bld->num_mips == 1) {
      ilevel_vec = lp_build_broadcast_scalar(&bld->int_size_bld, ilevel);
      *out_size = lp_build_minify(&bld->int_size_bld, bld->int_size,
                                  ilevel_vec, true);
      *out_size = lp_build_scale_view_dims(&bld->int_size_bld, *out_size,
                                           bld->int_tex_blocksize,
                                           bld->int_tex_blocksize_log2,
                                           bld->int_view_blocksize);
   } else {
      LLVMValueRef int_size_vec;
      LLVMValueRef int_tex_blocksize_vec, int_tex_blocksize_log2_vec;
      LLVMValueRef int_view_blocksize_vec;
      LLVMValueRef tmp[LP_MAX_VECTOR_LENGTH];
      const unsigned num_quads = bld->coord_bld.type.length / 4;

      if (bld->num_mips == num_quads) {
         /*
          * XXX: this should be #ifndef SANE_INSTRUCTION_SET.
          * intel "forgot" the variable shift count instruction until avx2.
          * A harmless 8x32 shift gets translated into 32 instructions
          * (16 extracts, 8 scalar shifts, 8 inserts), llvm is apparently
          * unable to recognize if there are really just 2 different shift
          * count values. So do the shift 4-wide before expansion.
          */
         struct lp_build_context bld4;
         struct lp_type type4;

         type4 = bld->int_coord_bld.type;
         type4.length = 4;

         lp_build_context_init(&bld4, bld->gallivm, type4);

         if (bld->dims == 1) {
            assert(bld->int_size_in_bld.type.length == 1);
            int_size_vec = lp_build_broadcast_scalar(&bld4,
                                                     bld->int_size);
            int_tex_blocksize_vec =
               lp_build_broadcast_scalar(&bld4, bld->int_tex_blocksize);
            int_tex_blocksize_log2_vec =
               lp_build_broadcast_scalar(&bld4, bld->int_tex_blocksize_log2);
            int_view_blocksize_vec =
               lp_build_broadcast_scalar(&bld4, bld->int_view_blocksize);
         } else {
            assert(bld->int_size_in_bld.type.length == 4);
            int_size_vec = bld->int_size;
            int_tex_blocksize_vec = bld->int_tex_blocksize;
            int_tex_blocksize_log2_vec = bld->int_tex_blocksize_log2;
            int_view_blocksize_vec = bld->int_view_blocksize;
         }

         for (unsigned i = 0; i < num_quads; i++) {
            LLVMValueRef ileveli;
            LLVMValueRef indexi = lp_build_const_int32(bld->gallivm, i);

            ileveli = lp_build_extract_broadcast(bld->gallivm,
                                                 bld->leveli_bld.type,
                                                 bld4.type,
                                                 ilevel,
                                                 indexi);
            tmp[i] = lp_build_minify(&bld4, int_size_vec, ileveli, true);
            tmp[i] = lp_build_scale_view_dims(&bld4, tmp[i],
                                              int_tex_blocksize_vec,
                                              int_tex_blocksize_log2_vec,
                                              int_view_blocksize_vec);
         }
         /*
          * out_size is [w0, h0, d0, _, w1, h1, d1, _, ...] vector for
          * dims > 1, [w0, w0, w0, w0, w1, w1, w1, w1, ...] otherwise.
          */
         *out_size = lp_build_concat(bld->gallivm,
                                     tmp,
                                     bld4.type,
                                     num_quads);
      } else {
         /* FIXME: this is terrible and results in _huge_ vector
          * (for the dims > 1 case).
          * Should refactor this (together with extract_image_sizes) and do
          * something more useful. Could for instance if we have width,height
          * with 4-wide vector pack all elements into a 8xi16 vector
          * (on which we can still do useful math) instead of using a 16xi32
          * vector.
          * For dims == 1 this will create [w0, w1, w2, w3, ...] vector.
          * For dims > 1 this will create [w0, h0, d0, _, w1, h1, d1, _, ...]
          * vector.
          */
         assert(bld->num_mips == bld->coord_bld.type.length);
         if (bld->dims == 1) {
            assert(bld->int_size_in_bld.type.length == 1);
            int_size_vec = lp_build_broadcast_scalar(&bld->int_coord_bld,
                                                     bld->int_size);
            int_tex_blocksize_vec =
               lp_build_broadcast_scalar(&bld->int_coord_bld,
                                         bld->int_tex_blocksize);
            int_tex_blocksize_log2_vec =
               lp_build_broadcast_scalar(&bld->int_coord_bld,
                                         bld->int_tex_blocksize_log2);
            int_view_blocksize_vec =
               lp_build_broadcast_scalar(&bld->int_coord_bld,
                                         bld->int_view_blocksize);
            *out_size = lp_build_minify(&bld->int_coord_bld, int_size_vec,
                                        ilevel, false);
            *out_size = lp_build_scale_view_dims(&bld->int_coord_bld,
                                                 *out_size,
                                                 int_tex_blocksize_vec,
                                                 int_tex_blocksize_log2_vec,
                                                 int_view_blocksize_vec);
         } else {
            LLVMValueRef ilevel1;
            for (unsigned i = 0; i < bld->num_mips; i++) {
               LLVMValueRef indexi = lp_build_const_int32(bld->gallivm, i);
               ilevel1 = lp_build_extract_broadcast(bld->gallivm,
                                                    bld->int_coord_type,
                                                    bld->int_size_in_bld.type,
                                                    ilevel, indexi);
               tmp[i] = bld->int_size;
               tmp[i] = lp_build_minify(&bld->int_size_in_bld, tmp[i],
                                        ilevel1, true);
               tmp[i] = lp_build_scale_view_dims(&bld->int_size_in_bld,
                                                 tmp[i],
                                                 bld->int_tex_blocksize,
                                                 bld->int_tex_blocksize_log2,
                                                 bld->int_view_blocksize);
            }
            *out_size = lp_build_concat(bld->gallivm, tmp,
                                        bld->int_size_in_bld.type,
                                        bld->num_mips);
         }
      }
   }

   if (dims >= 2) {
      *row_stride_vec = lp_build_get_level_stride_vec(bld,
                                                      bld->row_stride_type,
                                                      bld->row_stride_array,
                                                      ilevel);
   }
   if (dims == 3 || has_layer_coord(bld->static_texture_state->target)) {
      *img_stride_vec = lp_build_get_level_stride_vec(bld,
                                                      bld->img_stride_type,
                                                      bld->img_stride_array,
                                                      ilevel);
   }
}


/**
 * Extract and broadcast texture size.
 *
 * @param size_type   type of the texture size vector (either
 *                    bld->int_size_type or bld->float_size_type)
 * @param coord_type  type of the texture size vector (either
 *                    bld->int_coord_type or bld->coord_type)
 * @param size        vector with the texture size (width, height, depth)
 */
void
lp_build_extract_image_sizes(struct lp_build_sample_context *bld,
                             struct lp_build_context *size_bld,
                             struct lp_type coord_type,
                             LLVMValueRef size,
                             LLVMValueRef *out_width,
                             LLVMValueRef *out_height,
                             LLVMValueRef *out_depth)
{
   const unsigned dims = bld->dims;
   LLVMTypeRef i32t = LLVMInt32TypeInContext(bld->gallivm->context);
   struct lp_type size_type = size_bld->type;

   if (bld->num_mips == 1) {
      *out_width = lp_build_extract_broadcast(bld->gallivm,
                                              size_type,
                                              coord_type,
                                              size,
                                              LLVMConstInt(i32t, 0, 0));
      if (dims >= 2) {
         *out_height = lp_build_extract_broadcast(bld->gallivm,
                                                  size_type,
                                                  coord_type,
                                                  size,
                                                  LLVMConstInt(i32t, 1, 0));
         if (dims == 3) {
            *out_depth = lp_build_extract_broadcast(bld->gallivm,
                                                    size_type,
                                                    coord_type,
                                                    size,
                                                    LLVMConstInt(i32t, 2, 0));
         }
      }
   } else {
      unsigned num_quads = bld->coord_bld.type.length / 4;

      if (dims == 1) {
         *out_width = size;
      } else if (bld->num_mips == num_quads) {
         *out_width = lp_build_swizzle_scalar_aos(size_bld, size, 0, 4);
         if (dims >= 2) {
            *out_height = lp_build_swizzle_scalar_aos(size_bld, size, 1, 4);
            if (dims == 3) {
               *out_depth = lp_build_swizzle_scalar_aos(size_bld, size, 2, 4);
            }
         }
      } else {
         assert(bld->num_mips == bld->coord_type.length);
         *out_width = lp_build_pack_aos_scalars(bld->gallivm, size_type,
                                                coord_type, size, 0);
         if (dims >= 2) {
            *out_height = lp_build_pack_aos_scalars(bld->gallivm, size_type,
                                                    coord_type, size, 1);
            if (dims == 3) {
               *out_depth = lp_build_pack_aos_scalars(bld->gallivm, size_type,
                                                      coord_type, size, 2);
            }
         }
      }
   }
}


/**
 * Unnormalize coords.
 *
 * @param flt_size  vector with the integer texture size (width, height, depth)
 */
void
lp_build_unnormalized_coords(struct lp_build_sample_context *bld,
                             LLVMValueRef flt_size,
                             LLVMValueRef *s,
                             LLVMValueRef *t,
                             LLVMValueRef *r)
{
   const unsigned dims = bld->dims;
   LLVMValueRef width;
   LLVMValueRef height = NULL;
   LLVMValueRef depth = NULL;

   lp_build_extract_image_sizes(bld,
                                &bld->float_size_bld,
                                bld->coord_type,
                                flt_size,
                                &width,
                                &height,
                                &depth);

   /* s = s * width, t = t * height */
   *s = lp_build_mul(&bld->coord_bld, *s, width);
   if (dims >= 2) {
      *t = lp_build_mul(&bld->coord_bld, *t, height);
      if (dims >= 3) {
         *r = lp_build_mul(&bld->coord_bld, *r, depth);
      }
   }
}


/**
 * Generate new coords and faces for cubemap texels falling off the face.
 *
 * @param face   face (center) of the pixel
 * @param x0     lower x coord
 * @param x1     higher x coord (must be x0 + 1)
 * @param y0     lower y coord
 * @param y1     higher y coord (must be x0 + 1)
 * @param max_coord     texture cube (level) size - 1
 * @param next_faces    new face values when falling off
 * @param next_xcoords  new x coord values when falling off
 * @param next_ycoords  new y coord values when falling off
 *
 * The arrays hold the new values when under/overflow of
 * lower x, higher x, lower y, higher y coord would occur (in this order).
 * next_xcoords/next_ycoords have two entries each (for both new lower and
 * higher coord).
 */
void
lp_build_cube_new_coords(struct lp_build_context *ivec_bld,
                        LLVMValueRef face,
                        LLVMValueRef x0,
                        LLVMValueRef x1,
                        LLVMValueRef y0,
                        LLVMValueRef y1,
                        LLVMValueRef max_coord,
                        LLVMValueRef next_faces[4],
                        LLVMValueRef next_xcoords[4][2],
                        LLVMValueRef next_ycoords[4][2])
{
   /*
    * Lookup tables aren't nice for simd code hence try some logic here.
    * (Note that while it would not be necessary to do per-sample (4) lookups
    * when using a LUT as it's impossible that texels fall off of positive
    * and negative edges simultaneously, it would however be necessary to
    * do 2 lookups for corner handling as in this case texels both fall off
    * of x and y axes.)
    */
   /*
    * Next faces (for face 012345):
    * x < 0.0  : 451110
    * x >= 1.0 : 540001
    * y < 0.0  : 225422
    * y >= 1.0 : 334533
    * Hence nfx+ (and nfy+) == nfx- (nfy-) xor 1
    * nfx-: face > 1 ? (face == 5 ? 0 : 1) : (4 + face & 1)
    * nfy+: face & ~4 > 1 ? face + 2 : 3;
    * This could also use pshufb instead, but would need (manually coded)
    * ssse3 intrinsic (llvm won't do non-constant shuffles).
    */
   struct gallivm_state *gallivm = ivec_bld->gallivm;
   LLVMValueRef sel, sel_f2345, sel_f23, sel_f2, tmpsel, tmp;
   LLVMValueRef faceand1, sel_fand1, maxmx0, maxmx1, maxmy0, maxmy1;
   LLVMValueRef c2 = lp_build_const_int_vec(gallivm, ivec_bld->type, 2);
   LLVMValueRef c3 = lp_build_const_int_vec(gallivm, ivec_bld->type, 3);
   LLVMValueRef c4 = lp_build_const_int_vec(gallivm, ivec_bld->type, 4);
   LLVMValueRef c5 = lp_build_const_int_vec(gallivm, ivec_bld->type, 5);

   sel = lp_build_cmp(ivec_bld, PIPE_FUNC_EQUAL, face, c5);
   tmpsel = lp_build_select(ivec_bld, sel, ivec_bld->zero, ivec_bld->one);
   sel_f2345 = lp_build_cmp(ivec_bld, PIPE_FUNC_GREATER, face, ivec_bld->one);
   faceand1 = lp_build_and(ivec_bld, face, ivec_bld->one);
   tmp = lp_build_add(ivec_bld, faceand1, c4);
   next_faces[0] = lp_build_select(ivec_bld, sel_f2345, tmpsel, tmp);
   next_faces[1] = lp_build_xor(ivec_bld, next_faces[0], ivec_bld->one);

   tmp = lp_build_andnot(ivec_bld, face, c4);
   sel_f23 = lp_build_cmp(ivec_bld, PIPE_FUNC_GREATER, tmp, ivec_bld->one);
   tmp = lp_build_add(ivec_bld, face, c2);
   next_faces[3] = lp_build_select(ivec_bld, sel_f23, tmp, c3);
   next_faces[2] = lp_build_xor(ivec_bld, next_faces[3], ivec_bld->one);

   /*
    * new xcoords (for face 012345):
    * x < 0.0  : max   max   t     max-t max  max
    * x >= 1.0 : 0     0     max-t t     0    0
    * y < 0.0  : max   0     max-s s     s    max-s
    * y >= 1.0 : max   0     s     max-s s    max-s
    *
    * ncx[1] = face & ~4 > 1 ? (face == 2 ? max-t : t) : 0
    * ncx[0] = max - ncx[1]
    * ncx[3] = face > 1 ? (face & 1 ? max-s : s) : (face & 1) ? 0 : max
    * ncx[2] = face & ~4 > 1 ? max - ncx[3] : ncx[3]
    */
   sel_f2 = lp_build_cmp(ivec_bld, PIPE_FUNC_EQUAL, face, c2);
   maxmy0 = lp_build_sub(ivec_bld, max_coord, y0);
   tmp = lp_build_select(ivec_bld, sel_f2, maxmy0, y0);
   next_xcoords[1][0] = lp_build_select(ivec_bld, sel_f23, tmp, ivec_bld->zero);
   next_xcoords[0][0] = lp_build_sub(ivec_bld, max_coord, next_xcoords[1][0]);
   maxmy1 = lp_build_sub(ivec_bld, max_coord, y1);
   tmp = lp_build_select(ivec_bld, sel_f2, maxmy1, y1);
   next_xcoords[1][1] = lp_build_select(ivec_bld, sel_f23, tmp, ivec_bld->zero);
   next_xcoords[0][1] = lp_build_sub(ivec_bld, max_coord, next_xcoords[1][1]);

   sel_fand1 = lp_build_cmp(ivec_bld, PIPE_FUNC_EQUAL, faceand1, ivec_bld->one);

   tmpsel = lp_build_select(ivec_bld, sel_fand1, ivec_bld->zero, max_coord);
   maxmx0 = lp_build_sub(ivec_bld, max_coord, x0);
   tmp = lp_build_select(ivec_bld, sel_fand1, maxmx0, x0);
   next_xcoords[3][0] = lp_build_select(ivec_bld, sel_f2345, tmp, tmpsel);
   tmp = lp_build_sub(ivec_bld, max_coord, next_xcoords[3][0]);
   next_xcoords[2][0] = lp_build_select(ivec_bld, sel_f23, tmp, next_xcoords[3][0]);
   maxmx1 = lp_build_sub(ivec_bld, max_coord, x1);
   tmp = lp_build_select(ivec_bld, sel_fand1, maxmx1, x1);
   next_xcoords[3][1] = lp_build_select(ivec_bld, sel_f2345, tmp, tmpsel);
   tmp = lp_build_sub(ivec_bld, max_coord, next_xcoords[3][1]);
   next_xcoords[2][1] = lp_build_select(ivec_bld, sel_f23, tmp, next_xcoords[3][1]);

   /*
    * new ycoords (for face 012345):
    * x < 0.0  : t     t     0     max   t    t
    * x >= 1.0 : t     t     0     max   t    t
    * y < 0.0  : max-s s     0     max   max  0
    * y >= 1.0 : s     max-s 0     max   0    max
    *
    * ncy[0] = face & ~4 > 1 ? (face == 2 ? 0 : max) : t
    * ncy[1] = ncy[0]
    * ncy[3] = face > 1 ? (face & 1 ? max : 0) : (face & 1) ? max-s : max
    * ncx[2] = face & ~4 > 1 ? max - ncx[3] : ncx[3]
    */
   tmp = lp_build_select(ivec_bld, sel_f2, ivec_bld->zero, max_coord);
   next_ycoords[0][0] = lp_build_select(ivec_bld, sel_f23, tmp, y0);
   next_ycoords[1][0] = next_ycoords[0][0];
   next_ycoords[0][1] = lp_build_select(ivec_bld, sel_f23, tmp, y1);
   next_ycoords[1][1] = next_ycoords[0][1];

   tmpsel = lp_build_select(ivec_bld, sel_fand1, maxmx0, x0);
   tmp = lp_build_select(ivec_bld, sel_fand1, max_coord, ivec_bld->zero);
   next_ycoords[3][0] = lp_build_select(ivec_bld, sel_f2345, tmp, tmpsel);
   tmp = lp_build_sub(ivec_bld, max_coord, next_ycoords[3][0]);
   next_ycoords[2][0] = lp_build_select(ivec_bld, sel_f23, next_ycoords[3][0], tmp);
   tmpsel = lp_build_select(ivec_bld, sel_fand1, maxmx1, x1);
   tmp = lp_build_select(ivec_bld, sel_fand1, max_coord, ivec_bld->zero);
   next_ycoords[3][1] = lp_build_select(ivec_bld, sel_f2345, tmp, tmpsel);
   tmp = lp_build_sub(ivec_bld, max_coord, next_ycoords[3][1]);
   next_ycoords[2][1] = lp_build_select(ivec_bld, sel_f23, next_ycoords[3][1], tmp);
}


/** Helper used by lp_build_cube_lookup() */
static LLVMValueRef
lp_build_cube_imapos(struct lp_build_context *coord_bld, LLVMValueRef coord)
{
   /* ima = +0.5 / abs(coord); */
   LLVMValueRef posHalf = lp_build_const_vec(coord_bld->gallivm, coord_bld->type, 0.5);
   LLVMValueRef absCoord = lp_build_abs(coord_bld, coord);
   /* avoid div by zero */
   LLVMValueRef sel = lp_build_cmp(coord_bld, PIPE_FUNC_GREATER, absCoord, coord_bld->zero);
   LLVMValueRef div = lp_build_div(coord_bld, posHalf, absCoord);
   LLVMValueRef ima = lp_build_select(coord_bld, sel, div, coord_bld->zero);
   return ima;
}


/** Helper for doing 3-wise selection.
 * Returns sel1 ? val2 : (sel0 ? val0 : val1).
 */
static LLVMValueRef
lp_build_select3(struct lp_build_context *sel_bld,
                 LLVMValueRef sel0,
                 LLVMValueRef sel1,
                 LLVMValueRef val0,
                 LLVMValueRef val1,
                 LLVMValueRef val2)
{
   LLVMValueRef tmp = lp_build_select(sel_bld, sel0, val0, val1);
   return lp_build_select(sel_bld, sel1, val2, tmp);
}


/**
 * Generate code to do cube face selection and compute per-face texcoords.
 */
void
lp_build_cube_lookup(struct lp_build_sample_context *bld,
                     LLVMValueRef *coords,
                     const struct lp_derivatives *derivs_in, /* optional */
                     struct lp_derivatives *derivs_out, /* optional */
                     bool need_derivs)
{
   struct lp_build_context *coord_bld = &bld->coord_bld;
   LLVMBuilderRef builder = bld->gallivm->builder;
   struct gallivm_state *gallivm = bld->gallivm;
   LLVMValueRef si, ti, ri;

   /*
    * Do per-pixel face selection. We cannot however (as we used to do)
    * simply calculate the derivs afterwards (which is very bogus for
    * explicit derivs btw) because the values would be "random" when
    * not all pixels lie on the same face.
    */
   struct lp_build_context *cint_bld = &bld->int_coord_bld;
   struct lp_type intctype = cint_bld->type;
   LLVMTypeRef coord_vec_type = coord_bld->vec_type;
   LLVMTypeRef cint_vec_type = cint_bld->vec_type;
   LLVMValueRef as, at, ar, face, face_s, face_t;
   LLVMValueRef as_ge_at, maxasat, ar_ge_as_at;
   LLVMValueRef snewx, tnewx, snewy, tnewy, snewz, tnewz;
   LLVMValueRef tnegi, rnegi;
   LLVMValueRef ma, mai, signma, signmabit, imahalfpos;
   LLVMValueRef posHalf = lp_build_const_vec(gallivm, coord_bld->type, 0.5);
   LLVMValueRef signmask = lp_build_const_int_vec(gallivm, intctype,
                                                  1LL << (intctype.width - 1));
   LLVMValueRef signshift = lp_build_const_int_vec(gallivm, intctype,
                                                   intctype.width -1);
   LLVMValueRef facex = lp_build_const_int_vec(gallivm, intctype, PIPE_TEX_FACE_POS_X);
   LLVMValueRef facey = lp_build_const_int_vec(gallivm, intctype, PIPE_TEX_FACE_POS_Y);
   LLVMValueRef facez = lp_build_const_int_vec(gallivm, intctype, PIPE_TEX_FACE_POS_Z);
   LLVMValueRef s = coords[0];
   LLVMValueRef t = coords[1];
   LLVMValueRef r = coords[2];

   assert(PIPE_TEX_FACE_NEG_X == PIPE_TEX_FACE_POS_X + 1);
   assert(PIPE_TEX_FACE_NEG_Y == PIPE_TEX_FACE_POS_Y + 1);
   assert(PIPE_TEX_FACE_NEG_Z == PIPE_TEX_FACE_POS_Z + 1);

   /*
    * get absolute value (for x/y/z face selection) and sign bit
    * (for mirroring minor coords and pos/neg face selection)
    * of the original coords.
    */
   as = lp_build_abs(&bld->coord_bld, s);
   at = lp_build_abs(&bld->coord_bld, t);
   ar = lp_build_abs(&bld->coord_bld, r);

   /*
    * major face determination: select x if x > y else select y
    * select z if z >= max(x,y) else select previous result
    * if some axis are the same we chose z over y, y over x - the
    * dx10 spec seems to ask for it while OpenGL doesn't care (if we
    * wouldn't care could save a select or two if using different
    * compares and doing at_g_as_ar last since tnewx and tnewz are the
    * same).
    */
   as_ge_at = lp_build_cmp(coord_bld, PIPE_FUNC_GREATER, as, at);
   maxasat = lp_build_max(coord_bld, as, at);
   ar_ge_as_at = lp_build_cmp(coord_bld, PIPE_FUNC_GEQUAL, ar, maxasat);

   if (need_derivs) {
      /*
       * XXX: This is really really complex.
       * It is a bit overkill to use this for implicit derivatives as well,
       * no way this is worth the cost in practice, but seems to be the
       * only way for getting accurate and per-pixel lod values.
       */
      LLVMValueRef ima, imahalf, tmp, ddx[3], ddy[3];
      LLVMValueRef madx, mady, madxdivma, madydivma;
      LLVMValueRef sdxi, tdxi, rdxi, sdyi, tdyi, rdyi;
      LLVMValueRef tdxnegi, rdxnegi, tdynegi, rdynegi;
      LLVMValueRef sdxnewx, sdxnewy, sdxnewz, tdxnewx, tdxnewy, tdxnewz;
      LLVMValueRef sdynewx, sdynewy, sdynewz, tdynewx, tdynewy, tdynewz;
      LLVMValueRef face_sdx, face_tdx, face_sdy, face_tdy;
      /*
       * s = 1/2 * (sc / ma + 1)
       * t = 1/2 * (tc / ma + 1)
       *
       * s' = 1/2 * (sc' * ma - sc * ma') / ma^2
       * t' = 1/2 * (tc' * ma - tc * ma') / ma^2
       *
       * dx.s = 0.5 * (dx.sc - sc * dx.ma / ma) / ma
       * dx.t = 0.5 * (dx.tc - tc * dx.ma / ma) / ma
       * dy.s = 0.5 * (dy.sc - sc * dy.ma / ma) / ma
       * dy.t = 0.5 * (dy.tc - tc * dy.ma / ma) / ma
       */

      /* select ma, calculate ima */
      ma = lp_build_select3(coord_bld, as_ge_at, ar_ge_as_at, s, t, r);
      mai = LLVMBuildBitCast(builder, ma, cint_vec_type, "");
      signmabit = LLVMBuildAnd(builder, mai, signmask, "");
      ima = lp_build_div(coord_bld, coord_bld->one, ma);
      imahalf = lp_build_mul(coord_bld, posHalf, ima);
      imahalfpos = lp_build_abs(coord_bld, imahalf);

      if (!derivs_in) {
         ddx[0] = lp_build_ddx(coord_bld, s);
         ddx[1] = lp_build_ddx(coord_bld, t);
         ddx[2] = lp_build_ddx(coord_bld, r);
         ddy[0] = lp_build_ddy(coord_bld, s);
         ddy[1] = lp_build_ddy(coord_bld, t);
         ddy[2] = lp_build_ddy(coord_bld, r);
      } else {
         ddx[0] = derivs_in->ddx[0];
         ddx[1] = derivs_in->ddx[1];
         ddx[2] = derivs_in->ddx[2];
         ddy[0] = derivs_in->ddy[0];
         ddy[1] = derivs_in->ddy[1];
         ddy[2] = derivs_in->ddy[2];
      }

      /* select major derivatives */
      madx = lp_build_select3(coord_bld, as_ge_at, ar_ge_as_at, ddx[0], ddx[1], ddx[2]);
      mady = lp_build_select3(coord_bld, as_ge_at, ar_ge_as_at, ddy[0], ddy[1], ddy[2]);

      si = LLVMBuildBitCast(builder, s, cint_vec_type, "");
      ti = LLVMBuildBitCast(builder, t, cint_vec_type, "");
      ri = LLVMBuildBitCast(builder, r, cint_vec_type, "");

      sdxi = LLVMBuildBitCast(builder, ddx[0], cint_vec_type, "");
      tdxi = LLVMBuildBitCast(builder, ddx[1], cint_vec_type, "");
      rdxi = LLVMBuildBitCast(builder, ddx[2], cint_vec_type, "");

      sdyi = LLVMBuildBitCast(builder, ddy[0], cint_vec_type, "");
      tdyi = LLVMBuildBitCast(builder, ddy[1], cint_vec_type, "");
      rdyi = LLVMBuildBitCast(builder, ddy[2], cint_vec_type, "");

      /*
       * compute all possible new s/t coords, which does the mirroring,
       * and do the same for derivs minor axes.
       * snewx = signma * -r;
       * tnewx = -t;
       * snewy = s;
       * tnewy = signma * r;
       * snewz = signma * s;
       * tnewz = -t;
       */
      tnegi = LLVMBuildXor(builder, ti, signmask, "");
      rnegi = LLVMBuildXor(builder, ri, signmask, "");
      tdxnegi = LLVMBuildXor(builder, tdxi, signmask, "");
      rdxnegi = LLVMBuildXor(builder, rdxi, signmask, "");
      tdynegi = LLVMBuildXor(builder, tdyi, signmask, "");
      rdynegi = LLVMBuildXor(builder, rdyi, signmask, "");

      snewx = LLVMBuildXor(builder, signmabit, rnegi, "");
      tnewx = tnegi;
      sdxnewx = LLVMBuildXor(builder, signmabit, rdxnegi, "");
      tdxnewx = tdxnegi;
      sdynewx = LLVMBuildXor(builder, signmabit, rdynegi, "");
      tdynewx = tdynegi;

      snewy = si;
      tnewy = LLVMBuildXor(builder, signmabit, ri, "");
      sdxnewy = sdxi;
      tdxnewy = LLVMBuildXor(builder, signmabit, rdxi, "");
      sdynewy = sdyi;
      tdynewy = LLVMBuildXor(builder, signmabit, rdyi, "");

      snewz = LLVMBuildXor(builder, signmabit, si, "");
      tnewz = tnegi;
      sdxnewz = LLVMBuildXor(builder, signmabit, sdxi, "");
      tdxnewz = tdxnegi;
      sdynewz = LLVMBuildXor(builder, signmabit, sdyi, "");
      tdynewz = tdynegi;

      /* select the mirrored values */
      face = lp_build_select3(cint_bld, as_ge_at, ar_ge_as_at, facex, facey, facez);
      face_s = lp_build_select3(cint_bld, as_ge_at, ar_ge_as_at, snewx, snewy, snewz);
      face_t = lp_build_select3(cint_bld, as_ge_at, ar_ge_as_at, tnewx, tnewy, tnewz);
      face_sdx = lp_build_select3(cint_bld, as_ge_at, ar_ge_as_at, sdxnewx, sdxnewy, sdxnewz);
      face_tdx = lp_build_select3(cint_bld, as_ge_at, ar_ge_as_at, tdxnewx, tdxnewy, tdxnewz);
      face_sdy = lp_build_select3(cint_bld, as_ge_at, ar_ge_as_at, sdynewx, sdynewy, sdynewz);
      face_tdy = lp_build_select3(cint_bld, as_ge_at, ar_ge_as_at, tdynewx, tdynewy, tdynewz);

      face_s = LLVMBuildBitCast(builder, face_s, coord_vec_type, "");
      face_t = LLVMBuildBitCast(builder, face_t, coord_vec_type, "");
      face_sdx = LLVMBuildBitCast(builder, face_sdx, coord_vec_type, "");
      face_tdx = LLVMBuildBitCast(builder, face_tdx, coord_vec_type, "");
      face_sdy = LLVMBuildBitCast(builder, face_sdy, coord_vec_type, "");
      face_tdy = LLVMBuildBitCast(builder, face_tdy, coord_vec_type, "");

      /* deriv math, dx.s = 0.5 * (dx.sc - sc * dx.ma / ma) / ma */
      madxdivma = lp_build_mul(coord_bld, madx, ima);
      tmp = lp_build_mul(coord_bld, madxdivma, face_s);
      tmp = lp_build_sub(coord_bld, face_sdx, tmp);
      derivs_out->ddx[0] = lp_build_mul(coord_bld, tmp, imahalf);

      /* dx.t = 0.5 * (dx.tc - tc * dx.ma / ma) / ma */
      tmp = lp_build_mul(coord_bld, madxdivma, face_t);
      tmp = lp_build_sub(coord_bld, face_tdx, tmp);
      derivs_out->ddx[1] = lp_build_mul(coord_bld, tmp, imahalf);

      /* dy.s = 0.5 * (dy.sc - sc * dy.ma / ma) / ma */
      madydivma = lp_build_mul(coord_bld, mady, ima);
      tmp = lp_build_mul(coord_bld, madydivma, face_s);
      tmp = lp_build_sub(coord_bld, face_sdy, tmp);
      derivs_out->ddy[0] = lp_build_mul(coord_bld, tmp, imahalf);

      /* dy.t = 0.5 * (dy.tc - tc * dy.ma / ma) / ma */
      tmp = lp_build_mul(coord_bld, madydivma, face_t);
      tmp = lp_build_sub(coord_bld, face_tdy, tmp);
      derivs_out->ddy[1] = lp_build_mul(coord_bld, tmp, imahalf);

      signma = LLVMBuildLShr(builder, mai, signshift, "");
      coords[2] = LLVMBuildOr(builder, face, signma, "face");

      /* project coords */
      face_s = lp_build_mul(coord_bld, face_s, imahalfpos);
      face_t = lp_build_mul(coord_bld, face_t, imahalfpos);

      coords[0] = lp_build_add(coord_bld, face_s, posHalf);
      coords[1] = lp_build_add(coord_bld, face_t, posHalf);

      return;
   }

   ma = lp_build_select3(coord_bld, as_ge_at, ar_ge_as_at, s, t, r);
   mai = LLVMBuildBitCast(builder, ma, cint_vec_type, "");
   signmabit = LLVMBuildAnd(builder, mai, signmask, "");

   si = LLVMBuildBitCast(builder, s, cint_vec_type, "");
   ti = LLVMBuildBitCast(builder, t, cint_vec_type, "");
   ri = LLVMBuildBitCast(builder, r, cint_vec_type, "");

   /*
    * compute all possible new s/t coords, which does the mirroring
    * snewx = signma * -r;
    * tnewx = -t;
    * snewy = s;
    * tnewy = signma * r;
    * snewz = signma * s;
    * tnewz = -t;
    */
   tnegi = LLVMBuildXor(builder, ti, signmask, "");
   rnegi = LLVMBuildXor(builder, ri, signmask, "");

   snewx = LLVMBuildXor(builder, signmabit, rnegi, "");
   tnewx = tnegi;

   snewy = si;
   tnewy = LLVMBuildXor(builder, signmabit, ri, "");

   snewz = LLVMBuildXor(builder, signmabit, si, "");
   tnewz = tnegi;

   /* select the mirrored values */
   face_s = lp_build_select3(cint_bld, as_ge_at, ar_ge_as_at, snewx, snewy, snewz);
   face_t = lp_build_select3(cint_bld, as_ge_at, ar_ge_as_at, tnewx, tnewy, tnewz);
   face = lp_build_select3(cint_bld, as_ge_at, ar_ge_as_at, facex, facey, facez);

   face_s = LLVMBuildBitCast(builder, face_s, coord_vec_type, "");
   face_t = LLVMBuildBitCast(builder, face_t, coord_vec_type, "");

   /* add +1 for neg face */
   /* XXX with AVX probably want to use another select here -
    * as long as we ensure vblendvps gets used we can actually
    * skip the comparison and just use sign as a "mask" directly.
    */
   signma = LLVMBuildLShr(builder, mai, signshift, "");
   coords[2] = LLVMBuildOr(builder, face, signma, "face");

   /* project coords */
   imahalfpos = lp_build_cube_imapos(coord_bld, ma);
   face_s = lp_build_mul(coord_bld, face_s, imahalfpos);
   face_t = lp_build_mul(coord_bld, face_t, imahalfpos);

   coords[0] = lp_build_add(coord_bld, face_s, posHalf);
   coords[1] = lp_build_add(coord_bld, face_t, posHalf);
}


/**
 * Compute the partial offset of a pixel block along an arbitrary axis.
 *
 * @param coord   coordinate in pixels
 * @param stride  number of bytes between rows of successive pixel blocks
 * @param block_length  number of pixels in a pixels block along the coordinate
 *                      axis
 * @param out_offset    resulting relative offset of the pixel block in bytes
 * @param out_subcoord  resulting sub-block pixel coordinate
 */
void
lp_build_sample_partial_offset(struct lp_build_context *bld,
                               unsigned block_length,
                               LLVMValueRef coord,
                               LLVMValueRef stride,
                               LLVMValueRef *out_offset,
                               LLVMValueRef *out_subcoord)
{
   LLVMBuilderRef builder = bld->gallivm->builder;
   LLVMValueRef offset;
   LLVMValueRef subcoord;

   if (block_length == 1) {
      subcoord = bld->zero;
   } else {
      /*
       * Pixel blocks have power of two dimensions. LLVM should convert the
       * rem/div to bit arithmetic.
       * TODO: Verify this.
       * It does indeed BUT it does transform it to scalar (and back) when doing so
       * (using roughly extract, shift/and, mov, unpack) (llvm 2.7).
       * The generated code looks seriously unfunny and is quite expensive.
       */
#if 0
      LLVMValueRef block_width = lp_build_const_int_vec(bld->type, block_length);
      subcoord = LLVMBuildURem(builder, coord, block_width, "");
      coord    = LLVMBuildUDiv(builder, coord, block_width, "");
#else
      unsigned logbase2 = util_logbase2(block_length);
      LLVMValueRef block_shift = lp_build_const_int_vec(bld->gallivm, bld->type, logbase2);
      LLVMValueRef block_mask = lp_build_const_int_vec(bld->gallivm, bld->type, block_length - 1);
      subcoord = LLVMBuildAnd(builder, coord, block_mask, "");
      coord = LLVMBuildLShr(builder, coord, block_shift, "");
#endif
   }

   offset = lp_build_mul(bld, coord, stride);

   assert(out_offset);
   assert(out_subcoord);

   *out_offset = offset;
   *out_subcoord = subcoord;
}


/**
 * Compute the offset of a pixel block.
 *
 * x, y, z, y_stride, z_stride are vectors, and they refer to pixels.
 *
 * Returns the relative offset and i,j sub-block coordinates
 */
void
lp_build_sample_offset(struct lp_build_context *bld,
                       const struct util_format_description *format_desc,
                       LLVMValueRef x,
                       LLVMValueRef y,
                       LLVMValueRef z,
                       LLVMValueRef y_stride,
                       LLVMValueRef z_stride,
                       LLVMValueRef *out_offset,
                       LLVMValueRef *out_i,
                       LLVMValueRef *out_j)
{
   LLVMValueRef x_stride;
   LLVMValueRef offset;

   x_stride = lp_build_const_vec(bld->gallivm, bld->type,
                                 format_desc->block.bits/8);

   lp_build_sample_partial_offset(bld,
                                  format_desc->block.width,
                                  x, x_stride,
                                  &offset, out_i);

   if (y && y_stride) {
      LLVMValueRef y_offset;
      lp_build_sample_partial_offset(bld,
                                     format_desc->block.height,
                                     y, y_stride,
                                     &y_offset, out_j);
      offset = lp_build_add(bld, offset, y_offset);
   } else {
      *out_j = bld->zero;
   }

   if (z && z_stride) {
      LLVMValueRef z_offset;
      LLVMValueRef k;
      lp_build_sample_partial_offset(bld,
                                     1, /* pixel blocks are always 2D */
                                     z, z_stride,
                                     &z_offset, &k);
      offset = lp_build_add(bld, offset, z_offset);
   }

   *out_offset = offset;
}



void
lp_build_tiled_sample_offset(struct lp_build_context *bld,
                             enum pipe_format format,
                             const struct lp_static_texture_state *static_texture_state,
                             LLVMValueRef x,
                             LLVMValueRef y,
                             LLVMValueRef z,
                             LLVMValueRef width,
                             LLVMValueRef height,
                             LLVMValueRef z_stride,
                             LLVMValueRef *out_offset,
                             LLVMValueRef *out_i,
                             LLVMValueRef *out_j)
{
   struct gallivm_state *gallivm = bld->gallivm;
   LLVMBuilderRef builder = gallivm->builder;

   assert(static_texture_state->tiled);

   uint32_t res_dimensions = 1;
   switch (static_texture_state->res_target) {
   case PIPE_TEXTURE_2D:
   case PIPE_TEXTURE_CUBE:
   case PIPE_TEXTURE_RECT:
   case PIPE_TEXTURE_2D_ARRAY:
      res_dimensions = 2;
      break;
   case PIPE_TEXTURE_3D:
      res_dimensions = 3;
      break;
   default:
      break;
   }

   uint32_t dimensions = 1;
   switch (static_texture_state->target) {
   case PIPE_TEXTURE_2D:
   case PIPE_TEXTURE_CUBE:
   case PIPE_TEXTURE_RECT:
   case PIPE_TEXTURE_2D_ARRAY:
      dimensions = 2;
      break;
   case PIPE_TEXTURE_3D:
      dimensions = 3;
      break;
   default:
      break;
   }

   uint32_t block_size[3] = {
      util_format_get_blockwidth(format),
      util_format_get_blockheight(format),
      util_format_get_blockdepth(format),
   };

   uint32_t sparse_tile_size[3] = {
      util_format_get_tilesize(format, res_dimensions, static_texture_state->tiled_samples, 0) * block_size[0],
      util_format_get_tilesize(format, res_dimensions, static_texture_state->tiled_samples, 1) * block_size[1],
      util_format_get_tilesize(format, res_dimensions, static_texture_state->tiled_samples, 2) * block_size[2],
   };

   LLVMValueRef sparse_tile_size_log2[3] = {
      lp_build_const_vec(gallivm, bld->type, util_logbase2(sparse_tile_size[0])),
      lp_build_const_vec(gallivm, bld->type, util_logbase2(sparse_tile_size[1])),
      lp_build_const_vec(gallivm, bld->type, util_logbase2(sparse_tile_size[2])),
   };

   LLVMValueRef tile_index = LLVMBuildLShr(builder, x, sparse_tile_size_log2[0], "");

   if (y && dimensions > 1) {
      LLVMValueRef x_tile_count = lp_build_add(bld, width, lp_build_const_vec(gallivm, bld->type, sparse_tile_size[0] - 1));
      x_tile_count = LLVMBuildLShr(builder, x_tile_count, sparse_tile_size_log2[0], "");
      LLVMValueRef y_tile = LLVMBuildLShr(builder, y, sparse_tile_size_log2[1], "");
      tile_index = lp_build_add(bld, tile_index, lp_build_mul(bld, y_tile, x_tile_count));

      if (z && dimensions > 2) {
         LLVMValueRef y_tile_count = lp_build_add(bld, height, lp_build_const_vec(gallivm, bld->type, sparse_tile_size[1] - 1));
         y_tile_count = LLVMBuildLShr(builder, y_tile_count, sparse_tile_size_log2[1], "");
         LLVMValueRef z_tile = LLVMBuildLShr(builder, z, sparse_tile_size_log2[2], "");
         tile_index = lp_build_add(bld, tile_index, lp_build_mul(bld, z_tile, lp_build_mul(bld, x_tile_count, y_tile_count)));
      }
   }

   LLVMValueRef offset = LLVMBuildShl(builder, tile_index, lp_build_const_vec(gallivm, bld->type, 16), "");

   LLVMValueRef sparse_tile_masks[3] = {
      lp_build_const_vec(gallivm, bld->type, sparse_tile_size[0] - 1),
      lp_build_const_vec(gallivm, bld->type, sparse_tile_size[1] - 1),
      lp_build_const_vec(gallivm, bld->type, sparse_tile_size[2] - 1),
   };

   x = LLVMBuildAnd(builder, x, sparse_tile_masks[0], "");
   LLVMValueRef x_stride = lp_build_const_vec(gallivm, bld->type, util_format_get_blocksize(format));

   LLVMValueRef x_offset;
   lp_build_sample_partial_offset(bld, block_size[0],
                                  x, x_stride, &x_offset, out_i);
   offset = lp_build_add(bld, offset, x_offset);

   if (y && dimensions > 1) {
      y = LLVMBuildAnd(builder, y, sparse_tile_masks[1], "");
      LLVMValueRef y_stride = lp_build_const_vec(gallivm, bld->type, util_format_get_blocksize(format) *
                                                 sparse_tile_size[0] / block_size[0]);

      LLVMValueRef y_offset;
      lp_build_sample_partial_offset(bld, block_size[1],
                                     y, y_stride, &y_offset, out_j);
      offset = lp_build_add(bld, offset, y_offset);
   } else {
      *out_j = bld->zero;
   }

   if (z && (z_stride || dimensions > 2)) {
      if (dimensions > 2) {
         z = LLVMBuildAnd(builder, z, sparse_tile_masks[2], "");
         z_stride = lp_build_const_vec(gallivm, bld->type, util_format_get_blocksize(format) *
                                       sparse_tile_size[0] / block_size[0] *
                                       sparse_tile_size[1] / block_size[1]);
      }

      LLVMValueRef z_offset;
      LLVMValueRef k;
      lp_build_sample_partial_offset(bld, 1, z, z_stride, &z_offset, &k);
      offset = lp_build_add(bld, offset, z_offset);
   }

   *out_offset = offset;
}


static LLVMValueRef
lp_build_sample_min(struct lp_build_context *bld,
                    LLVMValueRef x,
                    LLVMValueRef v0,
                    LLVMValueRef v1)
{
   /* if the incoming LERP weight is 0 then the min/max
    * should ignore that value. */
   LLVMValueRef mask = lp_build_compare(bld->gallivm,
                                        bld->type,
                                        PIPE_FUNC_NOTEQUAL,
                                        x, bld->zero);
   LLVMValueRef min = lp_build_min(bld, v0, v1);

   return lp_build_select(bld, mask, min, v0);
}


static LLVMValueRef
lp_build_sample_max(struct lp_build_context *bld,
                    LLVMValueRef x,
                    LLVMValueRef v0,
                    LLVMValueRef v1)
{
   /* if the incoming LERP weight is 0 then the min/max
    * should ignore that value. */
   LLVMValueRef mask = lp_build_compare(bld->gallivm,
                                        bld->type,
                                        PIPE_FUNC_NOTEQUAL,
                                        x, bld->zero);
   LLVMValueRef max = lp_build_max(bld, v0, v1);

   return lp_build_select(bld, mask, max, v0);
}


static LLVMValueRef
lp_build_sample_min_2d(struct lp_build_context *bld,
                       LLVMValueRef x,
                       LLVMValueRef y,
                       LLVMValueRef a,
                       LLVMValueRef b,
                       LLVMValueRef c,
                       LLVMValueRef d)
{
   LLVMValueRef v0 = lp_build_sample_min(bld, x, a, b);
   LLVMValueRef v1 = lp_build_sample_min(bld, x, c, d);
   return lp_build_sample_min(bld, y, v0, v1);
}


static LLVMValueRef
lp_build_sample_max_2d(struct lp_build_context *bld,
                       LLVMValueRef x,
                       LLVMValueRef y,
                       LLVMValueRef a,
                       LLVMValueRef b,
                       LLVMValueRef c,
                       LLVMValueRef d)
{
   LLVMValueRef v0 = lp_build_sample_max(bld, x, a, b);
   LLVMValueRef v1 = lp_build_sample_max(bld, x, c, d);
   return lp_build_sample_max(bld, y, v0, v1);
}


static LLVMValueRef
lp_build_sample_min_3d(struct lp_build_context *bld,
                LLVMValueRef x,
                LLVMValueRef y,
                LLVMValueRef z,
                LLVMValueRef a, LLVMValueRef b,
                LLVMValueRef c, LLVMValueRef d,
                LLVMValueRef e, LLVMValueRef f,
                LLVMValueRef g, LLVMValueRef h)
{
   LLVMValueRef v0 = lp_build_sample_min_2d(bld, x, y, a, b, c, d);
   LLVMValueRef v1 = lp_build_sample_min_2d(bld, x, y, e, f, g, h);
   return lp_build_sample_min(bld, z, v0, v1);
}


static LLVMValueRef
lp_build_sample_max_3d(struct lp_build_context *bld,
                       LLVMValueRef x,
                       LLVMValueRef y,
                       LLVMValueRef z,
                       LLVMValueRef a, LLVMValueRef b,
                       LLVMValueRef c, LLVMValueRef d,
                       LLVMValueRef e, LLVMValueRef f,
                       LLVMValueRef g, LLVMValueRef h)
{
   LLVMValueRef v0 = lp_build_sample_max_2d(bld, x, y, a, b, c, d);
   LLVMValueRef v1 = lp_build_sample_max_2d(bld, x, y, e, f, g, h);
   return lp_build_sample_max(bld, z, v0, v1);
}


void
lp_build_reduce_filter(struct lp_build_context *bld,
                       enum pipe_tex_reduction_mode mode,
                       unsigned flags,
                       unsigned num_chan,
                       LLVMValueRef x,
                       LLVMValueRef *v00,
                       LLVMValueRef *v01,
                       LLVMValueRef *out)
{
   unsigned chan;
   switch (mode) {
   case PIPE_TEX_REDUCTION_MIN:
      for (chan = 0; chan < num_chan; chan++)
         out[chan] = lp_build_sample_min(bld, x, v00[chan], v01[chan]);
      break;
   case PIPE_TEX_REDUCTION_MAX:
      for (chan = 0; chan < num_chan; chan++)
         out[chan] = lp_build_sample_max(bld, x, v00[chan], v01[chan]);
      break;
   case PIPE_TEX_REDUCTION_WEIGHTED_AVERAGE:
   default:
      for (chan = 0; chan < num_chan; chan++)
         out[chan] = lp_build_lerp(bld, x, v00[chan], v01[chan], flags);
      break;
   }
}


void
lp_build_reduce_filter_2d(struct lp_build_context *bld,
                          enum pipe_tex_reduction_mode mode,
                          unsigned flags,
                          unsigned num_chan,
                          LLVMValueRef x,
                          LLVMValueRef y,
                          LLVMValueRef *v00,
                          LLVMValueRef *v01,
                          LLVMValueRef *v10,
                          LLVMValueRef *v11,
                          LLVMValueRef *out)
{
   switch (mode) {
   case PIPE_TEX_REDUCTION_MIN:
      for (unsigned chan = 0; chan < num_chan; chan++)
         out[chan] = lp_build_sample_min_2d(bld, x, y, v00[chan], v01[chan],
                                            v10[chan], v11[chan]);
      break;
   case PIPE_TEX_REDUCTION_MAX:
      for (unsigned chan = 0; chan < num_chan; chan++)
         out[chan] = lp_build_sample_max_2d(bld, x, y, v00[chan], v01[chan],
                                            v10[chan], v11[chan]);
      break;
   case PIPE_TEX_REDUCTION_WEIGHTED_AVERAGE:
   default:
      for (unsigned chan = 0; chan < num_chan; chan++)
         out[chan] = lp_build_lerp_2d(bld, x, y, v00[chan], v01[chan],
                                      v10[chan], v11[chan], flags);
      break;
   }
}


void
lp_build_reduce_filter_3d(struct lp_build_context *bld,
                          enum pipe_tex_reduction_mode mode,
                          unsigned flags,
                          unsigned num_chan,
                          LLVMValueRef x,
                          LLVMValueRef y,
                          LLVMValueRef z,
                          LLVMValueRef *v000,
                          LLVMValueRef *v001,
                          LLVMValueRef *v010,
                          LLVMValueRef *v011,
                          LLVMValueRef *v100,
                          LLVMValueRef *v101,
                          LLVMValueRef *v110,
                          LLVMValueRef *v111,
                          LLVMValueRef *out)
{
   switch (mode) {
   case PIPE_TEX_REDUCTION_MIN:
      for (unsigned chan = 0; chan < num_chan; chan++)
         out[chan] = lp_build_sample_min_3d(bld, x, y, z,
                                     v000[chan], v001[chan], v010[chan], v011[chan],
                                     v100[chan], v101[chan], v110[chan], v111[chan]);
      break;
   case PIPE_TEX_REDUCTION_MAX:
      for (unsigned chan = 0; chan < num_chan; chan++)
         out[chan] = lp_build_sample_max_3d(bld, x, y, z,
                                     v000[chan], v001[chan], v010[chan], v011[chan],
                                     v100[chan], v101[chan], v110[chan], v111[chan]);
      break;
   case PIPE_TEX_REDUCTION_WEIGHTED_AVERAGE:
   default:
      for (unsigned chan = 0; chan < num_chan; chan++)
         out[chan] = lp_build_lerp_3d(bld, x, y, z,
                                      v000[chan], v001[chan], v010[chan], v011[chan],
                                      v100[chan], v101[chan], v110[chan], v111[chan],
                                      flags);
      break;
   }
}
