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

void FBHDA_load()
{
	HWND hDesktop = GetDesktopWindow();
	HDC hdc = GetDC(hDesktop);
	char strbuf[PATH_MAX];
	
	if(ExtEscape(hdc, OP_FBHDA_SETUP, 0, NULL, sizeof(FBHDA_t *), (LPVOID)&hda))
	{
		if(hda != NULL)
		{
			if(hda->cb == sizeof(FBHDA_t))
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
								
								return; /* SUCCESS */
							}
						}
					}
					else
					{
						return; /* SUCCESS */
					}
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

DWORD *SVGA_CMB_alloc()
{
	DWORD *ptr = NULL;
	
	if(!FBHDA_valid()) return NULL;
	
	if(DeviceIoControl(hda_vxd, OP_SVGA_CMB_ALLOC,
		NULL, 0, &ptr, sizeof(DWORD),
		NULL, NULL))
	{
		return ptr;
	}
	
	return NULL;
}

void SVGA_CMB_free(DWORD *cmb)
{
	if(!FBHDA_valid()) return;
	
	DeviceIoControl(hda_vxd, OP_SVGA_CMB_FREE,
		&cmb, sizeof(DWORD), NULL, 0,
		NULL, NULL);
}

void SVGA_CMB_submit(DWORD *cmb, DWORD cmb_size, SVGA_CMB_status_t *status, DWORD flags, DWORD DXCtxId)
{
	SVGA_CMB_submit_io_t iobuf;
	SVGA_CMB_status_t status_io;;
	
	if(!FBHDA_valid()) return;
	
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
			if(status->qStatus == NULL)
			{
				status->qStatus = &(status->sStatus);
			}
		}
	}
}

DWORD SVGA_fence_get()
{
	DWORD fence = 0;
	
	if(!FBHDA_valid()) return 0;
		
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
	
	if(!FBHDA_valid()) return;
		
	
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
	if(!FBHDA_valid()) return;
		
	DeviceIoControl(hda_vxd, OP_SVGA_FENCE_WAIT,
		&fence_id, sizeof(DWORD), NULL, 0,
		NULL, NULL);
}

BOOL SVGA_region_create(SVGA_region_info_t *rinfo)
{
	SVGA_region_info_t result;
	
	if(!FBHDA_valid()) return FALSE;
		
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
	if(!FBHDA_valid()) return;
		
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
