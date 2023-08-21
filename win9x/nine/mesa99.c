#include <windows.h>
#include <stdio.h>

#include "util/u_debug.h"
#include "stw_winsys.h"
#include "stw_device.h"
#include "stw_tls.h"
#include "stw_context.h"
#include "gdi/gdi_sw_winsys.h"

#include "adapter9.h"
#include "surface9.h"
#include "pipe/p_screen.h"

#include "mesa99.h"

#define MESA99_ADAPTER_CNT 1

#define OPENGL_GETINFO 0x1101

typedef struct _opengl_icd_t
{
	long Version;
	long DriverVersion;
	char DLL[262]; /* wchat_t DLL */
} opengl_icd_t;

static struct stw_winsys stw_winsys = {};

static HMODULE hMesa = NULL;

typedef BOOL (WINAPI *MesaGetWinsysFunc)(struct stw_winsys *out);

static BOOL GetGLICD(char *OutDllName, size_t OutDllNameSize)
{
	HWND hDesktop = GetDesktopWindow();
	HDC hdc = GetDC(hDesktop);
	DWORD testData = OPENGL_GETINFO;
	
	if(ExtEscape(hdc, QUERYESCSUPPORT, sizeof(DWORD), (LPCSTR)&testData, 0, NULL))
	{
		opengl_icd_t icd = {};
		if(ExtEscape(hdc, OPENGL_GETINFO, 0, NULL, sizeof(opengl_icd_t), (LPSTR)&icd))
		{
			HKEY hKey;
			LSTATUS lResult;
			lResult = RegOpenKeyEx(HKEY_LOCAL_MACHINE, "Software\\Microsoft\\Windows\\CurrentVersion\\OpenGLdrivers", 0, KEY_READ, &hKey);
			if(lResult == ERROR_SUCCESS)
			{
				DWORD type;
				DWORD size = OutDllNameSize;
				lResult = RegQueryValueExA(hKey, icd.DLL, NULL, &type, (LPBYTE)OutDllName, &size);
				if(lResult == ERROR_SUCCESS)
				{
					if(type == REG_SZ)
					{
						return TRUE;
					}
				}
			}
		}
	}
	return FALSE;
}

static BOOL LoadWinsysDLL(char *dllname, HMODULE *OutHMesa)
{
	MesaGetWinsysFunc proc;
	HMODULE hMesaTest;
	
	hMesaTest = LoadLibraryA(dllname);
	if(hMesaTest)
	{
		proc = (MesaGetWinsysFunc)GetProcAddress(hMesaTest, "MesaGetWinsys");
		if(proc == NULL)
		{
			FreeLibrary(hMesaTest);
		}
		else
		{
			if(proc(&stw_winsys))
			{
				if(stw_winsys.create_screen)
				{
					*OutHMesa = hMesaTest;
					return TRUE;
				}
				FreeLibrary(hMesaTest);
			}
		}
	}
	return FALSE;
}

BOOL LoadWinsys()
{
	char dllname[MAX_PATH];
	if(GetGLICD(dllname, MAX_PATH-1))
	{
		printf("mesa99: ICD success\n");
		LoadWinsysDLL(dllname, &hMesa);
	}
	
	if(!hMesa)
	{
		printf("mesa99: try opengl32.dll\n");
		LoadWinsysDLL("opengl32.dll", &hMesa);
	}
	
	if(!hMesa)
	{
		printf("mesa99: try mesa3d.dll\n");
		LoadWinsysDLL("mesa3d.dll", &hMesa);
	}
	
	/* debug */
	if(!hMesa)
	{
		printf("mesa99: try mesa3d.w95.dll\n");
		LoadWinsysDLL("mesa3d.w95.dll", &hMesa);
	}
	
	printf("mesa99: loader = %p\n", hMesa);
	
	return hMesa ? TRUE : FALSE;
}

static void d3dadapter9_context_destroy(struct d3dadapter9_context *ctx)
{
	HeapFree(GetProcessHeap(), 0, ctx);
}

static HRESULT d3dadapter9_context_create(struct d3dadapter9_context *ctx, struct pipe_screen *s)
{
	ctx->hal                        = s;
	/** @todo Need software device here. Currently assigned to hw device to prevent NineDevice9_ctor crash. */
	ctx->ref                        = ctx->hal;
	// D3DADAPTER_IDENTIFIER9 identifier;
	ctx->linear_framebuffer         = TRUE;
	ctx->throttling                 = FALSE;
	ctx->throttling_value           = 0;
	ctx->vblank_mode                = 1;
	ctx->thread_submit              = FALSE;
	ctx->discard_delayed_release    = FALSE;
	ctx->tearfree_discard           = FALSE;
	ctx->csmt_force                 = FALSE;
	ctx->dynamic_texture_workaround = FALSE;
	ctx->shader_inline_constants    = FALSE;
	ctx->memfd_virtualsizelimit     = -1;
	ctx->override_vram_size         = -1;
	ctx->destroy                    = d3dadapter9_context_destroy;

	return D3D_OK;
}

BOOL WINAPI DllMain(HINSTANCE hModule, DWORD fdwReason, LPVOID lpvReserved)
{
	BOOL fReturn = TRUE;

	switch (fdwReason)
	{
		case DLL_PROCESS_ATTACH:
			fReturn = LoadWinsys();
			break;
	
		case DLL_PROCESS_DETACH:
			break;
	
		case DLL_THREAD_ATTACH:
			break;
	
		case DLL_THREAD_DETACH:
			if(hMesa)
			{
				FreeLibrary(hMesa);
				hMesa = NULL;
			}
			break;
	
		default:
			break;
	}

	return fReturn;
}

/* IUnknown */
ULONG WINAPI NineNine_AddRef(INineNine *This)
{
	LONG cnt = InterlockedIncrement(&This->refcount);
	
	return cnt;
}

ULONG WINAPI NineNine_Release(INineNine *This)
{
	ULONG cnt = InterlockedDecrement(&This->refcount);
	if(cnt == 0)
	{
		HeapFree(GetProcessHeap(), 0, This);
	}
	
	return cnt;
}

HRESULT WINAPI NineNine_QueryInterface(INineNine *This, REFIID riid, void **ppvObject)
{
	if(
		IsEqualGUID(&IID_IDirect3D9Ex, riid) ||
		IsEqualGUID(&IID_IDirect3D9, riid) ||
		IsEqualGUID(&IID_IUnknown, riid)
	)
	{
		NineNine_AddRef(This);
		*ppvObject = This;
		return S_OK;
	}
	
	*ppvObject = NULL;
	return E_NOINTERFACE;
}

/* IDirect3D9 */
HRESULT WINAPI NineNine_RegisterSoftwareDevice(INineNine *This, void *pInitializeFunction)
{
	return D3DERR_INVALIDCALL;
}

UINT WINAPI NineNine_GetAdapterCount(INineNine *This)
{
	return MESA99_ADAPTER_CNT;
}

HRESULT WINAPI NineNine_GetAdapterIdentifier(INineNine *This, UINT Adapter, DWORD Flags, D3DADAPTER_IDENTIFIER9 *pIdentifier)
{
	if(Adapter >= MESA99_ADAPTER_CNT)
	{
		return D3DERR_INVALIDCALL;
	}
	
	
	
	return D3DERR_INVALIDCALL;
}

UINT WINAPI NineNine_GetAdapterModeCount(INineNine *This, UINT Adapter, D3DFORMAT Format)
{
	return D3DERR_INVALIDCALL;
}

HRESULT WINAPI NineNine_EnumAdapterModes(INineNine *This, UINT Adapter, D3DFORMAT Format, UINT Mode, D3DDISPLAYMODE *pMode)
{
	return D3DERR_INVALIDCALL;
}

HRESULT WINAPI GetAdapterDisplayMode(INineNine *This, UINT Adapter, D3DDISPLAYMODE *pMode)
{
	return D3DERR_INVALIDCALL;
}

HRESULT WINAPI NineNine_CheckDeviceType(INineNine *This, UINT Adapter, D3DDEVTYPE DevType, D3DFORMAT AdapterFormat, D3DFORMAT BackBufferFormat, BOOL bWindowed)
{
	return D3DERR_INVALIDCALL;
}

HRESULT WINAPI NineNine_CheckDeviceFormat(INineNine *This, UINT Adapter, D3DDEVTYPE DeviceType, D3DFORMAT AdapterFormat, DWORD Usage, D3DRESOURCETYPE RType, D3DFORMAT CheckFormat)
{
	return D3DERR_INVALIDCALL;
}

HRESULT WINAPI NineNine_CheckDeviceMultiSampleType(INineNine *This, UINT Adapter, D3DDEVTYPE DeviceType, D3DFORMAT SurfaceFormat, BOOL Windowed, D3DMULTISAMPLE_TYPE MultiSampleType, DWORD *pQualityLevels)
{
	return D3DERR_INVALIDCALL;
}

HRESULT WINAPI NineNine_CheckDepthStencilMatch(INineNine *This, UINT Adapter, D3DDEVTYPE DeviceType, D3DFORMAT AdapterFormat, D3DFORMAT RenderTargetFormat, D3DFORMAT DepthStencilFormat)
{
	return D3DERR_INVALIDCALL;
}

HRESULT WINAPI NineNine_CheckDeviceFormatConversion(INineNine *This, UINT Adapter, D3DDEVTYPE DeviceType, D3DFORMAT SourceFormat, D3DFORMAT TargetFormat)
{
	return D3DERR_INVALIDCALL;
}

HRESULT WINAPI NineNine_GetDeviceCaps(INineNine *This, UINT Adapter, D3DDEVTYPE DeviceType, D3DCAPS9 *pCaps)
{
	return D3DERR_INVALIDCALL;
}

HMONITOR WINAPI NineNine_GetAdapterMonitor(INineNine *This, UINT Adapter)
{
//	return D3DERR_INVALIDCALL;
	return NULL;
}

HRESULT WINAPI NineNine_CreateDevice(INineNine *This, UINT Adapter, D3DDEVTYPE DeviceType, HWND hFocusWindow, DWORD BehaviorFlags,
	D3DPRESENT_PARAMETERS *pPresentationParameters, IDirect3DDevice9 **ppReturnedDeviceInterface)
{
	HRESULT rc;
	ID3DPresentGroup *pg = NULL;
	
	if(Adapter > MESA99_ADAPTER_CNT)
	{
		return D3DERR_INVALIDCALL;
	}
	
	rc = ID3DPresentGroup_new(This, hFocusWindow, &pg);
	if(rc != S_OK)
	{
		return rc;
	}
	
	return NineAdapter9_CreateDevice(This->adapter9, Adapter, DeviceType, hFocusWindow, BehaviorFlags, pPresentationParameters, This, pg, ppReturnedDeviceInterface);
}

/* IDirect3D9Ex */
HRESULT WINAPI NineNine_CreateDeviceEx(INineNine *This, UINT Adapter, D3DDEVTYPE DeviceType, HWND hFocusWindow, DWORD BehaviorFlags,
	D3DPRESENT_PARAMETERS *pPresentationParameters, D3DDISPLAYMODEEX *pFullscreenDisplayMode, IDirect3DDevice9Ex **ppReturnedDeviceInterface)
{
	return D3DERR_INVALIDCALL;
}

UINT WINAPI NineNine_GetAdapterModeCountEx(INineNine *This, UINT Adapter, const D3DDISPLAYMODEFILTER *pFilter)
{
	return D3DERR_INVALIDCALL;
}

HRESULT WINAPI NineNine_EnumAdapterModesEx(INineNine *This, UINT Adapter, const D3DDISPLAYMODEFILTER *pFilter, UINT Mode, D3DDISPLAYMODEEX *pMode)
{
	return D3DERR_INVALIDCALL;
}

HRESULT WINAPI NineNine_GetAdapterDisplayMode(INineNine *This, UINT Adapter, D3DDISPLAYMODE *pMode)
{
	return D3DERR_INVALIDCALL;
}

HRESULT WINAPI NineNine_GetAdapterDisplayModeEx(INineNine *This, UINT Adapter, D3DDISPLAYMODEEX *pMode, D3DDISPLAYROTATION *pRotation)
{
	return D3DERR_INVALIDCALL;
}

HRESULT WINAPI NineNine_GetAdapterLUID(INineNine *This, UINT Adapter, LUID *pLUID)
{
	return D3DERR_INVALIDCALL;
}

static IDirect3D9ExVtbl NineNineEx_vtable = {
	(void *)NineNine_QueryInterface,
	(void *)NineNine_AddRef,
	(void *)NineNine_Release,
	(void *)NineNine_RegisterSoftwareDevice,
	(void *)NineNine_GetAdapterCount,
	(void *)NineNine_GetAdapterIdentifier,
	(void *)NineNine_GetAdapterModeCount,
	(void *)NineNine_EnumAdapterModes,
	(void *)NineNine_GetAdapterDisplayMode,
	(void *)NineNine_CheckDeviceType,
	(void *)NineNine_CheckDeviceFormat,
	(void *)NineNine_CheckDeviceMultiSampleType,
	(void *)NineNine_CheckDepthStencilMatch,
	(void *)NineNine_CheckDeviceFormatConversion,
	(void *)NineNine_GetDeviceCaps,
	(void *)NineNine_GetAdapterMonitor,
	(void *)NineNine_CreateDevice,
	(void *)NineNine_GetAdapterModeCountEx,
	(void *)NineNine_EnumAdapterModesEx,
	(void *)NineNine_GetAdapterDisplayModeEx,
	(void *)NineNine_CreateDeviceEx,
	(void *)NineNine_GetAdapterLUID
};

HRESULT WINAPI NineNine_new(INineNine **ppOut)
{
	HRESULT hr;
	HWND hDesktop = GetDesktopWindow();
	HDC hdc = GetDC(hDesktop);
	
	printf("Creating screen\n");
	
#if !(defined(MESA_NEW) || defined(MESA23))
	struct pipe_screen *s = stw_winsys.create_screen();
#else
	struct pipe_screen *s = stw_winsys.create_screen(hdc);
#endif
  printf("screen? %p\n", s);
	
	INineNine *res = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(INineNine));
	res->base.lpVtbl = &NineNineEx_vtable;
	res->screen = s;
	
	hr = d3dadapter9_context_create(&res->ctx, s);
	if (SUCCEEDED(hr))
	{
		hr = NineAdapter9_new(&res->ctx, (struct NineAdapter9 **)&res->adapter9);
		if (FAILED(hr))
		{
            /// @todo NineAdapter9_new calls this as ctx->base.destroy,
            //       and will not call if memory allocation fails.
            // wddm_destroy(&pCtx->base);
		}
	}
	
	*ppOut = res;
	
	return hr;
}


