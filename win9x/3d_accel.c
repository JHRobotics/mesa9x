#include <Windows.h>
#include <string.h>
#include <stdio.h>

#include "3d_accel.h"

static fbhda_lib_t fbhda_lib = {NULL, 0};
static BOOL lib_invalid = FALSE; /* error_latch */

#define LoadAddress(_n) \
	fbhda_lib.p ## _n = (_n ## _t)GetProcAddress(fbhda_lib.lib, #_n); \
	if(fbhda_lib.p ## _n == NULL){break;}

static BOOL FBHFA_load_lib()
{
	if(lib_invalid)
	{
		return FALSE;
	}
	
	if(fbhda_lib.lib != NULL)
	{
		return TRUE;
	}

	fbhda_lib.lib = GetModuleHandleA(VMDISP9X_LIB);
	if(fbhda_lib.lib == NULL)
	{
		fbhda_lib.lib = LoadLibraryA(VMDISP9X_LIB);
	}
	
	if(fbhda_lib.lib)
	{
		do
		{
			LoadAddress(FBHDA_setup)
			LoadAddress(FBHDA_access_begin)
			LoadAddress(FBHDA_access_end)
			LoadAddress(FBHDA_access_rect)
			LoadAddress(FBHDA_swap)
			LoadAddress(FBHDA_clean)
			
			return TRUE;
		} while(0);
	}

	fbhda_lib.lib = NULL;
	lib_invalid = TRUE;
	return FALSE;
}

#undef LoadAddress


void FBHDA_load()
{
	/* DLL main, not use LoadLibraryA here */
}

void FBHDA_free()
{
	/* NOP */
}

void FBHDA_access_begin(DWORD flags)
{
	if(FBHFA_load_lib())
	{
		fbhda_lib.pFBHDA_access_begin(flags);
	}
}

void FBHDA_access_end(DWORD flags)
{
	if(FBHFA_load_lib())
	{
		fbhda_lib.pFBHDA_access_end(flags);
	}
}

BOOL FBHDA_swap(DWORD offset)
{
	if(FBHFA_load_lib())
	{
		return fbhda_lib.pFBHDA_swap(offset);
	}
	return FALSE;
}

void FBHDA_clean()
{
	if(FBHFA_load_lib())
	{
		fbhda_lib.pFBHDA_clean();
	}
}

FBHDA_t *FBHDA_setup()
{
	if(FBHFA_load_lib())
	{
		return fbhda_lib.pFBHDA_setup();
	}

	return NULL;
}
