/*
  simple spinlock, can be easy implemented in PM32 RING-3 DLL,
  PM32 RING-0 VXD and PM16 RING-3 DRV.
*/

#include <windows.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdint.h>
#include "vramcpy.h"


BOOL vram_lock(volatile DWORD *ptr)
{
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

void vram_unlock(volatile DWORD *ptr)
{
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
