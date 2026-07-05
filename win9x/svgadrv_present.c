/* here are function for prenset SVGA render to frame buffer or windows buffer */
#include <windows.h>
#include <stdint.h>
#include <stdio.h>

#define uint8  uint8_t
#define uint16 uint16_t
#define uint32 uint32_t
#define uint64 uint64_t

#include "wddm_screen.h"

#define SVGA
#include "3d_accel.h"
#include "svgadrv.h"
#include "vramcpy.h"

/* frame buffer */
#include "stw_framebuffer.h"

#include "svgadrv_cmds.h"

/* FRAMERATE_LIMIT - limits number of frames per second */
DEBUG_GET_ONCE_NUM_OPTION(framerate_limit, "FRAMERATE_LIMIT", 0)
DEBUG_GET_ONCE_BOOL_OPTION(svga_dma_fb, "SVGA_DMA_TO_FB", FALSE);

//DEBUG_GET_ONCE_BOOL_OPTION(blit_surf_to_screen_enabled, "SVGA_BLIT_SURF_TO_SCREEN", FALSE);
//DEBUG_GET_ONCE_BOOL_OPTION(dma_need_reread, "SVGA_DMA_NEED_REREAD", TRUE);

/* conversion between FILETIME and uint64_t */
typedef union _wintick_t {
	FILETIME ft;
	ULARGE_INTEGER u;
} wintick_t;

/**
 * Active waits (burns CPU) number of FILETIME ticks.
 * 1 tick = 100 ns
 *
 **/
static uint64_t asleep(uint64_t ftdelay)
{
	wintick_t start, act;

	GetSystemTimeAsFileTime(&(start.ft));

	do
	{
		GetSystemTimeAsFileTime(&(act.ft));
	} while(start.u.QuadPart+ftdelay >= act.u.QuadPart);

	return act.u.QuadPart;
}

/**
 * Wait for next frame - number of rames is in FRAMERATE_LIMIT enviroment option
 *
 **/
static void frame_wait(svga_inst_t *svga)
{
	wintick_t act;

	if(debug_get_option_framerate_limit() <= 0)
	{
		return;
	}

	uint64_t frame_time = 10000000ULL / debug_get_option_framerate_limit();

	GetSystemTimeAsFileTime(&(act.ft));

	uint64_t target = svga->lastframe.QuadPart + frame_time;

	if(target > act.u.QuadPart)
	{
		uint64_t delay =  target - act.u.QuadPart;
		if(delay > svga->delta)
		{
			delay -= svga->delta;

			act.u.QuadPart = asleep(delay);

			if(act.u.QuadPart > target)
			{
				svga->delta = act.u.QuadPart - target;
			}
		}
		else
		{
			svga->delta = (svga->delta / 10)*9;
		}

		svga->lastframe.QuadPart = target;
	}
	else
	{
		svga->lastframe.QuadPart = act.u.QuadPart;
	}
}


DWORD SVGA_pitch(DWORD width, DWORD bpp)
{
//	DWORD bp = (bpp + 7) / 8;
//	return (bp * width + 15) & 0xFFFFFFF0UL;
	DWORD bp = (bpp + 7) / 8;
	return (bp * width + (FBHDA_ROW_ALIGN-1)) & (~((DWORD)FBHDA_ROW_ALIGN-1));
}

/**
 * Present render to screen/window using direct vram access if its possible.
 * If not calls SVGAPresentWindow.
 *
 **/
void SVGAPresent(svga_inst_t *svga, HDC hDC, uint32_t cid, uint32_t sid)
{
	assert(svga);

  SVGA_DB_surface_t *sinfo = SVGASurfaceGet(svga, sid);
  
  if(sinfo == NULL)
  {
   	return;
  }
  
  const HWND hwnd = WindowFromDC(hDC);
  
  if(!hwnd)
		return;

  frame_wait(svga);
	SVGAPresentWindow(svga, hDC, cid, sid);
}

/**
 * Present render to screen/window using system StretchDIBits
 *
 **/
void SVGAPresentWindow(svga_inst_t *svga, HDC hDC, uint32_t cid, uint32_t sid)
{
//	SVGAWaitAll(svga);
#pragma pack(push)
#pragma pack(1)
	struct
	{
		SVGA3dCmdHeader           header;
		SVGA3dCmdSurfaceDMA       dma;
		SVGA3dCopyBox             box;
		SVGA3dCmdSurfaceDMASuffix suffix;
	} command = {{
		SVGA_3D_CMD_SURFACE_DMA,
		sizeof(SVGA3dCmdSurfaceDMA) + sizeof(SVGA3dCopyBox) + sizeof(SVGA3dCmdSurfaceDMASuffix)}};
	struct
	{
		SVGA3dCmdHeader            header;
		SVGA3dCmdReadbackGBSurface surf;
	} command_dx = {{
		SVGA_3D_CMD_READBACK_GB_SURFACE,
		sizeof(SVGA3dCmdReadbackGBSurface)}};
#pragma pack(pop)

	SVGA_DB_surface_t *sinfo = SVGASurfaceGet(svga, sid);
	void *gmr = NULL;

	if(sinfo == NULL || (sinfo->width * sinfo->height) == 0)
	{
		debug_printf("SVGAPresentWindow: null surface info!\n");
		/* Nothing to see here. Please disperse. */
		return;
	}

  const int sbpp = sinfo->bpp;
  
	if(sinfo->gmrId == 0) /* old way: copy surface to guest memory and display it */
	{
		if(set_fb_gmr(svga, sinfo->width, sinfo->height))
		{
			command.dma.guest.ptr.gmrId  = svga->softblit_gmr_id;
			command.dma.guest.ptr.offset = 0;
			command.dma.guest.pitch      = SVGA_pitch(sinfo->width, sbpp);
			command.dma.host.sid         = sid;
			command.dma.host.face        = 0;
			command.dma.host.mipmap      = 0;
			command.dma.transfer         = SVGA3D_READ_HOST_VRAM;

			command.box.x = 0;
			command.box.y = 0;
			command.box.z = 0;
			command.box.w = sinfo->width;
			command.box.h = sinfo->height;
			command.box.d = 1;
			command.box.srcx = 0;
			command.box.srcy = 0;
			command.box.srcz = 0;

			command.suffix.suffixSize    = sizeof(SVGA3dCmdSurfaceDMASuffix);
			command.suffix.maximumOffset = sinfo->height * SVGA_pitch(sinfo->width, sbpp);
			command.suffix.flags.discard         = 1;
			command.suffix.flags.unsynchronized  = 0;
			command.suffix.flags.reserved        = 0;

			SVGASend(svga, &command, sizeof(command), SVGA_CB_SYNC, 0);

			gmr = (void*)SVGARegionGet(svga, svga->softblit_gmr_id)->info.address;
		}
	}
	else /* new way: sync GMR and copy buffer to window */
	{
		command_dx.surf.sid = sid;

		SVGASend(svga, &command_dx, sizeof(command_dx), SVGA_CB_SYNC | SVGA_CB_FLAG_DX_CONTEXT, cid);

		gmr = (void*)SVGARegionGet(svga, sinfo->gmrId)->info.address;
	}
	
	assert(gmr);

	struct {
		BITMAPINFOHEADER bmiHeader;
		DWORD rmask;
		DWORD gmask;
		DWORD bmask;
	} bmi;
//	BITMAPINFO bmi;

	memset(&bmi, 0, sizeof(bmi));
	bmi.bmiHeader.biSize        = sizeof(BITMAPINFOHEADER);
	bmi.bmiHeader.biWidth       = sinfo->width;
	bmi.bmiHeader.biHeight      = -sinfo->height;
	bmi.bmiHeader.biPlanes      = 1;
	bmi.bmiHeader.biBitCount    = sbpp;
	bmi.bmiHeader.biCompression = BI_RGB;
	bmi.bmiHeader.biSizeImage   = 0;

	if(sbpp == 16)
	{
		bmi.bmiHeader.biCompression = BI_BITFIELDS;
		bmi.rmask = 0x0000F800;
		bmi.gmask = 0x000007E0;
		bmi.bmask = 0x0000001F;
	}
	else if(sbpp == 15)
	{
		bmi.bmiHeader.biCompression = BI_BITFIELDS;
		bmi.rmask = 0x00007C00;
		bmi.gmask = 0x000003E0;
		bmi.bmask = 0x0000001F;
	}
	else /*if(sbpp == 32) */
	{
		bmi.bmiHeader.biCompression = BI_BITFIELDS;
		bmi.rmask = 0x00FF0000;
		bmi.gmask = 0x0000FF00;
		bmi.bmask = 0x000000FF;
	}
	
	vramcpy_blit(hDC, (BITMAPINFO *)&bmi, gmr, sinfo->width, sinfo->height);
}

void SVGAPresentWinBlt(svga_inst_t *svga, HDC hDC, uint32_t cid, uint32_t sid)
{
	assert(svga);

	frame_wait(svga);

	SVGAPresentWindow(svga, hDC, cid, sid);
}
