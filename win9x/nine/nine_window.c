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

typedef struct wnd_item
{
	HWND window;
//	WNDPROC winproc;
	struct wnd_item *next;
	ID3DPresentM99 *present;
} wnd_item_t;

typedef struct wnd_list
{
	wnd_item_t *first;
	wnd_item_t *last;
} wnd_list_t;

static CRITICAL_SECTION nine_cs;
static wnd_list_t nine_wl = {NULL, NULL};
static DEVMODEA savedMode = {0};

static LRESULT CALLBACK nine_winproc(HWND window, UINT message, WPARAM wparam, LPARAM lparam);

static HHOOK win_hook = NULL;

static LRESULT CALLBACK win_hook_proc(int nCode, WPARAM wParam, LPARAM lParam);

void nine_init()
{
	InitializeCriticalSection(&nine_cs);
	nine_wl.first = NULL;
	nine_wl.last  = NULL;
	
	ZeroMemory(&savedMode, sizeof(DEVMODEA));
	savedMode.dmSize = sizeof(DEVMODEA);
	EnumDisplaySettingsA(NULL, ENUM_CURRENT_SETTINGS, &savedMode);
	
	win_hook = SetWindowsHookExA(WH_CALLWNDPROC, win_hook_proc, NULL, GetCurrentThreadId());
}

void nine_deinit()
{
	DEVMODEA cur;
	/* check first if in saved struct isn't some garbage */
	//if(savedMode.dmSize == sizeof(DEVMODEA))
	if(win_hook)
	{
    UnhookWindowsHookEx(win_hook);
    win_hook = NULL;
	}
	
	if(savedMode.dmSize != 0)
	{
		ZeroMemory(&cur, sizeof(DEVMODEA));
		cur.dmSize = sizeof(DEVMODEA);
		if(EnumDisplaySettingsA(NULL, ENUM_CURRENT_SETTINGS, &cur))
		{
			if(cur.dmBitsPerPel != savedMode.dmBitsPerPel ||
				cur.dmPelsWidth   != savedMode.dmPelsWidth ||
				cur.dmPelsHeight  != savedMode.dmPelsHeight)
			{
				savedMode.dmFields = DM_BITSPERPEL | DM_PELSWIDTH | DM_PELSHEIGHT;
				ChangeDisplaySettingsA(&savedMode, CDS_FULLSCREEN);
			}
		}
	}
}

static void nine_wnd_lock()
{
	EnterCriticalSection(&nine_cs);
}

static void nine_wnd_unlock()
{
	LeaveCriticalSection(&nine_cs);
}

static HRESULT set_display_mode(ID3DPresentM99 *This, DEVMODEA *new_mode)
{
	DEVMODEA current_mode;
	LONG hr;

	/* Filter invalid resolution */
	if(!new_mode->dmPelsWidth || !new_mode->dmPelsHeight)
		return D3DERR_INVALIDCALL;

	/* Ignore invalid frequency requested */
	if (new_mode->dmDisplayFrequency > 1000)
		new_mode->dmDisplayFrequency = 0;

	ZeroMemory(&current_mode, sizeof(DEVMODEA));
	current_mode.dmSize = sizeof(DEVMODEA);
	/* Only change the mode if necessary. */
	if (!EnumDisplaySettingsA(NULL, ENUM_CURRENT_SETTINGS, &current_mode))
	{
		ERR("Failed to get current display mode.\n");
	}
	else if (current_mode.dmPelsWidth != new_mode->dmPelsWidth
		|| current_mode.dmPelsHeight != new_mode->dmPelsHeight
		|| (current_mode.dmDisplayFrequency != new_mode->dmDisplayFrequency
		&& (new_mode->dmFields & DM_DISPLAYFREQUENCY)))
	{
		//TRACE("changing display settings to %ux%u\n", (UINT)new_mode->dmPelsWidth, (UINT)new_mode->dmPelsHeight);

		hr = ChangeDisplaySettingsExA(NULL, new_mode, 0, CDS_FULLSCREEN, NULL);
		if (hr != DISP_CHANGE_SUCCESSFUL)
		{
			/* try again without display RefreshRate */
			if (new_mode->dmFields & DM_DISPLAYFREQUENCY)
			{
				new_mode->dmFields &= ~DM_DISPLAYFREQUENCY;
				new_mode->dmDisplayFrequency = 0;
				hr = ChangeDisplaySettingsExA(NULL, new_mode, 0, CDS_FULLSCREEN, NULL);
				if (hr != DISP_CHANGE_SUCCESSFUL)
				{
					ERR("ChangeDisplaySettingsExA failed with 0x%08x\n", (int)hr);
					return D3DERR_INVALIDCALL;
				}
			}
			else
			{
				ERR("ChangeDisplaySettingsExA failed with 0x%08x\n", (int)hr);
				return D3DERR_INVALIDCALL;
			}
		}
	}
	return D3D_OK;
}

static void release_focus_window(ID3DPresentM99 *This)
{
	if(This->wrapped_wnd)
	{
		nine_unregister_window(This->wrapped_wnd);
	}
    
	InterlockedExchangePointer((void **)&This->wrapped_wnd, NULL);
	if(This->restore_screensaver)
	{
		SystemParametersInfoA(SPI_SETSCREENSAVEACTIVE, TRUE, NULL, 0);
		This->restore_screensaver = FALSE;
	}
}

/* see WINE's fullscreen_style() */
static LONG fullscreen_style(LONG style)
{
	/* Make sure the window is managed, otherwise we won't get keyboard input. */
	style |= WS_POPUP | WS_SYSMENU;
	style &= ~(WS_CAPTION | WS_THICKFRAME);

	return style;
}

/* see WINE's fullscreen_exstyle() */
static LONG fullscreen_exstyle(LONG exstyle)
{
	/* Filter out window decorations. */
	exstyle &= ~(WS_EX_WINDOWEDGE | WS_EX_CLIENTEDGE);

	return exstyle;
}

static LRESULT device_process_message(ID3DPresentM99 *present, HWND window, BOOL unicode, UINT message, WPARAM wparam, LPARAM lparam, WNDPROC proc)
{
	boolean filter_messages;
	WORD width, height;
	DEVMODEA new_mode;

	//TRACE("Got message: window %p, message %#x, wparam %#lx, lparam %#lx.\n",
	//      window, message, wparam, lparam);

	if (present->filter_messages && message != WM_DISPLAYCHANGE)
	{
		//TRACE("Filtering message: window %p, message %#x, wparam %#lx, lparam %#lx.\n",
		//      window, message, wparam, lparam);
		//return DefWindowProcA(window, message, wparam, lparam);
		return 0;
	}

	/* In fullscreen mode, the style is not supposed to affect appearance (because
	 * exclusive fullscreen). However there is no public API to get fullscreen mode
	 * and thus wine doesn't implement any way to get it. Thus fullscreen is emulated
	 * with a specific style. When apps change the style, do not pass them this message,
	 * such that the window doesn't get borders */
	if(message == WM_NCCALCSIZE && wparam == TRUE)
	{
		return 0;
	}

	if(message == WM_DESTROY)
	{
		//TRACE("unregister window %p.\n", window);
		if (window != present->wrapped_wnd)
		{
			ERR("Receiving window %p not wrapped (%p)\n", window, present->wrapped_wnd);
		}
		release_focus_window(present);
	}
	else if(message == WM_DISPLAYCHANGE)
	{
		width  = LOWORD(lparam);
		height = HIWORD(lparam);

		//TRACE("WM_DISPLAYCHANGE %ux%u -> %ux%u\n",
		//	present->params.BackBufferWidth, present->params.BackBufferHeight, width, height);

		/* Ex restores display mode, while non Ex requires the
		 * user to call Device::Reset() */
		if(!present->ex && !present->params.Windowed &&
			present->params.hDeviceWindow &&
			(width != present->params.BackBufferWidth || height != present->params.BackBufferHeight))
		{
			//TRACE("setting resolution_mismatch for non-extended\n");
			present->resolution_mismatch = TRUE;
		}
		else
		{
			present->resolution_mismatch = FALSE;
		}
	}
	else if(message == WM_ACTIVATEAPP)
	{
		filter_messages = present->filter_messages;
		present->filter_messages = TRUE;

		if(wparam == WA_INACTIVE)
		{
			//TRACE("WM_ACTIVATEAPP WA_INACTIVE\n");

			present->occluded = TRUE;
			present->reapply_mode = TRUE;

			ZeroMemory(&new_mode, sizeof(DEVMODEA));
			new_mode.dmSize = sizeof(DEVMODEA);
			/* TODO: Win95 has problem with ENUM_REGISTRY_SETTINGS */
			if(EnumDisplaySettingsA(NULL, ENUM_REGISTRY_SETTINGS, &new_mode))
			{
				set_display_mode(present, &new_mode);
			}

			if(!present->no_window_changes && IsWindowVisible(present->params.hDeviceWindow))
			{
				ShowWindow(present->params.hDeviceWindow, SW_MINIMIZE);
			}
		}
		else
		{
			//TRACE("WM_ACTIVATEAPP\n");

			present->occluded = FALSE;

			if(!present->no_window_changes)
			{
				/* restore window */
				SetWindowPos(present->params.hDeviceWindow, NULL, 0, 0,
					present->params.BackBufferWidth, present->params.BackBufferHeight,
					SWP_NOACTIVATE | SWP_NOZORDER);
			}

			if(present->ex)
			{
				ZeroMemory(&new_mode, sizeof(DEVMODEA));
				new_mode.dmSize = sizeof(DEVMODEA);
				new_mode.dmPelsWidth = present->params.BackBufferWidth;
				new_mode.dmPelsHeight = present->params.BackBufferHeight;
				new_mode.dmFields = DM_PELSWIDTH | DM_PELSHEIGHT;
				if(present->params.FullScreen_RefreshRateInHz)
				{
					new_mode.dmFields |= DM_DISPLAYFREQUENCY;
					new_mode.dmDisplayFrequency = present->params.FullScreen_RefreshRateInHz;
				}
				set_display_mode(present, &new_mode);
			}
		}
		present->filter_messages = filter_messages;
	}
	else if(message == WM_SYSCOMMAND)
	{
		if(wparam == SC_RESTORE)
		{
			DefWindowProcA(window, message, wparam, lparam);
		}
	}

	//return CallWindowProcA(proc, window, message, wparam, lparam);
	return 0;
}

static LRESULT CALLBACK win_hook_proc(int nCode, WPARAM wParam, LPARAM lParam)
{
	PCWPSTRUCT pParams = (PCWPSTRUCT)lParam;
	ID3DPresentM99 *present;
	wnd_item_t *item;
	WNDPROC proc;
	
	if (nCode < 0)
		return CallNextHookEx(NULL, nCode, wParam, lParam);

	nine_wnd_lock();

	item = nine_wl.first;
	while(item != NULL)
	{
		if(item->window == pParams->hwnd) break;
	}
	
	if(item)
	{
		present = item->present;

		if(present)
		{
			device_process_message(present, pParams->hwnd, FALSE, pParams->message, pParams->wParam, pParams->lParam, NULL);
		}
	}
	
	nine_wnd_unlock();
	
	return CallNextHookEx(NULL, nCode, wParam, lParam);
}

/*
static LRESULT CALLBACK nine_winproc(HWND window, UINT message, WPARAM wparam, LPARAM lparam)
{
	ID3DPresentM99 *present;
	wnd_item_t *item;
	WNDPROC proc;

	nine_wnd_lock();

	item = nine_wl.first;
	while(item != NULL)
	{
		if(item->window == window) break;
	}
	
	if(!item)
	{
		nine_wnd_unlock();
		ERR("Window %p is not registered with nine.\n", window);
		return DefWindowProcA(window, message, wparam, lparam);
	}

	present = item->present;
	proc    = item->winproc;
	
	nine_wnd_unlock();

	if(present)
		return device_process_message(present, window, FALSE, message, wparam, lparam, proc);

	return CallWindowProcA(proc, window, message, wparam, lparam);
}
*/

BOOL nine_register_window(HWND window, ID3DPresentM99 *present)
{
	nine_wnd_lock();
	
	wnd_item_t *item = calloc(1, sizeof(wnd_item_t));
	if(item == NULL)
	{
		nine_wnd_unlock();
		return FALSE;
	}
	
	item->window = window;
	item->next = NULL;
	
	
	/*
	JHR: for future NT (5.x) support
	  item->unicode = IsWindowUnicode(window);
	  if(item->unicode)
	  {
	    item->winproc = SetWindowLongPtrW(...)
	  }
	*/
	
	//item->winproc = (WNDPROC)SetWindowLongPtrA(window, GWLP_WNDPROC, (LONG_PTR)nine_winproc);
	item->present = present;
	
	if(nine_wl.last)
	{
		nine_wl.last->next = item;
		nine_wl.last       = item;
	}
	else
	{
		nine_wl.first = item;
		nine_wl.last  = item;
	}
	
	nine_wnd_unlock();
}

BOOL nine_unregister_window(HWND window)
{
	struct nine_wndproc *entry, *last;
	LONG_PTR proc;
	wnd_item_t *item, *prev;

	nine_wnd_lock();

	prev = NULL;
	item = nine_wl.first;
	while(item != NULL)
	{
		if(item->window == window) break;
		
		prev = item;
	}

	if(item == NULL)
	{
		nine_wnd_unlock();
		return FALSE;
	}

/*
	proc = GetWindowLongPtrA(window, GWLP_WNDPROC);
	if(proc != (LONG_PTR)nine_winproc)
	{
		item->present = NULL;
		nine_wnd_unlock();
		WARN("Not unregistering window %p, window proc %#lx doesn't match nine window proc %p.\n", window, proc, nine_winproc);
		return FALSE;
	}

	SetWindowLongPtrA(window, GWLP_WNDPROC, (LONG_PTR)item->winproc);
*/
	
	if(prev == NULL)
	{
		nine_wl.first = item->next;
	}
	else
	{
		prev->next = item->next;
	}
	
	if(item->next == NULL)
	{
		nine_wl.last = prev;
	}
	
	free(item);

	nine_wnd_unlock();
	return TRUE;
}

void restore_fullscreen_window(ID3DPresentM99 *This, HWND hwnd)
{
	/* See wined3d_swapchain_state_restore_from_fullscreen */
	unsigned int window_pos_flags = SWP_FRAMECHANGED | SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOMOVE | SWP_NOSIZE;
	HWND window_pos_after = NULL;
	boolean filter_messages;
	LONG style, style_ex;

	if(This->ex && !This->no_window_changes)
	{
		window_pos_after = (This->style_ex & WS_EX_TOPMOST) ? HWND_TOPMOST : HWND_NOTOPMOST;
		window_pos_flags |= (This->style & WS_VISIBLE) ? SWP_SHOWWINDOW : SWP_HIDEWINDOW;
		window_pos_flags &= ~SWP_NOZORDER;
	}
	/* switch from fullscreen to window */
	style = GetWindowLongA(hwnd, GWL_STYLE);
	style_ex = GetWindowLongA(hwnd, GWL_EXSTYLE);

	This->style ^= (This->style ^ style) & WS_VISIBLE;
	This->style_ex ^= (This->style_ex ^ style_ex) & WS_EX_TOPMOST;


	filter_messages = This->filter_messages;
	This->filter_messages = TRUE;
	if(style == fullscreen_style(This->style) &&
		style_ex == fullscreen_exstyle(This->style_ex))
	{
		SetWindowLongA(hwnd, GWL_STYLE, This->style);
		SetWindowLongA(hwnd, GWL_EXSTYLE, This->style_ex);
	}
	SetWindowPos(hwnd, window_pos_after, 0, 0, 0, 0, window_pos_flags);
	This->filter_messages = filter_messages;

	This->style = 0;
	This->style_ex = 0;
}

void setup_fullscreen_window(ID3DPresentM99 *This, HWND hwnd, int w, int h)
{
	boolean filter_messages;
	LONG style, style_ex;

	This->style = GetWindowLongA(hwnd, GWL_STYLE);
	This->style_ex = GetWindowLongA(hwnd, GWL_EXSTYLE);

	style = fullscreen_style(This->style);
	style_ex = fullscreen_exstyle(This->style_ex);

	filter_messages = This->filter_messages;
	This->filter_messages = TRUE;

	SetWindowLongA(hwnd, GWL_STYLE, style);
	SetWindowLongA(hwnd, GWL_EXSTYLE, style_ex);

	/* TODO: wine doesn't always set 0, 0. Multi-monitor ? */
	SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, w, h,
		SWP_FRAMECHANGED | SWP_NOACTIVATE |
		(This->no_window_changes ? SWP_NOZORDER : SWP_SHOWWINDOW));

	This->filter_messages = filter_messages;
}

void move_fullscreen_window(ID3DPresentM99 *This, HWND hwnd, int w, int h)
{
	boolean filter_messages;
	LONG style, style_ex;

	/* move draw window back to place */

	style = GetWindowLongA(hwnd, GWL_STYLE);
	style_ex = GetWindowLongA(hwnd, GWL_EXSTYLE);

	style = fullscreen_style(style);
	style_ex = fullscreen_exstyle(style_ex);

	filter_messages = This->filter_messages;
	This->filter_messages = TRUE;
	SetWindowLongA(hwnd, GWL_STYLE, style);
	SetWindowLongA(hwnd, GWL_EXSTYLE, style_ex);
	SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, w, h,
		SWP_FRAMECHANGED | SWP_NOACTIVATE |
		(This->no_window_changes ? SWP_NOZORDER : SWP_SHOWWINDOW));

	This->filter_messages = filter_messages;
}
