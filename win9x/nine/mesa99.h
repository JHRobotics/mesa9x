#ifndef __MESA99_H__INCLUDED__
#define __MESA99_H__INCLUDED__

typedef struct _INineNine
{
	IDirect3D9Ex base;
	LONG refcount;
	struct d3dadapter9_context ctx;
	ID3DAdapter9 *adapter9;
	struct pipe_screen *screen;
} INineNine;

HRESULT ID3DPresentGroup_new(INineNine *nine, HWND hFocusWindow, ID3DPresentGroup **pp);

#endif /* __MESA99_H__INCLUDED__ */
