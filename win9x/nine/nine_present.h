#ifndef __NINE_PRESENT_H__INCLUDED__
#define __NINE_PRESENT_H__INCLUDED__

typedef struct _ID3DPresentGroupM99
{
	ID3DPresentGroupVtbl *lpVtbl;
	ULONG refcount;
	ULONG major;
	ULONG minor;
	HWND focus_wnd;
	INineNine *nine;
} ID3DPresentGroupM99;

typedef struct _ID3DPresentM99
{
	ID3DPresentVtbl *lpVtbl;
	ULONG refcount;
	INineNine *nine;
	D3DPRESENT_PARAMETERS params;
	HWND focus_wnd;
	HCURSOR hCursor;
	DEVMODEA initial_mode;
	DWORD style;
	DWORD style_ex;
	
	BOOL reapply_mode;
	BOOL ex;
	BOOL resolution_mismatch;
	BOOL occluded;
	BOOL filter_messages;
	BOOL no_window_changes;
	BOOL restore_screensaver;
	HWND wrapped_wnd; /* basically focus_wnd but set at a different time */
	
	struct d3d_drawable *d3d;
} ID3DPresentM99;

struct d3d_drawable
{
//	Drawable drawable; /* X11 drawable */
	UINT width;
	UINT height;
	UINT depth;
	HDC hdc;
	HWND wnd; /* HWND (for convenience) */
	RECT windowRect;
	POINT offset; /* offset of the client area compared to the X11 drawable */
};

struct D3DWindowBuffer
{
	int width;
	int height;
	int bpp;
	int pitch;
	struct pipe_screen *screen;
	struct pipe_resource *res;
	struct pipe_context *ctx;
//	uint8_t data[0];
};

BOOL nine_unregister_window(HWND window);
BOOL nine_register_window(HWND window, ID3DPresentM99 *present);
void restore_fullscreen_window(ID3DPresentM99 *This, HWND hwnd);
void move_fullscreen_window(ID3DPresentM99 *This, HWND hwnd, int w, int h);
void setup_fullscreen_window(ID3DPresentM99 *This, HWND hwnd, int w, int h);

#endif /* __NINE_PRESENT_H__INCLUDED__ */
