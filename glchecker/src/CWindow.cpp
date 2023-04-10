/*
Copyright (c) 2016, David Liu
All rights reserved. Read the license for more information.
*/

#include "CEngine.h"

/* Functions */
#if defined(COS_WIN32)
LRESULT CALLBACK CEngine::CWndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
	CEngine* receiver=(CEngine*)GetWindowLongPtr(hWnd, GWLP_USERDATA);
	if (receiver)
		return receiver->CProcWindow(message, wParam, lParam);
	return DefWindowProc(hWnd, message, wParam, lParam);
}

inline LRESULT CEngine::CProcWindow(UINT message, WPARAM wParam, LPARAM lParam)
{
	if (!prevclick && key[CKEY_LMOUSE])
		prevclick = true;
	else if (prevclick && !key[CKEY_LMOUSE])
		prevclick = false;
	switch (message)
	{
	case WM_ACTIVATE:
		{
			if (wParam != WA_INACTIVE && !focus)
			{
				if (fullscreen)
				{
					CMinimize(false);
					ChangeDisplaySettings(&mode[bestres], CDS_FULLSCREEN);
				}
				focus = true;
			}
			else if (wParam == WA_INACTIVE && focus)
			{
				if (fullscreen)
				{
					CMinimize(true);
					ChangeDisplaySettings(NULL, 0);
				}
				focus = false;
			}
		}break;
	case WM_LBUTTONDOWN:
		key[CKEY_LMOUSE] = true;
		break;
	case WM_LBUTTONUP:
		key[CKEY_LMOUSE] = false;
		break;
	case WM_RBUTTONDOWN:
		key[CKEY_RMOUSE] = true;
		break;
	case WM_RBUTTONUP:
		key[CKEY_RMOUSE] = false;
		break;
	case WM_MOUSEMOVE:
		{
			mx = LOWORD(lParam);
			my = height - HIWORD(lParam);
			cmx = (float)mx / (float)width * rectx + xorg;
			cmy = (float)my / (float)height * recty + yorg;
		}break;
	case WM_KEYDOWN:
		key[wParam] = true;
		break;
	case WM_KEYUP:
		key[wParam] = false;
		break;
	case WM_CLOSE:
		exit(0);
		break;
	}
	return DefWindowProc(hWnd, message, wParam, lParam);
}

void CEngine::CMoveWindow(int posx, int posy, int pwidth, int pheight)
{
	if (pwidth >= 0)
		width = pwidth;
    else
        width = maxwidth;
	if (pheight >= 0)
		height = pheight;
    else
        height = maxheight;
	xpos = posx;
	ypos = posy;
	MoveWindow(hWnd, xpos, ypos, width, height, false);
};

void CEngine::CMinimize(bool mz)
{
	if (mz)
		ShowWindow(hWnd, SW_MINIMIZE);
	else
		ShowWindow(hWnd, SW_RESTORE);
}

void CEngine::CSetCursor(unsigned short x, unsigned short y)
{
	SetCursorPos(x, height - y);
}

void CEngine::CToggleCursor(bool visible)
{
	if (visible)
	{
		while (ShowCursor(true) < 0)
			ShowCursor(true);
	}
	else
	{
		while (ShowCursor(false) >= 0)
			ShowCursor(false);
	}
}

void CEngine::CMsgBox(const char *text, const char *title)
{
	MessageBox(NULL, text, title, 0);
}

void CEngine::CProcessMessage()
{
	while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE) > 0)
	{
		TranslateMessage(&msg);
		DispatchMessage(&msg);
	}
}

CByte8u CEngine::CGetTime()
{
	LARGE_INTEGER time;
	QueryPerformanceCounter( &time );
	return time.QuadPart / freq;
}

void CEngine::CSleep(unsigned int ms)
{
	Sleep(ms);
}

void CEngine::CSwapBuffers()
{
	SwapBuffers(hDC);
}

#elif defined(COS_LINUX)
unsigned short CKEY_A, CKEY_B, CKEY_C, CKEY_D, CKEY_E, CKEY_F, CKEY_G, CKEY_H, CKEY_I, CKEY_J, CKEY_K, CKEY_L, CKEY_M, CKEY_N
, CKEY_O, CKEY_P, CKEY_Q, CKEY_R, CKEY_S, CKEY_T, CKEY_U, CKEY_V, CKEY_W, CKEY_X, CKEY_Y, CKEY_Z, CKEY_0
, CKEY_1, CKEY_2, CKEY_3, CKEY_4, CKEY_5, CKEY_6, CKEY_7, CKEY_8, CKEY_9, CKEY_SPACE, CKEY_BACKSPACE, CKEY_TAB, CKEY_ENTER, CKEY_RIGHT, CKEY_LEFT
, CKEY_DOWN, CKEY_UP, CKEY_SEMICOLON, CKEY_DIVIDE, CKEY_TILDE, CKEY_RBRACKET, CKEY_LBRACKET
, CKEY_SLASH, CKEY_BACKSLASH, CKEY_QUOTE, CKEY_MINUS, CKEY_PLUS, CKEY_PERIOD, CKEY_COMMA, CKEY_RSHIFT, CKEY_LSHIFT, CKEY_RCTRL, CKEY_LCTRL, CKEY_ESC, CKEY_F1;

void CEngine::CProcessMessage()
{
	if (!prevclick && key[CKEY_LMOUSE])
		prevclick = true;
	else if (prevclick && !key[CKEY_LMOUSE])
		prevclick = false;
    while (XPending(hDis) > 0)
    {
        XNextEvent(hDis, &hEvent);
        switch (hEvent.type)
        {
        case KeyPress:
            key[hEvent.xkey.keycode] = true;
            break;
        case KeyRelease:
            key[hEvent.xkey.keycode] = false;
            break;
        case ButtonPress:
            {
                switch(hEvent.xbutton.button)
                {
                case 1:
                    key[CKEY_LMOUSE] = true;
                    break;
                case 3:
                    key[CKEY_RMOUSE] = true;
                    break;
                }
            }break;
        case ButtonRelease:
            {
                switch(hEvent.xbutton.button)
                {
                case 1:
                    key[CKEY_LMOUSE] = false;
                    break;
                case 3:
                    key[CKEY_RMOUSE] = false;
                    break;
                }
            }break;
        case FocusIn:
            {
                if (hEvent.xfocus.window == fake && !focus)
                {
					if (fullscreen)
					{
						CMinimize(false);
						XF86VidModeSwitchToMode(hDis, hScr, mode[bestres]);
						XF86VidModeSetViewPort(hDis, hScr, 0, 0);
					}
					else
						XRaiseWindow(hDis, hWin);
                    XSetInputFocus(hDis, hWin, RevertToNone, CurrentTime);
                    focus = true;
                }
            }break;
        case FocusOut:
            {
                if (hEvent.xfocus.window == hWin && focus)
                {
					if (fullscreen)
					{
						CMinimize(true);
						XF86VidModeSwitchToMode(hDis, hScr, mode[0]);
						XF86VidModeSetViewPort(hDis, hScr, 0, 0);
					}
					else
                    	XLowerWindow(hDis, hWin);
                    focus = false;
                }
            }break;
        case MotionNotify:
            {
                mx = hEvent.xmotion.x;
                my = height - hEvent.xmotion.y;
				cmx = (float)mx / (float)width * rectx + xorg;
				cmy = (float)my / (float)height * recty + yorg;
            }break;
        }
    }
}

void CEngine::CMoveWindow(int posx, int posy, int pwidth, int pheight)
{
    if (pwidth >= 0)
        width = pwidth;
    else
        width = maxwidth;
    if (pheight >= 0)
        height = pheight;
    else
        height = maxheight;
    xpos = posx;
    ypos = posy;
    XMoveResizeWindow(hDis, hWin, xpos, ypos, width, height);
}

void CEngine::CMinimize(bool mz)
{
	if (mz)
		XUnmapWindow(hDis, hWin);
	else
		XMapWindow(hDis, hWin);
}

void CEngine::CToggleCursor(bool visible)
{
    if (visible)
        XDefineCursor(hDis, hWin, None);
    else
        XDefineCursor(hDis, hWin, hide);
}

void CEngine::CSetCursor(unsigned short x, unsigned short y)
{
    XWarpPointer(hDis, None, hWin, 0, 0, width, height, x, y);
}

void CEngine::CMsgBox(const char *text, const char *title)
{
    // Init, temp window
    Window msgbox;
    GC gc;
    XFontStruct* font;
    XEvent e;
    XCharStruct overall;
    int ascent, descent, dir;
	Display* dis = XOpenDisplay(NULL);
	int scr = DefaultScreen(dis);
    font = XLoadQueryFont(dis, "fixed");
    XTextExtents(font, text, strlen(text), &dir, &ascent, &descent, &overall);
    if (overall.width < 60)
        overall.width = 60;
    msgbox = XCreateSimpleWindow(dis, DefaultRootWindow(dis), DisplayWidth(dis, scr) / 2-overall.width / 2 - 10, DisplayHeight(dis, scr) / 2 - 20, overall.width + 20, 40, 0, BlackPixel(dis, 0), WhitePixel(dis, 0));
    Atom wmDelete = XInternAtom(dis, "WM_DELETE_WINDOW", true);
    XSetWMProtocols(dis, msgbox, &wmDelete, 1);
    XStoreName(dis, msgbox, title);
    XSelectInput(dis, msgbox, ExposureMask);
    gc = XCreateGC(dis, msgbox, 0, 0);
    XSetBackground(dis, gc, WhitePixel(dis, 0));
    XSetForeground(dis, gc, BlackPixel(dis, 0));
    XSetFont(dis, gc, font->fid);
    XMapRaised(dis, msgbox);
    // Main
	dir = 0;
	while (dir == 0)
   	{
		while (XPending(dis) > 0)
		{
			XNextEvent(dis, &e);
	      	  	if (e.xexpose.window == msgbox && e.type == Expose)
	    	    	{
	    	     	  	XClearWindow(dis, msgbox);
	   	         	XDrawString(dis, msgbox, gc, 10, 40 / 2 + (ascent - descent) / 2, text, strlen(text));
	    	    	}
	   	     	else if (e.xclient.window == msgbox && e.type == ClientMessage)
			{
				dir = 1;
				break;
			}
		
    		}
		if (dir != 1)
			usleep(50000);
	}
	// Cleanup
    	XFreeFont(dis, font);
    	XFreeGC(dis, gc);
    	//XFree(&wmDelete);
    	XDestroyWindow(dis, msgbox);
	XCloseDisplay(dis);
	return;
}

CByte8u CEngine::CGetTime()
{
    timeval time;
    gettimeofday(&time, NULL);
    return (unsigned long long)time.tv_sec * 1000 + time.tv_usec / 1000;
}

void CEngine::CSleep(unsigned int ms)
{
    usleep(ms * 1000);
}

void CEngine::CSwapBuffers()
{
    glXSwapBuffers(hDis, hWin);
}
#endif
