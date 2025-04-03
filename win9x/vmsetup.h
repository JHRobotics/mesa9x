/******************************************************************************
 * Copyright (c) 2025 Jaroslav Hensl                                          *
 *                                                                            *
 * Permission is hereby granted, free of charge, to any person                *
 * obtaining a copy of this software and associated documentation             *
 * files (the "Software"), to deal in the Software without                    *
 * restriction, including without limitation the rights to use,               *
 * copy, modify, merge, publish, distribute, sublicense, and/or sell          *
 * copies of the Software, and to permit persons to whom the                  *
 * Software is furnished to do so, subject to the following                   *
 * conditions:                                                                *
 *                                                                            *
 * The above copyright notice and this permission notice shall be             *
 * included in all copies or substantial portions of the Software.            *
 *                                                                            *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,            *
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES            *
 * OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND                   *
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT                *
 * HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,               *
 * WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING               *
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR              *
 * OTHER DEALINGS IN THE SOFTWARE.                                            *
 *                                                                            *
 ******************************************************************************/
#ifndef __VMSETUP_H__INCLUDED__
#define __VMSETUP_H__INCLUDED__

#ifdef VMHAL9X_LIB
const char *vmhal_setup_str(const char *category, const char *name, BOOL empty_str);
DWORD vmhal_setup_dw(const char *category, const char *name);
void vmhal_setup_load(BOOL compat);
#else

typedef const char *(*vmhal_setup_str_h)(const char *category, const char *name, BOOL empty_str);
typedef DWORD (*vmhal_setup_dw_h)(const char *category, const char *name);

static inline void *vmhal_setup_proc(const char *procname)
{
	HMODULE vmdisp = GetModuleHandleA("vmdisp9x.dll");
	if(vmdisp == NULL)
	{
		vmdisp = LoadLibraryA("vmdisp9x.dll");
	}
	
	if(vmdisp)
	{
		return GetProcAddress(vmdisp, procname);
	}
	
	return NULL;
}

static inline const char *vmhal_setup_str(const char *category, const char *name, BOOL empty_str)
{
	vmhal_setup_str_h p = (vmhal_setup_str_h)vmhal_setup_proc("vmhal_setup_str");
	if(p)
	{
		return p(category, name, empty_str);
	}
	
	if(empty_str)
		return "";
	
	return NULL;
}

static inline DWORD vmhal_setup_dw(const char *category, const char *name)
{
	vmhal_setup_dw_h p = (vmhal_setup_dw_h)vmhal_setup_proc("vmhal_setup_dw");
	if(p)
	{
		return p(category, name);
	}
	
	return 0;
}

#endif /* !VMHAL9X_LIB */

#endif /* __VMSETUP_H__INCLUDED__ */
