/**************************************************************************
 *
 * Copyright 2008-2010 Vmware, Inc.
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sub license, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT.
 * IN NO EVENT SHALL VMWARE AND/OR ITS SUPPLIERS BE LIABLE FOR
 * ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 **************************************************************************/


#include "os_misc.h"

#include <stdarg.h>


#if defined(PIPE_SUBSYSTEM_WINDOWS_USER)

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN      // Exclude rarely-used stuff from Windows headers
#endif
#include <windows.h>
#include <stdio.h>

#else

#include <stdio.h>
#include <stdlib.h>

#endif


#if defined(PIPE_OS_LINUX) || defined(PIPE_OS_CYGWIN) || defined(PIPE_OS_SOLARIS)
#  include <unistd.h>
#elif defined(PIPE_OS_APPLE) || defined(PIPE_OS_BSD)
#  include <sys/sysctl.h>
#elif defined(PIPE_OS_HAIKU)
#  include <kernel/OS.h>
#elif defined(PIPE_OS_WINDOWS)
#  include <windows.h>
#else
#error unexpected platform in os_sysinfo.c
#endif

#ifdef VBOX_WITH_MESA3D_DBG
extern DECLCALLBACK(void) VBoxWddmUmLog(const char *pszString);
#endif

void
os_log_message(const char *message)
{
   /* If the GALLIUM_LOG_FILE environment variable is set to a valid filename,
    * write all messages to that file.
    */
   static FILE *fout = NULL;

   if (!fout) {
#ifdef DEBUG
      /* one-time init */
      const char *filename = os_get_option("GALLIUM_LOG_FILE");
      if (filename) {
         const char *mode = "w";
         if (filename[0] == '+') {
            /* If the filename is prefixed with '+' then open the file for
             * appending instead of normal writing.
             */
            mode = "a";
            filename++; /* skip the '+' */
         }
         fout = fopen(filename, mode);
      }
#endif
      if (!fout)
         fout = stderr;
   }

#if defined(WIN9X)
   fflush(stdout);
   fputs(message, fout);
   fflush(fout);
#elif defined(PIPE_SUBSYSTEM_WINDOWS_USER)
   OutputDebugStringA(message);
   if(GetConsoleWindow() && !IsDebuggerPresent()) {
      fflush(stdout);
      fputs(message, fout);
      fflush(fout);
   }
   else if (fout != stderr) {
      fputs(message, fout);
      fflush(fout);
   }
#else /* !PIPE_SUBSYSTEM_WINDOWS */
   fflush(stdout);
   fputs(message, fout);
   fflush(fout);
#endif
}


#if !defined(PIPE_SUBSYSTEM_EMBEDDED)
# ifndef WIN9X
const char *
os_get_option(const char *name)
{
   return getenv(name);
}
# else
#define OS_GET_BUFFER_SIZE 128
static char os_get_buffer[OS_GET_BUFFER_SIZE];

int crt_sse2_is_safe();

const char *
os_get_option(const char *name)
{
	char tmppath[MAX_PATH];
	char regpath[MAX_PATH];
  const char *dirs[2] = {
  	"global",
  	NULL
  };
  int dirs_cnt = 1;
  
  const char *env = getenv(name);
  
  /* 
   * try read option from registry, checked 2 locations:
   *   HKLM\Software\Mesa3D\<application.exe>\<name>
   *   HKLM\Software\Mesa3D\global\<name>
   *
   * So user can set option globaly or for selected application
   */
  if(env == NULL)
  {
	  if(GetModuleFileNameA(NULL, tmppath, MAX_PATH) > 0)
	  {
	    char *ptr = strrchr(tmppath, '\\');
	    if(ptr != NULL && strlen(ptr) > 1)
	    {
	    	dirs[1] = ptr+1;
	    	dirs_cnt++;
	    }
	  }
   
	  while(dirs_cnt > 0 && env == NULL)
	  {
	    HKEY  hKey;
	    LSTATUS lResult;
	    
	    dirs_cnt--;
	    
	    if(sizeof("Software\\Mesa3D\\") + strlen(dirs[dirs_cnt]) >= MAX_PATH)
	    {
	    	continue;
	    }
	    
	    strcpy(regpath, "Software\\Mesa3D\\");
	    strcat(regpath, dirs[dirs_cnt]);
	    
	    lResult = RegOpenKeyEx (HKEY_LOCAL_MACHINE, regpath, 0, KEY_READ, &hKey);
	
	    if(lResult == ERROR_SUCCESS)
	    {
	    	DWORD size = OS_GET_BUFFER_SIZE;
	    	DWORD type = 0;
	    	DWORD temp_dw = 0;
	    	
	      lResult = RegQueryValueExA(hKey, name, NULL, &type, os_get_buffer, &size);
	      
	      if(lResult == ERROR_SUCCESS)
	      {
	        switch(type)
	        {
	          case REG_SZ:
	          case REG_MULTI_SZ:
	          case REG_EXPAND_SZ:
	          	env = os_get_buffer;
	            break;
	          case REG_DWORD:
	          	temp_dw = *((LPDWORD)os_get_buffer);
	          	sprintf(os_get_buffer, "%u", temp_dw);
	          	env = os_get_buffer;
	          	break;
	        }
	      }
	      RegCloseKey(hKey);
	    }
	  } // while
  } // if env == NULL
  
  if(env == NULL)
  {
  	#ifdef HAVE_CRTEX
      if(strcmp(name, "GALLIUM_NOSSE") == 0)
      {
      	if(crt_sse2_is_safe() == 0)
      	{
      		env = "1";
      	}
      }
    #endif
  }
  
  
  return env;
}
# endif
#endif /* !PIPE_SUBSYSTEM_EMBEDDED */


/**
 * Return the size of the total physical memory.
 * \param size returns the size of the total physical memory
 * \return true for success, or false on failure
 */
bool
os_get_total_physical_memory(uint64_t *size)
{
#if defined(PIPE_OS_LINUX) || defined(PIPE_OS_CYGWIN) || defined(PIPE_OS_SOLARIS)
   const long phys_pages = sysconf(_SC_PHYS_PAGES);
   const long page_size = sysconf(_SC_PAGE_SIZE);

   if (phys_pages <= 0 || page_size <= 0)
      return false;

   *size = (uint64_t)phys_pages * (uint64_t)page_size;
   return true;
#elif defined(PIPE_OS_APPLE) || defined(PIPE_OS_BSD)
   size_t len = sizeof(*size);
   int mib[2];

   mib[0] = CTL_HW;
#if defined(PIPE_OS_APPLE)
   mib[1] = HW_MEMSIZE;
#elif defined(PIPE_OS_NETBSD) || defined(PIPE_OS_OPENBSD)
   mib[1] = HW_PHYSMEM64;
#elif defined(PIPE_OS_FREEBSD)
   mib[1] = HW_REALMEM;
#elif defined(PIPE_OS_DRAGONFLY)
   mib[1] = HW_PHYSMEM;
#else
#error Unsupported *BSD
#endif

   return (sysctl(mib, 2, size, &len, NULL, 0) == 0);
#elif defined(PIPE_OS_HAIKU)
   system_info info;
   status_t ret;

   ret = get_system_info(&info);
   if (ret != B_OK || info.max_pages <= 0)
      return false;

   *size = (uint64_t)info.max_pages * (uint64_t)B_PAGE_SIZE;
   return true;
#elif defined(PIPE_OS_WINDOWS)
#ifdef WIN9X
  MEMORYSTATUS status;
  status.dwLength = sizeof(status);
  GlobalMemoryStatus(&status);
  *size = (unsigned long long)status.dwMemoryLoad;

#else
   MEMORYSTATUSEX status;
   BOOL ret;

   status.dwLength = sizeof(status);
   ret = GlobalMemoryStatusEx(&status);
   *size = status.ullTotalPhys;
   
   return (ret == TRUE);
#endif
#else
#error unexpected platform in os_sysinfo.c
   return false;
#endif
}
