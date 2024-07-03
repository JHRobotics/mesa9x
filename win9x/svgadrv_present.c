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


/* FRAMERATE_LIMIT - limits number of frames per second */
DEBUG_GET_ONCE_NUM_OPTION(framerate_limit, "FRAMERATE_LIMIT", 0)

DEBUG_GET_ONCE_BOOL_OPTION(blit_surf_to_screen_enabled, "SVGA_BLIT_SURF_TO_SCREEN", FALSE);
DEBUG_GET_ONCE_BOOL_OPTION(dma_need_reread, "SVGA_DMA_NEED_REREAD", TRUE);

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

/**
 * VirtualBox HACK - VirtualBox is not able to trace when is FB change with DMA
 * transfer to VRAM. So this refresh whoale screen be rewrite it with the same
 * contents.
 **/
static void refresh_fb(svga_inst_t *svga)
{
	uint32_t x, y;
	uint32_t h = svga->hda->width;
	uint32_t w = svga->hda->height;
	uint32_t tmp;

	volatile uint32_t *ptr = (volatile uint32_t*)svga->hda->vram_pm32;

	for(y = 0; y < h; y++)
	{
		for(x = 0; x < w; x++)
		{
			tmp = *ptr;
			*ptr = tmp;
			ptr++;
		}
	}
}

static BOOL SVGAScreenTarget(svga_inst_t *svga, uint32_t cid, uint32_t source_sid, int rx, int ry, int rw, int rh)
{
	if(svga->dx && (svga->hda->flags & FB_ACCEL_VMSVGA10_ST) != 0)
	{
		struct
		{
			SVGA3dCmdHeader header;
			SVGA3dCmdDXPresentBlt blit;
		} blit_cmd = {{SVGA_3D_CMD_DX_PRESENTBLT, sizeof(SVGA3dCmdDXPresentBlt)}};
	
		struct
		{
			SVGA3dCmdHeader header;
			SVGA3dCmdUpdateGBScreenTarget st;
		} stupdate = {{SVGA_3D_CMD_UPDATE_GB_SCREENTARGET, sizeof(SVGA3dCmdUpdateGBScreenTarget)}};
		
		/* guest -> host */
		struct
		{
			SVGA3dCmdHeader header;
			SVGA3dCmdUpdateGBSurface surf;   /* SVGA_3D_CMD_UPDATE_GB_SURFACE */
		} gbupdate = {{SVGA_3D_CMD_UPDATE_GB_SURFACE, sizeof(SVGA3dCmdUpdateGBSurface)}, {1}};
		
		/* guest <- host */
		struct
		{
			SVGA3dCmdHeader header;
			SVGA3dCmdReadbackGBSurface surf;   /* SVGA_3D_CMD_READBACK_GB_SURFACE */
		} gbreread = {{SVGA_3D_CMD_READBACK_GB_SURFACE, sizeof(SVGA3dCmdReadbackGBSurface)}, {1}};
		
		blit_cmd.blit.srcSid = source_sid;
		blit_cmd.blit.srcSubResource = 0;
		blit_cmd.blit.dstSid = 1;
		blit_cmd.blit.destSubResource = 0;
		blit_cmd.blit.boxSrc.x = 0;
		blit_cmd.blit.boxSrc.y = 0;
		blit_cmd.blit.boxSrc.z = 0;
		blit_cmd.blit.boxSrc.w = rw;
		blit_cmd.blit.boxSrc.h = rh;
		blit_cmd.blit.boxSrc.d = 1;
		blit_cmd.blit.boxDest.x = rx;
		blit_cmd.blit.boxDest.y = ry;
		blit_cmd.blit.boxDest.z = 0;
		blit_cmd.blit.boxDest.w = rw;
		blit_cmd.blit.boxDest.h = rh;
		blit_cmd.blit.boxDest.d = 1;
		blit_cmd.blit.mode = 0;
		
		stupdate.st.stid = 0;
   	stupdate.st.rect.x = 0;
   	stupdate.st.rect.y = 0;
   	stupdate.st.rect.w = svga->hda->width;
   	stupdate.st.rect.h = svga->hda->height;

		if(svga->hda->flags & FB_MOUSE_NO_BLIT) /* no mouse, direct render */
		{
			SVGAStart(svga);
		  SVGAPush(svga, &blit_cmd, sizeof(blit_cmd));
		  SVGAPush(svga, &stupdate, sizeof(stupdate));
		  SVGAFinish(svga, SVGA_CB_FLAG_DX_CONTEXT | SVGA_CB_PRESENT_ASYNC | SVGA_CB_PRESENT_GPU, cid);
		}
		else
		{
			FBHDA_access_begin(0);
			SVGAPush(svga, &gbupdate, sizeof(gbupdate)); /* copy vram from host to guest */
			SVGAPush(svga, &blit_cmd, sizeof(blit_cmd)); /* present */
			SVGAPush(svga, &gbreread, sizeof(gbreread)); /* read result back to framebuffer */
			SVGAFinish(svga, SVGA_CB_SYNC | SVGA_CB_FLAG_DX_CONTEXT, cid);
			FBHDA_access_end(0);
		}
		
		return TRUE;
	}
	
	return FALSE;
}

/**
 * Present render to screen/window using direct vram access if its possible.
 * If not calls SVGAPresentWindow.
 *
 **/
void SVGAPresent(svga_inst_t *svga, HDC hDC, uint32_t cid, uint32_t sid)
{
	assert(svga);

	uint32_t bpp = svga->hda->bpp;
  SVGA_DB_surface_t *sinfo = SVGASurfaceGet(svga, sid);

  PIXELFORMATDESCRIPTOR pfd;

  if(sinfo == NULL)
  {
   	return;
  }

  frame_wait(svga);

  if(sinfo->bpp == 8 || bpp == 8)
  {
		SVGAPresentWindow(svga, hDC, cid, sid);
		return;
  }

	RECT wrectc = {0, 0, 0, 0};
	const HWND hwnd = WindowFromDC(hDC);

	if(!hwnd)
	{
		return;
	}

	if(!GetClientRect(hwnd, &wrectc)) /*  */
	{
		return;
	}

	POINT p1 = {wrectc.left, wrectc.top};
	POINT p2 = {wrectc.right, wrectc.bottom};
	ClientToScreen(hwnd, &p1);
	ClientToScreen(hwnd, &p2);

	if(!vramcpy_direct_rendering(hDC))
	{
		if(!vramcpy_top_window(hwnd))
		{
			SVGAPresentWindow(svga, hDC, cid, sid);
			return;
		}
	}

  int render_top  = p1.y;
  int render_left = p1.x;
	int render_width  = p2.x - p1.x;
	int render_height = p2.y - p1.y;

	if(render_width * render_height == 0)
	{
		/* Nothing to see here. Please disperse. */
		return;
	}
	
	if(SVGAScreenTarget(svga, cid, sid, render_left, render_top, render_width, render_height))
	{
		return;
	}
	
	/*
	 * quick way: surface and screen must have same color depth for HW present
	 */
	if((debug_get_option_blit_surf_to_screen_enabled() || svga->dx) &&
		/*(bpp == sinfo->bpp || svga->dx) &&*/
		bpp == 32) /* JH: it seems to work correctly only in 32 bits! */
	{
#pragma pack(push)
#pragma pack(1)
		struct
		{
			SVGA3dCmdHeader header;
			SVGA3dCmdBlitSurfaceToScreen blit;
		} command = {{SVGA_3D_CMD_BLIT_SURFACE_TO_SCREEN, sizeof(SVGA3dCmdBlitSurfaceToScreen)}};
#pragma pack(pop)

		command.blit.srcImage.sid = sid;
		command.blit.srcImage.face = 0;
		command.blit.srcImage.mipmap = 0;
		command.blit.srcRect.left = 0;
		command.blit.srcRect.top = 0;
		command.blit.srcRect.right  = render_width;
		command.blit.srcRect.bottom = render_height;
		command.blit.destScreenId = 0; /* primary */
		command.blit.destRect.top    = render_top;
		command.blit.destRect.left   = render_left;
		command.blit.destRect.right  = render_left + render_width;
		command.blit.destRect.bottom = render_top + render_height;

		if(render_left < 0)
		{
			command.blit.srcRect.left = -render_left;
			command.blit.destRect.left = 0;
		}

		if(render_top < 0)
		{
			command.blit.srcRect.top = -render_top;
			command.blit.destRect.top = 0;
		}

		if(render_left + render_width > svga->hda->width)
		{
			command.blit.destRect.right = svga->hda->width;
			command.blit.srcRect.right = command.blit.destRect.right - render_left;
		}

		if(render_top + render_height > svga->hda->height)
		{
			command.blit.destRect.bottom = svga->hda->height;
			command.blit.srcRect.bottom = command.blit.destRect.bottom - render_top;
		}

		if(svga->hda->flags & FB_MOUSE_NO_BLIT) /* no mouse, direct render */
		{
		  if(svga->dx)
		  {
		  	SVGASend(svga, &command, sizeof(command), SVGA_CB_FLAG_DX_CONTEXT | SVGA_CB_PRESENT_ASYNC, cid);
		  }
		  else
		  {
		  	SVGASend(svga, &command, sizeof(command), SVGA_CB_PRESENT_ASYNC, 0);
		  }
		}
		else
		{
			FBHDA_access_begin(0);
		  if(svga->dx)
		  {
		  	SVGASend(svga, &command, sizeof(command), SVGA_CB_SYNC | SVGA_CB_FLAG_DX_CONTEXT, cid);
		  }
		  else
		  {
		  	SVGASend(svga, &command, sizeof(command), SVGA_CB_SYNC, 0);
		  }
			FBHDA_access_end(0);
		}
	}
	/*
	 * GPU10 way, sync the GMR and use vramcopy to convert and copy to FB
	 */
	else if(svga->dx)
	{
#pragma pack(push)
#pragma pack(1)
		struct
		{
			SVGA3dCmdHeader            header;
			SVGA3dCmdReadbackGBSurface surf;
		} cmd_readback = {{
			SVGA_3D_CMD_READBACK_GB_SURFACE,
			sizeof(SVGA3dCmdReadbackGBSurface)}};
#pragma pack(pop)
		vramcpy_rect_t vrect;
		void *gmr;

		cmd_readback.surf.sid = sid;

		SVGASend(svga, &cmd_readback, sizeof(cmd_readback), SVGA_CB_SYNC | SVGA_CB_FLAG_DX_CONTEXT, cid);

		gmr = SVGARegionGet(svga, sinfo->gmrId)->info.address;
		assert(gmr);
		
		vrect.dst_pitch   = svga->hda->pitch;
		vrect.dst_x       = render_left;
		vrect.dst_y       = render_top;
		vrect.dst_w       = render_width;
		vrect.dst_h       = render_height;
		vrect.dst_bpp     = bpp;
		vrect.src_pitch   = sinfo->width * vramcpy_pointsize(sinfo->bpp);
		vrect.src_x       = 0;
		vrect.src_y       = 0;
		vrect.src_bpp     = sinfo->bpp;
		
		if(sinfo->width < render_width)
			vrect.dst_w = sinfo->width;

		if(sinfo->height < render_height)
			vrect.dst_h = sinfo->height;

		if(svga->hda->flags & FB_MOUSE_NO_BLIT) /* no mouse, direct render */
		{
			vramcpy(svga->hda->vram_pm32, gmr, &vrect);

			if(bpp == 32)
			{
				struct
				{
					uint32_t          cmd;
					SVGAFifoCmdUpdate rect;
				} command_update = {SVGA_CMD_UPDATE};
				command_update.rect.x      = render_left;
				command_update.rect.y      = render_top;
				command_update.rect.width  = render_width;
				command_update.rect.height = render_height;

				SVGASend(svga, &command_update, sizeof(command_update), SVGA_CB_PRESENT_ASYNC, 0);
			}
		}
		else
		{
			FBHDA_access_begin(0);
			vramcpy(svga->hda->vram_pm32, gmr, &vrect);
			FBHDA_access_end(0);	
		}
	}
	/*
	 * harder way: we'll need render surface to some GMR region and copy to frame buffer manualy
	 */
	else
	{
#pragma pack(push)
#pragma pack(1)
		struct
		{
			SVGA3dCmdHeader           header;
			SVGA3dCmdSurfaceDMA       dma;
			SVGA3dCopyBox             box;
			SVGA3dCmdSurfaceDMASuffix suffix;
		} command_dma = {{
			SVGA_3D_CMD_SURFACE_DMA,
			sizeof(SVGA3dCmdSurfaceDMA) + sizeof(SVGA3dCopyBox) + sizeof(SVGA3dCmdSurfaceDMASuffix)}};
		struct
		{
			SVGA3dCmdHeader            header;
			SVGA3dCmdSurfaceStretchBlt blit;
		} command_blit = {{
			SVGA_3D_CMD_SURFACE_STRETCHBLT,
			sizeof(SVGA3dCmdSurfaceStretchBlt)
		}};
		struct
		{
			uint32_t          cmd;
			SVGAFifoCmdUpdate rect;
		} command_update = {SVGA_CMD_UPDATE};
#pragma pack(pop)

    if(!set_fb_blitsid(svga, render_width, render_height, bpp))
    {
    	return;
    }

    command_blit.blit.src.sid    = sid;
    command_blit.blit.src.face   = 0;
    command_blit.blit.src.mipmap = 0;

    command_blit.blit.dest.sid    = svga->blitsid;
    command_blit.blit.dest.face   = 0;
    command_blit.blit.dest.mipmap = 0;

		command_blit.blit.boxSrc.x = 0;
		command_blit.blit.boxSrc.y = 0;
		command_blit.blit.boxSrc.z = 0;
		command_blit.blit.boxSrc.w = sinfo->width;
		command_blit.blit.boxSrc.h = sinfo->height;
		command_blit.blit.boxSrc.d = 1;

		command_blit.blit.boxDest = command_blit.blit.boxSrc;
		command_blit.blit.mode    = SVGA3D_STRETCH_BLT_LINEAR;

		if(bpp == sinfo->bpp)
		{
			command_dma.dma.host.sid        = sid;
		}
		else
		{
			command_dma.dma.host.sid        = svga->blitsid;
		}

		command_dma.dma.guest.ptr.gmrId  = SVGA_GMR_FRAMEBUFFER;
		command_dma.dma.guest.ptr.offset = 0;
		command_dma.dma.guest.pitch      = svga->hda->pitch;
		command_dma.dma.host.face        = 0;
		command_dma.dma.host.mipmap      = 0;
		command_dma.dma.transfer         = SVGA3D_READ_HOST_VRAM;
		command_dma.box.x = 0;
		command_dma.box.y = 0;
		command_dma.box.z = 0;
		command_dma.box.w = render_width;
		command_dma.box.h = render_height;
		command_dma.box.d = 1;
		command_dma.box.srcx = render_left;
		command_dma.box.srcy = render_top;
		command_dma.box.srcz = 0;

		if(render_left < 0)
		{
			command_dma.box.x = -render_left;
			command_dma.box.srcx = 0;
		}

		if(render_top < 0)
		{
			command_dma.box.y = -render_top;
			command_dma.box.srcy = 0;
		}

		if(render_left + render_width > svga->hda->width)
		{
			command_dma.box.w = svga->hda->width - render_left;
		}

		if(render_top + render_height > svga->hda->height)
		{
			command_dma.box.h = svga->hda->height - render_top;
		}

		command_dma.suffix.suffixSize    = sizeof(SVGA3dCmdSurfaceDMASuffix);
		command_dma.suffix.maximumOffset = svga->hda->stride;
		command_dma.suffix.flags.discard         = 0;
		command_dma.suffix.flags.unsynchronized  = 0;
		command_dma.suffix.flags.reserved        = 0;

		command_update.rect.x      = render_left;
		command_update.rect.y      = render_top;
		command_update.rect.width  = render_width;
		command_update.rect.height = render_height;

		/*
		uint32_t fence = SVGAFenceInsertCB(svga);
		uint32_t fence_cmd[2] = {SVGA_CMD_FENCE, fence};
		*/

		SVGAStart(svga);
		if(bpp != sinfo->bpp)
		{
			SVGAPush(svga, &command_blit, sizeof(command_blit));
		}
		SVGAPush(svga, &command_dma, sizeof(command_dma));

		BOOL need_reread = debug_get_option_dma_need_reread() && bpp != 32;

		if((svga->hda->flags & FB_MOUSE_NO_BLIT) && !need_reread)
		{
			SVGAPush(svga, &command_update, sizeof(command_update));
			SVGAFinish(svga, SVGA_CB_PRESENT_ASYNC, 0);
		}
		else
		{
			FBHDA_access_begin(0);
			SVGAFinish(svga, SVGA_CB_SYNC, 0);

			if(need_reread)
			{
				refresh_fb(svga);
			}

			/* note: update command is not needed here, FBHDA_access_end do it automaticaly  */
			FBHDA_access_end(0);
		}
	}
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
  const size_t sps = vramcpy_pointsize(sbpp);

	if(sinfo->gmrId == 0) /* old way: copy surface to guest memory and display it */
	{
	  if(!set_fb_gmr(svga, sinfo->width, sinfo->height))
	  {
	  	debug_printf("SVGAPresentWindow: failed to set GMR!\n");
	  	FBHDA_access_end(0);
	  	return;
	  }

		debug_printf("SVGAPresentWindow: %d %d, format: %d\n", sbpp, sps, sinfo->format);

		command.dma.guest.ptr.gmrId  = svga->softblit_gmr_id;
		command.dma.guest.ptr.offset = 0;
		command.dma.guest.pitch      = sinfo->width * sps;
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
		command.suffix.maximumOffset = svga->softblit_gmr_size;
		command.suffix.flags.discard         = 0;
		command.suffix.flags.unsynchronized  = 0;
		command.suffix.flags.reserved        = 0;

		SVGASend(svga, &command, sizeof(command), SVGA_CB_SYNC, 0);

		assert(svga->softblit_gmr_ptr);

		gmr = svga->softblit_gmr_ptr;
	}
	else /* new way: sync GMR and copy buffer to window */
	{
		command_dx.surf.sid = sid;

		SVGASend(svga, &command_dx, sizeof(command_dx), SVGA_CB_SYNC | SVGA_CB_FLAG_DX_CONTEXT, cid);

		gmr = (void*)SVGARegionGet(svga, sinfo->gmrId)->info.address;

		assert(gmr);
	}

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
	
	StretchDIBits(hDC, 0, 0, sinfo->width, sinfo->height,
	                   0, 0, sinfo->width, sinfo->height,
		                 gmr, (BITMAPINFO *)&bmi, 0, SRCCOPY);
}

void SVGAPresentWinBlt(svga_inst_t *svga, HDC hDC, uint32_t cid, uint32_t sid)
{
	assert(svga);

	frame_wait(svga);

	SVGAPresentWindow(svga, hDC, cid, sid);
}
