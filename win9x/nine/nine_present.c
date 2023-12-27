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
#include "nine_present.h"

const GUID IID_ID3DPresent = { 0x77D60E80, 0xF1E6, 0x11DF, { 0x9E, 0x39, 0x95, 0x0C, 0xDF, 0xD7, 0x20, 0x85 } };
const GUID IID_ID3DPresentGroup = { 0xB9C3016E, 0xF32A, 0x11DF, { 0x9C, 0x18, 0x92, 0xEA, 0xDE, 0xD7, 0x20, 0x85 } };

#define D3DADAPTER_DRIVER_PRESENT_VERSION_MAJOR 1
#define D3DADAPTER_DRIVER_PRESENT_VERSION_MINOR 0

/******************************************************************************
   Present
******************************************************************************/

/* IUnknown */
static ULONG WINAPI DRIPresent_AddRef(ID3DPresentM99 *This)
{
	LONG cnt = InterlockedIncrement(&This->refcount);
	
	return cnt;
}

static ULONG WINAPI DRIPresent_Release(ID3DPresentM99 *This)
{
	ULONG cnt = InterlockedDecrement(&This->refcount);
	if(cnt == 0)
	{
		HeapFree(GetProcessHeap(), 0, This);
	}
	
	return cnt;
}

static HRESULT WINAPI DRIPresent_QueryInterface(ID3DPresentM99 *This, REFIID riid, void **ppvObject)
{
	if(!ppvObject)
		return E_POINTER;

	if(IsEqualGUID(&IID_ID3DPresent, riid) || IsEqualGUID(&IID_IUnknown, riid))
	{
		*ppvObject = This;
		DRIPresent_AddRef(This);
		return S_OK;
	}

	*ppvObject = NULL;

	return E_NOINTERFACE;
}

static HRESULT ChangeDisplaySettingsIfNeccessary(ID3DPresentM99 *This, DEVMODEA *pMode)
{
	DEVMODEA cur = {0};
	cur.dmSize = sizeof(DEVMODEA);
	
	if(EnumDisplaySettingsA(NULL, ENUM_CURRENT_SETTINGS, &cur))
	{
		if(cur.dmBitsPerPel != pMode->dmBitsPerPel ||
			cur.dmPelsWidth   != pMode->dmPelsWidth ||
			cur.dmPelsHeight  != pMode->dmPelsHeight)
		{
			if(ChangeDisplaySettingsA(pMode, CDS_FULLSCREEN) == DISP_CHANGE_SUCCESSFUL)
			{
				return S_OK;
			}
		}
		
		return S_OK;
	}
	
	printf("EnumDisplaySettingsA failure\n");
	
	return E_NOINTERFACE;
}

/* ID3DPresent */
static HRESULT DRIPresent_ChangePresentParameters(ID3DPresentM99 *This, D3DPRESENT_PARAMETERS *params)
{
	//printf("%s\n", __FUNCTION__);
	
	HWND focus_window = This->focus_wnd ? This->focus_wnd : params->hDeviceWindow;
	RECT rect;
	DEVMODEA new_mode;
	HRESULT hr;
	BOOL filter_messages;

	This->params.SwapEffect = params->SwapEffect;
	This->params.AutoDepthStencilFormat = params->AutoDepthStencilFormat;
	This->params.Flags = params->Flags;
	This->params.FullScreen_RefreshRateInHz = params->FullScreen_RefreshRateInHz;
	This->params.PresentationInterval = params->PresentationInterval;
	This->params.EnableAutoDepthStencil = params->EnableAutoDepthStencil;

	if (!params->hDeviceWindow)
		params->hDeviceWindow = This->params.hDeviceWindow;
	else
		This->params.hDeviceWindow = params->hDeviceWindow;

	if(
		(This->params.BackBufferWidth != params->BackBufferWidth) ||
		(This->params.BackBufferHeight != params->BackBufferHeight) ||
		(This->params.Windowed != params->Windowed)
	)
	{
		if(!params->Windowed)
		{
			/* switch display mode */
			ZeroMemory(&new_mode, sizeof(DEVMODEA));
			new_mode.dmPelsWidth = params->BackBufferWidth;
			new_mode.dmPelsHeight = params->BackBufferHeight;
			new_mode.dmFields = DM_PELSWIDTH | DM_PELSHEIGHT;
			if(params->FullScreen_RefreshRateInHz)
			{
				new_mode.dmFields |= DM_DISPLAYFREQUENCY;
				new_mode.dmDisplayFrequency = params->FullScreen_RefreshRateInHz;
			}
			new_mode.dmSize = sizeof(DEVMODEA);
			hr = ChangeDisplaySettingsIfNeccessary(This, &new_mode);
			if(FAILED(hr))
			{
				printf(" FAILED ChangeDisplaySettingsIfNeccessary\n");
				return hr;
			}
			
			setup_fullscreen_window(This, focus_window, params->BackBufferWidth, params->BackBufferHeight);

			/* Dirty as BackBufferWidth and BackBufferHeight hasn't been set yet */
			This->resolution_mismatch = FALSE;
		}
		else if(!This->params.Windowed && params->Windowed)
		{
			hr = ChangeDisplaySettingsIfNeccessary(This, &This->initial_mode);
			if(FAILED(hr))
			{
				printf(" FAILED ChangeDisplaySettingsIfNeccessary\n");
				return hr;
			}

			/* Dirty as BackBufferWidth and BackBufferHeight hasn't been set yet */
			This->resolution_mismatch = FALSE;
		}

		if(This->params.Windowed)
		{
			if(!params->Windowed)
			{
				/* switch from window to fullscreen */
				if(!nine_register_window(focus_window, This))
				{
					printf(" FAILED nine_register_window\n");
					return D3DERR_INVALIDCALL;
				}

				SetWindowPos(focus_window, 0, 0, 0, 0, 0, SWP_NOSIZE | SWP_NOMOVE);

				setup_fullscreen_window(This, params->hDeviceWindow, params->BackBufferWidth, params->BackBufferHeight);
			}
		}
		else
		{
			if(!params->Windowed)
			{
				/* switch from fullscreen to fullscreen */
				filter_messages = This->filter_messages;
				This->filter_messages = TRUE;
				MoveWindow(params->hDeviceWindow, 0, 0, params->BackBufferWidth, params->BackBufferHeight, TRUE);
				This->filter_messages = filter_messages;
			}
			else if (This->style || This->style_ex)
			{
				restore_fullscreen_window(This, params->hDeviceWindow);
			}

			if (params->Windowed && !nine_unregister_window(focus_window))
			{
				printf("Window %p is not registered with nine.\n", focus_window);
			}
		}
		
		This->params.Windowed = params->Windowed;
	}
	else if(!params->Windowed)
	{
		move_fullscreen_window(This, params->hDeviceWindow, params->BackBufferWidth, params->BackBufferHeight);
	}
	else
	{
		//TRACE("Nothing changed.\n");
	}

	if (!params->BackBufferWidth || !params->BackBufferHeight)
	{
		if(!params->Windowed)
		{
			return D3DERR_INVALIDCALL;
		}

		if(!GetClientRect(params->hDeviceWindow, &rect))
		{
			return D3DERR_INVALIDCALL;
		}

		if(params->BackBufferWidth == 0)
			params->BackBufferWidth = rect.right - rect.left;

		if(params->BackBufferHeight == 0)
			params->BackBufferHeight = rect.bottom - rect.top;
	}

	/* Set as last in case of failed reset those aren't updated */
	This->params.BackBufferWidth = params->BackBufferWidth;
	This->params.BackBufferHeight = params->BackBufferHeight;
	This->params.BackBufferFormat = params->BackBufferFormat;
	This->params.BackBufferCount = params->BackBufferCount;
	This->params.MultiSampleType = params->MultiSampleType;
	This->params.MultiSampleQuality = params->MultiSampleQuality;

	//DRI3Present_UpdatePresentationInterval(This);

	if(!params->Windowed)
	{
		//
	}
	
	//printf("%s SUCCESS\n", __FUNCTION__);
	return D3D_OK;
}

static HRESULT WINAPI DRIPresent_SetPresentParameters(ID3DPresentM99 *This,
	D3DPRESENT_PARAMETERS *pPresentationParameters,
	D3DDISPLAYMODEEX *pFullscreenDisplayMode)
{
	return DRIPresent_ChangePresentParameters(This, pPresentationParameters);
}

#define DEPTH_BYTES(_d) (((_d)+7) >> 3)

#if 0
static HRESULT WINAPI DRIPresent_D3DWindowBufferFromDmaBuf(ID3DPresentM99 *This,
	int dmaBufFd, int width, int height, int stride, int depth,
	int bpp, struct D3DWindowBuffer **out)
{
	return D3DERR_DRIVERINTERNALERROR;
}
#endif

static HRESULT WINAPI DRIPresent_D3DWindowBufferFromRes(ID3DPresentM99 *This,
	struct pipe_screen *screen,	struct pipe_context *ctx, struct pipe_resource *res,
	D3DWindowBuffer **out)
{
	//printf("%s\n", __FUNCTION__);
	
	int w, h, bpp, pitch;
	
	if(MesaDimensions(screen, ctx, res, &w, &h, &bpp, &pitch))
	{
		//printf("Screen: %d, %d, %d, %d\n", w, h, bpp, pitch);
		D3DWindowBuffer *wb = HeapAlloc(GetProcessHeap(), 0, sizeof(D3DWindowBuffer));
		if(wb)
		{
			wb->width = w;
			wb->height = h;
			wb->bpp = bpp;
			wb->pitch = pitch;
			
			wb->res = res;
			wb->ctx = ctx;
			wb->screen = screen;
			
			*out = wb;
			return D3D_OK;
		}
	}
	
	return D3DERR_DRIVERINTERNALERROR;
}

static HRESULT WINAPI DRIPresent_DestroyD3DWindowBuffer(ID3DPresentM99 *This, struct D3DWindowBuffer *buffer)
{
	//printf("%s\n", __FUNCTION__);
    /* the pixmap is managed by the PRESENT backend.
     * But if it can delete it right away, we may have
     * better performance */
	HeapFree(GetProcessHeap(), 0, buffer);
	return D3D_OK;
}

static HRESULT WINAPI DRIPresent_WaitBufferReleased(ID3DPresentM99 *This, struct D3DWindowBuffer *buffer)
{
	//printf("%s\n", __FUNCTION__);
#ifdef IMPLEMENT
	if(!PRESENTWaitPixmapReleased(buffer->present_pixmap_priv))
	{
		return D3DERR_DRIVERINTERNALERROR;
	}
#endif
	return D3D_OK;
}

static HRESULT WINAPI DRIPresent_FrontBufferCopy(ID3DPresentM99 *This, struct D3DWindowBuffer *buffer)
{
	printf("%s\n", __FUNCTION__);
#ifdef IMPLEMENT
	if (PRESENTHelperCopyFront(This->gdi_display, buffer->present_pixmap_priv))
		return D3D_OK;
	else
#endif
	return D3D_OK;
}

struct d3d_drawable *get_d3d_drawable(HWND hwnd)
{
	struct d3d_drawable *d3d = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(struct d3d_drawable));
	if(d3d != NULL)
	{
		d3d->wnd = hwnd;
		d3d->hdc = GetDC(hwnd);
		GetClientRect(hwnd, &(d3d->windowRect));
		d3d->width  = d3d->windowRect.right - d3d->windowRect.left;
		d3d->height = d3d->windowRect.bottom - d3d->windowRect.top;
		d3d->depth  = 32;
	}
	
	return d3d;
}

void release_d3d_drawable(struct d3d_drawable *d3d)
{
	HeapFree(GetProcessHeap(), 0, d3d);
}

static HRESULT WINAPI DRIPresent_PresentBuffer(ID3DPresentM99 *This, struct D3DWindowBuffer *buffer, HWND hWndOverride, const RECT *pSourceRect, const RECT *pDestRect, const RGNDATA *pDirtyRegion, DWORD Flags)
{
	//printf("%s\n", __FUNCTION__);
	struct d3d_drawable *d3d;
	RECT dest_translate;
	RECT windowRect;
	RECT offset;
	HWND hwnd;

	if (hWndOverride)
		hwnd = hWndOverride;
	else if (This->params.hDeviceWindow)
		hwnd = This->params.hDeviceWindow;
	else
		hwnd = This->focus_wnd;

	//printf("%s: This=%p hwnd=%p\n", __FUNCTION__, This, hwnd);

	d3d = get_d3d_drawable(/*This->gdi_display, */hwnd);

	if (!d3d)
		return D3DERR_DRIVERINTERNALERROR;

    /* TODO: should we use a list here instead ? */
	if (This->d3d/* && (This->d3d->wnd != d3d->wnd)*/)
	{
		//destroy_d3dadapter_drawable(This->gdi_display, This->d3d->wnd);
		release_d3d_drawable(This->d3d);
	}

	This->d3d = d3d;

	GetWindowRect(d3d->wnd, &windowRect);
	/* The "correct" way to detect offset changes
	 * would be to catch any window related change with a
	 * listener. But it is complicated and this heuristic
	 * is fast and should work well. */
	if (windowRect.top != d3d->windowRect.top ||
		windowRect.left != d3d->windowRect.left ||
		windowRect.bottom != d3d->windowRect.bottom ||
		windowRect.right != d3d->windowRect.right)
	{
		d3d->windowRect = windowRect;
		//DRI3Present_FillOffset(This->gdi_display, d3d);
	}

	GetClientRect(d3d->wnd, &offset);
	offset.left += d3d->offset.x;
	offset.top += d3d->offset.y;
	offset.right += d3d->offset.x;
	offset.bottom += d3d->offset.y;

	if ((offset.top != 0) || (offset.left != 0))
	{
		if (!pDestRect)
			pDestRect = (const RECT *) &offset;
		else
		{
			dest_translate.top = pDestRect->top + offset.top;
			dest_translate.left = pDestRect->left + offset.left;
			dest_translate.bottom = pDestRect->bottom + offset.bottom;
			dest_translate.right = pDestRect->right + offset.right;
			pDestRect = (const RECT *) &dest_translate;
		}
	}

	HDC dc = GetDC(d3d->wnd);
	if(dc != NULL)
	{
		MesaPresent(buffer->screen, buffer->ctx, buffer->res, dc, pSourceRect, pDestRect);
	}
	
	//release_d3d_drawable(d3d);
	return D3D_OK;
}

/* Based on wine's wined3d_get_adapter_raster_status. */
static HRESULT WINAPI DRIPresent_GetRasterStatus(ID3DPresentM99 *This, D3DRASTER_STATUS *pRasterStatus )
{
	//printf("%s\n", __FUNCTION__);
	LONGLONG freq_per_frame, freq_per_line;
	LARGE_INTEGER counter, freq_per_sec;
	unsigned refresh_rate, height;

	if (!QueryPerformanceCounter(&counter) || !QueryPerformanceFrequency(&freq_per_sec))
		return D3DERR_INVALIDCALL;

	if (This->params.Windowed)
	{
		refresh_rate = This->initial_mode.dmDisplayFrequency;
		height = This->initial_mode.dmPelsHeight;
	}
	else
	{
		refresh_rate = This->params.FullScreen_RefreshRateInHz;
		height = This->params.BackBufferHeight;
	}

	if (refresh_rate == 0)
		refresh_rate = 60;

	freq_per_frame = freq_per_sec.QuadPart / refresh_rate;
	/* Assume 20 scan lines in the vertical blank. */
	freq_per_line = freq_per_frame / (height + 20);
	pRasterStatus->ScanLine = (counter.QuadPart % freq_per_frame) / freq_per_line;
	if (pRasterStatus->ScanLine < height)
		pRasterStatus->InVBlank = FALSE;
	else
	{
		pRasterStatus->ScanLine = 0;
		pRasterStatus->InVBlank = TRUE;
	}

	return D3D_OK;
}

static HRESULT WINAPI DRIPresent_GetDisplayMode(ID3DPresentM99 *This, D3DDISPLAYMODEEX *pMode, D3DDISPLAYROTATION *pRotation)
{
	DEVMODEA dm;

	ZeroMemory(&dm, sizeof(dm));
	dm.dmSize = sizeof(dm);

	EnumDisplaySettingsExA(NULL, ENUM_CURRENT_SETTINGS, &dm, 0);
	pMode->Width = dm.dmPelsWidth;
	pMode->Height = dm.dmPelsHeight;
	pMode->RefreshRate = dm.dmDisplayFrequency;
	pMode->ScanLineOrdering = (dm.dmDisplayFlags & DM_INTERLACED) ? D3DSCANLINEORDERING_INTERLACED : D3DSCANLINEORDERING_PROGRESSIVE;

	/* XXX This is called "guessing" */
	switch (dm.dmBitsPerPel)
	{
		case 32: pMode->Format = D3DFMT_X8R8G8B8; break;
		case 24: pMode->Format = D3DFMT_R8G8B8; break;
		case 16: pMode->Format = D3DFMT_R5G6B5; break;
		default:
			WARN("Unknown display format with %u bpp.\n", dm.dmBitsPerPel);
			pMode->Format = D3DFMT_UNKNOWN;
	}

	switch (dm.dmDisplayOrientation)
	{
		case DMDO_DEFAULT: *pRotation = D3DDISPLAYROTATION_IDENTITY; break;
		case DMDO_90:      *pRotation = D3DDISPLAYROTATION_90; break;
		case DMDO_180:     *pRotation = D3DDISPLAYROTATION_180; break;
		case DMDO_270:     *pRotation = D3DDISPLAYROTATION_270; break;
		default:           *pRotation = D3DDISPLAYROTATION_IDENTITY;
	}

	return D3D_OK;
}

static HRESULT WINAPI DRIPresent_GetPresentStats(ID3DPresentM99 *This, D3DPRESENTSTATS *pStats)
{
	return D3DERR_INVALIDCALL;
}

static HRESULT WINAPI DRIPresent_GetCursorPos(ID3DPresentM99 *This, POINT *pPoint)
{
	BOOL ok;
	HWND draw_window;

	if(!pPoint) return D3DERR_INVALIDCALL;

	draw_window = This->params.hDeviceWindow ? This->params.hDeviceWindow : This->focus_wnd;

	ok = GetCursorPos(pPoint);
	ok = ok && ScreenToClient(draw_window, pPoint);
	return ok ? S_OK : D3DERR_DRIVERINTERNALERROR;
}

static HRESULT WINAPI DRIPresent_SetCursorPos(ID3DPresentM99 *This, POINT *pPoint )
{
	BOOL ok;
	POINT real_pos;

	if (!pPoint)
		return D3DERR_INVALIDCALL;

	ok = SetCursorPos(pPoint->x, pPoint->y);
	if (!ok)
		goto error;

	ok = GetCursorPos(&real_pos);
	if (!ok || real_pos.x != pPoint->x || real_pos.y != pPoint->y)
		goto error;

	return D3D_OK;

error:
	SetCursor(NULL); /* Hide cursor rather than put wrong pos */
	return D3DERR_DRIVERINTERNALERROR;
}

/* Note: assuming 32x32 cursor */
static HRESULT WINAPI DRIPresent_SetCursor(ID3DPresentM99 *This, void *pBitmap, POINT *pHotspot, BOOL bShow)
{
	if (pBitmap)
	{
		ICONINFO info;
		HCURSOR cursor;

		DWORD mask[32];
		memset(mask, ~0, sizeof(mask));

		if (!pHotspot) return D3DERR_INVALIDCALL;

		info.fIcon = FALSE;
		info.xHotspot = pHotspot->x;
		info.yHotspot = pHotspot->y;
		info.hbmMask = CreateBitmap(32, 32, 1, 1, mask);
		info.hbmColor = CreateBitmap(32, 32, 1, 32, pBitmap);

		cursor = CreateIconIndirect(&info);
		if (info.hbmMask) DeleteObject(info.hbmMask);
		if (info.hbmColor) DeleteObject(info.hbmColor);
		if (cursor) DestroyCursor(This->hCursor);
		
		This->hCursor = cursor;
	}
	SetCursor(bShow ? This->hCursor : NULL);

	return D3D_OK;
}

static HRESULT WINAPI DRIPresent_SetGammaRamp(ID3DPresentM99 *This, const D3DGAMMARAMP *pRamp, HWND hWndOverride)
{
	HWND draw_window = This->params.hDeviceWindow ? This->params.hDeviceWindow : This->focus_wnd;
	HWND hWnd = hWndOverride ? hWndOverride : draw_window;
	HDC hdc;
	BOOL ok;

	if (!pRamp) return D3DERR_INVALIDCALL;

	hdc = GetDC(hWnd);
	ok = SetDeviceGammaRamp(hdc, (void *)pRamp);
	ReleaseDC(hWnd, hdc);
	
	return ok ? D3D_OK : D3DERR_DRIVERINTERNALERROR;
}

static HRESULT WINAPI DRIPresent_GetWindowInfo(ID3DPresentM99 *This, HWND hWnd, int *width, int *height, int *depth)
{
	//printf("%s\n", __FUNCTION__);
	HWND draw_window = This->params.hDeviceWindow ? This->params.hDeviceWindow : This->focus_wnd;
	RECT pRect;

	//printf("%s: This=%p hwnd=%p draw_window=%p\n", __FUNCTION__, This, hWnd, draw_window);

	/* For fullscreen modes, use the dimensions of the X11 window instead of
	 * the game window. This is for compability with Valve's "fullscreen hack",
	 * which won't switch to the game's resolution anymore, but instead scales
	 * the game window to the root window. Only then can page flipping be used.
     */
	if (!This->params.Windowed && This->d3d)
	{
		if (This->d3d->width > 0 && This->d3d->height > 0 && This->d3d->depth > 0)
		{
			if(width != NULL)  *width = This->d3d->width;
			if(height != NULL) *height = This->d3d->height;
			if(depth != NULL)  *depth = This->d3d->depth;
			return D3D_OK;
		}
	}

	if (!hWnd) hWnd = draw_window;
		
	if(!GetClientRect(hWnd, &pRect))
		return D3DERR_INVALIDCALL;
	
	//printf("pRect: %d %d %d %d\n", pRect.left, pRect.top, pRect.right, pRect.bottom);
	if(width != NULL)  *width = pRect.right - pRect.left;
	if(height != NULL) *height = pRect.bottom - pRect.top;
	if(depth != NULL)  *depth = 32; //24; //TODO
	return D3D_OK;
}

static ID3DPresentVtbl DRIPresent_vtable = {
    (void *)DRIPresent_QueryInterface,
    (void *)DRIPresent_AddRef,
    (void *)DRIPresent_Release,
    
    (void *)DRIPresent_SetPresentParameters,
    //(void *)DRIPresent_D3DWindowBufferFromDmaBuf,
    NULL,
    (void *)DRIPresent_D3DWindowBufferFromRes,
    (void *)DRIPresent_DestroyD3DWindowBuffer,
    (void *)DRIPresent_WaitBufferReleased,
    (void *)DRIPresent_FrontBufferCopy,
    (void *)DRIPresent_PresentBuffer,
    (void *)DRIPresent_GetRasterStatus,
    (void *)DRIPresent_GetDisplayMode,
    (void *)DRIPresent_GetPresentStats,
    (void *)DRIPresent_GetCursorPos,
    (void *)DRIPresent_SetCursorPos,
    (void *)DRIPresent_SetCursor,
    (void *)DRIPresent_SetGammaRamp,
    (void *)DRIPresent_GetWindowInfo,
#if D3DADAPTER_DRIVER_PRESENT_VERSION_MINOR >= 1
    (void *)DRIPresent_GetWindowOccluded,
#endif
#if D3DADAPTER_DRIVER_PRESENT_VERSION_MINOR >= 2
    (void *)DRIPresent_ResolutionMismatch,
    (void *)DRIPresent_CreateThread,
    (void *)DRIPresent_WaitForThread,
#endif
#if D3DADAPTER_DRIVER_PRESENT_VERSION_MINOR >= 3
    (void *)DRIPresent_SetPresentParameters2,
    (void *)DRIPresent_IsBufferReleased,
    (void *)DRIPresent_WaitBufferReleaseEvent,
#endif
};

HRESULT WINAPI ID3DPresent_new(INineNine *nine, HWND hFocusWindow, D3DPRESENT_PARAMETERS *params, ID3DPresent **pp)
{
	ID3DPresentM99 *res = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(ID3DPresentM99));
	if(!res)
	{
		return E_OUTOFMEMORY;
	}
	
	res->lpVtbl = &DRIPresent_vtable;
	res->refcount = 1;
	res->nine = nine;
	res->focus_wnd = hFocusWindow;
	EnumDisplaySettingsA(NULL, ENUM_CURRENT_SETTINGS, &res->initial_mode);
	
	res->style = GetWindowLongA(hFocusWindow, GWL_STYLE);
	res->style_ex = GetWindowLongA(hFocusWindow, GWL_EXSTYLE);
	res->d3d = NULL;
	
	if(params)
	{
		memcpy(&(res->params), params, sizeof(D3DPRESENT_PARAMETERS));
	}
	
	nine_register_window(hFocusWindow, res);
	
	*pp = (ID3DPresent*)res;
	return S_OK;
}

/******************************************************************************
   PresentGroup
 ******************************************************************************/

/* IUnknown */
static ULONG WINAPI DRIPresentGroup_AddRef(ID3DPresentGroupM99 *This)
{
	LONG cnt = InterlockedIncrement(&This->refcount);
	
	return cnt;
}

static ULONG WINAPI DRIPresentGroup_Release(ID3DPresentGroupM99 *This)
{
	ULONG cnt = InterlockedDecrement(&This->refcount);
	if(cnt == 0)
	{
		HeapFree(GetProcessHeap(), 0, This);
	}
	
	return cnt;
}

static HRESULT WINAPI DRIPresentGroup_QueryInterface(ID3DPresentGroupM99 *This, REFIID riid, void **ppvObject )
{
	if(!ppvObject)
		return E_POINTER;
        
	if(IsEqualGUID(&IID_ID3DPresentGroup, riid) || IsEqualGUID(&IID_IUnknown, riid))
	{
		*ppvObject = This;
		DRIPresentGroup_AddRef(This);
		return S_OK;
	}

	*ppvObject = NULL;

	return E_NOINTERFACE;
}

static UINT WINAPI DRIPresentGroup_GetMultiheadCount(ID3DPresentGroupM99 *This)
{
	return 1;
}

static HRESULT WINAPI DRIPresentGroup_GetPresent(ID3DPresentGroupM99 *This, UINT Index, ID3DPresent **ppPresent)
{
	//printf("DRIPresentGroup_GetPresent\n");
	
	if(Index >= DRIPresentGroup_GetMultiheadCount(This))
	{
		return D3DERR_INVALIDCALL;
	}
	
	return ID3DPresent_new(This->nine, This->focus_wnd, NULL, ppPresent);
}

static HRESULT WINAPI DRIPresentGroup_CreateAdditionalPresent(ID3DPresentGroupM99 *This, D3DPRESENT_PARAMETERS *pPresentationParameters, ID3DPresent **ppPresent)
{
	return ID3DPresent_new(This->nine, This->focus_wnd, pPresentationParameters, ppPresent);
}

static void WINAPI DRIPresentGroup_GetVersion(ID3DPresentGroupM99 *This, int *major, int *minor)
{
	*major = This->major;
	*minor = This->minor;
}

static ID3DPresentGroupVtbl DRIPresentGroup_vtable = {
	(void *)DRIPresentGroup_QueryInterface,
	(void *)DRIPresentGroup_AddRef,
	(void *)DRIPresentGroup_Release,
	(void *)DRIPresentGroup_GetMultiheadCount,
	(void *)DRIPresentGroup_GetPresent,
	(void *)DRIPresentGroup_CreateAdditionalPresent,
	(void *)DRIPresentGroup_GetVersion
};

HRESULT ID3DPresentGroup_new(INineNine *nine, HWND hFocusWindow, ID3DPresentGroup **pp)
{
	ID3DPresentGroupM99 *res = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(ID3DPresentGroupM99));
	if(!res)
	{
		return E_OUTOFMEMORY;
	}
	
	res->lpVtbl = &DRIPresentGroup_vtable;
	res->refcount = 1;
	res->major = D3DADAPTER_DRIVER_PRESENT_VERSION_MAJOR;
	res->minor = D3DADAPTER_DRIVER_PRESENT_VERSION_MINOR;
	res->nine = nine;
	res->focus_wnd = hFocusWindow;
	
	*pp = (ID3DPresentGroup*)res;
	return S_OK;
}
