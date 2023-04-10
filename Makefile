#
# This is GNU make file DON'T use it with nmake/wmake of whatever even if you
# plan to use MS compiler!
#

################################################################################
# Copyright (c) 2022-2023 Jaroslav Hensl                                       #
#                                                                              #
# See LICENCE file for law informations                                        #
# See README.md file for more build instructions                               #
#                                                                              #
################################################################################

#
# Usage:
#   1) copy config.mk-sample to config.mk
#   2) edit config.mk
#   3) run make
#

include config.mk

MESA_VER = mesa-17.3.9
#DEPS = config.mk Makefile

# only usefull with gcc/mingw
CSTD=c99
CXXSTD=gnu++11

# base address from 98/NT opengl32.dll
BASE_opengl32.w95.dll   := 0x78A40000
BASE_opengl32.w98me.dll := 0x78A40000

# base address from nvidia ICD driver
BASE_mesa3d.w95.dll     := 0x69500000
BASE_mesa3d.w98me.dll   := 0x69500000

BASE_vmwsgl32.dll       := 0x69500000

NULLOUT=$(if $(filter $(OS),Windows_NT),NUL,/dev/null)

GIT      ?= git
GIT_IS   := $(shell $(GIT) rev-parse --is-inside-work-tree 2> $(NULLOUT))
ifeq ($(GIT_IS),true)
  VERSION_BUILD := $(shell $(GIT) rev-list --count main)
endif

TARGETS = opengl32.w95.dll mesa3d.w95.dll vmwsgl32.dll glchecker.exe icdtest.exe wgltest.exe
ifdef LLVM
  TARGETS += opengl32.w98me.dll mesa3d.w98me.dll
endif

ifdef LLVM
  ifndef LLVM_VER
    $(error Define LLVM_VER in config.mk please!)
  endif
endif

all: $(TARGETS)
.PHONY: all clean

ifdef MSC
#
# MSC configuration
#
  OBJ := .obj
  LIBSUFFIX := .lib
  LIBPREFIX := 

  CL_INCLUDE = /Iincmsc /I$(MESA_VER)/include	/I$(MESA_VER)/include/GL /I$(MESA_VER)/src/mapi	/I$(MESA_VER)/src/util /I$(MESA_VER)/src /I$(MESA_VER)/src/mesa /I$(MESA_VER)/src/mesa/main \
    /I$(MESA_VER)/src/compiler -I$(MESA_VER)/src/compiler/nir /I$(MESA_VER)/src/gallium/state_trackers/wgl /I$(MESA_VER)/src/gallium/auxiliary /I$(MESA_VER)/src/gallium/include \
    /I$(MESA_VER)/src/gallium/drivers/svga /I$(MESA_VER)/src/gallium/drivers/svga/include /I$(MESA_VER)/src/gallium/winsys/sw  /I$(MESA_VER)/src/gallium/drivers /I$(MESA_VER)/src/gallium/winsys/svga/drm

  # /DNDEBUG
	CL_DEFS =  /D__i386__ /D_X86_ /D_USE_MATH_DEFINES /D_WIN32 /DWIN32 \
	  /DMAPI_MODE_UTIL /D_GDI32_ /DBUILD_GL32 /DKHRONOS_DLL_EXPORTS /DGDI_DLL_EXPORTS /DGL_API=GLAPI /DGL_APIENTRY=GLAPIENTRY /D_GLAPI_NO_EXPORTS /DCOBJMACROS /DINC_OLE2 \
	  /DPACKAGE_VERSION="\"$(MESA_VER)\"" /DPACKAGE_BUGREPORT="\"$(MESA_VER)\""
  
  ifdef DEBUG
    DD_DEFS = /DDEBUG
  else
    DD_DEFS = /DNDEBUG
  endif
  
  ifdef LLVM
    SIMD_DEFS = $(CL_DEFS) /DHAVE_LLVM=$(LLVM_VER) /DHAVE_GALLIUM_LLVMPIPE /DGALLIUM_LLVMPIPE /DHAVE_LLVMPIPE
    SIMD_INCLUDE += $(CL_INCLUDE) /I$(LLVM_DIR)/include
    
    opengl_simd_LIBS := MesaLibSimd.lib MesaUtilLibSimd.lib MesaGalliumLLVMPipe.lib MesaGalliumAuxLibSimd.lib
    opengl_simd_LIBS := $(opengl_simd_LIBS) $(shell $(LLVM_DIR)/bin/llvm-config --libs --link-static bitwriter engine mcdisassembler mcjit)
    
    # LLVMpipe 6.x required zlib
    MESA_SIMD_LIBS = $(MESA_LIBS) psapi.lib ole32.lib
    
    LIBS_TO_BUILD += $(LIBPREFIX)MesaGalliumLLVMPipe$(LIBSUFFIX)
    LIBS_TO_BUILD += $(LIBPREFIX)MesaUtilLibSimd$(LIBSUFFIX)
    LIBS_TO_BUILD += $(LIBPREFIX)MesaLibSimd$(LIBSUFFIX)
    LIBS_TO_BUILD += $(LIBPREFIX)MesaGalliumAuxLibSimd$(LIBSUFFIX)
    LIBS_TO_BUILD += $(LIBPREFIX)MesaGdiLibGLSimd$(LIBSUFFIX)
    LIBS_TO_BUILD += $(LIBPREFIX)MesaGdiLibICDSimd$(LIBSUFFIX)
    LIBS_TO_BUILD += $(LIBPREFIX)MesaWglLibSimd$(LIBSUFFIX)
    
    # for LLVM we need same cflags as LLVM itself
    LLVM_CFLAGS = $(shell $(LLVM_DIR)/bin/llvm-config --cflags)
    LLVM_CXXFLAGS = $(shell $(LLVM_DIR)/bin/llvm-config --cxxflags)
    ifndef LP_DEBUG
      SIMD_CFLAGS   = /nologo $(filter-out /W4,$(LLVM_CFLAGS)) $(SIMD_INCLUDE) $(SIMD_DEFS)
      SIMD_CXXFLAGS = /nologo  $(filter-out /W4,$(LLVM_CXXFLAGS)) $(SIMD_INCLUDE) $(SIMD_DEFS)
    else
      SIMD_CFLAGS   = /nologo $(MSC_RUNTIME) /Od /Z7 $(SIMD_INCLUDE) $(SIMD_DEFS) $(DD_DEFS)
      SIMD_CXXFLAGS = /nologo $(MSC_RUNTIME) /Od /Z7 $(SIMD_INCLUDE) $(SIMD_DEFS) $(DD_DEFS)
    endif
    SIMD_LDFLAGS = /nologo
  endif
  
  ifdef SPEED
    CFLAGS   = /nologo $(MSC_RUNTIME) /O2 /Oi /Zc:inline $(CL_INCLUDE) $(DD_DEFS) $(CL_DEFS)
    CXXFLAGS = /nologo $(MSC_RUNTIME) /O2 /Oi /Zc:inline $(CL_INCLUDE) $(DD_DEFS) $(CL_DEFS)
    LDFLAGS  = /nologo $(MSC_RUNTIME) /O2 /Oi /Zc:inline
  else
    CFLAGS   = /nologo $(MSC_RUNTIME) /Z7 /Od $(CL_INCLUDE) $(DD_DEFS) $(CL_DEFS)
    CXXFLAGS = /nologo $(MSC_RUNTIME) /Z7 /Od $(CL_INCLUDE) $(DD_DEFS) $(CL_DEFS)
    LDFLAGS  = /nologo $(MSC_RUNTIME) /Z7 /Od
  endif

  ifdef SPEED
    APP_CFLAGS   = /nologo $(MSC_RUNTIME) /O2 /Oi /Zc:inline /I. /Iglchecker $(CL_INCLUDE) $(DD_DEFS) $(CL_DEFS)
    APP_CXXFLAGS = /nologo $(MSC_RUNTIME) /O2 /Oi /Zc:inline /I. /Iglchecker $(CL_INCLUDE) $(DD_DEFS) $(CL_DEFS)
    APP_LDFLAGS  = /nologo $(MSC_RUNTIME) /O2 /Oi /Zc:inline
  else
    APP_CFLAGS   = /nologo $(MSC_RUNTIME) /Z7 /Od /I. /Iglchecker $(CL_INCLUDE) $(DD_DEFS) $(CL_DEFS)
    APP_CXXFLAGS = /nologo $(MSC_RUNTIME) /Z7 /Od /I. /Iglchecker $(CL_INCLUDE) $(DD_DEFS) $(CL_DEFS)
    APP_LDFLAGS  = /nologo $(MSC_RUNTIME) /Od
  endif
  
  ifdef SPEED
    ifdef LTO
      CFLAGS     += /GL
      CXXFLAGS   += /GL
      LDFLAGS    += /LTCG

      APP_CFLAGS   += /GL
      APP_CXXFLAGS += /GL
      APP_LDFLAGS  += /LTCG
    endif
  endif

	OPENGL_LIBS = MesaLib.lib MesaUtilLib.lib MesaGalliumAuxLib.lib
	SVGA_LIBS   = MesaLib.lib MesaUtilLib.lib MesaGalliumAuxLib.lib MesaSVGALib.lib
  MESA_LIBS = kernel32.lib user32.lib gdi32.lib
  
  app_LIBS  =  opengl32.lib gdi32.lib user32.lib
  EXEFLAGS_WIN = /link /OUT:$@ /PDB:$(@:exe=pdb) /SUBSYSTEM:WINDOWS
  EXEFLAGS_CMD = /link /OUT:$@ /PDB:$(@:exe=pdb) /SUBSYSTEM:CONSOLE
  
  %.c_gen.obj: %.c $(DEPS)
		$(CC) $(CFLAGS) /Fo"$@" /c $<
		
  %.cpp_gen.obj: %.cpp $(DEPS)
		$(CXX) $(CXXFLAGS) /Fo"$@" /c $<
	
  %.c_simd.obj: %.c $(DEPS)
		$(CC) $(SIMD_CFLAGS) /Fo"$@" /c $<
		
  %.cpp_simd.obj: %.cpp $(DEPS)
		$(CXX) $(SIMD_CXXFLAGS) /Fo"$@" /c $<
	
  %.c_app.obj: %.c $(DEPS)
		$(CC) $(APP_CFLAGS) /Fo"$@" /c $<
		
  %.cpp_app.obj: %.cpp $(DEPS)
		$(CXX) $(APP_CXXFLAGS) /Fo"$@" /c $<
  
  %.res: %.rc $(DEPS)
		$(WINDRES) /nologo /fo $@ $<
  
  LDLAGS = CXXFLAGS
  DLLFLAGS = /link /DLL /MACHINE:X86 /IMPLIB:$(@:dll=lib) /OUT:$@ /PDB:$(@:dll=pdb) /BASE:$(BASE_$@) /DEF:$(DEF_$@)
  
  DEF_opengl32.w95.dll = $(MESA_VER)/src/gallium/state_trackers/wgl/opengl32.def
  DEF_opengl32.w98me.dll = $(DEF_opengl32.w95.dll)
  
  DEF_mesa3d.w95.dll = mesa3d.def
  DEF_mesa3d.w98me.dll = $(DEF_mesa3d.w95.dll)
	
  LIBSTATIC = LIB.EXE /nologo /OUT:$@ 
	
else
#
# MinGW configurationn
#
  OBJ := .o
  LIBSUFFIX := .a
  LIBPREFIX := lib
  
  LD_DEPS := winpthreads/libpthread.a
  
  DLLFLAGS = -o $@ -shared -Wl,--dll,--out-implib,lib$(@:dll=a),--exclude-libs=pthread,--image-base,$(BASE_$@)$(TUNE_LD)
  
  OPENGL_DEF = $(MESA_VER)/src/gallium/state_trackers/wgl/opengl32.mingw.def
  MESA3D_DEF = mesa3d.mingw.def

  INCLUDE = -Iinclude -Iwinpthreads/include -I$(MESA_VER)/include	-I$(MESA_VER)/include/GL -I$(MESA_VER)/src/mapi	-I$(MESA_VER)/src/util -I$(MESA_VER)/src -I$(MESA_VER)/src/mesa -I$(MESA_VER)/src/mesa/main \
    -I$(MESA_VER)/src/compiler -I$(MESA_VER)/src/compiler/nir -I$(MESA_VER)/src/gallium/state_trackers/wgl -I$(MESA_VER)/src/gallium/auxiliary -I$(MESA_VER)/src/gallium/include \
    -I$(MESA_VER)/src/gallium/drivers/svga -I$(MESA_VER)/src/gallium/drivers/svga/include -I$(MESA_VER)/src/gallium/winsys/sw  -I$(MESA_VER)/src/gallium/drivers -I$(MESA_VER)/src/gallium/winsys/svga/drm \
    -Iwin9x

  DEFS =  -D__i386__ -D_X86_ -D_WIN32 -DWIN32 -DWIN9X -DWINVER=0x0400 -DHAVE_PTHREAD \
    -DBUILD_GL32 -D_GDI32_ -DGL_API=GLAPI -DGL_APIENTRY=GLAPIENTRY \
    -D_FILE_OFFSET_BITS=64 -D_LARGEFILE_SOURCE \
    -DMAPI_MODE_UTIL -D_GLAPI_NO_EXPORTS -DCOBJMACROS -DINC_OLE2 \
    -DPACKAGE_VERSION="\"$(MESA_VER)\"" -DPACKAGE_BUGREPORT="\"$(MESA_VER)\"" -DMALLOC_IS_ALIGNED -DHAVE_CRTEX
  
  DEFS += -DVBOX_WITH_MESA3D_STENCIL_CLEAR -DVBOX_WITH_MESA3D_DXFIX
  #DEFS += -DVBOX_WITH_MESA3D_NINE_SVGA -DVBOX_WITH_MESA3D_SVGA_HALFZ -DVBOX_WITH_MESA3D_SVGA_INSTANCING -DVBOX_WITH_MESA3D_SVGA_GPU_FINISHED

  ifdef VERSION_BUILD
    DEFS  += -DMESA9X_BUILD=$(VERSION_BUILD)
  endif

	OPENGL_LIBS = -L. -lMesaLib -lMesaUtilLib -lMesaGalliumAuxLib
	SVGA_LIBS   = -L. -lMesaLib -lMesaUtilLib -lMesaGalliumAuxLib -lMesaSVGALib
  MESA_LIBS  := winpthreads/crtfix.o -static -Lwinpthreads -lpthread -lkernel32 -luser32 -lgdi32
  
  ifdef DEBUG
    DD_DEFS = -DDEBUG
  else
    DD_DEFS = -DNDEBUG
  endif
  
  ifdef GUI_ERRORS
  	TUNE += -DGUI_ERRORS
  endif

  ifdef LLVM
    SIMD_DEFS = $(DEFS) -DHAVE_LLVM=$(LLVM_VER) -DHAVE_GALLIUM_LLVMPIPE -DGALLIUM_LLVMPIPE -DHAVE_LLVMPIPE
    SIMD_INCLUDE += $(INCLUDE) -I$(LLVM_DIR)/include
    
    opengl_simd_LIBS := -L. -L$(LLVM_DIR)/lib -lMesaLibSimd -lMesaUtilLibSimd -lMesaGalliumLLVMPipe -lMesaGalliumAuxLibSimd
    opengl_simd_LIBS := $(opengl_simd_LIBS) $(filter-out -lshell32,$(shell $(LLVM_DIR)/bin/llvm-config --libs --link-static bitwriter engine mcdisassembler mcjit))
    
    # LLVMpipe 6.x required zlib
    MESA_SIMD_LIBS := $(MESA_LIBS) -lpsapi -lole32 -lz
    
    LIBS_TO_BUILD += $(LIBPREFIX)MesaGalliumLLVMPipe$(LIBSUFFIX)
    LIBS_TO_BUILD += $(LIBPREFIX)MesaUtilLibSimd$(LIBSUFFIX)
    LIBS_TO_BUILD += $(LIBPREFIX)MesaLibSimd$(LIBSUFFIX)
    LIBS_TO_BUILD += $(LIBPREFIX)MesaGalliumAuxLibSimd$(LIBSUFFIX)
    LIBS_TO_BUILD += $(LIBPREFIX)MesaGdiLibGLSimd$(LIBSUFFIX)
    LIBS_TO_BUILD += $(LIBPREFIX)MesaGdiLibICDSimd$(LIBSUFFIX)
    LIBS_TO_BUILD += $(LIBPREFIX)MesaWglLibSimd$(LIBSUFFIX)
    
    # for LLVM we need same cflags as LLVM itself
    LLVM_CFLAGS = $(shell $(LLVM_DIR)/bin/llvm-config --cflags)
    LLVM_CXXFLAGS = $(shell $(LLVM_DIR)/bin/llvm-config --cxxflags)
    ifndef LP_DEBUG
      SIMD_CFLAGS   = -std=$(CSTD) $(filter-out -pedantic -Wall -W -Wextra -march=westmere -march=core2,$(LLVM_CFLAGS)) $(TUNE) $(SIMD_INCLUDE) $(SIMD_DEFS)
      SIMD_CXXFLAGS = $(filter-out -pedantic -pedantic -Wall -W -Wextra -march=westmere -march=core2,$(LLVM_CXXFLAGS)) $(TUNE) $(SIMD_INCLUDE) $(SIMD_DEFS)
    else
      SIMD_CFLAGS = -std=$(CSTD) -O1 -g  $(TUNE) $(SIMD_INCLUDE) $(SIMD_DEFS) $(DD_DEFS)
      SIMD_CXXFLAGS = -std=$(CXXSTD) -O1 -g $(TUNE) $(SIMD_INCLUDE) $(SIMD_DEFS) $(DD_DEFS)
    endif
    SIMD_LDFLAGS = -std=$(CXXSTD)
  endif
  
  ifdef SPEED
    CFLAGS = -std=$(CSTD) -O3 -fomit-frame-pointer -fno-exceptions $(TUNE) $(INCLUDE) -DNDEBUG $(DEFS)
    CXXFLAGS = -std=$(CXXSTD) -O3 -fomit-frame-pointer -fno-exceptions -fno-rtti $(TUNE) $(INCLUDE) -DNDEBUG $(DEFS)
    LDFLAGS = -std=$(CXXSTD) -O3 -fno-exceptions
  else
    CFLAGS = -std=$(CSTD) -O0 -g $(TUNE) $(INCLUDE) $(DD_DEFS) $(DEFS)
    CXXFLAGS = -std=$(CXXSTD) -O0 -g  $(TUNE) $(INCLUDE) $(DD_DEFS) $(DEFS)
    LDFLAGS = -std=$(CXXSTD)
  endif

  ifdef SPEED
    APP_CFLAGS   = -std=$(CSTD) -O3 -fomit-frame-pointer -fno-exceptions -I. -Iglchecker $(TUNE) $(INCLUDE) -DNDEBUG $(DEFS)
    APP_CXXFLAGS = -std=$(CXXSTD) -O3 -fomit-frame-pointer -fno-exceptions -fno-rtti $(INCLUDE) -DNDEBUG $(DEFS)
    APP_LDFLAGS  = -std=$(CXXSTD) -O3 -fno-exceptions 
  else
    APP_CFLAGS   = -std=$(CSTD) -O0 -g -I. -Iglchecker $(TUNE) $(INCLUDE) $(DD_DEFS) $(DEFS)
    APP_CXXFLAGS = -std=$(CXXSTD) -O0 -g $(TUNE) $(INCLUDE) $(DD_DEFS) $(DEFS)
    APP_LDFLAGS  = -std=$(CXXSTD) -O0 -g
  endif

  app_LIBS  = winpthreads/crtfix.o -static -Lwinpthreads -lpthread -lopengl32 -lgdi32
  EXEFLAGS_WIN = -o $@ -Wl,-subsystem,windows$(TUNE_LD)
  EXEFLAGS_CMD = -o $@ -Wl,-subsystem,console$(TUNE_LD)
  
  ifdef LTO
    CFLAGS       += -flto -fno-fat-lto-objects  -Werror=implicit-function-declaration
    CXXFLAGS     += -flto -fno-fat-lto-objects
    ifdef LLVM
      LDFLAGS      += $(LLVM_CXXFLAGS) -flto -fno-fat-lto-objects -fno-strict-aliasing
    else
      LDFLAGS      += -flto -fno-fat-lto-objects -fno-strict-aliasing
    endif
    
    CFLAGS_APP   += -flto -fno-fat-lto-objects
    CXXFLAGS_APP += -flto -fno-fat-lto-objects
    LDFLAGS_APP  += -flto -fno-fat-lto-objects
  endif
		
  %.c_gen.o: %.c $(DEPS)
		$(CC) $(CFLAGS) -c -o $@ $<
		
  %.cpp_gen.o: %.cpp $(DEPS)
		$(CXX) $(CXXFLAGS) -c -o $@ $<
	
  %.c_simd.o: %.c $(DEPS)
		$(CC) $(SIMD_CFLAGS) -c -o $@ $<
		
  %.cpp_simd.o: %.cpp $(DEPS)
		$(CXX) $(SIMD_CXXFLAGS) -c -o $@ $<
	
  %.c_app.o: %.c $(DEPS)
		$(CC) $(APP_CFLAGS) -c -o $@ $<
		
  %.cpp_app.o: %.cpp $(DEPS)
		$(CXX) $(APP_CXXFLAGS) -c -o $@ $<
		
  %.res: %.rc $(DEPS)
		$(WINDRES) -DWINDRES $(DEFS) --input $< --output $@ --output-format=coff
	
  LIBSTATIC = ar rcs -o $@ 

endif

%.asm:

%.asm$(OBJ): %.asm $(DEPS)
	nasm $< -f win32 -o $@
	
winpthreads/crtfix$(OBJ): $(DEPS) winpthreads/Makefile pthread.mk
	cd winpthreads && $(MAKE)

winpthreads/$(LIBPREFIX)pthread$(LIBSUFFIX): $(DEPS) winpthreads/Makefile pthread.mk
	cd winpthreads && $(MAKE)

LIBS_TO_BUILD += $(LIBPREFIX)MesaUtilLib$(LIBSUFFIX)
LIBS_TO_BUILD += $(LIBPREFIX)MesaLib$(LIBSUFFIX)
LIBS_TO_BUILD += $(LIBPREFIX)MesaGalliumAuxLib$(LIBSUFFIX)
LIBS_TO_BUILD += $(LIBPREFIX)MesaGdiLibGL$(LIBSUFFIX)
LIBS_TO_BUILD += $(LIBPREFIX)MesaGdiLibICD$(LIBSUFFIX)
LIBS_TO_BUILD += $(LIBPREFIX)MesaWglLib$(LIBSUFFIX)

MesaUtilLib_SRC  = \
	$(MESA_VER)/src/util/bitscan.c \
	$(MESA_VER)/src/util/build_id.c \
	$(MESA_VER)/src/util/crc32.c \
	$(MESA_VER)/src/util/debug.c \
	$(MESA_VER)/src/util/disk_cache.c \
	$(MESA_VER)/src/util/half_float.c \
	$(MESA_VER)/src/util/hash_table.c \
	$(MESA_VER)/src/util/ralloc.c \
	$(MESA_VER)/src/util/rand_xor.c \
	$(MESA_VER)/src/util/register_allocate.c \
	$(MESA_VER)/src/util/rgtc.c \
	$(MESA_VER)/src/util/set.c \
	$(MESA_VER)/src/util/mesa-sha1.c \
	$(MESA_VER)/src/util/sha1/sha1.c \
	$(MESA_VER)/src/util/slab.c \
	$(MESA_VER)/src/util/string_buffer.c \
	$(MESA_VER)/src/util/strtod.c \
	$(MESA_VER)/src/util/u_atomic.c \
	$(MESA_VER)/src/util/u_queue.c \
	$(MESA_VER)/src/util/u_vector.c
# Auto-generated
MesaUtilLib_SRC += \
	$(MESA_VER)/src/util/format_srgb.c

MesaLib_SRC  = \
	$(MESA_VER)/src/mesa/state_tracker/st_atifs_to_tgsi.c \
	$(MESA_VER)/src/mesa/state_tracker/st_atom_array.c \
	$(MESA_VER)/src/mesa/state_tracker/st_atom_atomicbuf.c \
	$(MESA_VER)/src/mesa/state_tracker/st_atom_blend.c \
	$(MESA_VER)/src/mesa/state_tracker/st_atom.c \
	$(MESA_VER)/src/mesa/state_tracker/st_atom_clip.c \
	$(MESA_VER)/src/mesa/state_tracker/st_atom_constbuf.c \
	$(MESA_VER)/src/mesa/state_tracker/st_atom_depth.c \
	$(MESA_VER)/src/mesa/state_tracker/st_atom_framebuffer.c \
	$(MESA_VER)/src/mesa/state_tracker/st_atom_image.c \
	$(MESA_VER)/src/mesa/state_tracker/st_atom_msaa.c \
	$(MESA_VER)/src/mesa/state_tracker/st_atom_pixeltransfer.c \
	$(MESA_VER)/src/mesa/state_tracker/st_atom_rasterizer.c \
	$(MESA_VER)/src/mesa/state_tracker/st_atom_sampler.c \
	$(MESA_VER)/src/mesa/state_tracker/st_atom_scissor.c \
	$(MESA_VER)/src/mesa/state_tracker/st_atom_shader.c \
	$(MESA_VER)/src/mesa/state_tracker/st_atom_stipple.c \
	$(MESA_VER)/src/mesa/state_tracker/st_atom_storagebuf.c \
	$(MESA_VER)/src/mesa/state_tracker/st_atom_tess.c \
	$(MESA_VER)/src/mesa/state_tracker/st_atom_texture.c \
	$(MESA_VER)/src/mesa/state_tracker/st_atom_viewport.c \
	$(MESA_VER)/src/mesa/state_tracker/st_cb_bitmap.c \
	$(MESA_VER)/src/mesa/state_tracker/st_cb_bitmap_shader.c \
	$(MESA_VER)/src/mesa/state_tracker/st_cb_blit.c \
	$(MESA_VER)/src/mesa/state_tracker/st_cb_bufferobjects.c \
	$(MESA_VER)/src/mesa/state_tracker/st_cb_clear.c \
	$(MESA_VER)/src/mesa/state_tracker/st_cb_compute.c \
	$(MESA_VER)/src/mesa/state_tracker/st_cb_condrender.c \
	$(MESA_VER)/src/mesa/state_tracker/st_cb_copyimage.c \
	$(MESA_VER)/src/mesa/state_tracker/st_cb_drawpixels.c \
	$(MESA_VER)/src/mesa/state_tracker/st_cb_drawpixels_shader.c \
	$(MESA_VER)/src/mesa/state_tracker/st_cb_drawtex.c \
	$(MESA_VER)/src/mesa/state_tracker/st_cb_eglimage.c \
	$(MESA_VER)/src/mesa/state_tracker/st_cb_fbo.c \
	$(MESA_VER)/src/mesa/state_tracker/st_cb_feedback.c \
	$(MESA_VER)/src/mesa/state_tracker/st_cb_flush.c \
	$(MESA_VER)/src/mesa/state_tracker/st_cb_memoryobjects.c \
	$(MESA_VER)/src/mesa/state_tracker/st_cb_msaa.c \
	$(MESA_VER)/src/mesa/state_tracker/st_cb_perfmon.c \
	$(MESA_VER)/src/mesa/state_tracker/st_cb_program.c \
	$(MESA_VER)/src/mesa/state_tracker/st_cb_queryobj.c \
	$(MESA_VER)/src/mesa/state_tracker/st_cb_rasterpos.c \
	$(MESA_VER)/src/mesa/state_tracker/st_cb_readpixels.c \
	$(MESA_VER)/src/mesa/state_tracker/st_cb_strings.c \
	$(MESA_VER)/src/mesa/state_tracker/st_cb_syncobj.c \
	$(MESA_VER)/src/mesa/state_tracker/st_cb_texturebarrier.c \
	$(MESA_VER)/src/mesa/state_tracker/st_cb_texture.c \
	$(MESA_VER)/src/mesa/state_tracker/st_cb_viewport.c \
	$(MESA_VER)/src/mesa/state_tracker/st_cb_xformfb.c \
	$(MESA_VER)/src/mesa/state_tracker/st_context.c \
	$(MESA_VER)/src/mesa/state_tracker/st_copytex.c \
	$(MESA_VER)/src/mesa/state_tracker/st_debug.c \
	$(MESA_VER)/src/mesa/state_tracker/st_draw.c \
	$(MESA_VER)/src/mesa/state_tracker/st_draw_feedback.c \
	$(MESA_VER)/src/mesa/state_tracker/st_extensions.c \
	$(MESA_VER)/src/mesa/state_tracker/st_format.c \
	$(MESA_VER)/src/mesa/state_tracker/st_gen_mipmap.c \
	$(MESA_VER)/src/mesa/state_tracker/st_glsl_to_nir.cpp \
	$(MESA_VER)/src/mesa/state_tracker/st_glsl_to_tgsi.cpp \
	$(MESA_VER)/src/mesa/state_tracker/st_glsl_to_tgsi_private.cpp \
	$(MESA_VER)/src/mesa/state_tracker/st_glsl_to_tgsi_temprename.cpp \
	$(MESA_VER)/src/mesa/state_tracker/st_glsl_types.cpp \
	$(MESA_VER)/src/mesa/state_tracker/st_manager.c \
	$(MESA_VER)/src/mesa/state_tracker/st_mesa_to_tgsi.c \
	$(MESA_VER)/src/mesa/state_tracker/st_nir_lower_builtin.c \
	$(MESA_VER)/src/mesa/state_tracker/st_nir_lower_tex_src_plane.c \
	$(MESA_VER)/src/mesa/state_tracker/st_pbo.c \
	$(MESA_VER)/src/mesa/state_tracker/st_program.c \
	$(MESA_VER)/src/mesa/state_tracker/st_sampler_view.c \
	$(MESA_VER)/src/mesa/state_tracker/st_scissor.c \
	$(MESA_VER)/src/mesa/state_tracker/st_shader_cache.c \
	$(MESA_VER)/src/mesa/state_tracker/st_texture.c \
	$(MESA_VER)/src/mesa/state_tracker/st_tgsi_lower_yuv.c \
	$(MESA_VER)/src/mesa/state_tracker/st_vdpau.c
MesaLib_SRC += \
	$(MESA_VER)/src/mesa/program/arbprogparse.c \
	$(MESA_VER)/src/mesa/program/ir_to_mesa.cpp \
	$(MESA_VER)/src/mesa/program/lex.yy.c \
	$(MESA_VER)/src/mesa/program/prog_cache.c \
	$(MESA_VER)/src/mesa/program/prog_execute.c \
	$(MESA_VER)/src/mesa/program/prog_instruction.c \
	$(MESA_VER)/src/mesa/program/prog_noise.c \
	$(MESA_VER)/src/mesa/program/prog_opt_constant_fold.c \
	$(MESA_VER)/src/mesa/program/prog_optimize.c \
	$(MESA_VER)/src/mesa/program/prog_parameter.c \
	$(MESA_VER)/src/mesa/program/prog_parameter_layout.c \
	$(MESA_VER)/src/mesa/program/prog_print.c \
	$(MESA_VER)/src/mesa/program/program.c \
	$(MESA_VER)/src/mesa/program/programopt.c \
	$(MESA_VER)/src/mesa/program/program_parse_extra.c \
	$(MESA_VER)/src/mesa/program/program_parse.tab.c \
	$(MESA_VER)/src/mesa/program/prog_statevars.c \
	$(MESA_VER)/src/mesa/program/symbol_table.c
MesaLib_SRC += \
	$(MESA_VER)/src/mesa/main/accum.c \
	$(MESA_VER)/src/mesa/main/api_arrayelt.c \
	$(MESA_VER)/src/mesa/main/api_exec.c \
	$(MESA_VER)/src/mesa/main/api_loopback.c \
	$(MESA_VER)/src/mesa/main/api_validate.c \
	$(MESA_VER)/src/mesa/main/arbprogram.c \
	$(MESA_VER)/src/mesa/main/arrayobj.c \
	$(MESA_VER)/src/mesa/main/atifragshader.c \
	$(MESA_VER)/src/mesa/main/attrib.c \
	$(MESA_VER)/src/mesa/main/barrier.c \
	$(MESA_VER)/src/mesa/main/bbox.c \
	$(MESA_VER)/src/mesa/main/blend.c \
	$(MESA_VER)/src/mesa/main/blit.c \
	$(MESA_VER)/src/mesa/main/bufferobj.c \
	$(MESA_VER)/src/mesa/main/buffers.c \
	$(MESA_VER)/src/mesa/main/clear.c \
	$(MESA_VER)/src/mesa/main/clip.c \
	$(MESA_VER)/src/mesa/main/colortab.c \
	$(MESA_VER)/src/mesa/main/compute.c \
	$(MESA_VER)/src/mesa/main/condrender.c \
	$(MESA_VER)/src/mesa/main/context.c \
	$(MESA_VER)/src/mesa/main/convolve.c \
	$(MESA_VER)/src/mesa/main/copyimage.c \
	$(MESA_VER)/src/mesa/main/cpuinfo.c \
	$(MESA_VER)/src/mesa/main/debug.c \
	$(MESA_VER)/src/mesa/main/debug_output.c \
	$(MESA_VER)/src/mesa/main/depth.c \
	$(MESA_VER)/src/mesa/main/dlist.c \
	$(MESA_VER)/src/mesa/main/drawpix.c \
	$(MESA_VER)/src/mesa/main/drawtex.c \
	$(MESA_VER)/src/mesa/main/enable.c \
	$(MESA_VER)/src/mesa/main/enums.c \
	$(MESA_VER)/src/mesa/main/errors.c \
	$(MESA_VER)/src/mesa/main/eval.c \
	$(MESA_VER)/src/mesa/main/execmem.c \
	$(MESA_VER)/src/mesa/main/extensions.c \
	$(MESA_VER)/src/mesa/main/extensions_table.c \
	$(MESA_VER)/src/mesa/main/externalobjects.c \
	$(MESA_VER)/src/mesa/main/fbobject.c \
	$(MESA_VER)/src/mesa/main/feedback.c \
	$(MESA_VER)/src/mesa/main/ff_fragment_shader.cpp \
	$(MESA_VER)/src/mesa/main/ffvertex_prog.c \
	$(MESA_VER)/src/mesa/main/fog.c \
	$(MESA_VER)/src/mesa/main/format_fallback.c \
	$(MESA_VER)/src/mesa/main/format_pack.c \
	$(MESA_VER)/src/mesa/main/format_unpack.c \
	$(MESA_VER)/src/mesa/main/formatquery.c \
	$(MESA_VER)/src/mesa/main/formats.c \
	$(MESA_VER)/src/mesa/main/format_utils.c \
	$(MESA_VER)/src/mesa/main/framebuffer.c \
	$(MESA_VER)/src/mesa/main/get.c \
	$(MESA_VER)/src/mesa/main/genmipmap.c \
	$(MESA_VER)/src/mesa/main/getstring.c \
	$(MESA_VER)/src/mesa/main/glformats.c \
	$(MESA_VER)/src/mesa/main/glthread.c \
	$(MESA_VER)/src/mesa/main/hash.c \
	$(MESA_VER)/src/mesa/main/hint.c \
	$(MESA_VER)/src/mesa/main/histogram.c \
	$(MESA_VER)/src/mesa/main/image.c \
	$(MESA_VER)/src/mesa/main/imports.c \
	$(MESA_VER)/src/mesa/main/light.c \
	$(MESA_VER)/src/mesa/main/lines.c \
	$(MESA_VER)/src/mesa/main/marshal.c \
	$(MESA_VER)/src/mesa/main/marshal_generated.c \
	$(MESA_VER)/src/mesa/main/matrix.c \
	$(MESA_VER)/src/mesa/main/mipmap.c \
	$(MESA_VER)/src/mesa/main/mm.c \
	$(MESA_VER)/src/mesa/main/multisample.c \
	$(MESA_VER)/src/mesa/main/objectlabel.c \
	$(MESA_VER)/src/mesa/main/objectpurge.c \
	$(MESA_VER)/src/mesa/main/pack.c \
	$(MESA_VER)/src/mesa/main/pbo.c \
	$(MESA_VER)/src/mesa/main/performance_monitor.c \
	$(MESA_VER)/src/mesa/main/performance_query.c \
	$(MESA_VER)/src/mesa/main/pipelineobj.c \
	$(MESA_VER)/src/mesa/main/pixel.c \
	$(MESA_VER)/src/mesa/main/pixelstore.c \
	$(MESA_VER)/src/mesa/main/pixeltransfer.c \
	$(MESA_VER)/src/mesa/main/points.c \
	$(MESA_VER)/src/mesa/main/polygon.c \
	$(MESA_VER)/src/mesa/main/program_resource.c \
	$(MESA_VER)/src/mesa/main/querymatrix.c \
	$(MESA_VER)/src/mesa/main/queryobj.c \
	$(MESA_VER)/src/mesa/main/rastpos.c \
	$(MESA_VER)/src/mesa/main/readpix.c \
	$(MESA_VER)/src/mesa/main/remap.c \
	$(MESA_VER)/src/mesa/main/renderbuffer.c \
	$(MESA_VER)/src/mesa/main/robustness.c \
	$(MESA_VER)/src/mesa/main/samplerobj.c \
	$(MESA_VER)/src/mesa/main/scissor.c \
	$(MESA_VER)/src/mesa/main/shaderapi.c \
	$(MESA_VER)/src/mesa/main/shaderimage.c \
	$(MESA_VER)/src/mesa/main/shaderobj.c \
	$(MESA_VER)/src/mesa/main/shader_query.cpp \
	$(MESA_VER)/src/mesa/main/shared.c \
	$(MESA_VER)/src/mesa/main/state.c \
	$(MESA_VER)/src/mesa/main/stencil.c \
	$(MESA_VER)/src/mesa/main/syncobj.c \
	$(MESA_VER)/src/mesa/main/texcompress.c \
	$(MESA_VER)/src/mesa/main/texcompress_bptc.c \
	$(MESA_VER)/src/mesa/main/texcompress_cpal.c \
	$(MESA_VER)/src/mesa/main/texcompress_etc.c \
	$(MESA_VER)/src/mesa/main/texcompress_fxt1.c \
	$(MESA_VER)/src/mesa/main/texcompress_rgtc.c \
	$(MESA_VER)/src/mesa/main/texcompress_s3tc.c \
	$(MESA_VER)/src/mesa/main/texenv.c \
	$(MESA_VER)/src/mesa/main/texformat.c \
	$(MESA_VER)/src/mesa/main/texgen.c \
	$(MESA_VER)/src/mesa/main/texgetimage.c \
	$(MESA_VER)/src/mesa/main/teximage.c \
	$(MESA_VER)/src/mesa/main/texobj.c \
	$(MESA_VER)/src/mesa/main/texparam.c \
	$(MESA_VER)/src/mesa/main/texstate.c \
	$(MESA_VER)/src/mesa/main/texstorage.c \
	$(MESA_VER)/src/mesa/main/texstore.c \
	$(MESA_VER)/src/mesa/main/texturebindless.c \
	$(MESA_VER)/src/mesa/main/textureview.c \
	$(MESA_VER)/src/mesa/main/transformfeedback.c \
	$(MESA_VER)/src/mesa/main/uniform_query.cpp \
	$(MESA_VER)/src/mesa/main/uniforms.c \
	$(MESA_VER)/src/mesa/main/varray.c \
	$(MESA_VER)/src/mesa/main/vdpau.c \
	$(MESA_VER)/src/mesa/main/version.c \
	$(MESA_VER)/src/mesa/main/viewport.c \
	$(MESA_VER)/src/mesa/main/vtxfmt.c \
	$(MESA_VER)/src/mesa/main/es1_conversion.c
MesaLib_SRC += \
	$(MESA_VER)/src/mesa/math/m_debug_clip.c \
	$(MESA_VER)/src/mesa/math/m_debug_norm.c \
	$(MESA_VER)/src/mesa/math/m_debug_xform.c \
	$(MESA_VER)/src/mesa/math/m_eval.c \
	$(MESA_VER)/src/mesa/math/m_matrix.c \
	$(MESA_VER)/src/mesa/math/m_translate.c \
	$(MESA_VER)/src/mesa/math/m_vector.c
MesaLib_SRC += \
	$(MESA_VER)/src/mesa/vbo/vbo_context.c \
	$(MESA_VER)/src/mesa/vbo/vbo_exec_api.c \
	$(MESA_VER)/src/mesa/vbo/vbo_exec_array.c \
	$(MESA_VER)/src/mesa/vbo/vbo_exec.c \
	$(MESA_VER)/src/mesa/vbo/vbo_exec_draw.c \
	$(MESA_VER)/src/mesa/vbo/vbo_exec_eval.c \
	$(MESA_VER)/src/mesa/vbo/vbo_minmax_index.c \
	$(MESA_VER)/src/mesa/vbo/vbo_noop.c \
	$(MESA_VER)/src/mesa/vbo/vbo_primitive_restart.c \
	$(MESA_VER)/src/mesa/vbo/vbo_rebase.c \
	$(MESA_VER)/src/mesa/vbo/vbo_save_api.c \
	$(MESA_VER)/src/mesa/vbo/vbo_save.c \
	$(MESA_VER)/src/mesa/vbo/vbo_save_draw.c \
	$(MESA_VER)/src/mesa/vbo/vbo_save_loopback.c \
	$(MESA_VER)/src/mesa/vbo/vbo_split.c \
	$(MESA_VER)/src/mesa/vbo/vbo_split_copy.c \
	$(MESA_VER)/src/mesa/vbo/vbo_split_inplace.c
MesaLib_SRC += \
	$(MESA_VER)/src/compiler/blob.c \
	$(MESA_VER)/src/compiler/glsl_types.cpp \
	$(MESA_VER)/src/compiler/nir_types.cpp \
	$(MESA_VER)/src/compiler/shader_enums.c \
	$(MESA_VER)/src/compiler/glsl/ast_array_index.cpp \
	$(MESA_VER)/src/compiler/glsl/ast_expr.cpp \
	$(MESA_VER)/src/compiler/glsl/ast_function.cpp \
	$(MESA_VER)/src/compiler/glsl/ast_to_hir.cpp \
	$(MESA_VER)/src/compiler/glsl/ast_type.cpp \
	$(MESA_VER)/src/compiler/glsl/builtin_functions.cpp \
	$(MESA_VER)/src/compiler/glsl/builtin_types.cpp \
	$(MESA_VER)/src/compiler/glsl/builtin_variables.cpp \
	$(MESA_VER)/src/compiler/glsl/generate_ir.cpp \
	$(MESA_VER)/src/compiler/glsl/glsl_parser_extras.cpp \
	$(MESA_VER)/src/compiler/glsl/glsl_symbol_table.cpp \
	$(MESA_VER)/src/compiler/glsl/glsl_to_nir.cpp \
	$(MESA_VER)/src/compiler/glsl/hir_field_selection.cpp \
	$(MESA_VER)/src/compiler/glsl/ir_array_refcount.cpp \
	$(MESA_VER)/src/compiler/glsl/ir_basic_block.cpp \
	$(MESA_VER)/src/compiler/glsl/ir_builder.cpp \
	$(MESA_VER)/src/compiler/glsl/ir_clone.cpp \
	$(MESA_VER)/src/compiler/glsl/ir_constant_expression.cpp \
	$(MESA_VER)/src/compiler/glsl/ir.cpp \
	$(MESA_VER)/src/compiler/glsl/ir_equals.cpp \
	$(MESA_VER)/src/compiler/glsl/ir_expression_flattening.cpp \
	$(MESA_VER)/src/compiler/glsl/ir_function_can_inline.cpp \
	$(MESA_VER)/src/compiler/glsl/ir_function_detect_recursion.cpp \
	$(MESA_VER)/src/compiler/glsl/ir_function.cpp \
	$(MESA_VER)/src/compiler/glsl/ir_hierarchical_visitor.cpp \
	$(MESA_VER)/src/compiler/glsl/ir_hv_accept.cpp \
	$(MESA_VER)/src/compiler/glsl/ir_print_visitor.cpp \
	$(MESA_VER)/src/compiler/glsl/ir_reader.cpp \
	$(MESA_VER)/src/compiler/glsl/ir_rvalue_visitor.cpp \
	$(MESA_VER)/src/compiler/glsl/ir_set_program_inouts.cpp \
	$(MESA_VER)/src/compiler/glsl/ir_validate.cpp \
	$(MESA_VER)/src/compiler/glsl/ir_variable_refcount.cpp \
	$(MESA_VER)/src/compiler/glsl/linker.cpp \
	$(MESA_VER)/src/compiler/glsl/link_atomics.cpp \
	$(MESA_VER)/src/compiler/glsl/link_functions.cpp \
	$(MESA_VER)/src/compiler/glsl/link_interface_blocks.cpp \
	$(MESA_VER)/src/compiler/glsl/link_uniforms.cpp \
	$(MESA_VER)/src/compiler/glsl/link_uniform_initializers.cpp \
	$(MESA_VER)/src/compiler/glsl/link_uniform_block_active_visitor.cpp \
	$(MESA_VER)/src/compiler/glsl/link_uniform_blocks.cpp \
	$(MESA_VER)/src/compiler/glsl/link_varyings.cpp \
	$(MESA_VER)/src/compiler/glsl/loop_analysis.cpp \
	$(MESA_VER)/src/compiler/glsl/loop_unroll.cpp \
	$(MESA_VER)/src/compiler/glsl/lower_blend_equation_advanced.cpp \
	$(MESA_VER)/src/compiler/glsl/lower_buffer_access.cpp \
	$(MESA_VER)/src/compiler/glsl/lower_const_arrays_to_uniforms.cpp \
	$(MESA_VER)/src/compiler/glsl/lower_cs_derived.cpp \
	$(MESA_VER)/src/compiler/glsl/lower_discard.cpp \
	$(MESA_VER)/src/compiler/glsl/lower_discard_flow.cpp \
	$(MESA_VER)/src/compiler/glsl/lower_distance.cpp \
	$(MESA_VER)/src/compiler/glsl/lower_if_to_cond_assign.cpp \
	$(MESA_VER)/src/compiler/glsl/lower_instructions.cpp \
	$(MESA_VER)/src/compiler/glsl/lower_int64.cpp \
	$(MESA_VER)/src/compiler/glsl/lower_jumps.cpp \
	$(MESA_VER)/src/compiler/glsl/lower_mat_op_to_vec.cpp \
	$(MESA_VER)/src/compiler/glsl/lower_noise.cpp \
	$(MESA_VER)/src/compiler/glsl/lower_offset_array.cpp \
	$(MESA_VER)/src/compiler/glsl/lower_packed_varyings.cpp \
	$(MESA_VER)/src/compiler/glsl/lower_named_interface_blocks.cpp \
	$(MESA_VER)/src/compiler/glsl/lower_packing_builtins.cpp \
	$(MESA_VER)/src/compiler/glsl/lower_subroutine.cpp \
	$(MESA_VER)/src/compiler/glsl/lower_tess_level.cpp \
	$(MESA_VER)/src/compiler/glsl/lower_texture_projection.cpp \
	$(MESA_VER)/src/compiler/glsl/lower_variable_index_to_cond_assign.cpp \
	$(MESA_VER)/src/compiler/glsl/lower_vec_index_to_cond_assign.cpp \
	$(MESA_VER)/src/compiler/glsl/lower_vec_index_to_swizzle.cpp \
	$(MESA_VER)/src/compiler/glsl/lower_vector.cpp \
	$(MESA_VER)/src/compiler/glsl/lower_vector_derefs.cpp \
	$(MESA_VER)/src/compiler/glsl/lower_vector_insert.cpp \
	$(MESA_VER)/src/compiler/glsl/lower_vertex_id.cpp \
	$(MESA_VER)/src/compiler/glsl/lower_output_reads.cpp \
	$(MESA_VER)/src/compiler/glsl/lower_shared_reference.cpp \
	$(MESA_VER)/src/compiler/glsl/lower_ubo_reference.cpp \
	$(MESA_VER)/src/compiler/glsl/opt_algebraic.cpp \
	$(MESA_VER)/src/compiler/glsl/opt_array_splitting.cpp \
	$(MESA_VER)/src/compiler/glsl/opt_conditional_discard.cpp \
	$(MESA_VER)/src/compiler/glsl/opt_constant_folding.cpp \
	$(MESA_VER)/src/compiler/glsl/opt_constant_propagation.cpp \
	$(MESA_VER)/src/compiler/glsl/opt_constant_variable.cpp \
	$(MESA_VER)/src/compiler/glsl/opt_copy_propagation.cpp \
	$(MESA_VER)/src/compiler/glsl/opt_copy_propagation_elements.cpp \
	$(MESA_VER)/src/compiler/glsl/opt_dead_builtin_variables.cpp \
	$(MESA_VER)/src/compiler/glsl/opt_dead_builtin_varyings.cpp \
	$(MESA_VER)/src/compiler/glsl/opt_dead_code.cpp \
	$(MESA_VER)/src/compiler/glsl/opt_dead_code_local.cpp \
	$(MESA_VER)/src/compiler/glsl/opt_dead_functions.cpp \
	$(MESA_VER)/src/compiler/glsl/opt_flatten_nested_if_blocks.cpp \
	$(MESA_VER)/src/compiler/glsl/opt_flip_matrices.cpp \
	$(MESA_VER)/src/compiler/glsl/opt_function_inlining.cpp \
	$(MESA_VER)/src/compiler/glsl/opt_if_simplification.cpp \
	$(MESA_VER)/src/compiler/glsl/opt_minmax.cpp \
	$(MESA_VER)/src/compiler/glsl/opt_noop_swizzle.cpp \
	$(MESA_VER)/src/compiler/glsl/opt_rebalance_tree.cpp \
	$(MESA_VER)/src/compiler/glsl/opt_redundant_jumps.cpp \
	$(MESA_VER)/src/compiler/glsl/opt_structure_splitting.cpp \
	$(MESA_VER)/src/compiler/glsl/opt_swizzle_swizzle.cpp \
	$(MESA_VER)/src/compiler/glsl/opt_tree_grafting.cpp \
	$(MESA_VER)/src/compiler/glsl/opt_vectorize.cpp \
	$(MESA_VER)/src/compiler/glsl/propagate_invariance.cpp \
	$(MESA_VER)/src/compiler/glsl/s_expression.cpp \
	$(MESA_VER)/src/compiler/glsl/string_to_uint_map.cpp \
	$(MESA_VER)/src/compiler/glsl/shader_cache.cpp \
	$(MESA_VER)/src/compiler/glsl/ir_builder_print_visitor.cpp \
	$(MESA_VER)/src/compiler/glsl/glsl_lexer.cpp \
	$(MESA_VER)/src/compiler/glsl/glsl_parser.cpp \
	$(MESA_VER)/src/compiler/glsl/glcpp/pp.c \
	$(MESA_VER)/src/compiler/glsl/glcpp/glcpp-lex.c \
	$(MESA_VER)/src/compiler/glsl/glcpp/glcpp-parse.c \
	$(MESA_VER)/src/compiler/nir/nir_constant_expressions.c \
	$(MESA_VER)/src/compiler/nir/nir_opcodes.c \
	$(MESA_VER)/src/compiler/nir/nir_opt_algebraic.c \
	$(MESA_VER)/src/compiler/nir/nir.c \
	$(MESA_VER)/src/compiler/nir/nir_clone.c \
	$(MESA_VER)/src/compiler/nir/nir_control_flow.c \
	$(MESA_VER)/src/compiler/nir/nir_dominance.c \
	$(MESA_VER)/src/compiler/nir/nir_from_ssa.c \
	$(MESA_VER)/src/compiler/nir/nir_gather_info.c \
	$(MESA_VER)/src/compiler/nir/nir_gs_count_vertices.c \
	$(MESA_VER)/src/compiler/nir/nir_inline_functions.c \
	$(MESA_VER)/src/compiler/nir/nir_instr_set.c \
	$(MESA_VER)/src/compiler/nir/nir_intrinsics.c \
	$(MESA_VER)/src/compiler/nir/nir_linking_helpers.c \
	$(MESA_VER)/src/compiler/nir/nir_liveness.c \
	$(MESA_VER)/src/compiler/nir/nir_loop_analyze.c \
	$(MESA_VER)/src/compiler/nir/nir_lower_64bit_packing.c \
	$(MESA_VER)/src/compiler/nir/nir_lower_alpha_test.c \
	$(MESA_VER)/src/compiler/nir/nir_lower_alu_to_scalar.c \
	$(MESA_VER)/src/compiler/nir/nir_lower_atomics.c \
	$(MESA_VER)/src/compiler/nir/nir_lower_atomics_to_ssbo.c \
	$(MESA_VER)/src/compiler/nir/nir_lower_bitmap.c \
	$(MESA_VER)/src/compiler/nir/nir_lower_clamp_color_outputs.c \
	$(MESA_VER)/src/compiler/nir/nir_lower_clip.c \
	$(MESA_VER)/src/compiler/nir/nir_lower_clip_cull_distance_arrays.c \
	$(MESA_VER)/src/compiler/nir/nir_lower_constant_initializers.c \
	$(MESA_VER)/src/compiler/nir/nir_lower_double_ops.c \
	$(MESA_VER)/src/compiler/nir/nir_lower_drawpixels.c \
	$(MESA_VER)/src/compiler/nir/nir_lower_global_vars_to_local.c \
	$(MESA_VER)/src/compiler/nir/nir_lower_gs_intrinsics.c \
	$(MESA_VER)/src/compiler/nir/nir_lower_load_const_to_scalar.c \
	$(MESA_VER)/src/compiler/nir/nir_lower_locals_to_regs.c \
	$(MESA_VER)/src/compiler/nir/nir_lower_idiv.c \
	$(MESA_VER)/src/compiler/nir/nir_lower_indirect_derefs.c \
	$(MESA_VER)/src/compiler/nir/nir_lower_int64.c \
	$(MESA_VER)/src/compiler/nir/nir_lower_io.c \
	$(MESA_VER)/src/compiler/nir/nir_lower_io_to_temporaries.c \
	$(MESA_VER)/src/compiler/nir/nir_lower_io_to_scalar.c \
	$(MESA_VER)/src/compiler/nir/nir_lower_io_types.c \
	$(MESA_VER)/src/compiler/nir/nir_lower_passthrough_edgeflags.c \
	$(MESA_VER)/src/compiler/nir/nir_lower_patch_vertices.c \
	$(MESA_VER)/src/compiler/nir/nir_lower_phis_to_scalar.c \
	$(MESA_VER)/src/compiler/nir/nir_lower_read_invocation_to_scalar.c \
	$(MESA_VER)/src/compiler/nir/nir_lower_regs_to_ssa.c \
	$(MESA_VER)/src/compiler/nir/nir_lower_returns.c \
	$(MESA_VER)/src/compiler/nir/nir_lower_samplers.c \
	$(MESA_VER)/src/compiler/nir/nir_lower_samplers_as_deref.c \
	$(MESA_VER)/src/compiler/nir/nir_lower_system_values.c \
	$(MESA_VER)/src/compiler/nir/nir_lower_tex.c \
	$(MESA_VER)/src/compiler/nir/nir_lower_to_source_mods.c \
	$(MESA_VER)/src/compiler/nir/nir_lower_two_sided_color.c \
	$(MESA_VER)/src/compiler/nir/nir_lower_uniforms_to_ubo.c \
	$(MESA_VER)/src/compiler/nir/nir_lower_vars_to_ssa.c \
	$(MESA_VER)/src/compiler/nir/nir_lower_var_copies.c \
	$(MESA_VER)/src/compiler/nir/nir_lower_vec_to_movs.c \
	$(MESA_VER)/src/compiler/nir/nir_lower_wpos_center.c \
	$(MESA_VER)/src/compiler/nir/nir_lower_wpos_ytransform.c \
	$(MESA_VER)/src/compiler/nir/nir_metadata.c \
	$(MESA_VER)/src/compiler/nir/nir_move_vec_src_uses_to_dest.c \
	$(MESA_VER)/src/compiler/nir/nir_normalize_cubemap_coords.c \
	$(MESA_VER)/src/compiler/nir/nir_opt_conditional_discard.c \
	$(MESA_VER)/src/compiler/nir/nir_opt_constant_folding.c \
	$(MESA_VER)/src/compiler/nir/nir_opt_copy_prop_vars.c \
	$(MESA_VER)/src/compiler/nir/nir_opt_copy_propagate.c \
	$(MESA_VER)/src/compiler/nir/nir_opt_cse.c \
	$(MESA_VER)/src/compiler/nir/nir_opt_dce.c \
	$(MESA_VER)/src/compiler/nir/nir_opt_dead_cf.c \
	$(MESA_VER)/src/compiler/nir/nir_opt_gcm.c \
	$(MESA_VER)/src/compiler/nir/nir_opt_global_to_local.c \
	$(MESA_VER)/src/compiler/nir/nir_opt_if.c \
	$(MESA_VER)/src/compiler/nir/nir_opt_intrinsics.c \
	$(MESA_VER)/src/compiler/nir/nir_opt_loop_unroll.c \
	$(MESA_VER)/src/compiler/nir/nir_opt_move_comparisons.c \
	$(MESA_VER)/src/compiler/nir/nir_opt_peephole_select.c \
	$(MESA_VER)/src/compiler/nir/nir_opt_remove_phis.c \
	$(MESA_VER)/src/compiler/nir/nir_opt_trivial_continues.c \
	$(MESA_VER)/src/compiler/nir/nir_opt_undef.c \
	$(MESA_VER)/src/compiler/nir/nir_phi_builder.c \
	$(MESA_VER)/src/compiler/nir/nir_print.c \
	$(MESA_VER)/src/compiler/nir/nir_propagate_invariant.c \
	$(MESA_VER)/src/compiler/nir/nir_remove_dead_variables.c \
	$(MESA_VER)/src/compiler/nir/nir_repair_ssa.c \
	$(MESA_VER)/src/compiler/nir/nir_search.c \
	$(MESA_VER)/src/compiler/nir/nir_split_var_copies.c \
	$(MESA_VER)/src/compiler/nir/nir_sweep.c \
	$(MESA_VER)/src/compiler/nir/nir_to_lcssa.c \
	$(MESA_VER)/src/compiler/nir/nir_validate.c \
	$(MESA_VER)/src/compiler/nir/nir_worklist.c
MesaLib_SRC += \
	$(MESA_VER)/src/mapi/glapi/glapi_dispatch.c \
	$(MESA_VER)/src/mapi/glapi/glapi_entrypoint.c \
	$(MESA_VER)/src/mapi/glapi/glapi_getproc.c \
	$(MESA_VER)/src/mapi/glapi/glapi_nop.c \
	$(MESA_VER)/src/mapi/glapi/glapi.c \
	$(MESA_VER)/src/mapi/u_current.c \
	$(MESA_VER)/src/mapi/u_execmem.c
# Not used
#	$(MESA_VER)/src/compiler/glsl/standalone_scaffolding.cpp
#	$(MESA_VER)/src/compiler/glsl/standalone.cpp

MesaWglLib_SRC = \
	$(MESA_VER)/src/gallium/state_trackers/wgl/stw_context.c \
	$(MESA_VER)/src/gallium/state_trackers/wgl/stw_device.c \
	$(MESA_VER)/src/gallium/state_trackers/wgl/stw_ext_context.c \
	$(MESA_VER)/src/gallium/state_trackers/wgl/stw_ext_extensionsstring.c \
	$(MESA_VER)/src/gallium/state_trackers/wgl/stw_ext_pbuffer.c \
	$(MESA_VER)/src/gallium/state_trackers/wgl/stw_ext_pixelformat.c \
	$(MESA_VER)/src/gallium/state_trackers/wgl/stw_ext_rendertexture.c \
	$(MESA_VER)/src/gallium/state_trackers/wgl/stw_ext_swapinterval.c \
	$(MESA_VER)/src/gallium/state_trackers/wgl/stw_framebuffer.c \
	$(MESA_VER)/src/gallium/state_trackers/wgl/stw_getprocaddress.c \
	$(MESA_VER)/src/gallium/state_trackers/wgl/stw_nopfuncs.c \
	$(MESA_VER)/src/gallium/state_trackers/wgl/stw_pixelformat.c \
	$(MESA_VER)/src/gallium/state_trackers/wgl/stw_st.c \
	$(MESA_VER)/src/gallium/state_trackers/wgl/stw_tls.c \
	$(MESA_VER)/src/gallium/state_trackers/wgl/stw_wgl.c \
	$(MESA_VER)/src/gallium/state_trackers/wgl/stw_debug.c \
	extra/knownfolders.c

MesaGalliumAuxLib_SRC = \
	$(MESA_VER)/src/gallium/auxiliary/cso_cache/cso_cache.c \
	$(MESA_VER)/src/gallium/auxiliary/cso_cache/cso_context.c \
	$(MESA_VER)/src/gallium/auxiliary/cso_cache/cso_hash.c \
	$(MESA_VER)/src/gallium/auxiliary/draw/draw_context.c \
	$(MESA_VER)/src/gallium/auxiliary/draw/draw_fs.c \
	$(MESA_VER)/src/gallium/auxiliary/draw/draw_gs.c \
	$(MESA_VER)/src/gallium/auxiliary/draw/draw_pipe_aaline.c \
	$(MESA_VER)/src/gallium/auxiliary/draw/draw_pipe_aapoint.c \
	$(MESA_VER)/src/gallium/auxiliary/draw/draw_pipe.c \
	$(MESA_VER)/src/gallium/auxiliary/draw/draw_pipe_clip.c \
	$(MESA_VER)/src/gallium/auxiliary/draw/draw_pipe_cull.c \
	$(MESA_VER)/src/gallium/auxiliary/draw/draw_pipe_flatshade.c \
	$(MESA_VER)/src/gallium/auxiliary/draw/draw_pipe_offset.c \
	$(MESA_VER)/src/gallium/auxiliary/draw/draw_pipe_pstipple.c \
	$(MESA_VER)/src/gallium/auxiliary/draw/draw_pipe_stipple.c \
	$(MESA_VER)/src/gallium/auxiliary/draw/draw_pipe_twoside.c \
	$(MESA_VER)/src/gallium/auxiliary/draw/draw_pipe_unfilled.c \
	$(MESA_VER)/src/gallium/auxiliary/draw/draw_pipe_util.c \
	$(MESA_VER)/src/gallium/auxiliary/draw/draw_pipe_validate.c \
	$(MESA_VER)/src/gallium/auxiliary/draw/draw_pipe_vbuf.c \
	$(MESA_VER)/src/gallium/auxiliary/draw/draw_pipe_wide_line.c \
	$(MESA_VER)/src/gallium/auxiliary/draw/draw_pipe_wide_point.c \
	$(MESA_VER)/src/gallium/auxiliary/draw/draw_prim_assembler.c \
	$(MESA_VER)/src/gallium/auxiliary/draw/draw_pt.c \
	$(MESA_VER)/src/gallium/auxiliary/draw/draw_pt_emit.c \
	$(MESA_VER)/src/gallium/auxiliary/draw/draw_pt_fetch.c \
	$(MESA_VER)/src/gallium/auxiliary/draw/draw_pt_fetch_emit.c \
	$(MESA_VER)/src/gallium/auxiliary/draw/draw_pt_fetch_shade_emit.c \
	$(MESA_VER)/src/gallium/auxiliary/draw/draw_pt_fetch_shade_pipeline.c \
	$(MESA_VER)/src/gallium/auxiliary/draw/draw_pt_post_vs.c \
	$(MESA_VER)/src/gallium/auxiliary/draw/draw_pt_so_emit.c \
	$(MESA_VER)/src/gallium/auxiliary/draw/draw_pt_util.c \
	$(MESA_VER)/src/gallium/auxiliary/draw/draw_pt_vsplit.c \
	$(MESA_VER)/src/gallium/auxiliary/draw/draw_vertex.c \
	$(MESA_VER)/src/gallium/auxiliary/draw/draw_vs.c \
	$(MESA_VER)/src/gallium/auxiliary/draw/draw_vs_exec.c \
	$(MESA_VER)/src/gallium/auxiliary/draw/draw_vs_variant.c \
	$(MESA_VER)/src/gallium/auxiliary/hud/font.c \
	$(MESA_VER)/src/gallium/auxiliary/hud/hud_context.c \
	$(MESA_VER)/src/gallium/auxiliary/hud/hud_cpu.c \
	$(MESA_VER)/src/gallium/auxiliary/hud/hud_nic.c \
	$(MESA_VER)/src/gallium/auxiliary/hud/hud_cpufreq.c \
	$(MESA_VER)/src/gallium/auxiliary/hud/hud_diskstat.c \
	$(MESA_VER)/src/gallium/auxiliary/hud/hud_sensors_temp.c \
	$(MESA_VER)/src/gallium/auxiliary/hud/hud_driver_query.c \
	$(MESA_VER)/src/gallium/auxiliary/hud/hud_fps.c \
	$(MESA_VER)/src/gallium/auxiliary/indices/u_primconvert.c \
	$(MESA_VER)/src/gallium/auxiliary/os/os_misc.c \
	$(MESA_VER)/src/gallium/auxiliary/os/os_process.c \
	$(MESA_VER)/src/gallium/auxiliary/os/os_time.c \
	$(MESA_VER)/src/gallium/auxiliary/pipebuffer/pb_buffer_fenced.c \
	$(MESA_VER)/src/gallium/auxiliary/pipebuffer/pb_buffer_malloc.c \
	$(MESA_VER)/src/gallium/auxiliary/pipebuffer/pb_bufmgr_alt.c \
	$(MESA_VER)/src/gallium/auxiliary/pipebuffer/pb_bufmgr_cache.c \
	$(MESA_VER)/src/gallium/auxiliary/pipebuffer/pb_bufmgr_debug.c \
	$(MESA_VER)/src/gallium/auxiliary/pipebuffer/pb_bufmgr_mm.c \
	$(MESA_VER)/src/gallium/auxiliary/pipebuffer/pb_bufmgr_ondemand.c \
	$(MESA_VER)/src/gallium/auxiliary/pipebuffer/pb_bufmgr_pool.c \
	$(MESA_VER)/src/gallium/auxiliary/pipebuffer/pb_bufmgr_slab.c \
	$(MESA_VER)/src/gallium/auxiliary/pipebuffer/pb_cache.c \
	$(MESA_VER)/src/gallium/auxiliary/pipebuffer/pb_slab.c \
	$(MESA_VER)/src/gallium/auxiliary/pipebuffer/pb_validate.c \
	$(MESA_VER)/src/gallium/auxiliary/postprocess/pp_celshade.c \
	$(MESA_VER)/src/gallium/auxiliary/postprocess/pp_colors.c \
	$(MESA_VER)/src/gallium/auxiliary/postprocess/pp_init.c \
	$(MESA_VER)/src/gallium/auxiliary/postprocess/pp_mlaa.c \
	$(MESA_VER)/src/gallium/auxiliary/postprocess/pp_program.c \
	$(MESA_VER)/src/gallium/auxiliary/postprocess/pp_run.c \
	$(MESA_VER)/src/gallium/auxiliary/rbug/rbug_connection.c \
	$(MESA_VER)/src/gallium/auxiliary/rbug/rbug_context.c \
	$(MESA_VER)/src/gallium/auxiliary/rbug/rbug_core.c \
	$(MESA_VER)/src/gallium/auxiliary/rbug/rbug_demarshal.c \
	$(MESA_VER)/src/gallium/auxiliary/rbug/rbug_shader.c \
	$(MESA_VER)/src/gallium/auxiliary/rbug/rbug_texture.c \
	$(MESA_VER)/src/gallium/auxiliary/rtasm/rtasm_cpu.c \
	$(MESA_VER)/src/gallium/auxiliary/rtasm/rtasm_execmem.c \
	$(MESA_VER)/src/gallium/auxiliary/rtasm/rtasm_x86sse.c \
	$(MESA_VER)/src/gallium/auxiliary/tgsi/tgsi_aa_point.c \
	$(MESA_VER)/src/gallium/auxiliary/tgsi/tgsi_build.c \
	$(MESA_VER)/src/gallium/auxiliary/tgsi/tgsi_dump.c \
	$(MESA_VER)/src/gallium/auxiliary/tgsi/tgsi_exec.c \
	$(MESA_VER)/src/gallium/auxiliary/tgsi/tgsi_emulate.c \
	$(MESA_VER)/src/gallium/auxiliary/tgsi/tgsi_from_mesa.c \
	$(MESA_VER)/src/gallium/auxiliary/tgsi/tgsi_info.c \
	$(MESA_VER)/src/gallium/auxiliary/tgsi/tgsi_iterate.c \
	$(MESA_VER)/src/gallium/auxiliary/tgsi/tgsi_lowering.c \
	$(MESA_VER)/src/gallium/auxiliary/tgsi/tgsi_parse.c \
	$(MESA_VER)/src/gallium/auxiliary/tgsi/tgsi_point_sprite.c \
	$(MESA_VER)/src/gallium/auxiliary/tgsi/tgsi_sanity.c \
	$(MESA_VER)/src/gallium/auxiliary/tgsi/tgsi_scan.c \
	$(MESA_VER)/src/gallium/auxiliary/tgsi/tgsi_strings.c \
	$(MESA_VER)/src/gallium/auxiliary/tgsi/tgsi_text.c \
	$(MESA_VER)/src/gallium/auxiliary/tgsi/tgsi_transform.c \
	$(MESA_VER)/src/gallium/auxiliary/tgsi/tgsi_two_side.c \
	$(MESA_VER)/src/gallium/auxiliary/tgsi/tgsi_ureg.c \
	$(MESA_VER)/src/gallium/auxiliary/tgsi/tgsi_util.c \
	$(MESA_VER)/src/gallium/auxiliary/translate/translate.c \
	$(MESA_VER)/src/gallium/auxiliary/translate/translate_cache.c \
	$(MESA_VER)/src/gallium/auxiliary/translate/translate_generic.c \
	$(MESA_VER)/src/gallium/auxiliary/translate/translate_sse.c \
	$(MESA_VER)/src/gallium/auxiliary/util/u_bitmask.c \
	$(MESA_VER)/src/gallium/auxiliary/util/u_blit.c \
	$(MESA_VER)/src/gallium/auxiliary/util/u_blitter.c \
	$(MESA_VER)/src/gallium/auxiliary/util/u_cache.c \
	$(MESA_VER)/src/gallium/auxiliary/util/u_cpu_detect.c \
	$(MESA_VER)/src/gallium/auxiliary/util/u_debug.c \
	$(MESA_VER)/src/gallium/auxiliary/util/u_debug_describe.c \
	$(MESA_VER)/src/gallium/auxiliary/util/u_debug_flush.c \
	$(MESA_VER)/src/gallium/auxiliary/util/u_debug_image.c \
	$(MESA_VER)/src/gallium/auxiliary/util/u_debug_memory.c \
	$(MESA_VER)/src/gallium/auxiliary/util/u_debug_refcnt.c \
	$(MESA_VER)/src/gallium/auxiliary/util/u_debug_stack.c \
	$(MESA_VER)/src/gallium/auxiliary/util/u_debug_symbol.c \
	$(MESA_VER)/src/gallium/auxiliary/util/u_dl.c \
	$(MESA_VER)/src/gallium/auxiliary/util/u_draw.c \
	$(MESA_VER)/src/gallium/auxiliary/util/u_draw_quad.c \
	$(MESA_VER)/src/gallium/auxiliary/util/u_dump_defines.c \
	$(MESA_VER)/src/gallium/auxiliary/util/u_dump_state.c \
	$(MESA_VER)/src/gallium/auxiliary/util/u_format.c \
	$(MESA_VER)/src/gallium/auxiliary/util/u_format_etc.c \
	$(MESA_VER)/src/gallium/auxiliary/util/u_format_latc.c \
	$(MESA_VER)/src/gallium/auxiliary/util/u_format_other.c \
	$(MESA_VER)/src/gallium/auxiliary/util/u_format_rgtc.c \
	$(MESA_VER)/src/gallium/auxiliary/util/u_format_s3tc.c \
	$(MESA_VER)/src/gallium/auxiliary/util/u_format_tests.c \
	$(MESA_VER)/src/gallium/auxiliary/util/u_format_yuv.c \
	$(MESA_VER)/src/gallium/auxiliary/util/u_format_zs.c \
	$(MESA_VER)/src/gallium/auxiliary/util/u_framebuffer.c \
	$(MESA_VER)/src/gallium/auxiliary/util/u_gen_mipmap.c \
	$(MESA_VER)/src/gallium/auxiliary/util/u_handle_table.c \
	$(MESA_VER)/src/gallium/auxiliary/util/u_hash_table.c \
	$(MESA_VER)/src/gallium/auxiliary/util/u_helpers.c \
	$(MESA_VER)/src/gallium/auxiliary/util/u_index_modify.c \
	$(MESA_VER)/src/gallium/auxiliary/util/u_linear.c \
	$(MESA_VER)/src/gallium/auxiliary/util/u_math.c \
	$(MESA_VER)/src/gallium/auxiliary/util/u_mm.c \
	$(MESA_VER)/src/gallium/auxiliary/util/u_network.c \
	$(MESA_VER)/src/gallium/auxiliary/util/u_prim_restart.c \
	$(MESA_VER)/src/gallium/auxiliary/util/u_pstipple.c \
	$(MESA_VER)/src/gallium/auxiliary/util/u_resource.c \
	$(MESA_VER)/src/gallium/auxiliary/util/u_ringbuffer.c \
	$(MESA_VER)/src/gallium/auxiliary/util/u_sampler.c \
	$(MESA_VER)/src/gallium/auxiliary/util/u_simple_shaders.c \
	$(MESA_VER)/src/gallium/auxiliary/util/u_suballoc.c \
	$(MESA_VER)/src/gallium/auxiliary/util/u_surface.c \
	$(MESA_VER)/src/gallium/auxiliary/util/u_surfaces.c \
	$(MESA_VER)/src/gallium/auxiliary/util/u_tests.c \
	$(MESA_VER)/src/gallium/auxiliary/util/u_texture.c \
	$(MESA_VER)/src/gallium/auxiliary/util/u_tile.c \
	$(MESA_VER)/src/gallium/auxiliary/util/u_transfer.c \
	$(MESA_VER)/src/gallium/auxiliary/util/u_upload_mgr.c \
	$(MESA_VER)/src/gallium/auxiliary/util/u_vbuf.c
# Auto-generated
MesaGalliumAuxLib_SRC += \
	$(MESA_VER)/src/gallium/auxiliary/indices/u_indices_gen.c \
	$(MESA_VER)/src/gallium/auxiliary/indices/u_unfilled_gen.c \
	$(MESA_VER)/src/gallium/auxiliary/util/u_format_table.c

MesaGalliumAuxLibSimd_SRC = \
  $(MesaGalliumAuxLib_SRC) \
  $(MESA_VER)/src/gallium/auxiliary/draw/draw_llvm.c \
  $(MESA_VER)/src/gallium/auxiliary/draw/draw_llvm_sample.c \
  $(MESA_VER)/src/gallium/auxiliary/draw/draw_pt_fetch_shade_pipeline_llvm.c \
  $(MESA_VER)/src/gallium/auxiliary/draw/draw_vs_llvm.c \
  $(MESA_VER)/src/gallium/auxiliary/gallivm/lp_bld_arit.c \
  $(MESA_VER)/src/gallium/auxiliary/gallivm/lp_bld_arit_overflow.c \
  $(MESA_VER)/src/gallium/auxiliary/gallivm/lp_bld_assert.c \
  $(MESA_VER)/src/gallium/auxiliary/gallivm/lp_bld_bitarit.c \
  $(MESA_VER)/src/gallium/auxiliary/gallivm/lp_bld_const.c \
  $(MESA_VER)/src/gallium/auxiliary/gallivm/lp_bld_conv.c \
  $(MESA_VER)/src/gallium/auxiliary/gallivm/lp_bld_flow.c \
  $(MESA_VER)/src/gallium/auxiliary/gallivm/lp_bld_format.c \
  $(MESA_VER)/src/gallium/auxiliary/gallivm/lp_bld_format_aos.c \
  $(MESA_VER)/src/gallium/auxiliary/gallivm/lp_bld_format_aos_array.c \
  $(MESA_VER)/src/gallium/auxiliary/gallivm/lp_bld_format_cached.c \
  $(MESA_VER)/src/gallium/auxiliary/gallivm/lp_bld_format_float.c \
  $(MESA_VER)/src/gallium/auxiliary/gallivm/lp_bld_format_soa.c \
  $(MESA_VER)/src/gallium/auxiliary/gallivm/lp_bld_format_srgb.c \
  $(MESA_VER)/src/gallium/auxiliary/gallivm/lp_bld_format_yuv.c \
  $(MESA_VER)/src/gallium/auxiliary/gallivm/lp_bld_gather.c \
  $(MESA_VER)/src/gallium/auxiliary/gallivm/lp_bld_init.c \
  $(MESA_VER)/src/gallium/auxiliary/gallivm/lp_bld_intr.c \
  $(MESA_VER)/src/gallium/auxiliary/gallivm/lp_bld_logic.c \
  $(MESA_VER)/src/gallium/auxiliary/gallivm/lp_bld_pack.c \
  $(MESA_VER)/src/gallium/auxiliary/gallivm/lp_bld_printf.c \
  $(MESA_VER)/src/gallium/auxiliary/gallivm/lp_bld_quad.c \
  $(MESA_VER)/src/gallium/auxiliary/gallivm/lp_bld_sample.c \
  $(MESA_VER)/src/gallium/auxiliary/gallivm/lp_bld_sample_aos.c \
  $(MESA_VER)/src/gallium/auxiliary/gallivm/lp_bld_sample_soa.c \
  $(MESA_VER)/src/gallium/auxiliary/gallivm/lp_bld_struct.c \
  $(MESA_VER)/src/gallium/auxiliary/gallivm/lp_bld_swizzle.c \
  $(MESA_VER)/src/gallium/auxiliary/gallivm/lp_bld_tgsi.c \
  $(MESA_VER)/src/gallium/auxiliary/gallivm/lp_bld_tgsi_action.c \
  $(MESA_VER)/src/gallium/auxiliary/gallivm/lp_bld_tgsi_aos.c \
  $(MESA_VER)/src/gallium/auxiliary/gallivm/lp_bld_tgsi_info.c \
  $(MESA_VER)/src/gallium/auxiliary/gallivm/lp_bld_tgsi_soa.c \
  $(MESA_VER)/src/gallium/auxiliary/gallivm/lp_bld_type.c \
  $(MESA_VER)/src/gallium/auxiliary/gallivm/lp_bld_debug.cpp \
  $(MESA_VER)/src/gallium/auxiliary/gallivm/lp_bld_misc.cpp

MesaNineLib_SRC    = \
	$(MESA_VER)/src/gallium/state_trackers/nine/adapter9.c \
	$(MESA_VER)/src/gallium/state_trackers/nine/authenticatedchannel9.c \
	$(MESA_VER)/src/gallium/state_trackers/nine/basetexture9.c \
	$(MESA_VER)/src/gallium/state_trackers/nine/buffer9.c \
	$(MESA_VER)/src/gallium/state_trackers/nine/cryptosession9.c \
	$(MESA_VER)/src/gallium/state_trackers/nine/cubetexture9.c \
	$(MESA_VER)/src/gallium/state_trackers/nine/device9.c \
	$(MESA_VER)/src/gallium/state_trackers/nine/device9ex.c \
	$(MESA_VER)/src/gallium/state_trackers/nine/device9video.c \
	$(MESA_VER)/src/gallium/state_trackers/nine/guid.c \
	$(MESA_VER)/src/gallium/state_trackers/nine/indexbuffer9.c \
	$(MESA_VER)/src/gallium/state_trackers/nine/iunknown.c \
	$(MESA_VER)/src/gallium/state_trackers/nine/nine_buffer_upload.c \
	$(MESA_VER)/src/gallium/state_trackers/nine/nine_debug.c \
	$(MESA_VER)/src/gallium/state_trackers/nine/nine_dump.c \
	$(MESA_VER)/src/gallium/state_trackers/nine/nineexoverlayextension.c \
	$(MESA_VER)/src/gallium/state_trackers/nine/nine_ff.c \
	$(MESA_VER)/src/gallium/state_trackers/nine/nine_helpers.c \
	$(MESA_VER)/src/gallium/state_trackers/nine/nine_lock.c \
	$(MESA_VER)/src/gallium/state_trackers/nine/nine_pipe.c \
	$(MESA_VER)/src/gallium/state_trackers/nine/nine_quirk.c \
	$(MESA_VER)/src/gallium/state_trackers/nine/nine_queue.c \
	$(MESA_VER)/src/gallium/state_trackers/nine/nine_shader.c \
	$(MESA_VER)/src/gallium/state_trackers/nine/nine_state.c \
	$(MESA_VER)/src/gallium/state_trackers/nine/pixelshader9.c \
	$(MESA_VER)/src/gallium/state_trackers/nine/query9.c \
	$(MESA_VER)/src/gallium/state_trackers/nine/resource9.c \
	$(MESA_VER)/src/gallium/state_trackers/nine/stateblock9.c \
	$(MESA_VER)/src/gallium/state_trackers/nine/surface9.c \
	$(MESA_VER)/src/gallium/state_trackers/nine/swapchain9.c \
	$(MESA_VER)/src/gallium/state_trackers/nine/swapchain9ex.c \
	$(MESA_VER)/src/gallium/state_trackers/nine/texture9.c \
	$(MESA_VER)/src/gallium/state_trackers/nine/threadpool.c \
	$(MESA_VER)/src/gallium/state_trackers/nine/vertexbuffer9.c \
	$(MESA_VER)/src/gallium/state_trackers/nine/vertexdeclaration9.c \
	$(MESA_VER)/src/gallium/state_trackers/nine/vertexshader9.c \
	$(MESA_VER)/src/gallium/state_trackers/nine/volume9.c \
	$(MESA_VER)/src/gallium/state_trackers/nine/volumetexture9.c

MesaSVGALib_SRC  = \
	$(MESA_VER)/src/gallium/drivers/svga/svga_cmd.c \
	$(MESA_VER)/src/gallium/drivers/svga/svga_cmd_vgpu10.c \
	$(MESA_VER)/src/gallium/drivers/svga/svga_context.c \
	$(MESA_VER)/src/gallium/drivers/svga/svga_draw_arrays.c \
	$(MESA_VER)/src/gallium/drivers/svga/svga_draw.c \
	$(MESA_VER)/src/gallium/drivers/svga/svga_draw_elements.c \
	$(MESA_VER)/src/gallium/drivers/svga/svga_format.c \
	$(MESA_VER)/src/gallium/drivers/svga/svga_link.c \
	$(MESA_VER)/src/gallium/drivers/svga/svga_msg.c \
	$(MESA_VER)/src/gallium/drivers/svga/svga_pipe_blend.c \
	$(MESA_VER)/src/gallium/drivers/svga/svga_pipe_blit.c \
	$(MESA_VER)/src/gallium/drivers/svga/svga_pipe_clear.c \
	$(MESA_VER)/src/gallium/drivers/svga/svga_pipe_constants.c \
	$(MESA_VER)/src/gallium/drivers/svga/svga_pipe_depthstencil.c \
	$(MESA_VER)/src/gallium/drivers/svga/svga_pipe_draw.c \
	$(MESA_VER)/src/gallium/drivers/svga/svga_pipe_flush.c \
	$(MESA_VER)/src/gallium/drivers/svga/svga_pipe_fs.c \
	$(MESA_VER)/src/gallium/drivers/svga/svga_pipe_gs.c \
	$(MESA_VER)/src/gallium/drivers/svga/svga_pipe_misc.c \
	$(MESA_VER)/src/gallium/drivers/svga/svga_pipe_query.c \
	$(MESA_VER)/src/gallium/drivers/svga/svga_pipe_rasterizer.c \
	$(MESA_VER)/src/gallium/drivers/svga/svga_pipe_sampler.c \
	$(MESA_VER)/src/gallium/drivers/svga/svga_pipe_streamout.c \
	$(MESA_VER)/src/gallium/drivers/svga/svga_pipe_vertex.c \
	$(MESA_VER)/src/gallium/drivers/svga/svga_pipe_vs.c \
	$(MESA_VER)/src/gallium/drivers/svga/svga_resource_buffer.c \
	$(MESA_VER)/src/gallium/drivers/svga/svga_resource_buffer_upload.c \
	$(MESA_VER)/src/gallium/drivers/svga/svga_resource.c \
	$(MESA_VER)/src/gallium/drivers/svga/svga_resource_texture.c \
	$(MESA_VER)/src/gallium/drivers/svga/svga_sampler_view.c \
	$(MESA_VER)/src/gallium/drivers/svga/svga_screen.c \
	$(MESA_VER)/src/gallium/drivers/svga/svga_screen_cache.c \
	$(MESA_VER)/src/gallium/drivers/svga/svga_shader.c \
	$(MESA_VER)/src/gallium/drivers/svga/svga_state.c \
	$(MESA_VER)/src/gallium/drivers/svga/svga_state_constants.c \
	$(MESA_VER)/src/gallium/drivers/svga/svga_state_framebuffer.c \
	$(MESA_VER)/src/gallium/drivers/svga/svga_state_fs.c \
	$(MESA_VER)/src/gallium/drivers/svga/svga_state_gs.c \
	$(MESA_VER)/src/gallium/drivers/svga/svga_state_need_swtnl.c \
	$(MESA_VER)/src/gallium/drivers/svga/svga_state_rss.c \
	$(MESA_VER)/src/gallium/drivers/svga/svga_state_sampler.c \
	$(MESA_VER)/src/gallium/drivers/svga/svga_state_tgsi_transform.c \
	$(MESA_VER)/src/gallium/drivers/svga/svga_state_tss.c \
	$(MESA_VER)/src/gallium/drivers/svga/svga_state_vdecl.c \
	$(MESA_VER)/src/gallium/drivers/svga/svga_state_vs.c \
	$(MESA_VER)/src/gallium/drivers/svga/svga_surface.c \
	$(MESA_VER)/src/gallium/drivers/svga/svga_swtnl_backend.c \
	$(MESA_VER)/src/gallium/drivers/svga/svga_swtnl_draw.c \
	$(MESA_VER)/src/gallium/drivers/svga/svga_swtnl_state.c \
	$(MESA_VER)/src/gallium/drivers/svga/svga_tgsi.c \
	$(MESA_VER)/src/gallium/drivers/svga/svga_tgsi_decl_sm30.c \
	$(MESA_VER)/src/gallium/drivers/svga/svga_tgsi_insn.c \
	$(MESA_VER)/src/gallium/drivers/svga/svga_tgsi_vgpu10.c \
	$(MESA_VER)/src/gallium/drivers/svga/svgadump/svga_dump.c \
	$(MESA_VER)/src/gallium/drivers/svga/svgadump/svga_shader_dump.c \
	$(MESA_VER)/src/gallium/drivers/svga/svgadump/svga_shader_op.c

MesaSVGAWinsysLib_SRC  = \
	$(MESA_VER)/src/gallium/winsys/svga/drm/pb_buffer_simple_fenced.c \
	$(MESA_VER)/src/gallium/winsys/svga/drm/vmw_buffer.c \
	$(MESA_VER)/src/gallium/winsys/svga/drm/vmw_context.c \
	$(MESA_VER)/src/gallium/winsys/svga/drm/vmw_fence.c \
	$(MESA_VER)/src/gallium/winsys/svga/drm/vmw_screen_pools.c \
	$(MESA_VER)/src/gallium/winsys/svga/drm/vmw_screen_svga.c \
	$(MESA_VER)/src/gallium/winsys/svga/drm/vmw_surface.c \
	$(MESA_VER)/src/gallium/winsys/svga/drm/vmw_shader.c \
	$(MESA_VER)/src/gallium/winsys/svga/drm/vmw_query.c \
	win9x/winsys/vmw_screen.c \
	win9x/winsys/vmw_screen_ioctl.c \
	win9x/winsys/vmw_screen_wddm.c \
	win9x/gadrv9x.cpp \
	win9x/gadrv9xenv.cpp \
	win9x/svgadrv.c
	
MesaGalliumSWRLib_SRC = \
	$(MESA_VER)/src/gallium/drivers/swr/swr_loader.cpp \
	$(MESA_VER)/src/gallium/drivers/swr/swr_clear.cpp \
	$(MESA_VER)/src/gallium/drivers/swr/swr_context.cpp \
	$(MESA_VER)/src/gallium/drivers/swr/swr_draw.cpp \
	$(MESA_VER)/src/gallium/drivers/swr/swr_screen.cpp \
	$(MESA_VER)/src/gallium/drivers/swr/swr_state.cpp \
	$(MESA_VER)/src/gallium/drivers/swr/swr_tex_sample.cpp \
	$(MESA_VER)/src/gallium/drivers/swr/swr_scratch.cpp \
	$(MESA_VER)/src/gallium/drivers/swr/swr_shader.cpp \
	$(MESA_VER)/src/gallium/drivers/swr/swr_fence.cpp \
	$(MESA_VER)/src/gallium/drivers/swr/swr_fence_work.cpp \
	$(MESA_VER)/src/gallium/drivers/swr/swr_query.cpp \
	$(MESA_VER)/src/gallium/drivers/swr/rasterizer/archrast/archrast.cpp \
	$(MESA_VER)/src/gallium/drivers/swr/rasterizer/common/formats.cpp \
	$(MESA_VER)/src/gallium/drivers/swr/rasterizer/common/os.cpp \
	$(MESA_VER)/src/gallium/drivers/swr/rasterizer/common/rdtsc_buckets.cpp \
	$(MESA_VER)/src/gallium/drivers/swr/rasterizer/common/swr_assert.cpp \
	$(MESA_VER)/src/gallium/drivers/swr/rasterizer/core/api.cpp \
	$(MESA_VER)/src/gallium/drivers/swr/rasterizer/core/backend.cpp \
	$(MESA_VER)/src/gallium/drivers/swr/rasterizer/core/backend_clear.cpp \
	$(MESA_VER)/src/gallium/drivers/swr/rasterizer/core/backend_sample.cpp \
	$(MESA_VER)/src/gallium/drivers/swr/rasterizer/core/backend_singlesample.cpp \
	$(MESA_VER)/src/gallium/drivers/swr/rasterizer/core/binner.cpp \
	$(MESA_VER)/src/gallium/drivers/swr/rasterizer/core/clip.cpp \
	$(MESA_VER)/src/gallium/drivers/swr/rasterizer/core/frontend.cpp \
	$(MESA_VER)/src/gallium/drivers/swr/rasterizer/core/pa_avx.cpp \
	$(MESA_VER)/src/gallium/drivers/swr/rasterizer/core/rasterizer.cpp \
	$(MESA_VER)/src/gallium/drivers/swr/rasterizer/core/rdtsc_core.cpp \
	$(MESA_VER)/src/gallium/drivers/swr/rasterizer/core/threads.cpp \
	$(MESA_VER)/src/gallium/drivers/swr/rasterizer/core/tilemgr.cpp \
	$(MESA_VER)/src/gallium/drivers/swr/rasterizer/jitter/blend_jit.cpp \
	$(MESA_VER)/src/gallium/drivers/swr/rasterizer/jitter/builder.cpp \
	$(MESA_VER)/src/gallium/drivers/swr/rasterizer/jitter/builder_misc.cpp \
	$(MESA_VER)/src/gallium/drivers/swr/rasterizer/jitter/fetch_jit.cpp \
	$(MESA_VER)/src/gallium/drivers/swr/rasterizer/jitter/JitManager.cpp \
	$(MESA_VER)/src/gallium/drivers/swr/rasterizer/jitter/streamout_jit.cpp \
	$(MESA_VER)/src/gallium/drivers/swr/rasterizer/memory/ClearTile.cpp \
	$(MESA_VER)/src/gallium/drivers/swr/rasterizer/memory/LoadTile.cpp \
	$(MESA_VER)/src/gallium/drivers/swr/rasterizer/memory/LoadTile_Linear.cpp \
	$(MESA_VER)/src/gallium/drivers/swr/rasterizer/memory/LoadTile_TileX.cpp \
	$(MESA_VER)/src/gallium/drivers/swr/rasterizer/memory/LoadTile_TileY.cpp \
	$(MESA_VER)/src/gallium/drivers/swr/rasterizer/memory/StoreTile.cpp \
	$(MESA_VER)/src/gallium/drivers/swr/rasterizer/memory/StoreTile_Linear2.cpp \
	$(MESA_VER)/src/gallium/drivers/swr/rasterizer/memory/StoreTile_Linear.cpp \
	$(MESA_VER)/src/gallium/drivers/swr/rasterizer/memory/StoreTile_TileW.cpp \
	$(MESA_VER)/src/gallium/drivers/swr/rasterizer/memory/StoreTile_TileX2.cpp \
	$(MESA_VER)/src/gallium/drivers/swr/rasterizer/memory/StoreTile_TileX.cpp \
	$(MESA_VER)/src/gallium/drivers/swr/rasterizer/memory/StoreTile_TileY2.cpp \
	$(MESA_VER)/src/gallium/drivers/swr/rasterizer/memory/StoreTile_TileY.cpp

MesaGdiLib_SRC = \
  $(MESA_VER)/src/gallium/winsys/sw/gdi/gdi_sw_winsys.c \
  $(MESA_VER)/src/gallium/drivers/softpipe/sp_buffer.c \
  $(MESA_VER)/src/gallium/drivers/softpipe/sp_clear.c \
  $(MESA_VER)/src/gallium/drivers/softpipe/sp_context.c \
  $(MESA_VER)/src/gallium/drivers/softpipe/sp_compute.c \
  $(MESA_VER)/src/gallium/drivers/softpipe/sp_draw_arrays.c \
  $(MESA_VER)/src/gallium/drivers/softpipe/sp_fence.c \
  $(MESA_VER)/src/gallium/drivers/softpipe/sp_flush.c \
  $(MESA_VER)/src/gallium/drivers/softpipe/sp_fs_exec.c \
  $(MESA_VER)/src/gallium/drivers/softpipe/sp_image.c \
  $(MESA_VER)/src/gallium/drivers/softpipe/sp_prim_vbuf.c \
  $(MESA_VER)/src/gallium/drivers/softpipe/sp_quad_blend.c \
  $(MESA_VER)/src/gallium/drivers/softpipe/sp_quad_depth_test.c \
  $(MESA_VER)/src/gallium/drivers/softpipe/sp_quad_fs.c \
  $(MESA_VER)/src/gallium/drivers/softpipe/sp_quad_pipe.c \
  $(MESA_VER)/src/gallium/drivers/softpipe/sp_quad_stipple.c \
  $(MESA_VER)/src/gallium/drivers/softpipe/sp_query.c \
  $(MESA_VER)/src/gallium/drivers/softpipe/sp_screen.c \
  $(MESA_VER)/src/gallium/drivers/softpipe/sp_setup.c \
  $(MESA_VER)/src/gallium/drivers/softpipe/sp_state_blend.c \
  $(MESA_VER)/src/gallium/drivers/softpipe/sp_state_clip.c \
  $(MESA_VER)/src/gallium/drivers/softpipe/sp_state_derived.c \
  $(MESA_VER)/src/gallium/drivers/softpipe/sp_state_image.c \
  $(MESA_VER)/src/gallium/drivers/softpipe/sp_state_rasterizer.c \
  $(MESA_VER)/src/gallium/drivers/softpipe/sp_state_sampler.c \
  $(MESA_VER)/src/gallium/drivers/softpipe/sp_state_shader.c \
  $(MESA_VER)/src/gallium/drivers/softpipe/sp_state_so.c \
  $(MESA_VER)/src/gallium/drivers/softpipe/sp_state_surface.c \
  $(MESA_VER)/src/gallium/drivers/softpipe/sp_state_vertex.c \
  $(MESA_VER)/src/gallium/drivers/softpipe/sp_surface.c \
  $(MESA_VER)/src/gallium/drivers/softpipe/sp_tex_sample.c \
  $(MESA_VER)/src/gallium/drivers/softpipe/sp_tex_tile_cache.c \
  $(MESA_VER)/src/gallium/drivers/softpipe/sp_texture.c \
  $(MESA_VER)/src/gallium/drivers/softpipe/sp_tile_cache.c \
  win9x/vramcpy.c

MesaGdiLibGL_SRC = \
  $(MesaGdiLib_SRC) \
  $(MESA_VER)/src/gallium/targets/libgl-gdi/libgl_gdi.c
  
MesaGdiLibICD_SRC = \
  $(MesaGdiLib_SRC) \
  $(MESA_VER)/src/gallium/targets/libgl-gdi/libgl_gdi_icd.c
  
MesaGdiLibVMW_SRC = \
  $(MesaGdiLib_SRC) \
  win9x/libgl_vmws.c

MesaGalliumLLVMPipe_SRC += \
  $(MESA_VER)/src/gallium/drivers/llvmpipe/lp_bld_alpha.c \
  $(MESA_VER)/src/gallium/drivers/llvmpipe/lp_bld_blend.c \
  $(MESA_VER)/src/gallium/drivers/llvmpipe/lp_bld_blend_aos.c \
  $(MESA_VER)/src/gallium/drivers/llvmpipe/lp_bld_blend_logicop.c \
  $(MESA_VER)/src/gallium/drivers/llvmpipe/lp_bld_depth.c \
  $(MESA_VER)/src/gallium/drivers/llvmpipe/lp_bld_interp.c \
  $(MESA_VER)/src/gallium/drivers/llvmpipe/lp_clear.c \
  $(MESA_VER)/src/gallium/drivers/llvmpipe/lp_context.c \
  $(MESA_VER)/src/gallium/drivers/llvmpipe/lp_draw_arrays.c \
  $(MESA_VER)/src/gallium/drivers/llvmpipe/lp_fence.c \
  $(MESA_VER)/src/gallium/drivers/llvmpipe/lp_flush.c \
  $(MESA_VER)/src/gallium/drivers/llvmpipe/lp_jit.c \
  $(MESA_VER)/src/gallium/drivers/llvmpipe/lp_memory.c \
  $(MESA_VER)/src/gallium/drivers/llvmpipe/lp_perf.c \
  $(MESA_VER)/src/gallium/drivers/llvmpipe/lp_query.c \
  $(MESA_VER)/src/gallium/drivers/llvmpipe/lp_rast.c \
  $(MESA_VER)/src/gallium/drivers/llvmpipe/lp_rast_debug.c \
  $(MESA_VER)/src/gallium/drivers/llvmpipe/lp_rast_tri.c \
  $(MESA_VER)/src/gallium/drivers/llvmpipe/lp_scene.c \
  $(MESA_VER)/src/gallium/drivers/llvmpipe/lp_scene_queue.c \
  $(MESA_VER)/src/gallium/drivers/llvmpipe/lp_screen.c \
  $(MESA_VER)/src/gallium/drivers/llvmpipe/lp_setup.c \
  $(MESA_VER)/src/gallium/drivers/llvmpipe/lp_setup_line.c \
  $(MESA_VER)/src/gallium/drivers/llvmpipe/lp_setup_point.c \
  $(MESA_VER)/src/gallium/drivers/llvmpipe/lp_setup_tri.c \
  $(MESA_VER)/src/gallium/drivers/llvmpipe/lp_setup_vbuf.c \
  $(MESA_VER)/src/gallium/drivers/llvmpipe/lp_state_blend.c \
  $(MESA_VER)/src/gallium/drivers/llvmpipe/lp_state_clip.c \
  $(MESA_VER)/src/gallium/drivers/llvmpipe/lp_state_derived.c \
  $(MESA_VER)/src/gallium/drivers/llvmpipe/lp_state_fs.c \
  $(MESA_VER)/src/gallium/drivers/llvmpipe/lp_state_gs.c \
  $(MESA_VER)/src/gallium/drivers/llvmpipe/lp_state_rasterizer.c \
  $(MESA_VER)/src/gallium/drivers/llvmpipe/lp_state_sampler.c \
  $(MESA_VER)/src/gallium/drivers/llvmpipe/lp_state_setup.c \
  $(MESA_VER)/src/gallium/drivers/llvmpipe/lp_state_so.c \
  $(MESA_VER)/src/gallium/drivers/llvmpipe/lp_state_surface.c \
  $(MESA_VER)/src/gallium/drivers/llvmpipe/lp_state_vertex.c \
  $(MESA_VER)/src/gallium/drivers/llvmpipe/lp_state_vs.c \
  $(MESA_VER)/src/gallium/drivers/llvmpipe/lp_surface.c \
  $(MESA_VER)/src/gallium/drivers/llvmpipe/lp_texture.c \
  $(MESA_VER)/src/gallium/drivers/llvmpipe/lp_tex_sample.c

glchecked_SRC = \
  glchecker/src/benchmark.cpp \
  glchecker/src/CEngine.cpp \
  glchecker/src/CGUI.cpp \
  glchecker/src/COpenGL.cpp \
  glchecker/src/CSound.cpp \
  glchecker/src/CVector.cpp \
  glchecker/src/CWindow.cpp \
  glchecker/src/glchecker.cpp \
  glchecker/src/parser.cpp

MesaUtilLib_OBJS := $(MesaUtilLib_SRC:.c=.c_gen$(OBJ))
MesaUtilLib_OBJS := $(MesaUtilLib_OBJS:.cpp=.cpp_gen$(OBJ))
$(LIBPREFIX)MesaUtilLib$(LIBSUFFIX): $(MesaUtilLib_OBJS)
	-$(RM) $@
	$(LIBSTATIC) $(MesaUtilLib_OBJS) 
	
MesaUtilLibSimd_OBJS := $(MesaUtilLib_SRC:.c=.c_simd$(OBJ))
MesaUtilLibSimd_OBJS := $(MesaUtilLibSimd_OBJS:.cpp=.cpp_simd$(OBJ))
$(LIBPREFIX)MesaUtilLibSimd$(LIBSUFFIX): $(MesaUtilLibSimd_OBJS)
	-$(RM) $@
	$(LIBSTATIC) $(MesaUtilLibSimd_OBJS) 

MesaLib_OBJS := $(MesaLib_SRC:.c=.c_gen$(OBJ))
MesaLib_OBJS := $(MesaLib_OBJS:.cpp=.cpp_gen$(OBJ))
$(LIBPREFIX)MesaLib$(LIBSUFFIX): $(MesaLib_OBJS)
	-$(RM) $@
	$(LIBSTATIC) $(MesaLib_OBJS)

MesaLibSimd_OBJS := $(MesaLib_SRC:.c=.c_simd$(OBJ))
MesaLibSimd_OBJS := $(MesaLibSimd_OBJS:.cpp=.cpp_simd$(OBJ))
$(LIBPREFIX)MesaLibSimd$(LIBSUFFIX): $(MesaLibSimd_OBJS)
	-$(RM) $@
	$(LIBSTATIC) $(MesaLibSimd_OBJS)

MesaGalliumAuxLib_OBJS := $(MesaGalliumAuxLib_SRC:.c=.c_gen$(OBJ))
MesaGalliumAuxLib_OBJS := $(MesaGalliumAuxLib_OBJS:.cpp=.cpp_gen$(OBJ))
$(LIBPREFIX)MesaGalliumAuxLib$(LIBSUFFIX): $(MesaGalliumAuxLib_OBJS)
	-$(RM) $@
	$(LIBSTATIC) $(MesaGalliumAuxLib_OBJS)

MesaGalliumAuxLibSimd_OBJS := $(MesaGalliumAuxLibSimd_SRC:.c=.c_simd$(OBJ))
MesaGalliumAuxLibSimd_OBJS := $(MesaGalliumAuxLibSimd_OBJS:.cpp=.cpp_simd$(OBJ))
$(LIBPREFIX)MesaGalliumAuxLibSimd$(LIBSUFFIX): $(MesaGalliumAuxLibSimd_OBJS)
	-$(RM) $@
	$(LIBSTATIC) $(MesaGalliumAuxLibSimd_OBJS)

MesaGalliumLLVMPipe_OBJS := $(MesaGalliumLLVMPipe_SRC:.c=.c_simd$(OBJ))
MesaGalliumLLVMPipe_OBJS := $(MesaGalliumLLVMPipe_OBJS:.cpp=.cpp_simd$(OBJ))
$(LIBPREFIX)MesaGalliumLLVMPipe$(LIBSUFFIX): $(MesaGalliumLLVMPipe_OBJS)
	-$(RM) $@
	$(LIBSTATIC) $(MesaGalliumLLVMPipe_OBJS)

MesaSVGALib_OBJS := $(MesaSVGALib_SRC:.c=.c_gen$(OBJ))
MesaSVGALib_OBJS := $(MesaSVGALib_OBJS:.cpp=.cpp_gen$(OBJ))
$(LIBPREFIX)MesaSVGALib$(LIBSUFFIX): $(MesaSVGALib_OBJS)
	-$(RM) $@
	$(LIBSTATIC) $(MesaSVGALib_OBJS)

MesaGdiLibGL_OBJS := $(MesaGdiLibGL_SRC:.c=.c_gen$(OBJ))
MesaGdiLibGL_OBJS := $(MesaGdiLibGL_OBJS:.cpp=.cpp_gen$(OBJ))
$(LIBPREFIX)MesaGdiLibGL$(LIBSUFFIX): $(MesaGdiLibGL_OBJS)
	-$(RM) $@
	$(LIBSTATIC) $(MesaGdiLibGL_OBJS)

MesaGdiLibGLSimd_OBJS := $(MesaGdiLibGL_SRC:.c=.c_simd$(OBJ))
MesaGdiLibGLSimd_OBJS := $(MesaGdiLibGLSimd_OBJS:.cpp=.cpp_simd$(OBJ))
$(LIBPREFIX)MesaGdiLibGLSimd$(LIBSUFFIX): $(MesaGdiLibGLSimd_OBJS)
	-$(RM) $@
	$(LIBSTATIC) $(MesaGdiLibGLSimd_OBJS)

MesaGdiLibICD_OBJS := $(MesaGdiLibICD_SRC:.c=.c_gen$(OBJ))
MesaGdiLibICD_OBJS := $(MesaGdiLibICD_OBJS:.cpp=.cpp_gen$(OBJ))
$(LIBPREFIX)MesaGdiLibICD$(LIBSUFFIX): $(MesaGdiLibICD_OBJS)
	-$(RM) $@
	$(LIBSTATIC) $(MesaGdiLibICD_OBJS)

MesaGdiLibICDSimd_OBJS := $(MesaGdiLibICD_SRC:.c=.c_simd$(OBJ))
MesaGdiLibICDSimd_OBJS := $(MesaGdiLibICDSimd_OBJS:.cpp=.cpp_simd$(OBJ))
$(LIBPREFIX)MesaGdiLibICDSimd$(LIBSUFFIX): $(MesaGdiLibICDSimd_OBJS)
	-$(RM) $@
	$(LIBSTATIC) $(MesaGdiLibICDSimd_OBJS)

MesaWglLib_OBJS := $(MesaWglLib_SRC:.c=.c_gen$(OBJ))
MesaWglLib_OBJS := $(MesaWglLib_OBJS:.cpp=.cpp_gen$(OBJ))
$(LIBPREFIX)MesaWglLib$(LIBSUFFIX): $(MesaWglLib_OBJS)
	-$(RM) $@
	$(LIBSTATIC) $(MesaWglLib_OBJS)

MesaWglLibSimd_OBJS := $(MesaWglLib_SRC:.c=.c_simd$(OBJ))
MesaWglLibSimd_OBJS := $(MesaWglLibSimd_OBJS:.cpp=.cpp_simd$(OBJ))
$(LIBPREFIX)MesaWglLibSimd$(LIBSUFFIX): $(MesaWglLibSimd_OBJS)
	-$(RM) $@
	$(LIBSTATIC) $(MesaWglLibSimd_OBJS)

MesaGdiLibVMW_OBJS := $(MesaGdiLibVMW_SRC:.c=.c_gen$(OBJ))
MesaGdiLibVMW_OBJS := $(MesaGdiLibVMW_OBJS:.cpp=.cpp_gen$(OBJ))

MesaSVGAWinsysLib_OBJS := $(MesaSVGAWinsysLib_SRC:.c=.c_gen$(OBJ))
MesaSVGAWinsysLib_OBJS := $(MesaSVGAWinsysLib_OBJS:.cpp=.cpp_gen$(OBJ))

# software opengl32 replacement
opengl32.w95.dll: $(LIBS_TO_BUILD) $(DEPS) opengl32.res $(LD_DEPS)
	$(LD) $(LDFLAGS) $(MesaWglLib_OBJS) $(MesaGdiLibGL_OBJS) $(OPENGL_LIBS) $(MESA_LIBS) opengl32.res $(DLLFLAGS) $(OPENGL_DEF)

opengl32.w98me.dll: $(LIBS_TO_BUILD) $(DEPS) opengl32.res $(LD_DEPS)
	$(LD) $(LDFLAGS) $(MesaWglLibSimd_OBJS) $(MesaGdiLibGLSimd_OBJS) $(opengl_simd_LIBS) $(MESA_SIMD_LIBS) opengl32.res $(DLLFLAGS) $(OPENGL_DEF)

# sotware ICD driver
mesa3d.w95.dll: $(LIBS_TO_BUILD) $(DEPS) mesa3d.res $(LD_DEPS)
	$(LD) $(LDFLAGS) $(MesaWglLib_OBJS) $(MesaGdiLibICD_OBJS) $(OPENGL_LIBS) $(MESA_LIBS) mesa3d.res $(DLLFLAGS) $(MESA3D_DEF)

mesa3d.w98me.dll: $(LIBS_TO_BUILD) $(DEPS) mesa3d.res $(LD_DEPS)
	$(LD) $(SIMD_LDFLAGS) $(MesaWglLibSimd_OBJS) $(MesaGdiLibICDSimd_OBJS) $(opengl_simd_LIBS) $(MESA_SIMD_LIBS) mesa3d.res $(DLLFLAGS) $(MESA3D_DEF) 

# accelerated ICD driver
vmwsgl32.dll: $(LIBS_TO_BUILD) $(MesaWgl_OBJS) $(MesaGdiLibVMW_OBJS) $(MesaSVGALib_OBJS) $(MesaSVGAWinsysLib_OBJS) $(DEPS) vmwsgl32.res $(LD_DEPS)
	$(LD) $(LDFLAGS) $(MesaWglLib_OBJS) $(MesaGdiLibVMW_OBJS) $(MesaSVGALib_OBJS) $(MesaSVGAWinsysLib_OBJS) $(OPENGL_LIBS) $(MESA_LIBS) vmwsgl32.res $(DLLFLAGS) $(OPENGL_DEF)

# benchmark
glchecked_OBJS := $(glchecked_SRC:.cpp=.cpp_app$(OBJ))

ifdef DEBUG
glchecker.exe: $(glchecked_OBJS) $(DEPS) $(LD_DEPS)
	$(LD) $(APP_LDFLAGS) $(glchecked_OBJS) $(app_LIBS) $(EXEFLAGS_CMD)

else
glchecker.exe: $(glchecked_OBJS) $(DEPS) $(LD_DEPS)
	$(LD) $(APP_LDFLAGS) $(glchecked_OBJS) $(app_LIBS) $(EXEFLAGS_WIN)

endif

# ICD tester
icdtest.exe: icdtest.c_app$(OBJ) $(DEPS) $(LD_DEPS)
	$(LD) $(APP_LDFLAGS) icdtest.c_app$(OBJ) $(app_LIBS) $(EXEFLAGS_CMD)

# WGL tester
wgltest.exe: wgltest.c_app$(OBJ) $(DEPS) $(LD_DEPS)
	$(LD) $(APP_LDFLAGS) wgltest.c_app$(OBJ) $(app_LIBS) $(EXEFLAGS_CMD)

ifdef OBJ
clean:
	-$(RM) $(MesaUtilLib_OBJS)
	-$(RM) $(MesaUtilLibSimd_OBJS)
	-$(RM) $(MesaLib_OBJS)
	-$(RM) $(MesaLibSimd_OBJS)
	-$(RM) $(MesaGalliumAuxLib_OBJS)
	-$(RM) $(MesaGalliumAuxLibSimd_OBJS)
	-$(RM) $(MesaGalliumLLVMPipe_OBJS)
	-$(RM) $(MesaSVGALib_OBJS)
	-$(RM) $(MesaGdiLibGL_OBJS)
	-$(RM) $(MesaGdiLibGLSimd_OBJS)
	-$(RM) $(MesaGdiLibICD_OBJS)
	-$(RM) $(MesaGdiLibICDSimd_OBJS)
	-$(RM) $(MesaWglLib_OBJS)
	-$(RM) $(MesaWglLibSimd_OBJS)
	-$(RM) $(MesaWglLib_OBJS)
	-$(RM) $(glchecked_OBJS)
	-$(RM) $(MesaGdiLibVMW_OBJS)
	-$(RM) $(MesaSVGALib_OBJS)
	-$(RM) $(MesaSVGAWinsysLib_OBJS)
	-$(RM) icdtest.c_app$(OBJ)
	-$(RM) wgltest.c_app$(OBJ)
	-$(RM) $(LIBPREFIX)MesaUtilLib$(LIBSUFFIX)
	-$(RM) $(LIBPREFIX)MesaUtilLibSimd$(LIBSUFFIX)
	-$(RM) $(LIBPREFIX)MesaLib$(LIBSUFFIX)
	-$(RM) $(LIBPREFIX)MesaLibSimd$(LIBSUFFIX)
	-$(RM) $(LIBPREFIX)MesaGalliumAuxLib$(LIBSUFFIX)
	-$(RM) $(LIBPREFIX)MesaGalliumAuxLibSimd$(LIBSUFFIX)
	-$(RM) $(LIBPREFIX)MesaGalliumLLVMPipe$(LIBSUFFIX)
	-$(RM) $(LIBPREFIX)MesaSVGALib$(LIBSUFFIX)
	-$(RM) $(LIBPREFIX)MesaGdiLibGL$(LIBSUFFIX)
	-$(RM) $(LIBPREFIX)MesaGdiLibGLSimd$(LIBSUFFIX)
	-$(RM) $(LIBPREFIX)MesaGdiLibICD$(LIBSUFFIX)
	-$(RM) $(LIBPREFIX)MesaGdiLibICDSimd$(LIBSUFFIX)
	-$(RM) $(LIBPREFIX)MesaGdiLib$(LIBSUFFIX)
	-$(RM) $(LIBPREFIX)MesaGdiLibSimd$(LIBSUFFIX)
	-$(RM) $(LIBPREFIX)MesaWglLib$(LIBSUFFIX)
	-$(RM) $(LIBPREFIX)MesaWglLibSimd$(LIBSUFFIX)
	-$(RM) vmwsgl32.res
	-$(RM) opengl32.res
	-$(RM) mesa3d.res
	-$(RM) opengl32.w95.dll
	-$(RM) $(LIBPREFIX)opengl32.w95$(LIBSUFFIX)
	-$(RM) opengl32.w98me.dll
	-$(RM) $(LIBPREFIX)opengl32.w98me$(LIBSUFFIX)
	-$(RM) mesa3d.w95.dll
	-$(RM) $(LIBPREFIX)mesa3d.w95$(LIBSUFFIX)
	-$(RM) mesa3d.w98me.dll
	-$(RM) $(LIBPREFIX)mesa3d.w98me$(LIBSUFFIX)
	-$(RM) vmwsgl32.dll
	-$(RM) $(LIBPREFIX)vmwsgl32$(LIBSUFFIX)
	-$(RM) glchecker.exe
	-$(RM) icdtest.exe
	-$(RM) wgltest.exe
	-cd winpthreads && $(MAKE) clean
endif
