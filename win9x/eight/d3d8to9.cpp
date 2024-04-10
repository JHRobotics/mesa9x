/**
 * Copyright (C) 2015 Patrick Mours. All rights reserved.
 * License: https://github.com/crosire/d3d8to9#license
 */

#include "d3dx9.hpp"
#include "d3d8to9.hpp"

PFN_D3DXAssembleShader D3DXAssembleShader = nullptr;
PFN_D3DXDisassembleShader D3DXDisassembleShader = nullptr;
PFN_D3DXLoadSurfaceFromSurface D3DXLoadSurfaceFromSurface = nullptr;

#ifndef D3D8TO9NOLOG
 // Very simple logging for the purpose of debugging only.
std::ofstream LOG;
#endif

#ifdef WIN9X
struct INineNine;
extern "C" 
{
	BOOL mesa99_init();
	ULONG WINAPI NineNine_AddRef(INineNine *This);
	extern INineNine *nineInst;
	typedef IDirect3D9 * (WINAPI *Direct3DCreate9f)(UINT sdk_version);
}
#endif

extern "C" IDirect3D8 *WINAPI Direct3DCreate8(UINT SDKVersion)
{
#ifndef D3D8TO9NOLOG
	static bool LogMessageFlag = true;

	if (!LOG.is_open())
	{
		LOG.open("d3d8.log", std::ios::trunc);
	}

	if (!LOG.is_open() && LogMessageFlag)
	{
		LogMessageFlag = false;
		MessageBox(nullptr, TEXT("Failed to open debug log file \"d3d8.log\"!"), nullptr, MB_ICONWARNING);
	}

	LOG << "Redirecting '" << "Direct3DCreate8" << "(" << SDKVersion << ")' ..." << std::endl;
	LOG << "> Passing on to 'Direct3DCreate9':" << std::endl;
#endif

#ifdef WIN9X
	IDirect3D9 *d3d = nullptr;
	char *use_msdx_str = getenv("D8TOD9_MSDX");
	if(use_msdx_str != NULL)
	{
		int use_msdx = atoi(use_msdx_str);
		if(use_msdx > 0)
		{
			HMODULE dll = LoadLibraryA("d3d9.dll");
			if(dll)
			{
				Direct3DCreate9f Direct3DCreate9h = (Direct3DCreate9f)GetProcAddress(dll, "Direct3DCreate9");
				if(Direct3DCreate9h)
				{
					d3d = Direct3DCreate9h(D3D_SDK_VERSION);
				}
			}
		}
	}
	
	if(d3d == nullptr)
	{
		if(mesa99_init())
		{
			NineNine_AddRef(nineInst);
			d3d = (IDirect3D9*)nineInst;
			//NineNine_new((INineNine**)&d3d);
		}
	}
#else
	IDirect3D9 *const d3d = Direct3DCreate9(D3D_SDK_VERSION);
#endif

	if (d3d == nullptr)
	{
		return nullptr;
	}

	// Load D3DX
	if (!D3DXAssembleShader || !D3DXDisassembleShader || !D3DXLoadSurfaceFromSurface)
	{
		const HMODULE module = LoadLibrary(TEXT("d3dx9_31.dll"));

		if (module != nullptr)
		{
			D3DXAssembleShader = reinterpret_cast<PFN_D3DXAssembleShader>(GetProcAddress(module, "D3DXAssembleShader"));
			D3DXDisassembleShader = reinterpret_cast<PFN_D3DXDisassembleShader>(GetProcAddress(module, "D3DXDisassembleShader"));
			D3DXLoadSurfaceFromSurface = reinterpret_cast<PFN_D3DXLoadSurfaceFromSurface>(GetProcAddress(module, "D3DXLoadSurfaceFromSurface"));
		}
#ifndef WIN9X /* JH: shader probably won't work anyway... Edit: shader works, only needed lower DLL */
		else
		{
# ifndef D3D8TO9NOLOG
			LOG << "Failed to load d3dx9_31.dll! Some features will not work correctly." << std::endl;
# endif
			if (MessageBox(nullptr, TEXT(
					"Failed to load d3dx9_31.dll! Some features will not work correctly.\n\n"
					"It's required to install the \"Microsoft DirectX End-User Runtime\"\n\n"
					"Please click \"OK\" to open the official download page or \"Cancel\" to continue anyway."), nullptr, MB_ICONWARNING | MB_TOPMOST | MB_SETFOREGROUND | MB_OKCANCEL | MB_DEFBUTTON1) == IDOK)
			{
				ShellExecute(nullptr, TEXT("open"), TEXT("https://www.microsoft.com/download/details.aspx?id=35"), nullptr, nullptr, SW_SHOW);

				return nullptr;
			}
		}
#endif
	}

	return new Direct3D8(d3d);
}


extern "C" HRESULT WINAPI ValidatePixelShader(DWORD* pixelshader, DWORD* reserved1, BOOL bool1, DWORD* toto)
{
  HRESULT ret;

  if (!pixelshader)
      return E_FAIL;

  if (reserved1)
      return E_FAIL;

  switch(*pixelshader) {
        case 0xFFFF0100:
        case 0xFFFF0101:
        case 0xFFFF0102:
        case 0xFFFF0103:
        case 0xFFFF0104:
            ret=S_OK;
            break;
        default:
            ret=E_FAIL;
        }
  return ret;
}

extern "C" HRESULT WINAPI ValidateVertexShader(DWORD* vertexshader, DWORD* reserved1, DWORD* reserved2, BOOL bool1, DWORD* toto)
{
  HRESULT ret;

  if (!vertexshader)
      return E_FAIL;

  if (reserved1 || reserved2)
      return E_FAIL;

  switch(*vertexshader) {
        case 0xFFFE0101:
        case 0xFFFE0100:
            ret=S_OK;
            break;
        default:
            ret=E_FAIL;
        }

  return ret;
}

extern "C" HRESULT WINAPI D3D8GetSWInfo(void)
{
    return 0;
}
