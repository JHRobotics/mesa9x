/******************************************************************************
 * Copyright (c) 2023-2024 Jaroslav Hensl                                     *
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
*******************************************************************************/
#include <windows.h>
#include <stdio.h>
#include <stdarg.h>
#include <GL/gl.h>
#include <GL/glext.h>
#include <math.h>

static const char *usage = 
  "wgltest [command <command arg>] [another command <...>]\n\n"
  "supported commands:\n"
  "system: use opengl through opengl32.dll and gdi32.dll\n"
  "wrapper <library.dll>: use opengl from library wrapping original opengl32.dll\n"
  "driver <library.dll>: use opengl from ICD driver\n"
  "width <pixels>: set width of testing window\n"
  "height <pixels>: set height of testing window\n"
  "window: run test in window\n"
  "fullscreen: run test on fullscreen\n"
  "help: show this message";

static void GUIError(const char *fmt, ...)
{
	static char msgbuf[250];
	
	va_list args;
	va_start(args, fmt);
	vsprintf(msgbuf, fmt, args);
	va_end(args);
	
	MessageBoxA(NULL, msgbuf, "WGLtest: error", MB_OK | MB_ICONHAND);
}

#define FUNC_GL     1
#define FUNC_WGL    2
#define FUNC_DRV    3
#define FUNC_WRAP   4
#define FUNC_WINAPI 5

#define LIB_GL32    1
#define LIB_GDI32   2
#define LIB_DRV     3

#define GL_TYPE_SYSTEM  1
#define GL_TYPE_WRAPPER 2
#define GL_TYPE_DRV     3

typedef void (APIENTRY *glClearColor_f)(GLfloat red, GLfloat green, GLfloat blue, GLfloat alpha);
typedef void (APIENTRY *glClear_f)(GLbitfield mask);
typedef void (APIENTRY *glPushMatrix_f)(void);
typedef void (APIENTRY *glPopMatrix_f)(void);
typedef void (APIENTRY *glBegin_f)(GLenum mode);
typedef void (APIENTRY *glEnd_f)(void);
typedef void (APIENTRY *glRotatef_f)(GLfloat angle, GLfloat x, GLfloat y, GLfloat z);
typedef void (APIENTRY *glColor3f_f)(GLfloat red, GLfloat green, GLfloat blue);
typedef void (APIENTRY *glVertex3f_f)(GLfloat x, GLfloat y, GLfloat z);
typedef void (APIENTRY *glFlush_f)(void);
typedef const GLubyte* (APIENTRY *glGetString_f)(GLenum name);
typedef void (APIENTRY *glGetIntegerv_f)(GLenum pname, GLint *data);

typedef BOOL (WINAPI *wglMakeCurrent_f)(HDC hdc, HGLRC hglrc);
typedef HGLRC (WINAPI *wglCreateContext_f)(HDC hdc);
typedef BOOL (WINAPI *wglDeleteContext_f)(HGLRC hglrc);

typedef PROC (WINAPI *wglGetProcAddress_f)(LPCSTR name);

typedef BOOL (WINAPI *SetPixelFormat_f)(HDC hdc, int format, const PIXELFORMATDESCRIPTOR *ppfd);
typedef BOOL (WINAPI *SwapBuffers_f)(HDC hdc);
typedef int  (WINAPI *ChoosePixelFormat_f)(HDC hdc, const PIXELFORMATDESCRIPTOR *ppfd);
typedef int  (WINAPI *DescribePixelFormat_f)(HDC hdc, int iPixelFormat, UINT nBytes, LPPIXELFORMATDESCRIPTOR ppfd);

typedef int  (APIENTRY *wglChoosePixelFormat_f)(HDC hdc, CONST PIXELFORMATDESCRIPTOR *ppfd);
typedef BOOL (APIENTRY *wglSetPixelFormat_f)(HDC hdc, int iPixelFormat, const PIXELFORMATDESCRIPTOR *ppfd);
typedef BOOL (APIENTRY *wglSwapBuffers_f)(HDC hdc);
typedef int  (APIENTRY *wglDescribePixelFormat_f)(HDC hdc, int iPixelFormat, UINT nBytes, LPPIXELFORMATDESCRIPTOR ppfd);

typedef BOOL (APIENTRY *DrvSetPixelFormat_f)(HDC hdrc, LONG format);
typedef BOOL (APIENTRY *DrvSwapBuffers_f)(HDC hdc);
typedef LONG (APIENTRY *DrvDescribePixelFormat_f)(HDC hdc, int iPixelFormat, UINT nBytes, PIXELFORMATDESCRIPTOR* ppfd);
typedef PROC (APIENTRY *DrvGetProcAddress_f)(LPCSTR name);
typedef void* (APIENTRY *DrvSetContext_f)(HDC hdc, HGLRC dhglrc, void *pfnSetProcTable);

typedef HGLRC (APIENTRY *DrvCreateLayerContext_f)(HDC hdc, INT iLayerPlane);
typedef BOOL  (APIENTRY *DrvReleaseContext_f)(HGLRC dhglrc);

#define GL_API_LOAD(_n, _t, _l) _n ## _f _n;
struct
{
	#include "wgltest_api.h"
	HMODULE gllib;
	HMODULE gdilib;
	int type;
} GL32;
#undef GL_API_LOAD

static BOOL gl_load_system()
{
	memset(&GL32, 0, sizeof(GL32));
	
	GL32.gllib = LoadLibraryA("opengl32.dll");
	if(GL32.gllib == NULL)
	{
		GUIError("Failure: LoadLibraryA(opengl32.dll) = 0x%X", GetLastError());
		return FALSE;
	}
	
	GL32.gdilib = LoadLibraryA("gdi32.dll");
	if(GL32.gdilib == NULL)
	{
		GUIError("Failure: LoadLibraryA(gdi32.dll) = 0x%X", GetLastError());
		FreeLibrary(GL32.gllib);
		GL32.gllib = NULL;
		return FALSE;
	}
	
#define GL_API_LOAD(_n, _t, _l) if(_l == LIB_GL32){ \
		GL32._n = (_n ## _f)GetProcAddress(GL32.gllib, #_n); \
		if(GL32._n == NULL && _t != FUNC_WRAP){GUIError("Symbol '%s' not found in opengl32.dll", #_n); return FALSE;} \
	}else if(_l == LIB_GDI32){ \
		GL32._n = (_n ## _f)GetProcAddress(GL32.gdilib, #_n); \
		if(GL32._n == NULL){GUIError("Symbol '%s' not found in gdi32.dll", #_n); return FALSE;} \
	}
	
	#include "wgltest_api.h"

#undef GL_API_LOAD

	GL32.type = GL_TYPE_SYSTEM;

	return TRUE;	
}

static BOOL gl_load_wrapper(const char *dll)
{
	memset(&GL32, 0, sizeof(GL32));
	
	GL32.gllib = LoadLibraryA(dll);
	if(GL32.gllib == NULL)
	{
		GUIError("Failure: LoadLibraryA(%s) = 0x%X", dll, GetLastError());
		return FALSE;
	}
	
	GL32.gdilib = LoadLibraryA("gdi32.dll");
	if(GL32.gdilib == NULL)
	{
		GUIError("Failure: LoadLibraryA(gdi32.dll) = 0x%X", GetLastError());
		FreeLibrary(GL32.gllib);
		return FALSE;
	}
	
#define GL_API_LOAD(_n, _t, _l) if(_l == LIB_GL32){ \
		GL32._n = (_n ## _f)GetProcAddress(GL32.gllib, #_n); \
		if(GL32._n == NULL){GUIError("Symbol '%s' not found in %s\n", #_n, dll); return FALSE;} \
	}else if(_l == LIB_GDI32){ \
		GL32._n = (_n ## _f)GetProcAddress(GL32.gdilib, #_n); \
		if(GL32._n == NULL && _t != LIB_GDI32){GUIError("Symbol '%s' not found in gdi32.dll", #_n); return FALSE;} \
	}
	
	#include "wgltest_api.h"

#undef GL_API_LOAD

	GL32.type = GL_TYPE_WRAPPER;

	return TRUE;	
}

static BOOL gl_load_driver(const char *dll)
{
	memset(&GL32, 0, sizeof(GL32));
	
	GL32.gllib = LoadLibraryA(dll);
	if(GL32.gllib == NULL)
	{
		GUIError("Failure: LoadLibraryA(%s) = 0x%X", dll, GetLastError());
		return FALSE;
	}
	
	GL32.gdilib = LoadLibraryA("gdi32.dll");
	
	/* using GetProcAddress */
#define GL_API_LOAD(_n, _t, _l) if(_l == LIB_DRV){ \
		GL32._n = (_n ## _f)GetProcAddress(GL32.gllib, #_n); \
		if(GL32._n == NULL){GUIError("Symbol '%s' not found in %s\n", #_n, dll); return FALSE;} \
	}else if(_l == LIB_GDI32){ \
		if(GL32.gllib != NULL){ \
			GL32._n = (_n ## _f)GetProcAddress(GL32.gdilib, #_n);} \
	}
	
	#include "wgltest_api.h"
#undef GL_API_LOAD


#define GL_API_LOAD(_n, _t, _l) if(_l == LIB_GL32){ \
		GL32._n = (_n ## _f)GL32.DrvGetProcAddress(#_n); \
		if(GL32._n == NULL && _t == FUNC_GL){GUIError("DrvGetProcAddress fail for symbol '%s' in %s\n", #_n, dll); return FALSE;} \
	}
	
	#include "wgltest_api.h"
#undef GL_API_LOAD
	
	GL32.type = GL_TYPE_DRV;

	return TRUE;	
}

static BOOL WINAPI wrpSetPixelFormat(HDC hdc, int format, const PIXELFORMATDESCRIPTOR *ppfd)
{
	switch(GL32.type)
	{
		case GL_TYPE_SYSTEM:
			return GL32.SetPixelFormat(hdc, format, ppfd);
		case GL_TYPE_WRAPPER:
			return GL32.wglSetPixelFormat(hdc, format, ppfd);
		case GL_TYPE_DRV:
			return GL32.DrvSetPixelFormat(hdc, format);
	}
	
	return FALSE;
}

static BOOL WINAPI wrpSwapBuffers(HDC hdc)
{
	switch(GL32.type)
	{
		case GL_TYPE_SYSTEM:
			return GL32.SwapBuffers(hdc);
		case GL_TYPE_WRAPPER:
			return GL32.wglSwapBuffers(hdc);
		case GL_TYPE_DRV:
			return GL32.DrvSwapBuffers(hdc);
	}
	
	return FALSE;
}

static int WINAPI wrpChoosePixelFormat(HDC hdc, const PIXELFORMATDESCRIPTOR *ppfd)
{
	switch(GL32.type)
	{
		case GL_TYPE_SYSTEM:
			return GL32.ChoosePixelFormat(hdc, ppfd);
		case GL_TYPE_WRAPPER:
			return GL32.wglChoosePixelFormat(hdc, ppfd);
		case GL_TYPE_DRV:
			if(GL32.wglChoosePixelFormat)
			{
				return GL32.wglChoosePixelFormat(hdc, ppfd);
			}
			else if(GL32.ChoosePixelFormat)
			{
				return GL32.ChoosePixelFormat(hdc, ppfd);
			}
			break;
	}
	
	return 0;
}

static int WINAPI wrpDescribePixelFormat(HDC hdc, int iPixelFormat, UINT nBytes, LPPIXELFORMATDESCRIPTOR ppfd)
{
	switch(GL32.type)
	{
		case GL_TYPE_SYSTEM:
			return GL32.DescribePixelFormat(hdc, iPixelFormat, nBytes, ppfd);
		case GL_TYPE_WRAPPER:
			return GL32.wglDescribePixelFormat(hdc, iPixelFormat, nBytes, ppfd);
		case GL_TYPE_DRV:
			return GL32.DrvDescribePixelFormat(hdc, iPixelFormat, nBytes, ppfd);
	}
	
	return 0;
}

static BOOL APIENTRY wrpMakeCurrent(HDC hdc, HGLRC hglrc)
{
	if(GL32.wglMakeCurrent != NULL)
	{
		return GL32.wglMakeCurrent(hdc, hglrc);
	}
	
	return GL32.DrvSetContext(hdc, hglrc, NULL) ? TRUE : FALSE;
}

static HGLRC APIENTRY wrpCreateContext(HDC hdc)
{
	if(GL32.wglCreateContext != NULL)
	{
		return GL32.wglCreateContext(hdc);
	}
	
	return GL32.DrvCreateLayerContext(hdc, 0);
}

static BOOL APIENTRY wrpDeleteContext(HGLRC hglrc)
{
	if(GL32.wglDeleteContext != NULL)
	{
		return GL32.wglDeleteContext(hglrc);
	}
	
	return GL32.DrvReleaseContext(hglrc);
}

PROC APIENTRY wrpGetProcAddress(LPCSTR lpszProc)
{
	if(GL32.wglGetProcAddress)
	{
		return GL32.wglGetProcAddress(lpszProc);
	}
	
	return GL32.DrvGetProcAddress(lpszProc);
}

BOOL running = TRUE;
BOOL gui_running = TRUE;
DWORD timer = 0;

#define IDT_TIMER1 1

#define ONEOVER255 (1.0f/255.0f)
#define ONEOVER128 (1.0f/128.0f)

void draw(float rot, signed char bg)
{
	GL32.glClearColor(fabs(bg*ONEOVER128), fabs(bg*ONEOVER128), fabs(bg*ONEOVER128), 1.0f);
	GL32.glClear(GL_COLOR_BUFFER_BIT);
	
	GL32.glPushMatrix();
	
	GL32.glRotatef(rot, 0.0f, 0.0f, 1.0f);
	
	GL32.glBegin(GL_POLYGON);
		GL32.glColor3f(1, 0, 0);
		GL32.glVertex3f(-0.75, -0.75,  0);
		
		GL32.glColor3f(0, 1, 0);
		GL32.glVertex3f(0.75,  -0.75,  0);
		
		GL32.glColor3f(0, 0, 1);
		GL32.glVertex3f(0,      0.75,  0);
	GL32.glEnd();
	
	
	GL32.glPopMatrix();
	
	GL32.glFlush();
}

LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
	switch(message)
	{
		case WM_CREATE:
		  //PostQuitMessage(0);
		break;
	case WM_DESTROY:
		PostQuitMessage(0);
		running = FALSE;
		break;
	case WM_TIMER:
		switch(wParam)
		{
			case IDT_TIMER1:
				timer++;
				return 0;
		}	
		break;
	case WM_KEYDOWN:
		if((GetAsyncKeyState(VK_ESCAPE) & 0x0001) ||
			(GetAsyncKeyState(VK_RETURN) & 0x0001) ||
			(GetAsyncKeyState(VK_SPACE) & 0x0001))
		{
			PostQuitMessage(0);
			running = FALSE;
		}
		break;
	case WM_SETCURSOR:
		if(LOWORD(lParam) == HTCLIENT)
		{
			SetCursor(NULL);
			return TRUE;
		}
	default:
		return DefWindowProc(hWnd, message, wParam, lParam);
	}
	return 0;
}

#define DPIX(_spx) ((int)(ceil((_spx)*rdpiX)))
#define DPIY(_spx) ((int)(ceil((_spx)*rdpiY)))

float rdpiX = 1;
float rdpiY = 1;

#define START_X 10
#define START_Y 10
#define LINE_HEIGHT 20
#define LINE_HL     10
#define DEF_WIDTH   100
#define HSPACE 10

char *gl_vendor = "(null)";
char *gl_renderer = "(null)";
char *gl_version = "(null)";
char *gl_glsl    = "(null)";
char *gl_ext     = "";
int gl_ext_cnt = 0;

LRESULT CALLBACK ReportWndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
	switch(message)
	{
		case WM_CREATE:
		{
			RECT win_size;
			GetWindowRect(hwnd, &win_size);
			//AdjustWindowRectEx(&win_size, GetWindowLongA(hwnd, GWL_STYLE), FALSE, GetWindowLongA(hwnd, GWL_EXSTYLE));
			
			int win_w = win_size.right - win_size.left;
			int win_h = win_size.bottom - win_size.top;
			
			int draw_x = START_X;
			int draw_y = START_Y;
			
			CreateWindowA("STATIC", "GL Vendor:",	WS_VISIBLE | WS_CHILD | SS_RIGHT,
				DPIX(draw_x), DPIY(draw_y), DPIX(DEF_WIDTH), DPIY(LINE_HEIGHT),
				hwnd, (HMENU)0, ((LPCREATESTRUCT)lParam)->hInstance, NULL);
			draw_x += DEF_WIDTH + HSPACE;
			CreateWindowA("EDIT",  gl_vendor,	WS_VISIBLE | WS_CHILD | ES_LEFT | WS_BORDER | WS_GROUP | ES_READONLY,
				DPIX(draw_x), DPIY(draw_y), win_w - DPIX(draw_x+START_X + HSPACE), DPIY(LINE_HEIGHT),
				hwnd, (HMENU)0, ((LPCREATESTRUCT)lParam)->hInstance, NULL);
			draw_x = START_X;
			draw_y += LINE_HEIGHT + LINE_HL;
			
			CreateWindowA("STATIC", "GL Renderer:",	WS_VISIBLE | WS_CHILD | SS_RIGHT,
				DPIX(draw_x), DPIY(draw_y), DPIX(DEF_WIDTH), DPIY(LINE_HEIGHT), hwnd, (HMENU)0,
				((LPCREATESTRUCT)lParam)->hInstance, NULL);
			draw_x += DEF_WIDTH + HSPACE;
			CreateWindowA("EDIT",  gl_renderer,	WS_VISIBLE | WS_CHILD | ES_LEFT | WS_BORDER | WS_GROUP | ES_READONLY,
				DPIX(draw_x), DPIY(draw_y), win_w - DPIX(draw_x+START_X + HSPACE), DPIY(LINE_HEIGHT),
				hwnd, (HMENU)0, ((LPCREATESTRUCT)lParam)->hInstance, NULL);
			draw_x = START_X;
			draw_y += LINE_HEIGHT + LINE_HL;
			
			CreateWindowA("STATIC", "GL Version:",	WS_VISIBLE | WS_CHILD | SS_RIGHT,
				DPIX(draw_x), DPIY(draw_y), DPIX(DEF_WIDTH), DPIY(LINE_HEIGHT), hwnd, (HMENU)0,
				((LPCREATESTRUCT)lParam)->hInstance, NULL);
			draw_x += DEF_WIDTH + HSPACE;
			CreateWindowA("EDIT",  gl_version,	WS_VISIBLE | WS_CHILD | ES_LEFT | WS_BORDER | WS_GROUP | ES_READONLY,
				DPIX(draw_x), DPIY(draw_y), win_w - DPIX(draw_x+START_X + HSPACE), DPIY(LINE_HEIGHT),
				hwnd, (HMENU)0, ((LPCREATESTRUCT)lParam)->hInstance, NULL);
			draw_x = START_X;
			draw_y += LINE_HEIGHT + LINE_HL;
			
			CreateWindowA("STATIC", "GLSL version:",	WS_VISIBLE | WS_CHILD | SS_RIGHT,
				DPIX(draw_x), DPIY(draw_y), DPIX(DEF_WIDTH), DPIY(LINE_HEIGHT), hwnd, (HMENU)0,
				((LPCREATESTRUCT)lParam)->hInstance, NULL);
			draw_x += DEF_WIDTH + HSPACE;
			CreateWindowA("EDIT",  gl_glsl,	WS_VISIBLE | WS_CHILD | ES_LEFT | WS_BORDER | WS_GROUP | ES_READONLY,
				DPIX(draw_x), DPIY(draw_y), win_w - DPIX(draw_x+START_X + HSPACE), DPIY(LINE_HEIGHT),
				hwnd, (HMENU)0, ((LPCREATESTRUCT)lParam)->hInstance, NULL);
			draw_x = START_X;
			draw_y += LINE_HEIGHT + LINE_HL;
			
			char num_buf[32];
			sprintf(num_buf, "%d", gl_ext_cnt);
			
			CreateWindowA("STATIC", "GL extensions:",	WS_VISIBLE | WS_CHILD | SS_RIGHT,
				DPIX(draw_x), DPIY(draw_y), DPIX(DEF_WIDTH), DPIY(LINE_HEIGHT), hwnd, (HMENU)0,
				((LPCREATESTRUCT)lParam)->hInstance, NULL);
			draw_x += DEF_WIDTH + HSPACE;
			CreateWindowA("STATIC",  num_buf,	WS_VISIBLE | WS_CHILD | SS_LEFT | ES_READONLY,
				DPIX(draw_x), DPIY(draw_y), (DEF_WIDTH), DPIY(LINE_HEIGHT),
				hwnd, (HMENU)0, ((LPCREATESTRUCT)lParam)->hInstance, NULL);
			draw_x = START_X;
			draw_y += LINE_HEIGHT + LINE_HL;
			
			CreateWindowA("EDIT",  gl_ext,	WS_VISIBLE | WS_CHILD | WS_BORDER | WS_VSCROLL | ES_MULTILINE | ES_AUTOVSCROLL | ES_AUTOHSCROLL | ES_READONLY,
				DPIX(draw_x), DPIY(draw_y), win_w - DPIX(draw_x+START_X + HSPACE), win_h - DPIY(draw_y + START_Y + LINE_HEIGHT*2),
				hwnd, (HMENU)0, ((LPCREATESTRUCT)lParam)->hInstance, NULL);
			
			break;
		}
	case WM_DESTROY:
		PostQuitMessage(0);
		gui_running = FALSE;
		break;
	default:
		return DefWindowProc(hwnd, message, wParam, lParam);
	}
	return 0;
}

#define WND_CLASS_NAME "oglversionchecksample"
#define WND_CLASS_NAME2 "ogltes2"
#define WND_CLASS_NAME3 "oglreport"

/*
 * We try to create and destroy context for test,
 * if driver is able of context recreation.
 */
BOOL gl_test()
{
	WNDCLASS wc      = {0};
	HANDLE           win;
	
	PIXELFORMATDESCRIPTOR pfd =
	{
		sizeof(PIXELFORMATDESCRIPTOR),
		1,
		PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER,    //Flags
		PFD_TYPE_RGBA,        // The kind of framebuffer. RGBA or palette.
		32,                   // Colordepth of the framebuffer.
		0, 0, 0, 0, 0, 0,
		0,
		0,
		0,
		0, 0, 0, 0,
		24,                   // Number of bits for the depthbuffer
		8,                    // Number of bits for the stencilbuffer
		0,                    // Number of Aux buffers in the framebuffer.
		PFD_MAIN_PLANE,
		0,
		0, 0, 0
	};
	
	HDC dc;
	HGLRC ctx;
	int pixel_format;
	
	wc.lpfnWndProc   = DefWindowProc;
	wc.hInstance     = NULL;
	wc.hbrBackground = (HBRUSH)(COLOR_BACKGROUND);
	wc.lpszClassName = WND_CLASS_NAME2;
	wc.style         = CS_OWNDC;
	wc.hInstance     = GetModuleHandle(NULL);

	
	if( !RegisterClass(&wc) )
	{
		GUIError("FAILURE: RegisterClass: %d", GetLastError());
		return FALSE;
	}
	
	win = CreateWindowA(WND_CLASS_NAME2, "WGL Check 1",
		WS_OVERLAPPEDWINDOW|WS_VISIBLE, 0,0,20,20,0,0, NULL, 0);
		
	if(!win)
	{
		GUIError("FAILURE: CreateWindowA: %d", GetLastError());
		return FALSE;
	}
	
	dc = GetDC(win);
	
	pixel_format = wrpDescribePixelFormat(dc, 0, 0, NULL);

	pixel_format = wrpChoosePixelFormat(dc, &pfd); 
	wrpSetPixelFormat(dc, pixel_format, &pfd);

	ctx = wrpCreateContext(dc);
	if(ctx == NULL)
	{
		GUIError("FAILURE: CreateContext: %d", GetLastError());
		return FALSE;
	}
	wrpMakeCurrent(dc, ctx);
	
	wrpDeleteContext(ctx);
	
	DestroyWindow(win);
	
	return TRUE;
}

void gui_report()
{
	MSG msg          = {0};
	WNDCLASS wc      = {0};
	HANDLE           winreport;
	
	wc.style         = CS_HREDRAW | CS_VREDRAW;
	wc.lpfnWndProc   = ReportWndProc;
	wc.lpszClassName = WND_CLASS_NAME3;
	wc.hbrBackground = GetSysColorBrush(COLOR_3DFACE);
	wc.hInstance     = GetModuleHandle(NULL);
	wc.hIcon         = LoadIconA(wc.hInstance, MAKEINTRESOURCE(101));
	
	if(!RegisterClass(&wc))
	{
		return;
	}
	
	winreport = CreateWindowA(WND_CLASS_NAME3, "WGLtest report",
		(WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX)|WS_VISIBLE, 0,0,512,400,0,0, NULL, 0);
		
	if(!winreport)
	{
		return;
	}
	
	do
	{
		while(PeekMessage(&msg, NULL, 0, 0, PM_REMOVE))
		{
			TranslateMessage(&msg);
			DispatchMessage(&msg);
		}
	}
	while(gui_running);
	
	DestroyWindow(winreport);
}

#define XSTR(_s) #_s
#define STR(_s) XSTR(_s)

#if defined(DEFAULT_WRAPPER)
static int   setup_gl_type = GL_TYPE_WRAPPER;
static char *setup_gl_dll = STR(DEFAULT_WRAPPER);
#elif defined(DEFAULT_DRIVER)
static int   setup_gl_type = GL_TYPE_DRV;
static char *setup_gl_dll = STR(DEFAULT_DRIVER);
#else
static int   setup_gl_type = GL_TYPE_SYSTEM;
static char *setup_gl_dll = NULL;
#endif

static BOOL  setup_fullscreen = FALSE;
static int   setup_width  = 640;
static int   setup_height = 480;
static BOOL  setup_show_help = FALSE;

BOOL parse_cmd(int argc, char **argv)
{
	int i;
	for(i = 1; i < argc; i++)
	{
		if(strcmp(argv[i], "wrapper") == 0)
		{
			if(i+1 == argc)
			{
				GUIError("Command line: wrapper command require DLL name");
				return FALSE;
			}
			setup_gl_type = GL_TYPE_WRAPPER;
			setup_gl_dll  = argv[i+1];
			i++;
		}
		else if(strcmp(argv[i], "driver") == 0)
		{
			if(i+1 == argc)
			{
				GUIError("Command line: driver command require DLL name");
				return FALSE;
			}
			setup_gl_type = GL_TYPE_DRV;
			setup_gl_dll  = argv[i+1];
			i++;
		}
		else if(strcmp(argv[i], "system") == 0)
		{
			setup_gl_type = GL_TYPE_SYSTEM;
		}
		else if(strcmp(argv[i], "help") == 0)
		{
			setup_show_help = TRUE;
			return TRUE;
		}
		else if(strcmp(argv[i], "width") == 0)
		{
			if(i+1 == argc)
			{
				GUIError("Command line: width command require number");
				return FALSE;
			}
			setup_width = atoi(argv[i+1]);			
			i++;
			if(setup_width == 0)
			{
				GUIError("Command line: width cannot be 0");
				return FALSE;
			}
		}
		else if(strcmp(argv[i], "height") == 0)
		{
			if(i+1 == argc)
			{
				GUIError("Command line: height command require number");
				return FALSE;
			}
			setup_height = atoi(argv[i+1]);			
			i++;
			if(setup_height == 0)
			{
				GUIError("Command line: height cannot be 0");
				return FALSE;
			}
		}
		else if(strcmp(argv[i], "fullscreen") == 0)
		{
			setup_fullscreen = TRUE;
		}
		else if(strcmp(argv[i], "window") == 0)
		{
			setup_fullscreen = FALSE;
		}
		else
		{
			GUIError("Command line: unknown argument '%s'\n\nrun '%s help' to list supported command line arguments", argv[i], argv[0]);
			return FALSE;
		}
	} // for
	
	return TRUE;
}

BOOL fullscreen(HWND hwnd, int width, int height)
{
	DEVMODE fullscreenSettings;
	BOOL success;

	EnumDisplaySettings(NULL, 0, &fullscreenSettings);
	fullscreenSettings.dmPelsWidth        = width;
	fullscreenSettings.dmPelsHeight       = height;
	fullscreenSettings.dmFields           = DM_PELSWIDTH | DM_PELSHEIGHT;
	
	LONG_PTR old_style = GetWindowLongPtr(hwnd, GWL_STYLE); 
	LONG_PTR old_exstyle = GetWindowLongPtr(hwnd, GWL_EXSTYLE); 

	SetWindowLongPtr(hwnd, GWL_EXSTYLE, WS_EX_APPWINDOW | WS_EX_TOPMOST);
	SetWindowLongPtr(hwnd, GWL_STYLE, WS_POPUP | WS_VISIBLE);
	SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, width, height, SWP_SHOWWINDOW);
	success = ChangeDisplaySettings(&fullscreenSettings, CDS_FULLSCREEN) == DISP_CHANGE_SUCCESSFUL;
	if(success)	
	{
		ShowWindow(hwnd, SW_MAXIMIZE);
	}
	else
	{
		SetWindowLongPtrA(hwnd, GWL_EXSTYLE, old_exstyle);
		SetWindowLongPtrA(hwnd, GWL_STYLE, old_style);
	}

	return success;
}

BOOL restore()
{
	return ChangeDisplaySettings(NULL, CDS_RESET) == DISP_CHANGE_SUCCESSFUL;
}

int main(int argc, char **argv)
{
	MSG msg          = {0};
	WNDCLASS wc      = {0};
	HANDLE           win;
	
	if(parse_cmd(argc, argv) == FALSE)
	{
		return -1;
	}
	
	if(setup_show_help)
	{
		MessageBoxA(NULL, usage, "WGLtest: HELP", MB_OK | MB_ICONASTERISK);
		return 0;
	}
	
	switch(setup_gl_type)
	{
		case GL_TYPE_SYSTEM:
			if(!gl_load_system()) return -1;
			break;
		case GL_TYPE_WRAPPER:
			if(!gl_load_wrapper(setup_gl_dll)) return -1;
			break;
		case GL_TYPE_DRV:
			if(!gl_load_driver(setup_gl_dll)) return -1;
			break;
	}

	PIXELFORMATDESCRIPTOR pfd =
	{
		sizeof(PIXELFORMATDESCRIPTOR),
		1,
		PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER,    //Flags
		PFD_TYPE_RGBA,        // The kind of framebuffer. RGBA or palette.
		32,                   // Colordepth of the framebuffer.
		0, 0, 0, 0, 0, 0,
		0,
		0,
		0,
		0, 0, 0, 0,
		24,                   // Number of bits for the depthbuffer
		8,                    // Number of bits for the stencilbuffer
		0,                    // Number of Aux buffers in the framebuffer.
		PFD_MAIN_PLANE,
		0,
		0, 0, 0
	};
	
	HDC dc;
	HGLRC ctx;
	int pixel_format;

	if(!gl_test())
	{
		return -1;
	}

	wc.lpfnWndProc       = WndProc;
	wc.hbrBackground     = (HBRUSH)(COLOR_BACKGROUND);
	wc.lpszClassName     = WND_CLASS_NAME;
	wc.style             = CS_OWNDC;
	wc.hInstance         = GetModuleHandle(NULL);
	wc.hIcon             = LoadIconA(wc.hInstance, MAKEINTRESOURCE(101));
	
	if( !RegisterClass(&wc) )
		return -1;
	
	win = CreateWindowA(WND_CLASS_NAME, "WGLtest triangle",
		WS_OVERLAPPEDWINDOW|WS_VISIBLE, 0, 0, setup_width, setup_height, 0, 0, NULL, 0);
		
	if(!win)
	{
		return -1;
	}
	
	if(setup_fullscreen)
	{
		if(!fullscreen(win, setup_width, setup_height))
		{
			GUIError("Cannot enter to this fullscreen mode (%d x %d)", setup_width, setup_height);
			setup_fullscreen = FALSE;
		}
	}
	
	dc = GetDC(win);

	pixel_format = wrpChoosePixelFormat(dc, &pfd);
	//printf("pixel format = %d\n", pixel_format);
	wrpSetPixelFormat(dc, pixel_format, &pfd);

	ctx = wrpCreateContext(dc);
	if(ctx == NULL)
	{
		return -1;
	}
	wrpMakeCurrent(dc, ctx);

	const char *tmp;
	
	tmp = (const char*)GL32.glGetString(GL_VENDOR);
	if(tmp)
	{
		gl_vendor = malloc(strlen(tmp) + 1);
		if(gl_vendor)	strcpy(gl_vendor, tmp);
	}
	
	tmp = (const char*)GL32.glGetString(GL_RENDERER);
	if(tmp)
	{
		gl_renderer = malloc(strlen(tmp) + 1);
		if(gl_renderer) strcpy(gl_renderer, tmp);
	}
	
	tmp = (const char*)GL32.glGetString(GL_VERSION);
	if(tmp)
	{
		gl_version = malloc(strlen(tmp) + 1);
		if(gl_version)	strcpy(gl_version, tmp);
	}
	
	tmp = (const char*)GL32.glGetString(GL_SHADING_LANGUAGE_VERSION);
	if(tmp)
	{
		gl_glsl = malloc(strlen(tmp) + 1);
		if(gl_glsl)	strcpy(gl_glsl, tmp);
	}
	
	tmp = (const char*)GL32.glGetString(GL_EXTENSIONS);
	if(tmp)
	{
		char *ptr = (char*)tmp;
		size_t len = 0;
		gl_ext_cnt = 0;
		while(*ptr != '\0')
		{
			if(*ptr == ' ')
			{
				gl_ext_cnt++;
			}
			ptr++;
		}
		len = ptr - tmp;
		if(len != 0)
		{
			gl_ext_cnt++;
		}
		
		gl_ext = malloc(len + gl_ext_cnt + 1);
		if(gl_ext)
		{
			char *dst = gl_ext;
			ptr = (char*)tmp;
			while(*ptr != '\0')
			{
				if(*ptr == ' ')
				{
					*dst++ = '\r';
					*dst++ = '\n';
				}
				else
				{
					*dst++ = *ptr;
				}
				
				ptr++;
			}
			*dst = '\0';
		}
	}
	
	SetTimer(win, IDT_TIMER1, 10, NULL); 
	
	do
	{
		while(PeekMessage(&msg, NULL, 0, 0, PM_REMOVE))
		{
			TranslateMessage(&msg);
			DispatchMessage(&msg);
		}
		draw(0.2f * timer, (timer >> 2) & 0xFF);
		wrpSwapBuffers(dc);
	}
	while(running);
	
	wrpMakeCurrent(NULL, NULL);
	wrpDeleteContext(ctx);
	DestroyWindow(win);
	
	if(setup_fullscreen)
	{
		restore();
	}

	gui_report();
	
	// todo: freelibrary
	// todo: free(gl_vendor) ...

	return 0;
}
