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

#include "target-helpers/inline_sw_helper.h"

#include "mesa99.h"

#include "../../mesa9x.h"

#define MESA99_ADAPTER_CNT 1

#define OPENGL_GETINFO 0x1101

const GUID IID_NineAdaper = { 0x0b1cfb95, 0x639b, 0x45d3, { 0x87, 0xf7, 0x5f, 0x9e, 0xde, 0xfa, 0xd0, 0x10 } };

#define ADAPTER() ((struct NineAdapter9*)This->adapter9)

typedef struct _opengl_icd_t
{
	long Version;
	long DriverVersion;
	char DLL[262]; /* wchat_t DLL */
} opengl_icd_t;

//static struct stw_winsys stw_winsys = {};

/* globals */
static HMODULE hMesa = NULL;

MesaScreenCreateH MesaScreenCreate = NULL;
MesaPresentH MesaPresent = NULL;
MesaDimensionsH MesaDimensions = NULL;

static INineNine *nineInst = NULL;
static CRITICAL_SECTION nine_if_cs;

//#ifdef DEBUG
void mesa99_printf(const char *file, const char *fn, int line, const char *fmt, ...)
{
  va_list args;
	FILE *fa;
	
	fa = fopen("C:\\mesa99.log", "ab");
	
	fprintf(fa, "%s:%s:%d: ", file, fn, line);
	
	va_start(args, fmt);
  vfprintf(fa, fmt, args);
  va_end(args);
  
  fputs("\r\n", fa);
	fclose(fa);
}
//#endif

void nine_lock_proc()
{
	EnterCriticalSection(&nine_if_cs);
}

void nine_unlock_proc()
{
	LeaveCriticalSection(&nine_if_cs);
}


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

#define TRY_LOAD_FUNC(_fname) \
	proc = GetProcAddress(hMesaTest, #_fname); \
	if(proc == NULL){FreeLibrary(hMesaTest); mesa99_dbg("not found %s in %s", #_fname, dllname); return FALSE;} \
	_fname = (_fname ## H)proc;

static BOOL LoadWinsysDLL(char *dllname, HMODULE *OutHMesa)
{
	void *proc;
	HMODULE hMesaTest;
	
	hMesaTest = LoadLibraryA(dllname);
	if(hMesaTest)
	{
		TRY_LOAD_FUNC(MesaScreenCreate)
		TRY_LOAD_FUNC(MesaPresent)
		TRY_LOAD_FUNC(MesaDimensions)
	}
	
	*OutHMesa = hMesaTest;
	return TRUE;
}

BOOL LoadWinsys()
{
	char dllname[MAX_PATH];
	if(GetGLICD(dllname, MAX_PATH-1))
	{
		mesa99_dbg("mesa99: ICD success");
		LoadWinsysDLL(dllname, &hMesa);
	}
	
	if(!hMesa)
	{
		mesa99_dbg("mesa99: try opengl32.dll");
		LoadWinsysDLL("opengl32.dll", &hMesa);
	}
	
	if(!hMesa)
	{
		mesa99_dbg("mesa99: try mesa3d.dll");
		LoadWinsysDLL("mesa3d.dll", &hMesa);
	}
	
	/* debug */
	if(!hMesa)
	{
		mesa99_dbg("mesa99: try mesa3d.w98me.dll");
		LoadWinsysDLL("mesa3d.w98me.dll", &hMesa);
	}
	
	mesa99_dbg("mesa99: loader = %p", hMesa);
	
	return hMesa ? TRUE : FALSE;
}

static void d3dadapter9_context_destroy(struct d3dadapter9_context *ctx)
{
	free(ctx);
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
	ctx->override_vram_size         = 128; /* in MB (!) */
	ctx->destroy                    = d3dadapter9_context_destroy;

	return D3D_OK;
}

/* IUnknown */
ULONG WINAPI NineNine_AddRef(INineNine *This)
{
	mesa99_dbg("enter");
	
	LONG cnt = InterlockedIncrement(&This->refcount);
	return cnt;
}

ULONG WINAPI NineNine_Release(INineNine *This)
{
	mesa99_dbg("enter");
	
	ULONG cnt = InterlockedDecrement(&This->refcount);
	if(cnt == 0)
	{
		
		//free(This);
	}
	
	return cnt;
}

HRESULT WINAPI NineNine_QueryInterface(INineNine *This, REFIID riid, void **ppvObject)
{
	mesa99_dbg("enter");
	
	if(
		IsEqualGUID(&IID_IDirect3D9Ex, riid) ||
		IsEqualGUID(&IID_IDirect3D9, riid) ||
		IsEqualGUID(&IID_IUnknown, riid)
	)
	{
		NineNine_AddRef(This);
		*ppvObject = This;
		return D3D_OK;
	}
	
	*ppvObject = NULL;
	return E_NOINTERFACE;
}

/* IDirect3D9 */
HRESULT WINAPI NineNine_RegisterSoftwareDevice(INineNine *This, void *pInitializeFunction)
{
	mesa99_dbg("enter");
	
	return D3DERR_INVALIDCALL;
}

UINT WINAPI NineNine_GetAdapterCount(INineNine *This)
{
	return MESA99_ADAPTER_CNT;
}

HRESULT WINAPI NineNine_GetAdapterIdentifier(INineNine *This, UINT Adapter, DWORD Flags, D3DADAPTER_IDENTIFIER9 *pIdentifier)
{
	if(Adapter > NineNine_GetAdapterCount(This))
	{
		return D3DERR_INVALIDCALL;
	}
	
	mesa99_dbg("enter");
	
	memset(&pIdentifier->Driver[0], 0, sizeof(pIdentifier->Driver));
	memset(&pIdentifier->Description[0], 0, sizeof(pIdentifier->Description));
	memset(&pIdentifier->DeviceName[0], 0, sizeof(pIdentifier->DeviceName));
	
	strcpy(pIdentifier->Driver, "mesa99.dll");
	strcpy(pIdentifier->Description, "Mesa Nine Adapter");
	
	if(This->screen)
	{
		strcat(pIdentifier->Description, " (");
		strcat(pIdentifier->Description, This->screen->get_name(This->screen));
		strcat(pIdentifier->Description, ")");
	}
	
	strcpy(pIdentifier->DeviceName, "\\\\.\\DISPLAY1");
	
	pIdentifier->DriverVersionLowPart  = (MESA9X_PATCH << 16) | MESA9X_BUILD;
	pIdentifier->DriverVersionHighPart = (MESA9X_MAJOR << 16) | MESA9X_MINOR;
	pIdentifier->VendorId = 0x15AD;
	pIdentifier->DeviceId = 0x0405;
	pIdentifier->SubSysId = 0x040515AD;
	pIdentifier->Revision = 0;
	memcpy(&pIdentifier->DeviceIdentifier, &IID_NineAdaper, sizeof(GUID));
	pIdentifier->WHQLLevel = 0;
	
	return D3D_OK;
}

UINT WINAPI NineNine_GetAdapterModeCount(INineNine *This, UINT Adapter, D3DFORMAT Format)
{
	if(Adapter > NineNine_GetAdapterCount(This))
	{
		return 0;
	}
	
	mesa99_dbg("enter");
	
	switch(Format)
	{
		case D3DFMT_A8R8G8B8:
		case D3DFMT_X8R8G8B8:
		case D3DFMT_R8G8B8:
		case D3DFMT_R5G6B5:
			break;
		default:
			return 0;
			break;
	}
	
	int modes = 0;
	int iMode;
	DEVMODEA devMode;
	
	for(iMode = 0 ;; iMode++)
	{
		memset(&devMode, 0, sizeof(DEVMODE));
		devMode.dmSize = sizeof(DEVMODE);
		
		if(!EnumDisplaySettingsA(NULL, iMode, &devMode))
		{
			break;
		}
		
		switch(devMode.dmBitsPerPel)
		{
			case 16:
				if(Format == D3DFMT_R5G6B5)
				{
					modes++;
				}
				break;
			case 24:
				if(Format == D3DFMT_R8G8B8)
				{
					modes++;
				}
				break;
			case 32:
				if(Format == D3DFMT_A8R8G8B8 ||
					Format == D3DFMT_X8R8G8B8)
				{
					modes++;
				}
				break;
		}
	}

	return modes;
}

HRESULT WINAPI NineNine_EnumAdapterModes(INineNine *This, UINT Adapter, D3DFORMAT Format, UINT Mode, D3DDISPLAYMODE *pMode)
{
	if(Adapter > NineNine_GetAdapterCount(This))
	{
		return D3DERR_INVALIDCALL;
	}
	
	mesa99_dbg("enter");
	
	int modes = 0;
	int iMode;
	DEVMODEA devMode;
	
	for(iMode = 0 ;; iMode++)
	{
		memset(&devMode, 0, sizeof(DEVMODE));
		devMode.dmSize = sizeof(DEVMODE);
		
		if(!EnumDisplaySettingsA(NULL, iMode, &devMode))
		{
			break;
		}
		
		switch(devMode.dmBitsPerPel)
		{
			case 16:
				if(Format == D3DFMT_R5G6B5)
				{
					if(modes == Mode)
					{
						pMode->Format      = D3DFMT_R5G6B5;
						pMode->RefreshRate = 0;
						pMode->Width       = devMode.dmPelsWidth;
						pMode->Height      = devMode.dmPelsHeight;
						
						return D3D_OK;
					}
					modes++;
				}
				break;
			case 24:
				if(Format == D3DFMT_R8G8B8)
				{
					if(modes == Mode)
					{
						pMode->Format      = D3DFMT_R8G8B8;
						pMode->RefreshRate = 0;
						pMode->Width       = devMode.dmPelsWidth;
						pMode->Height      = devMode.dmPelsHeight;
						
						return D3D_OK;
					}
					modes++;
				}
				break;
			case 32:
				if(Format == D3DFMT_A8R8G8B8 ||
					Format == D3DFMT_X8R8G8B8)
				{
					if(modes == Mode)
					{
						pMode->Format      = D3DFMT_X8R8G8B8;
						pMode->RefreshRate = 0;
						pMode->Width       = devMode.dmPelsWidth;
						pMode->Height      = devMode.dmPelsHeight;
						
						return D3D_OK;
					}
					modes++;
				}
				break;
		}
	}
	
	return D3DERR_NOTAVAILABLE;
}

HRESULT WINAPI NineNine_GetAdapterDisplayMode(INineNine *This, UINT Adapter, D3DDISPLAYMODE *pMode)
{
	if(Adapter > NineNine_GetAdapterCount(This))
	{
		return D3DERR_INVALIDCALL;
	}
	
	mesa99_dbg("enter");
	
	DEVMODEA devMode = {0};
	devMode.dmSize = sizeof(DEVMODE);
	
	if(EnumDisplaySettingsA(NULL, ENUM_CURRENT_SETTINGS, &devMode))
	{
		switch(devMode.dmBitsPerPel)
		{
			case 16:
				pMode->Format = D3DFMT_R5G6B5;
				break;
			case 24:
				pMode->Format = D3DFMT_R8G8B8;
				break;
			case 32:
				pMode->Format = D3DFMT_X8R8G8B8;
				break;
			default:
				return D3DERR_NOTAVAILABLE;
		}
		
		pMode->RefreshRate = 0;
		pMode->Width       = devMode.dmPelsWidth;
		pMode->Height      = devMode.dmPelsHeight;
		return D3D_OK;
	}
	
	return D3DERR_INVALIDCALL;
}

HRESULT WINAPI NineNine_CheckDeviceType(INineNine *This, UINT Adapter, D3DDEVTYPE DevType, D3DFORMAT AdapterFormat, D3DFORMAT BackBufferFormat, BOOL bWindowed)
{
	if(Adapter > NineNine_GetAdapterCount(This))
	{
		return D3DERR_INVALIDCALL;
	}
	
	mesa99_dbg("enter");
	return NineAdapter9_CheckDeviceType(ADAPTER(), DevType, AdapterFormat, BackBufferFormat, bWindowed);
}

HRESULT WINAPI NineNine_CheckDeviceFormat(INineNine *This, UINT Adapter, D3DDEVTYPE DeviceType, D3DFORMAT AdapterFormat, DWORD Usage, D3DRESOURCETYPE RType, D3DFORMAT CheckFormat)
{
	if(Adapter > NineNine_GetAdapterCount(This))
	{
		return D3DERR_INVALIDCALL;
	}
	
	mesa99_dbg("enter %X %X %X %X %X", Adapter, DeviceType, Usage, RType, CheckFormat);
	HRESULT rc = NineAdapter9_CheckDeviceFormat(ADAPTER(), DeviceType, AdapterFormat, Usage, RType, CheckFormat);
	mesa99_dbg("result 0x%X", rc);
	
	return rc;
}

HRESULT WINAPI NineNine_CheckDeviceMultiSampleType(INineNine *This, UINT Adapter, D3DDEVTYPE DeviceType, D3DFORMAT SurfaceFormat, BOOL Windowed, D3DMULTISAMPLE_TYPE MultiSampleType, DWORD *pQualityLevels)
{
	if(Adapter > NineNine_GetAdapterCount(This))
	{
		return D3DERR_INVALIDCALL;
	}
	
	mesa99_dbg("enter");
	return NineAdapter9_CheckDeviceMultiSampleType(ADAPTER(), DeviceType, SurfaceFormat, Windowed, MultiSampleType, pQualityLevels);
}

HRESULT WINAPI NineNine_CheckDepthStencilMatch(INineNine *This, UINT Adapter, D3DDEVTYPE DeviceType, D3DFORMAT AdapterFormat, D3DFORMAT RenderTargetFormat, D3DFORMAT DepthStencilFormat)
{
	if(Adapter > NineNine_GetAdapterCount(This))
	{
		return D3DERR_INVALIDCALL;
	}
	
	mesa99_dbg("enter");
	return NineAdapter9_CheckDepthStencilMatch(ADAPTER(), DeviceType, AdapterFormat, RenderTargetFormat, DepthStencilFormat);
}

HRESULT WINAPI NineNine_CheckDeviceFormatConversion(INineNine *This, UINT Adapter, D3DDEVTYPE DeviceType, D3DFORMAT SourceFormat, D3DFORMAT TargetFormat)
{
	if(Adapter > NineNine_GetAdapterCount(This))
	{
		return D3DERR_INVALIDCALL;
	}
	
	mesa99_dbg("enter");
	return NineAdapter9_CheckDeviceFormatConversion(ADAPTER(), DeviceType, SourceFormat, TargetFormat);
}

HRESULT WINAPI NineNine_GetDeviceCaps(INineNine *This, UINT Adapter, D3DDEVTYPE DeviceType, D3DCAPS9 *pCaps)
{
	if(Adapter > NineNine_GetAdapterCount(This))
	{
		return D3DERR_INVALIDCALL;
	}

	mesa99_dbg("enter");
	return NineAdapter9_GetDeviceCaps(ADAPTER(), DeviceType, pCaps);
}

/* from: https://devblogs.microsoft.com/oldnewthing/20070809-00/?p=25643 */
static HMONITOR GetPrimaryMonitorHandle()
{
	const POINT ptZero = {0, 0};
	return MonitorFromPoint(ptZero, MONITOR_DEFAULTTOPRIMARY);
}

HMONITOR WINAPI NineNine_GetAdapterMonitor(INineNine *This, UINT Adapter)
{
	if(Adapter > NineNine_GetAdapterCount(This))
	{
		return NULL;
	}

	mesa99_dbg("enter");
	return GetPrimaryMonitorHandle();
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
	
	mesa99_dbg("enter");
	//nine_lock();
	
	rc = ID3DPresentGroup_new(This, hFocusWindow, &pg);
	if(rc !=  D3D_OK)
	{
		mesa99_dbg("ID3DPresentGroup_new FAIL");
		nine_unlock();
		return rc;
	}
	
	rc = NineAdapter9_CreateDevice(ADAPTER(), Adapter, DeviceType, hFocusWindow, BehaviorFlags, pPresentationParameters, (IDirect3D9 *)This, pg, ppReturnedDeviceInterface);
	if(FAILED(rc))
	{
		mesa99_dbg("FAILED NineAdapter9_CreateDevice");
	}
	
	//nine_unlock();
	
	return rc;
}

/* IDirect3D9Ex */
HRESULT WINAPI NineNine_CreateDeviceEx(INineNine *This, UINT Adapter, D3DDEVTYPE DeviceType, HWND hFocusWindow, DWORD BehaviorFlags,
	D3DPRESENT_PARAMETERS *pPresentationParameters, D3DDISPLAYMODEEX *pFullscreenDisplayMode, IDirect3DDevice9Ex **ppReturnedDeviceInterface)
{
	HRESULT rc;
	ID3DPresentGroup *pg = NULL;
	
	if(Adapter > MESA99_ADAPTER_CNT)
	{
		return D3DERR_INVALIDCALL;
	}
	
	mesa99_dbg("enter");
	//nine_lock();
	
	rc = ID3DPresentGroup_new(This, hFocusWindow, &pg);
	if(rc !=  D3D_OK)
	{
		nine_unlock();
		return rc;
	}
	
	rc = NineAdapter9_CreateDeviceEx(ADAPTER(), Adapter, DeviceType, hFocusWindow, BehaviorFlags, pPresentationParameters,
		pFullscreenDisplayMode, (IDirect3D9Ex *)This, pg, ppReturnedDeviceInterface
	);
	
	//nine_unlock();
	
	return rc;
}

UINT WINAPI NineNine_GetAdapterModeCountEx(INineNine *This, UINT Adapter, const D3DDISPLAYMODEFILTER *pFilter)
{
	if(pFilter->Size != sizeof(D3DDISPLAYMODEFILTER))
	{
		return D3DERR_INVALIDCALL;
	}
	
	if(Adapter > NineNine_GetAdapterCount(This))
	{
		return 0;
	}
	
	if(pFilter->ScanLineOrdering == D3DSCANLINEORDERING_INTERLACED)
	{
		return 0;
	}
	
	mesa99_dbg("enter");
	
	switch(pFilter->Format)
	{
		case D3DFMT_A8R8G8B8:
		case D3DFMT_X8R8G8B8:
		case D3DFMT_R8G8B8:
		case D3DFMT_R5G6B5:
			break;
		default:
			return 0;
			break;
	}
	
	int modes = 0;
	int iMode;
	DEVMODEA devMode;
	
	for(iMode = 0 ;; iMode++)
	{
		memset(&devMode, 0, sizeof(DEVMODE));
		devMode.dmSize = sizeof(DEVMODE);
		
		if(!EnumDisplaySettingsA(NULL, iMode, &devMode))
		{
			break;
		}
		
		switch(devMode.dmBitsPerPel)
		{
			case 16:
				if(pFilter->Format == D3DFMT_R5G6B5)
				{
					modes++;
				}
				break;
			case 24:
				if(pFilter->Format == D3DFMT_R8G8B8)
				{
					modes++;
				}
				break;
			case 32:
				if(pFilter->Format == D3DFMT_A8R8G8B8 ||
					pFilter->Format == D3DFMT_X8R8G8B8)
				{
					modes++;
				}
				break;
		}
	}

	return modes;
}

HRESULT WINAPI NineNine_EnumAdapterModesEx(INineNine *This, UINT Adapter, const D3DDISPLAYMODEFILTER *pFilter, UINT Mode, D3DDISPLAYMODEEX *pMode)
{
	if(pFilter->Size != sizeof(D3DDISPLAYMODEFILTER))
	{
		return D3DERR_INVALIDCALL;
	}
	
	if(Adapter > NineNine_GetAdapterCount(This))
	{
		return D3DERR_NOTAVAILABLE;
	}
	
	if(pFilter->ScanLineOrdering == D3DSCANLINEORDERING_INTERLACED)
	{
		return D3DERR_NOTAVAILABLE;
	}
	
	mesa99_dbg("enter");
	
	int modes = 0;
	int iMode;
	DEVMODEA devMode;
	
	for(iMode = 0 ;; iMode++)
	{
		memset(&devMode, 0, sizeof(DEVMODE));
		devMode.dmSize = sizeof(DEVMODE);
		
		if(!EnumDisplaySettingsA(NULL, iMode, &devMode))
		{
			break;
		}
		
		switch(devMode.dmBitsPerPel)
		{
			case 16:
				if(pFilter->Format == D3DFMT_R5G6B5)
				{
					if(modes == Mode)
					{
						pMode->Format      = D3DFMT_R5G6B5;
						pMode->RefreshRate = 0;
						pMode->Width       = devMode.dmPelsWidth;
						pMode->Height      = devMode.dmPelsHeight;
						
						return D3D_OK;
					}
					modes++;
				}
				break;
			case 24:
				if(pFilter->Format == D3DFMT_R8G8B8)
				{
					if(modes == Mode)
					{
						pMode->Format      = D3DFMT_R8G8B8;
						pMode->RefreshRate = 0;
						pMode->Width       = devMode.dmPelsWidth;
						pMode->Height      = devMode.dmPelsHeight;
						
						return D3D_OK;
					}
					modes++;
				}
				break;
			case 32:
				if(pFilter->Format == D3DFMT_A8R8G8B8 ||
					pFilter->Format == D3DFMT_X8R8G8B8)
				{
					if(modes == Mode)
					{
						pMode->Format      = D3DFMT_X8R8G8B8;
						pMode->RefreshRate = 0;
						pMode->Width       = devMode.dmPelsWidth;
						pMode->Height      = devMode.dmPelsHeight;
						
						return D3D_OK;
					}
					modes++;
				}
				break;
		}
	}
	
	return D3DERR_NOTAVAILABLE;
}

HRESULT WINAPI NineNine_GetAdapterDisplayModeEx(INineNine *This, UINT Adapter, D3DDISPLAYMODEEX *pMode, D3DDISPLAYROTATION *pRotation)
{
	if(Adapter > NineNine_GetAdapterCount(This))
	{
		return D3DERR_INVALIDCALL;
	}
	
	mesa99_dbg("enter");
	
	DEVMODEA devMode = {0};
	devMode.dmSize = sizeof(DEVMODE);
	
	if(EnumDisplaySettingsA(NULL, ENUM_CURRENT_SETTINGS, &devMode))
	{
		if(pMode != NULL)
		{
			switch(devMode.dmBitsPerPel)
			{
				case 16:
					pMode->Format = D3DFMT_R5G6B5;
					break;
				case 24:
					pMode->Format = D3DFMT_R8G8B8;
					break;
				case 32:
					pMode->Format = D3DFMT_X8R8G8B8;
					break;
				default:
					return D3DERR_NOTAVAILABLE;
			}
			
			pMode->RefreshRate = 0;
			pMode->Width       = devMode.dmPelsWidth;
			pMode->Height      = devMode.dmPelsHeight;
			pMode->ScanLineOrdering = D3DSCANLINEORDERING_PROGRESSIVE;
		}
		
		if(pRotation)
		{
			*pRotation = D3DDISPLAYROTATION_IDENTITY;
		}
		
		return D3D_OK;
	}
	
	return D3DERR_INVALIDCALL;
}

HRESULT WINAPI NineNine_GetAdapterLUID(INineNine *This, UINT Adapter, LUID *pLUID)
{
	mesa99_dbg("enter");
	
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

static struct pipe_screen *last_screen = NULL;

HRESULT WINAPI NineNine_new(INineNine **ppOut)
{
	HRESULT hr;
	HWND hDesktop = GetDesktopWindow();
	HDC hdc = GetDC(hDesktop);
	struct pipe_screen *screen = NULL;
	
	if(!MesaScreenCreate)
	{
		return D3DERR_INVALIDCALL;
	}
	
	mesa99_dbg("Creating screen");
	
	if(last_screen != NULL)
	{
		screen = last_screen;
	}
	else
	{
		if(!MesaScreenCreate(hdc, &screen))
		{
			mesa99_dbg("MesaScreenCreate failed");
			return D3DERR_INVALIDCALL;
		}
		//last_screen = screen;
	}
	
  mesa99_dbg("screen? %p", screen);	
	INineNine *res = calloc(1, sizeof(INineNine));
	res->base.lpVtbl = &NineNineEx_vtable;
	res->screen = screen;

	hr = d3dadapter9_context_create(&res->ctx, screen);
	if (SUCCEEDED(hr))
	{
		hr = NineAdapter9_new(&res->ctx, (struct NineAdapter9 **)&res->adapter9);
		if (FAILED(hr))
		{
            /// @todo NineAdapter9_new calls this as ctx->base.destroy,
            //       and will not call if memory allocation fails.
            // wddm_destroy(&pCtx->base);
      mesa99_dbg("d3dadapter9_context_create FAILED");
    	return D3DERR_INVALIDCALL;
		}
	}
	
	*ppOut = res;
	
	return hr;
}

/* d3d9.dll like interface
 * (mostly )from Wine
 */
#define DECLSPEC_HOTPATCH

void WINAPI DebugSetMute(void) {
    /* nothing to do */
}

IDirect3D9 * WINAPI DECLSPEC_HOTPATCH Direct3DCreate9(UINT sdk_version)
{
	mesa99_dbg("enter");
	
	NineNine_AddRef(nineInst);
	
	return (IDirect3D9*)nineInst;
}

HRESULT WINAPI DECLSPEC_HOTPATCH Direct3DCreate9Ex(UINT sdk_version, IDirect3D9Ex **d3d9ex)
{
	mesa99_dbg("enter");
	
	NineNine_AddRef(nineInst);
	*d3d9ex = (IDirect3D9Ex*)nineInst;
	
	return D3D_OK;
}

/*******************************************************************
 *       Direct3DShaderValidatorCreate9 (D3D9.@)
 *
 * No documentation available for this function.
 * SDK only says it is internal and shouldn't be used.
 */
void* WINAPI Direct3DShaderValidatorCreate9(void)
{
    return NULL;
}

static int D3DPERF_event_level = 0;

/***********************************************************************
 *              D3DPERF_BeginEvent (D3D9.@)
 */
int WINAPI D3DPERF_BeginEvent(D3DCOLOR color, const WCHAR *name)
{
    return D3DPERF_event_level++;
}

/***********************************************************************
 *              D3DPERF_EndEvent (D3D9.@)
 */
int WINAPI D3DPERF_EndEvent(void) {
    return --D3DPERF_event_level;
}

/***********************************************************************
 *              D3DPERF_GetStatus (D3D9.@)
 */
DWORD WINAPI D3DPERF_GetStatus(void) {
    return 0;
}

/***********************************************************************
 *              D3DPERF_SetOptions (D3D9.@)
 *
 */
void WINAPI D3DPERF_SetOptions(DWORD options)
{

}

/***********************************************************************
 *              D3DPERF_QueryRepeatFrame (D3D9.@)
 */
BOOL WINAPI D3DPERF_QueryRepeatFrame(void) {
    return FALSE;
}

/***********************************************************************
 *              D3DPERF_SetMarker (D3D9.@)
 */
void WINAPI D3DPERF_SetMarker(D3DCOLOR color, const WCHAR *name)
{

}

/***********************************************************************
 *              D3DPERF_SetRegion (D3D9.@)
 */
void WINAPI D3DPERF_SetRegion(D3DCOLOR color, const WCHAR *name)
{
	
}

/***********************************************************************/

BOOL WINAPI DllMain(HINSTANCE hModule, DWORD fdwReason, LPVOID lpvReserved)
{
	BOOL fReturn = TRUE;

	switch (fdwReason)
	{
		case DLL_PROCESS_ATTACH:
			InitializeCriticalSection(&nine_if_cs);
			fReturn = LoadWinsys();
			if(fReturn)
			{
				if(nine_init())
				{
					if(NineNine_new(&nineInst) == D3D_OK)
					{
						return TRUE;
					}
					nine_deinit();
				}
			}
			
			DeleteCriticalSection(&nine_if_cs);
			return FALSE;
	
		case DLL_PROCESS_DETACH:
			mesa99_dbg("DLL_PROCESS_DETACH: %d", lpvReserved);
			
			if(hMesa)
			{
				FreeLibrary(hMesa);
				hMesa = NULL;
			}
			
			mesa99_dbg("Cleanup, num refs: %d", nineInst->refcount);
			
			nine_deinit();
			DeleteCriticalSection(&nine_if_cs);
			break;
	
		case DLL_THREAD_ATTACH:
			break;
	
		case DLL_THREAD_DETACH:
			break;
	
		default:
			break;
	}

	return fReturn;
}


