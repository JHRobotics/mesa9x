#include <windows.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include "vramcpy.h"

typedef void (*vramcpy_f)(uint8_t *psrc, uint8_t *pdst, vramcpy_rect_t *rect);

/*
#ifdef __GNUC__
#define vram_memcpy __builtin_memcpy
#else
#define vram_memcpy memcpy
#endif
*/
#define vram_memcpy memcpy_inline

/* sse vector types */
#if defined(__GNUC__) && defined(__SSE__)
typedef unsigned int   v4ui __attribute__ ((vector_size(16)));
typedef unsigned short v4us __attribute__ ((vector_size(8)));
#endif


#define R_RGB16(_px) (((_px) & 0xF800) >> 8)
#define G_RGB16(_px) (((_px) & 0x07E0) >> 3)
#define B_RGB16(_px) (((_px) & 0x001F) << 3)

#define R_RGB15(_px) (((_px) & 0x7E00) >> 7)
#define G_RGB15(_px) (((_px) & 0x03E0) >> 2)
#define B_RGB15(_px) (((_px) & 0x001F) << 3)

#define R_RGB32(_px) (((px) >> 16) & 0xFF)
#define G_RGB32(_px) (((px) >> 8) & 0xFF)
#define B_RGB32(_px) ((_px) & 0xFF)

#define R_RGB24 R_RGB32
#define G_RGB24 G_RGB32
#define B_RGB24 B_RGB32

#define B_BGR16(_px) (((_px) & 0xF800) >> 8)
#define G_BGR16(_px) (((_px) & 0x07E0) >> 3)
#define R_BGR16(_px) (((_px) & 0x001F) << 3)

#define B_BGR15(_px) (((_px) & 0x7E00) >> 7)
#define G_BGR15(_px) (((_px) & 0x03E0) >> 2)
#define R_BGR15(_px) (((_px) & 0x001F) << 3)

#define B_BGR32(_px) (((px) >> 16) & 0xFF)
#define G_BGR32(_px) (((px) >> 8) & 0xFF)
#define R_BGR32(_px) ((_px) & 0xFF)

#define B_BGR24 R_BGR32
#define G_BGR24 G_BGR32
#define R_BGR24 B_BGR32

#define RGB_16(_r, _g, _b) ( \
	(((_r) & 0xF8) << 8) | \
	(((_g) & 0xFC) << 3) | \
	(((_b) /*& 0xF8*/) >> 3) )
	
#define RGB_15(_r, _g, _b) ( \
	(((_r) & 0xF8) << 7) | \
	(((_g) & 0xF8) << 2) | \
	(((_b) /*& 0xF8*/) >> 3) )

#define RGB_32(_r, _g, _b) (((_r) << 16) | ((_g) << 8) | (_b))

#define RGB_24 RGB_32

#define PS_15 2
#define PS_16 2
#define PS_24 3
#define PS_32 4

#define PT_15 uint16_t 
#define PT_16 uint16_t
#define PT_24 uint32_t
#define PT_32 uint32_t

#define READ_16(_ptr) (*((uint16_t*)_ptr))
#define READ_32(_ptr) (*((uint32_t*)_ptr))
#define READ_15 READ_16
#define READ_24 READ_32

#define WRITE_16(_ptr, _px) *((uint16_t*)_ptr) = _px
#define WRITE_32(_ptr, _px) *((uint32_t*)_ptr) = _px
#define WRITE_15 WRITE_16

#define CONV_FUNC(_src, _dst, _colors, _postfix) \
static void vramcpy_ ## _src ## _ ## _dst ## _ ## _colors ## _ ## _postfix (uint8_t *psrc, uint8_t *pdst, vramcpy_rect_t *rect) { \
	uint32_t y, x; \
	psrc += rect->src_pitch * rect->src_y; \
	pdst += rect->dst_pitch * rect->dst_y; \
	for(y = 0; y < rect->dst_h; y++) { \
		uint8_t *ppsrc = psrc + (rect->src_x * PS_ ## _src); \
		uint8_t *ppdst = pdst + (rect->dst_x * PS_ ## _dst); \
		for(x = 0; x < rect->dst_w; x++) { \
			const PT_ ## _src px = READ_ ## _src (ppsrc); \
			WRITE_ ## _dst (ppdst, RGB_ ## _dst (\
				GAMMA_R(R_ ## _colors ## _src (px)), \
				GAMMA_G(G_ ## _colors ## _src (px)), \
				GAMMA_B(B_ ## _colors ## _src (px)) \
			)); \
			ppsrc += PS_ ## _src; \
			ppdst += PS_ ## _dst; \
		}  \
		psrc += rect->src_pitch; \
		pdst += rect->dst_pitch; \
	} }

#define CONV_FUNC24(_src, _colors, _postfix) \
static void vramcpy_ ## _src ## _24 ## _ ## _colors ## _ ## _postfix (uint8_t *psrc, uint8_t *pdst, vramcpy_rect_t *rect) { \
	uint32_t y, x; \
	psrc += rect->src_pitch * rect->src_y; \
	pdst += rect->dst_pitch * rect->dst_y; \
	for(y = 0; y < rect->dst_h; y++) { \
		uint8_t *ppsrc = psrc + (rect->src_x * PS_ ## _src); \
		uint8_t *ppdst = pdst + (rect->dst_x * 3); \
		for(x = 0; x < rect->dst_w; x++) { \
			const PT_ ## _src px = READ_ ## _src (ppsrc); \
			*ppdst = GAMMA_R(B_ ## _colors ## _src (px)); ppdst++; \
			*ppdst = GAMMA_G(G_ ## _colors ## _src (px)); ppdst++; \
			*ppdst = GAMMA_B(R_ ## _colors ## _src (px)); ppdst++; \
			ppsrc += PS_ ## _src; \
		}  \
		psrc += rect->src_pitch; \
		pdst += rect->dst_pitch; \
	} }

#define CONV_FUNC_COPY_RGB(_src) \
static void vramcpy_ ## _src ## _ ## _src ## _RGB_copy (uint8_t *psrc, uint8_t *pdst, vramcpy_rect_t *rect) { \
	uint32_t y; \
	psrc += rect->src_pitch * rect->src_y; \
	pdst += rect->dst_pitch * rect->dst_y; \
	for(y = 0; y < rect->dst_h; y++){ \
		vram_memcpy(pdst + (rect->dst_x * PS_ ## _src), psrc + (rect->src_x * PS_ ## _src), rect->dst_w * PS_ ## _src); \
		psrc += rect->src_pitch; \
		pdst += rect->dst_pitch; \
	} }


static inline void memcpy_inline(void *dst, void *src, size_t count)
{
	size_t dw_count;
	size_t dw_size;
	size_t i;
	DWORD *ddst = (DWORD*)dst;
	DWORD *dsrc = (DWORD*)src;
	
	dw_count = count >> 2;
	dw_size = count & 0xFFFFFFFCUL;
	
	for(i = 0; i < dw_count; i++)
	{
		*ddst++ = *dsrc++;
	}
	
	for(i = dw_size; i < count; i++)
	{
		((BYTE*)dst)[i] = ((BYTE*)src)[i]; 
	}
}

#define GAMMA_R(_r) _r
#define GAMMA_G(_g) _g
#define GAMMA_B(_b) _b

CONV_FUNC_COPY_RGB(15);
CONV_FUNC(16, 15, RGB, fast);
CONV_FUNC(24, 15, RGB, fast);
//CONV_FUNC(32, 15, RGB, fast);

CONV_FUNC(15, 16, RGB, fast);
CONV_FUNC_COPY_RGB(16);
CONV_FUNC(24, 16, RGB, fast);
//CONV_FUNC(32, 16, RGB, fast);

CONV_FUNC24(15, RGB, fast);
CONV_FUNC24(16, RGB, fast);
CONV_FUNC_COPY_RGB(24);
//CONV_FUNC24(32, RGB, fast);

//CONV_FUNC(15, 32, RGB, fast);
//CONV_FUNC(16, 32, RGB, fast);
//CONV_FUNC(24, 32, RGB, fast);
CONV_FUNC_COPY_RGB(32);

/* 16 -> 32 */
static void vramcpy_16_32_RGB_faster(uint8_t *psrc, uint8_t *pdst, vramcpy_rect_t *rect)
{
	uint32_t y, x;
	psrc += rect->src_pitch * rect->src_y;
	pdst += rect->dst_pitch * rect->dst_y;

	for(y = 0; y < rect->dst_h; y++)
	{
		uint8_t *src_pix = psrc + (rect->src_x * 2);
		uint8_t *dst_pix = pdst + (rect->dst_x * 4);
		int i = rect->dst_w;
#if defined(__GNUC__) && defined(__SSE__)
		for(; i >= 4; i -= 4)
		{
			v4us v1 = *((v4us*)src_pix);
			v4ui v = __builtin_convertvector(v1, v4ui);
			v4ui r, g, b;

			r = v & 0x0000F800;
			g = v & 0x000007E0;
			b = v & 0x0000001F;
			r = r << 8;
			g = g << 5;
			b = b << 3;
			v = r | g | b;
			*((v4ui*)dst_pix) = v;
			src_pix += 2*4;
			dst_pix += 4*4;
		}
#endif
		for(; i > 0; i--)
		{
			uint32_t p = *((uint16_t*)src_pix);
			p =
				((p & 0x0000F800) << 8) |
				((p & 0x000007E0) << 5) |
				((p & 0x0000001F) << 3);
			*((uint32_t*)dst_pix) = p;

			src_pix += 2;
			dst_pix += 4;
		}

		psrc += rect->src_pitch;
		pdst += rect->dst_pitch;
	}
}

/* 15 -> 32 */
static void vramcpy_15_32_RGB_faster(uint8_t *psrc, uint8_t *pdst, vramcpy_rect_t *rect)
{
	uint32_t y, x;
	psrc += rect->src_pitch * rect->src_y;
	pdst += rect->dst_pitch * rect->dst_y;

	for(y = 0; y < rect->dst_h; y++)
	{
		uint8_t *src_pix = psrc + (rect->src_x * 2);
		uint8_t *dst_pix = pdst + (rect->dst_x * 4);
		int i = rect->dst_w;
#if defined(__GNUC__) && defined(__SSE__)
		for(; i >= 4; i -= 4)
		{
			v4us v1 = *((v4us*)src_pix);
			v4ui v = __builtin_convertvector(v1, v4ui);
			v4ui r, g, b;

			r = v & 0x00007C00;
			g = v & 0x000003E0;
			b = v & 0x0000001F;
			r = r << 9;
			g = g << 6;
			b = b << 3;
			v = r | g | b;
			*((v4ui*)dst_pix) = v;
			src_pix += 2*4;
			dst_pix += 4*4;
		}
#endif
		for(; i > 0; i--)
		{
			uint32_t p = *((uint16_t*)src_pix);
			p =
				((p & 0x00007C00) << 9) |
				((p & 0x000003E0) << 6) |
				((p & 0x0000001F) << 3);
			*((uint32_t*)dst_pix) = p;

			src_pix += 2;
			dst_pix += 4;
		}

		psrc += rect->src_pitch;
		pdst += rect->dst_pitch;
	}
}

/* 24 -> 32 */
static void vramcpy_24_32_RGB_faster(uint8_t *psrc, uint8_t *pdst, vramcpy_rect_t *rect)
{
	uint32_t y, x;
	psrc += rect->src_pitch * rect->src_y;
	pdst += rect->dst_pitch * rect->dst_y;

	for(y = 0; y < rect->dst_h; y++)
	{
		uint8_t *src_pix = psrc + (rect->src_x * 4);
		uint8_t *dst_pix = pdst + (rect->dst_x * 3);
		int i = rect->dst_w;
		for(; i > 0; i--)
		{
			*((uint32_t*)(dst_pix)) = (*((uint32_t*)src_pix)) & 0xFFFFFF;
			src_pix += 3;
			dst_pix += 4;
		}
		psrc += rect->src_pitch;
		pdst += rect->dst_pitch;
	}
}

/* 32 -> 16 */
static void vramcpy_32_16_RGB_faster(uint8_t *psrc, uint8_t *pdst, vramcpy_rect_t *rect)
{
	uint32_t y, x;
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
			uint32_t p = *((uint32_t*)src_pix);
			p = ((p & 0x00F80000) >> 8) | \
			    ((p & 0x0000FC00) >> 5) | \
			    ((p & 0x000000F8) >> 3);
			*((uint16_t*)dst_pix) = (uint16_t)p;
				
			src_pix += 4;
			dst_pix += 2;
		}

		psrc += rect->src_pitch;
		pdst += rect->dst_pitch;
	}
}

/* 32 -> 15 */
static void vramcpy_32_15_RGB_faster(uint8_t *psrc, uint8_t *pdst, vramcpy_rect_t *rect)
{
	uint32_t y, x;
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
			g = v & 0x0000F800;
			b = v & 0x000000F8;
			r = r >> 9;
			g = g >> 6;
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
			uint32_t p = *((uint32_t*)src_pix);
			p = ((p & 0x00F80000) >> 9) | \
			    ((p & 0x0000F800) >> 6) | \
			    ((p & 0x000000F8) >> 3);
			*((uint16_t*)dst_pix) = (uint16_t)p;
				
			src_pix += 4;
			dst_pix += 2;
		}

		psrc += rect->src_pitch;
		pdst += rect->dst_pitch;
	}
}

/* 32 -> 24 */
static void vramcpy_32_24_RGB_faster(uint8_t *psrc, uint8_t *pdst, vramcpy_rect_t *rect)
{
	uint32_t y, x;
	psrc += rect->src_pitch * rect->src_y;
	pdst += rect->dst_pitch * rect->dst_y;

	for(y = 0; y < rect->dst_h; y++)
	{
		uint8_t *src_pix = psrc + (rect->src_x * 4);
		uint8_t *dst_pix = pdst + (rect->dst_x * 3);
		int i = rect->dst_w;
		for(; i >= 4; i -= 4)
		{
			uint32_t p1 = *((uint32_t*)src_pix)      & 0x00FFFFFF;
			uint32_t p2 = *((uint32_t*)(src_pix+4))  & 0x00FFFFFF;
			uint32_t p3 = *((uint32_t*)(src_pix+8))  & 0x00FFFFFF;
			uint32_t p4 = *((uint32_t*)(src_pix+12)) & 0x00FFFFFF;

			p1 = p1 | (p2 << 24);
			p2 = (p2 >> 8) | (p3 << 16);
			p3 = (p3 >> 16) | (p4 << 8); 

			*((uint32_t*)(dst_pix  )) = p1;
			*((uint32_t*)(dst_pix+4)) = p2;
			*((uint32_t*)(dst_pix+8)) = p3;

			src_pix += 4*4;
			dst_pix += 3*4;
		}
		for(; i > 0; i--)
		{
			uint32_t px = *((uint32_t*)src_pix);
			*dst_pix      = (px >> 16) & 0xFF;
			*(dst_pix+1) =  (px >> 8)  & 0xFF;
			*(dst_pix+2) =  px & 0xFF;
			dst_pix += 3;
		}

		psrc += rect->src_pitch;
		pdst += rect->dst_pitch;
	}
}


#undef GAMMA_R
#undef GAMMA_G
#undef GAMMA_B

static void vramcpy_invalid(uint8_t *psrc, uint8_t *pdst, vramcpy_rect_t *rect)
{
#ifdef DEBUG
	printf("invalid conversion %d %d\n", rect->src_bpp, rect->dst_bpp);
#endif
}

#define CONV_TABLE_HASH(_src, _dst) ((((_src) & 0x78) << 1) | (((_dst) & 0x78) >> 3))

#define VRAMCPY_ENTRY(_tlb, _src, _dst, _colors, _postfix) _tlb[CONV_TABLE_HASH(_src, _dst)] = vramcpy_ ## _src ## _ ## _dst ## _ ## _colors ## _ ## _postfix


#define VRAM_TLB_SIZE 256

static vramcpy_f vram_tbl[VRAM_TLB_SIZE] = { NULL };
static vramcpy_f vram_tbl_gamma[VRAM_TLB_SIZE] = { NULL };

//#define vramcpy_pointsize_fast(bpp) (((uint32_t)(bpp)+7) >> 3)

static void vramcpy_init()
{
	size_t i;
	for(i = 0; i < VRAM_TLB_SIZE; i++)
	{
		vram_tbl[i] = vramcpy_invalid;
	}
	
	/* RGB -> RGB */
	VRAMCPY_ENTRY(vram_tbl, 15,15,RGB, copy);
	VRAMCPY_ENTRY(vram_tbl, 16,15,RGB, fast);
	VRAMCPY_ENTRY(vram_tbl, 24,15,RGB, fast);
	VRAMCPY_ENTRY(vram_tbl, 32,15,RGB, faster);

	VRAMCPY_ENTRY(vram_tbl, 15,16,RGB, fast);
	VRAMCPY_ENTRY(vram_tbl, 16,16,RGB, copy);
	VRAMCPY_ENTRY(vram_tbl, 24,16,RGB, fast);
	VRAMCPY_ENTRY(vram_tbl, 32,16,RGB, faster);

	VRAMCPY_ENTRY(vram_tbl, 15,24,RGB, fast);
	VRAMCPY_ENTRY(vram_tbl, 16,24,RGB, fast);
	VRAMCPY_ENTRY(vram_tbl, 24,24,RGB, copy);
	VRAMCPY_ENTRY(vram_tbl, 32,24,RGB, faster);

	VRAMCPY_ENTRY(vram_tbl, 15,32,RGB, faster);
	VRAMCPY_ENTRY(vram_tbl, 16,32,RGB, faster);
	VRAMCPY_ENTRY(vram_tbl, 24,32,RGB, faster);
	VRAMCPY_ENTRY(vram_tbl, 32,32,RGB, copy);
}

/* rectagle src to dst */
void vramcpy(void *dst, void *src, vramcpy_rect_t *rect)
{
	uint8_t *psrc = src;
	uint8_t *pdst = dst;
	uint32_t y, x;
	
	static uint32_t last_func = 0;
	if(last_func != CONV_TABLE_HASH(rect->src_bpp, rect->dst_bpp))
	{
#ifdef DEBUG
		printf("Using VRAM blit: %d -> %d\n", rect->src_bpp, rect->dst_bpp);
#endif
		last_func = CONV_TABLE_HASH(rect->src_bpp, rect->dst_bpp);
	}
	
	if(vram_tbl[0] == NULL)
	{
		vramcpy_init();
	}
		
	vram_tbl[CONV_TABLE_HASH(rect->src_bpp, rect->dst_bpp)](psrc, pdst, rect);
}

typedef struct _vram_gamma_table_t
{
	uint32_t r[256];
	uint32_t g[256];
	uint32_t b[256];
} vram_gamma_table_t;

static vram_gamma_table_t gamma_table;

#define GAMMA_R(_r) gamma_table.r[_r]
#define GAMMA_G(_r) gamma_table.g[_r]
#define GAMMA_B(_r) gamma_table.b[_r]

CONV_FUNC(15, 15, RGB, gamma);
CONV_FUNC(16, 15, RGB, gamma);
CONV_FUNC(24, 15, RGB, gamma);
CONV_FUNC(32, 15, RGB, gamma);

CONV_FUNC(15, 16, RGB, gamma);
CONV_FUNC(16, 16, RGB, gamma);
CONV_FUNC(24, 16, RGB, gamma);
CONV_FUNC(32, 16, RGB, gamma);

CONV_FUNC24(15, RGB, gamma);
CONV_FUNC24(16, RGB, gamma);
CONV_FUNC24(24, RGB, gamma);
CONV_FUNC24(32, RGB, gamma);

CONV_FUNC(15, 32, RGB, gamma);
CONV_FUNC(16, 32, RGB, gamma);
CONV_FUNC(24, 32, RGB, gamma);
CONV_FUNC(32, 32, RGB, gamma);

#undef GAMMA_R
#undef GAMMA_G
#undef GAMMA_B

static void vramcpy_gamma_init()
{
	uint32_t i;
	for(i = 0; i < VRAM_TLB_SIZE; i++)
	{
		vram_tbl_gamma[i] = vramcpy_invalid;
	}
	
	/* init gamma table too */
	for(i = 0; i < 256; i++)
	{
		gamma_table.r[i] = i;
		gamma_table.g[i] = i;
		gamma_table.b[i] = i;
	}
	
	/* RGB -> RGB */
	VRAMCPY_ENTRY(vram_tbl_gamma, 15,15,RGB, gamma);
	VRAMCPY_ENTRY(vram_tbl_gamma, 16,15,RGB, gamma);
	VRAMCPY_ENTRY(vram_tbl_gamma, 24,15,RGB, gamma);
	VRAMCPY_ENTRY(vram_tbl_gamma, 32,15,RGB, gamma);

	VRAMCPY_ENTRY(vram_tbl_gamma, 15,16,RGB, gamma);
	VRAMCPY_ENTRY(vram_tbl_gamma, 16,16,RGB, gamma);
	VRAMCPY_ENTRY(vram_tbl_gamma, 24,16,RGB, gamma);
	VRAMCPY_ENTRY(vram_tbl_gamma, 32,16,RGB, gamma);

	VRAMCPY_ENTRY(vram_tbl_gamma, 15,24,RGB, gamma);
	VRAMCPY_ENTRY(vram_tbl_gamma, 16,24,RGB, gamma);
	VRAMCPY_ENTRY(vram_tbl_gamma, 24,24,RGB, gamma);
	VRAMCPY_ENTRY(vram_tbl_gamma, 32,24,RGB, gamma);

	VRAMCPY_ENTRY(vram_tbl_gamma, 15,32,RGB, gamma);
	VRAMCPY_ENTRY(vram_tbl_gamma, 16,32,RGB, gamma);
	VRAMCPY_ENTRY(vram_tbl_gamma, 24,32,RGB, gamma);
	VRAMCPY_ENTRY(vram_tbl_gamma, 32,32,RGB, gamma);
}

/* rectagle src to dst */
void vramcpy_gamma(void *dst, void *src, vramcpy_rect_t *rect)
{
	uint8_t *psrc = src;
	uint8_t *pdst = dst;
	uint32_t y, x;

	if(vram_tbl_gamma[0] == NULL)
	{
		vramcpy_gamma_init();
	}
		
	vram_tbl_gamma[CONV_TABLE_HASH(rect->src_bpp, rect->dst_bpp)](psrc, pdst, rect);
}

void vramcpy_gamma_load(HDC hDC)
{
	if(vram_tbl_gamma[0] == NULL)
	{
		vramcpy_gamma_init();
	}
	
	int i;
	WORD gArray[3][256];
	if(GetDeviceGammaRamp(hDC, gArray))
	{
		for(i = 0; i < 256; i++)
		{
			gamma_table.r[i] = gArray[0][i] >> 8;
			gamma_table.g[i] = gArray[1][i] >> 8;
			gamma_table.b[i] = gArray[2][i] >> 8;
		}
	}
}
