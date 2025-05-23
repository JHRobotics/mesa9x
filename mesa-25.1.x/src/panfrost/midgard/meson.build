# Copyright © 2018 Rob Clark
# Copyright © 2019 Collabora
# SPDX-License-Identifier: MIT

libpanfrost_midgard_files = files(
  'midgard_compile.c',
  'mir.c',
  'midgard_address.c',
  'midgard_print.c',
  'midgard_schedule.c',
  'midgard_derivatives.c',
  'midgard_emit.c',
  'midgard_helper_invocations.c',
  'midgard_ra.c',
  'midgard_ra_pipeline.c',
  'midgard_liveness.c',
  'midgard_ops.c',
  'mir_promote_uniforms.c',
  'mir_squeeze.c',
  'midgard_nir_lower_image_bitsize.c',
  'midgard_nir_type_csel.c',
  'midgard_opt_copy_prop.c',
  'midgard_opt_dce.c',
  'midgard_opt_perspective.c',
  'midgard_opt_prop.c',
  'midgard_errata_lod.c',
  'nir_fuse_io_16.c',
)

midgard_nir_algebraic_c = custom_target(
  'midgard_nir_algebraic.c',
  input : 'midgard_nir_algebraic.py',
  output : 'midgard_nir_algebraic.c',
  command : [
    prog_python, '@INPUT@', '-p', dir_compiler_nir,
  ],
  capture : true,
  depend_files : nir_algebraic_depends,
)

libpanfrost_midgard_disasm = static_library(
  'panfrost_midgard_disasm',
  ['disassemble.c', 'midgard_ops.c', 'midgard_print_constant.c'],
  include_directories : [inc_include, inc_src],
  c_args : [no_override_init_args],
  gnu_symbol_visibility : 'hidden',
  build_by_default : false,
)

libpanfrost_midgard = static_library(
  'panfrost_midgard',
  [libpanfrost_midgard_files, midgard_nir_algebraic_c],
  include_directories : [inc_include, inc_src],
  dependencies: [
   idep_nir,
   idep_mesautil,
  ],
  link_with: [libpanfrost_util, libpanfrost_midgard_disasm],
  c_args : [no_override_init_args],
  gnu_symbol_visibility : 'hidden',
  build_by_default : false,
)
