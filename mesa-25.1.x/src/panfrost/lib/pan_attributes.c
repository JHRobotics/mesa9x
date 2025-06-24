/*
 * Copyright (C) 2019 Collabora, Ltd.
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

#include "util/u_math.h"
#include "pan_encoder.h"

/* This file handles attribute descriptors. The
 * bulk of the complexity is from instancing. See mali_job for
 * notes on how this works. But basically, for small vertex
 * counts, we have a lookup table, and for large vertex counts,
 * we look at the high bits as a heuristic. This has to match
 * exactly how the hardware calculates this (which is why the
 * algorithm is so weird) or else instancing will break. */

/* Given an odd number (of the form 2k + 1), compute k */
#define ODD(odd) ((odd - 1) >> 1)

static unsigned
panfrost_small_padded_vertex_count(unsigned idx)
{
   if (idx < 10)
      return idx;
   else
      return (idx + 1) & ~1;
}

static unsigned
panfrost_large_padded_vertex_count(uint32_t vertex_count)
{
   /* First, we have to find the highest set one */
   unsigned highest = 32 - __builtin_clz(vertex_count);

   /* Using that, we mask out the highest 4-bits */
   unsigned n = highest - 4;
   unsigned nibble = (vertex_count >> n) & 0xF;

   /* Great, we have the nibble. Now we can just try possibilities. Note
    * that we don't care about the bottom most bit in most cases, and we
    * know the top bit must be 1 */

   unsigned middle_two = (nibble >> 1) & 0x3;

   switch (middle_two) {
   case 0b00:
      if (!(nibble & 1))
         return (1 << n) * 9;
      else
         return (1 << (n + 1)) * 5;
   case 0b01:
      return (1 << (n + 2)) * 3;
   case 0b10:
      return (1 << (n + 1)) * 7;
   case 0b11:
      return (1 << (n + 4));
   default:
      return 0; /* unreachable */
   }
}

unsigned
panfrost_padded_vertex_count(unsigned vertex_count)
{
   if (vertex_count < 20)
      return panfrost_small_padded_vertex_count(vertex_count);
   else
      return panfrost_large_padded_vertex_count(vertex_count);
}

unsigned
panfrost_compute_magic_divisor(unsigned hw_divisor, unsigned *divisor_r,
                               unsigned *divisor_e)
{
   unsigned r = util_logbase2(hw_divisor);

   uint64_t shift_hi = 32 + r;
   uint64_t t = (uint64_t)1 << shift_hi;
   uint64_t f0 = t + hw_divisor / 2;
   uint64_t fi = f0 / hw_divisor;
   uint64_t ff = f0 - fi * hw_divisor;

   uint64_t d = fi - (1ul << 31);
   *divisor_r = r;
   *divisor_e = ff > hw_divisor / 2 ? 1 : 0;

   return d;
}
