#
# generate Mesa file
#
include config.mk
MESA_VER = mesa-25.0.x

MESA_SIZEOF_PTR = 4

GENERATE_FILES = \
	$(MESA_VER)/src/compiler/builtin_types.h \
	$(MESA_VER)/src/compiler/builtin_types.c \
	$(MESA_VER)/src/compiler/nir/nir_builder_opcodes.h \
	$(MESA_VER)/src/compiler/nir/nir_constant_expressions.c \
	$(MESA_VER)/src/compiler/nir/nir_opcodes.h \
	$(MESA_VER)/src/compiler/nir/nir_opcodes.c \
	$(MESA_VER)/src/compiler/nir/nir_opt_algebraic.c \
	$(MESA_VER)/src/compiler/nir/nir_intrinsics.h \
	$(MESA_VER)/src/compiler/nir/nir_intrinsics_indices.h \
	$(MESA_VER)/src/compiler/nir/nir_intrinsics.c \
	$(MESA_VER)/src/compiler/glsl/glsl_lexer.cpp \
	$(MESA_VER)/src/compiler/glsl/glsl_parser.cpp \
	$(MESA_VER)/src/compiler/glsl/ir_expression_operation_constant.h \
	$(MESA_VER)/src/compiler/glsl/ir_expression_operation_strings.h \
	$(MESA_VER)/src/compiler/ir_expression_operation.h \
	$(MESA_VER)/src/compiler/glsl/float64_glsl.h \
	$(MESA_VER)/src/compiler/glsl/cross_platform_settings_piece_all.h \
	$(MESA_VER)/src/compiler/glsl/bc1_glsl.h \
	$(MESA_VER)/src/compiler/glsl/bc4_glsl.h \
	$(MESA_VER)/src/compiler/glsl/etc2_rgba_stitch_glsl.h \
	$(MESA_VER)/src/compiler/glsl/astc_glsl.h \
	$(MESA_VER)/src/compiler/glsl/glcpp/glcpp-lex.c \
	$(MESA_VER)/src/compiler/glsl/glcpp/glcpp-parse.c \
	$(MESA_VER)/src/compiler/spirv/vtn_gather_types.c \
	$(MESA_VER)/src/compiler/spirv/spirv_info.c \
	$(MESA_VER)/src/compiler/spirv/vtn_generator_ids.h \
	$(MESA_VER)/src/mapi/glapi/glapi_mapi_tmp.h \
	$(MESA_VER)/src/mapi/glapi/glprocs.h \
	$(MESA_VER)/src/mapi/glapi/glapitemp.h \
	$(MESA_VER)/src/mapi/glapi/glapitable.h \
	$(MESA_VER)/src/mapi/glapi/glapi_gentable.c \
	$(MESA_VER)/src/mapi/glapi/enums.c \
	$(MESA_VER)/src/mesa/main/api_exec_init.c \
	$(MESA_VER)/src/mesa/main/api_exec_decl.h \
	$(MESA_VER)/src/mesa/main/api_save_init.h \
	$(MESA_VER)/src/mesa/main/api_save.h \
	$(MESA_VER)/src/mesa/main/api_beginend_init.h \
	$(MESA_VER)/src/mesa/main/api_hw_select_init.h \
	$(MESA_VER)/src/mesa/main/unmarshal_table.c \
	$(MESA_VER)/src/mesa/main/marshal_generated0.c \
	$(MESA_VER)/src/mesa/main/marshal_generated1.c \
	$(MESA_VER)/src/mesa/main/marshal_generated2.c \
	$(MESA_VER)/src/mesa/main/marshal_generated3.c \
	$(MESA_VER)/src/mesa/main/marshal_generated4.c \
	$(MESA_VER)/src/mesa/main/marshal_generated5.c \
	$(MESA_VER)/src/mesa/main/marshal_generated6.c \
	$(MESA_VER)/src/mesa/main/marshal_generated7.c \
	$(MESA_VER)/src/mesa/main/marshal_generated.h \
	$(MESA_VER)/src/mesa/main/dispatch.h \
	$(MESA_VER)/src/mapi/glapi/gen/indirect.c \
	$(MESA_VER)/src/mapi/glapi/gen/indirect.h \
	$(MESA_VER)/src/mapi/glapi/gen/indirect_init.c \
	$(MESA_VER)/src/mapi/glapi/gen/indirect_size.h \
	$(MESA_VER)/src/mapi/glapi/gen/indirect_size.c \
	$(MESA_VER)/src/mapi/glapi/gen/glapi_x86.S \
	$(MESA_VER)/src/mesa/main/format_fallback.c \
	$(MESA_VER)/src/mesa/main/get_hash.h \
	$(MESA_VER)/src/mesa/main/format_info.h \
	$(MESA_VER)/src/mesa/program/lex.yy.c \
	$(MESA_VER)/src/mesa/program/program_parse.tab.c \
	$(MESA_VER)/src/gallium/auxiliary/driver_trace/tr_util.c \
	$(MESA_VER)/src/gallium/auxiliary/util/u_tracepoints.c \
	$(MESA_VER)/src/gallium/auxiliary/util/u_tracepoints.h \
	$(MESA_VER)/src/gallium/auxiliary/indices/u_indices_gen.c \
	$(MESA_VER)/src/gallium/auxiliary/indices/u_unfilled_gen.c \
	$(MESA_VER)/src/util/format/u_format_pack.h \
	$(MESA_VER)/src/util/format/u_format_table.c \
	$(MESA_VER)/src/util/format/u_format_gen.h \
	$(MESA_VER)/src/util/format_srgb.c \
	$(MESA_VER)/src/util/driconf_static.h

all: $(GENERATE_FILES)
.PHONY: all clean distclean

clean:

distclean:
	-$(RM) $(GENERATE_FILES)

$(MESA_VER)/src/compiler/nir/nir_builder_opcodes.h:
	cd $(dir $@) && $(PYTHON) nir_builder_opcodes_h.py > $(notdir $@)

$(MESA_VER)/src/compiler/nir/nir_constant_expressions.c:
	cd $(dir $@) && $(PYTHON) nir_constant_expressions.py > $(notdir $@)

$(MESA_VER)/src/compiler/nir/nir_opcodes.h:
	cd $(dir $@) && $(PYTHON) nir_opcodes_h.py > $(notdir $@)

$(MESA_VER)/src/compiler/nir/nir_opcodes.c:
	cd $(dir $@) && $(PYTHON) nir_opcodes_c.py > $(notdir $@)

$(MESA_VER)/src/compiler/nir/nir_opt_algebraic.c:
	cd $(dir $@) && $(PYTHON) nir_opt_algebraic.py --out $(notdir $@)

$(MESA_VER)/src/compiler/nir/nir_intrinsics.h:
	cd $(dir $@) && $(PYTHON) nir_intrinsics_h.py --outdir .

$(MESA_VER)/src/compiler/nir/nir_intrinsics_indices.h:
	cd $(dir $@) && $(PYTHON) nir_intrinsics_indices_h.py --outdir .

$(MESA_VER)/src/compiler/nir/nir_intrinsics.c:
	cd $(dir $@) && $(PYTHON) nir_intrinsics_c.py --outdir .

$(MESA_VER)/src/compiler/glsl/glsl_lexer.cpp:
	cd $(dir $@) && $(FLEX) -o $(notdir $@) glsl_lexer.ll

$(MESA_VER)/src/compiler/glsl/glsl_parser.cpp:
	cd $(dir $@) && $(BISON) -o $(notdir $@) --defines=glsl_parser.h -p _mesa_glsl_ glsl_parser.yy

$(MESA_VER)/src/compiler/glsl/ir_expression_operation_constant.h:
	cd $(dir $@) && $(PYTHON) ir_expression_operation.py constant > $(notdir $@)

$(MESA_VER)/src/compiler/glsl/ir_expression_operation_strings.h:
	cd $(dir $@) && $(PYTHON) ir_expression_operation.py strings > $(notdir $@)

$(MESA_VER)/src/compiler/ir_expression_operation.h:
	cd $(dir $@)glsl && $(PYTHON) ir_expression_operation.py enum > ../$(notdir $@)

$(MESA_VER)/src/compiler/glsl/float64_glsl.h:
	cd $(dir $@) && $(PYTHON) ../../util/xxd.py float64.glsl float64_glsl.h -n float64_source

$(MESA_VER)/src/compiler/glsl/cross_platform_settings_piece_all.h:
	cd $(dir $@) && $(PYTHON) ../../util/xxd.py CrossPlatformSettings_piece_all.glsl cross_platform_settings_piece_all.h -n cross_platform_settings_piece_all_header

$(MESA_VER)/src/compiler/glsl/bc1_glsl.h:
	cd $(dir $@) && $(PYTHON) ../../util/xxd.py bc1.glsl bc1_glsl.h -n bc1_source

$(MESA_VER)/src/compiler/glsl/bc4_glsl.h:
	cd $(dir $@) && $(PYTHON) ../../util/xxd.py bc4.glsl bc4_glsl.h -n bc4_source

$(MESA_VER)/src/compiler/glsl/etc2_rgba_stitch_glsl.h:
	cd $(dir $@) && $(PYTHON) ../../util/xxd.py etc2_rgba_stitch.glsl etc2_rgba_stitch_glsl.h -n etc2_rgba_stitch_source

$(MESA_VER)/src/compiler/glsl/astc_glsl.h:
	cd $(dir $@) && $(PYTHON) ../../util/xxd.py astc_decoder.glsl astc_glsl.h -n astc_source

$(MESA_VER)/src/compiler/glsl/glcpp/glcpp-lex.c:
	cd $(dir $@) && $(FLEX) -o $(notdir $@) glcpp-lex.l 

$(MESA_VER)/src/compiler/glsl/glcpp/glcpp-parse.c:
	cd $(dir $@) && $(BISON) -o $(notdir $@) --defines=glcpp-parse.h -p glcpp_parser_ glcpp-parse.y

$(MESA_VER)/src/compiler/spirv/vtn_gather_types.c:
	cd $(dir $@) && $(PYTHON) vtn_gather_types_c.py spirv.core.grammar.json $(notdir $@)

$(MESA_VER)/src/compiler/spirv/spirv_info.c:
	cd $(dir $@) && $(PYTHON) spirv_info_gen.py --json spirv.core.grammar.json --out-c $(notdir $@) --out-h spirv_info.h

$(MESA_VER)/src/compiler/spirv/vtn_generator_ids.h:
	cd $(dir $@) && $(PYTHON) vtn_generator_ids_h.py spir-v.xml $(notdir $@)

$(MESA_VER)/src/mapi/glapi/glapi_mapi_tmp.h:
	cd $(dir $@)gen && $(PYTHON) ../../mapi_abi.py gl_and_es_API.xml --printer glapi > ../$(notdir $@)

$(MESA_VER)/src/mapi/glapi/glprocs.h:
	cd $(dir $@)gen && $(PYTHON) gl_procs.py -c -f gl_and_es_API.xml > ../$(notdir $@)

$(MESA_VER)/src/mapi/glapi/glapitemp.h:
	cd $(dir $@)gen && $(PYTHON) gl_apitemp.py -f gl_and_es_API.xml > ../$(notdir $@)

$(MESA_VER)/src/mapi/glapi/glapitable.h:
	cd $(dir $@)gen && $(PYTHON) gl_table.py -f gl_and_es_API.xml > ../$(notdir $@)

$(MESA_VER)/src/mapi/glapi/glapi_gentable.c:
	cd $(dir $@)gen && $(PYTHON) gl_gentable.py -f gl_and_es_API.xml > ../$(notdir $@)

$(MESA_VER)/src/mapi/glapi/enums.c:
	cd $(dir $@)gen && $(PYTHON) gl_enums.py -f ../registry/gl.xml > ../$(notdir $@)

$(MESA_VER)/src/mesa/main/api_exec_init.c:
	cd $(MESA_VER)/src/mapi/glapi/gen && $(PYTHON) api_exec_init.py -f gl_and_es_API.xml > ../../../mesa/main/$(notdir $@)

$(MESA_VER)/src/mesa/main/api_exec_decl.h:
	cd $(MESA_VER)/src/mapi/glapi/gen && $(PYTHON) api_exec_decl_h.py -f gl_and_es_API.xml > ../../../mesa/main/$(notdir $@)

$(MESA_VER)/src/mesa/main/api_save_init.h:
	cd $(MESA_VER)/src/mapi/glapi/gen && $(PYTHON) api_save_init_h.py -f gl_and_es_API.xml > ../../../mesa/main/$(notdir $@)

$(MESA_VER)/src/mesa/main/api_save.h:
	cd $(MESA_VER)/src/mapi/glapi/gen && $(PYTHON) api_save_h.py -f gl_and_es_API.xml > ../../../mesa/main/$(notdir $@)

$(MESA_VER)/src/mesa/main/api_beginend_init.h:
	cd $(MESA_VER)/src/mapi/glapi/gen && $(PYTHON) api_beginend_init_h.py -f gl_and_es_API.xml > ../../../mesa/main/$(notdir $@)

$(MESA_VER)/src/mesa/main/api_hw_select_init.h:
	cd $(MESA_VER)/src/mapi/glapi/gen && $(PYTHON) api_hw_select_init_h.py -f gl_and_es_API.xml > ../../../mesa/main/$(notdir $@)

$(MESA_VER)/src/mesa/main/unmarshal_table.c:
	cd $(MESA_VER)/src/mapi/glapi/gen && $(PYTHON) gl_unmarshal_table.py gl_and_es_API.xml $(MESA_SIZEOF_PTR) > ../../../mesa/main/$(notdir $@)

$(MESA_VER)/src/mesa/main/marshal_generated0.c:
	cd $(MESA_VER)/src/mapi/glapi/gen && $(PYTHON) gl_marshal.py gl_and_es_API.xml 0 8 $(MESA_SIZEOF_PTR) > ../../../mesa/main/$(notdir $@)

$(MESA_VER)/src/mesa/main/marshal_generated1.c:
	cd $(MESA_VER)/src/mapi/glapi/gen && $(PYTHON) gl_marshal.py gl_and_es_API.xml 1 8 $(MESA_SIZEOF_PTR) > ../../../mesa/main/$(notdir $@)

$(MESA_VER)/src/mesa/main/marshal_generated2.c:
	cd $(MESA_VER)/src/mapi/glapi/gen && $(PYTHON) gl_marshal.py gl_and_es_API.xml 2 8 $(MESA_SIZEOF_PTR) > ../../../mesa/main/$(notdir $@)

$(MESA_VER)/src/mesa/main/marshal_generated3.c:
	cd $(MESA_VER)/src/mapi/glapi/gen && $(PYTHON) gl_marshal.py gl_and_es_API.xml 3 8 $(MESA_SIZEOF_PTR) > ../../../mesa/main/$(notdir $@)

$(MESA_VER)/src/mesa/main/marshal_generated4.c:
	cd $(MESA_VER)/src/mapi/glapi/gen && $(PYTHON) gl_marshal.py gl_and_es_API.xml 4 8 $(MESA_SIZEOF_PTR) > ../../../mesa/main/$(notdir $@)

$(MESA_VER)/src/mesa/main/marshal_generated5.c:
	cd $(MESA_VER)/src/mapi/glapi/gen && $(PYTHON) gl_marshal.py gl_and_es_API.xml 5 8 $(MESA_SIZEOF_PTR) > ../../../mesa/main/$(notdir $@)

$(MESA_VER)/src/mesa/main/marshal_generated6.c:
	cd $(MESA_VER)/src/mapi/glapi/gen && $(PYTHON) gl_marshal.py gl_and_es_API.xml 6 8 $(MESA_SIZEOF_PTR) > ../../../mesa/main/$(notdir $@)

$(MESA_VER)/src/mesa/main/marshal_generated7.c:
	cd $(MESA_VER)/src/mapi/glapi/gen && $(PYTHON) gl_marshal.py gl_and_es_API.xml 7 8 $(MESA_SIZEOF_PTR) > ../../../mesa/main/$(notdir $@)

$(MESA_VER)/src/mesa/main/marshal_generated.h:
	cd $(MESA_VER)/src/mapi/glapi/gen && $(PYTHON) gl_marshal_h.py gl_and_es_API.xml $(MESA_SIZEOF_PTR) > ../../../mesa/main/$(notdir $@)

$(MESA_VER)/src/mesa/main/dispatch.h:
	cd $(MESA_VER)/src/mapi/glapi/gen && $(PYTHON) gl_table.py -f gl_and_es_API.xml -m dispatch > ../../../mesa/main/$(notdir $@)

#$(MESA_VER)/src/mesa/main/remap_helper.h:
#	cd $(MESA_VER)/src/mapi/glapi/gen && $(PYTHON) remap_helper.py -f gl_and_es_API.xml > ../../../mesa/main/$(notdir $@)

$(MESA_VER)/src/mapi/glapi/gen/indirect.c:
	cd $(dir $@) && $(PYTHON) glX_proto_send.py -f gl_API.xml -m proto > $(notdir $@)

$(MESA_VER)/src/mapi/glapi/gen/indirect.h:
	cd $(dir $@) && $(PYTHON) glX_proto_send.py -f gl_API.xml -m init_h > $(notdir $@)

$(MESA_VER)/src/mapi/glapi/gen/indirect_init.c:
	cd $(dir $@) && $(PYTHON) glX_proto_send.py -f gl_API.xml -m init_c > $(notdir $@)

$(MESA_VER)/src/mapi/glapi/gen/indirect_size.h:
	cd $(dir $@) && $(PYTHON) glX_proto_size.py -f gl_API.xml --only-set -m size_h --header-tag _INDIRECT_SIZE_H_ > $(notdir $@)

$(MESA_VER)/src/mapi/glapi/gen/indirect_size.c:
	cd $(dir $@) && $(PYTHON) glX_proto_size.py -f gl_API.xml --only-set -m size_c > $(notdir $@)

$(MESA_VER)/src/mapi/glapi/gen/glapi_x86.S:
	cd $(dir $@) && $(PYTHON) gl_x86_asm.py -f gl_and_es_API.xml > $(notdir $@)

$(MESA_VER)/src/mesa/main/format_fallback.c:
	cd $(dir $@) && $(PYTHON) format_fallback.py formats.csv $(notdir $@)

$(MESA_VER)/src/mesa/main/get_hash.h:
	cd $(dir $@) && $(PYTHON) get_hash_generator.py -f ../../mapi/glapi/gen/gl_and_es_API.xml > $(notdir $@)

$(MESA_VER)/src/mesa/main/format_info.h:
	cd $(dir $@) && $(PYTHON) format_info.py formats.csv > $(notdir $@)

$(MESA_VER)/src/mesa/program/lex.yy.c:
	cd $(dir $@) && $(FLEX) -o $(notdir $@) program_lexer.l

$(MESA_VER)/src/mesa/program/program_parse.tab.c:
	cd $(dir $@) && $(BISON) -o $(notdir $@) --defines=program_parse.tab.h program_parse.y

$(MESA_VER)/src/gallium/auxiliary/driver_trace/tr_util.c:
	cd $(dir $@) && $(PYTHON) enums2names.py ../../include/pipe/p_defines.h ../../include/pipe/p_video_enums.h ../../../util/blend.h -C $(notdir $@) -H tr_util.h -I tr_util.h

$(MESA_VER)/src/gallium/auxiliary/util/u_tracepoints.c:
	cd $(MESA_VER)/src/util/perf && python ../../gallium/auxiliary/util/u_tracepoints.py -p . -C ../../gallium/auxiliary/util/$(notdir $@)

$(MESA_VER)/src/gallium/auxiliary/util/u_tracepoints.h:
	cd $(MESA_VER)/src/util/perf && python ../../gallium/auxiliary/util/u_tracepoints.py -p . -H ../../gallium/auxiliary/util/$(notdir $@)

$(MESA_VER)/src/gallium/auxiliary/indices/u_indices_gen.c:
	cd $(dir $@) && $(PYTHON) u_indices_gen.py $(notdir $@)

$(MESA_VER)/src/gallium/auxiliary/indices/u_unfilled_gen.c:
	cd $(dir $@) && $(PYTHON) u_unfilled_gen.py $(notdir $@)

$(MESA_VER)/src/util/format/u_format_pack.h:
	cd $(dir $@) && $(PYTHON) u_format_table.py --header u_format.yaml > $(notdir $@)

$(MESA_VER)/src/util/format/u_format_table.c:
	cd $(dir $@) && $(PYTHON) u_format_table.py u_format.yaml > $(notdir $@)

$(MESA_VER)/src/util/format/u_format_gen.h:
	cd $(dir $@) && $(PYTHON) u_format_table.py --enums u_format.yaml > $(notdir $@)

$(MESA_VER)/src/util/format_srgb.c:
	cd $(dir $@) && $(PYTHON) format_srgb.py > $(notdir $@)

$(MESA_VER)/src/compiler/builtin_types.h:
	cd $(dir $@) && $(PYTHON) builtin_types_h.py builtin_types.h

$(MESA_VER)/src/compiler/builtin_types.c:
	cd $(dir $@) && $(PYTHON) builtin_types_c.py builtin_types.c

$(MESA_VER)/src/util/driconf_static.h:
	cd $(dir $@) && $(PYTHON) driconf_static.py 00-mesa-defaults.conf $(notdir $@)
