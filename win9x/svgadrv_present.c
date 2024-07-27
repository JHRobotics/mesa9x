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

typedef struct _RenderRect
{
	int x;
	int y;
	int w;
	int h;
	int srcx;
	int srcy;
	int surf_w;
	int surf_h;
	int surf_bpp;
	int surf_pitch;
	BOOL need_conv;
} RenderRect;

static BOOL initRect(RenderRect *rr, POINT *leftTop, POINT *rightBottom)
{
  rr->x = leftTop->x;
  rr->y = leftTop->y;
	rr->w = rightBottom->x - leftTop->x;
	rr->h = rightBottom->y - leftTop->y;
	rr->srcx = 0;
	rr->srcy = 0;
	rr->surf_bpp = 0;
	rr->surf_pitch = 0;
	rr->need_conv = FALSE;
}

static BOOL adjustRect(RenderRect *rr, svga_inst_t *svga, SVGA_DB_surface_t *sinfo)
{
	if(rr->x < 0)
	{
		rr->srcx = -rr->x;
		rr->x = 0;
	}
	else
	{
		rr->srcx = 0;
	}
	
	if(rr->y < 0)
	{
		rr->srcy = -rr->y;
		rr->y = 0;
	}
	else
	{
		rr->srcy = 0;
	}
	
	if(rr->w > (sinfo->width - rr->srcx))
	{
		rr->w = sinfo->width - rr->srcx;
	}
	
	if(rr->h > sinfo->height - rr->srcy)
	{
		rr->w = sinfo->height - rr->srcy;
	}
	
	if((rr->x + rr->w) > svga->hda->width)
	{
		rr->w -= (rr->x + rr->w) - svga->hda->width;
	}
	
	if((rr->y + rr->h) > svga->hda->height)
	{
		rr->h -= (rr->y + rr->h) - svga->hda->height;
	}
	
	rr->surf_bpp   = sinfo->bpp;
	rr->surf_w     = sinfo->width;
	rr->surf_h     = sinfo->height;
	rr->surf_pitch = sinfo->width * vramcpy_pointsize(sinfo->bpp);
	
	if(rr->w > 0 && rr->h > 0)
	{
		rr->need_conv = TRUE;
		if(sinfo->bpp == svga->hda->bpp)
		{
			switch(sinfo->format)
			{
				case SVGA3D_R5G6B5:
				case SVGA3D_A8R8G8B8:
				case SVGA3D_X8R8G8B8:
				case SVGA3D_B5G6R5_UNORM:
				case SVGA3D_R8G8B8A8_UNORM:
					rr->need_conv = FALSE;
					break;
			}
		}
		
		return TRUE;
	}
	
	return FALSE;
}


static BOOL SVGAPresentScreenTarget(svga_inst_t *svga, uint32_t cid, uint32_t source_sid, RenderRect *rr)
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
		blit_cmd.blit.boxSrc.x = rr->srcx;
		blit_cmd.blit.boxSrc.y = rr->srcy;
		blit_cmd.blit.boxSrc.z = 0;
		blit_cmd.blit.boxSrc.w = rr->w;
		blit_cmd.blit.boxSrc.h = rr->h;
		blit_cmd.blit.boxSrc.d = 1;
		blit_cmd.blit.boxDest.x = rr->x;
		blit_cmd.blit.boxDest.y = rr->y;
		blit_cmd.blit.boxDest.z = 0;
		blit_cmd.blit.boxDest.w = rr->w;
		blit_cmd.blit.boxDest.h = rr->h;
		blit_cmd.blit.boxDest.d = 1;
		blit_cmd.blit.mode = 0;
		
		stupdate.st.stid = 0;
   	stupdate.st.rect.x = rr->x;
   	stupdate.st.rect.y = rr->y;
   	stupdate.st.rect.w = rr->w;
   	stupdate.st.rect.h = rr->h;

		if(svga->hda->flags & FB_MOUSE_NO_BLIT) /* no mouse, direct render */
		{
			SVGAStart(svga);
		  SVGAPush(svga, &blit_cmd, sizeof(blit_cmd));
		  SVGAPush(svga, &stupdate, sizeof(stupdate));
		  SVGAFinish(svga, SVGA_CB_FLAG_DX_CONTEXT | SVGA_CB_PRESENT | SVGA_CB_UPDATE | SVGA_CB_DIRTY_SURFACE, cid);
		}
		else
		{
			FBHDA_access_begin(0);
			SVGAPush(svga, &gbupdate, sizeof(gbupdate)); /* copy vram from host to guest */
			SVGAPush(svga, &blit_cmd, sizeof(blit_cmd)); /* present */
			SVGAPush(svga, &gbreread, sizeof(gbreread)); /* read result back to framebuffer */
			SVGAFinish(svga, SVGA_CB_SYNC | SVGA_CB_PRESENT | SVGA_CB_FLAG_DX_CONTEXT, cid);
			FBHDA_access_end(0);
		}
		
		return TRUE;
	}
	
	return FALSE;
}

static BOOL SVGAPresentLegacy(svga_inst_t *svga, uint32_t source_sid, RenderRect *rr)
{
	if(!svga->dx)
	{
		if(debug_get_option_dma_need_reread() && svga->hda->bpp != 32)
		{
			vramcpy_rect_t vrect;
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
			#pragma pack(pop)
			
			command_dma.dma.guest.ptr.gmrId  = SVGA_GMR_FRAMEBUFFER;
			command_dma.dma.guest.ptr.offset = svga->hda->stride;
			command_dma.dma.guest.pitch      = rr->surf_pitch;
			command_dma.dma.host.sid         = source_sid;
			command_dma.dma.host.face        = 0;
			command_dma.dma.host.mipmap      = 0;
			command_dma.dma.transfer         = SVGA3D_READ_HOST_VRAM;
	
			command_dma.box.x = 0;
			command_dma.box.y = 0;
			command_dma.box.z = 0;
			command_dma.box.w = rr->surf_w;
			command_dma.box.h = rr->surf_h;
			command_dma.box.d = 1;
			command_dma.box.srcx = 0;
			command_dma.box.srcy = 0;
			command_dma.box.srcz = 0;
	
			command_dma.suffix.suffixSize    = sizeof(SVGA3dCmdSurfaceDMASuffix);
			command_dma.suffix.maximumOffset = rr->surf_pitch * rr->surf_h;
			command_dma.suffix.flags.discard         = 0;
			command_dma.suffix.flags.unsynchronized  = 0;
			command_dma.suffix.flags.reserved        = 0;

			SVGASend(svga, &command_dma, sizeof(command_dma), 0, 0);
			
			vrect.dst_pitch   = svga->hda->pitch;
			vrect.dst_x       = rr->x;
			vrect.dst_y       = rr->y;
			vrect.dst_w       = rr->w;
			vrect.dst_h       = rr->h;
			vrect.dst_bpp     = svga->hda->bpp;
			vrect.src_pitch   = rr->surf_pitch;
			vrect.src_x       = rr->srcx;
			vrect.src_y       = rr->srcy;
			vrect.src_bpp     = rr->surf_bpp;
			
			FBHDA_access_begin(0);
			SVGAWaitAll(svga);
			vramcpy(svga->hda->vram_pm32, ((uint8_t*)svga->hda->vram_pm32) + svga->hda->stride, &vrect);
			FBHDA_access_end(0);
		}
		else
		{
			#pragma pack(push)
			#pragma pack(1)
				struct
				{
					SVGA3dCmdHeader           header;
					SVGA3dCmdPresent          present;
					SVGA3dCopyRect            rect;
				} command_present = {{SVGA_3D_CMD_PRESENT, sizeof(SVGA3dCmdPresent) + sizeof(SVGA3dCopyRect)}};
				struct
				{
					SVGA3dCmdHeader           header;
					SVGA3dRect                rect;
				} command_present_readback = {{SVGA_3D_CMD_PRESENT_READBACK, sizeof(SVGA3dRect)}};
				struct
				{
					SVGA3dCmdHeader            header;
					SVGA3dCmdSurfaceStretchBlt blit;
				} command_blit = {{
					SVGA_3D_CMD_SURFACE_STRETCHBLT,
					sizeof(SVGA3dCmdSurfaceStretchBlt)
				}};
			#pragma pack(pop)
			
			command_present.rect.x = rr->x;
			command_present.rect.y = rr->y;
			command_present.rect.w = rr->w;
			command_present.rect.h = rr->h;
			command_present.rect.srcx = rr->srcx;
			command_present.rect.srcy = rr->srcy;
			
			command_present_readback.rect.x = rr->x;
			command_present_readback.rect.y = rr->y;
			command_present_readback.rect.w = rr->w;
			command_present_readback.rect.h = rr->h;
			
			if(rr->need_conv)
			{
				if(!set_fb_blitsid(svga, rr->surf_w, rr->surf_h, svga->hda->bpp))
				{
					return FALSE;
				}
				
		    command_blit.blit.src.sid    = source_sid;
		    command_blit.blit.src.face   = 0;
		    command_blit.blit.src.mipmap = 0;
		
		    command_blit.blit.dest.sid    = svga->blitsid;
		    command_blit.blit.dest.face   = 0;
		    command_blit.blit.dest.mipmap = 0;
		
				command_blit.blit.boxSrc.x = 0;
				command_blit.blit.boxSrc.y = 0;
				command_blit.blit.boxSrc.z = 0;
				command_blit.blit.boxSrc.w = rr->surf_w;
				command_blit.blit.boxSrc.h = rr->surf_h;
				command_blit.blit.boxSrc.d = 1;
		
				command_blit.blit.boxDest.x = 0;
				command_blit.blit.boxDest.y = 0;
				command_blit.blit.boxDest.z = 0;
				command_blit.blit.boxDest.w = rr->surf_w;
				command_blit.blit.boxDest.h = rr->surf_h;
				command_blit.blit.boxDest.d = 1;
		
				command_blit.blit.mode    = SVGA3D_STRETCH_BLT_LINEAR;
				
				command_present.present.sid = svga->blitsid;
			}
			else
			{
				command_present.present.sid = source_sid;
			}
	
			if(svga->hda->flags & FB_MOUSE_NO_BLIT) /* no mouse, direct render */
			{
				SVGAStart(svga);
				if(rr->need_conv)
					SVGAPush(svga, &command_blit, sizeof(command_blit));
					
				SVGAPush(svga, &command_present, sizeof(command_present));
				SVGAPush(svga, &command_present_readback, sizeof(command_present_readback));
				SVGAFinish(svga, SVGA_CB_PRESENT | SVGA_CB_UPDATE, 0);
			}
			else
			{
				FBHDA_access_begin(0);
				SVGAStart(svga);
				if(rr->need_conv)
					SVGAPush(svga, &command_blit, sizeof(command_blit));
				
				SVGAPush(svga, &command_present, sizeof(command_present));
				SVGAPush(svga, &command_present_readback, sizeof(command_present_readback));
				SVGAFinish(svga, SVGA_CB_SYNC | SVGA_CB_PRESENT, 0);
				FBHDA_access_end(0);
			}
		} // !reread
		
		return TRUE;
	}
	
	return FALSE;
}

static BOOL SVGAPresentDX(svga_inst_t *svga, uint32_t cid, uint32_t source_sid, RenderRect *rr, SVGA_DB_surface_t *sinfo)
{
	if((debug_get_option_blit_surf_to_screen_enabled() && svga->hda->bpp == 32))
	{
		// VirtualBox only!
#pragma pack(push)
#pragma pack(1)
			struct
			{
				SVGA3dCmdHeader header;
				SVGA3dCmdBlitSurfaceToScreen blit;
			} command_blit_scr = {{SVGA_3D_CMD_BLIT_SURFACE_TO_SCREEN, sizeof(SVGA3dCmdBlitSurfaceToScreen)}};
#pragma pack(pop)

			command_blit_scr.blit.srcImage.sid    = source_sid;
			command_blit_scr.blit.srcImage.face   = 0;
			command_blit_scr.blit.srcImage.mipmap = 0;
			command_blit_scr.blit.srcRect.left    = rr->srcx;
			command_blit_scr.blit.srcRect.top     = rr->srcy;
			command_blit_scr.blit.srcRect.right   = rr->srcx + rr->w;
			command_blit_scr.blit.srcRect.bottom  = rr->srcy + rr->h;
			command_blit_scr.blit.destScreenId    = 0; /* primary */
			command_blit_scr.blit.destRect.left   = rr->x;
			command_blit_scr.blit.destRect.top    = rr->y;
			command_blit_scr.blit.destRect.right  = rr->x + rr->w;
			command_blit_scr.blit.destRect.bottom = rr->y + rr->h;
	
			if(svga->hda->flags & FB_MOUSE_NO_BLIT) /* no mouse, direct render */
			{
			  SVGASend(svga, &command_blit_scr, sizeof(command_blit_scr), SVGA_CB_FLAG_DX_CONTEXT | SVGA_CB_PRESENT | SVGA_CB_UPDATE, cid);
			}
			else
			{
				FBHDA_access_begin(0);
			  SVGASend(svga, &command_blit_scr, sizeof(command_blit_scr), SVGA_CB_SYNC | SVGA_CB_FLAG_DX_CONTEXT | SVGA_CB_PRESENT, cid);
				FBHDA_access_end(0);
			}
			
		return TRUE;
	}
	else if(svga->dx)
	{
#pragma pack(push)
#pragma pack(1)
	struct
	{
		SVGA3dCmdHeader            header;
		SVGA3dCmdReadbackGBSurface surf;
	} command_readback = {{
		SVGA_3D_CMD_READBACK_GB_SURFACE,
		sizeof(SVGA3dCmdReadbackGBSurface)}};
	struct
	{
		uint32_t          cmd;
		SVGAFifoCmdUpdate rect;
	} command_update = {SVGA_CMD_UPDATE};
#pragma pack(pop)
		vramcpy_rect_t vrect;
		void *gmr;
	
		command_readback.surf.sid = source_sid;
		SVGASend(svga, &command_readback, sizeof(command_readback), SVGA_CB_PRESENT | SVGA_CB_SYNC | SVGA_CB_FLAG_DX_CONTEXT, cid);
		gmr = SVGARegionGet(svga, sinfo->gmrId)->info.address;
		
		assert(gmr);
		
		vrect.dst_pitch   = svga->hda->pitch;
		vrect.dst_x       = rr->x;
		vrect.dst_y       = rr->y;
		vrect.dst_w       = rr->w;
		vrect.dst_h       = rr->h;
		vrect.dst_bpp     = svga->hda->bpp;
		vrect.src_pitch   = rr->surf_pitch;
		vrect.src_x       = rr->srcx;
		vrect.src_y       = rr->srcy;
		vrect.src_bpp     = rr->surf_bpp;

		if(svga->hda->flags & FB_MOUSE_NO_BLIT) /* no mouse, direct render */
		{
			vramcpy(svga->hda->vram_pm32, gmr, &vrect);

			if(svga->hda->bpp == 32)
			{
				command_update.rect.x      = rr->x;
				command_update.rect.y      = rr->y;
				command_update.rect.width  = rr->w;
				command_update.rect.height = rr->h;
				SVGASend(svga, &command_update, sizeof(command_update), SVGA_CB_PRESENT | SVGA_CB_UPDATE, 0);
			}
		}
		else
		{
			FBHDA_access_begin(0);
			vramcpy(svga->hda->vram_pm32, gmr, &vrect);
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

  SVGA_DB_surface_t *sinfo = SVGASurfaceGet(svga, sid);
  
  if(sinfo == NULL)
  {
   	return;
  }
  
  const HWND hwnd = WindowFromDC(hDC);
  
  if(!hwnd)
		return;

  frame_wait(svga);

  if(sinfo->bpp <= 8 || svga->hda->bpp <= 8)
  {
		SVGAPresentWindow(svga, hDC, cid, sid);
		return;
  }

	if(!vramcpy_direct_rendering(hDC))
	{
		if(!vramcpy_top_window(hwnd))
		{
			SVGAPresentWindow(svga, hDC, cid, sid);
			return;
		}
	}

	/* get wwindows coordinates on screen */
	RECT wrectc = {0, 0, 0, 0};

	if(!GetClientRect(hwnd, &wrectc))
		return;

	POINT p1 = {wrectc.left, wrectc.top};
	POINT p2 = {wrectc.right, wrectc.bottom};
	ClientToScreen(hwnd, &p1);
	ClientToScreen(hwnd, &p2);
	
	RenderRect rr;
	initRect(&rr, &p1, &p2);

	if(!adjustRect(&rr, svga, sinfo))
	{
		/* Nothing to see here. Please disperse. */
		return;
	}
	
	if(SVGAPresentScreenTarget(svga, cid, sid, &rr))
	{
		return;
	}
	
	if(SVGAPresentDX(svga, cid, sid, &rr, sinfo))
	{
		return;
	}
	
	if(SVGAPresentLegacy(svga, sid, &rr))
	{
		return;
	}

	/* failback */
	SVGAPresentWindow(svga, hDC, cid, sid);
}

/**
 * Present render to screen/window using system StretchDIBits
 *
 **/
void SVGAPresentWindow(svga_inst_t *svga, HDC hDC, uint32_t cid, uint32_t sid)
{
	SVGAWaitAll(svga);
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
	struct
	{
		SVGA3dCmdHeader           header;
		SVGA3dCmdPresent          present;
		SVGA3dCopyRect            rect;
	} command_present = {{SVGA_3D_CMD_PRESENT, sizeof(SVGA3dCmdPresent) + sizeof(SVGA3dCopyRect)}};
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
		/* newer variant using VRAM */
		command.dma.guest.ptr.gmrId  = SVGA_GMR_FRAMEBUFFER;
		command.dma.guest.ptr.offset = svga->hda->stride;
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
		command.suffix.maximumOffset = sinfo->height * sinfo->width * sps;
		command.suffix.flags.discard         = 0;
		command.suffix.flags.unsynchronized  = 0;
		command.suffix.flags.reserved        = 0;

		SVGASend(svga, &command, sizeof(command), SVGA_CB_SYNC, 0);

		gmr = ((uint8_t*)svga->hda->vram_pm32) + svga->hda->stride;
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
