#ifndef __VRAMCPY_H__INCLUDED__
#define __VRAMCPY_H__INCLUDED__

#include <string.h>

typedef struct _vramcpy_rect_t
{
	uint32_t dst_pitch;
	uint32_t dst_x;
	uint32_t dst_y;
	uint32_t dst_w;
	uint32_t dst_h;
	uint32_t dst_bpp;
	uint32_t src_pitch;
	uint32_t src_x;
	uint32_t src_y;
	uint32_t src_bpp;
} vramcpy_rect_t;

struct sw_displaytarget;
struct sw_winsys;

void vramcpy(void *dst, void *src, vramcpy_rect_t *rect);
size_t vramcpy_pointsize(uint32_t bpp);
BOOL vramcpy_direct_rendering(HDC hDC);
BOOL vramcpy_top_window(HWND win);

uint32_t vramcpy_calc_framebuffer(uint32_t w, uint32_t h, uint32_t bpp);

void vramcpy_display(struct sw_winsys *winsys, struct sw_displaytarget *dt, HDC hDC);

BOOL vram_lock(volatile DWORD *ptr);
void vram_unlock(volatile DWORD *ptr);

#endif
