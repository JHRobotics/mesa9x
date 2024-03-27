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


/* sometimes missing */
#ifndef MIN
#define MIN(_a, _b) ((_b) < (_a) ? (_b) : (_a))
#endif

/* page size */
#ifndef PAGE_SIZE
#define PAGE_SIZE 4096
#endif

#ifndef PAGE_SHIFT
#define PAGE_SHIFT 12
#endif

#define SVGA_ASSERT assert(svga != NULL)

#ifndef SVGA_CB_FLAG_DX_CONTEXT
#define SVGA_CB_FLAG_DX_CONTEXT SVGA_CB_DX_FLAG_DX_CONTEXT
#endif

#define GMR_LIMIT_DEFAULT_MB 192
// ^ max recomended value for 512 MB RAM
#define GMR_LIMIT_MIN_MB 64

/*
 * GUI (errors...)
 */
#ifdef GUI_ERRORS
void GUIError(svga_inst_t *svga, const char *msg, ...)
{
  static char msgbuf[512];
  int ans;

  va_list args;
  va_start(args, msg);
  vsprintf(msgbuf, msg, args);
  va_end(args);

  ans = MessageBoxA(NULL, msgbuf, "GPU Driver Error", MB_ICONERROR | MB_RETRYCANCEL | MB_DEFBUTTON2);

	if(ans == IDRETRY)
	{
		ExitWindowsEx(EWX_REBOOT | EWX_FORCEIFHUNG, SHTDN_REASON_MAJOR_OPERATINGSYSTEM);
		ExitProcess(0);
	}
	else
	{
		ExitProcess(-1);
	}
}
#else
#define GUIError(...)
#endif

/* debug messages */
#ifdef DEBUG
void svga_printf(svga_inst_t *svga, const char *fmt, ...)
{
	// ...
}
#endif
/*
 * Database (shared between processes)
 */
#define DB_IDNext(_t, _tname, _fname) \
	static uint32_t SVGA ## _fname ## IDNext(svga_inst_t *svga, int start_index){ \
	uint32_t sel_id = 0; \
	SVGA_ASSERT; \
	SVGA_DB_lock(); \
	for(uint32_t id = start_index-1; id < svga->db->_tname ## _cnt; id++) { \
		if(svga->db->_tname[id].pid == 0) { \
			memset(&(svga->db->_tname[id]), 0, sizeof(_t)); \
			svga->db->_tname[id].pid = svga->pid; \
			sel_id = id+1; \
			break; \
	} } \
	SVGA_DB_unlock(); \
	return sel_id; }

#define DB_IDPop(_t, _tname, _fname) \
	static uint32_t SVGA ## _fname ## IDPop(svga_inst_t *svga, uint32_t ident){ \
	uint32_t sel_id = 0; \
	SVGA_ASSERT; \
	if(ident == 0) { \
		ident = svga->pid; } \
	SVGA_DB_lock(); \
	for(uint32_t id = 0; id < svga->db->_tname ## _cnt; id++) { \
		if(svga->db->_tname[id].pid == ident) { \
			svga->db->_tname[id].pid = svga->pid; \
			sel_id = id+1; \
			break; \
	} } \
	SVGA_DB_unlock(); \
	return sel_id; }

#define DB_IDFree(_t, _tname, _fname) \
	static void SVGA ## _fname ## IDFree(svga_inst_t *svga, uint32_t sid){ \
	SVGA_ASSERT; \
	if(sid == 0) return; \
	SVGA_DB_lock(); \
	svga->db->_tname[sid-1].pid = 0; \
	SVGA_DB_unlock(); }

#define DB_IDInfo(_t, _tname, _fname) \
	static _t *SVGA ## _fname ## IDInfo(svga_inst_t *svga, uint32_t sid){ \
	SVGA_ASSERT; \
	if(sid == 0) return NULL; \
	if(svga->db->_tname[sid-1].pid != 0){	\
		return &(svga->db->_tname[sid-1]);} \
	return NULL; }


DB_IDNext(SVGA_DB_surface_t, surfaces, Surface);
DB_IDPop(SVGA_DB_surface_t,  surfaces, Surface);
DB_IDFree(SVGA_DB_surface_t, surfaces, Surface);
DB_IDInfo(SVGA_DB_surface_t, surfaces, Surface);

DB_IDNext(SVGA_DB_context_t, contexts, Context);
DB_IDPop(SVGA_DB_context_t,  contexts, Context);
DB_IDFree(SVGA_DB_context_t, contexts, Context);
DB_IDInfo(SVGA_DB_context_t, contexts, Context);

DB_IDNext(SVGA_DB_region_t, regions, GMR);
DB_IDPop(SVGA_DB_region_t,  regions, GMR);
DB_IDFree(SVGA_DB_region_t, regions, GMR);
DB_IDInfo(SVGA_DB_region_t, regions, GMR);

/* lookup for ident/PID, position in buffer is stored in state */
BOOL SVGAGetNextPid(svga_inst_t *svga, uint32_t *pid, uint32_t *state)
{
	uint32_t fpid = 0;
	do
	{
		if((*state) < svga->db->regions_cnt)
		{
			fpid = svga->db->regions[(*state)].pid;
		}
		else if((*state) < svga->db->regions_cnt + svga->db->contexts_cnt)
		{
			fpid = svga->db->contexts[(*state) - svga->db->regions_cnt].pid;;
		}
		else if((*state) < svga->db->regions_cnt + svga->db->contexts_cnt + svga->db->surfaces_cnt)
		{
			fpid = svga->db->surfaces[(*state) - (svga->db->regions_cnt + svga->db->contexts_cnt)].pid;
		}
		else
		{
			break;
		}

		(*state)++;
	} while(fpid == 0);

	if(fpid)
	{
		*pid = fpid;
		return TRUE;
	}

	return FALSE;
}

/*
 * HW registry
 */
/* read hardware register */
BOOL SVGAReadReg(svga_inst_t *svga, uint32_t reg, uint32_t *val)
{
	*val = SVGA_query(SVGA_QUERY_REGS, reg);

	return TRUE;
}

/*
 * Fences
 */
/* wait to given fence */
void SVGAFenceSync(svga_inst_t *svga, uint32_t fence)
{
	SVGA_ASSERT;

	if(fence > svga->last_seen_fence)
	{
		// fence id overrun
	}
	if(fence > svga->last_complete_fence)
	{
		SVGA_fence_wait(fence);
		SVGA_fence_query((DWORD *)&svga->last_complete_fence, (DWORD *)&svga->last_seen_fence);
	}
}

/* query fence state */
BOOL SVGAFenceQuery(svga_inst_t *svga, uint32_t fence, uint32_t *fenceStatus, uint32_t *lastPassed, uint32_t *lastFence)
{
	SVGA_ASSERT;

	uint32_t last_fence;
	uint32_t passed_fence;

	SVGA_fence_query((DWORD *)&passed_fence, (DWORD *)&last_fence);

	if(fence > last_fence)
	{
		if(fenceStatus)
			*fenceStatus = 1;

		if(lastPassed)
			*lastPassed = fence;

		if(lastFence)
			*lastFence = fence;

		return TRUE; /* FALSE? */
	}

	if(fenceStatus)
	{
		if(fence <= passed_fence)
			*fenceStatus = 1;
		else
			*fenceStatus = 0;
	}

	if(lastPassed)
		*lastPassed = passed_fence;

	if(lastFence)
		*lastFence = last_fence;

	svga->last_complete_fence  = passed_fence;
	svga->last_seen_fence      = last_fence;

	return TRUE;
}

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
		while(*(svga->cmd_stat[id].qStatus) == SVGA_PROC_NONE);
	}

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

/*
 * Inicializators
 */

static BOOL SVGAInitOTables(svga_inst_t *svga)
{
	//BOOL createCtx = FALSE;
	DWORD i;
#pragma pack(push)
#pragma pack(1)
	struct
	{
		uint32_t cmd;
		uint32_t size;
		SVGA3dCmdSetOTableBase otable;
	} cmd;
#pragma pack(pop)

	SVGA_OT_info_entry_t *ot = SVGA_OT_setup();
	if(ot == NULL)
	{
		return FALSE;
	}

	for(i = SVGA_OTABLE_MOB; i < SVGA_OTABLE_DX_MAX; i++)
	{
		SVGA_OT_info_entry_t *entry = &(ot[i]);

		if((entry->flags & SVGA_OT_FLAG_ACTIVE) == 0)
		{
			if(entry->size > 0)
			{
				cmd.cmd = SVGA_3D_CMD_SET_OTABLE_BASE;
				cmd.size = sizeof(SVGA3dCmdSetOTableBase);
				cmd.otable.type = i;
				cmd.otable.baseAddress = entry->phy / PAGE_SIZE;
				cmd.otable.sizeInBytes = entry->size;
			  cmd.otable.validSizeInBytes = 0;
			  cmd.otable.ptDepth = SVGA3D_MOBFMT_RANGE;

			  SVGASend(svga, &cmd, sizeof(cmd), SVGA_CB_SYNC, 0);
			  debug_printf("setting OTable: %d!\n", i);

			  entry->flags |= SVGA_OT_FLAG_ACTIVE;
			}
		}
	}

	return TRUE;
}

/*
 * Resources
 */
DEBUG_GET_ONCE_NUM_OPTION(gmr_limit_mb, "SVGA_GMR_LIMIT", GMR_LIMIT_DEFAULT_MB);

/* create SVGA interface */
BOOL SVGACreate(svga_inst_t *svga)
{
	if(!SVGA_valid())
	{
		GUIError(svga, "Your GPU isn't VMWare SVGA II or you haven't installed corresponding driver.");
		return FALSE;
	}

	memset(svga, 0, sizeof(svga_inst_t));

	svga->dx = FALSE;
	svga->hda = FBHDA_setup();
	svga->pid = GetCurrentProcessId();
	svga->db  = SVGA_DB_setup();

	if(svga->hda->flags & FB_ACCEL_VMSVGA3D)
	{
		int i;
		debug_printf("%s SUCCESS!\n", __FUNCTION__);
		svga->ctx_id = 0;

		for(i = 0; i < CMD_BUFFER_COUNT; i++)
		{
			svga->cmd_buf[i] = SVGA_CMB_alloc();
			if(svga->cmd_buf[i] == NULL)
			{
				GUIError(svga, "Cannot allocate memory for command buffers. Please reboot the system and try it again!");
				return FALSE;
			}

			svga->cmd_stat[i].sStatus = SVGA_PROC_COMPLETED;
			svga->cmd_stat[i].qStatus = &(svga->cmd_stat[i].sStatus);
		}

		svga->cmd_next = 0;
		svga->cmd_act  = -1;

		svga->cmd_fence = 0;
		svga->last_seen_fence = 1;
		svga->last_complete_fence = 1;

		svga->softblit_gmr_id = 0;
		svga->softblit_gmr_ptr = NULL;
		svga->softblit_gmr_size = 0;

		/* GEN10 */
		if(svga->hda->flags & FB_ACCEL_VMSVGA10)
		{
			SVGAInitOTables(svga);
		}

		int limit_mb = debug_get_option_gmr_limit_mb();
		if(limit_mb == 0)
		{
			limit_mb = GMR_LIMIT_DEFAULT_MB;
		}
		else if(limit_mb < GMR_LIMIT_MIN_MB)
		{
			limit_mb = GMR_LIMIT_MIN_MB;
		}

		svga->gmr_mem_limit = limit_mb * 1024 * 1024;

		return TRUE;
	}
	else
	{
		GUIError(svga, "3D functionality is disabled!\n\nIt could be disabled in VM configuration or is disabled by hypervisor due missing compatible GPU on host site or VM serve wrong 3D api");
	}

	return FALSE;
}

/* create context */
uint32_t SVGAContextCreate(svga_inst_t *svga)
{
#pragma pack(push)
#pragma pack(1)
	struct
	{
		SVGA3dCmdHeader header;
		SVGA3dCmdDefineContext defctx;
	} ctx_cmd;
#pragma pack(pop)

	SVGA_ASSERT;

	if(svga->dx)
		ctx_cmd.header.id = SVGA_3D_CMD_DX_DEFINE_CONTEXT;
	else
		ctx_cmd.header.id = SVGA_3D_CMD_CONTEXT_DEFINE;

	ctx_cmd.header.size = sizeof(SVGA3dCmdDefineContext);
	ctx_cmd.defctx.cid = SVGAContextIDNext(svga, 1);

	SVGASend(svga, &ctx_cmd, sizeof(ctx_cmd), SVGA_CB_SYNC, 0);

	svga->ctx_id = ctx_cmd.defctx.cid;

	debug_printf("creating context %d\n", svga->ctx_id);

	SVGAContextIDInfo(svga, svga->ctx_id)->cotable = NULL;

	return ctx_cmd.defctx.cid;
}

/* destroy context */
void SVGAContextDestroy(svga_inst_t *svga, uint32_t cid)
{
#pragma pack(push)
#pragma pack(1)
	struct
	{
		SVGA3dCmdHeader header;
		SVGA3dCmdDestroyContext defctx;
	} ctx_cmd;
#pragma pack(pop)

	SVGA_ASSERT;
	assert(cid > 0);

	debug_printf("destroing context %d\n", cid);

	if(svga->dx)
		ctx_cmd.header.id = SVGA_3D_CMD_DX_DESTROY_CONTEXT;
	else
		ctx_cmd.header.id = SVGA_3D_CMD_CONTEXT_DESTROY;

	ctx_cmd.header.size = sizeof(SVGA3dCmdDefineContext);
	ctx_cmd.defctx.cid = cid;

	SVGASend(svga, &ctx_cmd, sizeof(ctx_cmd), SVGA_CB_SYNC, 0);

	SVGAContextIDFree(svga, cid);
	svga->ctx_id = 0;
}

#define COTABLE_ENTRIES_BLOCK 128

static uint32_t cootable_step(SVGACOTableType type)
{
	switch(type)
	{
		case SVGA_COTABLE_SRVIEW:
			return 16*COTABLE_ENTRIES_BLOCK;
		case SVGA_COTABLE_DXSHADER:
			return 4*COTABLE_ENTRIES_BLOCK;
		default:
			return COTABLE_ENTRIES_BLOCK;
	}
}

static const svga_cotable_t def_cotable = {{
	{SVGA_COTABLE_RTVIEW,          sizeof(SVGACOTableDXRTViewEntry),          COTABLE_ENTRIES_BLOCK, 0},
	{SVGA_COTABLE_DSVIEW,          sizeof(SVGACOTableDXDSViewEntry),          COTABLE_ENTRIES_BLOCK, 0},
	{SVGA_COTABLE_SRVIEW,          sizeof(SVGACOTableDXSRViewEntry),          16*COTABLE_ENTRIES_BLOCK, 0},
	{SVGA_COTABLE_ELEMENTLAYOUT,   sizeof(SVGACOTableDXElementLayoutEntry),   COTABLE_ENTRIES_BLOCK, 0},
	{SVGA_COTABLE_BLENDSTATE,      sizeof(SVGACOTableDXBlendStateEntry),      COTABLE_ENTRIES_BLOCK, 0},
	{SVGA_COTABLE_DEPTHSTENCIL,    sizeof(SVGACOTableDXDepthStencilEntry),    COTABLE_ENTRIES_BLOCK, 0},
	{SVGA_COTABLE_RASTERIZERSTATE, sizeof(SVGACOTableDXRasterizerStateEntry), COTABLE_ENTRIES_BLOCK, 0},
	{SVGA_COTABLE_SAMPLER,         sizeof(SVGACOTableDXSamplerEntry),         COTABLE_ENTRIES_BLOCK, 0},
	{SVGA_COTABLE_STREAMOUTPUT,    sizeof(SVGACOTableDXStreamOutputEntry),    COTABLE_ENTRIES_BLOCK, 0},
	{SVGA_COTABLE_DXQUERY,         sizeof(SVGACOTableDXQueryEntry),           4*COTABLE_ENTRIES_BLOCK, 0},
	{SVGA_COTABLE_DXSHADER,        sizeof(SVGACOTableDXShaderEntry),          COTABLE_ENTRIES_BLOCK, 0},
	{SVGA_COTABLE_UAVIEW,          sizeof(SVGACOTableDXUAViewEntry),          COTABLE_ENTRIES_BLOCK, 0},
}};

/* initialize DX cotables for CTX */
BOOL SVGAContextCotableCreate(svga_inst_t *svga, uint32_t cid)
{
#pragma pack(push)
#pragma pack(1)
	struct
	{
		uint32 cmd;
		uint32 size;
		SVGA3dCmdDXSetCOTable entry;
	} cmd_cotable; // SVGA_3D_CMD_DX_SET_COTABLE
#pragma pack(pop)

	// create GB context (if doesn't exist)
	// SVGACBContextCreate(svga);
	svga_cotable_t *cotable = malloc(sizeof(svga_cotable_t));

	if(cotable == NULL)
	{
		return FALSE;
	}

	memcpy(cotable, &def_cotable, sizeof(svga_cotable_t));

	/* allocate cotable entries */
	for(int i = 0; i < SVGA_COTABLE_MAX; i++)
	{
		if(cotable->item[i].cbItem > 0)
	 	{
	 		cotable->item[i].gmr_id = SVGARegionCreate(svga, cotable->item[i].cbItem * cotable->item[i].count, NULL);
	 	}
	}

	/* sync to MOBs creations */
	//SVGAFullSync(svga);

	for(int i = 0; i < SVGA_COTABLE_MAX; i++)
	{
		if(cotable->item[i].gmr_id != 0 && cotable->item[i].cbItem > 0)
		{
			cmd_cotable.cmd = SVGA_3D_CMD_DX_SET_COTABLE;
			cmd_cotable.size = sizeof(SVGA3dCmdDXSetCOTable);
			cmd_cotable.entry.cid = cid;
			cmd_cotable.entry.mobid = cotable->item[i].gmr_id;
			cmd_cotable.entry.type  = cotable->item[i].type;
			cmd_cotable.entry.validSizeInBytes = 0;

			SVGASend(svga, &cmd_cotable, sizeof(cmd_cotable), SVGA_CB_SYNC | SVGA_CB_FLAG_DX_CONTEXT, cid);
		}
	}

  SVGAContextIDInfo(svga, cid)->cotable = cotable;
}

BOOL SVGAContextCotableUpdate(svga_inst_t *svga, uint32_t cid, SVGACOTableType type, uint32_t destId)
{
#pragma pack(push)
#pragma pack(1)
	struct
	{
		SVGA3dCmdHeader header;
		SVGA3dCmdDXSetCOTable entry;
	} cmd_grow_cotable = {{SVGA_3D_CMD_DX_SET_COTABLE, sizeof(SVGA3dCmdDXSetCOTable)}};

	struct
	{
		SVGA3dCmdHeader header;
		SVGA3dCmdDXReadbackCOTable entry;
	} cmd_read_cotable = {{SVGA_3D_CMD_DX_READBACK_COTABLE, sizeof(SVGA3dCmdDXReadbackCOTable)}};
#pragma pack(pop)

	assert(type < SVGA_COTABLE_MAX);

	if(cid == SVGA3D_INVALID_ID)
		return FALSE;

	svga_cotable_t *cotable = SVGAContextIDInfo(svga, cid)->cotable;

	if(cotable == NULL)
	{
		svga_printf(svga, "SVGAContextIDInfoGet FAIL!\n");
		return FALSE;
	}

	if(destId+1 >= cotable->item[type].count)
	{
		/* cotable needs to be resized */
		void *old_mem = NULL;
		void *new_mem = NULL;
		uint32_t new_mem_ptr32 = 0;
		uint32_t old_gmrId = cotable->item[type].gmr_id;
		uint32_t new_gmrId;

		if(old_gmrId)
		{
			old_mem = (void*)SVGAGMRIDInfo(svga, old_gmrId)->info.address;
		}

		/* new number of items round to multiple of COTABLE_ENTRIES_BLOCK */
		uint32_t step = cootable_step(type);

		uint32_t new_count = ((destId + 1 + step) / step) * step;
		uint32_t new_cb = new_count * cotable->item[type].cbItem;

		/* command for flush table to quest */
		cmd_read_cotable.entry.cid   = cid;
		cmd_read_cotable.entry.type  = type;

		/* command to update table in host */
		cmd_grow_cotable.entry.cid = cid;
		cmd_grow_cotable.entry.mobid = SVGA3D_INVALID_ID;
		cmd_grow_cotable.entry.type = type;
		cmd_grow_cotable.entry.validSizeInBytes = cotable->item[type].cbItem * cotable->item[type].count;

		/* process readback */
		SVGAPush(svga, &cmd_read_cotable, sizeof(cmd_read_cotable));
		SVGAFinish(svga,  SVGA_CB_SYNC | SVGA_CB_FLAG_DX_CONTEXT, cid);
		//SVGASend(svga, &cmd_read_cotable, sizeof(cmd_read_cotable), SVGA_CB_SYNC | SVGA_CB_FLAG_DX_CONTEXT, cid);

		SVGAStart(svga);

		svga_printf(svga, "Grow #%d - start copy (%d -> %d)", destId, cotable->item[type].count, new_count);

		new_gmrId = SVGARegionCreate(svga, cotable->item[type].cbItem * new_count, &(new_mem_ptr32));
		new_mem = (void*)new_mem_ptr32;

		if(new_gmrId && new_mem)
		{
			cmd_grow_cotable.entry.mobid = new_gmrId;

			if(new_mem)
			{
				memcpy(new_mem, old_mem, cmd_grow_cotable.entry.validSizeInBytes);
			}
			else
			{
				cmd_grow_cotable.entry.validSizeInBytes  = 0;
			}
		}
		else
		{
			return FALSE;
		}

		/* process grow */
		//SVGASend(svga, &cmd_grow_cotable, sizeof(cmd_grow_cotable), SVGA_CB_SYNC | SVGA_CB_FLAG_DX_CONTEXT, cid);
		SVGAPush(svga, &cmd_grow_cotable, sizeof(cmd_grow_cotable));
		SVGAFinish(svga, SVGA_CB_SYNC | SVGA_CB_FLAG_DX_CONTEXT, cid);

		/* update values */
		cotable->item[type].count  = new_count;
		cotable->item[type].gmr_id = new_gmrId;

		/* delete old GMR */
		SVGARegionDestroy(svga, old_gmrId);

		svga_printf(svga, "Done grow %d to %d", type, new_count);

		SVGAStart(svga);
	}

	return TRUE;
}

/* destroy CTX DX cotable */
void SVGAContextCotableDestroy(svga_inst_t *svga, uint32_t cid)
{
#pragma pack(push)
#pragma pack(1)
	struct
	{
		uint32 cmd;
		uint32 size;
		SVGA3dCmdDXSetCOTable entry;
	} cmd_cotable; // SVGA_3D_CMD_DX_SET_COTABLE
#pragma pack(pop)

	svga_cotable_t *cotable = SVGAContextIDInfo(svga, cid)->cotable;

	if(cotable == NULL) return;

	SVGAContextIDInfo(svga, cid)->cotable = NULL;

	/* invalidate cotable */
	for(int i = 0; i < SVGA_COTABLE_MAX; i++)
	{
		if(cotable->item[i].gmr_id != 0)
		{
			cmd_cotable.cmd         = SVGA_3D_CMD_DX_SET_COTABLE;
			cmd_cotable.size        = sizeof(SVGA3dCmdDXSetCOTable);
			cmd_cotable.entry.cid   = cid;
			cmd_cotable.entry.mobid = SVGA3D_INVALID_ID;
			cmd_cotable.entry.type  = cotable->item[i].type;
			cmd_cotable.entry.validSizeInBytes = 0;

			SVGASend(svga, &cmd_cotable, sizeof(cmd_cotable), SVGA_CB_SYNC | SVGA_CB_FLAG_DX_CONTEXT, cid);
		}
	}

	/* destroy MOBs */
	for(int i = 0; i < SVGA_COTABLE_MAX; i++)
	{
		if(cotable->item[i].gmr_id != 0)
		{
			SVGARegionDestroy(svga, cotable->item[i].gmr_id);
			cotable->item[i].gmr_id = 0;
		}
	}

	free(cotable);
}

/* destroy the surface */
void SVGASurfaceDestroy(svga_inst_t *svga, uint32_t sid, uint32_t *out_surface_size)
{
	SVGA_ASSERT;
	assert(sid > 0);

#pragma pack(push)
#pragma pack(1)
	struct {
		uint32_t cmd;
		uint32_t size;
		SVGA3dCmdBindGBSurface bind;
	} cmd_unbind;
	struct {
		uint32_t cmd;
		uint32_t size;
		SVGA3dCmdDestroySurface surface;
	} cmd_surface;
#pragma pack(pop)

	SVGA_DB_surface_t *sinfo = SVGASurfaceIDInfo(svga, sid);

	if(!sinfo) return;

	if(out_surface_size)
	{
		*out_surface_size = sinfo->size;
	}

	if(sinfo->gmrId) // GB surface
	{
		cmd_unbind.cmd  = SVGA_3D_CMD_BIND_GB_SURFACE;
		cmd_unbind.size = sizeof(SVGA3dCmdBindGBSurface);
		cmd_unbind.bind.sid   = sid;
		cmd_unbind.bind.mobid = SVGA3D_INVALID_ID;

		cmd_surface.cmd = SVGA_3D_CMD_DESTROY_GB_SURFACE;
		cmd_surface.size = sizeof(SVGA3dCmdDestroySurface);
		cmd_surface.surface.sid = sid;

		SVGAStart(svga);
		SVGAPush(svga, &cmd_unbind, sizeof(cmd_unbind));
		SVGAPush(svga, &cmd_surface, sizeof(cmd_surface));
		SVGAFinish(svga, /*SVGA_CB_SYNC*/0, 0);
	}
	else
	{
		cmd_surface.cmd = SVGA_3D_CMD_SURFACE_DESTROY;
		cmd_surface.size = sizeof(SVGA3dCmdDestroySurface);
		cmd_surface.surface.sid = sid;

		SVGASend(svga, &cmd_surface, sizeof(cmd_surface),
			/*SVGA_CB_SYNC*/0, 0
		);
	}

	if(sinfo->gmrId && sinfo->gmrMngt)
	{
		SVGARegionDestroy(svga, sinfo->gmrId);
	}

	SVGASurfaceIDFree(svga, sid);
}

/* destroy the interface */
void SVGADestroy(svga_inst_t *svga)
{
	SVGA_ASSERT;
	int i;

	if(svga->ctx_id)
	{
		SVGAContextDestroy(svga, svga->ctx_id);
		svga->ctx_id = 0;
	}

	for(i = 0; i < CMD_BUFFER_COUNT; i++)
	{
		if(svga->cmd_buf[i] != NULL)
		{
			SVGA_CMB_free(svga->cmd_buf[i]);
		}
	}
}

/* create GMR */
static uint32_t SVGARegionCreateLimit(svga_inst_t *svga, uint32_t size, uint32_t *user_page, uint32_t start_id, BOOL mobonly)
{
	SVGA_ASSERT;

	uint32_t rid = SVGAGMRIDNext(svga, start_id);

	assert(rid > 0);

	SVGA_region_info_t *gmr = &(SVGAGMRIDInfo(svga, rid)->info);
	gmr->region_id = rid;
	gmr->size      = size;
	gmr->mobonly   = mobonly ? 1 : 0;

	if(!SVGA_region_create(gmr))
	{
		GUIError(svga, "Failed to allocate physical RAM space (%d bytes). Please attach more memory to VM or reboot guest OS.", size);
		return 0;
	}

	if(user_page)
	{
		*user_page = (DWORD)gmr->address;
	}

	return rid;
}

uint32_t SVGARegionCreate(svga_inst_t *svga, uint32_t size, uint32_t *user_page)
{
	if(svga->dx)
	{
		/* we're keeping some IDs for out internal frame buffer rendering */
		return SVGARegionCreateLimit(svga, size, user_page, 5, TRUE/*FALSE*/);
	}

	return SVGARegionCreateLimit(svga, size, user_page, 1, FALSE);
}

/* destroy the GMR */
void SVGARegionDestroy(svga_inst_t *svga, uint32_t regionId)
{
	SVGA_ASSERT;

	SVGA_DB_region_t *info = SVGAGMRIDInfo(svga, regionId);

	if(!info)
	{
		return;
	}

	SVGA_region_info_t *gmr = &(info->info);

	SVGA_region_free(gmr);

	SVGAGMRIDFree(svga, regionId);
}

/* read all hardware registers, fifo register and caps list to VBOXGAHWINFO struct */
BOOL SVGAReadHwInfo(svga_inst_t *svga, VBOXGAHWINFO *pHwInfo)
{
	int i;

	SVGA_ASSERT;

	pHwInfo->u32HwType = VBOX_GA_HW_TYPE_VMSVGA;
	pHwInfo->u.svga.cbInfoSVGA = sizeof(VBOXGAHWINFOSVGA);
	SVGA_query_vector(SVGA_QUERY_REGS, 0, GA_HWINFO_REGS, (DWORD*)&(pHwInfo->u.svga.au32Regs[0]));
	SVGA_query_vector(SVGA_QUERY_FIFO, 0, GA_HWINFO_REGS, (DWORD*)&(pHwInfo->u.svga.au32Fifo[0]));
	SVGA_query_vector(SVGA_QUERY_CAPS, 0, GA_HWINFO_REGS, (DWORD*)&(pHwInfo->u.svga.au32Caps[0]));

#if 0
  /* dump caps */
	for(i = 0; i < 512; i += 8)
	{
		printf("C: %08X %08X %08X %08X %08X %08X %08X %08X\n",
		 pHwInfo->u.svga.au32Caps[i+0],
		 pHwInfo->u.svga.au32Caps[i+1],
		 pHwInfo->u.svga.au32Caps[i+2],
		 pHwInfo->u.svga.au32Caps[i+3],
		 pHwInfo->u.svga.au32Caps[i+4],
		 pHwInfo->u.svga.au32Caps[i+5],
		 pHwInfo->u.svga.au32Caps[i+6],
		 pHwInfo->u.svga.au32Caps[i+7]);
	}
#endif

	return TRUE;
}

/* clear all allocated resource identified by PID (or current process id, if pid is 0) */
void SVGACleanup(svga_inst_t *svga, uint32_t pid)
{
	svga_inst_t lsvga;
	uint32_t id;
	BOOL free_res = FALSE;

	if(svga == NULL)
	{
		if(!SVGACreate(&lsvga))
		{
			return;
		}
		svga = &lsvga;
		free_res = TRUE;
	}

	if(pid == 0)
	{
		pid = svga->pid;
	}

	debug_printf("Cleaning surfaces...");
	while((id = SVGASurfaceIDPop(svga, pid)) != 0)
	{
		debug_printf("Cleaning surface: %d\n", id);
		SVGASurfaceDestroy(svga, id, NULL);
	}

	debug_printf("Cleaning contextes...");
	while((id = SVGAContextIDPop(svga, pid)) != 0)
	{
		debug_printf("Cleaning context: %d\n", id);
		SVGAContextDestroy(svga, id);
	}

	debug_printf("Cleaning GMRs...");
	while((id = SVGAGMRIDPop(svga, pid)) != 0)
	{
		debug_printf("Cleaning region: %d\n", id);
		SVGARegionDestroy(svga, id);
	}

	if(free_res)
	{
		SVGADestroy(svga);
	}
}

static int format_to_bpp(SVGA3dSurfaceFormat type)
{
  switch(type)
  {
    case SVGA3D_FORMAT_INVALID:
   	  return 0;
    case SVGA3D_X8R8G8B8:
    case SVGA3D_A8R8G8B8:
   	  return 32;
    case SVGA3D_R5G6B5:
    case SVGA3D_X1R5G5B5:
    case SVGA3D_A1R5G5B5:
    case SVGA3D_A4R4G4B4:
   	  return 16;
    case SVGA3D_Z_D32:
   	  return 32;
    case SVGA3D_Z_D16:
   	  return 16;
    case SVGA3D_Z_D24S8:
   	  return 32;
    case SVGA3D_Z_D15S1:
   	  return 16;
    case SVGA3D_LUMINANCE8:
    case SVGA3D_LUMINANCE4_ALPHA4:
    	return 8;
    case SVGA3D_LUMINANCE16:
    case SVGA3D_LUMINANCE8_ALPHA8:
			return 16;
    case SVGA3D_DXT1:
    case SVGA3D_DXT2:
    	return 4;
    case SVGA3D_DXT3:
    case SVGA3D_DXT4:
    case SVGA3D_DXT5:
    	return 8;
    case SVGA3D_BUMPU8V8:
    case SVGA3D_BUMPL6V5U5:
    	return 16;
    case SVGA3D_BUMPX8L8V8U8:
    	return 32;
/*    case SVGA3D_BUMPL8V8U8:
    	return 24;*/
    case SVGA3D_ARGB_S10E5:
      return 16;
    case SVGA3D_ARGB_S23E8:
    	return 32;

    case SVGA3D_A2R10G10B10:
    	return 32;

   /* signed formats */
    case SVGA3D_V8U8:
    	return 16;
    case SVGA3D_Q8W8V8U8:
    	return 32;
    case SVGA3D_CxV8U8:
    	return 16;

   /* mixed formats */
    case SVGA3D_X8L8V8U8:
    	return 32;
    case SVGA3D_A2W10V10U10:
    	return 32;

    case SVGA3D_ALPHA8:
    	return 8;

   /* Single- and dual-component floating point formats */
    case SVGA3D_R_S10E5:
    	return 16;
    case SVGA3D_R_S23E8:
    	return 32;
    case SVGA3D_RG_S10E5:
    	return 16;
    case SVGA3D_RG_S23E8:
    	return 32;

   /*
    * Any surface can be used as a buffer object, but SVGA3D_BUFFER is
    * the most efficient format to use when creating new surfaces
    * expressly for index or vertex data.
    */

    case SVGA3D_BUFFER:
    	return 32;

    case SVGA3D_Z_D24X8:
    	return 32;

    case SVGA3D_V16U16:
    	return 32;

    case SVGA3D_G16R16:
    	return 32;
    case SVGA3D_A16B16G16R16:
    	return 64;

   /* Packed Video formats */
    case SVGA3D_UYVY:
    case SVGA3D_YUY2:
    	return 32;

   /* Planar video formats */
    case SVGA3D_NV12:
    	return 16;

   /* Video format with alpha */
    case SVGA3D_AYUV:
    	return 32;

    case SVGA3D_BC4_UNORM:
    case SVGA3D_BC5_UNORM:
    	return 8;

   /* Advanced D3D9 depth formats. */
    case SVGA3D_Z_DF16:
    	return 16;
    case SVGA3D_Z_DF24:
    case SVGA3D_Z_D24S8_INT:
    	return 32;

    /* VGPU10 */
		case SVGA3D_B5G6R5_UNORM:
   	case SVGA3D_B5G5R5A1_UNORM:
   		return 16;
   	case SVGA3D_B8G8R8A8_UNORM:
   	case SVGA3D_B8G8R8X8_UNORM:
   		return 32;
	}

	return 32;
}

typedef struct _format_debug_{
	uint32_t id;
	const char *name;
} format_debug_t;

#define FORMAT_ITEM(_e) {_e, #_e},
format_debug_t format_debug_table[] = {
   FORMAT_ITEM(SVGA3D_FORMAT_INVALID)
   FORMAT_ITEM(SVGA3D_X8R8G8B8)
   FORMAT_ITEM(SVGA3D_A8R8G8B8)
   FORMAT_ITEM(SVGA3D_R5G6B5)
   FORMAT_ITEM(SVGA3D_X1R5G5B5)
   FORMAT_ITEM(SVGA3D_A1R5G5B5)
   FORMAT_ITEM(SVGA3D_A4R4G4B4)
   FORMAT_ITEM(SVGA3D_Z_D32)
   FORMAT_ITEM(SVGA3D_Z_D16)
   FORMAT_ITEM(SVGA3D_Z_D24S8)
   FORMAT_ITEM(SVGA3D_Z_D15S1)
   FORMAT_ITEM(SVGA3D_LUMINANCE8)
   FORMAT_ITEM(SVGA3D_LUMINANCE4_ALPHA4)
   FORMAT_ITEM(SVGA3D_LUMINANCE16)
   FORMAT_ITEM(SVGA3D_LUMINANCE8_ALPHA8)
   FORMAT_ITEM(SVGA3D_DXT1)
   FORMAT_ITEM(SVGA3D_DXT2)
   FORMAT_ITEM(SVGA3D_DXT3)
   FORMAT_ITEM(SVGA3D_DXT4)
   FORMAT_ITEM(SVGA3D_DXT5)
   FORMAT_ITEM(SVGA3D_BUMPU8V8)
   FORMAT_ITEM(SVGA3D_BUMPL6V5U5)
   FORMAT_ITEM(SVGA3D_BUMPX8L8V8U8)
   FORMAT_ITEM(SVGA3D_FORMAT_DEAD1)
   FORMAT_ITEM(SVGA3D_ARGB_S10E5)
   FORMAT_ITEM(SVGA3D_ARGB_S23E8)
   FORMAT_ITEM(SVGA3D_A2R10G10B10)
   FORMAT_ITEM(SVGA3D_V8U8)
   FORMAT_ITEM(SVGA3D_Q8W8V8U8)
   FORMAT_ITEM(SVGA3D_CxV8U8)
   FORMAT_ITEM(SVGA3D_X8L8V8U8)
   FORMAT_ITEM(SVGA3D_A2W10V10U10)
   FORMAT_ITEM(SVGA3D_ALPHA8)
   FORMAT_ITEM(SVGA3D_R_S10E5)
   FORMAT_ITEM(SVGA3D_R_S23E8)
   FORMAT_ITEM(SVGA3D_RG_S10E5)
   FORMAT_ITEM(SVGA3D_RG_S23E8)
   FORMAT_ITEM(SVGA3D_BUFFER)
   FORMAT_ITEM(SVGA3D_Z_D24X8)
   FORMAT_ITEM(SVGA3D_V16U16)
   FORMAT_ITEM(SVGA3D_G16R16)
   FORMAT_ITEM(SVGA3D_A16B16G16R16)
   FORMAT_ITEM(SVGA3D_UYVY)
   FORMAT_ITEM(SVGA3D_YUY2)
   FORMAT_ITEM(SVGA3D_NV12)
   FORMAT_ITEM(SVGA3D_AYUV)
   FORMAT_ITEM(SVGA3D_R32G32B32A32_TYPELESS)
   FORMAT_ITEM(SVGA3D_R32G32B32A32_UINT)
   FORMAT_ITEM(SVGA3D_R32G32B32A32_SINT)
   FORMAT_ITEM(SVGA3D_R32G32B32_TYPELESS)
   FORMAT_ITEM(SVGA3D_R32G32B32_FLOAT)
   FORMAT_ITEM(SVGA3D_R32G32B32_UINT)
   FORMAT_ITEM(SVGA3D_R32G32B32_SINT)
   FORMAT_ITEM(SVGA3D_R16G16B16A16_TYPELESS)
   FORMAT_ITEM(SVGA3D_R16G16B16A16_UINT)
   FORMAT_ITEM(SVGA3D_R16G16B16A16_SNORM)
   FORMAT_ITEM(SVGA3D_R16G16B16A16_SINT)
   FORMAT_ITEM(SVGA3D_R32G32_TYPELESS)
   FORMAT_ITEM(SVGA3D_R32G32_UINT)
   FORMAT_ITEM(SVGA3D_R32G32_SINT)
   FORMAT_ITEM(SVGA3D_R32G8X24_TYPELESS)
   FORMAT_ITEM(SVGA3D_D32_FLOAT_S8X24_UINT)
   FORMAT_ITEM(SVGA3D_R32_FLOAT_X8X24)
   FORMAT_ITEM(SVGA3D_X32_G8X24_UINT)
   FORMAT_ITEM(SVGA3D_R10G10B10A2_TYPELESS)
   FORMAT_ITEM(SVGA3D_R10G10B10A2_UINT)
   FORMAT_ITEM(SVGA3D_R11G11B10_FLOAT)
   FORMAT_ITEM(SVGA3D_R8G8B8A8_TYPELESS)
   FORMAT_ITEM(SVGA3D_R8G8B8A8_UNORM)
   FORMAT_ITEM(SVGA3D_R8G8B8A8_UNORM_SRGB)
   FORMAT_ITEM(SVGA3D_R8G8B8A8_UINT)
   FORMAT_ITEM(SVGA3D_R8G8B8A8_SINT)
   FORMAT_ITEM(SVGA3D_R16G16_TYPELESS)
   FORMAT_ITEM(SVGA3D_R16G16_UINT)
   FORMAT_ITEM(SVGA3D_R16G16_SINT)
   FORMAT_ITEM(SVGA3D_R32_TYPELESS)
   FORMAT_ITEM(SVGA3D_D32_FLOAT)
   FORMAT_ITEM(SVGA3D_R32_UINT)
   FORMAT_ITEM(SVGA3D_R32_SINT)
   FORMAT_ITEM(SVGA3D_R24G8_TYPELESS)
   FORMAT_ITEM(SVGA3D_D24_UNORM_S8_UINT)
   FORMAT_ITEM(SVGA3D_R24_UNORM_X8)
   FORMAT_ITEM(SVGA3D_X24_G8_UINT)
   FORMAT_ITEM(SVGA3D_R8G8_TYPELESS)
   FORMAT_ITEM(SVGA3D_R8G8_UNORM)
   FORMAT_ITEM(SVGA3D_R8G8_UINT)
   FORMAT_ITEM(SVGA3D_R8G8_SINT)
   FORMAT_ITEM(SVGA3D_R16_TYPELESS)
   FORMAT_ITEM(SVGA3D_R16_UNORM)
   FORMAT_ITEM(SVGA3D_R16_UINT)
   FORMAT_ITEM(SVGA3D_R16_SNORM)
   FORMAT_ITEM(SVGA3D_R16_SINT)
   FORMAT_ITEM(SVGA3D_R8_TYPELESS)
   FORMAT_ITEM(SVGA3D_R8_UNORM)
   FORMAT_ITEM(SVGA3D_R8_UINT)
   FORMAT_ITEM(SVGA3D_R8_SNORM)
   FORMAT_ITEM(SVGA3D_R8_SINT)
   FORMAT_ITEM(SVGA3D_P8)
   FORMAT_ITEM(SVGA3D_R9G9B9E5_SHAREDEXP)
   FORMAT_ITEM(SVGA3D_R8G8_B8G8_UNORM)
   FORMAT_ITEM(SVGA3D_G8R8_G8B8_UNORM)
   FORMAT_ITEM(SVGA3D_BC1_TYPELESS)
   FORMAT_ITEM(SVGA3D_BC1_UNORM_SRGB)
   FORMAT_ITEM(SVGA3D_BC2_TYPELESS)
   FORMAT_ITEM(SVGA3D_BC2_UNORM_SRGB)
   FORMAT_ITEM(SVGA3D_BC3_TYPELESS)
   FORMAT_ITEM(SVGA3D_BC3_UNORM_SRGB)
   FORMAT_ITEM(SVGA3D_BC4_TYPELESS)
   FORMAT_ITEM(SVGA3D_ATI1)
   FORMAT_ITEM(SVGA3D_BC4_SNORM)
   FORMAT_ITEM(SVGA3D_BC5_TYPELESS)
   FORMAT_ITEM(SVGA3D_ATI2)
   FORMAT_ITEM(SVGA3D_BC5_SNORM)
   FORMAT_ITEM(SVGA3D_R10G10B10_XR_BIAS_A2_UNORM)
   FORMAT_ITEM(SVGA3D_B8G8R8A8_TYPELESS)
   FORMAT_ITEM(SVGA3D_B8G8R8A8_UNORM_SRGB)
   FORMAT_ITEM(SVGA3D_B8G8R8X8_TYPELESS)
   FORMAT_ITEM(SVGA3D_B8G8R8X8_UNORM_SRGB)
   FORMAT_ITEM(SVGA3D_Z_DF16)
   FORMAT_ITEM(SVGA3D_Z_DF24)
   FORMAT_ITEM(SVGA3D_Z_D24S8_INT)
   FORMAT_ITEM(SVGA3D_YV12)
   FORMAT_ITEM(SVGA3D_R32G32B32A32_FLOAT)
   FORMAT_ITEM(SVGA3D_R16G16B16A16_FLOAT)
   FORMAT_ITEM(SVGA3D_R16G16B16A16_UNORM)
   FORMAT_ITEM(SVGA3D_R32G32_FLOAT)
   FORMAT_ITEM(SVGA3D_R10G10B10A2_UNORM)
   FORMAT_ITEM(SVGA3D_R8G8B8A8_SNORM)
   FORMAT_ITEM(SVGA3D_R16G16_FLOAT)
   FORMAT_ITEM(SVGA3D_R16G16_UNORM)
   FORMAT_ITEM(SVGA3D_R16G16_SNORM)
   FORMAT_ITEM(SVGA3D_R32_FLOAT)
   FORMAT_ITEM(SVGA3D_R8G8_SNORM)
   FORMAT_ITEM(SVGA3D_R16_FLOAT)
   FORMAT_ITEM(SVGA3D_D16_UNORM)
   FORMAT_ITEM(SVGA3D_A8_UNORM)
   FORMAT_ITEM(SVGA3D_BC1_UNORM)
   FORMAT_ITEM(SVGA3D_BC2_UNORM)
   FORMAT_ITEM(SVGA3D_BC3_UNORM)
   FORMAT_ITEM(SVGA3D_B5G6R5_UNORM)
   FORMAT_ITEM(SVGA3D_B5G5R5A1_UNORM)
   FORMAT_ITEM(SVGA3D_B8G8R8A8_UNORM)
   FORMAT_ITEM(SVGA3D_B8G8R8X8_UNORM)
   FORMAT_ITEM(SVGA3D_BC4_UNORM)
   FORMAT_ITEM(SVGA3D_BC5_UNORM)
   0, NULL
};

#undef FORMAT_ITEM

const char *get_format_name(SVGA3dSurfaceFormat e)
{
	format_debug_t *item = &format_debug_table[0];

	while(item->name != NULL)
	{
		if(e == item->id)
		{
			return item->name;
		}
		item++;
	}

	return "Unknown format";
}

static BOOL set_fb_gmr(svga_inst_t *svga, uint32_t render_width, uint32_t render_height)
{
	uint32_t softblit_minsize = vramcpy_calc_framebuffer(render_width, render_height, 32);

	if(!svga->softblit_gmr_id || softblit_minsize > svga->softblit_gmr_size)
	{
		if(svga->softblit_gmr_id)
		{
			SVGARegionDestroy(svga, svga->softblit_gmr_id);
			svga->softblit_gmr_id = 0;
		}

		svga->softblit_gmr_size = softblit_minsize;
		svga->softblit_gmr_id = SVGARegionCreateLimit(svga, svga->softblit_gmr_size, (uint32_t*)(&svga->softblit_gmr_ptr), 1, FALSE);
		svga_printf(svga, "Created new display region: %d (%p)", svga->softblit_gmr_id, svga->softblit_gmr_ptr);
		if(!svga->softblit_gmr_id)
		{
				/* GMR allocation error */
				return FALSE;
		}
	}

	return TRUE;
}

/* create or recreate surface with same size as renderer but same format as screen
 *
 */
static BOOL set_fb_blitsid(svga_inst_t *svga, uint32_t render_width, uint32_t render_height, uint32_t screen_bpp)
{
	if(svga->blitsid != 0 && svga->blitsid != SVGA3D_INVALID_ID)
	{
		SVGA_DB_surface_t *sinfo = SVGASurfaceIDInfo(svga, svga->blitsid);
		if(sinfo != NULL)
		{
			if(sinfo->width == render_width &&
				 sinfo->height == render_height &&
				 sinfo->bpp == screen_bpp
				)
			{
				return TRUE;
			}
			else
			{
				SVGASurfaceDestroy(svga, svga->blitsid, NULL);
				svga->blitsid = 0;
			}
		}
	}

	GASURFCREATE createParms = {
		SVGA3D_SURFACE_HINT_TEXTURE | SVGA3D_SURFACE_HINT_RENDERTARGET,
		//SVGA3D_SURFACE_BIND_SHADER_RESOURCE | SVGA3D_SURFACE_BIND_RENDER_TARGET, /* flags */
		SVGA3D_FORMAT_INVALID, /* format */
		0, /* usage */
		{1}, /* mipmaps */
		0, /* size (not count to limit) */
	};

	if(svga->dx)
	{
		switch(screen_bpp)
		{
			case 16:
				//createParms.format = SVGA3D_B5G6R5_UNORM;
				createParms.format = SVGA3D_R5G6B5;
				break;
			case 32:
				createParms.format = SVGA3D_R8G8B8A8_UNORM;
				break;
			default:
				return FALSE;
		}
	}
	else
	{
		switch(screen_bpp)
		{
			case 16:
				createParms.format = SVGA3D_R5G6B5;
				break;
			case 32:
				createParms.format = SVGA3D_A8R8G8B8;
				break;
			default:
				return FALSE;
		}
	}

	GASURFSIZE size =
	{
		render_width, /* width */
		render_height, /* height */
		1 /* depth */
	};

	debug_printf("set_fb_blitsid: %d, %d\n", render_width, render_height);
	return SVGASurfaceCreate(svga, &createParms, &size, 1, &(svga->blitsid));
}

/* FRAMERATE_LIMIT - limits number of frames per second */
DEBUG_GET_ONCE_NUM_OPTION(framerate_limit, "FRAMERATE_LIMIT", 0)

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

void SVGAPresentWinBlt(svga_inst_t *svga, HDC hDC, uint32_t cid, uint32_t sid)
{
	assert(svga);

	frame_wait(svga);

	SVGAPresentWindow(svga, hDC, cid, sid);
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

static uint32_t last_sid_format = -1;

DEBUG_GET_ONCE_BOOL_OPTION(blit_surf_to_screen_enabled, "SVGA_BLIT_SURF_TO_SCREEN", FALSE);
DEBUG_GET_ONCE_BOOL_OPTION(dma_need_reread, "SVGA_DMA_NEED_REREAD", TRUE);

/**
 * Present render to screen/window using direct vram access if its possible.
 * If not calls SVGAPresentWindow.
 *
 **/
void SVGAPresent(svga_inst_t *svga, HDC hDC, uint32_t cid, uint32_t sid)
{
	assert(svga);

	uint32_t bpp = svga->hda->bpp;
  SVGA_DB_surface_t *sinfo = SVGASurfaceIDInfo(svga, sid);

  PIXELFORMATDESCRIPTOR pfd;

  if(sinfo == NULL)
  {
   	return;
  }

  frame_wait(svga);

  if(last_sid_format != sinfo->format)
  {
  	svga_printf(svga, "SVGAPresent = bpp: %d, format: %u", sinfo->bpp, sinfo->format);
  	last_sid_format = sinfo->format;
  }

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
		  	SVGASend(svga, &command, sizeof(command), SVGA_CB_FLAG_DX_CONTEXT, cid);
		  }
		  else
		  {
		  	SVGASend(svga, &command, sizeof(command), 0, 0);
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

		gmr = SVGAGMRIDInfo(svga, sinfo->gmrId)->info.address;
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

				SVGASend(svga, &command_update, sizeof(command_update), 0, 0);
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
			SVGAFinish(svga, 0, 0);
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

	SVGA_DB_surface_t *sinfo = SVGASurfaceIDInfo(svga, sid);
	void *gmr = NULL;

	if(sinfo == NULL || (sinfo->width * sinfo->height) == 0)
	{
		debug_printf("SVGAPresentWindow: null surface info!\n");
		/* Nothing to see here. Please disperse. */
		return;
	}

  if(last_sid_format != sinfo->format)
  {
  	svga_printf(svga, "SVGAPresent = bpp: %d, format: %u", sinfo->bpp, sinfo->format);
  	last_sid_format = sinfo->format;
  }

  const int sbpp = sinfo->bpp;
  const size_t sps = vramcpy_pointsize(sbpp);

	if(sinfo->gmrId == 0) /* old way: copy surface to guest memory and display it */
	{
	  if(!set_fb_gmr(svga, sinfo->width, sinfo->height))
	  {
	  	debug_printf("SVGAPresentWindow: failed to set GMR!\n");
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

		gmr = (void*)SVGAGMRIDInfo(svga, sinfo->gmrId)->info.address;

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

void SVGACompose(svga_inst_t *svga, uint32_t cid, uint32_t srcSid, uint32_t destSid, LPCRECT pRect)
{
#pragma pack(push)
#pragma pack(1)
	struct
	{
		uint32_t type;
		uint32_t size;
		SVGA3dCmdSurfaceCopy surfaceCopy;
		SVGA3dCopyBox box;
	} command;
	struct
	{
		uint32_t type;
		uint32_t size;
		SVGA3dCmdDXSurfaceCopyAndReadback copy;
	} command_dx;
#pragma pack(pop)

	svga_printf(svga, "SVGACompose(-, %d, %d, %d, -)", cid, srcSid, destSid);

	if(!svga->dx)
	{
    command.type = SVGA_3D_CMD_SURFACE_COPY;
    command.size = sizeof(SVGA3dCmdSurfaceCopy) + sizeof(SVGA3dCopyBox);

    command.surfaceCopy.src.sid     = srcSid;
    command.surfaceCopy.src.face    = 0;
    command.surfaceCopy.src.mipmap  = 0;
    command.surfaceCopy.dest.sid    = destSid;
    command.surfaceCopy.dest.face   = 0;
    command.surfaceCopy.dest.mipmap = 0;

    command.box.x    = pRect->left;
    command.box.y    = pRect->top;
    command.box.z    = 0;
    command.box.w    = pRect->right - pRect->left;
    command.box.h    = pRect->bottom - pRect->top;
    command.box.d    = 1;
    command.box.srcx = 0;
    command.box.srcy = 0;
    command.box.srcz = 0;

    SVGASend(svga, &command, sizeof(command), SVGA_CB_SYNC, 0);
	}
	else
	{
		command_dx.type = SVGA_3D_CMD_DX_SURFACE_COPY_AND_READBACK;
		command_dx.size = sizeof(SVGA3dCmdDXSurfaceCopyAndReadback);
		command_dx.copy.srcSid   = srcSid;
		command_dx.copy.destSid  = destSid;
    command_dx.copy.box.x    = pRect->left;
    command_dx.copy.box.y    = pRect->top;
    command_dx.copy.box.z    = 0;
    command_dx.copy.box.w    = pRect->right - pRect->left;
    command_dx.copy.box.h    = pRect->bottom - pRect->top;
    command_dx.copy.box.d    = 1;
    command_dx.copy.box.srcx = 0;
    command_dx.copy.box.srcy = 0;
    command_dx.copy.box.srcz = 0;

    SVGASend(svga, &command_dx, sizeof(command_dx), SVGA_CB_SYNC | SVGA_CB_FLAG_DX_CONTEXT, cid);
	}
}

/* Queue all allocated resource and free them if procces which allocate them
 * is dead. Useful if application ends with crash and this to cleanup
 * on new begining
 * (keep in mind, that driver is near all in userspace)
 */
void SVGAZombieKiller()
{
	svga_inst_t lSvga;
	if(SVGACreate(&lSvga))
	{
		uint32_t state = 0;
		uint32_t pid;
		while(SVGAGetNextPid(&lSvga, &pid, &state))
		{
			debug_printf("checking process: 0x%X\n", pid);

			/*
			 * Windows 9x reusing pid very often, if you re-run crashed process,
			 * you probably have same PID as the deceased
			 */
			if(pid == lSvga.pid)
			{
				SVGACleanup(&lSvga, pid);
			}
			else
			{
				HANDLE proc = OpenProcess(PROCESS_QUERY_INFORMATION, FALSE, pid);
				if(proc != NULL)
				{
					DWORD code;
					if(GetExitCodeProcess(proc, &code))
					{
						if(code != STILL_ACTIVE)
						{
							SVGACleanup(&lSvga, pid);
						}
					}

					CloseHandle(proc);
				}
				else
				{
					SVGACleanup(&lSvga, pid);
				}
			}
		}

		SVGADestroy(&lSvga);
	}
}

BOOL SVGASurfaceCreate(svga_inst_t *svga, GASURFCREATE *pCreateParms, GASURFSIZE *paSizes, uint32_t cSizes, uint32_t *outSid)
{
	const uint32_t cbCmd = sizeof(SVGA3dCmdHeader)
                        + sizeof(SVGA3dCmdDefineSurface_v2)
                        + cSizes * sizeof(SVGA3dSize);

	uint8_t *cmd = (uint8_t *)alloca(cbCmd);

	memset(cmd, 0, cbCmd);

	SVGA3dCmdHeader *header = (SVGA3dCmdHeader*)cmd;
	SVGA3dCmdDefineSurface_v2 *surface = (SVGA3dCmdDefineSurface_v2*)(cmd+sizeof(SVGA3dCmdHeader));

	header->id = SVGA_3D_CMD_SURFACE_DEFINE_V2;
	header->size = cbCmd - sizeof(SVGA3dCmdHeader);

	uint32_t sid = SVGASurfaceIDNext(svga, 1);

	if(sid)
	{
		surface->sid = sid;
		surface->surfaceFlags = pCreateParms->flags;
	  surface->format = (SVGA3dSurfaceFormat)pCreateParms->format;

	  for(int i = 0; i < SVGA3D_MAX_SURFACE_FACES; i++)
	  {
	  	surface->face[i].numMipLevels = pCreateParms->mip_levels[i];
	  }

	  surface->multisampleCount = 0;
	  surface->autogenFilter    = SVGA3D_TEX_FILTER_NONE;

	  SVGA3dSize *siz = (SVGA3dSize*)(cmd+sizeof(SVGA3dCmdHeader)+sizeof(SVGA3dCmdDefineSurface_v2));

	  for(int i = 0; i < cSizes; i++)
	  {
	    siz->width = paSizes->cWidth;
	    siz->height = paSizes->cHeight;
	    siz->depth = paSizes->cDepth;

	  	if(i == 0) /* save only face 0 */
	  	{
	  		SVGA_DB_surface_t *sinfo = SVGASurfaceIDInfo(svga, sid);

	  		sinfo->format = surface->format;
	  		sinfo->bpp    = format_to_bpp(surface->format);
	  		sinfo->width  = siz->width;
	  		sinfo->height = siz->height;
	  		sinfo->gmrId  = 0;
	  		sinfo->gmrMngt = 0;
	  		sinfo->size   = pCreateParms->size;

#if 0
			  {
			  	FILE *fa = fopen("C:\\surf.log", "ab");
			  	fprintf(fa, "%s (%d): %d x %d\r\n", get_format_name(sinfo->format), sinfo->format, sinfo->width, sinfo->height);
			  	fclose(fa);
			  }
#endif
	  	}

	  	paSizes++;
	  	siz++;
	  }

	  SVGASend(svga, cmd, cbCmd, /*SVGA_CB_SYNC*/0, 0);



		*outSid = sid;
		return TRUE;
	}

	return FALSE;
}

BOOL SVGASurfaceGBCreate(svga_inst_t *svga, SVGAGBSURFCREATE *pCreateParms)
{
	if(!svga->dx)
	{
		return FALSE;
	}

#pragma pack(push)
#pragma pack(1)
	struct
	{
		uint32_t type;
		uint32_t size;
		SVGA3dCmdDefineGBSurface_v4 gbsurf;
	} cmd;

	struct
	{
		uint32_t type;
		uint32_t size;
		SVGA3dCmdBindGBSurface bind;
	} cmd_bind;

#pragma pack(pop)

  uint32_t sid = SVGASurfaceIDNext(svga, 1);

  if(sid == 0)
  {
  	return FALSE;
  }

	uint32_t cbGB = 0;
	uint32_t userAddress = 0;
	uint32_t size_round = (pCreateParms->cbGB + PAGE_SIZE - 1) & (~((uint32_t)PAGE_SIZE-1));

#if 0
	{
	 	FILE *fa = fopen("C:\\surfgb.log", "ab");
	 	fprintf(fa, "%s (%d): %d x %d, out: %d\r\n",
	 		get_format_name(pCreateParms->s.format),
	 		pCreateParms->s.format,
	 		pCreateParms->s.size.width,
	 		pCreateParms->s.size.height,
	 		pCreateParms->GMRreturn);
	 	fclose(fa);
	}
#endif

	/* Allocate GMR, if not already supplied. */
	if(pCreateParms->gmrid == SVGA3D_INVALID_ID)
	{
	 	if(SVGARegionsSize(svga) + size_round > svga->gmr_mem_limit)
	 	{
	 		/* forbid to create surface when exceeds memory limit, but mesa still need
	 		   some temporary buffers, allow them
	 		 */
	 		if(!(pCreateParms->s.format == SVGA3D_BUFFER && pCreateParms->s.size.width == 1024*1024))
	 		{
	 			SVGASurfaceIDFree(svga, sid);
		 		return FALSE;
		 	}
	 	}

		pCreateParms->gmrid = SVGARegionCreateLimit(svga, size_round, &userAddress, 10, TRUE);

		if(pCreateParms->gmrid == 0)
		{
			SVGASurfaceIDFree(svga, sid);
			return FALSE;
		}
	}
	else
	{
		cbGB = SVGAGMRIDInfo(svga, pCreateParms->gmrid)->info.size;
		userAddress = (DWORD)SVGAGMRIDInfo(svga, pCreateParms->gmrid)->info.address;
	}

	cmd.type                      = SVGA_3D_CMD_DEFINE_GB_SURFACE_V4;
	cmd.size                      = sizeof(SVGA3dCmdDefineGBSurface_v4);
	cmd.gbsurf.sid                = sid;
	cmd.gbsurf.surfaceFlags       = pCreateParms->s.flags;
	cmd.gbsurf.format             = pCreateParms->s.format;
	cmd.gbsurf.numMipLevels       = pCreateParms->s.numMipLevels;
	cmd.gbsurf.multisampleCount   = pCreateParms->s.sampleCount;
	cmd.gbsurf.multisamplePattern = pCreateParms->s.multisamplePattern;
	cmd.gbsurf.qualityLevel       = pCreateParms->s.qualityLevel;
	cmd.gbsurf.autogenFilter      = SVGA3D_TEX_FILTER_NONE;
	cmd.gbsurf.size               = pCreateParms->s.size;
	cmd.gbsurf.arraySize          = pCreateParms->s.numFaces;
	cmd.gbsurf.bufferByteStride   = 0;

	cmd_bind.type = SVGA_3D_CMD_BIND_GB_SURFACE;
	cmd_bind.size = sizeof(SVGA3dCmdBindGBSurface);
	cmd_bind.bind.sid = sid;
	cmd_bind.bind.mobid = pCreateParms->gmrid;

	SVGAStart(svga);
	SVGAPush(svga, &cmd, sizeof(cmd));
	SVGAPush(svga, &cmd_bind, sizeof(cmd_bind));
	SVGAFinish(svga, /*SVGA_CB_SYNC*/0, 0);

  /* pCreateParms->gmrid;  In/Out: Backing GMR. */
  pCreateParms->cbGB = cbGB; /* Out: Size of backing memory. */
  pCreateParms->userAddress = userAddress; /* Out: R3 mapping of the backing memory */
  pCreateParms->u32Sid  = sid; /* Out: Surface id. */

	SVGA_DB_surface_t *sinfo = SVGASurfaceIDInfo(svga, sid);
	sinfo->format = pCreateParms->s.format;
	sinfo->bpp    = format_to_bpp(pCreateParms->s.format);
	sinfo->width  = pCreateParms->s.size.width;
	sinfo->height = pCreateParms->s.size.height;
	sinfo->gmrId  = pCreateParms->gmrid;
	sinfo->gmrMngt = pCreateParms->GMRreturn ? 0 : 1;
	sinfo->size = pCreateParms->cbGB;

  return TRUE;
}

BOOL SVGASurfaceInfo(svga_inst_t *svga, uint32_t sid, uint32_t *pWidth, uint32_t *pHeight, uint32_t *pBpp, uint32_t *pPitch)
{
	SVGA_DB_surface_t *sinfo = SVGASurfaceIDInfo(svga, sid);

	if(sinfo == NULL || (sinfo->width * sinfo->height) == 0)
	{
		return FALSE;
	}

	*pWidth  = sinfo->width;
	*pHeight = sinfo->height;
	*pBpp    = sinfo->bpp;
	*pPitch  = sinfo->width * vramcpy_pointsize(sinfo->bpp);

	return TRUE;
}

uint32_t SVGARegionSize(svga_inst_t *svga, uint32_t gmrid)
{
	SVGA_DB_region_t *rinfo = SVGAGMRIDInfo(svga, gmrid);

	if(rinfo == NULL) return 0;

	return rinfo->info.size;
}

uint32_t SVGARegionsSize(svga_inst_t *svga)
{
	return svga->db->stat_regions_usage;
}
