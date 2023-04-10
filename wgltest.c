#include <windows.h>
#include <stdio.h>
#include <GL/GL.h>

LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
	switch(message)
	{
		case WM_CREATE:
		  //PostQuitMessage(0);
		break;
	case WM_DESTROY:
		PostQuitMessage(0);
		break;
	default:
		return DefWindowProc(hWnd, message, wParam, lParam);
	}
	return 0;
}

#define WND_CLASS_NAME "oglversionchecksample"

int main(int argc, char *argv)
{
	MSG msg          = {0};
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
	SYSTEM_INFO si;
	GLint cliplanes = 0;
	
	printf("wgltest\n");
	
	GetSystemInfo(&si);
	printf("Page size: %u\n", si.dwPageSize);
	
	wc.lpfnWndProc   = WndProc;
	wc.hInstance     = NULL;
	wc.hbrBackground = (HBRUSH)(COLOR_BACKGROUND);
	wc.lpszClassName = WND_CLASS_NAME;
	wc.style = CS_OWNDC;
	
	printf("RegisterClass!\n");
	if( !RegisterClass(&wc) )
		return 1;
	
	printf("Create windows!\n");
	
	win = CreateWindowA(WND_CLASS_NAME, "WGL Check",
		WS_OVERLAPPEDWINDOW|WS_VISIBLE, 0,0,640,480,0,0, NULL, 0);
		
	if(!win)
	{
		printf("error: %d\n", GetLastError());
		return 0;
	}
		
	printf("Catch create!\n");

	dc = GetDC(win);

	pixel_format = ChoosePixelFormat(dc, &pfd); 
	SetPixelFormat(dc, pixel_format, &pfd);

	ctx = wglCreateContext(dc);
	wglMakeCurrent (dc, ctx);

	printf("GL_VERSION: %s\n", glGetString(GL_VERSION));
	printf("GL_RENDERER: %s\n", glGetString(GL_RENDERER));
	
	glGetIntegerv(GL_MAX_CLIP_PLANES, &cliplanes);
	
	printf("GL_MAX_CLIP_PLANES: %d\n", cliplanes);

  printf("Loooop!\n");

	while( GetMessage( &msg, NULL, 0, 0 ) > 0 )
		DispatchMessage( &msg );

	wglDeleteContext(ctx);

	return 0;
}
