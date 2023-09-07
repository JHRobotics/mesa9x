#include <windows.h>
#include <stdint.h>
#include <stdio.h>

#define uint8  uint8_t
#define uint16 uint16_t
#define uint32 uint32_t
#define uint64 uint64_t

#include "wddm_screen.h"
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

/* supported functions: */
#ifndef QUERYESCSUPPORT
#define QUERYESCSUPPORT 8
#endif

#define OPENGL_GETINFO 0x1101

/* neighbor 'known' functions */
#define OPENGL_CMD     0x1100
#define WNDOBJ_SETUP   0x1102

/* debug output from ring-3 application */
#define SVGA_DBG             0x110B

/* new escape codes */
#define SVGA_API             0x110F
#define SVGA_READ_REG        0x1110
#define SVGA_HDA_REQ         0x1112
#define SVGA_REGION_CREATE   0x1114
#define SVGA_REGION_FREE     0x1115
#define SVGA_SYNC            0x1116
#define SVGA_RING            0x1117

#define SVGA_HWINFO_REGS   0x1121
#define SVGA_HWINFO_FIFO   0x1122
#define SVGA_HWINFO_CAPS   0x1123

/* VXD only */
#define SVGA_ALLOCPHY        0x1200
#define SVGA_FREEPHY         0x1201
#define SVGA_OTABLE_QUERY    0x1202
#define SVGA_OTABLE_FLAGS    0x1203

#define SVGA_CB_LOCK         0x1204
#define SVGA_CB_SUBMIT       0x1205
#define SVGA_CB_SYNC         0x1206
#define SVGA_CB_SUBMIT_SYNC  0x1207

#define FLAG_ALLOCATED 1
#define FLAG_ACTIVE    2

/* Actual driver api */
#define DRV_API_LEVEL 20230907UL

#define SVGA_ASSERT assert(svga)
#define VXD_ASSERT assert(svga->vxd)

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

/* get desktop DC */
static HDC SVGAGetDesktopCTX()
{
	HWND hDesktop = GetDesktopWindow();
	return GetDC(hDesktop);
}

#define LOCK_FIFO ULF_LOCK_FIFO
#define LOCK_UL   ULF_LOCK_UL

/* VXD spinlock LOCK */
static BOOL SVGALock(svga_inst_t *svga, uint32_t lock_id)
{
	SVGA_ASSERT;
	
	volatile uint32_t *ptr = svga->hda.userlist_linear + lock_id;
	
	if(ptr)
	{
		__asm volatile (
			"movl %0, %%ecx;"
			"spinlock_try%=:"
			"movl $1, %%eax;"
			"xchgl (%%ecx),%%eax;"
			"testl %%eax,%%eax;"
			"jnz spinlock_try%=;"
			: 
			: "m" (ptr)
			: "%eax", "%ecx"
		);
		return TRUE;
	}
	
	return FALSE;
}

/* VXD spinlock UNLOCK */
static void SVGAUnlock(svga_inst_t *svga, uint32_t lock_id)
{
	SVGA_ASSERT;
	
	volatile uint32_t *ptr = svga->hda.userlist_linear + lock_id;
	
	if(ptr)
	{
		__asm volatile (
			"movl   %0, %%ecx;"
			"xorl   %%eax, %%eax;"
			"xchgl (%%ecx), %%eax;"
			: 
			: "m" (ptr)
			: "%eax", "%ecx"
		);
	}
}

/* Sync vGPU state */
static void SVGACMDSync(svga_inst_t *svga)
{
	SVGA_ASSERT;
	
	/* try 32bit call */
	if(svga->vxd)
	{
		if(DeviceIoControl(svga->vxd, SVGA_SYNC, NULL, 0, NULL, 0, NULL, NULL))
		{
			return;
		}
	}
	
	/* 16bit call */
	ExtEscape(svga->dc, SVGA_SYNC, 0, NULL, 0, NULL);
}

/* Tell the vGPU that is time to do something */
static void SVGACMDRing(svga_inst_t *svga)
{
	SVGA_ASSERT;
	
	/* try 32bit call */
	if(svga->vxd)
	{
		if(DeviceIoControl(svga->vxd, SVGA_RING, NULL, 0, NULL, 0, NULL, NULL))
		{
			return;
		}
	}
	
	/* 16bit call */
	ExtEscape(svga->dc, SVGA_RING, 0, NULL, 0, NULL);
}

#ifdef DEBUG
//#if 1
/* Send debug message to driver */
static void SVGACMDDebug(svga_inst_t *svga, const char *msg)
{
	DWORD len = strlen(msg);
	if(svga->vxd)
	{
		if(DeviceIoControl(svga->vxd, SVGA_DBG, (LPVOID)msg, len+1, NULL, 0, NULL, NULL))
		{
			return;
		}
	}
}

/* send debug info to VXD */
void svga_printf(svga_inst_t *svga, const char *fmt, ...)
{
	char strbuf[512];
  va_list args;
  
  va_start(args, fmt);
  vsprintf(strbuf, fmt, args);
  SVGACMDDebug(svga, strbuf);
  va_end(args);
}
#endif

#if 0
/* These function are here for developing purposes, in release is much better
 * to manipulate with physical memory in RING-0 driver.
 */
static BOOL SVGAAllocPhysical(svga_inst_t *svga, DWORD nPages, void **linear, DWORD *physical)
{
	DWORD in[1] = {nPages};
	DWORD out[2] = {0, 0};
	
	if(svga->vxd)
	{
		if(DeviceIoControl(svga->vxd, SVGA_ALLOCPHY, (LPVOID)in, sizeof(in), (LPVOID)out, sizeof(out), NULL, NULL))
		{
			*linear = (void*)out[0];
			*physical = out[1];
			return TRUE;
		}
	}
	
	return FALSE;
}

static BOOL SVGAFreePhysical(svga_inst_t *svga, void *linear)
{
	DWORD in[1] = {(DWORD)linear};
	
	if(svga->vxd)
	{
		DeviceIoControl(svga->vxd, SVGA_ALLOCPHY, (LPVOID)in, sizeof(in), NULL, 0, NULL, NULL);
	}
}
#endif

/* OTable manipulation */
typedef struct _otinfo_entry_t
{
	DWORD   phy;
	void   *lin;
	DWORD   size;
	DWORD   flags;
} otinfo_entry_t;

/* 
	input:
	 - id 
	output:
	 - linear
	 - physical
	 - size
	 - flags
 */
static BOOL SVGAOTableQuery(svga_inst_t *svga, DWORD id, otinfo_entry_t *outEntry)
{
	DWORD in[1] = {id};
	DWORD out[4] = {0, 0, 0, 0};
	
	if(svga->vxd)
	{
		if(DeviceIoControl(svga->vxd, SVGA_OTABLE_QUERY, (LPVOID)in, sizeof(in), (LPVOID)out, sizeof(out), NULL, NULL))
		{
			outEntry->lin   = (void*)out[0];
			outEntry->phy   = out[1];
			outEntry->size  = out[2];
			outEntry->flags = out[3];
			
			return TRUE;
		}
	}
	
	return FALSE;
}

/* input:
	- id
	- new flags
*/
static void SVGAOTableFlags(svga_inst_t *svga, DWORD id, DWORD flags)
{
	DWORD in[2] = {id, flags};
	
	if(svga->vxd)
	{
		DeviceIoControl(svga->vxd, SVGA_OTABLE_FLAGS, (LPVOID)in, sizeof(in), NULL, 0, NULL, NULL);
	}
}


static BOOL SVGALockCB(svga_inst_t *svga, void **outPtr)
{
	DWORD out[1] = {0};
	
	if(svga->vxd)
	{
		BOOL rv = FALSE;
		if(SVGALock(svga, ULF_LOCK_CB))
		{
			if(DeviceIoControl(svga->vxd, SVGA_CB_LOCK, NULL, 0, out, sizeof(out), NULL, NULL))
			{
				*outPtr = (void*)out[0];
				rv = TRUE;
			}
			SVGAUnlock(svga, ULF_LOCK_CB);
		}
		return rv;
	}
	
	return FALSE;
}

static BOOL SVGASubmitCB(svga_inst_t *svga, void *ptr, uint32_t cbctx_id, BOOL sync)
{
	DWORD in[2] = {(DWORD)ptr, cbctx_id};
	DWORD ctl_id = SVGA_CB_SUBMIT;
	
	if(sync)
	{
		ctl_id = SVGA_CB_SUBMIT_SYNC;
	}
	
	if(svga->vxd)
	{
		BOOL rv;
		
		if(SVGALock(svga, ULF_LOCK_CB))
		{
			rv = DeviceIoControl(svga->vxd, ctl_id, (LPVOID)in, sizeof(in), NULL, 0, NULL, NULL);
			SVGAUnlock(svga, ULF_LOCK_CB);
		}
		
		return rv;
	}
	
	return FALSE;
}

static void SVGASyncCB(svga_inst_t *svga)
{
	if(svga->vxd)
	{
		if(SVGALock(svga, ULF_LOCK_CB))
		{
			DeviceIoControl(svga->vxd, SVGA_CB_SYNC, NULL, 0, NULL, 0, NULL, NULL);
			SVGAUnlock(svga, ULF_LOCK_CB);
		}
	}
}

/* return and allocate first free surface ID */
uint32_t SVGASurfaceIDNext(svga_inst_t *svga)
{
	uint32_t surf_id = 0;
	
	SVGA_ASSERT;
	
	if(SVGALock(svga, LOCK_UL))
	{
		uint32_t id = 0;
		for(id = 1; id < svga->hda.ul_surf_count; id++)
		{
			volatile uint32_t *id_ptr = &svga->hda.userlist_linear[svga->hda.ul_surf_start + id];
			if(*id_ptr == 0)
			{
				*id_ptr = svga->pid;
				surf_id = id;
				
				break;
			}
		}
		SVGAUnlock(svga, LOCK_UL);
	} // lock
	
	return surf_id;
}

/* return first surface ID allocated by 'ident' */
uint32_t SVGASurfaceIDPop(svga_inst_t *svga, uint32_t ident)
{
	uint32_t surf_id = 0;
	
	SVGA_ASSERT;
	
	if(ident == 0)
	{
		ident = svga->pid;
	}
		
	if(SVGALock(svga, LOCK_UL))
	{
		uint32_t id = 0;
		for(id = 1; id < svga->hda.ul_surf_count; id++)
		{
			volatile uint32_t *id_ptr = &svga->hda.userlist_linear[svga->hda.ul_surf_start + id];
			if(*id_ptr == ident)
			{
				surf_id = id;
				break;
			}
		}
		SVGAUnlock(svga, LOCK_UL);
	} // lock
	
	return surf_id;
}

/* free surface ID */
void SVGASurfaceIDFree(svga_inst_t *svga, uint32_t sid)
{
	SVGA_ASSERT;
	
	if(SVGALock(svga, LOCK_UL))
	{
		svga->hda.userlist_linear[svga->hda.ul_surf_start + sid] = 0;
		SVGAUnlock(svga, LOCK_UL);
	} // lock
	
}

#define CTX_INDEX_PID     0
#define CTX_INDEX_COTABLE 1
#define CTX_INDEX_CNT     2

/* return and allocate first free context ID  */
uint32_t SVGAContextIDNext(svga_inst_t *svga)
{
	uint32_t ctx_id = 0;
	
	SVGA_ASSERT;
	
	if(SVGALock(svga, LOCK_UL))
	{
		uint32_t id = 0;
		for(id = 1; id < svga->hda.ul_ctx_count; id++)
		{
			volatile uint32_t *id_ptr = &svga->hda.userlist_linear[svga->hda.ul_ctx_start + CTX_INDEX_CNT*id];
			if(*id_ptr == 0)
			{
				*id_ptr = svga->pid;
				ctx_id = id;
				
				break;
			}
		}
		SVGAUnlock(svga, LOCK_UL);
	} // lock
	
	return ctx_id;
}

/* free context ID */
void SVGAContextIDFree(svga_inst_t *svga, uint32_t ctx_id)
{
	SVGA_ASSERT;
	
	if(SVGALock(svga, LOCK_UL))
	{
		svga->hda.userlist_linear[svga->hda.ul_ctx_start + ctx_id*CTX_INDEX_CNT] = 0;
		SVGAUnlock(svga, LOCK_UL);
	} // lock
}

/* return first context ID identified by 'ident' */
uint32_t SVGAContextIDPop(svga_inst_t *svga, uint32_t ident)
{
	uint32_t ctx_id = 0;
	
	SVGA_ASSERT;
	
	if(ident == 0)
	{
		ident = svga->pid;
	}
		
	if(SVGALock(svga, LOCK_UL))
	{
		uint32_t id = 0;
		for(id = 1; id < svga->hda.ul_ctx_count; id++)
		{
			volatile uint32_t *id_ptr = &svga->hda.userlist_linear[svga->hda.ul_ctx_start + id*CTX_INDEX_CNT];
			if(*id_ptr == ident)
			{
				ctx_id = id;
				break;
			}
		}
		SVGAUnlock(svga, LOCK_UL);
	} // lock
	
	return ctx_id;
}

/* set extra data for context */
void SVGAContextInfoSet(svga_inst_t *svga, uint32_t cid, uint32_t index, uint32_t data)
{
	SVGA_ASSERT;
	
	assert(index >= 1 && index < CTX_INDEX_CNT);
	
	if(SVGALock(svga, LOCK_UL))
	{
		volatile uint32_t *id_ptr = &svga->hda.userlist_linear[svga->hda.ul_ctx_start + cid*CTX_INDEX_CNT];
		id_ptr[index] = data;
		
		SVGAUnlock(svga, LOCK_UL);
	} // lock
}

/* get extra data for context */
uint32_t SVGAContextInfoGet(svga_inst_t *svga, uint32_t cid, uint32_t index)
{
	uint32_t data = 0;
	
	SVGA_ASSERT;
	
	assert(index >= 0 && index < CTX_INDEX_CNT);
	
	if(SVGALock(svga, LOCK_UL))
	{
		volatile uint32_t *id_ptr = &svga->hda.userlist_linear[svga->hda.ul_ctx_start + cid*CTX_INDEX_CNT];
		data = id_ptr[index];
		
		SVGAUnlock(svga, LOCK_UL);
	} // lock
	
	return data;
}

#define GMR_INDEX_PID     0
#define GMR_INDEX_ADDRESS 1
#define GMR_INDEX_SIZE    2
#define GMR_INDEX_PGBLK   3
#define GMR_INDEX_MOBADDR 4
#define GMR_INDEX_MOBPPN  5
#define GMR_INDEX_CNT     6


/* return and allocate first free GMR (graphic memory region) ID */
uint32_t SVGAGMRIDNext(svga_inst_t *svga)
{
	uint32_t gmr_id = 0;
	
	SVGA_ASSERT;
	
	if(SVGALock(svga, LOCK_UL))
	{
		uint32_t id = 0;
		for(id = 1; id < svga->hda.ul_gmr_count; id++)
		{
			volatile uint32_t *id_ptr = &svga->hda.userlist_linear[svga->hda.ul_gmr_start + id*GMR_INDEX_CNT];
			if(*id_ptr == 0)
			{
				*id_ptr = svga->pid;
				gmr_id = id;
				
				break;
			}
		}
		SVGAUnlock(svga, LOCK_UL);
	} // lock
	
	return gmr_id;
}

/* set memory address for GMR ID */
void SVGAGMRInfoSet(svga_inst_t *svga, uint32_t rid, uint32_t index, uint32_t data)
{
	SVGA_ASSERT;
	
	assert(index >= 1 && index < GMR_INDEX_CNT);
	
	if(SVGALock(svga, LOCK_UL))
	{
		volatile uint32_t *id_ptr = &svga->hda.userlist_linear[svga->hda.ul_gmr_start + rid*GMR_INDEX_CNT];
		id_ptr[index] = data;
		
		SVGAUnlock(svga, LOCK_UL);
	} // lock
}

/* get memory address for GMR ID */
uint32_t SVGAGMRInfoGet(svga_inst_t *svga, uint32_t rid, uint32_t index)
{
	uint32_t data = 0;
	
	SVGA_ASSERT;
	
	assert(index >= 0 && index < GMR_INDEX_CNT);
	
	if(SVGALock(svga, LOCK_UL))
	{
		volatile uint32_t *id_ptr = &svga->hda.userlist_linear[svga->hda.ul_gmr_start + rid*GMR_INDEX_CNT];
		data = id_ptr[index];
		
		SVGAUnlock(svga, LOCK_UL);
	} // lock
	
	return data;
}

/* free GMR ID */
void SVGAGMRIDFree(svga_inst_t *svga, uint32_t rid)
{
	SVGA_ASSERT;
	
	if(SVGALock(svga, LOCK_UL))
	{
		svga->hda.userlist_linear[svga->hda.ul_gmr_start + rid*GMR_INDEX_CNT] = 0;
		SVGAUnlock(svga, LOCK_UL);
	} // lock
}

/* return first GMR ID identified by 'ident' */
uint32_t SVGAGMRIDPop(svga_inst_t *svga, uint32_t ident)
{
	uint32_t gmr_id = 0;
	
	SVGA_ASSERT;
	
	if(ident == 0)
	{
		ident = svga->pid;
	}
		
	if(SVGALock(svga, LOCK_UL))
	{
		uint32_t id = 0;
		for(id = 1; id < svga->hda.ul_gmr_count; id++)
		{
			volatile uint32_t *id_ptr = &svga->hda.userlist_linear[svga->hda.ul_gmr_start + id*GMR_INDEX_CNT];
			if(*id_ptr == ident)
			{
				gmr_id = id;
				break;
			}
		}
		SVGAUnlock(svga, LOCK_UL);
	} // lock
	
	return gmr_id;
}

/* return last used fence ID, 0 if no fence ever used */
uint32_t SVGAFenceIDCur(svga_inst_t *svga)
{
	uint32_t fence_id;
	
	if(SVGALock(svga, LOCK_UL))
	{
		fence_id = svga->hda.userlist_linear[svga->hda.ul_fence_index];
		SVGAUnlock(svga, LOCK_UL);
	} // lock
	
	return fence_id;
}

/* allocate next fence id */
uint32_t SVGAFenceIDNext(svga_inst_t *svga)
{
	uint32_t fence_id;
	
	if(SVGALock(svga, LOCK_UL))
	{
		volatile uint32_t *ptr = &svga->hda.userlist_linear[svga->hda.ul_fence_index];
		(*ptr)++;
		if(*ptr == 0)
		{
			*ptr = fence_id = 1;
		}
		else
		{
			fence_id = *ptr;
		}
		
		SVGAUnlock(svga, LOCK_UL);
	} // lock
	
	return fence_id;
}

/* lookup for ident/PID, position in buffer is stored in state */
BOOL SVGAGetNextPid(svga_inst_t *svga, uint32_t *pid, uint32_t *state)
{
	uint32_t fpid = 0;
	do
	{
		if(*state >= svga->hda.userlist_length)
		{
			break;
		}
		
		if((*state) >= svga->hda.ul_gmr_start &&
			(*state) < (svga->hda.ul_gmr_start+svga->hda.ul_gmr_count*GMR_INDEX_CNT) &&
			(((*state)-svga->hda.ul_gmr_start) % GMR_INDEX_CNT) == 0)
		{
			fpid = svga->hda.userlist_linear[(*state)];
		}
		else if((*state) >= svga->hda.ul_ctx_start &&
			(*state) < (svga->hda.ul_ctx_start+svga->hda.ul_ctx_count*CTX_INDEX_CNT) &&
			(((*state)-svga->hda.ul_ctx_start) % CTX_INDEX_CNT) == 0)
		{
			fpid = svga->hda.userlist_linear[(*state)];
		}
		else if((*state) >= svga->hda.ul_surf_start && (*state) < (svga->hda.ul_surf_start+svga->hda.ul_surf_count))
		{
			fpid = svga->hda.userlist_linear[(*state)];
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

/* inicialize user list if needed
 * Because of non linear addresing in PM16 is easier (and much faster) do here
 * in user mode
 */
void SVGAUserlistInit(svga_inst_t *svga)
{
	if(SVGALock(svga, LOCK_UL))
	{
		/* check if uset list is dirty */
		if(svga->hda.userlist_linear[ULF_DIRTY] == 0xFFFFFFFFUL)
		{
			/* is dirty, zero it! */
			memset((void*)&svga->hda.userlist_linear[svga->hda.ul_fence_index], 0, 
				(svga->hda.userlist_length - svga->hda.ul_fence_index) * sizeof(uint32_t));
			
			svga->hda.userlist_linear[ULF_DIRTY] = 0;
		}
		
		SVGAUnlock(svga, LOCK_UL);
	} // lock
	
	debug_printf("userlist:\n  ul_flags_index: %d\n  ul_fence_index: %d\n  ul_gmr_start: %d\n  ul_gmr_count: %d\n"
	  "  ul_ctx_start: %d\n  ul_ctx_count: %d\n  ul_surf_start %d\n  ul_surf_count: %d\n userlist_length: %d\n", 
	  svga->hda.ul_flags_index, 
	  svga->hda.ul_fence_index,
	  svga->hda.ul_gmr_start,
	  svga->hda.ul_gmr_count,
	  svga->hda.ul_ctx_start,
	  svga->hda.ul_ctx_count,
	  svga->hda.ul_surf_start,
	  svga->hda.ul_surf_count,
	  svga->hda.userlist_length
	);	
}

/* sync hardware state = drain FIFO and re-read all registers */
void SVGAFullSync(svga_inst_t *svga)
{
	SVGACMDSync(svga);
}

/* called when is FIFO full, so this full sync to drain it
 * TODO: is full sync necessarily? Maybe we could send 'RING' and wait for while
 */
static void FIFOFull(svga_inst_t *svga)
{
	SVGAFullSync(svga);
}

/* check fifo campatibility */
static BOOL HasFIFOCap(volatile uint32_t *fifo, uint32_t cap)
{
	return (fifo[SVGA_FIFO_CAPABILITIES] & cap) != 0;
}

/* physical write command to fifo, don't call this directly */
static void FifoWriteCopy(volatile uint32_t *fifo, uint32_t *cmd_buf, size_t cmd_bytes)
{
	BOOL reserveable = HasFIFOCap(fifo, SVGA_FIFO_CAP_RESERVE);
	BOOL commited = FALSE;
	
	uint32_t max = fifo[SVGA_FIFO_MAX];
	uint32_t min = fifo[SVGA_FIFO_MIN];
	uint32_t nextCmd = fifo[SVGA_FIFO_NEXT_CMD];
	uint32_t stop = fifo[SVGA_FIFO_STOP];
	
	if(reserveable)
	{
		/* copy as two chunks */
		uint32 chunkSize = MIN(cmd_bytes, max - nextCmd);
		fifo[SVGA_FIFO_RESERVED] = cmd_bytes;

		if(chunkSize > 0)
		{
			memcpy(((uint8_t *)fifo) + nextCmd, cmd_buf, chunkSize);
		}
		
		if(cmd_bytes - chunkSize > 0)
		{
			memcpy(((uint8_t *)fifo) + min, ((uint8_t *)cmd_buf) + chunkSize, cmd_bytes - chunkSize);
		}
		
		nextCmd += cmd_bytes;
		if(nextCmd >= max)
		{
			nextCmd -= max - min;
		}
	}
	else
	{
		/* copy word by word */
		uint32_t *dword = cmd_buf;
						
		while(cmd_bytes > 0)
		{
			fifo[nextCmd / sizeof(uint32)] = *dword++;
			nextCmd += sizeof(uint32);
			if (nextCmd >= max)
			{
				nextCmd = min;
			}
			fifo[SVGA_FIFO_NEXT_CMD] = nextCmd;
			cmd_bytes -= sizeof(uint32);
			break;
		}
		commited = TRUE;
	}
	
	if(!commited)
	{
		fifo[SVGA_FIFO_NEXT_CMD] = nextCmd;
		fifo[SVGA_FIFO_RESERVED] = 0;
	}
}

/* write command to fifo */
static BOOL FifoWrite(svga_inst_t *svga, uint32_t *cmd_buf, size_t cmd_bytes)
{
	DWORD wait_rc;
	
	SVGA_ASSERT;
	assert((cmd_bytes % 4) == 0);
	
	if(SVGALock(svga, LOCK_FIFO))
	{
		volatile uint32 *fifo = svga->hda.fifo_linear;
		uint32_t max = fifo[SVGA_FIFO_MAX];
		uint32_t min = fifo[SVGA_FIFO_MIN];
		uint32_t nextCmd = fifo[SVGA_FIFO_NEXT_CMD];
//		BOOL reserveable = SVGA_HasFIFOCap(SVGA_FIFO_CAP_RESERVE);
//		BOOL commited = FALSE;
		
		for(;;)
		{
			uint32_t stop = fifo[SVGA_FIFO_STOP];
			
			if(nextCmd >= stop)
			{
		    /* There is no valid FIFO data between nextCmd and max */
				if(nextCmd + cmd_bytes < max || (nextCmd + cmd_bytes == max && stop > min))
				{
					/*  There is already enough contiguous space between nextCmd and max (the end of the buffer) */
					FifoWriteCopy(fifo, cmd_buf, cmd_bytes);
					break;
				}
				else if ((max - nextCmd) + (stop - min) <= cmd_bytes)
				{
					FIFOFull(svga);
				}
				else
				{
					/* Data fits in FIFO but only if we split it. */
					FifoWriteCopy(fifo, cmd_buf, cmd_bytes);
					break;
				}
			}
			else
			{
				/* There is FIFO data between nextCmd and max */
				if(nextCmd + cmd_bytes < stop)
				{
					/* There is already enough contiguous space between nextCmd and stop */
					FifoWriteCopy(fifo, cmd_buf, cmd_bytes);
					break;
				}
				else
				{
					FIFOFull(svga);
				}
			}
		} // forever
				
		SVGAUnlock(svga, LOCK_FIFO);
	} // lock
	
	return TRUE;
}


/* check if escape command is supported by driver */
static int IsSupportedEsc(HDC gdi_ctx, int code)
{
	int test = 0;
	DWORD inData = code;
	
	test = ExtEscape(gdi_ctx, QUERYESCSUPPORT, sizeof(DWORD), (LPCSTR)&inData, 0, NULL);
	return test;
}

/* verify if all driver components are on same API */
BOOL SVGAVerifyDriver(HDC gdi_ctx, uint32_t *drv_ver, uint32_t *vxd_ver)
{
	uint32_t vers[2] = {0, 0};
	
	if(ExtEscape(gdi_ctx, SVGA_API, 0, NULL, sizeof(vers), (LPSTR)vers) != 0)
	{
		if(drv_ver)
		{
			*drv_ver = vers[0];
		}
		
		if(vxd_ver)
		{
			*vxd_ver = vers[1];
		}
		
		return vers[0] == DRV_API_LEVEL && vers[1] == DRV_API_LEVEL;
	}
	
	return FALSE;
}

/* check if driver (16bit) supports minimal of command set */
BOOL IsSVGAHW(HDC gdi_ctx)
{
	size_t i;
	const int codes[] = {
		SVGA_HWINFO_REGS,
		SVGA_HWINFO_FIFO,
		SVGA_HWINFO_CAPS,
		SVGA_API,
		0
	};
	
	for(i = 0; i < (sizeof(codes)/sizeof(int)); i++)
	{
		if(codes[i] == 0)
		{
			break;
		}
		
		if(IsSupportedEsc(gdi_ctx, codes[i]) == 0)
		{
			return FALSE;
		}
	}
	
	return TRUE;
}

/* check if graphical driver is realy SVGA and supporting all commands and is 3D campatible */
BOOL IsSVGA(HDC gdi_ctx)
{
	size_t i;
	const int codes[] = {
		SVGA_READ_REG,
		SVGA_HDA_REQ,
		SVGA_REGION_CREATE,
		SVGA_REGION_FREE,
		SVGA_SYNC,
		SVGA_RING,
		0
	};
	
	if(!IsSVGAHW(gdi_ctx))
	{
		return FALSE;
	}
	
	if(!SVGAVerifyDriver(gdi_ctx, NULL, NULL))
	{
		return FALSE;
	}
	
	for(i = 0; i < (sizeof(codes)/sizeof(int)); i++)
	{
		if(codes[i] == 0)
		{
			break;
		}
		
		if(IsSupportedEsc(gdi_ctx, codes[i]) == 0)
		{
			return FALSE;
		}
	}
	
	return TRUE;
}

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
	
	otinfo_entry_t entry;
	for(i = SVGA_OTABLE_MOB; i < SVGA_OTABLE_DX_MAX; i++)
	{
		if(!SVGAOTableQuery(svga, i, &entry))
		{
			return FALSE;
		}
		
		if((entry.flags & FLAG_ACTIVE) == 0)
		{
			if(entry.size > 0)
			{
				cmd.cmd = SVGA_3D_CMD_SET_OTABLE_BASE;
				cmd.size = sizeof(SVGA3dCmdSetOTableBase);
				cmd.otable.type = i;
				cmd.otable.baseAddress = entry.phy / PAGE_SIZE;
				cmd.otable.sizeInBytes = entry.size;
			  cmd.otable.validSizeInBytes = 0;
			  cmd.otable.ptDepth = SVGA3D_MOBFMT_RANGE;
			  
			  SVGAFifoWrite(svga, &cmd, sizeof(cmd));
			  debug_printf("setting OTable: %d!\n", i);
			  
			  entry.flags |= FLAG_ACTIVE;
			  SVGAOTableFlags(svga, i, entry.flags);	  
			}
		}
	}
	
	/* sync */
	uint32_t fence = SVGAFenceInsert(svga);
	SVGAFenceSync(svga, fence);
	
	return TRUE;
}

/* create SVGA interface */
BOOL SVGACreate(svga_inst_t *svga, HWND win)
{
	memset(svga, 0, sizeof(svga_inst_t));

	if(win == INVALID_HANDLE_VALUE)
	{
		svga->dc = SVGAGetDesktopCTX();
	}
	else
	{
		svga->dc = GetDC(win);
	}
	
	svga->pid = GetCurrentProcessId();
	
	if(IsSVGA(svga->dc))
	{
		if(ExtEscape(svga->dc, SVGA_HDA_REQ, 0, NULL, sizeof(svga_hda_t), (LPSTR)&(svga->hda)))
		{
			assert(svga->hda.fifo_linear);
			assert(svga->hda.userlist_linear);
			
			SVGAUserlistInit(svga);
			svga->surfinfo = (svga_surfinfo_t*)calloc(sizeof(svga_surfinfo_t), svga->hda.ul_surf_count);
			
			svga->vxd = CreateFileA("\\\\.\\vmwsmini.vxd", 0, 0, NULL, 0, FILE_FLAG_DELETE_ON_CLOSE, NULL);
#ifdef DEBUG
			SVGACMDDebug(svga, "RING-3 DLL init");
#endif

			debug_printf("%s SUCCESS!\n", __FUNCTION__);
			svga->ctx_id = 0;
			
			SVGACBContextCreate(svga);
			SVGAInitOTables(svga);
			
			return TRUE;
		}
	}
	else
	{
		if(IsSVGAHW(svga->dc))
		{
			uint32_t drv_ver = -1;
			uint32_t vxd_ver = -1;
			
			if(SVGAVerifyDriver(svga->dc, &drv_ver, &vxd_ver))
			{
				GUIError(svga, "3D functionality is disabled!\n\nIt could be disabled in VM configuration or is disabled by hypervizor due missing compatible GPU on host site or VM serve wrong 3D api (Virtual GPU GEN9 is required)");
			}
			else
			{
				GUIError(svga, "Mismatch in driver files detected:\nDLL API: %d\nVXD API: %d\nDRV API:%d\n\nPlease try to reboot the OS or reinstall driver", DRV_API_LEVEL, vxd_ver, drv_ver);
			}
		}
		else
		{
			GUIError(svga, "Your GPU isn't VMWare SVGA II or you haven't installed corresponding driver.");
		}
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
	ctx_cmd.defctx.cid = SVGAContextIDNext(svga);
	
	if(svga->have_cb_context)
	{
		cb_state_t cbs;
		cb_lock(svga, &cbs);
		cb_push(&cbs, &ctx_cmd, sizeof(ctx_cmd));
		cb_submit(svga, &cbs, SVGA3D_INVALID_ID, SVGA_CB_CONTEXT_DEFAULT);
	}
	else
	{
		SVGAFifoWrite(svga, &ctx_cmd, sizeof(ctx_cmd));
		SVGAFullSync(svga);
	}
	
	svga->ctx_id = ctx_cmd.defctx.cid;
	
	svga_printf(svga, "creating context %d\n", svga->ctx_id);
	
	SVGAContextInfoSet(svga, ctx_cmd.defctx.cid, CTX_INDEX_COTABLE, 0);
	
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
	
	svga_printf(svga, "destroing context %d\n", cid);
	
	if(svga->dx)
		ctx_cmd.header.id = SVGA_3D_CMD_DX_DESTROY_CONTEXT;
	else
		ctx_cmd.header.id = SVGA_3D_CMD_CONTEXT_DESTROY;
	
	ctx_cmd.header.size = sizeof(SVGA3dCmdDefineContext);
	ctx_cmd.defctx.cid = cid;
	
	if(svga->have_cb_context)
	{
		cb_state_t cbs;
		cb_lock(svga, &cbs);
		cb_push(&cbs, &ctx_cmd, sizeof(ctx_cmd));
		cb_submit(svga, &cbs, SVGA3D_INVALID_ID, SVGA_CB_CONTEXT_DEFAULT);
	}
	else
	{
		SVGAFifoWrite(svga, &ctx_cmd, sizeof(ctx_cmd));
	}
	
	SVGAFullSync(svga);
	
	SVGAContextIDFree(svga, cid);
	svga->ctx_id = 0;
}

#define COTABLE_ENTRIES_BLOCK 128

static const svga_cotable_t def_cotable = {{
	{SVGA_COTABLE_RTVIEW,          sizeof(SVGACOTableDXRTViewEntry),          COTABLE_ENTRIES_BLOCK, 0},
	{SVGA_COTABLE_DSVIEW,          sizeof(SVGACOTableDXDSViewEntry),          COTABLE_ENTRIES_BLOCK, 0},
	{SVGA_COTABLE_SRVIEW,          sizeof(SVGACOTableDXSRViewEntry),          COTABLE_ENTRIES_BLOCK, 0},
	{SVGA_COTABLE_ELEMENTLAYOUT,   sizeof(SVGACOTableDXElementLayoutEntry),   COTABLE_ENTRIES_BLOCK, 0},
	{SVGA_COTABLE_BLENDSTATE,      sizeof(SVGACOTableDXBlendStateEntry),      COTABLE_ENTRIES_BLOCK, 0},
	{SVGA_COTABLE_DEPTHSTENCIL,    sizeof(SVGACOTableDXDepthStencilEntry),    COTABLE_ENTRIES_BLOCK, 0},
	{SVGA_COTABLE_RASTERIZERSTATE, sizeof(SVGACOTableDXRasterizerStateEntry), COTABLE_ENTRIES_BLOCK, 0},
	{SVGA_COTABLE_SAMPLER,         sizeof(SVGACOTableDXSamplerEntry),         COTABLE_ENTRIES_BLOCK, 0},
	{SVGA_COTABLE_STREAMOUTPUT,    sizeof(SVGACOTableDXStreamOutputEntry),    COTABLE_ENTRIES_BLOCK, 0},
	{SVGA_COTABLE_DXQUERY,         sizeof(SVGACOTableDXQueryEntry),           COTABLE_ENTRIES_BLOCK, 0},
	{SVGA_COTABLE_DXSHADER,        sizeof(SVGACOTableDXShaderEntry),          COTABLE_ENTRIES_BLOCK, 0},
	{SVGA_COTABLE_UAVIEW,          sizeof(SVGACOTableDXUAViewEntry),          COTABLE_ENTRIES_BLOCK, 0},
}};

void SVGACBContextCreate(svga_inst_t *svga)
{
/* CB context is enabled by driver now */
	if(!svga->have_cb_context)
	{
		cb_state_t cbs;
		/* check command buffer availability by tries to use it  */
		if(cb_lock(svga, &cbs))
		{
			cb_submit(svga, &cbs, SVGA3D_INVALID_ID, SVGA_CB_CONTEXT_DEVICE); /* do nothing, only unlock buffer */
			svga->have_cb_context = TRUE;
			
			svga_printf(svga, "CB context is ready!");
			
			svga->have_cb_context = TRUE;
		}
	}
}

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
	
	cb_state_t cbs;
	
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
	SVGAFullSync(svga);

	/* set cotable */
	cb_lock(svga, &cbs);
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
		  	
			cb_push(&cbs, &cmd_cotable, sizeof(cmd_cotable));
		}
	}

  cb_submit_sync(svga, &cbs, cid, SVGA_CB_CONTEXT_DEFAULT);
  
  SVGAContextInfoSet(svga, cid, CTX_INDEX_COTABLE, (DWORD)cotable);
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
	
	DWORD ptr32 = SVGAContextInfoGet(svga, cid, CTX_INDEX_COTABLE);
	
	if(ptr32 == 0)
	{
		svga_printf(svga, "SVGAContextInfoGet FAIL!\n");
		return FALSE;
	}
	
	svga_cotable_t *cotable = (svga_cotable_t *)ptr32;
	
	if(destId+1 >= cotable->item[type].count)
	{
		/* cotable needs to be resized */
		cb_state_t cbs;
		void *old_mem = NULL;
		void *new_mem = NULL;
		uint32_t new_mem_ptr32 = 0;
		uint32_t old_gmrId = cotable->item[type].gmr_id;
		uint32_t new_gmrId;
		
		if(old_gmrId)
		{
			old_mem = (void*)SVGAGMRInfoGet(svga, old_gmrId, GMR_INDEX_ADDRESS);
		}
		
		/* new number of items round to multiple of COTABLE_ENTRIES_BLOCK */
		uint32_t new_count = ((destId + 1 + COTABLE_ENTRIES_BLOCK) / COTABLE_ENTRIES_BLOCK) * COTABLE_ENTRIES_BLOCK;
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
		cb_lock(svga, &cbs);
		cb_push(&cbs, &cmd_read_cotable, sizeof(cmd_read_cotable));
		cb_submit_sync(svga, &cbs, cid, SVGA_CB_CONTEXT_DEFAULT);
		
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
		cb_lock(svga, &cbs);
		cb_push(&cbs, &cmd_grow_cotable, sizeof(cmd_grow_cotable));
		cb_submit_sync(svga, &cbs, cid, SVGA_CB_CONTEXT_DEFAULT);
		
		/* update values */
		cotable->item[type].count  = new_count;
		cotable->item[type].gmr_id = new_gmrId;
		
		/* delete old GMR */
		SVGARegionDestroy(svga, old_gmrId);
		
		svga_printf(svga, "Done grow %d to %d", type, new_count);
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
	
	cb_state_t cbs;
	svga_cotable_t *cotable = (svga_cotable_t*)SVGAContextInfoGet(svga, cid, CTX_INDEX_COTABLE);
	
	if(cotable == 0) return;
	
	/* invalidate cotable */
	cb_lock(svga, &cbs);
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
		  
			cb_push(&cbs, &cmd_cotable, sizeof(cmd_cotable));
		}
	}
	
	cb_submit(svga, &cbs, cid, SVGA_CB_CONTEXT_DEFAULT);
	
	SVGAFullSync(svga);
	
	/* destroy MOBs */
	for(int i = 0; i < SVGA_COTABLE_MAX; i++)
	{
		if(cotable->item[i].gmr_id != 0)
		{
			SVGARegionDestroy(svga, cotable->item[i].gmr_id);
			cotable->item[i].gmr_id = 0;
		}
	}
	
	SVGAFullSync(svga);
	
	free(cotable);	
}

/* destroy the surface */
void SVGASurfaceDestroy(svga_inst_t *svga, uint32_t sid)
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
	
	cb_state_t cbs;		
	
	if(svga->surfinfo[sid].gmrId) // GB surface
	{
		cmd_unbind.cmd  = SVGA_3D_CMD_BIND_GB_SURFACE;
		cmd_unbind.size = sizeof(SVGA3dCmdBindGBSurface);
		cmd_unbind.bind.sid   = sid;
		cmd_unbind.bind.mobid = SVGA3D_INVALID_ID;
		
		cmd_surface.cmd = SVGA_3D_CMD_DESTROY_GB_SURFACE;
		cmd_surface.size = sizeof(SVGA3dCmdDestroySurface);
		cmd_surface.surface.sid = sid;
		
		if(svga->have_cb_context)
		{
			cb_lock(svga, &cbs);
			cb_push(&cbs, &cmd_unbind, sizeof(cmd_unbind));
			cb_push(&cbs, &cmd_surface, sizeof(cmd_surface));
			cb_submit(svga, &cbs, SVGA3D_INVALID_ID, SVGA_CB_CONTEXT_DEFAULT);
		}
		else
		{
			SVGAFifoWrite(svga, &cmd_unbind, sizeof(cmd_unbind));
			SVGAFifoWrite(svga, &cmd_surface, sizeof(cmd_surface));
		}
	}
	else
	{
		cmd_surface.cmd = SVGA_3D_CMD_SURFACE_DESTROY;
		cmd_surface.size = sizeof(SVGA3dCmdDestroySurface);
		cmd_surface.surface.sid = sid;
		
		if(svga->have_cb_context)
		{
			cb_lock(svga, &cbs);
			cb_push(&cbs, &cmd_surface, sizeof(cmd_surface));
			cb_submit(svga, &cbs, SVGA3D_INVALID_ID, SVGA_CB_CONTEXT_DEFAULT);
		}
		else
		{
			SVGAFifoWrite(svga, &cmd_surface, sizeof(cmd_surface));
		}
	}
	SVGASurfaceIDFree(svga, sid);
}

/* destroy the interface */
void SVGADestroy(svga_inst_t *svga)
{
	SVGA_ASSERT;
	
	if(svga->ctx_id)
	{
		SVGAContextDestroy(svga, svga->ctx_id);
		svga->ctx_id = 0;
	}
	
	if(svga->surfinfo)
	{
		free(svga->surfinfo);
		svga->surfinfo = NULL;
	}
	
	if(svga->vxd)
	{
		CloseHandle(svga->vxd);
	}
}

/* read hardware register */
BOOL SVGAReadReg(svga_inst_t *svga, uint32_t reg, uint32_t *val)
{
	static uint32_t sreg = 0;
	static uint32_t sval = 0;
	
	SVGA_ASSERT;
	
	sreg = reg;
	
	if(ExtEscape(svga->dc, SVGA_READ_REG, sizeof(uint32_t), (LPCSTR)&sreg, sizeof(uint32_t), (LPSTR)&sval))
	{
		if(val != NULL)
		{
			*val = sval;
		}
		
		return TRUE;
	}
		
	return FALSE;
}

/* insert fence and return its ID */
uint32_t SVGAFenceInsert(svga_inst_t *svga)
{
	SVGA_ASSERT;
	
	uint32 fence = SVGAFenceIDNext(svga);
#pragma pack(push)
#pragma pack(1)
  struct {
		uint32 id;
  	uint32 fence;
	} cmd;
#pragma pack(pop)   
  
	cmd.id = SVGA_CMD_FENCE;
	cmd.fence = fence;
	FifoWrite(svga, (uint32_t*)&cmd, sizeof(cmd));

  if(fence == 1)
  {
  	/*
  	 * potencial fence overrun eg. SVGAFencePassed() returns immediately,
  	 * do extra sync to make sure, you're sync to this and all next fences
  	 */
  	SVGACMDSync(svga);
  }

	return fence;
}

/* generate only ID, needs to be inset to buffer manually */
uint32_t SVGAFenceInsertCB(svga_inst_t *svga)
{
	SVGA_ASSERT;
	uint32 fence = SVGAFenceIDNext(svga);
	
  if(fence == 1)
  {
  	/*
  	 * overrun eg. SVGAFencePassed() returns immediately, force sync
  	 */
  	SVGACMDSync(svga);
  	cb_sync(svga);
  }

	return fence;
}

/* check if fence has passed */
BOOL
#ifdef __GNUC__
__attribute__ ((noinline))
#endif
SVGAFencePassed(svga_inst_t *svga, uint32_t fence)
{
	SVGA_ASSERT;
	
	if(fence > SVGAFenceIDCur(svga))
	{
		return TRUE;
	}
	
	if(SVGALock(svga, LOCK_FIFO))
	{
		volatile uint32 *fifo = svga->hda.fifo_linear;
		BOOL res;
		
		res = ((int32)(fifo[SVGA_FIFO_FENCE] - fence)) >= 0;
		
		SVGAUnlock(svga, LOCK_FIFO);
		return res;
	}
	
	/* return TRUE on failure because of deadlocks */
	return TRUE;
}

/* wait to given fence
 * TODO: burning CPU in wait, solve with interupts and mutexes?
 */
void SVGAFenceSync(svga_inst_t *svga, uint32_t fence)
{
	BOOL alarm = FALSE;
	while(!SVGAFencePassed(svga, fence))
	{
		if(!alarm)
		{
			SVGACMDRing(svga);
			alarm = TRUE;
		}
	}
}

/* query fence state */
BOOL SVGAFenceQuery(svga_inst_t *svga, uint32_t fence, uint32_t *fenceStatus, uint32_t *lastPassed, uint32_t *lastFence)
{
	SVGA_ASSERT;
	
	if(SVGALock(svga, LOCK_FIFO))
	{
		volatile uint32 *fifo = svga->hda.fifo_linear;
		
		if(lastPassed)
		{
			*lastPassed = fifo[SVGA_FIFO_FENCE];
		}
		
		if(fenceStatus)
		{
			*fenceStatus = ((int32)(fifo[SVGA_FIFO_FENCE] - fence)) >= 0;
		}
		
		SVGAUnlock(svga, LOCK_FIFO);
		
		if(lastFence)
		{
			*lastFence = SVGAFenceIDCur(svga);
		}
	}
	
	return TRUE;
}

/* write fifo (public interface) */
BOOL SVGAFifoWrite(svga_inst_t *svga, void *cmd, size_t cmd_bytes)
{
	return FifoWrite(svga, (uint32_t*)cmd, cmd_bytes);
}

/* create GMR */
uint32_t SVGARegionCreate(svga_inst_t *svga, uint32_t size, uint32_t *user_page)
{
	static uint32_t ins[2] = {
		0, /* region id */
		0, /* pages */
	};
	static uint32_t ids[5] = {
		0, /* region id */
		0, /* user page address */
		0, /* page block */
		0, /* mob PT address */
		0, /* mob ppn */
	};
	
	uint32_t rid = SVGAGMRIDNext(svga);
	
	SVGA_ASSERT;
	assert(rid > 0);

	/* number of pages */
	ins[0] = rid;
	ins[1] = (size + PAGE_SIZE - 1) / PAGE_SIZE;
	
	SVGALock(svga, ULF_LOCK_GMR);
	
#if 0
	/* old 16 bit call */
	if(!ExtEscape(svga->dc, SVGA_REGION_CREATE, sizeof(ins), (LPCSTR)&ins, sizeof(ids), (LPSTR)&ids))
	{
		SVGAUnlock(svga, ULF_LOCK_GMR);
		return 0; /* call error */
	}
#else
	if(!svga->vxd)
	{
		SVGAUnlock(svga, ULF_LOCK_GMR);
		return 0;
	}
	
	if(!DeviceIoControl(svga->vxd, SVGA_REGION_CREATE, (LPVOID)&ins, sizeof(ins), (LPVOID)&ids, sizeof(ids), NULL, NULL))
	{
		SVGAUnlock(svga, ULF_LOCK_GMR);
		return 0;
	}
#endif

	if(ids[0] == 0)
	{
		SVGAUnlock(svga, ULF_LOCK_GMR);
		GUIError(svga, "Failed to allocate continuous physical RAM space (%d bytes). Please attach more memory to VM or reboot guest OS.", size);	
		return 0; /* allocation error */
	}
	
	SVGAUnlock(svga, ULF_LOCK_GMR);
	
	assert(ins[0] == ids[0]);
	
	if(user_page)
	{
		*user_page = ids[1];
	}
	
	if(ids[4] == 0) /* mob PPN */
	{
		SVGAGuestMemDescriptor *gmr_desc = (SVGAGuestMemDescriptor*)ids[2];
		ids[4] = gmr_desc->ppn;
	}
	
	SVGAGMRInfoSet(svga, ids[0], GMR_INDEX_ADDRESS, ids[1]);
	SVGAGMRInfoSet(svga, ids[0], GMR_INDEX_SIZE, size);
	SVGAGMRInfoSet(svga, ids[0], GMR_INDEX_PGBLK, ids[2]);
	SVGAGMRInfoSet(svga, ids[0], GMR_INDEX_MOBADDR, ids[3]);

	if(svga->dx)
	{
		SVGAGMRInfoSet(svga, ids[0], GMR_INDEX_MOBPPN,  ids[4]);
		
#pragma pack(push)
#pragma pack(1)
		struct
		{
			uint32_t type;
			uint32_t size;
			SVGA3dCmdDefineGBMob mob;
		} cmd_mob;
#pragma pack(pop)
		SVGAGuestMemDescriptor *gmr_desc = (SVGAGuestMemDescriptor*)ids[2];
		
		cmd_mob.type              = SVGA_3D_CMD_DEFINE_GB_MOB;
		cmd_mob.size              = sizeof(SVGA3dCmdDefineGBMob);
		cmd_mob.mob.mobid         = rid;
		cmd_mob.mob.base          = ids[4];
		cmd_mob.mob.sizeInBytes   = size;
		if(ids[3] == 0)
		{
			cmd_mob.mob.ptDepth     = SVGA3D_MOBFMT_RANGE;
		}
		else
		{
			cmd_mob.mob.ptDepth     = SVGA3D_MOBFMT_PTDEPTH_2;
		}
		
		if(svga->have_cb_context)
		{
			cb_state_t cbs;
			cb_lock(svga, &cbs);
			cb_push(&cbs, &cmd_mob, sizeof(cmd_mob));
			cb_submit(svga, &cbs, SVGA3D_INVALID_ID, SVGA_CB_CONTEXT_DEFAULT);
		}
		else
		{
			SVGAFifoWrite(svga, &cmd_mob, sizeof(cmd_mob));
			
			uint32_t fence = SVGAFenceInsert(svga);
			SVGAFenceSync(svga, fence);
		}
	}
	else
	{
		/* mob region have PPN */
		SVGAGMRInfoSet(svga, ids[0], GMR_INDEX_MOBPPN, 0);
	}
	
	SVGACBContextCreate(svga);
	
	return rid;
}

/* destroy the GMR */
void SVGARegionDestroy(svga_inst_t *svga, uint32_t regionId)
{
	static uint32_t ids[4] = {
		0, /* region id */
		0, /* user page address */
		0, /* page block address */
		0, /* mob PT address */
	};

	SVGA_ASSERT;
	
	ids[0] = regionId;
	ids[1] = SVGAGMRInfoGet(svga, regionId, GMR_INDEX_ADDRESS);
	ids[2] = SVGAGMRInfoGet(svga, regionId, GMR_INDEX_PGBLK);
	ids[3] = SVGAGMRInfoGet(svga, regionId, GMR_INDEX_MOBADDR);
	
	DWORD mobPPN = SVGAGMRInfoGet(svga, regionId, GMR_INDEX_MOBPPN);
	
	if(mobPPN)
	{
#pragma pack(push)
#pragma pack(1)
		struct
		{
			uint32_t type;
			uint32_t size;
			SVGA3dCmdDestroyGBMob mob;
		} cmd_mob_destroy;
#pragma pack(pop)

		cmd_mob_destroy.type = SVGA_3D_CMD_DESTROY_GB_MOB;
		cmd_mob_destroy.size = sizeof(SVGA3dCmdDestroyGBMob);
		cmd_mob_destroy.mob.mobid = regionId;

		if(svga->have_cb_context)
		{
			cb_state_t cbs;
			cb_lock(svga, &cbs);
			cb_push(&cbs, &cmd_mob_destroy, sizeof(cmd_mob_destroy));
			cb_submit_sync(svga, &cbs, SVGA3D_INVALID_ID, SVGA_CB_CONTEXT_DEFAULT);
		}
		else
		{
			SVGAFifoWrite(svga, &cmd_mob_destroy, sizeof(cmd_mob_destroy));
			SVGACMDSync(svga);
		
			uint32_t fence = SVGAFenceInsert(svga);
			SVGAFenceSync(svga, fence);
		}
	}
	
		
	if(ids[1] != 0)
	{
#if 0
		ExtEscape(svga->dc, SVGA_REGION_FREE, sizeof(ids), (LPCSTR)&ids, 0, NULL);
#else
		if(svga->vxd);
		{
			DeviceIoControl(svga->vxd, SVGA_REGION_FREE, (LPVOID)&ids, sizeof(ids), NULL, 0, NULL, NULL);
		}
#endif
	}
	
	SVGAGMRIDFree(svga, regionId);
}

/* read all hardware registers, fifo register and caps list to VBOXGAHWINFO struct */
BOOL SVGAReadHwInfo(svga_inst_t *svga, VBOXGAHWINFO *pHwInfo)
{
	int i;
	
	SVGA_ASSERT;
	
	pHwInfo->u32HwType = VBOX_GA_HW_TYPE_VMSVGA;
	pHwInfo->u.svga.cbInfoSVGA = sizeof(VBOXGAHWINFOSVGA);
	ExtEscape(svga->dc, SVGA_HWINFO_REGS, 0, NULL, sizeof(uint32_t)*GA_HWINFO_REGS, (LPSTR)&(pHwInfo->u.svga.au32Regs[0]));
	ExtEscape(svga->dc, SVGA_HWINFO_FIFO, 0, NULL, sizeof(uint32_t)*GA_HWINFO_FIFO, (LPSTR)&(pHwInfo->u.svga.au32Fifo[0]));
	ExtEscape(svga->dc, SVGA_HWINFO_CAPS, 0, NULL, sizeof(uint32_t)*GA_HWINFO_CAPS, (LPSTR)&(pHwInfo->u.svga.au32Caps[0]));
	
	//set_fake_caps(&(pHwInfo->u.svga.au32Caps[0]), 512);
	
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
		if(!SVGACreate(&lsvga, INVALID_HANDLE_VALUE))
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
		SVGASurfaceDestroy(svga, id);
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
		svga->softblit_gmr_id = SVGARegionCreate(svga, svga->softblit_gmr_size, (uint32_t*)(&svga->softblit_gmr_ptr));
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
		svga_surfinfo_t *sinfo = &svga->surfinfo[svga->blitsid];
		if(sinfo != NULL)
		{
			if(sinfo->size.width == render_width &&
				 sinfo->size.height == render_height &&
				 sinfo->bpp == screen_bpp
				)
			{
				return TRUE;
			}
			else
			{
				SVGASurfaceDestroy(svga, svga->blitsid);
				svga->blitsid = 0;
			}
		}
	}
	
	GASURFCREATE createParms = {
		SVGA3D_SURFACE_HINT_TEXTURE | SVGA3D_SURFACE_HINT_RENDERTARGET, 
		//SVGA3D_SURFACE_BIND_SHADER_RESOURCE | SVGA3D_SURFACE_BIND_RENDER_TARGET, /* flags */
		SVGA3D_FORMAT_INVALID, /* format */
		0, /* usage */
		{1} /* mipmaps */
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
	uint32_t h = svga->hda.userlist_linear[ULF_HEIGHT];
	uint32_t w = svga->hda.userlist_linear[ULF_PITCH]/sizeof(uint32_t);
	uint32_t tmp;
	
	volatile uint32_t *ptr = (volatile uint32_t*)svga->hda.vram_linear;
	
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
	
	uint32_t bpp = svga->hda.userlist_linear[ULF_BPP];
  svga_surfinfo_t *sinfo = &svga->surfinfo[sid];
  uint32_t cb_cid = SVGA3D_INVALID_ID;
  
  PIXELFORMATDESCRIPTOR pfd;
  
  if(svga->dx)
  {
  	cb_cid = cid;
  }
  
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
	if(debug_get_option_blit_surf_to_screen_enabled() && (bpp == sinfo->bpp || svga->dx) && bpp == 32) /* JH: it seems to work correctly only in 32 bits! */
	{
		uint32_t fence;
	        
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
		
		if(render_left + render_width > svga->hda.userlist_linear[ULF_WIDTH])
		{
			command.blit.destRect.right = svga->hda.userlist_linear[ULF_WIDTH];
			command.blit.srcRect.right = command.blit.destRect.right - render_left;
		}
		
		if(render_top + render_height > svga->hda.userlist_linear[ULF_HEIGHT])
		{
			command.blit.destRect.bottom = svga->hda.userlist_linear[ULF_HEIGHT];
			command.blit.srcRect.bottom = command.blit.destRect.bottom - render_top;
		}

		if(svga->have_cb_context)
		{
	  	cb_state_t cbs;
	  	cb_lock(svga, &cbs);
	  	cb_push(&cbs, &command, sizeof(command));
	  	cb_submit(svga, &cbs, cb_cid, SVGA_CB_CONTEXT_DEFAULT);
		}
		else
		{
			SVGAFifoWrite(svga, &command, sizeof(command));
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
		cb_state_t cbs;
		vramcpy_rect_t vrect;
		void *gmr;

		cmd_readback.surf.sid = sid;
		
		cb_lock(svga, &cbs);
		cb_push(&cbs, &cmd_readback, sizeof(cmd_readback));
		cb_submit_sync(svga, &cbs, cid, SVGA_CB_CONTEXT_DEFAULT);
		
		gmr = (void*)SVGAGMRInfoGet(svga, sinfo->gmrId, GMR_INDEX_ADDRESS);
		assert(gmr);
		
		vrect.dst_pitch   = svga->hda.userlist_linear[ULF_PITCH];
		vrect.dst_x       = render_left;
		vrect.dst_y       = render_top;
		vrect.dst_w       = render_width;
		vrect.dst_h       = render_height;
		vrect.dst_bpp     = bpp;
		vrect.src_pitch   = sinfo->size.width * vramcpy_pointsize(sinfo->bpp);
		vrect.src_x       = 0;
		vrect.src_y       = 0;
		vrect.src_bpp     = sinfo->bpp;
		
		vramcpy(svga->hda.vram_linear, gmr, &vrect);		
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
		command_blit.blit.boxSrc.w = sinfo->size.width;
		command_blit.blit.boxSrc.h = sinfo->size.height;
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
		command_dma.dma.guest.pitch      = svga->hda.userlist_linear[ULF_PITCH];
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
		
		if(render_left + render_width > svga->hda.userlist_linear[ULF_WIDTH])
		{
			command_dma.box.w = svga->hda.userlist_linear[ULF_WIDTH] - render_left;
		}
		
		if(render_top + render_height > svga->hda.userlist_linear[ULF_HEIGHT])
		{
			command_dma.box.h = svga->hda.userlist_linear[ULF_HEIGHT] - render_top;
		}
		
		command_dma.suffix.suffixSize    = sizeof(SVGA3dCmdSurfaceDMASuffix);
		command_dma.suffix.maximumOffset = svga->hda.userlist_linear[ULF_PITCH] * svga->hda.userlist_linear[ULF_HEIGHT];
		command_dma.suffix.flags.discard         = 0;
		command_dma.suffix.flags.unsynchronized  = 0;
		command_dma.suffix.flags.reserved        = 0;
		
		command_update.rect.x      = render_left;
		command_update.rect.y      = render_top;
		command_update.rect.width  = render_width;
		command_update.rect.height = render_height;
		
		uint32_t fence = SVGAFenceInsertCB(svga);
		uint32_t fence_cmd[2] = {SVGA_CMD_FENCE, fence};	
		
		if(svga->have_cb_context)
		{
			cb_state_t cbs;
			cb_lock(svga, &cbs);
			
			if(bpp != sinfo->bpp)
				cb_push(&cbs, &command_blit, sizeof(command_blit));
			
			cb_push(&cbs, &command_dma, sizeof(command_dma));
			
			if(bpp == 32)
			{
				cb_push(&cbs, &command_update, sizeof(command_update));
				cb_submit(svga, &cbs, cb_cid, SVGA_CB_CONTEXT_DEFAULT);
			}
			else
			{
				if(debug_get_option_dma_need_reread())
				{
					cb_submit_sync(svga, &cbs, cb_cid, SVGA_CB_CONTEXT_DEFAULT);
					refresh_fb(svga);
				}
				else
				{
					cb_push(&cbs, &command_update, sizeof(command_update));
					cb_submit(svga, &cbs, cb_cid, SVGA_CB_CONTEXT_DEFAULT);
				}
			}
		}
		else
		{
			if(bpp != sinfo->bpp)
				SVGAFifoWrite(svga, &command_blit, sizeof(command_blit));
			
			SVGAFifoWrite(svga, &command_dma, sizeof(command_dma));
			
			if(bpp == 32)
			{
				SVGAFifoWrite(svga, &command_update, sizeof(command_update));
			}
			else
			{
				uint32_t fence = SVGAFenceInsert(svga);
				SVGAFenceSync(svga, fence);
				refresh_fb(svga);
			}
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

	cb_state_t cbs;
	svga_surfinfo_t *sinfo = &svga->surfinfo[sid];
	void *gmr = NULL;

	if(sinfo == NULL || (sinfo->size.width * sinfo->size.height) == 0)
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
	
	if(!sinfo->gmrId) /* old way: copy surface to guest memory and display it */
	{
	  if(!set_fb_gmr(svga, sinfo->size.width, sinfo->size.height))
	  {
	  	debug_printf("SVGAPresentWindow: failed to set GMR!\n");
	  	return;
	  }
	  
		debug_printf("SVGAPresentWindow: %d %d, format: %d\n", sbpp, sps, sinfo->format);
		
		command.dma.guest.ptr.gmrId  = svga->softblit_gmr_id;
		command.dma.guest.ptr.offset = 0;
		command.dma.guest.pitch      = sinfo->size.width * sps;
		command.dma.host.sid         = sid;
		command.dma.host.face        = 0;
		command.dma.host.mipmap      = 0;
		command.dma.transfer         = SVGA3D_READ_HOST_VRAM;
			
		command.box.x = 0;
		command.box.y = 0;
		command.box.z = 0;
		command.box.w = sinfo->size.width;
		command.box.h = sinfo->size.height;
		command.box.d = 1;
		command.box.srcx = 0;
		command.box.srcy = 0;
		command.box.srcz = 0;
			
		command.suffix.suffixSize    = sizeof(SVGA3dCmdSurfaceDMASuffix);
		command.suffix.maximumOffset = svga->softblit_gmr_size;
		command.suffix.flags.discard         = 0;
		command.suffix.flags.unsynchronized  = 0;
		command.suffix.flags.reserved        = 0;
		
		if(svga->have_cb_context)
		{
			cb_lock(svga, &cbs);
			cb_push(&cbs, &command, sizeof(command));
			cb_submit_sync(svga, &cbs, SVGA3D_INVALID_ID, SVGA_CB_CONTEXT_DEFAULT);
		}
		else
		{
			SVGAFifoWrite(svga, &command, sizeof(command));
	
			uint32_t fence = SVGAFenceInsert(svga);
			SVGAFenceSync(svga, fence);
		}
	
		assert(svga->softblit_gmr_ptr);
	
		gmr = svga->softblit_gmr_ptr;
	}
	else /* new way: sync GMR and copy buffer to window */
	{
		command_dx.surf.sid = sid;
		
		cb_lock(svga, &cbs);
		cb_push(&cbs, &command_dx, sizeof(command_dx));
		
		cb_submit_sync(svga, &cbs, cid, SVGA_CB_CONTEXT_DEFAULT);
		
		gmr = (void*)SVGAGMRInfoGet(svga, sinfo->gmrId, GMR_INDEX_ADDRESS);
		
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
	bmi.bmiHeader.biWidth       = sinfo->size.width;
	bmi.bmiHeader.biHeight      = -sinfo->size.height;
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
	
	if(SVGALock(svga, ULF_LOCK_FB))
	{
		StretchDIBits(hDC, 0, 0, sinfo->size.width, sinfo->size.height,
	                   0, 0, sinfo->size.width, sinfo->size.height,
		                 gmr, (BITMAPINFO *)&bmi, 0, SRCCOPY);
		SVGAUnlock(svga, ULF_LOCK_FB);
	}
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
    
    if(svga->have_cb_context)
    {
    	cb_state_t cbs;
    	cb_lock(svga, &cbs);
    	cb_push(&cbs, &command, sizeof(command));
   		cb_submit(svga, &cbs, SVGA3D_INVALID_ID, SVGA_CB_CONTEXT_DEFAULT);
    }
    else
    {
			SVGAFifoWrite(svga, &command, sizeof(command));
			uint32_t fence = SVGAFenceInsert(svga);
			SVGAFenceSync(svga, fence);
		}
	}
	else
	{
		cb_state_t cbs;
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
    
    cb_lock(svga, &cbs);
    cb_push(&cbs, &command_dx, sizeof(command_dx));
    cb_submit(svga, &cbs, cid, SVGA_CB_CONTEXT_DEFAULT);
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
	if(SVGACreate(&lSvga, INVALID_HANDLE_VALUE))
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
	
	uint32_t sid = SVGASurfaceIDNext(svga);
	
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
	  		svga->surfinfo[sid].format = surface->format;
	  		svga->surfinfo[sid].bpp    = format_to_bpp(surface->format);
	  		svga->surfinfo[sid].size.width = siz->width;
	  		svga->surfinfo[sid].size.height = siz->height;
	  		svga->surfinfo[sid].size.depth = siz->depth;
	  		svga->surfinfo[sid].gmrId = 0;
	  	}
	  	
	  	paSizes++;
	  	siz++;
	  }
	  
	  if(svga->have_cb_context)
	  {
	  	cb_state_t cbs;
			cb_lock(svga, &cbs);
			cb_push(&cbs, cmd, cbCmd);
			cb_submit_sync(svga, &cbs, SVGA3D_INVALID_ID, SVGA_CB_CONTEXT_DEFAULT); // need sync?

		  *outSid = sid;
		  return TRUE;
	  }
	  else
	  {
		  if(SVGAFifoWrite(svga, cmd, cbCmd))
		  {
		  	*outSid = sid;
		  	return TRUE;
		  }
		}
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

  uint32_t sid = SVGASurfaceIDNext(svga);
  
  if(sid == 0)
  {
  	return FALSE;
  }
  
	uint32_t cbGB = 0;
	uint32_t userAddress = 0;
	
	/*if(pCreateParms->s.flags & SVGA3D_SURFACE_HINT_RENDERTARGET)
	{
		svga_printf(svga, "SVGASurfaceGBCreate: SVGA3D_SURFACE_HINT_RENDERTARGET");
	}*/

	/* Allocate GMR, if not already supplied. */
	if(pCreateParms->gmrid == SVGA3D_INVALID_ID)
	{
		uint32_t u32NumPages = (pCreateParms->cbGB + PAGE_SIZE - 1) >> PAGE_SHIFT;
		cbGB = u32NumPages * PAGE_SIZE;
		pCreateParms->gmrid = SVGARegionCreate(svga, cbGB, &userAddress);
		
		if(pCreateParms->gmrid == 0)
		{
			SVGASurfaceIDFree(svga, sid);
			return FALSE;
		}
	}
	else
	{
		cbGB = SVGAGMRInfoGet(svga, pCreateParms->gmrid, GMR_INDEX_SIZE);
		userAddress = SVGAGMRInfoGet(svga, pCreateParms->gmrid, GMR_INDEX_ADDRESS);
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
	
	//svga_printf(svga, "new surface(%d): %s", sid, get_format_name(pCreateParms->s.format));
	
	//SVGAFifoWrite(svga, &cmd, sizeof(cmd));

	cmd_bind.type = SVGA_3D_CMD_BIND_GB_SURFACE;
	cmd_bind.size = sizeof(SVGA3dCmdBindGBSurface);
	cmd_bind.bind.sid = sid;
	cmd_bind.bind.mobid = pCreateParms->gmrid;
	
	//SVGAFifoWrite(svga, &cmd_bind, sizeof(cmd_bind));

	/* command buffers are faster than FIFO, so sync FIFO before continue */
	cb_state_t cbs;
	cb_lock(svga, &cbs);
	cb_push(&cbs, &cmd, sizeof(cmd));
	cb_push(&cbs, &cmd_bind, sizeof(cmd_bind));
	
	cb_submit(svga, &cbs, SVGA3D_INVALID_ID, SVGA_CB_CONTEXT_DEFAULT);
	
  /* pCreateParms->gmrid;  In/Out: Backing GMR. */
  pCreateParms->cbGB = cbGB; /* Out: Size of backing memory. */
  pCreateParms->userAddress = userAddress; /* Out: R3 mapping of the backing memory */
  pCreateParms->u32Sid  = sid; /* Out: Surface id. */

	svga->surfinfo[sid].format = pCreateParms->s.format;
	svga->surfinfo[sid].bpp    = format_to_bpp(pCreateParms->s.format);
	svga->surfinfo[sid].size.width = pCreateParms->s.size.width;
	svga->surfinfo[sid].size.height = pCreateParms->s.size.height;
	svga->surfinfo[sid].size.depth = pCreateParms->s.size.depth;
	svga->surfinfo[sid].gmrId = pCreateParms->gmrid;

  return TRUE;
}

BOOL cb_lock(svga_inst_t *svga, cb_state_t *cbs)
{
	memset(cbs, 0, sizeof(cb_state_t));
	
  do
  {
  	if(!SVGALockCB(svga, (void **)&(cbs->cb)))
  	{
  		return FALSE;
  	}
  } while(cbs->cb == NULL);
 
	memset(cbs->cb, 0, sizeof(SVGACBHeaderDX));
 	
  cbs->cb_ptr = (uint8_t*)cbs->cb;
  cbs->cb_pos = sizeof(SVGACBHeaderDX);
  cbs->cmd_count = 0;
  
  return TRUE;
}

static void cb_submit_proc(svga_inst_t *svga, cb_state_t *cbs, uint32_t cid, uint32_t cbctx_id, BOOL sync)
{
  if(cbs->cb != NULL)
  {
  	cbs->cb->length = cbs->cb_pos - sizeof(SVGACBHeaderDX);
  	
  	if(cbctx_id != SVGA_CB_CONTEXT_DEVICE && cid != SVGA3D_INVALID_ID)
  	{
  		cbs->cb->flags = (SVGACBFlagsDX)(cbs->cb->flags | SVGA_CB_DX_FLAG_DX_CONTEXT);
  		cbs->cb->dxContext = cid;
  	}
  	
  	SVGASubmitCB(svga, cbs->cb, cbctx_id, sync);
  	
  	debug_printf("cb_send: cmds %d, size %d\n", cbs->cmd_count, cbs->cb->length);
  }
}

void cb_submit(svga_inst_t *svga, cb_state_t *cbs, uint32_t cid, uint32_t cbctx_id)
{
	cb_submit_proc(svga, cbs, cid, cbctx_id, FALSE);
}

void cb_submit_sync(svga_inst_t *svga, cb_state_t *cbs, uint32_t cid, uint32_t cbctx_id)
{
	cb_submit_proc(svga, cbs, cid, cbctx_id, TRUE);
}

void cb_sync(svga_inst_t *svga)
{
  SVGASyncCB(svga);
}

void cb_push(cb_state_t *cbs, const void *buffer, size_t size)
{
  if(cbs->cb != NULL)
  {
		memcpy(cbs->cb_ptr + cbs->cb_pos, buffer, size);
		cbs->cb_pos += size;
		cbs->cmd_count++;
	}
}

BOOL cb_full(cb_state_t *cbs, size_t cbNeed)
{
	if(cbs->cb == NULL)
  {
  	return FALSE;
  }
	
	return cbs->cb_pos + cbNeed > SVGA_CB_MAX_SIZE ||
		cbs->cmd_count >=  SVGA_CB_MAX_QUEUED_PER_CONTEXT;
}
