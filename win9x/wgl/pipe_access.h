#ifndef PIPE_ACCESS_H
#define PIPE_ACCESS_H

#ifdef __cplusplus
extern "C" {
#endif

#define MESA_SCREEN_USE_TGSI 1

BOOL WINAPI MesaDimensions(struct pipe_screen *screen, struct pipe_context *ctx, struct pipe_resource *res, int *pWidth, int *pHeight, int *pBpp, int *pPitch);
BOOL WINAPI MesaScreenCreate(HDC hdc, struct pipe_screen **pScreen, DWORD flags);
BOOL WINAPI MesaPresent(struct pipe_screen *screen, struct pipe_context *ctx, struct pipe_resource *res, HDC dc, const RECT *pSrcRest, const RECT *pDstRect);

typedef BOOL (WINAPI *MesaScreenCreateH)(HDC hdc, struct pipe_screen **pScreen, DWORD flags);
typedef BOOL (WINAPI *MesaPresentH)(struct pipe_screen *screen, struct pipe_context *ctx, struct pipe_resource *res, HDC dc, const RECT *pSrcRest, const RECT *pDstRect);
typedef BOOL (WINAPI *MesaDimensionsH)(struct pipe_screen *screen, struct pipe_context *ctx, struct pipe_resource *res, int *pWidth, int *pHeight, int *pBpp, int *pPitch);

#ifdef __cplusplus
}
#endif

#endif
