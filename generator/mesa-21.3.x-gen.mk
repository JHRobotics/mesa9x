#
# generate some Mesa sources
#
MESA_VER ?= mesa-21.3.x

GENERATE_FILES = \
	$(MESA_VER)/src/compiler/glsl/float64_glsl.h \
	$(MESA_VER)/src/compiler/glsl/glcpp/glcpp-lex.c \
	$(MESA_VER)/src/compiler/glsl/glcpp/glcpp-parse.c \
	$(MESA_VER)/src/compiler/glsl/glcpp/glcpp-parse.h \
	$(MESA_VER)/src/compiler/glsl/glsl_lexer.cpp \
	$(MESA_VER)/src/compiler/glsl/glsl_parser.cpp \
	$(MESA_VER)/src/compiler/glsl/glsl_parser.h \
	$(MESA_VER)/src/compiler/ir_expression_operation.h \
	$(MESA_VER)/src/compiler/ir_expression_operation_constant.h \
	$(MESA_VER)/src/compiler/ir_expression_operation_strings.h \
	$(MESA_VER)/src/compiler/nir/nir_builder_opcodes.h \
	$(MESA_VER)/src/compiler/nir/nir_constant_expressions.c \
	$(MESA_VER)/src/compiler/nir/nir_intrinsics.c \
	$(MESA_VER)/src/compiler/nir/nir_intrinsics.h \
	$(MESA_VER)/src/compiler/nir/nir_intrinsics_indices.h \
	$(MESA_VER)/src/compiler/nir/nir_opcodes.c \
	$(MESA_VER)/src/compiler/nir/nir_opcodes.h \
	$(MESA_VER)/src/compiler/nir/nir_opt_algebraic.c \
	$(MESA_VER)/src/compiler/spirv/spirv_info.c \
	$(MESA_VER)/src/compiler/spirv/vtn_gather_types.c \
	$(MESA_VER)/src/compiler/spirv/vtn_generator_ids.h \
	$(MESA_VER)/src/gallium/auxiliary/indices/u_indices_gen.c \
	$(MESA_VER)/src/gallium/auxiliary/indices/u_unfilled_gen.c \
	$(MESA_VER)/src/mapi/glapi/enums.c  \
	$(MESA_VER)/src/mapi/glapi/glapitable.h \
	$(MESA_VER)/src/mapi/glapi/glapitemp.h \
	$(MESA_VER)/src/mapi/glapi/glprocs.h \
	$(MESA_VER)/src/mesa/main/api_exec.c \
	$(MESA_VER)/src/mesa/main/dispatch.h \
	$(MESA_VER)/src/mesa/main/format_fallback.c \
	$(MESA_VER)/src/mesa/main/format_info.h \
	$(MESA_VER)/src/mesa/main/get_hash.h \
	$(MESA_VER)/src/mesa/main/marshal_generated.h \
	$(MESA_VER)/src/mesa/main/marshal_generated0.c \
	$(MESA_VER)/src/mesa/main/marshal_generated1.c \
	$(MESA_VER)/src/mesa/main/marshal_generated2.c \
	$(MESA_VER)/src/mesa/main/marshal_generated3.c \
	$(MESA_VER)/src/mesa/main/marshal_generated4.c \
	$(MESA_VER)/src/mesa/main/marshal_generated5.c \
	$(MESA_VER)/src/mesa/main/marshal_generated6.c \
	$(MESA_VER)/src/mesa/main/marshal_generated7.c \
	$(MESA_VER)/src/mesa/main/remap_helper.h \
	$(MESA_VER)/src/mesa/program/lex.yy.c \
	$(MESA_VER)/src/mesa/program/program_parse.tab.c \
	$(MESA_VER)/src/mesa/program/program_parse.tab.h \
	$(MESA_VER)/src/util/format/u_format_pack.c \
	$(MESA_VER)/src/util/format/u_format_table.c \
	$(MESA_VER)/src/util/format/u_format_pack.h \
	$(MESA_VER)/src/util/format_srgb.c \
	$(MESA_VER)/src/mapi/glapi/gen/glapi_x86.S \

generator: $(GENERATE_FILES)

$(MESA_VER).deps: $(GENERATE_FILES)
	@echo $(MESA_VER) > $@

distclean: clean
	-$(RM) $(GENERATE_FILES)

$(MESA_VER)/src/compiler/glsl/float64_glsl.h:
	cd $(dir $@) && $(PYTHON) ../../util/xxd.py float64.glsl float64_glsl.h -n float64_source

$(MESA_VER)/src/compiler/glsl/glcpp/glcpp-lex.c:
	cd $(dir $@) && $(FLEX) -o $(notdir $@) glcpp-lex.l 
	
$(MESA_VER)/src/compiler/glsl/glcpp/glcpp-parse.c:
	cd $(dir $@) && $(BISON) -o $(notdir $@) --defines=glcpp-parse.h -p glcpp_parser_ glcpp-parse.y

$(MESA_VER)/src/compiler/glsl/glcpp/glcpp-parse.h: $(MESA_VER)/src/compiler/glsl/glcpp/glcpp-parse.c

$(MESA_VER)/src/compiler/glsl/glsl_lexer.cpp:
	cd $(dir $@) && $(FLEX) -o $(notdir $@) glsl_lexer.ll

$(MESA_VER)/src/compiler/glsl/glsl_parser.cpp:
	cd $(dir $@) && $(BISON) -o $(notdir $@) --defines=glsl_parser.h -p _mesa_glsl_ glsl_parser.yy

$(MESA_VER)/src/compiler/glsl/glsl_parser.h: $(MESA_VER)/src/compiler/glsl/glsl_parser.cpp

$(MESA_VER)/src/compiler/ir_expression_operation.h:
	cd $(dir $@)glsl && $(PYTHON) ir_expression_operation.py enum > ../$(notdir $@)

$(MESA_VER)/src/compiler/ir_expression_operation_constant.h:
	cd $(dir $@)glsl && $(PYTHON) ir_expression_operation.py constant > ../$(notdir $@)

$(MESA_VER)/src/compiler/ir_expression_operation_strings.h:
	cd $(dir $@)glsl && $(PYTHON) ir_expression_operation.py strings > ../$(notdir $@)

$(MESA_VER)/src/compiler/nir/nir_builder_opcodes.h:
	cd $(dir $@) && $(PYTHON) nir_builder_opcodes_h.py > $(notdir $@)

$(MESA_VER)/src/compiler/nir/nir_constant_expressions.c:
	cd $(dir $@) && $(PYTHON) nir_constant_expressions.py > $(notdir $@)

$(MESA_VER)/src/compiler/nir/nir_intrinsics.c:
	cd $(dir $@) && $(PYTHON) nir_intrinsics_c.py --outdir .

$(MESA_VER)/src/compiler/nir/nir_intrinsics.h:
	cd $(dir $@) && $(PYTHON) nir_intrinsics_h.py --outdir .

$(MESA_VER)/src/compiler/nir/nir_intrinsics_indices.h:
	cd $(dir $@) && $(PYTHON) nir_intrinsics_indices_h.py --outdir .

$(MESA_VER)/src/compiler/nir/nir_opcodes.c:
	cd $(dir $@) && $(PYTHON) nir_opcodes_c.py > $(notdir $@)

$(MESA_VER)/src/compiler/nir/nir_opcodes.h:
	cd $(dir $@) && $(PYTHON) nir_opcodes_h.py > $(notdir $@)

$(MESA_VER)/src/compiler/nir/nir_opt_algebraic.c:
	cd $(dir $@) && $(PYTHON) nir_opt_algebraic.py > $(notdir $@)

$(MESA_VER)/src/compiler/spirv/spirv_info.c:
	cd $(dir $@) && $(PYTHON) spirv_info_c.py spirv.core.grammar.json $(notdir $@)

$(MESA_VER)/src/compiler/spirv/vtn_gather_types.c:
	cd $(dir $@) && $(PYTHON) vtn_gather_types_c.py spirv.core.grammar.json $(notdir $@)

$(MESA_VER)/src/compiler/spirv/vtn_generator_ids.h:
	cd $(dir $@) && $(PYTHON) vtn_generator_ids_h.py spir-v.xml $(notdir $@)

$(MESA_VER)/src/gallium/auxiliary/indices/u_indices_gen.c:
	cd $(dir $@) && $(PYTHON) u_indices_gen.py > $(notdir $@)

$(MESA_VER)/src/gallium/auxiliary/indices/u_unfilled_gen.c:
	cd $(dir $@) && $(PYTHON) u_unfilled_gen.py > $(notdir $@)

$(MESA_VER)/src/mapi/glapi/enums.c:
	cd $(dir $@)gen && $(PYTHON) gl_enums.py -f ../registry/gl.xml > ../$(notdir $@)

$(MESA_VER)/src/mapi/glapi/glapitable.h:
	cd $(dir $@)gen && $(PYTHON) gl_table.py -f gl_and_es_API.xml > ../$(notdir $@)

$(MESA_VER)/src/mapi/glapi/glapitemp.h:
	cd $(dir $@)gen && $(PYTHON) gl_apitemp.py -f gl_and_es_API.xml > ../$(notdir $@)

$(MESA_VER)/src/mapi/glapi/glprocs.h:
	cd $(dir $@)gen && $(PYTHON) gl_procs.py -c -f gl_and_es_API.xml > ../$(notdir $@)

$(MESA_VER)/src/mesa/main/api_exec.c:
	cd $(MESA_VER)/src/mapi/glapi/gen && $(PYTHON) gl_genexec.py -f gl_and_es_API.xml > ../../../mesa/main/$(notdir $@)

$(MESA_VER)/src/mesa/main/dispatch.h:
	cd $(MESA_VER)/src/mapi/glapi/gen && $(PYTHON) gl_table.py -f gl_and_es_API.xml -m remap_table > ../../../mesa/main/$(notdir $@)

$(MESA_VER)/src/mesa/main/format_fallback.c:
	cd $(dir $@) && $(PYTHON) format_fallback.py formats.csv $(notdir $@)

$(MESA_VER)/src/mesa/main/format_info.h:
	cd $(dir $@) && $(PYTHON) format_info.py formats.csv > $(notdir $@)

$(MESA_VER)/src/mesa/main/get_hash.h:
	cd $(dir $@) && $(PYTHON) get_hash_generator.py -f ../../mapi/glapi/gen/gl_and_es_API.xml > $(notdir $@)

$(MESA_VER)/src/mesa/main/marshal_generated.h:
	cd $(MESA_VER)/src/mapi/glapi/gen && $(PYTHON) gl_marshal_h.py -f gl_and_es_API.xml > ../../../mesa/main/$(notdir $@)

$(MESA_VER)/src/mesa/main/marshal_generated0.c:
	cd $(MESA_VER)/src/mapi/glapi/gen && $(PYTHON) gl_marshal.py -f gl_and_es_API.xml -i 0 -n 8 > ../../../mesa/main/$(notdir $@)

$(MESA_VER)/src/mesa/main/marshal_generated1.c:
	cd $(MESA_VER)/src/mapi/glapi/gen && $(PYTHON) gl_marshal.py -f gl_and_es_API.xml -i 1 -n 8 > ../../../mesa/main/$(notdir $@)

$(MESA_VER)/src/mesa/main/marshal_generated2.c:
	cd $(MESA_VER)/src/mapi/glapi/gen && $(PYTHON) gl_marshal.py -f gl_and_es_API.xml -i 2 -n 8 > ../../../mesa/main/$(notdir $@)

$(MESA_VER)/src/mesa/main/marshal_generated3.c:
	cd $(MESA_VER)/src/mapi/glapi/gen && $(PYTHON) gl_marshal.py -f gl_and_es_API.xml -i 3 -n 8 > ../../../mesa/main/$(notdir $@)

$(MESA_VER)/src/mesa/main/marshal_generated4.c:
	cd $(MESA_VER)/src/mapi/glapi/gen && $(PYTHON) gl_marshal.py -f gl_and_es_API.xml -i 4 -n 8 > ../../../mesa/main/$(notdir $@)

$(MESA_VER)/src/mesa/main/marshal_generated5.c:
	cd $(MESA_VER)/src/mapi/glapi/gen && $(PYTHON) gl_marshal.py -f gl_and_es_API.xml -i 5 -n 8 > ../../../mesa/main/$(notdir $@)

$(MESA_VER)/src/mesa/main/marshal_generated6.c:
	cd $(MESA_VER)/src/mapi/glapi/gen && $(PYTHON) gl_marshal.py -f gl_and_es_API.xml -i 6 -n 8 > ../../../mesa/main/$(notdir $@)

$(MESA_VER)/src/mesa/main/marshal_generated7.c:
	cd $(MESA_VER)/src/mapi/glapi/gen && $(PYTHON) gl_marshal.py -f gl_and_es_API.xml -i 7 -n 8 > ../../../mesa/main/$(notdir $@)

$(MESA_VER)/src/mesa/main/remap_helper.h:
	cd $(MESA_VER)/src/mapi/glapi/gen && $(PYTHON) remap_helper.py -f gl_and_es_API.xml > ../../../mesa/main/$(notdir $@)

$(MESA_VER)/src/mapi/glapi/gen/glapi_x86.S:
	cd $(dir $@) && $(PYTHON) gl_x86_asm.py -f gl_and_es_API.xml > $(notdir $@)

$(MESA_VER)/src/mesa/program/lex.yy.c:
	cd $(dir $@) && $(FLEX) -o $(notdir $@) program_lexer.l

$(MESA_VER)/src/mesa/program/program_parse.tab.c:
	cd $(dir $@) && $(BISON) -o $(notdir $@) --defines=program_parse.tab.h program_parse.y

$(MESA_VER)/src/mesa/program/program_parse.tab.h: $(MESA_VER)/src/mesa/program/program_parse.tab.c

$(MESA_VER)/src/util/format/u_format_pack.c:
	cd $(dir $@) && $(PYTHON) u_format_pack.py u_format.csv --header > $(notdir $@)

$(MESA_VER)/src/util/format/u_format_pack.h:
	cd $(dir $@) && $(PYTHON) u_format_table.py u_format.csv --header > $(notdir $@)

$(MESA_VER)/src/util/format/u_format_table.c:
	cd $(dir $@) && $(PYTHON) u_format_table.py u_format.csv > $(notdir $@)

$(MESA_VER)/src/util/format_srgb.c:
	cd $(dir $@) && $(PYTHON) format_srgb.py > $(notdir $@)

