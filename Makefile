#
# This is GNU make file DON'T use it with nmake/wmake or whatever even if you
# plan to use MS compiler!
#

################################################################################
# Copyright (c) 2022-2023 Jaroslav Hensl                                       #
#                                                                              #
# See LICENCE file for law informations                                        #
# See README.md file for more detailed build instructions                      #
#                                                                              #
################################################################################

#
# Usage:
#   1) copy config.mk-sample to config.mk
#   2) edit config.mk
#   3) run make
#

include config.mk

MESA_VER ?= mesa-21.3.x
DEPS = config.mk Makefile

ifeq ($(MESA_VER),mesa-17.3.9)
  MESA_MAJOR := 17
endif

ifeq ($(MESA_VER),mesa-21.3.x)
  MESA_MAJOR := 21
  MESA_GPU10 := 1
endif

ifeq ($(MESA_VER),mesa-23.1.x)
  MESA_MAJOR := 23
  MESA_GPU10 := 1
endif

ifeq ($(MESA_VER),mesa-24.0.x)
  MESA_MAJOR := 24
  MESA_GPU10 := 1
endif

# only usefull with gcc/mingw
CSTD=c99
ifdef MESA_GPU10
  CXXSTD=gnu++14
else
  CXXSTD=gnu++11
endif

# base address from 98/NT opengl32.dll
BASE_opengl32.w95.dll   := 0x78A40000
BASE_opengl32.w98me.dll := 0x78A40000

# base address from nvidia ICD driver
BASE_mesa3d.w95.dll     := 0x69500000
BASE_mesa3d.w98me.dll   := 0x69500000

BASE_vmwsgl32.dll       := 0x69500000
BASE_mesa99.dll         := 0x00990000
BASE_mesa89.dll         := 0x00A30000
BASE_mesad3d10.w95.dll  := 0x10000000
BASE_mesad3d10.w98me.dll  := 0x10000000

NULLOUT=$(if $(filter $(OS),Windows_NT),NUL,/dev/null)

VERSION_BUILD := 0

GIT      ?= git
GIT_IS   := $(shell $(GIT) rev-parse --is-inside-work-tree 2> $(NULLOUT))
ifeq ($(GIT_IS),true)
  VERSION_BUILD := $(shell $(GIT) rev-list --count main)
endif

TARGETS = opengl32.w95.dll mesa3d.w95.dll vmwsgl32.dll glchecker.exe icdtest.exe wgltest.exe mesa99.dll mesa89.dll
ifdef LLVM
  TARGETS += opengl32.w98me.dll mesa3d.w98me.dll
endif

ifdef LLVM
  ifndef LLVM_VER
    $(error Define LLVM_VER in config.mk please!)
  endif
endif

all: generator $(TARGETS)
.PHONY: generator all clean distclean

include $(MESA_VER).mk

PACKAGE_VERSION := 9x $(MESA_DIST_VERSION).$(VERSION_BUILD)

ifdef MSC
#
# MSC configuration
#
  OBJ := .obj
  LIBSUFFIX := .lib
  LIBPREFIX := 

  CL_INCLUDE = /Iincmsc /I$(MESA_VER)/include	/I$(MESA_VER)/include/GL /I$(MESA_VER)/src/mapi	/I$(MESA_VER)/src/util /I$(MESA_VER)/src /I$(MESA_VER)/src/mesa /I$(MESA_VER)/src/mesa/main \
    /I$(MESA_VER)/src/compiler -I$(MESA_VER)/src/compiler/nir /I$(MESA_VER)/src/gallium/state_trackers/wgl /I$(MESA_VER)/src/gallium/auxiliary /I$(MESA_VER)/src/gallium/auxiliary/util /I$(MESA_VER)/src/gallium/include \
    /I$(MESA_VER)/src/gallium/drivers/svga /I$(MESA_VER)/src/gallium/drivers/svga/include /I$(MESA_VER)/src/gallium/winsys/sw  /I$(MESA_VER)/src/gallium/drivers /I$(MESA_VER)/src/gallium/winsys/svga/drm

  # /DNDEBUG
	CL_DEFS =  /D__i386__ /D_X86_ /D_USE_MATH_DEFINES /D_WIN32 /DWIN32 \
	  /DMAPI_MODE_UTIL /D_GDI32_ /DBUILD_GL32 /DKHRONOS_DLL_EXPORTS /DGDI_DLL_EXPORTS /DGL_API=GLAPI /DGL_APIENTRY=GLAPIENTRY /D_GLAPI_NO_EXPORTS /DCOBJMACROS /DINC_OLE2 \
	  /DPACKAGE_VERSION="\"$(PACKAGE_VERSION)\"" /DPACKAGE_BUGREPORT="\"$(PACKAGE_VERSION)\""
  
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
  
  DLLFLAGS = -o $@ -shared -Wl,--dll,--out-implib,lib$(@:dll=a),--exclude-all-symbols,--exclude-libs=pthread,--image-base,$(BASE_$@)$(TUNE_LD)
  
  OPENGL_DEF = opengl32.mingw.def
  MESA3D_DEF = mesa3d.mingw.def
  MESA99_DEF = mesa99.mingw.def
  MESA89_DEF = mesa89.mingw.def
  D3D10_DEF  = d3d10_sw.def

  INCLUDE = -Iinclude -Iwinpthreads/include -I$(MESA_VER)/include	-I$(MESA_VER)/include/GL -I$(MESA_VER)/src/mapi	-I$(MESA_VER)/src/util -I$(MESA_VER)/src -I$(MESA_VER)/src/mesa -I$(MESA_VER)/src/mesa/main \
    -I$(MESA_VER)/src/compiler -I$(MESA_VER)/src/compiler/nir -I$(MESA_VER)/src/gallium/state_trackers/wgl -I$(MESA_VER)/src/gallium/auxiliary -I$(MESA_VER)/src/gallium/auxiliary/util -I$(MESA_VER)/src/gallium/include \
    -I$(MESA_VER)/src/gallium/drivers/svga -I$(MESA_VER)/src/gallium/drivers/svga/include -I$(MESA_VER)/src/gallium/winsys/sw  -I$(MESA_VER)/src/gallium/drivers -I$(MESA_VER)/src/gallium/winsys/svga/drm \
    -I$(MESA_VER)/src/util/format -I$(MESA_VER)/src/gallium/frontends/wgl -I$(MESA_VER)/include/D3D9 -I$(MESA_VER)/src/gallium/frontends -I$(MESA_VER)/src/gallium/frontends/wgl -I$(MESA_VER)/include/D3D9 \
    -I$(MESA_VER)/src/gallium/frontends/nine -I$(MESA_VER)/include/winddk -Iinclude/winddk -Iwin9x

  DEFS =  -D__i386__ -D_X86_ -D_WIN32 -DWIN32 -DWIN9X -DWINVER=0x0400 -DHAVE_PTHREAD \
    -DBUILD_GL32 -D_GDI32_ -DGL_API=GLAPI -DGL_APIENTRY=GLAPIENTRY \
    -D_FILE_OFFSET_BITS=64 -D_LARGEFILE_SOURCE \
    -DMAPI_MODE_UTIL -D_GLAPI_NO_EXPORTS -DCOBJMACROS -DINC_OLE2 \
    -DPACKAGE_VERSION="\"$(PACKAGE_VERSION)\"" -DPACKAGE_BUGREPORT="\"$(PACKAGE_VERSION)\"" -DMALLOC_IS_ALIGNED -DHAVE_CRTEX \
    -DHAVE_OPENGL=1 -DHAVE_OPENGL_ES_2=0 -DHAVE_OPENGL_ES_1=0 -DWINDOWS_NO_FUTEX -DGALLIUM_SOFTPIPE
  
#  VBOX_WITH_MESA3D_D3D_FROM_SYSTEMMEM Create D3DPOOL_SYSTEMMEM textures from provided system memory pointer.
#  VBOX_WITH_MESA3D_NINE_SVGA          Make the D3D state tracker to work together with VMSVGA.
#  VBOX_WITH_MESA3D_SVGA_GPU_FINISHED  PIPE_QUERY_GPU_FINISHED in VMSVGA driver.
#  VBOX_WITH_MESA3D_SVGA_HALFZ         D3D Z coord [0.0;1.0] in the Gallium SVGA driver (VGPU9 only).
#  VBOX_WITH_MESA3D_SVGA_INSTANCING    Instancing for DrawPrimitives in the Gallium SVGA driver
#                                      (VGPU9 only, VGPU10 has it).
  
  DEFS += -DVBOX_WITH_MESA3D_STENCIL_CLEAR -DVBOX_WITH_MESA3D_DXFIX
  #DEFS += -DVBOX_WITH_MESA3D_NINE_SVGA -DVBOX_WITH_MESA3D_SVGA_HALFZ -DVBOX_WITH_MESA3D_SVGA_INSTANCING -DVBOX_WITH_MESA3D_SVGA_GPU_FINISHED
  
  ifdef MESA_GPU10
    DEFS += -DVBOX_WITH_MESA3D_SVGA_HALFZ -DVBOX_WITH_MESA3D_NINE_SVGA
  else
    DEFS += -DVBOX_WITH_MESA3D_SVGA_INSTANCING
  endif

  ifdef VERSION_BUILD
    DEFS  += -DMESA9X_BUILD=$(VERSION_BUILD)
  endif
  
    DEFS += -DMESA_MAJOR=$(MESA_MAJOR)

	OPENGL_LIBS = -L. -lMesaLib -lMesaUtilLib -lMesaGalliumAuxLib -lMesaUtilLib -lMesaLib
	SVGA_LIBS   = -L. -lMesaLib -lMesaUtilLib -lMesaGalliumAuxLib -lMesaSVGALib -lMesaLib
  MESA_LIBS  := winpthreads/crtfix.o -static -Lwinpthreads -lpthread -lkernel32 -luser32 -lgdi32
  MESA89_LIBS := -lgdi32
  
  ifdef DEBUG
    DD_DEFS = -DDEBUG
  else
    DD_DEFS = -DNDEBUG -DD3D8TO9NOLOG
  endif
    
  ifdef GUI_ERRORS
  	TUNE += -DGUI_ERRORS
  endif

  ifdef LLVM
    SIMD_DEFS = $(DEFS) -DHAVE_LLVM=$(LLVM_VER) -DHAVE_GALLIUM_LLVMPIPE -DGALLIUM_LLVMPIPE -DHAVE_LLVMPIPE -DDRAW_LLVM_AVAILABLE
    SIMD_INCLUDE += $(INCLUDE) -I$(LLVM_DIR)/include
    
    opengl_simd_LIBS := -L. -L$(LLVM_DIR)/lib -lMesaLibSimd -lMesaUtilLibSimd -lMesaGalliumLLVMPipe -lMesaGalliumAuxLibSimd -lMesaLibSimd -lMesaUtilLibSimd
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
      SIMD_CFLAGS   = -std=$(CSTD) $(filter-out -pedantic -Wall -W -Wextra -march=westmere -march=core2,$(LLVM_CFLAGS)) $(TUNE) $(SIMD_INCLUDE) $(SIMD_DEFS) $(filter-out -DDEBUG -DNDEBUG,$(DD_DEFS))
      SIMD_CXXFLAGS = $(filter-out -pedantic -pedantic -Wall -W -Wextra -march=westmere -march=core2 -std=gnu++11,$(LLVM_CXXFLAGS)) -std=$(CXXSTD) $(TUNE) $(SIMD_INCLUDE) $(SIMD_DEFS) $(filter-out -DDEBUG -DNDEBUG,$(DD_DEFS))
    else
      SIMD_CFLAGS = -std=$(CSTD) -O1 -g  $(TUNE) $(SIMD_INCLUDE) $(SIMD_DEFS) $(DD_DEFS)
      SIMD_CXXFLAGS = -std=$(CXXSTD) -O1 -g $(TUNE) $(SIMD_INCLUDE) $(SIMD_DEFS) $(DD_DEFS)
    endif
    SIMD_LDFLAGS = -std=$(CXXSTD)
  endif
  
  ifdef SPEED
    CFLAGS = -std=$(CSTD) -O3 -fomit-frame-pointer -fno-exceptions $(TUNE) $(INCLUDE) $(DD_DEFS) $(DEFS)
    CXXFLAGS = -std=$(CXXSTD) -O3 -fomit-frame-pointer -fno-exceptions -fno-rtti $(TUNE) $(INCLUDE) $(DD_DEFS) $(DEFS)
    LDFLAGS = -std=$(CXXSTD) -O3 -fno-exceptions
  else
    CFLAGS = -std=$(CSTD) -O0 -g $(TUNE) $(INCLUDE) $(DD_DEFS) $(DEFS)
    CXXFLAGS = -std=$(CXXSTD) -O0 -g  $(TUNE) $(INCLUDE) $(DD_DEFS) $(DEFS)
    LDFLAGS = -std=$(CXXSTD)
  endif

  ifdef SPEED
    APP_CFLAGS   = -std=$(CSTD) -O3 -fomit-frame-pointer -fno-exceptions -I. -Iglchecker $(TUNE) $(INCLUDE) $(DD_DEFS) $(DEFS)
    APP_CXXFLAGS = -std=$(CXXSTD) -O3 -fomit-frame-pointer -fno-exceptions -fno-rtti $(INCLUDE) $(DD_DEFS) $(DEFS)
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
      LDFLAGS    += $(LLVM_CXXFLAGS) -flto -fno-fat-lto-objects -fno-strict-aliasing
    else
      LDFLAGS    += -flto -fno-fat-lto-objects -fno-strict-aliasing
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

eight_SRC = \
  win9x/eight/d3d8to9.cpp \
  win9x/eight/d3d8to9_base.cpp \
  win9x/eight/d3d8to9_device.cpp \
  win9x/eight/d3d8to9_index_buffer.cpp \
  win9x/eight/d3d8to9_surface.cpp \
  win9x/eight/d3d8to9_swap_chain.cpp \
  win9x/eight/d3d8to9_texture.cpp \
  win9x/eight/d3d8to9_vertex_buffer.cpp \
  win9x/eight/d3d8to9_volume.cpp \
  win9x/eight/d3d8types.cpp \
  win9x/eight/interface_query.cpp

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

MesaGdiLib_OBJS := $(MesaGdiLib_SRC:.c=.c_gen$(OBJ))
MesaGdiLib_OBJS := $(MesaGdiLib_OBJS:.cpp=.cpp_gen$(OBJ))
MesaGdiLibSimd_OBJS := $(MesaGdiLib_SRC:.c=.c_simd$(OBJ))
MesaGdiLibSimd_OBJS := $(MesaGdiLibSimd_OBJS:.cpp=.cpp_simd$(OBJ))

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

MesaNineLib_OBJS := $(MesaNineLib_SRC:.c=.c_gen$(OBJ))
MesaNineLib_OBJS := $(MesaNineLib_OBJS:.cpp=.cpp_gen$(OBJ))

MesaD3D10Lib_OBJS := $(MesaD3D10Lib_SRC:.c=.c_gen$(OBJ))
MesaD3D10Lib_OBJS := $(MesaD3D10Lib_OBJS:.cpp=.cpp_gen$(OBJ))

MesaD3D10LibSimd_OBJS := $(MesaD3D10Lib_SRC:.c=.c_simd$(OBJ))
MesaD3D10LibSimd_OBJS := $(MesaD3D10LibSimd_OBJS:.cpp=.cpp_simd$(OBJ))

eight_OBJS := $(eight_SRC:.c=.c_gen$(OBJ))
eight_OBJS := $(eight_OBJS:.cpp=.cpp_gen$(OBJ))

# software opengl32 replacement
opengl32.w95.dll: $(LIBS_TO_BUILD) $(DEPS) opengl32.res $(LD_DEPS)
	$(LD) $(LDFLAGS) $(MesaWglLib_OBJS) $(MesaGdiLibGL_OBJS) $(OPENGL_LIBS) $(MESA_LIBS) opengl32.res $(DLLFLAGS) $(OPENGL_DEF)

opengl32.w98me.dll: $(LIBS_TO_BUILD) $(DEPS) opengl32.res $(LD_DEPS)
	$(LD) $(LDFLAGS) $(MesaWglLibSimd_OBJS) $(MesaGdiLibGLSimd_OBJS) $(opengl_simd_LIBS) $(MESA_SIMD_LIBS) opengl32.res $(DLLFLAGS) $(OPENGL_DEF)

# software ICD driver
mesa3d.w95.dll: $(LIBS_TO_BUILD) $(DEPS) mesa3d.res $(LD_DEPS)
	$(LD) $(LDFLAGS) $(MesaWglLib_OBJS) $(MesaGdiLibICD_OBJS) $(OPENGL_LIBS) $(MESA_LIBS) mesa3d.res $(DLLFLAGS) $(MESA3D_DEF)

mesa3d.w98me.dll: $(LIBS_TO_BUILD) $(DEPS) mesa3d.res $(LD_DEPS)
	$(LD) $(SIMD_LDFLAGS) $(MesaWglLibSimd_OBJS) $(MesaGdiLibICDSimd_OBJS) $(opengl_simd_LIBS) $(MESA_SIMD_LIBS) mesa3d.res $(DLLFLAGS) $(MESA3D_DEF) 

# accelerated ICD driver
vmwsgl32.dll: $(LIBS_TO_BUILD) $(MesaWgl_OBJS) $(MesaGdiLibVMW_OBJS) $(MesaSVGALib_OBJS) $(MesaSVGAWinsysLib_OBJS) $(DEPS) vmwsgl32.res $(LD_DEPS)
	$(LD) $(LDFLAGS) $(MesaWglLib_OBJS) $(MesaGdiLibVMW_OBJS) $(MesaSVGALib_OBJS) $(MesaSVGAWinsysLib_OBJS) $(OPENGL_LIBS) $(MESA_LIBS) vmwsgl32.res $(DLLFLAGS) $(OPENGL_DEF)

mesa99.dll: mesa3d.w95.dll $(MesaNineLib_OBJS)
	$(LD) $(LDFLAGS) $(MesaNineLib_OBJS) $(OPENGL_LIBS) $(MESA_LIBS) $(DLLFLAGS) $(MESA99_DEF)

mesa89.dll: mesa99.dll $(eight_OBJS)
	$(LD) $(LDFLAGS) $(MesaNineLib_OBJS) $(OPENGL_LIBS) $(MESA_LIBS) $(eight_OBJS) $(MESA89_LIBS) $(DLLFLAGS) $(MESA89_DEF)

mesad3d10.w95.dll: $(LIBS_TO_BUILD) $(DEPS) $(LD_DEPS) $(MesaD3D10Lib_OBJS)
	$(LD) $(LDFLAGS) $(MesaD3D10Lib_OBJS) $(MesaGdiLib_OBJS) $(OPENGL_LIBS) $(MESA_LIBS) $(DLLFLAGS) $(D3D10_DEF)
	
mesad3d10.w98me.dll: $(LIBS_TO_BUILD) $(DEPS) $(LD_DEPS) $(MesaD3D10LibSimd_OBJS)
	$(LD) $(LDFLAGS) $(MesaD3D10LibSimd_OBJS) $(MesaGdiLibSimd_OBJS) $(opengl_simd_LIBS) $(MESA_SIMD_LIBS) $(DLLFLAGS) $(D3D10_DEF)

# benchmark
glchecked_OBJS := $(glchecked_SRC:.cpp=.cpp_app$(OBJ))

#ifdef DEBUG
glchecker.exe: $(glchecked_OBJS) $(DEPS) $(LD_DEPS)
	$(LD) $(APP_LDFLAGS) $(glchecked_OBJS) $(app_LIBS) $(EXEFLAGS_CMD)

#else
#glchecker.exe: $(glchecked_OBJS) $(DEPS) $(LD_DEPS)
#	$(LD) $(APP_LDFLAGS) $(glchecked_OBJS) $(app_LIBS) $(EXEFLAGS_WIN)
#
#endif

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
	-$(RM) $(MesaNineLib_OBJS)
	-$(RM) $(MesaD3D10Lib_OBJS)
	-$(RM) $(MesaD3D10LibSimd_OBJS)
	-$(RM) $(eight_OBJS)
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
	-$(RM) $(LIBPREFIX)mesa99$(LIBSUFFIX)
	-$(RM) $(LIBPREFIX)mesa89$(LIBSUFFIX)
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
	-$(RM) mesad3d10.w95.dll
	-$(RM) mesad3d10.w98me.dll
	-$(RM) mesa99.dll
	-$(RM) mesa89.dll
	-$(RM) glchecker.exe
	-$(RM) icdtest.exe
	-$(RM) wgltest.exe
	-cd winpthreads && $(MAKE) clean
endif

generator:
	$(MAKE) -f generator/$(MESA_VER)-gen.mk

distclean:
	$(MAKE) -f generator/$(MESA_VER)-gen.mk distclean
