#include <windows.h>
#include <wingdi.h>
#include <stdio.h>
#include <stdlib.h>

/**
 QUERYESCSUPPORT = 8

 FEATURESETTING_PRIVATE_BEGIN 0x1000
 FEATURESETTING_PRIVATE_END 0x1FFF

 **/

static int ExtEscapeTest(HDC device_ctx)
{
	int test = 0;
	DWORD inData = 0x1101;
	
	test = ExtEscape(device_ctx, 8, sizeof(DWORD), (LPCSTR)&inData, 0, NULL);
	
	printf("ExtEscape test #1: %d\n", test);
	
	return test;
}

#define TEST2_BUFSIZE 0x214
static int ExtEscapeTest2(HDC device_ctx)
{
	int test = 0;
	DWORD inData = 0;
	unsigned char buf[TEST2_BUFSIZE];
	int i = 0;
	
	memset(buf, 0, TEST2_BUFSIZE);
	
	test = ExtEscape(device_ctx, 0x1101, sizeof(DWORD), (LPCSTR)&inData, TEST2_BUFSIZE, buf);
	
	printf("ExtEscape test #2: %d\n", test);
	
	if(test > 0)
	{
		for(i = 0; i < TEST2_BUFSIZE; i++)
		{
			if(buf[i] > 0)
			{
				printf("0x%03x: 0x%02d", i, buf[i]);
				if(buf[i] >= 0x20 && buf[i] < 0x80)
				{
					printf(" (%c)", buf[i]);
				}
				printf("\n");
			}
		}
	}
	
	return test;
}

LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
	switch(message)
	{
		case WM_CREATE:
		{
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
			
			HDC winDeviceContext = GetDC(hWnd);
			
			if(ExtEscapeTest(winDeviceContext) != 0)
			{
				ExtEscapeTest2(winDeviceContext);
			}
			
			PostQuitMessage(0);
		}
		break;
		case WM_DESTROY:
			break;
		default:
			return DefWindowProc(hWnd, message, wParam, lParam);
	}
	return 0;
}

#define WND_CLASS_NAME "icdtestcls"

int main(int argc, char *argv)
{
	MSG msg          = {0};
	WNDCLASS wc      = {0};
	HANDLE win;
	int c;
	
	wc.lpfnWndProc   = WndProc;
	wc.hInstance     = NULL;
	wc.hbrBackground = (HBRUSH)(COLOR_BACKGROUND);
	wc.lpszClassName = WND_CLASS_NAME;
	wc.style = CS_OWNDC;

	if(RegisterClassA(&wc))
	{
		win = CreateWindowA(WND_CLASS_NAME, "ICD Test", 
		      WS_OVERLAPPEDWINDOW|WS_VISIBLE, 0,0,640,480,0,0, NULL, 0);
			
		if(win)
		{
			while(GetMessage(&msg, NULL, 0, 0) > 0)
			{
				DispatchMessage(&msg);
			}
		}
		else
		{
			printf("CreateWindowA error: %d\n", GetLastError());
		}
	}
	else
	{
		printf("RegisterClassA error: %d\n", GetLastError());
	}
		
	printf("Press enter to exit!\n");
	
	do
	{
		c = getchar();
	} while(c != '\n' && c != EOF);

	return 0;
}
