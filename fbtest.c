/******************************************************************************
 * Copyright (c) 2023-2024 Jaroslav Hensl                                     *
 *                                                                            *
 * Permission is hereby granted, free of charge, to any person                *
 * obtaining a copy of this software and associated documentation             *
 * files (the "Software"), to deal in the Software without                    *
 * restriction, including without limitation the rights to use,               *
 * copy, modify, merge, publish, distribute, sublicense, and/or sell          *
 * copies of the Software, and to permit persons to whom the                  *
 * Software is furnished to do so, subject to the following                   *
 * conditions:                                                                *
 *                                                                            *
 * The above copyright notice and this permission notice shall be             *
 * included in all copies or substantial portions of the Software.            *
 *                                                                            *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,            *
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES            *
 * OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND                   *
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT                *
 * HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,               *
 * WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING               *
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR              *
 * OTHER DEALINGS IN THE SOFTWARE.                                            *
 *                                                                            *
*******************************************************************************/
#include <windows.h>
#include <wingdi.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <ctype.h>

#include "win9x/3d_accel.h"

/*** standard function, see more on win9x/3d_accel.c ***/

FBHDA_t *hda = NULL;
HANDLE hda_vxd = INVALID_HANDLE_VALUE;

void FBHDA_load()
{
	HWND hDesktop = GetDesktopWindow();
	HDC hdc = GetDC(hDesktop);
	char strbuf[PATH_MAX];

	if(ExtEscape(hdc, OP_FBHDA_SETUP, 0, NULL, sizeof(FBHDA_t *), (LPVOID)&hda))
	{
		if(hda != NULL)
		{
			if(hda->cb == sizeof(FBHDA_t) && hda->version == API_3DACCEL_VER)
			{
				strcpy(strbuf, "\\\\.\\");
				strcat(strbuf, hda->vxdname);
				
				hda_vxd = CreateFileA(strbuf, 0, 0, 0, CREATE_NEW, FILE_FLAG_DELETE_ON_CLOSE, 0);
				
				return;
			}
			else
			{
				printf("error FBHDA_load(): wrong version, sizeof(FBHDA_t) = %ld vs %d, API_3DACCEL_VER = %ld vs %d\n",
					hda->cb, sizeof(FBHDA_t), hda->version, API_3DACCEL_VER
				);
			}
		}
		else
		{
			printf("error FBHDA_load(): NULL from ExtEscape(..., OP_FBHDA_SETUP, ...)");
		}
	}
	else
	{
			printf("error FBHDA_load(): ExtEscape failed\n");
	}
	
	hda = NULL;
}

void FBHDA_free()
{
	if(hda_vxd != INVALID_HANDLE_VALUE)
	{
		CloseHandle(hda_vxd);
	}
}

FBHDA_t *FBHDA_setup()
{
	return hda;
}

BOOL FBHDA_swap(DWORD offset)
{
	DWORD result = 0;
	
	if(hda_vxd == INVALID_HANDLE_VALUE) return FALSE;
	
	if(DeviceIoControl(hda_vxd, OP_FBHDA_SWAP,
		&offset, sizeof(DWORD), &result, sizeof(DWORD),
		NULL, NULL))
	{
		return result == 0 ? FALSE : TRUE;
	}
	
	return FALSE;
}

/*** dump ***/
typedef struct _flags_set_dump_t
{
	DWORD flag;
	const char *name;
} flags_set_dump_t;

#define FLAGS_SET(_f) {_f, #_f}

flags_set_dump_t flags_set_dump[] = {
	FLAGS_SET(FB_SUPPORT_FLIPING),
	FLAGS_SET(FB_ACCEL_VIRGE),
	FLAGS_SET(FB_ACCEL_CHROMIUM),
	FLAGS_SET(FB_ACCEL_QEMU3DFX),
	FLAGS_SET(FB_ACCEL_VMSVGA),
	FLAGS_SET(FB_ACCEL_VMSVGA3D),
	FLAGS_SET(FB_ACCEL_VMSVGA10),
	FLAGS_SET(FB_MOUSE_NO_BLIT),
	FLAGS_SET(FB_FORCE_SOFTWARE),
	FLAGS_SET(FB_ACCEL_VMSVGA10_ST),
	{0, NULL}
};

static void FBHDA_dump(FBHDA_t *ptr)
{
	printf("dump of FBHDA\n====\n");
	printf("%-16s\t%lu\n", "cb:",      ptr->cb);
	printf("%-16s\t%lu\n", "flags:",   ptr->flags);
	printf("%-16s\t%lu\n", "version:", ptr->version);
	printf("%-16s\t%lu\n", "width:",   ptr->width);
	printf("%-16s\t%lu\n", "height:",  ptr->height);
	printf("%-16s\t%lu\n", "bpp:",     ptr->bpp);
	
	printf("%-16s\t%lu\n",     "pitch:", ptr->pitch);
	printf("%-16s\t%lu\n",    "surface", ptr->surface);
	printf("%-16s\t%lu\n",    "stride:", ptr->stride);
	printf("%-16s\t%p\n",  "vram_pm32:", ptr->vram_pm32);
	printf("%-16s\t%lu\n", "vram_size:", ptr->vram_size);
	printf("%-16s\t%s\n",    "vxdname:", ptr->vxdname);
	printf("====\n");
	printf("flags sets:\n");
	
	flags_set_dump_t *s = &flags_set_dump[0];
	
	while(s->name != NULL)
	{
		if(ptr->flags & s->flag)
		{
			printf("%s\n", s->name);
		}
		s++;
	}
	printf("====\n");
}

/*** few drawing functions  ***/
static void FBHDA_draw_px(FBHDA_t *ptr, UINT x, UINT y, DWORD px, DWORD surface)
{
	uint8_t *vram = (uint8_t *)ptr->vram_pm32;
	vram += ptr->surface;
	
	switch(ptr->bpp)
	{
		case 15:
		{
			uint16_t *vram_ptr = (uint16_t*)(vram + (ptr->pitch * y));
			uint8_t r = (px >> 16) & 0xFF;
			uint8_t g = (px >>  8) & 0xFF;
			uint8_t b =         px & 0xFF;
			
			vram_ptr[x] = ((r >> 3) << 10) | ((g >> 3) << 5) | (b >> 3);
			break;
		}
		case 16:
		{
			uint16_t *vram_ptr = (uint16_t*)(vram + (ptr->pitch * y));
			uint8_t r = (px >> 16) & 0xFF;
			uint8_t g = (px >>  8) & 0xFF;
			uint8_t b =         px & 0xFF;
			
			vram_ptr[x] = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3);
			break;
		}
		case 24:
		{
			uint8_t *vram_ptr = (vram + (ptr->pitch * y));
			vram_ptr[x*3 + 0] =         px & 0xFF;
			vram_ptr[x*3 + 1] = (px >>  8) & 0xFF;
			vram_ptr[x*3 + 2] = (px >> 16) & 0xFF;
			break;
		}
		case 32:
		{
			uint32_t *vram_ptr = (uint32_t*)(vram + (ptr->pitch * y));
			vram_ptr[x] = px;
			break;
		}
	}
}

static void FBHDA_draw_border(FBHDA_t *info, DWORD color32, UINT size, DWORD surface)
{
	UINT x, y;

	for(y = 0; y < size; y++)
	{
		for(x = 0; x < info->width; x++)
		{
			FBHDA_draw_px(info, x, y, color32, surface);
		}
	}
	
	for(y = size; y < info->height - size; y++)
	{
		for(x = 0; x < size; x++)
		{
			FBHDA_draw_px(info, x, y, color32, surface);
		}
		
		for(x = info->width - size; x < info->width; x++)
		{
			FBHDA_draw_px(info, x, y, color32, surface);
		}
	}
	
	for(y = info->height - size; y < info->height; y++)
	{
		for(x = 0; x < info->width; x++)
		{
			FBHDA_draw_px(info, x, y, color32, surface);
		}
	}
}

static void FBHDA_draw_fill(FBHDA_t *info, DWORD color32, DWORD surface)
{
	UINT x, y;
	
	for(y = 0; y < info->height; y++)
	{
		for(x = 0; x < info->width; x++)
		{
			FBHDA_draw_px(info, x, y, color32, surface);
		}
	}
}

BOOL FBHDA_flip(FBHDA_t *info, DWORD surface)
{
	DWORD offset = info->stride*surface;
	
	if(info->flags & FB_SUPPORT_FLIPING)
	{
		return FBHDA_swap(offset);
	}
	
	return FALSE;
}

BOOL validate(FBHDA_t *info)
{
	if(info == NULL)
	{
		printf("FBHDA is not supported!\n");
		return FALSE;
	}
	
	return TRUE;
}

/*** control ***/
static BOOL menu(const char *inp_buff, char item)
{
	const char *ptr = inp_buff;
	
	while(*ptr != '\0')
	{
		if(tolower(*ptr) == item)
		{
			return TRUE;
		}
		ptr++;
	}
	
	return FALSE;
}

void print_menu()
{
	printf("Commands:\n");
	printf("\tb: draw border\n");
	printf("\tf: fill screen\n");
	printf("\tg: fill green\n");
	printf("\tr: reset surface\n");
	printf("\ts: switch to surface 1\n");
	printf("\tw: fill surface 1\n");
	printf("\tq: draw border surface 1\n");
	printf("\tx: exit\n");
}

#define inbuf_size 128

int main(int argc, char **argv)
{
	int c;
	char inbuf[inbuf_size];
	
	FBHDA_load();
	
	FBHDA_t *ptr = FBHDA_setup();
	
	if(validate(ptr))
		FBHDA_dump(ptr);

	printf("Enter command: (m for menu)\n");	

	for(;;)
	{
		size_t sp = 0;
		for(;;)
		{
			c = getchar();
			if(c != '\n' && c != EOF)
			{
				inbuf[sp] = c;
			}
			else
			{
				inbuf[sp] = '\0';
				break;
			}
			
			if(++sp == inbuf_size-1)
			{
				inbuf[sp] = '\0';
				break;
			}
		}
		
		if(menu(inbuf, 'b'))
		{
			if(validate(ptr))
				FBHDA_draw_border(ptr, 0x00FF0000, 4, 0);
		}
		else if(menu(inbuf, 'f'))
		{
			if(validate(ptr))
				FBHDA_draw_fill(ptr, 0x000000FF, 0);
		}
		else if(menu(inbuf, 'g'))
		{
			if(validate(ptr))
				FBHDA_draw_fill(ptr, 0x0000FF00, 0);
		}
		else if(menu(inbuf, 's'))
		{
			if(validate(ptr))
				if(!FBHDA_flip(ptr, 1))
				{
					printf("fliping is not supported!\n");
				}
		}
		else if(menu(inbuf, 'r'))
		{
			if(validate(ptr))
				if(!FBHDA_flip(ptr, 0))
				{
					printf("fliping is not supported!\n");
				}
		}
		else if(menu(inbuf, 'q'))
		{
			if(validate(ptr))
				FBHDA_draw_border(ptr, 0x00FF0000, 4, 1);
		}
		else if(menu(inbuf, 'w'))
		{
			if(validate(ptr))
				FBHDA_draw_fill(ptr, 0x000000FF, 1);
		}
		else if(menu(inbuf, 'x'))
		{
			printf("exiting...\n");
			break;
		}
		else if(menu(inbuf, 'm') || menu(inbuf, 'h'))
		{
			print_menu();
		}
		else
		{
			printf("unknown command\n");
		}
	}

	FBHDA_free();

	return 0;
}
