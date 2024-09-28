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
DEBUG_GET_ONCE_BOOL_OPTION(mesa_sw_gamma, "MESA_SW_GAMMA_ENABLED", FALSE);
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
/*			switch(sinfo->format)
			{
				case SVGA3D_R5G6B5:
				case SVGA3D_A8R8G8B8:
				case SVGA3D_X8R8G8B8:
				case SVGA3D_B5G6R5_UNORM:
				case SVGA3D_R8G8B8A8_UNORM:
					rr->need_conv = FALSE;
					break;
			}*/
			rr->need_conv = FALSE;
		}
		
		return TRUE;
	}
	
	return FALSE;
}

DWORD SVGA_pitch(DWORD width, DWORD bpp)
{
	DWORD bp = (bpp + 7) / 8;
	return (bp * width + 15) & 0xFFFFFFF0UL;
}

#if 0
static void copy3ways_present(svga_inst_t *svga, uint32_t sid, RenderRect *rr, BOOL sync)
{
/*
	surface 16, backbuffer 16: SVGA_3D_CMD_PRESENT_READBACK sid -> screen, vramcpy screen -> backbuffer
	surface 32, backbuffer 16: SVGA_3D_CMD_PRESENT_READBACK sid -> screen, vramcpy screen -> backbuffer
	surface 16, backbuffer 32: SVGA_3D_CMD_PRESENT_READBACK sid -> screen, SVGA_CMD_BLIT_SCREEN_TO_GMRFB screen -> backbuffer
	surface 32, backbuffer 32: SVGA_3D_CMD_PRESENT_READBACK sid -> screen, SVGA_CMD_BLIT_SCREEN_TO_GMRFB screen -> backbuffer
*/
	cmd_present_t cmd_present = CMD_PRESENT_READBACK_INIT;

	cmd_present.present.sid = sid;

	cmd_present.rect.x = rr->x;
	cmd_present.rect.y = rr->y;
	cmd_present.rect.w = rr->w;
	cmd_present.rect.h = rr->h;
	cmd_present.rect.srcx = rr->srcx;
	cmd_present.rect.srcy = rr->srcy;

	if(svga->hda->bpp != 32)
	{
		vramcpy_rect_t vrect;
		
		vrect.dst_pitch   = svga->hda->pitch;
		vrect.dst_x       = rr->x;
		vrect.dst_y       = rr->y;
		vrect.dst_w       = rr->w;
		vrect.dst_h       = rr->h;
		vrect.dst_bpp     = svga->hda->bpp;
		vrect.src_pitch   = SVGA_pitch(svga->hda->width, 32);
		vrect.src_x       = rr->x;
		vrect.src_y       = rr->y;
		vrect.src_bpp     = 32;
		
		SVGAPush(svga, &cmd_present, sizeof(cmd_present_t));
		SVGAFinish(svga, SVGA_CB_SYNC | SVGA_CB_PRESENT, 0);
		
		vramcpy(((BYTE*)svga->hda->vram_pm32) + svga->hda->surface, svga->hda->vram_pm32, &vrect);
	}
	else
	{
		uint32_t flags = SVGA_CB_PRESENT;
		cmd_blit_screen_gmrfb_t gmrblit = CMD_BLIT_SCREEN_GMRFB_INIT;
		
	  gmrblit.blit.destOrigin.x   = rr->x;
	  gmrblit.blit.destOrigin.y   = rr->y;
	  gmrblit.blit.srcRect.left   = rr->x;
	  gmrblit.blit.srcRect.right  = rr->x + rr->w;
	  gmrblit.blit.srcRect.top    = rr->y;
	  gmrblit.blit.srcRect.bottom = rr->y + rr->h;
	  gmrblit.blit.srcScreenId    = 1;

		SVGAStart(svga);
		SVGAPush(svga, &cmd_present, sizeof(cmd_present_t));
		if(sync)
		{
			SVGAPush(svga, &gmrblit, sizeof(cmd_blit_screen_gmrfb_t));
			SVGAFinish(svga, SVGA_CB_SYNC | SVGA_CB_PRESENT, 0);
		}
		else
		{
			SVGAFinish(svga, SVGA_CB_DIRTY_SURFACE | SVGA_CB_PRESENT, 0);
		}
	}
}
#endif

static void copy3ways_dma(svga_inst_t *svga, uint32_t sid, RenderRect *rr, BOOL sync, BOOL update_bug)
{
/*
	surface 16, backbuffer 16: SVGA_3D_CMD_SURFACE_DMA sid -> backbuffer, vramcpy backbuffer -> screen
	surface 32, backbuffer 32: SVGA_3D_CMD_SURFACE_DMA sid -> backbuffer, SVGA_CMD_BLIT_GMRFB_TO_SCREEN screen -> backbuffer
*/
	if(svga->hda->bpp == 32)
	{
		if(sync || update_bug)
		{
			cmd_surfacedma_t cmd_dma     = CMD_SURFACEDMA_INIT;
			cmd_blit_gmrfb_screen_t cmd_blit = CMD_BLIT_GMRFB_SCREEN_INIT;
			//cmd_update_t cmd_update      = CMD_UPDATE_INIT;

			cmd_dma.dma.guest.ptr.gmrId  = SVGA_GMR_FRAMEBUFFER;
			cmd_dma.dma.guest.ptr.offset = svga->hda->surface;
			cmd_dma.dma.guest.pitch      = svga->hda->pitch;
			cmd_dma.dma.host.sid         = sid;
			cmd_dma.dma.host.face        = 0;
			cmd_dma.dma.host.mipmap      = 0;
			cmd_dma.dma.transfer         = SVGA3D_READ_HOST_VRAM;

			cmd_dma.box.x = rr->srcx;
			cmd_dma.box.y = rr->srcy;
			cmd_dma.box.z = 0;
			cmd_dma.box.w = rr->w;
			cmd_dma.box.h = rr->h;
			cmd_dma.box.d = 1;
			cmd_dma.box.srcx = rr->x;
			cmd_dma.box.srcy = rr->y;
			cmd_dma.box.srcz = 0;
	
			cmd_dma.suffix.suffixSize            = sizeof(SVGA3dCmdSurfaceDMASuffix);
			cmd_dma.suffix.maximumOffset         = svga->hda->stride;
			cmd_dma.suffix.flags.discard         = 1; // 0
			cmd_dma.suffix.flags.unsynchronized  = 0;
			cmd_dma.suffix.flags.reserved        = 0;
	
		  cmd_blit.blit.srcOrigin.x     = rr->x;
		  cmd_blit.blit.srcOrigin.y     = rr->y;
		  cmd_blit.blit.destRect.left   = rr->x;
		  cmd_blit.blit.destRect.right  = rr->x + rr->w;
		  cmd_blit.blit.destRect.top    = rr->y;
		  cmd_blit.blit.destRect.bottom = rr->y + rr->h;
		  cmd_blit.blit.destScreenId    = 0;
	
		  //cmd_update.rect.x = rr->x;
		  //cmd_update.rect.y = rr->y;
		  //cmd_update.rect.width  = rr->w;
		  //cmd_update.rect.height = rr->h;
	
			SVGAStart(svga);
			SVGAPush(svga, &cmd_dma, sizeof(cmd_surfacedma_t));
			SVGAPush(svga, &cmd_blit, sizeof(cmd_blit_gmrfb_screen_t));
			//SVGAPush(svga, &cmd_update, sizeof(cmd_update_t));
			
			if(sync)
			{
				SVGAFinish(svga, SVGA_CB_PRESENT | SVGA_CB_SYNC, 0);
			}
			else
			{
				SVGAFinish(svga, SVGA_CB_PRESENT, 0);
			}
		}
		else
		{
			cmd_surfacedma_t cmd_dma     = CMD_SURFACEDMA_INIT;
			cmd_update_t cmd_update      = CMD_UPDATE_INIT;
			
			cmd_dma.dma.guest.ptr.gmrId  = SVGA_GMR_FRAMEBUFFER;
			cmd_dma.dma.guest.ptr.offset = 0;
			cmd_dma.dma.guest.pitch      = svga->hda->pitch; /* pitch of surface == pitch of screen */
			cmd_dma.dma.host.sid         = sid;
			cmd_dma.dma.host.face        = 0;
			cmd_dma.dma.host.mipmap      = 0;
			cmd_dma.dma.transfer         = SVGA3D_READ_HOST_VRAM;
			
			cmd_dma.box.x = rr->srcx;
			cmd_dma.box.y = rr->srcy;
			cmd_dma.box.z = 0;
			cmd_dma.box.w = rr->w;
			cmd_dma.box.h = rr->h;
			cmd_dma.box.d = 1;
			cmd_dma.box.srcx = rr->x;
			cmd_dma.box.srcy = rr->y;
			cmd_dma.box.srcz = 0;
			
			cmd_dma.suffix.suffixSize            = sizeof(SVGA3dCmdSurfaceDMASuffix);
			cmd_dma.suffix.maximumOffset         = svga->hda->stride;
			cmd_dma.suffix.flags.discard         = 1; // 0
			cmd_dma.suffix.flags.unsynchronized  = 0;
			cmd_dma.suffix.flags.reserved        = 0;
			
		  cmd_update.rect.x = rr->x;
		  cmd_update.rect.y = rr->y;
		  cmd_update.rect.width  = rr->w;
		  cmd_update.rect.height = rr->h;
		  
			SVGAStart(svga);
			SVGAPush(svga, &cmd_dma, sizeof(cmd_surfacedma_t));
			SVGAPush(svga, &cmd_update, sizeof(cmd_update_t));
			SVGAFinish(svga, SVGA_CB_PRESENT | SVGA_CB_UPDATE | SVGA_CB_DIRTY_SURFACE, 0);
		}
	}
	else if(svga->hda->bpp == rr->surf_bpp)
	{
		cmd_surfacedma_t cmd_dma     = CMD_SURFACEDMA_INIT;
		cmd_update_t cmd_update      = CMD_UPDATE_INIT;
		vramcpy_rect_t vrect;

		cmd_dma.dma.guest.ptr.gmrId  = SVGA_GMR_FRAMEBUFFER;
		cmd_dma.dma.guest.ptr.offset = svga->hda->surface;
		cmd_dma.dma.guest.pitch      = svga->hda->pitch;
		cmd_dma.dma.host.sid         = sid;
		cmd_dma.dma.host.face        = 0;
		cmd_dma.dma.host.mipmap      = 0;
		cmd_dma.dma.transfer         = SVGA3D_READ_HOST_VRAM;

		cmd_dma.box.x = rr->srcx;
		cmd_dma.box.y = rr->srcy;
		cmd_dma.box.z = 0;
		cmd_dma.box.w = rr->w;
		cmd_dma.box.h = rr->h;
		cmd_dma.box.d = 1;
		cmd_dma.box.srcx = rr->x;
		cmd_dma.box.srcy = rr->y;
		cmd_dma.box.srcz = 0;

		cmd_dma.suffix.suffixSize            = sizeof(SVGA3dCmdSurfaceDMASuffix);
		cmd_dma.suffix.maximumOffset         = svga->hda->stride;
		cmd_dma.suffix.flags.discard         = 1; // 0
		cmd_dma.suffix.flags.unsynchronized  = 0;
		cmd_dma.suffix.flags.reserved        = 0;

	  cmd_update.rect.x = rr->x;
	  cmd_update.rect.x = rr->x;
	  cmd_update.rect.width  = rr->w;
	  cmd_update.rect.height = rr->h;

		SVGASend(svga, &cmd_dma, sizeof(cmd_surfacedma_t), SVGA_CB_UPDATE | SVGA_CB_SYNC, 0);

		vrect.dst_pitch   = SVGA_pitch(svga->hda->width, 32);
		vrect.dst_x       = rr->x;
		vrect.dst_y       = rr->y;
		vrect.dst_w       = rr->w;
		vrect.dst_h       = rr->h;
		vrect.dst_bpp     = 32;
		vrect.src_pitch   = svga->hda->pitch;
		vrect.src_x       = rr->x;
		vrect.src_y       = rr->y;
		vrect.src_bpp     = svga->hda->bpp;

		vramcpy(((BYTE*)svga->hda->vram_pm32) + svga->hda->surface, svga->hda->vram_pm32, &vrect);
		SVGASend(svga, &cmd_update, sizeof(cmd_update_t), SVGA_CB_UPDATE | SVGA_CB_SYNC, 0);
	}
	else if(rr->surf_bpp == 32 && !update_bug)
	{
		cmd_surfacedma_t cmd_dma     = CMD_SURFACEDMA_INIT;
		cmd_update_t cmd_update      = CMD_UPDATE_INIT;
		vramcpy_rect_t vrect;

		cmd_dma.dma.guest.ptr.gmrId  = SVGA_GMR_FRAMEBUFFER;
		cmd_dma.dma.guest.ptr.offset = 0;
		cmd_dma.dma.guest.pitch      = SVGA_pitch(svga->hda->width, 32);
		cmd_dma.dma.host.sid         = sid;
		cmd_dma.dma.host.face        = 0;
		cmd_dma.dma.host.mipmap      = 0;
		cmd_dma.dma.transfer         = SVGA3D_READ_HOST_VRAM;

		cmd_dma.box.x = rr->srcx;
		cmd_dma.box.y = rr->srcy;
		cmd_dma.box.z = 0;
		cmd_dma.box.w = rr->w;
		cmd_dma.box.h = rr->h;
		cmd_dma.box.d = 1;
		cmd_dma.box.srcx = rr->x;
		cmd_dma.box.srcy = rr->y;
		cmd_dma.box.srcz = 0;

		cmd_dma.suffix.suffixSize            = sizeof(SVGA3dCmdSurfaceDMASuffix);
		cmd_dma.suffix.maximumOffset         = svga->hda->stride;
		cmd_dma.suffix.flags.discard         = 1; // 0
		cmd_dma.suffix.flags.unsynchronized  = 0;
		cmd_dma.suffix.flags.reserved        = 0;

	  cmd_update.rect.x = rr->x;
	  cmd_update.rect.y = rr->y;
	  cmd_update.rect.width  = rr->w;
	  cmd_update.rect.height = rr->h;

		SVGAStart(svga);
		SVGAPush(svga, &cmd_dma, sizeof(cmd_surfacedma_t));
		SVGAPush(svga, &cmd_update, sizeof(cmd_update_t));

		if(sync)
		{
			SVGAFinish(svga, SVGA_CB_UPDATE | SVGA_CB_SYNC, 0);
			
			vramcpy_rect_t vrect;
			
			vrect.dst_pitch   = svga->hda->pitch;
			vrect.dst_x       = rr->x;
			vrect.dst_y       = rr->y;
			vrect.dst_w       = rr->w;
			vrect.dst_h       = rr->h;
			vrect.dst_bpp     = svga->hda->bpp;
			vrect.src_pitch   = SVGA_pitch(svga->hda->width, 32);
			vrect.src_x       = rr->x;
			vrect.src_y       = rr->y;
			vrect.src_bpp     = 32;

			vramcpy(((BYTE*)svga->hda->vram_pm32) + svga->hda->surface, svga->hda->vram_pm32, &vrect);
			SVGASend(svga, &cmd_update, sizeof(cmd_update_t), SVGA_CB_UPDATE | SVGA_CB_SYNC, 0);
		}
		else
		{
			SVGAFinish(svga, SVGA_CB_PRESENT | SVGA_CB_UPDATE | SVGA_CB_DIRTY_SURFACE, 0);
		}
	}
}

#if 0
static BOOL SVGAPresentDirect(svga_inst_t *svga, uint32_t source_sid, RenderRect *rr)
{
	if(svga->hda->flags & FB_MOUSE_NO_BLIT)
	{
		copy3ways_present(svga, source_sid, rr, FALSE);
	}
	else
	{
		FBHDA_access_begin(FBHDA_ACCESS_RAW_BUFFERING);
		copy3ways_present(svga, source_sid, rr, TRUE);
		FBHDA_access_end(0);
	}
	
	return TRUE;
}
#else
static BOOL SVGAPresentDirect(svga_inst_t *svga, uint32_t source_sid, RenderRect *rr)
{
	BOOL vmware_update_bug = TRUE;
	if(svga->dx)
	{
		vmware_update_bug = svga->hda->flags & FB_BUG_VMWARE_UPDATE;
	}
	else if(debug_get_option_svga_dma_fb())
	{
		vmware_update_bug = FALSE;
	}
	
	if(!rr->need_conv || (rr->surf_bpp == 32 && !vmware_update_bug))
	{
		if(svga->hda->flags & FB_MOUSE_NO_BLIT)
		{
			copy3ways_dma(svga, source_sid, rr, FALSE, vmware_update_bug);
		}
		else
		{
			FBHDA_access_begin(FBHDA_ACCESS_RAW_BUFFERING);
			copy3ways_dma(svga, source_sid, rr, TRUE, vmware_update_bug);
			FBHDA_access_end(0);
		}
		
		return TRUE;	
	}

	return FALSE;
}
#endif

static DWORD present_last_gamma = -1;

static void SVGAPresentCopy(svga_inst_t *svga, HDC hDC, uint32_t cid, uint32_t sid, RenderRect *rr, BOOL gamma)
{
	cmd_surfacedma_t command_dma = CMD_SURFACEDMA_INIT;
	cmd_readback_gb_surface_t command_dx = CMD_READBACK_GB_SURFACE_INIT;
	vramcpy_rect_t vrect;
	
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

	if(sinfo->gmrId == 0) /* vGPU9 */
	{
		if(set_fb_gmr(svga, sinfo->width, sinfo->height))
		{
			command_dma.dma.guest.ptr.gmrId  = svga->softblit_gmr_id;
			command_dma.dma.guest.ptr.offset = 0;
			command_dma.dma.guest.pitch      = sinfo->width * sps;
			command_dma.dma.host.sid         = sid;
			command_dma.dma.host.face        = 0;
			command_dma.dma.host.mipmap      = 0;
			command_dma.dma.transfer         = SVGA3D_READ_HOST_VRAM;

			command_dma.box.x = 0;
			command_dma.box.y = 0;
			command_dma.box.z = 0;
			command_dma.box.w = sinfo->width;
			command_dma.box.h = sinfo->height;
			command_dma.box.d = 1;
			command_dma.box.srcx = 0;
			command_dma.box.srcy = 0;
			command_dma.box.srcz = 0;

			command_dma.suffix.suffixSize    = sizeof(SVGA3dCmdSurfaceDMASuffix);
			command_dma.suffix.maximumOffset = sinfo->height * sinfo->width * sps;
			command_dma.suffix.flags.discard         = 1;
			command_dma.suffix.flags.unsynchronized  = 0;
			command_dma.suffix.flags.reserved        = 0;

			SVGASend(svga, &command_dma, sizeof(command_dma), SVGA_CB_SYNC, 0);

			gmr = (void*)SVGARegionGet(svga, svga->softblit_gmr_id)->info.address;
		}
	}
	else /* vGPU10 */
	{
		command_dx.surf.sid = sid;
		SVGASend(svga, &command_dx, sizeof(command_dx), SVGA_CB_SYNC | SVGA_CB_FLAG_DX_CONTEXT, cid);
		gmr = (void*)SVGARegionGet(svga, sinfo->gmrId)->info.address;
	}
	
	if(gmr == NULL) return;

	if(gamma)
	{
		if(svga->hda->gamma_update != present_last_gamma)
		{
			vramcpy_gamma_load(hDC);
			present_last_gamma = svga->hda->gamma_update;
		}
	}

#if 1
	vrect.dst_pitch   = svga->hda->pitch;
	vrect.dst_bpp     = svga->hda->bpp;
#else
	vrect.dst_pitch   = SVGA_pitch(svga->hda->width, 32);
	vrect.dst_bpp     = 32;
#endif
	vrect.dst_x       = rr->x;
	vrect.dst_y       = rr->y;
	vrect.dst_w       = rr->w;
	vrect.dst_h       = rr->h;
	vrect.src_pitch   = sinfo->width * sps;
	vrect.src_x       = rr->srcx;
	vrect.src_y       = rr->srcy;
	vrect.src_bpp     = sinfo->bpp;

#if 1
	FBHDA_access_rect(rr->x, rr->y, rr->x+rr->w, rr->y+rr->h);
	if(gamma)
	{
		vramcpy_gamma(((BYTE*)svga->hda->vram_pm32)+svga->hda->surface, gmr, &vrect);
	}
	else
	{
		vramcpy(((BYTE*)svga->hda->vram_pm32)+svga->hda->surface, gmr, &vrect);
	}
	FBHDA_access_end(0);
#else
	FBHDA_access_begin(FBHDA_ACCESS_RAW_BUFFERING);
	vramcpy_gamma(svga->hda->vram_pm32, gmr, &vrect);
	FBHDA_access_end(FBHDA_ACCESS_SURFACE_DIRTY);
#endif
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
	
	if(svga->hda->overlay == 0)
	{
		if(!debug_get_option_mesa_sw_gamma())
		{
			if(!SVGAPresentDirect(svga, sid, &rr))
			{
				SVGAPresentCopy(svga, hDC, cid, sid, &rr, FALSE);
			}
		}
		else
		{
			SVGAPresentCopy(svga, hDC, cid, sid, &rr, TRUE);
		}
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
		if(set_fb_gmr(svga, sinfo->width, sinfo->height))
		{
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
			command.suffix.maximumOffset = sinfo->height * sinfo->width * sps;
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
