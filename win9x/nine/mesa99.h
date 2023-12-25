#ifndef __MESA99_H__INCLUDED__
#define __MESA99_H__INCLUDED__

typedef struct _INineNine
{
	IDirect3D9Ex base;
	LONG refcount;
	struct d3dadapter9_context ctx;
	ID3DAdapter9 *adapter9;
	struct pipe_screen *screen;
	struct stw_context *gdi_ctx;
} INineNine;

HRESULT ID3DPresentGroup_new(INineNine *nine, HWND hFocusWindow, ID3DPresentGroup **pp);

typedef BOOL (WINAPI *MesaScreenCreateH)(HDC hdc, struct pipe_screen **pScreen);
typedef BOOL (WINAPI *MesaPresentH)(struct pipe_screen *screen, struct pipe_context *ctx, struct pipe_resource *res, HDC dc, const RECT *pSrcRest, const RECT *pDstRect);
typedef BOOL (WINAPI *MesaDimensionsH)(struct pipe_screen *screen, struct pipe_context *ctx, struct pipe_resource *res, int *pWidth, int *pHeight, int *pBpp, int *pPitch);

extern MesaScreenCreateH MesaScreenCreate;
extern MesaPresentH MesaPresent;
extern MesaDimensionsH MesaDimensions;

void nine_init();
void nine_deinit();

#endif /* __MESA99_H__INCLUDED__ */
