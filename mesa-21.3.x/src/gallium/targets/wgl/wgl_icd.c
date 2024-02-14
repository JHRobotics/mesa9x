#define UNLOAD_PROTECTED
#include "wgl.c"

HRESULT WINAPI DllCanUnloadNow()
{
   return S_FALSE;
}
