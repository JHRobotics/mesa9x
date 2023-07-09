#define UNLOAD_PROTECTED
#include "libgl_gdi.c"

HRESULT WINAPI DllCanUnloadNow()
{
   return S_FALSE;
}
