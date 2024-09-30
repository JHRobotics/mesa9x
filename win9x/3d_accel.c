#include <Windows.h>
#include <string.h>
#include <stdio.h>

#define SVGA
#include "3d_accel.h"

static FBHDA_t *hda = NULL;
static HANDLE hda_vxd = INVALID_HANDLE_VALUE;
static SVGA_DB_t *svga_db = NULL;
static SVGA_OT_info_entry_t *svga_ot = NULL;
static HANDLE svga_mux = INVALID_HANDLE_VALUE;
static BOOL svga_have_3d = FALSE;

static void SVGA_CMB_clean();
#define CMB_TABLE_SIZE 64
static DWORD *cmb_table[64];

void FBHDA_load()
{
	HWND hDesktop = GetDesktopWindow();
	HDC hdc = GetDC(hDesktop);
	char strbuf[PATH_MAX];
	svga_have_3d = FALSE;
	
	memset(&cmb_table[0], 0, sizeof(cmb_table));
	
	if(ExtEscape(hdc, OP_FBHDA_SETUP, 0, NULL, sizeof(FBHDA_t *), (LPVOID)&hda))
	{
		if(hda != NULL)
		{
			if(hda->cb == sizeof(FBHDA_t) && hda->version == API_3DACCEL_VER)
			{
				strcpy(strbuf, "\\\\.\\");
				strcat(strbuf, hda->vxdname);
				
				hda_vxd = CreateFileA(strbuf, 0, 0, 0, CREATE_NEW, FILE_FLAG_DELETE_ON_CLOSE, 0);
				if(hda_vxd != INVALID_HANDLE_VALUE)
				{
					if(SVGA_valid()) /* SVGA, try get database */
					{
						if(DeviceIoControl(hda_vxd, OP_SVGA_DB_SETUP,
							NULL, 0, (LPVOID)&svga_db, sizeof(SVGA_DB_t *),
							NULL, NULL))
						{
							if(svga_db != NULL)
							{
								char mutexname[PATH_MAX];
								
								/* try to load otable */
								DeviceIoControl(hda_vxd, OP_SVGA_OT_SETUP,
									NULL, 0, (LPVOID)&svga_ot, sizeof(SVGA_OT_info_entry_t *),
									NULL, NULL);
								
								strcpy(mutexname, "global\\");
								strcat(mutexname, svga_db->mutexname);
								
								svga_mux = CreateMutexA(NULL, FALSE, mutexname);
								
								svga_have_3d = TRUE; /* SUCCESS = SVGA3D */
							}
						}
					}
					return; /* SUCCESS = frame buffer */
				}
			}
		}
	}
	
	hda = NULL;
}

static BOOL FBHDA_valid()
{
	return hda_vxd != INVALID_HANDLE_VALUE;
}

void FBHDA_free()
{
	if(SVGA_valid())
	{
		SVGA_CMB_clean();
		//SVGA_flushcache();
	}
	
	if(hda_vxd != INVALID_HANDLE_VALUE)
	{
		CloseHandle(hda_vxd);
	}
	
	if(svga_mux != INVALID_HANDLE_VALUE)
	{
		CloseHandle(svga_mux);
	}
}

FBHDA_t *FBHDA_setup()
{
	return hda;
}

void FBHDA_access_begin(DWORD flags)
{
	if(!FBHDA_valid()) return;

	DeviceIoControl(hda_vxd, OP_FBHDA_ACCESS_BEGIN, &flags, sizeof(DWORD), NULL, 0, NULL, NULL);
}

void FBHDA_access_end(DWORD flags)
{
	if(!FBHDA_valid()) return;

	DeviceIoControl(hda_vxd, OP_FBHDA_ACCESS_END, &flags, sizeof(DWORD), NULL, 0, NULL, NULL);
}

void FBHDA_access_rect(DWORD left, DWORD top, DWORD right, DWORD bottom)
{
	if(!FBHDA_valid()) return;
		
	DWORD rect[4] = {left, top, right, bottom};

	DeviceIoControl(hda_vxd, OP_FBHDA_ACCESS_RECT, &rect, sizeof(rect), NULL, 0, NULL, NULL);
}

BOOL FBHDA_swap(DWORD offset)
{
	DWORD result = 0;
	
	if(!FBHDA_valid()) return FALSE;
	
	if(DeviceIoControl(hda_vxd, OP_FBHDA_SWAP,
		&offset, sizeof(DWORD), &result, sizeof(DWORD),
		NULL, NULL))
	{
		return result == 0 ? FALSE : TRUE;
	}
	
	return FALSE;
}

void FBHDA_clean()
{
	if(!FBHDA_valid()) return;

	DeviceIoControl(hda_vxd, OP_FBHDA_CLEAN, NULL, 0, NULL, 0, NULL, NULL);
}

BOOL SVGA_valid()
{
	DWORD result = 0;
	
	if(!FBHDA_valid()) return FALSE;
	
	if(DeviceIoControl(hda_vxd, OP_SVGA_VALID,
		NULL, 0, &result, sizeof(DWORD),
		NULL, NULL))
	{
		return result == 0 ? FALSE : TRUE;
	}
	
	return FALSE;
}

static BOOL SVGA3D_enabled()
{
	return svga_have_3d;
}

DWORD *SVGA_CMB_alloc()
{
	DWORD *ptr = NULL;
	
	if(!SVGA3D_enabled()) return NULL;
	
	if(DeviceIoControl(hda_vxd, OP_SVGA_CMB_ALLOC,
		NULL, 0, &ptr, sizeof(DWORD),
		NULL, NULL))
	{
		if(ptr)
		{
			int i;
			for(i = 0; i < CMB_TABLE_SIZE; i++)
			{
				if(cmb_table[i] == NULL)
				{
					cmb_table[i] = ptr;
					return ptr;
				}
			}
			
			/* we have more than 64 CMBs, something is realy weird, free CMB and return NULL */
			DeviceIoControl(hda_vxd, OP_SVGA_CMB_FREE, &ptr, sizeof(DWORD), NULL, 0, NULL, NULL);
		}
		
		return NULL;
	}
	
	return NULL;
}

void SVGA_CMB_free(DWORD *cmb)
{
	int i;
	
	if(!SVGA3D_enabled()) return;
	
	for(i = 0; i < CMB_TABLE_SIZE; i++)
	{
		if(cmb_table[i] == cmb)
		{
			cmb_table[i] = NULL;
			
			/* free CMB only if is on table, just in case */
			DeviceIoControl(hda_vxd, OP_SVGA_CMB_FREE,
				&cmb, sizeof(DWORD), NULL, 0,
				NULL, NULL);
				
			return;
		}
	}
}

/* destroy all alocated CMBs */
static void SVGA_CMB_clean()
{
	int i;
	
	for(i = 0; i < CMB_TABLE_SIZE; i++)
	{
		if(cmb_table[i] != NULL)
		{
			SVGA_CMB_free(cmb_table[i]);
		}
	}
}

void SVGA_CMB_submit(DWORD *cmb, DWORD cmb_size, SVGA_CMB_status_t *status, DWORD flags, DWORD DXCtxId)
{
	SVGA_CMB_submit_io_t iobuf;
	SVGA_CMB_status_t status_io;
	
	if(!SVGA3D_enabled()) return;
	
	iobuf.cmb      = cmb;
	iobuf.cmb_size = cmb_size;
	iobuf.flags    = flags;
	iobuf.DXCtxId  = DXCtxId;
	
	if(DeviceIoControl(hda_vxd, OP_SVGA_CMB_SUBMIT,
		&iobuf, sizeof(SVGA_CMB_submit_io_t), &status_io, sizeof(SVGA_CMB_status_t),
		NULL, NULL))
	{
		if(status)
		{
			*status = status_io;
		}
	}
}

DWORD SVGA_fence_get()
{
	DWORD fence = 0;
	
	if(!SVGA3D_enabled()) return 0;
		
	if(DeviceIoControl(hda_vxd, OP_SVGA_FENCE_GET,
		NULL, 0, &fence, sizeof(DWORD),
		NULL, NULL))
	{
		return fence;
	}

	return 0;
}

void SVGA_fence_query(DWORD FBPTR ptr_fence_passed, DWORD FBPTR ptr_fence_last)
{
	DWORD results[2];
	
	if(!SVGA3D_enabled()) return;
	
	if(DeviceIoControl(hda_vxd, OP_SVGA_FENCE_QUERY,
		NULL, 0, &results, sizeof(DWORD)*2,
		NULL, NULL))
	{
		*ptr_fence_passed = results[0];
		*ptr_fence_last   = results[1];
	}

	return;
}


void SVGA_fence_wait(DWORD fence_id)
{
	if(!SVGA3D_enabled()) return;
		
	DeviceIoControl(hda_vxd, OP_SVGA_FENCE_WAIT,
		&fence_id, sizeof(DWORD), NULL, 0,
		NULL, NULL);
}

BOOL SVGA_region_create(SVGA_region_info_t *rinfo)
{
	SVGA_region_info_t result;
	
	if(!SVGA3D_enabled()) return FALSE;
		
	if(rinfo->region_id == 0) return FALSE;
	
	if(DeviceIoControl(hda_vxd, OP_SVGA_REGION_CREATE,
		(LPVOID)rinfo, sizeof(SVGA_region_info_t), (LPVOID)&result, sizeof(SVGA_region_info_t),
		NULL, NULL))
	{
		memcpy(rinfo, &result, sizeof(SVGA_region_info_t));
				
		if(rinfo->address != NULL)
		{
			return TRUE;
		}
	}
	
	return FALSE;
	
}

void SVGA_region_free(SVGA_region_info_t FBPTR rinfo)
{
	if(!SVGA3D_enabled()) return;
		
	if(rinfo->region_id == 0) return;
	
	DeviceIoControl(hda_vxd, OP_SVGA_REGION_FREE,
		(LPVOID)rinfo, sizeof(SVGA_region_info_t), NULL, 0,
		NULL, NULL);
}

DWORD SVGA_query(DWORD type, DWORD index)
{
	DWORD ioin[2] = {
		type,
		index
	};
	DWORD result = 0;
	
	if(!FBHDA_valid()) return result;
	
	if(DeviceIoControl(hda_vxd, OP_SVGA_QUERY,
		(LPVOID)ioin, sizeof(DWORD)*2, (LPVOID)result, sizeof(LPVOID),
		NULL, NULL))
	{
		return result;
	}
	
	return result;
}

void SVGA_query_vector(DWORD type, DWORD index_start, DWORD count, DWORD *out)
{
	DWORD ioin[3] = {
		type,
		index_start,
		count
	};
	
	if(!FBHDA_valid()) return;
	
	DeviceIoControl(hda_vxd, OP_SVGA_QUERY_VECTOR,
		(LPVOID)ioin, sizeof(DWORD)*3, (LPVOID)out, sizeof(DWORD)*count,
		NULL, NULL);
}

SVGA_DB_t *SVGA_DB_setup()
{
	return svga_db;
}

SVGA_OT_info_entry_t *SVGA_OT_setup()
{
	return svga_ot;
}

void SVGA_DB_lock()
{
	WaitForSingleObject(svga_mux, INFINITE);
	// if(wait_rc == WAIT_OBJECT_0)
}

void SVGA_DB_unlock()
{
	ReleaseMutex(svga_mux);
}

void SVGA_flushcache()
{
	if(!SVGA3D_enabled()) return;
		
	DeviceIoControl(hda_vxd, OP_SVGA_FLUSHCACHE,
		NULL, 0, NULL, 0,
		NULL, NULL);
}

BOOL SVGA_vxdcmd(DWORD cmd, DWORD arg)
{
	DWORD result = 0;
	DWORD buffer[2] = {
		cmd,
		arg
	};
	
	if(!FBHDA_valid()) return FALSE;
	
	if(DeviceIoControl(hda_vxd, OP_SVGA_VXDCMD,
		(LPVOID)&buffer[0], sizeof(buffer), (LPVOID)result, sizeof(DWORD),
		NULL, NULL))
	{
		return result == 0 ? FALSE : TRUE;
	}
	
	return FALSE;
}
