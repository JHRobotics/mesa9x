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

/*
 * Commands uploading
 */
void SVGAStart(svga_inst_t *svga)
{
	int id = svga->cmd_next++;
	if(svga->cmd_next >= CMD_BUFFER_COUNT)
	{
		svga->cmd_next = 0;
	}
		
	if(svga->cmd_stat[id].qStatus)
	{
		while(*(svga->cmd_stat[id].qStatus) == SVGA_CB_STATUS_NONE);
	}

	svga->cmd_stat[id].sStatus = SVGA_PROC_QUEUED;
	svga->cmd_stat[id].qStatus = NULL;

	svga->cmd_act = id;
	svga->cmd_pos = 0;
}

void SVGAPush(svga_inst_t *svga, const void *cmd, const size_t size)
{
	uint8_t *ptr = svga->cmd_buf[svga->cmd_act];
	ptr += svga->cmd_pos;

	memcpy(ptr, cmd, size);
	svga->cmd_pos += size;
}

void *SVGAPull(svga_inst_t *svga, const size_t size)
{
	uint8_t *ptr = svga->cmd_buf[svga->cmd_act];
	ptr += svga->cmd_pos;
	svga->cmd_pos += size;

	return (void*)ptr;
}

void SVGAFinish(svga_inst_t *svga, DWORD flags, DWORD DXCtxId)
{
	const int id = svga->cmd_act;

	SVGA_CMB_submit(svga->cmd_buf[id], svga->cmd_pos, &(svga->cmd_stat[id]), flags, DXCtxId);

	svga->last_complete_fence = svga->cmd_stat[id].fifo_fence_last;

	if(svga->cmd_stat[id].fifo_fence_used != 0)
	{
		svga->last_seen_fence = svga->cmd_stat[id].fifo_fence_used;
		svga->cmd_fence = svga->cmd_stat[id].fifo_fence_used;
	}
	svga->cmd_pos = 0;
}

void SVGASend(svga_inst_t *svga, const void *cmd, const size_t size, DWORD flags, DWORD DXCtxId)
{
	SVGAStart(svga);
	SVGAPush(svga, cmd, size);
	SVGAFinish(svga, flags, DXCtxId);
}

void SVGAWaitAll(svga_inst_t *svga)
{
	int i;
	int to_wait;
	
	do
	{
		to_wait = 0;
		for(i = 0; i < CMD_BUFFER_COUNT; i++)
		{
			switch(svga->cmd_stat[i].sStatus)
			{
				case SVGA_PROC_NONE:
					if(svga->cmd_stat[i].qStatus)
					{
						while(*(svga->cmd_stat[i].qStatus) == SVGA_CB_STATUS_NONE);
					}
					break;
				case SVGA_PROC_COMPLETED:
				case SVGA_PROC_ERROR:
					break;
				case SVGA_PROC_FENCE:
					if(svga->cmd_stat[i].fifo_fence_used)
					{
						SVGA_fence_wait(svga->cmd_stat[i].fifo_fence_used);
					}
					break;
				default:
					to_wait++;
					break;
			}
		}
	} while(to_wait != 0);
}
