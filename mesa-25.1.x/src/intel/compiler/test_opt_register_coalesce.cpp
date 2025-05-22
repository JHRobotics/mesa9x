/*
 * Copyright © 2025 Intel Corporation
 * SPDX-License-Identifier: MIT
 */

#include "test_helpers.h"
#include "brw_builder.h"

class RegisterCoalesceTest : public brw_shader_pass_test {};

TEST_F(RegisterCoalesceTest, BasicMov)
{
   brw_builder bld = make_shader();
   brw_builder exp = make_shader();

   brw_reg a = vgrf(bld, exp, BRW_TYPE_F);
   brw_reg b = vgrf(bld, exp, BRW_TYPE_F);
   brw_reg c = vgrf(bld, exp, BRW_TYPE_F);
   brw_reg i = brw_imm_f(42.0);

   brw_reg x = vgrf(bld, exp, BRW_TYPE_F);
   brw_reg y = vgrf(bld, exp, BRW_TYPE_F);

   bld.ADD(x, a, b);
   bld.MOV(y, x);
   bld.MUL(c, y, i);

   EXPECT_PROGRESS(brw_opt_register_coalesce, bld);

   exp.ADD(x, a, b);
   exp.MUL(c, x, i);

   EXPECT_SHADERS_MATCH(bld, exp);
}

TEST_F(RegisterCoalesceTest, RegistersInterfere)
{
   brw_builder bld = make_shader();

   brw_reg a = vgrf(bld, BRW_TYPE_F);
   brw_reg b = vgrf(bld, BRW_TYPE_F);
   brw_reg c = vgrf(bld, BRW_TYPE_F);
   brw_reg d = vgrf(bld, BRW_TYPE_F);
   brw_reg i = brw_imm_f(42.0);

   brw_reg x = vgrf(bld, BRW_TYPE_F);
   brw_reg y = vgrf(bld, BRW_TYPE_F);

   bld.ADD(x, a, b);
   bld.MOV(y, x);
   bld.ADD(x, x, x);
   bld.MUL(c, y, i);
   bld.ADD(d, y, y);

   EXPECT_NO_PROGRESS(brw_opt_register_coalesce, bld);
}

TEST_F(RegisterCoalesceTest, InterfereButContainEachOther)
{
   brw_builder bld = make_shader();
   brw_builder exp = make_shader();

   brw_reg a = vgrf(bld, exp, BRW_TYPE_F);
   brw_reg b = vgrf(bld, exp, BRW_TYPE_F);
   brw_reg c = vgrf(bld, exp, BRW_TYPE_F);
   brw_reg d = vgrf(bld, exp, BRW_TYPE_F);
   brw_reg e = vgrf(bld, exp, BRW_TYPE_F);

   brw_reg x = vgrf(bld, exp, BRW_TYPE_F);
   brw_reg y = vgrf(bld, exp, BRW_TYPE_F);

   bld.MUL(x, a, b);
   bld.ADD(c, x, x);
   bld.ADD(d, x, x);
   bld.MOV(y, x);
   bld.ADD(e, x, y);

   EXPECT_PROGRESS(brw_opt_register_coalesce, bld);

   exp.MUL(x, a, b);
   exp.ADD(c, x, x);
   exp.ADD(d, x, x);
   exp.ADD(e, x, x);

   EXPECT_SHADERS_MATCH(bld, exp);
}
