#ifdef MESA23
# include "state_tracker/st_format.h"
# include "state_tracker/st_context.h"
#endif

#include <windows.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdint.h>
#include "vramcpy.h"

/* SW headers */
#ifdef MESA_NEW
# include "pipe/p_format.h"
# include "pipe/p_context.h"
#endif

#include "util/u_inlines.h"

#if defined(MESA_NEW) || defined(MESA23)
# include "u_format.h"
#else
# include "util/u_format.h"
#endif

#include "util/u_math.h"
#include "util/u_memory.h"

#if defined(MESA_NEW) || defined(MESA23)
# include "frontend/sw_winsys.h"
#else
# include "state_tracker/sw_winsys.h"
#endif

#include "gdi/gdi_sw_winsys.h"


#define QUERYESCSUPPORT 8
#define OPENGL_GETINFO 0x1101
#define FBHDA_REQ      0x110A
#define FBHDA_UPDATE   0x110C
#define FBHDA_NEED_UPDATE 1
#define FBHDA_LOCKING 2
#define FBHDA_FLIPING 4

#pragma pack(push)
#pragma pack(1)
typedef struct _FBHDA
{
	volatile DWORD width;
	volatile DWORD height;
	volatile DWORD bpp;
	volatile DWORD pitch;
	void *       fb_pm32;
	DWORD        fb_pm16;
	DWORD        flags;
	volatile DWORD lock;
	void *       vram_pm32;
	DWORD        vram_pm16;
	DWORD        vram_size;
} FBHDA;
#pragma pack(pop)

/* convert bits per pixel to bytes per pixel */
#define vramcpy_pointsize_fast(bpp) (((uint32_t)(bpp)+7) >> 3)

size_t vramcpy_pointsize(uint32_t bpp)
{
	return vramcpy_pointsize_fast(bpp);
}

/* calculate framebuffer size and round up it to MB */
uint32_t vramcpy_calc_framebuffer(uint32_t w, uint32_t h, uint32_t bpp)
{
	size_t pb = vramcpy_pointsize_fast(bpp);
	
	uint32_t mbs = ((w*h*pb) + 0xFFFFF) / 0x100000; // size in MB
	
	return mbs * 0x100000;
}

static FBHDA *fbhda = NULL;
static BOOL   fbhda_have_flags = FALSE;
static HDC failure_hdc = NULL;

static int IsSupportedEsc(HDC gdi_ctx, int code)
{
	int test = 0;
	DWORD inData = code;
	
	test = ExtEscape(gdi_ctx, QUERYESCSUPPORT, sizeof(DWORD), (LPCSTR)&inData, 0, NULL);
	return test;
}

/* same as in gdi_sw_winsys.c */
static inline struct gdi_sw_displaytarget *gdi_sw_displaytarget(struct sw_displaytarget *buf)
{
   return (struct gdi_sw_displaytarget *)buf;
}

static inline void vramcpy_display_window(struct gdi_sw_displaytarget *gdt, HDC hDC)
{
	StretchDIBits(hDC, 0, 0, gdt->width, gdt->height,
	                   0, 0, gdt->width, gdt->height,
		                 gdt->data, &gdt->bmi, 0, SRCCOPY);
}

BOOL vramcpy_direct_rendering(HDC hDC)
{
	static HDC saved_hDC = INVALID_HANDLE_VALUE;
	static BOOL saved_result = FALSE;
	
	if(saved_hDC == hDC) return saved_result;
	
	PIXELFORMATDESCRIPTOR pfd;
	
	DescribePixelFormat(hDC, GetPixelFormat(hDC), sizeof(PIXELFORMATDESCRIPTOR), &pfd);

	if((pfd.dwFlags & PFD_DRAW_TO_WINDOW) == 0)
	{
		saved_result = TRUE;
	}
	else
	{
		saved_result = FALSE;
	}
	
	saved_hDC = hDC;
	
	return saved_result;
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

#define fbhda_need_update (fbhda_have_flags && (fbhda->flags & FBHDA_NEED_UPDATE))
#define fbhda_locking     (fbhda_have_flags && (fbhda->flags & FBHDA_LOCKING))

void vramcpy_display(struct sw_winsys *winsys, struct sw_displaytarget *dt, HDC hDC)
{
	static uint32_t fbhda_ptr;
	struct gdi_sw_displaytarget *gdt = gdi_sw_displaytarget(dt);

	if(hDC != failure_hdc)
	{
		if(fbhda == NULL)
		{
			if(IsSupportedEsc(hDC, FBHDA_REQ))
			{
				if(ExtEscape(hDC, FBHDA_REQ, 0, NULL, sizeof(uint32_t), (LPSTR)&fbhda_ptr))
				{
					if(fbhda_ptr != 0)
					{
						fbhda = (FBHDA*)fbhda_ptr;
						
						if(IsSupportedEsc(hDC, FBHDA_UPDATE))
						{
							fbhda_have_flags = TRUE;
						}
						
					}
				}
			}
			
			if(fbhda == NULL)
			{
				failure_hdc = hDC; /* not query display for every time */
			}
		}
		
		if(fbhda != NULL && fbhda->bpp > 8)
		{
			HWND hwnd = WindowFromDC(hDC);
			
			if(hwnd == NULL) return;
			
			if(!vramcpy_direct_rendering(hDC))
			{
				if(!vramcpy_top_window(hwnd))
				{
					vramcpy_display_window(gdt, hDC);
					return;
				}
			}

			vramcpy_rect_t crect;
			RECT wrect;
			
			if(GetClientRect(hwnd, &wrect))
			{
				POINT p1 = {wrect.left, wrect.top};
				POINT p2 = {wrect.right, wrect.bottom};
				ClientToScreen(hwnd, &p1);
				ClientToScreen(hwnd, &p2);
								
				crect.dst_x     = p1.x;
				crect.dst_y     = p1.y;
				crect.dst_w     = p2.x - p1.x;
				crect.dst_h     = p2.y - p1.y;
				crect.dst_bpp   = fbhda->bpp;
				crect.dst_pitch = fbhda->pitch;
				crect.src_x     = 0;
				crect.src_y     = 0;
				crect.src_bpp   = gdt->bmi.bmiHeader.biBitCount;
				crect.src_pitch = gdt->stride;//gdt->width * vramcpy_pointsize_fast(crect.src_bpp);
				
				if(fbhda_locking) vram_lock(&fbhda->lock);
				vramcpy(fbhda->fb_pm32, gdt->data, &crect);
				if(fbhda_locking)	vram_unlock(&fbhda->lock);
				
				if(fbhda_need_update)
				{
					ExtEscape(hDC, FBHDA_UPDATE, sizeof(RECT), (LPCSTR)&wrect, 0, NULL);
				}
								
				return;
			}
		}
		
	}
	
	vramcpy_display_window(gdt, hDC);
}

void vramcpy_display_sw(struct sw_winsys *winsys, struct sw_displaytarget *dt, HDC hDC)
{
	struct gdi_sw_displaytarget *gdt = gdi_sw_displaytarget(dt);
	vramcpy_display_window(gdt, hDC);
}
