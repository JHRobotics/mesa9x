#include <windows.h>
#include <time.h>
#include <stdio.h>
#include <stdarg.h>

#define WGL_WGLEXT_PROTOTYPES

#include <GL/gl.h>
#include <GL/wglext.h>

#include "glapi/glapi.h"
#include "stw_device.h"
#include "stw_icd.h"
#include "stw_nopfuncs.h"

#include "util/u_debug.h"

void debug9x_printf(const char *filename, int line, const char *msg, ...)
{
  time_t rawtime;
  struct tm * timeinfo;
  FILE *lfile;
  va_list args;

	lfile = fopen("C:\\mesa3d.txt", "ab");
	if(lfile)
	{
		time (&rawtime);
		timeinfo = localtime(&rawtime);
		fprintf(lfile, "[%04d-%02d-%02d %02d:%02d:%02d] %s:%d ",
			timeinfo->tm_year+1900,
			timeinfo->tm_mon+1,
			timeinfo->tm_mday,
			timeinfo->tm_hour,
			timeinfo->tm_min,
			timeinfo->tm_sec,
			filename,
			line
		);
 	 
		va_start(args, msg);
		vfprintf(lfile, msg, args);
		va_end(args);
		
		fprintf(lfile, "\r\n");
 	 
		fclose(lfile);
	}
}

#define DEBUG9X_MSG(_msg, ...) debug9x_printf(__FILE__, __LINE__, _msg, ##__VA_ARGS__)

/*
BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, void *reserved)
{	
  switch (reason)
  {
    case DLL_PROCESS_ATTACH:
    	DEBUG9X_MSG("DLL_PROCESS_ATTACH");
			break;

    case DLL_PROCESS_DETACH:
    	DEBUG9X_MSG("DLL_PROCESS_DETACH");
			break;
		
    default:
    	DEBUG9X_MSG("unknown reason: %d", reason);
    	break;
	}

	return TRUE;
}*/
