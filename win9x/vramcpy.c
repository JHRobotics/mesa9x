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

/* sse vector types */
#if defined(__GNUC__) && defined(__SSE__)
typedef unsigned int   v4ui __attribute__ ((vector_size(16)));
typedef unsigned short v4us __attribute__ ((vector_size(8)));
#endif


#define QUERYESCSUPPORT 8
#define OPENGL_GETINFO 0x1101
#define FBHDA_REQ      0x110A
#define FBHDA_UPDATE   0x110C
#define FBHDA_NEED_UPDATE 1

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
} FBHDA;
#pragma pack(pop)

/* convert bits per pixel to bytes per pixel */
#define vramcpy_pointsize_fast(bpp) (((uint32_t)(bpp)+7) >> 3)

size_t vramcpy_pointsize(uint32_t bpp)
{
	return vramcpy_pointsize_fast(bpp);
}

/* read pixel from buffer as 32bit XRGB */
static inline uint32_t pixel_read(uint8_t *src, size_t ps)
{
	uint32_t pixel = 0;
	switch(ps)
	{
		case 1:
			pixel = (src[0] << 16) | (src[0] << 8) | src[1]; // TODO: placeholder only!
			break;
		case 2:
			{
				uint16_t tmp = *((uint16_t*)src);
				pixel = (tmp & 0x1F) << 3;
				tmp >>= 5;
				pixel |= (tmp & 0x3F) << (8 + 2);
				tmp >>= 6;
				pixel |= tmp << (16 + 3);
			}
			break;
		case 3:
			pixel = (src[0] << 16) | (src[1] << 8) | src[2];
			break;
		case 4:
			pixel = *((uint32_t*)src);
			break;
	}
	
	return pixel;
}

/* write 32bit pixel to buffer */
static inline void pixel_write(uint8_t *dst, size_t ps, uint32_t pixel)
{
	switch(ps)
	{
		case 1:
			*dst = (pixel >> 8) & 0xFF; // TODO: placeholder only!
			break;
		case 2:
			{
			uint16_t tmp;
			tmp  =  (pixel >> ( 0 + 3)) & 0x1F;
			tmp |= ((pixel >> ( 8 + 2)) & 0x3F) << 5;
			tmp |= ((pixel >> (16 + 3)) & 0x1F) << (5+6);
			*((uint16_t*)dst) = tmp;
			}
			break;
		case 3:
			dst[0] = (pixel >> 16) & 0xFF;
			dst[1] = (pixel >> 8) & 0xFF;
			dst[2] = pixel & 0xFF;
			break;
		case 4:
			*((uint32_t*)dst) = pixel;
			break;
	}
}

/* rectagle src to dst */
void vramcpy(void *dst, void *src, vramcpy_rect_t *rect)
{
	uint8_t *psrc = src;
	uint8_t *pdst = dst;
	uint32_t y, x;
	
	if(rect->src_bpp == rect->dst_bpp)
	{
		const size_t ps = vramcpy_pointsize_fast(rect->src_bpp);
		
		psrc += rect->src_pitch * rect->src_y;
		pdst += rect->dst_pitch * rect->dst_y;
			
		for(y = 0; y < rect->dst_h; y++)
		{
			memcpy(pdst + (rect->dst_x * ps), psrc + (rect->src_x * ps), rect->dst_w*ps);
			psrc += rect->src_pitch;
			pdst += rect->dst_pitch;
		}
	}
	else if(rect->src_bpp == 32 && rect->dst_bpp == 16) /* accelerated 32b rendering to 16b screen */
	{
		psrc += rect->src_pitch * rect->src_y;
		pdst += rect->dst_pitch * rect->dst_y;
			
		for(y = 0; y < rect->dst_h; y++)
		{
			uint8_t *src_pix = psrc + (rect->src_x * 4);
			uint8_t *dst_pix = pdst + (rect->dst_x * 2);
			int i = rect->dst_w;
#if defined(__GNUC__) && defined(__SSE__)
			for(; i >= 4; i -= 4)
			{
				v4us v1;
				v4ui r, g, b;
				v4ui v = *((v4ui*)src_pix);
				r = v & 0x00F80000;
				g = v & 0x0000FC00;
				b = v & 0x000000F8;
				r = r >> 8;
				g = g >> 5;
				b = b >> 3;
				v = r | g | b;
				v1 = __builtin_convertvector(v, v4us);
				*((v4us*)dst_pix) = v1;
				
				src_pix += 4*4;
				dst_pix += 2*4;
			}
#endif
			for(; i > 0; i--)
			{
				uint32_t r, g, b;
				uint32_t p = *((uint32_t*)src_pix);
				r = p & 0x00F80000;
				g = p & 0x0000FC00;
				b = p & 0x000000F8;
				r = r >> 8;
				g = g >> 5;
				b = b >> 3;
				p = r | g | b;
				*((uint16_t*)dst_pix) = (uint16_t)p;
				
				src_pix += 4;
				dst_pix += 2;
			}
			
			psrc += rect->src_pitch;
			pdst += rect->dst_pitch;
		}
	}
	else
	{
		const size_t ps = vramcpy_pointsize_fast(rect->src_bpp);
		const size_t pd = vramcpy_pointsize_fast(rect->dst_bpp);
		
		psrc += rect->src_pitch * rect->src_y;
		pdst += rect->dst_pitch * rect->dst_y;
			
		for(y = 0; y < rect->dst_h; y++)
		{
			uint8_t *ppsrc = psrc + (rect->src_x * ps);
			uint8_t *ppdst = pdst + (rect->dst_x * pd);
			for(x = 0; x < rect->dst_w; x++)
			{
				const uint32_t px = pixel_read(ppsrc, ps);
				pixel_write(ppdst, pd, px);
				ppsrc += ps;
				ppdst += pd;
			}
			psrc += rect->src_pitch;
			pdst += rect->dst_pitch;
		}
	}
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
		
		if(fbhda != NULL)
		{
			if(!vramcpy_direct_rendering(hDC));
			{
				vramcpy_display_window(gdt, hDC);
				return;
			}
			
			HWND hwnd = WindowFromDC(hDC);
			vramcpy_rect_t crect;
			RECT wrect;
			
			if(hwnd && GetWindowRect(hwnd, &wrect))
			{
				uint32_t render_width = wrect.right - wrect.left;
				uint32_t render_height = wrect.bottom - wrect.top;
				
				crect.dst_x     = wrect.left;
				crect.dst_y     = wrect.top;
				crect.dst_w     = render_width;
				crect.dst_h     = render_height;
				crect.dst_bpp   = fbhda->bpp;
				crect.dst_pitch = fbhda->pitch;
				crect.src_x     = 0;
				crect.src_y     = 0;
				crect.src_bpp   = gdt->bmi.bmiHeader.biBitCount;
				crect.src_pitch = gdt->stride;//gdt->width * vramcpy_pointsize_fast(crect.src_bpp);
				
				vramcpy(fbhda->fb_pm32, gdt->data, &crect);
				if(fbhda_have_flags)
				{
					if(fbhda->flags & FBHDA_NEED_UPDATE)
					{
						ExtEscape(hDC, FBHDA_UPDATE, sizeof(RECT), (LPCSTR)&wrect, 0, NULL);
					}
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
