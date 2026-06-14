#if MESA_MAJOR >= 23
# include "pipe/p_defines.h"
# include "state_tracker/st_format.h"
# include "state_tracker/st_context.h"
#endif

#include <windows.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdint.h>
#include "3d_accel.h"
#include "vramcpy.h"

/* SW headers */
#if MESA_MAJOR == 21
# include "pipe/p_format.h"
# include "pipe/p_context.h"
#endif

#include "util/u_inlines.h"

#if MESA_MAJOR >= 21
# include "u_format.h"
#else
# include "util/u_format.h"
#endif

#include "util/u_math.h"
#include "util/u_memory.h"

#if MESA_MAJOR >= 21
# include "frontend/sw_winsys.h"
#else
# include "state_tracker/sw_winsys.h"
#endif

#include "gdi/gdi_sw_winsys.h"

DEBUG_GET_ONCE_BOOL_OPTION(mesa_sw_gamma, "MESA_SW_GAMMA_ENABLED", FALSE);

DWORD HDA_pitch(DWORD width, DWORD bpp)
{
	DWORD bp = (bpp + 7) / 8;
	return (bp * width + (FBHDA_ROW_ALIGN-1)) & (~((DWORD)FBHDA_ROW_ALIGN-1));
}

/* calculate framebuffer size and round up it to MB */
uint32_t vramcpy_calc_framebuffer(uint32_t w, uint32_t h, uint32_t bpp)
{
	uint32_t mbs = ((h*HDA_pitch(w, bpp)) + 0xFFFFF) / 0x100000; // size in MB
	
	return mbs * 0x100000;
}

/* same as in gdi_sw_winsys.c */
static inline struct gdi_sw_displaytarget *gdi_sw_displaytarget(struct sw_displaytarget *buf)
{
	return (struct gdi_sw_displaytarget *)buf;
}

void vramcpy_blit(HDC hDC, BITMAPINFO *bmi, void *data, uint32_t w, uint32_t h)
{
	FBHDA_t *fbhda = FBHDA_setup();
	BITMAPINFOHEADER *src = (BITMAPINFOHEADER*)bmi;

	StretchDIBits(hDC, 0, 0, w, h,
                     0, 0, w, h,
	                   data, bmi, 0, SRCCOPY);
}

static inline void vramcpy_display_window(struct gdi_sw_displaytarget *gdt, HDC hDC)
{
	vramcpy_blit(hDC, &gdt->bmi, gdt->data, gdt->width, gdt->height);
}

BOOL vramcpy_top_window(HWND win)
{
	HWND act = GetForegroundWindow();
	
	if(act == win)
	{
		return TRUE;
	}
	
	return FALSE;
}

#ifndef MIN
#define MIN(_x, _y) (((_x) > (_y)) ? (_y) : (_x))
#endif

void vramcpy_display(struct sw_winsys *winsys, struct sw_displaytarget *dt, HDC hDC)
{
	struct gdi_sw_displaytarget *gdt = gdi_sw_displaytarget(dt);
	
	FBHDA_t *fbhda = FBHDA_setup();

	if(fbhda != NULL && fbhda->bpp > 8)
	{
		if(fbhda->overlay == 0)
		{
			HWND hwnd = WindowFromDC(hDC);
			
			if(hwnd == NULL) return;
				
			if(!vramcpy_top_window(hwnd))
			{
				vramcpy_display_window(gdt, hDC);
				return;
			}
				
			vramcpy_rect_t crect;
			RECT wrect;
				
			if(GetClientRect(hwnd, &wrect))
			{
				POINT p1 = {wrect.left, wrect.top};
				POINT p2 = {wrect.right, wrect.bottom};
				ClientToScreen(hwnd, &p1);
				ClientToScreen(hwnd, &p2);
				
				int view_x = p1.x;
				int view_y = p1.y;
				int view_w = p2.x - p1.x;
				int view_h = p2.y - p1.y;
				int src_x = 0;
				int src_y = 0;
				
				if(view_x < 0)
				{
					src_x  = -view_x;
					view_x = 0;
					view_w -= src_x;
				}
				
				if(view_y < 0)
				{
					src_y  = -view_y;
					view_y = 0;
					view_h -= src_y;
				}
				
				if(view_w+src_x > gdt->width)
				{
					view_w = gdt->width - src_x;
				}
				
				if(view_h+src_y > gdt->height)
				{
					view_h = gdt->height - src_y;
				}
				
				if(view_x + view_w > fbhda->width)
				{
					view_w -= (view_x + view_w) - fbhda->width;
				}
				
				if(view_y + view_h > fbhda->height)
				{
					view_h -= (view_y + view_h) - fbhda->height;
				}
				
				if(view_w > 0 && view_h > 0)
				{
					crect.dst_x     = view_x;
					crect.dst_y     = view_y;
					crect.dst_w     = view_w;
					crect.dst_h     = view_h;
					crect.dst_bpp   = fbhda->bpp;
					crect.dst_pitch = fbhda->pitch;
					crect.src_x     = src_x;
					crect.src_y     = src_y;
					crect.src_bpp   = gdt->bmi.bmiHeader.biBitCount;
					crect.src_pitch = gdt->stride;
				
					FBHDA_access_begin(0);
					vramcpy(((BYTE*)fbhda->vram_pm32) + fbhda->surface, gdt->data, &crect);
					FBHDA_access_end(0);
				}
				
				return;
			}
		} // overlay 0
	} // fbhda
	
	vramcpy_display_window(gdt, hDC);
}

void vramcpy_display_sw(struct sw_winsys *winsys, struct sw_displaytarget *dt, HDC hDC)
{
	struct gdi_sw_displaytarget *gdt = gdi_sw_displaytarget(dt);
	vramcpy_display_window(gdt, hDC);
}
