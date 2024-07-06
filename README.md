# Mesa3D port for Windows 9x
This project is simple way to use OpenGL accelerated applications under Windows 98/Me in virtual machine even without real HW acceleration support. OpenGL support can be pure software though Mesa3D library with LLVMpipe driver. This project is also providing some support for accelerated framebuffer support and real hardware acceleration (through VMWare SVGA3D protocol).

There are also some disadvantages - at first, software rendering is much slower and for gaming you need high performance host CPU. Second, the guest system must support SSE instruction set (Windows 98 + Me, but no Windows 95). You also need attach more memory to virtual machine - the library is huge (about 100 MB) and need fit to RAM itself + it's emulation of GPU memory.

## Full accelerated package
This is only OpenGL driver, if you need ready-to-use package for running DirectX, OpenglGL, Glide application and games use this: https://github.com/JHRobotics/softgpu

## Requirements
Windows 98/Me with MSVC runtime (installed with Internet Explorer 4.0 and newer). Windows 95 doesn't support SSE, so rendering is slow (can be hacked, see: [#Optimalizations](#optimalizations)). Binaries also working on all newer Windows (from NT 3.51 to 11).

~Mesa 17.x build for Windows 95 require at last Pentium II CPU (this is theoretical and emulator value, I don't expect anyone to run it on a *real* Pentium II)~

~Mesa 17.x build for Windows 98 require at last Intel Core2 CPU (SSE3 required).~

~Mesa 21.x build for Windows 98 require at last Intel Core-i CPU (SSE4.2 required).~

Current Mesa 23.1.6 is supported on Windows 95/98/Me. **Build for Windows 95** is without SSE instrumentation, so you can use Windows 95 binaries if you want run this on virtual machine/emulator without SSE support.

If you decide to use **98/Me build**, your (virtual) CPU needs support these instructions MMX, SSE, SSE2, SSE3, SSSE3, CX16, SAHF and FXSR (Intel Core2).


## Usage
There is 2 ways to use this library/driver: first is traditional `opengl32.dll` which you can copy to directory with your application which you would like to run and second one OpenGL ICD (Installable Client Driver).

### opengl32.dll in application directory
This way is preferred if you want "accelerate" one application - download `mesa3d_9x_llvmpipe.zip` from releases unpack to your application directory. There are 2 drawing drivers availed - LLVMpipe and softpipe. LLVMpipe (default) is faster, and drawing is accelerated though LLVM, softpipe is reference driver and it's much slower, but doesn't need SSE so it is usable on Windows 95. If you have space problem (HDD or RAM) you can use `mesa9x-*-sp-opengl32.zip` variant which is smaller library but contain only softpipe driver.

You can theoretically copy `opengl32.dll` to `WINDOWS\SYSTEM` directory (and replace system one), but some applications using some sort of Microsoft specific software rendering and if you replace original DLL, this application stops working. These are for example 3D screensavers and life without 3D Maze is... umm... as life without 3D Maze! :-) But despite these minor problems, it has no effect on the stability of the system. OR you can use OpenGL ICD!

### OpenGL ICD
In Microsoft Windows OS is OpenGL driver implement different way than DirectX API which is part of video driver: in OpenGL case the video driver reports only name of OpenGL driver system and `opengl32.dll` only forward OpenGL calls to library associated with this driver (name of `DLL` is in registry). On Windows 7 and newer you need only entry in registry but for Windows 9X I created slightly modified version of [Michal Necasek's VirtualBox driver](http://www.os2museum.com/wp/windows-9x-video-minidriver-hd/), driver is here https://github.com/JHRobotics/vmdisp9x

## Configuration

The standard way to configure Mesa3D is described in [manual](https://docs.mesa3d.org/envvars.html) - using environmental values. Another way is set to registry to `HKLM\Software\Mesa3D` you can use subkey `global` for settings that are used for all applications or use `name of program exe` subkey for specific application.

For example, some old games has fixed-length buffer for GL extensions string and it's shorter than `glGetString(GL_EXTENSIONS)`, which needs to be limited, one is Quake II:
```
REGEDIT4

[HKEY_LOCAL_MACHINE\Software\Mesa3D\quake2.exe]
"MESA_EXTENSION_MAX_YEAR"="2000"
```

Some useful variables:
- `MESA_NO_DITHER`: disable dithering and improve speed a bit
- `MESA_EXTENSION_MAX_YEAR`: if you have problem with too many GL extensions (see previous example)
- `GALLIUM_DRIVER`: `llvmpipe` (default) or `softpipe` for reference (and slower) driver if you have problem with `llvmpipe`
- `LP_NATIVE_VECTOR_WIDTH`: only usable values are `128` for SSE accelerated LLVM code or `256` for AVX accelerated code, any other values lead to crash.

## Optimizations
For Windows 95 you can enable SSE and use 98 version with LLVMpipe. But here is one big warning: Windows 95 doesn't save/restore XMM registers on task switch, so if you run more than one application which using SSE you'll see errors and faults. You can do it copying `simd95.com` to `C:\` and add following line to `autoexec.bat`
```
C:\simd95.com
```

This program must by run from real mode without memory manager! (That's why you need add this utility to `autoexec.bat` and not run it directly).

### AVX
You can enable AVX instruction set in Windows 98 by same utility as SSE in 95. And with same disadvantages. (Except AVX instructions are rare in modern application so usage is relatively safe). But first you must turn in for virtual machine. For older VirtualBox (in newer releases AVX are turned on by default) this command enables AVX:
```
VBoxManage setextradata "Virtual machine name" VBoxInternal/CPUM/IsaExts/AVX 1
```
And if you CPU supporting you can enable AVX2 too:
```
VBoxManage setextradata "Virtual machine name" VBoxInternal/CPUM/IsaExts/AVX2 1
```

Now copy `simd95.com` to `C:\` and add it to `autoexec.bat`. After reboot SSE and AVX are exposed to programs, but using AVX in this port of Mesa3D is disabled by default (for obvious reasons). You can turn on in registry by setting
`HKLM\Software\Mesa3D\global\LP_NATIVE_VECTOR_WIDTH` to `256`.

You can now check if it's working and test if it's stable.

HINT: You can set this setting to specific application(s) because isn't safe run more than one AVX application.

### Real hardware acceleration

For VirtualBox 7.0.x use following command to turn on acceleration: (*My Windows 98* is your virtual machine name)
```
VBoxManage modifyvm "My Windows 98" --graphicscontroller=vmsvga
```
You can enable switch GPU in GUI *but* Oracle software tries to be smarter than its users, so every setting in GUI consequence GPU change back to VBoxVGA (and without acceleration).

If it's bother you, change virtual machine type to Linux.
```
VBoxManage setextradata "My Windows 98" --os-type=Linux
```
VirtualBox is now happy with you virtual GPU choose.

VirtualBox 7.0.x can use 2 variant of virtual GPU (vGPU):

#### vGPU9

Allow OpenGL 2.1, combination width [Wine](https://github.com/JHRobotics/wine9x/) allows using using DirectX up to 9, but without shaders. 

Default and only one variant in VirtualBox 6.x, but for VirtualBox 7, you have to use this command to set **vGPU9**:

```
VBoxManage setextradata "My Windows 98" "VBoxInternal/Devices/vga/0/Config/VMSVGA10" "0"
```

When using vGPU9, please set system RAM to at last 256 MB.

#### vGPU10

This is default option in VirtualBox 7 and currently allow OpenGL 4.1. Is more flexible and have more features compared to vGPU9, but needs more RAM and in some case (for example Quake 2 based games) is slower than vGPU10. You can force using vGPU10 by this command:

```
VBoxManage setextradata "My Windows 98" "VBoxInternal/Devices/vga/0/Config/VMSVGA10" "0"
```

When using vGPU9, please set system RAM to at last 512 MB, but for later DX9 applications you need 1 GB RAM.

VirtualBox can choose between vGPU9 and vGPU10 and turn off GPU10 for example when you have unsupported HW, to set default behaviour, you can use this command:

```
VBoxManage setextradata "My Windows 98" "VBoxInternal/Devices/vga/0/Config/VMSVGA10" ""
```


## Source code
This repository contains more projects modified for Win9x:
- Mesa3D v17.3.9 from VirtualBox (6.1.x) ~~in future plan is support 3D acceleration with VMWARE SVGA II/VirtualBox SVGA virtual cards.~~ (works now!)
- Mesa3D v21.3.8 from VirtualBox 7.0
- Mesa3D v23.1.x, mainline version, from https://mesa3d.org/
- ~~library winpthreads from MinGW, in Windows 9x completly missing `AddVectoredExceptionHandler` (that's why usually `g++` programs refuses to start) and `TryEnterCriticalSection` which exists as entry point in kernel32, but only returns failure - which is very funny in conditional and leads to deadlocks (in better case) or memory/resources leaks (I spend on it some dreamless nights, investigating why OpenGL programs crash about 1 hour of running).~~ moved here: https://github.com/JHRobotics/pthread9x
- ~~VBox video minidriver, my very light modification adds OpenGL ICD support. (by Michal Necasek, http://www.os2museum.com/wp/windows-9x-video-minidriver-hd/)~~ (Driver moved here https://github.com/JHRobotics/vmdisp9x, the modifications were somewhat more extensive)
- OpenGLChecker, simple program to benchmark OpenGL (by David Liu, https://sourceforge.net/projects/openglchecker/)
- some extra utilities ~~and SIMD enable hack~~ (SIMD95 moved here: https://github.com/JHRobotics/simd95)
- 9x patches for LLVM 5.0.2 and 6.0.1

## Compilation from source

You need:
- MinGW that can produce working binary for Windows 9x
- LLVM source (3.x to 6.x, 6.0.1 recommended)
- python 2.7 (or newer, for LLVM, python 3.6+ for generating Mesa sources)
- cmake (for LLVM)
- zlib (for LLVM)
- flex (generating Mesa sources, recommend using msys version)
- bison (generating Mesa sources, recommend using msys version)
- mako (generating Mesa sources, install from python or msys)
- GNU patch
- GNU make (usually packed with MinGW)

### MINGW
If targeting Windows 98/Me you can use actual MinGW from [MSYS2 project](https://www.msys2.org/), since I replacing *winpthreads* and reimplementing *strtoll* and *strtoull* from CRT.

For targeting WIN95/NT, you need MinGW build without SSE instructions in runtime. Current releases are compiled by [this MinGW build](https://github.com/niXman/mingw-builds-binaries/releases/download/13.1.0-rt_v11-rev1/i686-13.1.0-release-posix-dwarf-msvcrt-rt_v11-rev1.7z) from [this project](https://github.com/niXman/mingw-builds-binaries/).

### LLVM (6.0.1)
Mesa3D require LLVM 3.9 and later, and 6.0.1 was last one before Mesa version 17.3.9 was released, so newer version may works but additional modification may be required. Usually newer LLVM is faster. 
https://releases.llvm.org/download.html#6.0.1 and download *LLVM source code* (`llvm-6.0.1.src.tar.xz`)
https://releases.llvm.org/5.0.2/llvm-5.0.2.src.tar.xz

Apply patch for you release (on example 6.0.1):
```
cd C:\source\llvm-6.0.1.src
patch -p1 < ../mesa9x/llvm/llvm-9x-6.0.1.patch
```

Create directory for build and run configure, CMAKE_INSTALL_PREFIX is directory where install LLVM build (eg. != source and != build directory)
```
mkdir build
cd build
cmake -G"MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=C:/source/llvm-build ..
```

Now you can run make, use `-j` to set number of parallel compilations at same time (faster = number of CPUs * number of CPU cores * number of CPU threads, example: 1x quadcore CPU with hyperthreading = 8)

```
mingw32-make -j8
```

After complete (take some time), install to prefix directory
```
mingw32-make install
```

### LLVM (18.x.x)
Experimentally it is possible usage of current LLVM version.

You need LLVM self (for example: `llvm-18.1.8.src.tar.xz`) + extra cmake files (for example: `cmake-18.1.8.src.tar.xz`). Extract archives to somewhere (in examples it is `C:\source\llvm`) and rename `cmake-x.y.z.src` to just `cmake`. Also create `llvm-build` directory for build files and `llvm-win32` for installation. Copy also `llvm-9x-18.1.8.patch` from Mesa9x repository here.  Directory tree should like:

 ```
├───cmake
│   └───Modules
├───llvm-18.1.8.src
│   ├───benchmarks
│   ├───bindings
│   ├───cmake
│   └─── ...
├───llvm-build (empty)
├───llvm-win32 (empty)
└───llvm-9x-18.1.8.patch

```

Now apply LLVM 9x patch:

```
cd C:\source\llvm\llvm-18.1.8.src
patch -p1 < ../llvm-9x-18.1.8.patch
```

Run `cmake`:

```
cd C:\source\llvm\llvm-build
cmake -G"MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release -DLLVM_INCLUDE_TESTS=OFF -DLLVM_INCLUDE_BENCHMARKS=OFF -DLLVM_TARGETS_TO_BUILD=X86 -DLLVM_ENABLE_ZSTD=OFF -DCMAKE_INSTALL_PREFIX=C:/source/llvm/llvm-win32 ../llvm-18.1.8.src
```

Run `make` (`-j8` is number if building threads, please adjust this value for your CPU):

```
mingw32-make -j8
```

After complete (take some time), install to prefix directory
```
mingw32-make install
```

For this example, the entry to Mesa9x `config.mk` should like this:

```
MESA_VER = mesa-24.0.x
LLVM_DIR = C:/source/llvm/llvm-win32
LLVM_VER = 0x1212
LLVM_2024 = 1
```


### Mesa3D
I omitted classic configure file and create only GNU Make file - target is only one and this is more portable.

Copy `config.mk.sample` to `config.mk` as open in in text editor and follow instructions, if you don't some special options (debug, targeting specific CPU) you just set `LLVM_DIR` to LLVM installation (same directory as you set in CMAKE_INSTALL_PREFIX).

Now you can build project
```
mingw32-make -j8
```

### Visual studio (outdated and not much tested)
Microsoft Visual Studio 2005 (at last Professional, not free Express Edition) is last official with Windows 9x target support. And we need at last version 2015 to build this project (required is C++11 or C++14 support) - Visual Studio IS NOT WAY to produce Windows 95/98/Me binary. But it can be useful at debug. So short description how to build:

At first, build LLVM:
```
cmake -G"NMake Makefiles" -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=C:/source/llvm-build-msc ..
set CL=/MP
nmake
nmake install
```
