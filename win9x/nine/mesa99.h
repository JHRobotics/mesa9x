#ifndef __MESA99_H__INCLUDED__
#define __MESA99_H__INCLUDED__

#include "wgl/pipe_access.h"

typedef struct _INineNine
{
	IDirect3D9Ex base;
	LONG refcount;
	struct d3dadapter9_context ctx;
	ID3DAdapter9 *adapter9;
	struct pipe_screen *screen;
	struct stw_context *gdi_ctx;
} INineNine;

HRESULT ID3DPresentGroup_new(INineNine *nine, HWND hFocusWindow, ID3DPresentGroup **pp);

extern MesaScreenCreateH MesaScreenCreateProc;
extern MesaPresentH MesaPresentProc;
extern MesaDimensionsH MesaDimensionsProc;

void nine_lock_proc();
void nine_unlock_proc();
void nine_restore_screen();

#define nine_lock() do{mesa99_dbg("-> lock");nine_lock_proc();}while(0)
#define nine_unlock() do{mesa99_dbg("<- unlock");nine_lock_proc();}while(0)

extern volatile BOOL wine_hook_disabled;

BOOL nine_init();
void nine_deinit();

BOOL mesa99_init();
void mesa99_cleanup();

void mesa99_printf(const char *file, const char *fn, int line, const char *fmt, ...);

#ifdef DEBUG
#define mesa99_dbg(_fmt, ...) mesa99_printf(__FILE__, __FUNCTION__, __LINE__, _fmt __VA_OPT__(,) __VA_ARGS__)
#else
#define mesa99_dbg(_fmt, ...) do{}while(0)
#endif

#endif /* __MESA99_H__INCLUDED__ */
