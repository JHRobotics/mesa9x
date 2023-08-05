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

#define FLAG_ALLOCATED 1
#define FLAG_ACTIVE    2

/* Actual driver api */
#define DRV_API_LEVEL 20230805UL

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


static void *SVGALockCB(svga_inst_t *svga)
{
	DWORD out[1] = {0};
	
	if(svga->vxd)
	{
		if(DeviceIoControl(svga->vxd, SVGA_CB_LOCK, NULL, 0, out, sizeof(out), NULL, NULL))
		{
			return (void*)out[0];
		}
	}
	
	return NULL;
}

static BOOL SVGASubmitCB(svga_inst_t *svga, void *ptr, uint32_t cbctx_id)
{
	DWORD in[2] = {(DWORD)ptr, cbctx_id};
	
	if(svga->vxd)
	{
		if(DeviceIoControl(svga->vxd, SVGA_CB_SUBMIT, (LPVOID)in, sizeof(in), NULL, 0, NULL, NULL))
		{
			return TRUE;
		}
	}
	
	return FALSE;
}

static void SVGASyncCB(svga_inst_t *svga)
{
	if(svga->vxd)
	{
		DeviceIoControl(svga->vxd, SVGA_CB_SYNC, NULL, 0, NULL, 0, NULL, NULL);
	}
}

/* send debug info to VXD */
static void svga_printf(svga_inst_t *svga, const char *fmt, ...)
{
	char strbuf[512];
  va_list args;
  
  va_start(args, fmt);
  vsprintf(strbuf, fmt, args);
  SVGACMDDebug(svga, strbuf);
  va_end(args);
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
			volatile uint32_t *id_ptr = &svga->hda.userlist_linear[svga->hda.ul_ctx_start + id];
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
		svga->hda.userlist_linear[svga->hda.ul_ctx_start + ctx_id] = 0;
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
			volatile uint32_t *id_ptr = &svga->hda.userlist_linear[svga->hda.ul_ctx_start + id];
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
		
		if((*state) >= svga->hda.ul_gmr_start && (*state) < (svga->hda.ul_gmr_start+svga->hda.ul_gmr_count*GMR_INDEX_CNT) && (*state) % GMR_INDEX_CNT == 0)
		{
			fpid = svga->hda.userlist_linear[(*state)];
		}
		else if((*state) >= svga->hda.ul_ctx_start && (*state) < (svga->hda.ul_ctx_start+svga->hda.ul_ctx_count))
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

#define COTABLE_ENTRIES 1024

static const svga_cotable_t def_cotable = {{
	{SVGA_COTABLE_RTVIEW,          sizeof(SVGACOTableDXRTViewEntry),          COTABLE_ENTRIES, 0},
	{SVGA_COTABLE_DSVIEW,          sizeof(SVGACOTableDXDSViewEntry),          COTABLE_ENTRIES, 0},
	{SVGA_COTABLE_SRVIEW,          sizeof(SVGACOTableDXSRViewEntry),          COTABLE_ENTRIES, 0},
	{SVGA_COTABLE_ELEMENTLAYOUT,   sizeof(SVGACOTableDXElementLayoutEntry),   COTABLE_ENTRIES, 0},
	{SVGA_COTABLE_BLENDSTATE,      sizeof(SVGACOTableDXBlendStateEntry),      COTABLE_ENTRIES, 0},
	{SVGA_COTABLE_DEPTHSTENCIL,    sizeof(SVGACOTableDXDepthStencilEntry),    COTABLE_ENTRIES, 0},
	{SVGA_COTABLE_RASTERIZERSTATE, sizeof(SVGACOTableDXRasterizerStateEntry), COTABLE_ENTRIES, 0},
	{SVGA_COTABLE_SAMPLER,         sizeof(SVGACOTableDXSamplerEntry),         COTABLE_ENTRIES, 0},
	{SVGA_COTABLE_STREAMOUTPUT,    sizeof(SVGACOTableDXStreamOutputEntry),    COTABLE_ENTRIES, 0},
	{SVGA_COTABLE_DXQUERY,         sizeof(SVGACOTableDXQueryEntry),           COTABLE_ENTRIES, 0},
	{SVGA_COTABLE_DXSHADER,        sizeof(SVGACOTableDXShaderEntry),          COTABLE_ENTRIES, 0},
	{SVGA_COTABLE_UAVIEW,          sizeof(SVGACOTableDXUAViewEntry),          COTABLE_ENTRIES, 0},
}};

/* create SVGA interface */
BOOL SVGACreate(svga_inst_t *svga, HWND win)
{
	memset(svga, 0, sizeof(svga_inst_t));
	svga->cotable = def_cotable;
	
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
	
	SVGAFifoWrite(svga, &ctx_cmd, sizeof(ctx_cmd));
	
	svga->ctx_id = ctx_cmd.defctx.cid;
	
	svga_printf(svga, "creating context %d\n", svga->ctx_id);
	
	SVGAFullSync(svga);
	
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
	
	SVGAFifoWrite(svga, &ctx_cmd, sizeof(ctx_cmd));
	SVGAFullSync(svga);
	
	SVGAContextIDFree(svga, cid);
	svga->ctx_id = 0;
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
	
	if(svga->surfinfo[sid].gmrId)
	{
		cmd_unbind.cmd  = SVGA_3D_CMD_BIND_GB_SURFACE;
		cmd_unbind.size = sizeof(SVGA3dCmdBindGBSurface);
		cmd_unbind.bind.sid   = sid;
		cmd_unbind.bind.mobid = SVGA3D_INVALID_ID;
		
		SVGAFifoWrite(svga, &cmd_unbind, sizeof(cmd_unbind));
	}
	
	cmd_surface.cmd = SVGA_3D_CMD_SURFACE_DESTROY;
	cmd_surface.size = sizeof(SVGA3dCmdDestroySurface);
	cmd_surface.surface.sid = sid;
	
	SVGAFifoWrite(svga, &cmd_surface, sizeof(cmd_surface));
	
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

/* check if fence has passed */
BOOL
#ifdef __GNUC__
__attribute__ ((noinline))
#endif
SVGAFencePassed(svga_inst_t *svga, uint32_t fence)
{
	SVGA_ASSERT;
	
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
	
#if 0
	/* old 16 bit call */
	if(!ExtEscape(svga->dc, SVGA_REGION_CREATE, sizeof(ins), (LPCSTR)&ins, sizeof(ids), (LPSTR)&ids))
	{
		return 0; /* call error */
	}
#else
	if(!svga->vxd) return 0;
	
	if(!DeviceIoControl(svga->vxd, SVGA_REGION_CREATE, (LPVOID)&ins, sizeof(ins), (LPVOID)&ids, sizeof(ids), NULL, NULL))
		return 0;
#endif

	if(ids[0] == 0)
	{
		GUIError(svga, "Failed to allocate continuous physical RAM space (%d bytes). Please attach more memory to VM or reboot guest OS.", size);	
		return 0; /* allocation error */
	}
	
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
		
		SVGAFifoWrite(svga, &cmd_mob, sizeof(cmd_mob));
		
		svga_printf(svga, "GMR: ppn: %u, numPages: %u", gmr_desc->ppn, gmr_desc->numPages);	
		svga_printf(svga, "SVGA_3D_CMD_DEFINE_GB_MOB (%d, %u, %d)", cmd_mob.mob.mobid, cmd_mob.mob.base, cmd_mob.mob.sizeInBytes);
	}
	else
	{
		/* mob region have PPN */
		SVGAGMRInfoSet(svga, ids[0], GMR_INDEX_MOBPPN, 0);
	}
	
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
		
		SVGAFifoWrite(svga, &cmd_mob_destroy, sizeof(cmd_mob_destroy));
		SVGACMDSync(svga);
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

static BOOL set_fb_gmr(svga_inst_t *svga, uint32_t render_width, uint32_t render_height)
{
	uint32_t softblit_minsize = vramcpy_calc_framebuffer(render_width, render_height, 32);
	
	svga_printf(svga, "set_fb_gmr: GMR: %d", svga->softblit_gmr_id);

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
	
	svga_printf(svga, "SVGAPresentWinBlt(-, -, %d, %d)", cid, sid);

	SVGAPresentWindow(svga, hDC, cid, sid);
}

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
  
  if(sinfo == NULL)
  {
   	return;
  }
   
  const int sbpp = format_to_bpp(sinfo->format);
  
  frame_wait(svga);
  
  if(sbpp == 8 || bpp == 8)
  {
		SVGAPresentWindow(svga, hDC, cid, sid);
		return;
  }

	RECT wrect = {0, 0, 0, 0};
	const HWND hwnd = WindowFromDC(hDC);
	
	if(!hwnd)
	{
		return;
	}
	
	if(GetTopWindow(GetDesktopWindow()) != hwnd)
	{
		SVGAPresentWindow(svga, hDC, cid, sid);
		return;
	}
	
	if(!IsWindowVisible(hwnd))
	{
		SVGAPresentWindow(svga, hDC, cid, sid);
		return;
	}
	
	if(!GetWindowRect(hwnd, &wrect))
	{
		SVGAPresentWindow(svga, hDC, cid, sid);
		return;
	}
  
  uint32_t render_top  = wrect.top;
  uint32_t render_left = wrect.left;
	uint32_t render_width  = wrect.right - wrect.left;  //fb->client_rect.right - fb->client_rect.left;
	uint32_t render_height = wrect.bottom - wrect.top; //fb->client_rect.bottom - fb->client_rect.top;
  
	if(render_width * render_height == 0)
	{
		/* Nothing to see here. Please disperse. */
		return;
	}
  
	// surface and screen must have same color depth for HW present
	if(bpp == sbpp && bpp == 32) /* JH: it seems to work correctly only in 32 bits! */
	{
		uint32_t fence;
	        
#pragma pack(push)
#pragma pack(1)
		struct
		{
			uint32_t type;
			uint32_t size;
			uint32   sid;
			SVGA3dCopyRect rect;
		} command;
#pragma pack(pop)
	
		command.type = SVGA_3D_CMD_PRESENT;
		command.size = sizeof(uint32_t) + sizeof(SVGA3dCopyRect);
		command.sid = sid;
		command.rect.x = wrect.left;
		command.rect.y = wrect.top;
		command.rect.w = render_width;
		command.rect.h = render_height;
		command.rect.srcx = 0;
		command.rect.srcy = 0;
	
		SVGAFifoWrite(svga, &command, sizeof(command));
	  
		fence = SVGAFenceInsert(svga);
		SVGAFenceSync(svga, fence);
	}
	else /* harder way, we'll need render surface to some GMR region and copy to frame buffer manualy */
	{
#pragma pack(push)
#pragma pack(1)
		struct
		{
			uint32_t type;
			uint32_t size;
			SVGA3dCmdSurfaceDMA       dma;
			SVGA3dCopyBox             box;
			SVGA3dCmdSurfaceDMASuffix suffix;
			
		} command;
#pragma pack(pop)

    vramcpy_rect_t crect;
    
    if(!set_fb_gmr(svga, render_width, render_height))
    {
    	return;
    }
		
		memset(&command, 0, sizeof(command));

		command.type = SVGA_3D_CMD_SURFACE_DMA;
		command.size = sizeof(SVGA3dCmdSurfaceDMA) + sizeof(SVGA3dCopyBox) + sizeof(SVGA3dCmdSurfaceDMASuffix);
		command.dma.guest.ptr.gmrId  = svga->softblit_gmr_id;
		command.dma.guest.ptr.offset = 0;
		command.dma.guest.pitch      = render_width * 4;
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

		SVGAFifoWrite(svga, &command, sizeof(command));

		uint32_t fence = SVGAFenceInsert(svga);
		SVGAFenceSync(svga, fence);
		
		crect.dst_x     = wrect.left;
		crect.dst_y     = wrect.top;
		crect.dst_w     = render_width;
		crect.dst_h     = render_height;
		crect.dst_bpp   = bpp;
		crect.dst_pitch = svga->hda.userlist_linear[ULF_PITCH];
		crect.src_x     = 0;
		crect.src_y     = 0;
		crect.src_bpp   = sbpp;
		crect.src_pitch = render_width * 4;
		
		assert(svga->hda.vram_linear);
		assert(svga->softblit_gmr_ptr);
		
		if(SVGALock(svga, ULF_LOCK_FB))
		{
			vramcpy(svga->hda.vram_linear, svga->softblit_gmr_ptr, &crect);
			SVGALock(svga, ULF_LOCK_FB);
		}
		
		/* update framebuffer command */
#pragma pack(push)
#pragma pack(1)
		struct
		{
			uint32_t cmd;
			SVGAFifoCmdUpdate rect;
		} updatecmd;
#pragma pack(pop)
		
		updatecmd.cmd = SVGA_CMD_UPDATE;
		updatecmd.rect.x      = wrect.left;
		updatecmd.rect.y      = wrect.top;
		updatecmd.rect.width  = render_width;
		updatecmd.rect.height = render_height;
		
		SVGAFifoWrite(svga, &updatecmd, sizeof(updatecmd));
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
		uint32_t type;
		uint32_t size;
		SVGA3dCmdSurfaceDMA       dma;
		SVGA3dCopyBox             box;
		SVGA3dCmdSurfaceDMASuffix suffix;
			
	} command;
#pragma pack(pop)

	vramcpy_rect_t crect;
	svga_surfinfo_t *sinfo = &svga->surfinfo[sid];
	void *gmr = NULL;

	if(sinfo == NULL || (sinfo->size.width * sinfo->size.height) == 0)
	{
		debug_printf("SVGAPresentWindow: null surface info!\n");
		/* Nothing to see here. Please disperse. */
		return;
	}

  const int sbpp = format_to_bpp(sinfo->format);
  const size_t sps = vramcpy_pointsize(sbpp);
	
	if(!sinfo->gmrId) /* old way: copy surface to guest memory and display it */
	{
	  if(!set_fb_gmr(svga, sinfo->size.width, sinfo->size.height))
	  {
	  	debug_printf("SVGAPresentWindow: failed to set GMR!\n");
	  	return;
	  }
	  
		debug_printf("SVGAPresentWindow: %d %d, format: %d\n", sbpp, sps, sinfo->format);
		
		memset(&command, 0, sizeof(command));
	
		command.type = SVGA_3D_CMD_SURFACE_DMA;
		command.size = sizeof(SVGA3dCmdSurfaceDMA) + sizeof(SVGA3dCopyBox) + sizeof(SVGA3dCmdSurfaceDMASuffix);
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
	
		SVGAFifoWrite(svga, &command, sizeof(command));
	
		uint32_t fence = SVGAFenceInsert(svga);
		SVGAFenceSync(svga, fence);
	
		assert(svga->softblit_gmr_ptr);
	
		gmr = svga->softblit_gmr_ptr;
	}
	else
	{
		debug_printf("SVGAPresentWindow: V10 %d %d, format: %d\n", sbpp, sps, sinfo->format);
		debug_printf("GRM: %d %d\n", sinfo->gmrId, SVGAGMRInfoGet(svga, sinfo->gmrId, GMR_INDEX_SIZE));
		
#pragma pack(push)
#pragma pack(1)
    struct
    {
    	  uint32_t type;
        uint32_t size;
        SVGA3dCmdReadbackGBSurface surface;
    } command;
#pragma pack(pop)
		command.type = SVGA_3D_CMD_READBACK_GB_SURFACE;
		command.size = sizeof(SVGA3dCmdReadbackGBSurface);
		command.surface.sid = sid;

		cb_state_t cbs;
		
		cb_lock(svga, &cbs);
		cb_push(&cbs, &command, sizeof(command));
		cb_submit(svga, &cbs, cid, SVGA_CB_CONTEXT_DEFAULT);
		
		cb_sync(svga);
		
		gmr = (void*)SVGAGMRInfoGet(svga, sinfo->gmrId, GMR_INDEX_ADDRESS);
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
	else if(sbpp == 32)
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
	
	debug_printf("SVGACompose(-, %d, %d, %d, -)\n", cid, srcSid, destSid);
	
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
        
		SVGAFifoWrite(svga, &command, sizeof(command));
		uint32_t fence = SVGAFenceInsert(svga);
		SVGAFenceSync(svga, fence);
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

	svga_printf(svga, "SVGASurfaceGBCreate: GMR(%d), size(%u)", pCreateParms->gmrid, pCreateParms->cbGB);

	/* Allocate GMR, if not already supplied. */
	if (pCreateParms->gmrid == SVGA3D_INVALID_ID)
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
	
	SVGAFifoWrite(svga, &cmd, sizeof(cmd));

	cmd_bind.type = SVGA_3D_CMD_BIND_GB_SURFACE;
	cmd_bind.size = sizeof(SVGA3dCmdBindGBSurface);
	cmd_bind.bind.sid = sid;
	cmd_bind.bind.mobid = pCreateParms->gmrid;
	
	SVGAFifoWrite(svga, &cmd_bind, sizeof(cmd_bind));
	
	/* command buffers are faster than FIFO, so sync FIFO before continue */
	uint32_t fence = SVGAFenceInsert(svga);
	SVGAFenceSync(svga, fence);
	
  /* pCreateParms->gmrid;  In/Out: Backing GMR. */
  pCreateParms->cbGB = cbGB; /* Out: Size of backing memory. */
  pCreateParms->userAddress = userAddress; /* Out: R3 mapping of the backing memory */
  pCreateParms->u32Sid  = sid; /* Out: Surface id. */

	svga->surfinfo[sid].format = pCreateParms->s.format;
	svga->surfinfo[sid].size.width = pCreateParms->s.size.width;
	svga->surfinfo[sid].size.height = pCreateParms->s.size.height;
	svga->surfinfo[sid].size.depth = pCreateParms->s.size.depth;
	svga->surfinfo[sid].gmrId = pCreateParms->gmrid;

  return TRUE;
}

void cb_lock(svga_inst_t *svga, cb_state_t *cbs)
{
  if(svga->dx)
  {
  	do
  	{
  		cbs->cb = (SVGACBHeaderDX*)SVGALockCB(svga);
  	} while(cbs->cb == NULL);
  	
  	memset(cbs->cb, 0, sizeof(SVGACBHeaderDX));
  	cbs->cb_ptr = (uint8_t*)cbs->cb;
  	cbs->cb_pos = sizeof(SVGACBHeaderDX);
  	cbs->cmd_count = 0;
  }
}

void cb_submit(svga_inst_t *svga, cb_state_t *cbs, uint32_t cid, uint32_t cbctx_id)
{
  if(svga->dx)
  {
  	cbs->cb->length = cbs->cb_pos - sizeof(SVGACBHeaderDX);
  	
  	if(cbctx_id != SVGA_CB_CONTEXT_DEVICE)
  	{
  		cbs->cb->flags = (SVGACBFlagsDX)(cbs->cb->flags | SVGA_CB_DX_FLAG_DX_CONTEXT);
  		cbs->cb->dxContext = cid;
  	}
  	
  	SVGASubmitCB(svga, cbs->cb, cbctx_id);
  	
  	debug_printf("cb_send: cmds %d, size %d\n", cbs->cmd_count, cbs->cb->length);
  }
}

void cb_sync(svga_inst_t *svga)
{
  if(svga->dx)
  {
  	SVGASyncCB(svga);
  }
}

void cb_push(cb_state_t *cbs, const void *buffer, size_t size)
{
	memcpy(cbs->cb_ptr + cbs->cb_pos, buffer, size);
	cbs->cb_pos += size;
	cbs->cmd_count++;
}
