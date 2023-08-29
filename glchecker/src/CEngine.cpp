/*
Copyright (c) 2016, David Liu
All rights reserved. Read the license for more information.
*/

#include "CEngine.h"

/* Functions */
bool CEngine::CSetupEngine(CWindow* attribs) // Creates a fully functional CEngine
{
	// Basic setup
	#ifdef CDEBUG
	CMsgBox("Initialization of CEngine has started...", "CDEBUG");
	#endif
	int i;
	focus = true;
	//setlocale(LC_ALL, "C");
	srand(time(NULL));
	toggle = prevclick = fullscreen = false;
	inputtime = 0;
    #if defined(COS_WIN32)
	winsock = false;
	#endif
	memset(key, 0, sizeof(key));
	resolution = NULL;
	// Sound
	if (CBit(attribs->properties, CPROP_SOUND))
	{
		/*basslib = LoadLibrary("bass.dll");
		if (basslib == NULL)
		{
			strcpy(inerr ,"Failed to find/load bass.dll.");
			return false;
		}
		if (*/
		/*CByte2u hb = LOWORD(BASS_GetVersion());
		sndv[3] = hb;
		sndv[2] = (hb >> 8);
		hb = HIWORD(BASS_GetVersion());
		sndv[1] = hb;
		sndv[0] = (hb >> 8);
		if (hb != BASSVERSION)
		{
			strcpy(inerr ,"Incorrect version of BASS was loaded.");
			return false;
		}*/
	}
    #if defined(COS_WIN32)
	// Window data
	hInstance = GetModuleHandle(NULL);
	maxwidth = GetSystemMetrics(SM_CXSCREEN);
	maxheight = GetSystemMetrics(SM_CYSCREEN);
	if (attribs->pwidth >= 0)
		width = attribs->pwidth;
	else
		width = maxwidth;
	if (attribs->pheight >= 0)
		height = attribs->pheight;
	else
		height = maxheight;
    xpos = attribs->posx;
    ypos = attribs->posy;
	// Timer
	LARGE_INTEGER freqs;
	if (!QueryPerformanceFrequency(&freqs))
	{
		strcpy(inerr ,"Unable to set the timer.");
		return false;
	}
	if (freqs.QuadPart < 1000)
	{
		strcpy(inerr ,"Timer resolution is too small.");
		return false;
	}
	else
		freq = freqs.QuadPart / 1000;
	// Sockets
	if (CBit(attribs->properties, CPROP_SOCKETS))
	{
/*		WSADATA wsadata;
		if (WSAStartup(MAKEWORD(2, 2), &wsadata) != 0)
		{
			strcpy(inerr ,"Unable to set up Windows Sockets.");
			return false;
		}
		if (wsadata.wVersion != MAKEWORD(2, 2))
		{
			strcpy(inerr ,"Required version of Windows Sockets is not available.");
			return false;
		}
		winsock = true;
		*/
		winsock = false;
	}
	#elif defined(COS_LINUX)
	// Window data
	width = attribs->pwidth;
	height = attribs->pheight;
	xpos = attribs->posx;
    ypos = attribs->posy;
	#endif
	if (attribs->type == CWIN_CONSOLE) // No OpenGL or window for console
	{
		#ifdef CDEBUG
		CMsgBox("Creating the console window...", "CDEBUG");
		#endif
		#if defined(COS_WIN32)
		if (!AllocConsole())
		{
			strcpy(inerr ,"Unable to create the console.");
			return false;
		}
		freopen("CONOUT$", "w", stdout);
		SetConsoleTitle(attribs->wndtitle);
		SetConsoleTextAttribute(GetStdHandle(STD_OUTPUT_HANDLE), FOREGROUND_GREEN | FOREGROUND_BLUE | FOREGROUND_RED);
		#elif defined(COS_LINUX)
		printf("%c]0;%s%c", '\033', attribs->wndtitle, '\007');
		#endif
		if (CBit(attribs->properties, CPROP_SOUND))
		{
			#ifdef CDEBUG
			CMsgBox("Initialzing sound...", "CDEBUG");
			#endif
			/*if (!BASS_Init(-1, 44100, 0, NULL, NULL))
			{
				strcpy(inerr ,"Unable to initialize the sound output device.");
				return false;
			}*/
		}
		return true;
	}
	char vers[4];
	#if defined(COS_WIN32)
	// Window properties
	#ifdef CDEBUG
	CMsgBox("Setting window attributes...", "CDEBUG");
	#endif
	wndname = NULL; // Remember window name when attrib struct is gone
	WNDCLASSEX wcex;
	wcex.cbSize = sizeof(WNDCLASSEX);
	wcex.style = CS_OWNDC;
	wcex.lpfnWndProc = CWndProc;
	wcex.cbClsExtra = 0;
	wcex.cbWndExtra = 0;
	wcex.hInstance = hInstance;
	if (attribs->iconpath == NULL)
		wcex.hIcon = NULL;
	else if ((wcex.hIcon = (HICON)LoadImage(NULL, attribs->iconpath, IMAGE_ICON, 0, 0, LR_LOADFROMFILE)) == NULL)
	{
		strcpy(inerr ,"Unable to find/load the icon file.");
		return false;
	}
	wcex.hCursor = LoadCursor(NULL, IDC_ARROW); // Default cursor
	wcex.hbrBackground = NULL;
	wcex.lpszMenuName = NULL;
	wcex.lpszClassName = attribs->wndname;
	wcex.hIconSm = NULL;
	// Registering window
	if(!RegisterClassEx(&wcex))
	{
		strcpy(inerr ,"Unable to register the window.");
		return false;
	}
	wndname = new char[strlen(attribs->wndname) + 1];
	strcpy(wndname, attribs->wndname);
	// Find resolutions
	#ifdef CDEBUG
	CMsgBox("Finding supported resolutions...", "CDEBUG");
	#endif
	rescount = -1;
	DEVMODE screen;
	screen.dmSize = sizeof(DEVMODE);
	while (EnumDisplaySettings(NULL, ++rescount, &screen) != 0);
	if (rescount > 0)
	{
		resolution = new CResolution[rescount];
		mode = new DEVMODE[rescount];
		CLoops(i, 0, rescount)
		{
			mode[i].dmSize = sizeof(DEVMODE);
			EnumDisplaySettings(NULL, i, &mode[i]);
			resolution[i].width = mode[i].dmPelsWidth;
			resolution[i].height = mode[i].dmPelsHeight;
			resolution[i].bits = mode[i].dmBitsPerPel;
			resolution[i].freq = mode[i].dmDisplayFrequency;
		}
	}
	if (attribs->type == CWIN_FULLSCREEN)
	{
		#ifdef CDEBUG
		CMsgBox("Changing to fullscreen resolution...", "CDEBUG");
		#endif
		if (rescount < 1)
		{
			strcpy(inerr ,"Fullscreen resolutions are not supported.");
			return false;
		}
		int wd, hd, bit, swd = -1, shd, sbit;
		CLoops(i, 0, rescount)
		{
			wd = abs(width - resolution[i].width);
			hd = abs(height - resolution[i].height);
			bit = abs((int)attribs->colorbits - resolution[i].bits);
			if (wd <= swd && hd <= shd && bit <= sbit || swd == -1)
			{
				swd = wd;
				shd = hd;
				sbit = bit;
				bestres = i;
			}
		}
		width = resolution[bestres].width;
		height = resolution[bestres].height;
		fullscreen = true;
		CSetCursor(width / 2, height / 2);
		if (ChangeDisplaySettings(&mode[bestres], CDS_FULLSCREEN) != DISP_CHANGE_SUCCESSFUL)
		{
			strcpy(inerr ,"Unable to switch to fullscreen mode.");
			return false;
		}
	}
	// Creating window
	#ifdef CDEBUG
	CMsgBox("Creating the window...", "CDEBUG");
	#endif
	unsigned long style;
	if (attribs->type == CWIN_STANDARD)
	{
		style = WS_OVERLAPPEDWINDOW;
		// Set client area exactly to desired size
		RECT wrect;
		wrect.left = 0;
		wrect.right = width;
		wrect.top = 0;
		wrect.bottom = height;
		AdjustWindowRect(&wrect, style, false);
	}
	else
		style = WS_POPUP;
	if (!(hWnd = CreateWindow(attribs->wndname, attribs->wndtitle, WS_CLIPSIBLINGS | WS_CLIPCHILDREN | style,
		xpos, ypos, width, height, NULL, NULL, hInstance, NULL)))
	{
		strcpy(inerr ,"Unable to create the window.");
		return false;
	}
	SetWindowLongPtr(hWnd, GWLP_USERDATA, (LONG_PTR)this);
	// Show it properly
	ShowWindow(hWnd, SW_SHOW);
	SetFocus(hWnd);
	// OpenGL
	#ifdef CDEBUG
	CMsgBox("Setting OpenGL attributes...", "CDEBUG");
	#endif
	PIXELFORMATDESCRIPTOR pfd;
	HGLRC tmpRC;
	int iFormat;
	if (!(hDC = GetDC(hWnd)))
	{
		strcpy(inerr ,"Unable to create a device context.");
		return false;
	}
	memset(&pfd, 0, sizeof(pfd));
	pfd.nSize = sizeof(pfd);
	pfd.nVersion = 1;
	pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
	pfd.iPixelType = PFD_TYPE_RGBA;
	pfd.cColorBits = attribs->colorbits;
	pfd.cDepthBits = attribs->depthbits;
	pfd.iLayerType = PFD_MAIN_PLANE;
	if (!(iFormat = ChoosePixelFormat(hDC, &pfd)))
	{
		strcpy(inerr ,"Unable to find a suitable pixel format.");
		return false;
	}
	if (!SetPixelFormat(hDC, iFormat, &pfd))
	{
		strcpy(inerr ,"Unable to initialize the pixel format.");
		return false;
	}
	#ifdef CDEBUG
	CMsgBox("Creating the OpenGL context...", "CDEBUG");
	#endif
	if (!(tmpRC = wglCreateContext(hDC)))
	{
		strcpy(inerr ,"Unable to create a rendering context.");
		return false;
	}
	if (!wglMakeCurrent(hDC, tmpRC))
	{
		strcpy(inerr ,"Unable to activate the rendering context.");
		return false;
	}
	vers[0] = 1;
	glGetIntegerv(GL_MAJOR_VERSION, &i);
	glv[0] = i;
	glGetIntegerv(GL_MINOR_VERSION, &i);
	if (glGetError() == GL_INVALID_ENUM)
		vers[0] = 0;
	else
		glv[1] = i;
	if (vers[0] == 0)
	{
		strncpy(vers, (char*)glGetString(GL_VERSION), 3);
		vers[3] = '\0';
		unsigned short a, b;
		if (sscanf(vers, "%hu.%hu", &a, &b) != 2)
		{
			strcpy(inerr ,"Unable to retrieve the OpenGL version.");
			return false;
		}
		glv[0] = a;
		glv[1] = b;
	}
	#ifdef CDEBUG
	{
		char ag[11];
		strcpy(ag, "OpenGL ");
		strcat(ag, vers);
		CMsgBox(ag, "CDEBUG");
	}
	#endif
	hRC = NULL;
	if (glv[0] > 2 && CBit(attribs->properties, CPROP_MODERNGL)) // Have OpenGL 3.+ support
	{
		#ifdef CDEBUG
		CMsgBox("Creating new modern OpenGL context...", "CDEBUG");
		#endif
		if ((wglChoosePixelFormatARB = (PFNWGLCHOOSEPIXELFORMATARBPROC)wglGetProcAddress("wglChoosePixelFormatARB")))
		{
			const int attrib[] =
			{
				WGL_DRAW_TO_WINDOW_ARB, GL_TRUE,
				WGL_SUPPORT_OPENGL_ARB, GL_TRUE,
				WGL_ACCELERATION_ARB, WGL_FULL_ACCELERATION_ARB,
				WGL_DOUBLE_BUFFER_ARB, GL_TRUE,
				WGL_PIXEL_TYPE_ARB, WGL_TYPE_RGBA_ARB,
				WGL_COLOR_BITS_ARB, attribs->colorbits,
				//WGL_ALPHA_BITS_ARB, 8,
				WGL_DEPTH_BITS_ARB, attribs->depthbits,
				WGL_STENCIL_BITS_ARB, 0,
				0, 0
			};
			const float fa[] =
			{
				0.0f, 0.0f
			};
			unsigned int nFormats;
			if (wglChoosePixelFormatARB(hDC, attrib, fa, 1, &iFormat, &nFormats) && nFormats > 0)
			{
				// Set new pixel format
				if (!SetPixelFormat(hDC, iFormat, &pfd))
				{
					strcpy(inerr ,"Unable to initialize modern pixel format.");
					return false;
				}
				#ifdef CDEBUG
				CMsgBox("Using modern pixel format...", "CDEBUG");
				#endif
			}
		}
		if ((wglCreateContextAttribsARB = (PFNWGLCREATECONTEXTATTRIBSARBPROC)wglGetProcAddress("wglCreateContextAttribsARB")))
		{
			const int attribs[] = {WGL_CONTEXT_MAJOR_VERSION_ARB, glv[0], WGL_CONTEXT_MINOR_VERSION_ARB, glv[1], WGL_CONTEXT_FLAGS_ARB, 0,0};
			hRC = wglCreateContextAttribsARB(hDC, 0, attribs);
			if (hRC != NULL) // Use new context if good
			{
				wglMakeCurrent(NULL, NULL);
				wglDeleteContext(tmpRC);
				if (!wglMakeCurrent(hDC, hRC))
				{
					strcpy(inerr ,"Unable to activate the rendering context.");
					return false;
				}
				#ifdef CDEBUG
				CMsgBox("Using modern OpenGL context...", "CDEBUG");
				#endif
				moderncontext = true;
			}
		}
	}
	if (hRC == NULL)
	{
		hRC = tmpRC;
		moderncontext = false;
	}
	// Sound (windowed mode required hWnd)
	if (CBit(attribs->properties, CPROP_SOUND))
	{
		#ifdef CDEBUG
		CMsgBox("Initializing sound...", "CDEBUG");
		#endif
		/*if (!BASS_Init(-1, 44100, 0, hWnd, NULL))
		{
			strcpy(inerr ,"Unable to initialize the sound output device.");
			return false;
		}*/
	}
	#elif defined(COS_LINUX)
	// Setting up display/connection
	#ifdef CDEBUG
	CMsgBox("Initializing X connection...", "CDEBUG");
	#endif
	int attrib[] = {GLX_RGBA, GLX_DOUBLEBUFFER, GLX_RED_SIZE, 4, GLX_GREEN_SIZE, 4, GLX_BLUE_SIZE,
        4, GLX_DEPTH_SIZE, 16, None};
    hDis = XOpenDisplay(NULL);
    if (hDis == NULL)
    {
		strcpy(inerr ,"Unable to open display.");
        return false;
    }
    hScr = DefaultScreen(hDis);
    hVi = glXChooseVisual(hDis, hScr, attrib);
    if (hVi == NULL)
    {
		strcpy(inerr ,"Unable to setup visual data.");
        return false;
    }
	hRoot = RootWindow(hDis, hVi->screen);
    cMap = XCreateColormap(hDis, hRoot, hVi->visual, AllocNone);
	// Resolutions
	#ifdef CDEBUG
	CMsgBox("Finding supported resolutions...", "CDEBUG");
	#endif
	int event, error;
	if (!XF86VidModeQueryExtension(hDis, &event, &error) && !XRRQueryExtension(hDis, &event, &error))
	{
		strcpy(inerr ,"X server does not support fullscreen.");
		return false;
	}
    XF86VidModeGetAllModeLines(hDis, hScr, &rescount, &mode);
	XRRScreenConfiguration* conf = XRRGetScreenInfo(hDis, hRoot);
	resolution = new CResolution[rescount];
	event = XRRConfigCurrentRate(conf);
	error = DefaultDepth(hDis, hScr);
	CLoops(i, 0, rescount)
	{
		resolution[i].width = mode[i]->hdisplay;
		resolution[i].height = mode[i]->vdisplay;
		resolution[i].freq = event;
		resolution[i].bits = error;
	}
	XRRFreeScreenConfigInfo(conf);
    // Setting properties
    maxwidth = DisplayWidth(hDis, hScr);
    maxheight = DisplayHeight(hDis, hScr);
	if (width < 0)
	{
		width = maxwidth;
		height= maxheight;
	}
	if (attribs->type == CWIN_FULLSCREEN)
	{
		if (rescount < 1)
		{
			strcpy(inerr ,"Fullscreen resolutions are not supported.");
			return false;
		}
		int wd, hd, swd = -1, shd;
    	CLoops(i, 0, rescount)
   		{
			wd = abs(width - resolution[i].width);
			hd = abs(height - resolution[i].height);
			//bit = abs((int)attribs->colorbits - resolution[i].bits) linux already has optimal colour depth
			if (wd <= swd && hd <= shd || swd == -1)
			{
				swd = wd;
				shd = hd;
				bestres = i;
			}
		}
		width = resolution[bestres].width;
		height = resolution[bestres].height;
		fullscreen = true;
	}
	#ifdef CDEBUG
	CMsgBox("Setting window attributes...", "CDEBUG");
	#endif
    wAtt.colormap = cMap;
    wAtt.border_pixel = 0;
    wAtt.event_mask = KeyPressMask | KeyReleaseMask | ButtonPressMask | ButtonReleaseMask | PointerMotionMask | FocusChangeMask;
    wAtt.override_redirect = true;
    XWMHints* wHints = XAllocWMHints();
    XClassHint *cHints = XAllocClassHint();
    XSizeHints* sHints = XAllocSizeHints();
    if (wHints == NULL || cHints == NULL || sHints == NULL)
    {
        strcpy(inerr ,"Unable to allocate WM hints.");
        return false;
    }
    XTextProperty wName, cName;
    XStringListToTextProperty(&attribs->wndtitle, 1, &wName);
    XStringListToTextProperty(&attribs->wndname, 1, &cName);
    wHints->initial_state = NormalState;
    wHints->input = false;
    wHints->flags = InputHint | StateHint;
    cHints->res_name = attribs->wndtitle;
    cHints->res_class = attribs->wndname;
    sHints->flags = PPosition | PSize;
    // Final window	
	#ifdef CDEBUG
	CMsgBox("Creating the window...", "CDEBUG");
	#endif
    hWin = XCreateWindow(hDis, hRoot, xpos, ypos, width, height, 0, hVi->depth, InputOutput, hVi->visual,
                        CWBorderPixel | CWColormap | CWEventMask | CWOverrideRedirect, &wAtt);
	XSetWMProperties(hDis, hWin, &wName, &cName, NULL, 0, sHints, wHints, cHints);
    // Icon
    unsigned int pw, ph;
    int px, py;
    /*if (attribs->iconpath != NULL)
    {
        if (XpmReadFileToPixmap(hDis, hWin, attribs->iconpath, &hIcon, NULL, &catt) != XpmSuccess)
        {
            strcpy(inerr ,"Unable find/load the icon pixmap.");
            hIcon = NULL;
            return false;
        }
    }
    else*/
    hIcon = NULL;
    // Fake properties
    wHints->icon_pixmap = hIcon;
    wHints->initial_state = NormalState;
    wHints->input = true;
    wHints->flags = InputHint | StateHint | IconPixmapHint;
    cHints->res_name = attribs->wndtitle;
    cHints->res_class = attribs->wndname;
    sHints->flags = PPosition|PSize;
	// Create fake window
    XSetWindowAttributes tAtt;
    tAtt.event_mask = FocusChangeMask;
    fake = XCreateWindow(hDis, hRoot, -1, -1, 1, 1, 0, 0, InputOutput, 0, CWEventMask, &tAtt);
	XSetWMProperties(hDis, fake, &wName, &cName, NULL, 0, sHints, wHints, cHints);
	XFree(wHints);
    XFree(cHints);
    XFree(sHints);
	// Setup keys
    CKEY_A = XKeysymToKeycode(hDis, XK_a);
    CKEY_B = XKeysymToKeycode(hDis, XK_b);
    CKEY_C = XKeysymToKeycode(hDis, XK_c);
    CKEY_D = XKeysymToKeycode(hDis, XK_d);
    CKEY_E = XKeysymToKeycode(hDis, XK_e);
    CKEY_F = XKeysymToKeycode(hDis, XK_f);
    CKEY_G = XKeysymToKeycode(hDis, XK_g);
    CKEY_H = XKeysymToKeycode(hDis, XK_h);
    CKEY_I = XKeysymToKeycode(hDis, XK_i);
    CKEY_J = XKeysymToKeycode(hDis, XK_j);
    CKEY_K = XKeysymToKeycode(hDis, XK_k);
    CKEY_L = XKeysymToKeycode(hDis, XK_l);
    CKEY_M = XKeysymToKeycode(hDis, XK_m);
    CKEY_N = XKeysymToKeycode(hDis, XK_n);
    CKEY_O = XKeysymToKeycode(hDis, XK_o);
    CKEY_P = XKeysymToKeycode(hDis, XK_p);
    CKEY_Q = XKeysymToKeycode(hDis, XK_q);
    CKEY_R = XKeysymToKeycode(hDis, XK_r);
    CKEY_S = XKeysymToKeycode(hDis, XK_s);
    CKEY_T = XKeysymToKeycode(hDis, XK_t);
    CKEY_U = XKeysymToKeycode(hDis, XK_u);
    CKEY_V = XKeysymToKeycode(hDis, XK_v);
    CKEY_W = XKeysymToKeycode(hDis, XK_w);
    CKEY_X = XKeysymToKeycode(hDis, XK_x);
    CKEY_Y = XKeysymToKeycode(hDis, XK_y);
    CKEY_Z = XKeysymToKeycode(hDis, XK_z);
    CKEY_0 = XKeysymToKeycode(hDis, XK_0);
    CKEY_1 = XKeysymToKeycode(hDis, XK_1);
    CKEY_2 = XKeysymToKeycode(hDis, XK_2);
    CKEY_3 = XKeysymToKeycode(hDis, XK_3);
    CKEY_4 = XKeysymToKeycode(hDis, XK_4);
    CKEY_5 = XKeysymToKeycode(hDis, XK_5);
    CKEY_6 = XKeysymToKeycode(hDis, XK_6);
    CKEY_7 = XKeysymToKeycode(hDis, XK_7);
    CKEY_8 = XKeysymToKeycode(hDis, XK_8);
    CKEY_9 = XKeysymToKeycode(hDis, XK_9);
    CKEY_SPACE = XKeysymToKeycode(hDis, XK_space);
    CKEY_BACKSPACE = XKeysymToKeycode(hDis, XK_BackSpace);
    CKEY_TAB = XKeysymToKeycode(hDis, XK_Tab);
    CKEY_ENTER = XKeysymToKeycode(hDis, XK_Return);
    CKEY_RIGHT = XKeysymToKeycode(hDis, XK_Right);
    CKEY_LEFT = XKeysymToKeycode(hDis, XK_Left);
    CKEY_DOWN = XKeysymToKeycode(hDis, XK_Down);
    CKEY_UP = XKeysymToKeycode(hDis, XK_Up);
    CKEY_SEMICOLON = XKeysymToKeycode(hDis, XK_semicolon);
    CKEY_DIVIDE = XKeysymToKeycode(hDis, XK_slash);
    CKEY_TILDE = XKeysymToKeycode(hDis, XK_asciitilde);
    CKEY_RBRACKET = XKeysymToKeycode(hDis, XK_bracketright);
    CKEY_LBRACKET = XKeysymToKeycode(hDis, XK_bracketleft);
	CKEY_SLASH = XKeysymToKeycode(hDis, XK_slash);
    CKEY_BACKSLASH = XKeysymToKeycode(hDis, XK_backslash);
    CKEY_QUOTE = XKeysymToKeycode(hDis, XK_apostrophe);
    CKEY_MINUS = XKeysymToKeycode(hDis, XK_minus);
    CKEY_PLUS = XKeysymToKeycode(hDis, XK_plus);
    CKEY_PERIOD = XKeysymToKeycode(hDis, XK_period);
    CKEY_COMMA = XKeysymToKeycode(hDis, XK_comma);
    CKEY_RSHIFT = XKeysymToKeycode(hDis, XK_Shift_R);
    CKEY_LSHIFT = XKeysymToKeycode(hDis, XK_Shift_L);
    CKEY_RCTRL = XKeysymToKeycode(hDis, XK_Control_R);
    CKEY_LCTRL = XKeysymToKeycode(hDis, XK_Control_L);
    CKEY_ESC = XKeysymToKeycode(hDis, XK_Escape);
    CKEY_F1 = XKeysymToKeycode(hDis, XK_F1);
    // Setup the cursors
	Pixmap blank;
	XColor col;
	char data[1] = {0};
	blank = XCreateBitmapFromData (hDis, hWin, data, 1, 1);
	if (blank == None)
	{
		strcpy(inerr ,"Failed to allocate cursors.");
        	return false;
	}
	hide = XCreatePixmapCursor(hDis, blank, blank, &col, &col, 0, 0);
	XFreePixmap(hDis, blank);
    // OpenGL
	#ifdef CDEBUG
	CMsgBox("Setting OpenGL attributes...", "CDEBUG");
	#endif
	if (!glXQueryExtension(hDis, &i, &i))
    {
        strcpy(inerr ,"X server does not have GLX extension support.");
        return false;
    }
	#ifdef CDEBUG
	CMsgBox("Creating the OpenGL context...", "CDEBUG");
	#endif
	hCx = glXCreateContext(hDis, hVi, 0, true);
    if (hCx == NULL)
    {
        strcpy(inerr ,"Unable create OpenGL context.");
        return false;
    }
    glXMakeCurrent(hDis, hWin, hCx);
	vers[0] = 1;
	glGetIntegerv(GL_MAJOR_VERSION, &i);
	glv[0] = i;
	glGetIntegerv(GL_MINOR_VERSION, &i);
	if (glGetError() == GL_INVALID_ENUM)
		vers[0] = 0;
	else
		glv[1] = i;
	if (vers[0] == 0)
	{
		strncpy(vers, (char*)glGetString(GL_VERSION), 3);
		vers[3] = '\0';
		unsigned short a, b;
		if (sscanf(vers, "%hu.%hu", &a, &b) != 2)
		{
			strcpy(inerr ,"Unable to retrieve the OpenGL version.");
			return false;
		}
		glv[0] = a;
		glv[1] = b;
	}
	#ifdef CDEBUG
	{
		char ag[11];
		strcpy(ag, "OpenGL ");
		strcat(ag, vers);
		CMsgBox(ag, "CDEBUG");
	}
	#endif
	// Show window
	if (fullscreen)
	{
		#ifdef CDEBUG
		CMsgBox("Changing to fullscreen resolution...", "CDEBUG");
		#endif
		CSetCursor(width / 2, height / 2);
		XF86VidModeSwitchToMode(hDis, hScr, mode[bestres]);
		XF86VidModeSetViewPort(hDis, hScr, 0, 0);
	}
	XMapWindow(hDis, fake);
	XMapRaised(hDis, hWin);
   	XSetInputFocus(hDis, hWin, RevertToNone, CurrentTime);
	// Sound
	if (CBit(attribs->properties, CPROP_SOUND))
	{
		#ifdef CDEBUG
		CMsgBox("Initialzing sound...", "CDEBUG");
		#endif
		/*if (!BASS_Init(1, 44100, 0, NULL, NULL))
		{
			strcpy(inerr ,"Unable to initialize the sound output device.");
			return false;
		}*/
	}
	#endif
	// OpenGL support
	CLoops(vers[0], 0, CGL_COREFEATURES)
	{
		glcorefeature[vers[0]] = false;
	}
	CLoops(vers[0], 0, CGL_EXTENSIONS)
	{
		glextensions[vers[0]] = false;
	}
	
	// Core
	#ifdef CDEBUG
	CMsgBox("Checking available OpenGL functionalities...", "CDEBUG");
	#endif
	#if defined(COS_WIN32)
	//if (glv[0] != 1 && glv[1] != 0)
	if (glv[0] > 1 || (glv[0] == 1 && glv[1] > 1))
	{
		glcorefeature[CGL_VA] = true;
	}
	
	if (glv[0] > 2)
	{
		// Mipmap
		glGenerateMipmap = (PFNGLGENERATEMIPMAPPROC)wglGetProcAddress("glGenerateMipmap");
		if (glGenerateMipmap)
			glcorefeature[CGL_MIPMAP] = true; // Use modern way
	}
	else if (glv[0] > 1 || (glv[0] == 1 && glv[1] > 3))
	{
		glcorefeature[CGL_MIPMAP] = true; // Use legacy method
	}
	
	if (glv[0] > 1 || (glv[0] == 1 && glv[1] > 4))
	{
		// VBO
		glGenBuffers = (PFNGLGENBUFFERSARBPROC)wglGetProcAddress("glGenBuffers");
		glBindBuffer = (PFNGLBINDBUFFERARBPROC)wglGetProcAddress("glBindBuffer");
		glBufferData = (PFNGLBUFFERDATAARBPROC)wglGetProcAddress("glBufferData");
		glBufferSubData = (PFNGLBUFFERSUBDATAARBPROC)wglGetProcAddress("glBufferSubData");
		glDeleteBuffers = (PFNGLDELETEBUFFERSARBPROC)wglGetProcAddress("glDeleteBuffers");
		glGetBufferParameteriv = (PFNGLGETBUFFERPARAMETERIVARBPROC)wglGetProcAddress("glGetBufferParameteriv");
		glMapBuffer = (PFNGLMAPBUFFERARBPROC)wglGetProcAddress("glMapBuffer");
		glUnmapBuffer = (PFNGLUNMAPBUFFERARBPROC)wglGetProcAddress("glUnmapBuffer");
		if (glGenBuffers && glBindBuffer && glBufferData && glBufferSubData && glMapBuffer && glUnmapBuffer && glDeleteBuffers && glGetBufferParameteriv)
			glcorefeature[CGL_VBO] = true;
	}
	if (glv[0] > 1)
	{
		// Shader
		glCreateProgram = (PFNGLCREATEPROGRAMPROC)wglGetProcAddress("glCreateProgram");
		glDeleteProgram = (PFNGLDELETEPROGRAMPROC)wglGetProcAddress("glDeleteProgram");
	    glUseProgram = (PFNGLUSEPROGRAMPROC)wglGetProcAddress("glUseProgram");
		glAttachShader = (PFNGLATTACHSHADERPROC)wglGetProcAddress("glAttachShader");
		glDetachShader = (PFNGLDETACHSHADERPROC)wglGetProcAddress("glDetachShader");
		glLinkProgram = (PFNGLLINKPROGRAMPROC)wglGetProcAddress("glLinkProgram");
		glCreateShader = (PFNGLCREATESHADERPROC)wglGetProcAddress("glCreateShader");
		glDeleteShader = (PFNGLDELETESHADERPROC)wglGetProcAddress("glDeleteShader");
		glShaderSource = (PFNGLSHADERSOURCEPROC)wglGetProcAddress("glShaderSource");
		glCompileShader = (PFNGLCOMPILESHADERPROC)wglGetProcAddress("glCompileShader");
		glGetShaderiv = (PFNGLGETSHADERIVPROC)wglGetProcAddress("glGetShaderiv");
		glGetProgramiv = (PFNGLGETPROGRAMIVPROC)wglGetProcAddress("glGetProgramiv");
		glGetShaderInfoLog = (PFNGLGETSHADERINFOLOGPROC)wglGetProcAddress("glGetShaderInfoLog");
		glGetUniformLocation = (PFNGLGETUNIFORMLOCATIONPROC)wglGetProcAddress("glGetUniformLocation");
		glIsProgram = (PFNGLISPROGRAMPROC)wglGetProcAddress("glIsProgram");
		glIsShader = (PFNGLISSHADERPROC)wglGetProcAddress("glIsShader");
		glUniform1i = (PFNGLUNIFORM1IPROC)wglGetProcAddress("glUniform1i");
		glUniform1iv = (PFNGLUNIFORM1IVPROC)wglGetProcAddress("glUniform1iv");
		glUniform2iv = (PFNGLUNIFORM2IVPROC)wglGetProcAddress("glUniform2iv");
		glUniform3iv = (PFNGLUNIFORM3IVPROC)wglGetProcAddress("glUniform3iv");
		glUniform4iv = (PFNGLUNIFORM4IVPROC)wglGetProcAddress("glUniform4iv");
		glUniform1f = (PFNGLUNIFORM1FPROC)wglGetProcAddress("glUniform1f");
		glUniform1fv = (PFNGLUNIFORM1FVPROC)wglGetProcAddress("glUniform1fv");
		glUniform2fv = (PFNGLUNIFORM2FVPROC)wglGetProcAddress("glUniform2fv");
		glUniform3fv = (PFNGLUNIFORM3FVPROC)wglGetProcAddress("glUniform3fv");
		glUniform4fv = (PFNGLUNIFORM4FVPROC)wglGetProcAddress("glUniform4fv");
		glUniformMatrix4fv = (PFNGLUNIFORMMATRIX4FVPROC)wglGetProcAddress("glUniformMatrix4fv");
		glGetAttribLocation = (PFNGLGETATTRIBLOCATIONPROC)wglGetProcAddress("glGetAttribLocation");
		glVertexAttrib1f = (PFNGLVERTEXATTRIB1FPROC)wglGetProcAddress("glVertexAttrib1f");
		glVertexAttrib1fv = (PFNGLVERTEXATTRIB1FVPROC)wglGetProcAddress("glVertexAttrib1fv");
		glVertexAttrib2fv = (PFNGLVERTEXATTRIB2FVPROC)wglGetProcAddress("glVertexAttrib2fv");
		glVertexAttrib3fv = (PFNGLVERTEXATTRIB3FVPROC)wglGetProcAddress("glVertexAttrib3fv");
		glVertexAttrib4fv = (PFNGLVERTEXATTRIB4FVPROC)wglGetProcAddress("glVertexAttrib4fv");
		glEnableVertexAttribArray = (PFNGLENABLEVERTEXATTRIBARRAYPROC)wglGetProcAddress("glEnableVertexAttribArray");
		glDisableVertexAttribArray = (PFNGLDISABLEVERTEXATTRIBARRAYPROC)wglGetProcAddress("glDisableVertexAttribArray");
		glVertexAttribPointer = (PFNGLVERTEXATTRIBPOINTERPROC)wglGetProcAddress("glVertexAttribPointer");
		glBindAttribLocation = (PFNGLBINDATTRIBLOCATIONPROC)wglGetProcAddress("glBindAttribLocation");
		if (glCreateProgram && glDeleteProgram && glUseProgram && glAttachShader && glDetachShader && glLinkProgram && glCreateShader && glDeleteShader && glShaderSource &&
			glCompileShader && glGetShaderiv && glGetProgramiv && glGetShaderInfoLog && glGetUniformLocation && glUniform1i && glUniformMatrix4fv && glGetAttribLocation &&
			glVertexAttrib1f && glEnableVertexAttribArray && glDisableVertexAttribArray && glVertexAttribPointer && glBindAttribLocation)
			glcorefeature[CGL_SHADER] = true;
	}
	if (glv[0] > 2)
	{
		// VAO
		glGenVertexArrays = (PFNGLGENVERTEXARRAYSPROC)wglGetProcAddress("glGenVertexArrays");
		glDeleteVertexArrays = (PFNGLDELETEVERTEXARRAYSPROC)wglGetProcAddress("glDeleteVertexArrays");
		glBindVertexArray = (PFNGLBINDVERTEXARRAYPROC)wglGetProcAddress("glBindVertexArray");
		if (glGenVertexArrays && glDeleteVertexArrays && glBindVertexArray)
			glcorefeature[CGL_VAO] = true;
	}
	#elif defined(COS_LINUX)
	if (glv[0] != 1 && glv[1] != 0)
		glcorefeature[CGL_VA] = true;
	if (glv[0] > 1 || (glv[0] == 1 && glv[1] > 4))
		glcorefeature[CGL_VBO] = true;
	if (glv[0] > 1)
		glcorefeature[CGL_SHADER] = true;
	if (glv[0] > 2)
		glcorefeature[CGL_VAO] = true;
	#endif
	if (CBit(attribs->properties, CPROP_GLEXT))
	{
		// Extensions
		#ifdef CDEBUG
		CMsgBox("Loading OpenGL extensions...", "CDEBUG");
		#endif
		if (CCheckExtension("GL_EXT_texture_filter_anisotropic"))
			glGetFloatv(GL_TEXTURE_MAX_ANISOTROPY_EXT, &maxAF);
		else
			glextensions[CGL_ANIFILTER] = false;
		#if defined(COS_WIN32)
		if (CCheckExtension("GL_ARB_vertex_buffer_object"))
		{
			glGenBuffersARB = (PFNGLGENBUFFERSARBPROC)wglGetProcAddress("glGenBuffersARB");
			glBindBufferARB = (PFNGLBINDBUFFERARBPROC)wglGetProcAddress("glBindBufferARB");
			glBufferDataARB = (PFNGLBUFFERDATAARBPROC)wglGetProcAddress("glBufferDataARB");
			glBufferSubDataARB = (PFNGLBUFFERSUBDATAARBPROC)wglGetProcAddress("glBufferSubDataARB");
			glDeleteBuffersARB = (PFNGLDELETEBUFFERSARBPROC)wglGetProcAddress("glDeleteBuffersARB");
			glGetBufferParameterivARB = (PFNGLGETBUFFERPARAMETERIVARBPROC)wglGetProcAddress("glGetBufferParameterivARB");
			glMapBufferARB = (PFNGLMAPBUFFERARBPROC)wglGetProcAddress("glMapBufferARB");
			glUnmapBufferARB = (PFNGLUNMAPBUFFERARBPROC)wglGetProcAddress("glUnmapBufferARB");
		}
		if (!glGenBuffersARB || !glBindBufferARB || !glBufferDataARB || !glBufferSubDataARB || !glMapBufferARB || !glUnmapBufferARB || !glDeleteBuffersARB || !glGetBufferParameterivARB)
			glextensions[CGL_VBO] = false;
		#elif defined(COS_LINUX)
		if (glv[0] == 1)
		{
			if (glv[1] < 5)
			{
				if (CCheckExtension("GL_ARB_vertex_buffer_object"))
				{
					glGenBuffersARB = (PFNGLGENBUFFERSARBPROC)glXGetProcAddress((GLubyte*)"glGenBuffersARB");
					glBindBufferARB = (PFNGLBINDBUFFERARBPROC)glXGetProcAddress((GLubyte*)"glBindBufferARB");
					glBufferDataARB = (PFNGLBUFFERDATAARBPROC)glXGetProcAddress((GLubyte*)"glBufferDataARB");
					glBufferSubDataARB = (PFNGLBUFFERSUBDATAARBPROC)glXGetProcAddress((GLubyte*)"glBufferSubDataARB");
					glDeleteBuffersARB = (PFNGLDELETEBUFFERSARBPROC)glXGetProcAddress((GLubyte*)"glDeleteBuffersARB");
					glGetBufferParameterivARB = (PFNGLGETBUFFERPARAMETERIVARBPROC)glXGetProcAddress((GLubyte*)"glGetBufferParameterivARB");
					glMapBufferARB = (PFNGLMAPBUFFERARBPROC)glXGetProcAddress((GLubyte*)"glMapBufferARB");
					glUnmapBufferARB = (PFNGLUNMAPBUFFERARBPROC)glXGetProcAddress((GLubyte*)"glUnmapBufferARB");
				}
				if (!glGenBuffersARB || !glBindBufferARB || !glBufferDataARB || !glBufferSubDataARB || !glMapBufferARB || !glUnmapBufferARB || !glDeleteBuffersARB || !glGetBufferParameterivARB)
					glextensions[CGL_VBO] = false;
			}
		}
		#endif
	}
	if (glcorefeature[CGL_SHADER] || glextensions[CGL_SHADER])
	{
		strncpy(vers, (char*)glGetString(GL_SHADING_LANGUAGE_VERSION), 3);
		vers[3] = '\0';
		unsigned short a, b;
		if (sscanf(vers, "%hu.%hu", &a, &b) != 2)
		{
			strcpy(inerr ,"Unable to retrieve the GLSL version.");
			return false;
		}
		glslv[0] = a;
		glslv[1] = b;
	}
	// CEngine OpenGL states
	#ifdef CDEBUG
	CMsgBox("Setting OpenGL states...", "CDEBUG");
	#endif
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glEnable(GL_BLEND);
	glEnable(GL_TEXTURE_2D);
	glClearColor(attribs->background[0], attribs->background[1], attribs->background[2], attribs->background[3]);
	//glPixelStorei(GL_UNPACK_ALIGNMENT, 4); default alignment
	glcorefeature[CGL_VBO] = false; // not supported
	// GUI model
	guimodel.primitive = CMODEL_TRISTRIPS;
	guimodel.vertexcnt = 4;
	guimodel.vertex = new float[8];
	guimodel.texcoord = new float[8];
	guimodel.texcoord[0] = 0.0f;guimodel.texcoord[1] = 0.0f;
	guimodel.texcoord[2] = 1.0f;guimodel.texcoord[3] = 0.0f;
	guimodel.texcoord[4] = 0.0f;guimodel.texcoord[5] = 1.0f;
	guimodel.texcoord[6] = 1.0f;guimodel.texcoord[7] = 1.0f;
	return true;
}

bool CEngine::CCleanupEngine()
{
	bool result = true;
	//BASS_Free();
	if (resolution != NULL)
		delete[] resolution;
	#if defined(COS_WIN32)
	FreeConsole();
	if (fullscreen)
	{
		ChangeDisplaySettings(NULL, 0);
		delete[] mode;
	}
	/*if (winsock && WSACleanup() == SOCKET_ERROR)
	{
		strcpy(inerr ,"CCleanupEngine: Unable successfully deregister Winsock.");
		result = false;
	}*/
	if (hRC)
	{
		if (!wglMakeCurrent(NULL, NULL))
		{
			strcpy(inerr ,"CCleanupEngine: Unable release the device context and rendering context.");
			result = false;
		}
		if (!wglDeleteContext(hRC))
		{
			strcpy(inerr ,"CCleanupEngine: Unable to release the rendering context.");
			result = false;
		}
		hRC = NULL;
	}
	if (hDC && !ReleaseDC(hWnd, hDC))
	{
		strcpy(inerr ,"CCleanupEngine: Unable to release the device context.");
		hDC = NULL;
		result = false;
	}
	if (hWnd && !DestroyWindow(hWnd))
	{
		strcpy(inerr ,"CCleanupEngine: Unable to destroy the window.");
		hWnd = NULL;
		result = false;
	}
	if (hInstance && wndname != NULL && !UnregisterClass(wndname, hInstance))
	{
		strcpy(inerr ,"CCleanupEngine: Unable to clear the window data.");
		hInstance = NULL;
		delete[] wndname;
		result = false;
	}
	#elif defined(COS_LINUX)
	if (hIcon != NULL)
		 XFreePixmap(hDis, hIcon);
	if (hide != NULL)
		 XFreeCursor(hDis, hide);
	if (hCursor != NULL)
		XFreeCursor(hDis, hCursor);
	if (fullscreen)
	{
		int i;
		XF86VidModeSwitchToMode(hDis, hScr, mode[0]);
		XF86VidModeSetViewPort(hDis, hScr, 0, 0);
		XFree(mode);
	}
    if (hCx)
    {
		 if (!glXMakeCurrent(hDis, None, NULL))
		 {
			 strcpy(inerr ,"CCleanupEngine: Unable to release the rendering context.");
			result = false;
		 }
		 glXDestroyContext(hDis, hCx);
		 hCx = NULL;
    }
	XDestroyWindow(hDis, hWin);
	XDestroyWindow(hDis, fake);
    XCloseDisplay(hDis);
	#endif
	CDeleteModel(&guimodel);
	return result;
}

bool CEngine::CAbsolutePath(char *storage, size_t size)
{
	// Path
	int bytes;
	storage[size - 2] = '\0'; //memset(storage, 0, size);
	#if defined(COS_WIN32)
	bytes = GetModuleFileName(NULL, storage, size);
	if (bytes == 0)
   	{
		strcpy(inerr ,"CAbsolutePath: Unable to get the absolute path.");
		return false;
	}
	#elif defined(COS_LINUX)
	char tmp[32];
	sprintf(tmp, "/proc/%d/exe", getpid());
	bytes = readlink(tmp, storage, size - 1);
	if(bytes >= 0)
		storage[bytes] = '\0';
	else
	{
		strcpy(inerr ,"CAbsolutePath: Unable to get the absolute path.");
		return false;
	}
	#endif
	int i;
	bytes--;
	CLoopr(i, bytes, 0, 1)
	{
		if (storage[i] == '/' || storage[i] == '\\')
			break;
		else if (i == 0)
		{
			strcpy(inerr ,"CAbsolutePath: Absolute path retrieved is invalid.");
			return false;
		}
	}
	storage[i + 1] = '\0';
	return true;
}

char *CEngine::CGetLastError()
{
	return inerr;
}

void CEngine::CGetVersion(unsigned char *major, unsigned char *minor, signed char *revision)
{
	if (major != NULL)
		(*major) = CVERSION_MAJOR;
	if (minor != NULL)
		(*minor) = CVERSION_MINOR;
	if (revision != NULL)
		(*revision) = CVERSION_REVISION;
}

void CEngine::CDeleteResource(unsigned char* ptr, bool isarray)
{
	if (isarray)
		delete[] ptr;
	else
		delete ptr;
}

bool CEngine::COpenFile(const char *path, char** buf, size_t* bufsize, unsigned char mode, bool binary)
{
	char mod[3];
	long len;
	FILE* f;
	if (mode == CFILE_READ)
		strcpy(mod, "r");
	else if (mode == CFILE_WRITE)
		strcpy(mod, "w");
	else if (mode == CFILE_APPEND)
		strcpy(mod, "a");
	else
		return false; // Unknown value
	if (binary)
		strcat(mod, "b");
	if ((f = fopen(path, mod)) == NULL)
	{
		strcpy(inerr, "COpenFile: Unable to open/find the file.");
		return false;
	}
	fseek(f, 0, SEEK_END);
	len = ftell(f);
	if (len == 0)
	{
		strcpy(inerr, "COpenFile: File opened is empty.");
		fclose(f);
		return false;
	}
	if (mode == CFILE_READ)
	{
		rewind(f);
		(*buf) = new char[len + 1];
		if (bufsize != NULL)
			(*bufsize) = len + 1;
		size_t result = fread(*buf, 1, len, f);
		(*buf)[result] = '\0';
		fclose(f);
		if (binary && result != len)
		{
			strcpy(inerr, "COpenFile: Failed to successfully read the file.");
			delete[] (*buf);
			return false;
		}
	}
	else if (mode == CFILE_WRITE)
	{
	}
	else
	{
	}
	return true;
}

bool CEngine::CParseData(char *inbuf, char *outbuf, char *stopchars, unsigned int readsize, unsigned int outsize, unsigned int *progress)
{
	unsigned char c, m = strlen(stopchars);
	unsigned int pos = 0, pprogress;
	if (progress != NULL)
		pprogress = (*progress);
	else
		pprogress = 0;
	while (true)
	{
		if (pos < outsize)
		{
			if (pprogress == readsize)
			{
				outbuf[pos] = '\0';
				if (progress != NULL)
					(*progress) = readsize;
				break;
			}
			CLoops(c, 0, m)
			{
				if (inbuf[pprogress] == stopchars[c])
				{
					outbuf[pos] = '\0';
					if (progress != NULL)
						(*progress) = ++pprogress;
					return true;
				}
			}
			outbuf[pos] = inbuf[pprogress];
			pos++;
			pprogress++;
		}
		else
		{
			strcpy(inerr, "CParseData: Output buffer is too small.");
			return false;
		}
	}
	return true;
}
		
void CEngine::CShiftCharArray(char *str, int shift, unsigned int size, char fillup)
{
	unsigned int end, b;
	if (shift > 0) // To the right
	{
		b = shift - 1;
		CLoopr(end, size, b, 1)
		{
			str[end] = str[end - shift];
		}
		CLoops(end, 0, (unsigned)shift)
		{
			str[end] = fillup;
		}
	}
	else // To the left
	{
		CLoops(end, 0, size)
		{
			str[end] = str[end - shift];
		}
		b = end + 1;
		CLoops(end, b, size)
		{
			str[end] = fillup;
		}
	}
}

char CEngine::CKeyToChar(unsigned short key, bool shift) // Compliant with the standard US-keyboard
{
	// Numbers
	if (!shift)
	{
		// Numerals
		if (key == CKEY_0)
			return '0';
		else if (key == CKEY_1)
			return '1';
		else if (key == CKEY_2)
			return '2';
		else if (key == CKEY_3)
			return '3';
		else if (key == CKEY_4)
			return '4';
		else if (key == CKEY_5)
			return '5';
		else if (key == CKEY_6)
			return '6';
		else if (key == CKEY_7)
			return '7';
		else if (key == CKEY_8)
			return '8';
		else if (key == CKEY_9)
			return '9';
		// Letters
		else if (key == CKEY_A)
			return 'a';
		else if (key == CKEY_B)
			return 'b';
		else if (key == CKEY_C)
			return 'c';
		else if (key == CKEY_D)
			return 'd';
		else if (key == CKEY_E)
			return 'e';
		else if (key == CKEY_F)
			return 'f';
		else if (key == CKEY_G)
			return 'g';
		else if (key == CKEY_H)
			return 'h';
		else if (key == CKEY_I)
			return 'i';
		else if (key == CKEY_J)
			return 'j';
		else if (key == CKEY_K)
			return 'k';
		else if (key == CKEY_L)
			return 'l';
		else if (key == CKEY_M)
			return 'm';
		else if (key == CKEY_N)
			return 'n';
		else if (key == CKEY_O)
			return 'o';
		else if (key == CKEY_P)
			return 'p';
		else if (key == CKEY_Q)
			return 'q';
		else if (key == CKEY_R)
			return 'r';
		else if (key == CKEY_S)
			return 's';
		else if (key == CKEY_T)
			return 't';
		else if (key == CKEY_U)
			return 'u';
		else if (key == CKEY_V)
			return 'v';
		else if (key == CKEY_W)
			return 'w';
		else if (key == CKEY_X)
			return 'x';
		else if (key == CKEY_Y)
			return 'y';
		else if (key == CKEY_Z)
			return 'z';
		// Punctuation
		else if (key == CKEY_PLUS)
			return '=';
		else if (key == CKEY_QUOTE)
			return '\'';
		else if (key == CKEY_SEMICOLON)
			return ';';
		else if (key == CKEY_LBRACKET)
			return '[';
		else if (key == CKEY_RBRACKET)
			return ']';
		else if (key == CKEY_PERIOD)
			return '.';
		else if (key == CKEY_COMMA)
			return ',';
		else if (key == CKEY_SLASH)
			return '/';
		else if (key == CKEY_BACKSLASH)
			return '\\';
		else if (key == CKEY_TILDE)
			return '`';
		else if (key == CKEY_MINUS)
			return '-';
	}
	else
	{
		// Letters
		if (key == CKEY_A)
			return 'A';
		else if (key == CKEY_B)
			return 'B';
		else if (key == CKEY_C)
			return 'C';
		else if (key == CKEY_D)
			return 'D';
		else if (key == CKEY_E)
			return 'E';
		else if (key == CKEY_F)
			return 'F';
		else if (key == CKEY_G)
			return 'G';
		else if (key == CKEY_H)
			return 'H';
		else if (key == CKEY_I)
			return 'I';
		else if (key == CKEY_J)
			return 'J';
		else if (key == CKEY_K)
			return 'K';
		else if (key == CKEY_L)
			return 'L';
		else if (key == CKEY_M)
			return 'M';
		else if (key == CKEY_N)
			return 'N';
		else if (key == CKEY_O)
			return 'O';
		else if (key == CKEY_P)
			return 'P';
		else if (key == CKEY_Q)
			return 'Q';
		else if (key == CKEY_R)
			return 'R';
		else if (key == CKEY_S)
			return 'S';
		else if (key == CKEY_T)
			return 'T';
		else if (key == CKEY_U)
			return 'U';
		else if (key == CKEY_V)
			return 'V';
		else if (key == CKEY_W)
			return 'W';
		else if (key == CKEY_X)
			return 'X';
		else if (key == CKEY_Y)
			return 'Y';
		else if (key == CKEY_Z)
			return 'Z';
		// Symbols
		else if (key == CKEY_SEMICOLON)
			return ':';
		else if (key == CKEY_QUOTE)
			return '"';
		else if (key == CKEY_PERIOD)
			return '>';
		else if (key == CKEY_COMMA)
			return '<';
		else if (key == CKEY_SLASH)
			return '?';
		else if (key == CKEY_BACKSLASH)
			return '|';
		else if (key == CKEY_1)
			return '!';
		else if (key == CKEY_2)
			return '@';
		else if (key == CKEY_3)
			return '#';
		else if (key == CKEY_4)
			return '$';
		else if (key == CKEY_5)
			return '%';
		else if (key == CKEY_6)
			return '^';
		else if (key == CKEY_7)
			return '&';
		else if (key == CKEY_8)
			return '*';
		else if (key == CKEY_TILDE)
			return '~';
		// Brackets
		else if (key == CKEY_0)
			return ')';
		else if (key == CKEY_9)
			return '(';
		else if (key == CKEY_LBRACKET)
			return '{';
		else if (key == CKEY_RBRACKET)
			return '}';
		// Math
		else if (key == CKEY_PLUS)
			return '+';
		else if (key == CKEY_MINUS)
			return '_';
	}
	if (key == CKEY_SPACE)
		return ' ';
	return 0;
}

#if defined(COS_WIN32)
int CEngine::CSearchDir(char *path, const char *format, char*** filestr)
{
	// Count files
	unsigned int i = 1, loop;
	WIN32_FIND_DATA fdata;
	HANDLE hfind;
	hfind = FindFirstFile(strcat(path, format), &fdata);
	if (hfind == INVALID_HANDLE_VALUE)
	{
		if (GetLastError() == ERROR_FILE_NOT_FOUND)
			return 0;
		else
		{
			strcpy(inerr, "CSearchDir: Function failed to search directory.");
			return -1;
		}
	}
	while (FindNextFile(hfind, &fdata))
		i++;
	// Allocate buffer
	if (filestr != NULL)
	{
		(*filestr) = new char*[i];
		// Restart
		FindClose(hfind);
		hfind = FindFirstFile(path, &fdata);
		// Assign data and allocate
		loop = 0;
		(*filestr)[0] = new char[strlen(fdata.cFileName) + 1];
		strcpy((*filestr)[0], fdata.cFileName);
		while (FindNextFile(hfind, &fdata) != 0)
		{
			loop++;
			strlen(fdata.cFileName);
			(*filestr)[loop] = new char[strlen(fdata.cFileName) + 1];
			strcpy((*filestr)[loop], fdata.cFileName);
		}
	}
	// Close
	FindClose(hfind);
	return i;
}
#elif defined(COS_LINUX)
int CEngine::CSearchDir(char *path, const char *format, char*** filestr)
{
    unsigned int i=0;
    DIR* dir;
    dirent* temp;
    // Count files
    dir = opendir(path);
    if (dir == NULL)
	{
		strcpy(inerr, "CSearchDir: Function failed to search directory.");
        return -1;
	}
    while ((temp = readdir(dir)))
    {
        if (fnmatch(format, temp->d_name, FNM_PERIOD) == 0)
            i++;
    }
    closedir(dir);
    if (i == 0)
        return 0;
    // Allocate
	if (filestr != NULL)
	{
		(*filestr) = new char*[i];
		// Store in filestr
		dir = opendir(path);
		i = 0;
		while ((temp = readdir(dir)))
		{
			if (fnmatch(format, temp->d_name, FNM_PERIOD) == 0)
			{
				(*filestr)[i] = new char[strlen(temp->d_name) + 1];
				strcpy((*filestr)[i], temp->d_name);
				i++;
			}
		}
		closedir(dir);
	}
    return i;
}
#endif
