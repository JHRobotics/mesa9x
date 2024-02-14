#ifndef GDI_SW_WINSYS_H
#define GDI_SW_WINSYS_H

#include <windows.h>

#include "pipe/p_compiler.h"
#include "frontend/sw_winsys.h"

void gdi_sw_display( struct sw_winsys *winsys,
                     struct sw_displaytarget *dt,
                     HDC hDC );

struct gdi_sw_displaytarget
{
   enum pipe_format format;
   unsigned width;
   unsigned height;
   unsigned stride;

   unsigned size;

   void *data;

   BITMAPINFO bmi;
};

struct sw_winsys *
gdi_create_sw_winsys(void);

#endif
