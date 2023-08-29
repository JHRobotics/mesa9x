/*
Copyright (c) 2016, David Liu
All rights reserved. Read the license for more information.
*/

/* Headers */
#include "CEngine.h"
#include "glchecker.h"
#include "benchmark.h"

/* Objects */
CEngine core;
Application app;

struct GLData
{
	char *glversion, *glslversion, *implementor, *renderer, *glxversion, **extensions;
	unsigned short extcnt;
	unsigned int extlen;
	GLData()
	{
	    glversion = NULL;
	    glslversion = NULL;
	    implementor = NULL;
	    glxversion = NULL;
	    extensions = NULL;
	    extcnt = extlen = 0;
	}
	~GLData()
	{
		unsigned short cnt;
		if (glversion != NULL)
			delete[] glversion;
		if (glslversion != NULL)
			delete[] glslversion;
		if (implementor != NULL)
			delete[] implementor;
		if (renderer != NULL)
			delete[] renderer;
		if (glxversion != NULL)
			delete[] glxversion;
		if (extcnt > 0)
		{
			CLoops(cnt, 0, extcnt)
			{
				delete[] extensions[cnt];
			}
			delete[] extensions;
		}
	}
};
GLData gldata;

#include "pages.h" // GUI pages

/* Functions */
void MoveWndSys()
{
	if (core.key[CKEY_RMOUSE])
	{
		if (app.pmx == -1)
		{
			app.pmx = core.mx;
			app.pmy = core.my;
		}
        core.CMoveWindow(core.xpos + core.mx - app.pmx, core.ypos + app.pmy - core.my, APP_WIDTH, APP_HEIGHT);
	}
	else if (app.pmx != -1)
		app.pmx =- 1;
}

char *RelativePath(const char *path, const char *directory)
{
	strcpy(app.temppath, app.rootpath);
	if (directory != NULL)
		strcat(app.temppath, directory);
	strcat(app.temppath, path);
	return app.temppath;
}

void UpdateLoading(const char *txt)
{
	glClear(GL_COLOR_BUFFER_BIT);
	core.CPrintText(txt, app.font[FONT_TEXT], 1.5f, 1.5f);
	core.CSwapBuffers();
	core.CProcessMessage();
}

void ReportError(const char *error, const char *path, bool message)
{
	while (true)
	{
		core.CProcessMessage();
		if (core.focus)
		{
			app.prevtime = core.CGetTime() - app.lefttime; // Keep app.lefttime constant
			glClear(GL_COLOR_BUFFER_BIT);
			if (message)
			{
				core.CPrintText(error, app.font[FONT_TEXT], 0.6f, 6.0f);
				if (core.CPushbutton("Go!", app.font[FONT_PUSH], &app.mask[MASK_LPUSH], app.sample, 4.25f, 1.0f, CGuicol_r) == CPUSH_CLICKED)
					return;
			}
			else
			{
				core.CPrintText(error, app.font[FONT_TEXT], 0.6f, 3.0f);
				if (path != NULL && path[0] != '\0')
				{
					core.CPrintText("Path:", app.font[FONT_TEXT], 0.6f, 2.5f);
					core.CPrintText(path, app.font[FONT_TEXT], 1.8f, 2.5f, CGuicol_s);
				}
				if (core.CPushbutton("OK", app.font[FONT_PUSH], &app.mask[MASK_LPUSH], app.sample, 1.0f, 1.0f, CGuicol_r) == CPUSH_CLICKED)
					return;
			}
			MoveWndSys();
			core.CDrawCursor(app.cursor);
			core.CSwapBuffers();
		}
		core.CSleep(APP_FRAME);
	}
}

/* Main */
bool Init()
{
	// Init variables
	unsigned int count, i;
	int maj, min;
	// Engine version
	
	core.CGetVersion(&app.cmaj, &app.cmin, &app.crev);
	
	if (CVERSION_MAJOR != app.cmaj || CVERSION_MINOR != app.cmin)
	{
		sprintf(app.rootpath, "%u.%u", app.cmaj, app.cmin);
		strcpy(app.temppath, "CEngine ");
		strcat(app.temppath, app.rootpath);
		strcat(app.temppath, " is required.");
		core.CMsgBox(app.temppath, "Fatal error");
		return false;
	}
	// Engine data
	if (!core.CAbsolutePath(app.rootpath, CSIZE_MAXPATH))
	{
		core.CMsgBox(core.CGetLastError(), "Fatal error");
		return false;
	}
	
	// Engine setup
	CWindow attributes;
	attributes.type = CWIN_POPUP;
	attributes.properties = CPROP_SOUND;
	attributes.wndname = (char*)"CWnd";
	attributes.wndtitle = (char*)"OpenGLChecker";
	attributes.iconpath = RelativePath("glchecker/data/icon.ico");
	attributes.colorbits = 32;
	attributes.depthbits = 16;
	attributes.background[0] = attributes.background[1] = attributes.background[2] = attributes.background[3] = 0.0f;
	attributes.pwidth = APP_WIDTH;
	attributes.pheight = APP_HEIGHT;
	attributes.posx = 0;
	attributes.posy = 0;
	
	if (!core.CSetupEngine(&attributes))
	{
		core.CMsgBox(core.CGetLastError(), "CEngine error");
		return false;
	}

	// Application
	if (!core.CCreateFont(RelativePath("glchecker/data/font_push.tga"), 0.4f, 0.8f, app.font[FONT_PUSH]) ||
		!core.CCreateFont(RelativePath("glchecker/data/font_base.tga"), 0.5f, 0.9f, app.font[FONT_DATA]) ||
		!core.CCreateFont(RelativePath("glchecker/data/font_text.tga"), 0.2f, 0.45f, app.font[FONT_TEXT]))
	{
		core.CMsgBox(core.CGetLastError(), "Fonts error");
		return false;
	}
	core.CToggleCursor(false);
	
	if (!core.CCreateCursor(app.cursor, RelativePath("glchecker/data/cursor.tga"), 0.5f, 0.5f))
	{
		core.CMsgBox(core.CGetLastError(), "Cursor error");
		return false;
	}
	
	core.CSoundVol(CSOUND_MAXVOL / 2);
	
	// Loader display and OpenGL attributes
	core.CUseShader(0);
	
	glCullFace(GL_BACK);
	glShadeModel(GL_SMOOTH);
	core.CVerticalSync(false);
	core.CResize2D(APP_WIDTH, APP_HEIGHT, 10.0, 10.0);
	UpdateLoading("Loading...");
	// Resources

	if (!core.CLoadSample(RelativePath("glchecker/data/sound_push.wav"), app.sample, 1))
	{
		ReportError("Warning: failed to find/load GUI sounds.");
		ReportError(core.CGetLastError());
	}
	if (!core.CLoadTGA(RelativePath("glchecker/data/mask_lpush.tga"), app.mask[MASK_LPUSH]) || 
		!core.CLoadTGA(RelativePath("glchecker/data/mask_mpush.tga"), app.mask[MASK_MPUSH]) || 
		!core.CLoadTGA(RelativePath("glchecker/data/mask_rpush.tga"), app.mask[MASK_RPUSH]) ||
		!core.CLoadTGA(RelativePath("glchecker/data/mask_button.tga"), app.mask[MASK_BUTTON]))
	{
		ReportError("Warning: failed to find/load GUI masks.");
		ReportError(core.CGetLastError());
	}
	core.CSetTexture(app.mask[MASK_LPUSH], 0);
	core.CSetTexture(app.mask[MASK_MPUSH], 0);
	core.CSetTexture(app.mask[MASK_RPUSH], 0);
	core.CSetTexture(app.mask[MASK_BUTTON], 0);

	if (!core.CLoadBMP(RelativePath("glchecker/data/credit.bmp"), app.credit))
	{
		ReportError("Warning: failed to find/load credits.");
		ReportError(core.CGetLastError());
	}
	else
		core.CSetTexture(app.credit, 0);
	// Find benchmarks
	app.benchcnt = core.CSearchDir(RelativePath("glchecker/benchmarks/"), "*.obs", &app.benchnames); // only look for .obs files
	if (app.benchcnt == -1)
	{
		ReportError("Warning: failed to search the benchmark directory.");
		ReportError(core.CGetLastError());
		return false;
	}
	
	// Setup scripter
	SetupScripter();
	// Application setup
	app.barcol[0] = 0.7f;
	app.barcol[1] = 1.0f;
	app.barcol[2] = 1.0f;
	app.extscrl = app.benchscrl = 0.0f;
	memset(app.extfind, 0, sizeof(app.extfind));
	app.extpos = 0;
	app.extclick = false;
	app.clickmem = true;
	app.pmx = -1;
	app.rot = 0.0f;
	app.benchmode = false;
	app.prevtime = app.lefttime = 0;
	// OpenGL strings
	count = strlen((char*)glGetString(GL_VERSION)) + 1;
	gldata.glversion = new char[count];
	strcpy(gldata.glversion, (char*)glGetString(GL_VERSION));
	
	count = 0;
	const char *shading_language = (const char*)glGetString(GL_SHADING_LANGUAGE_VERSION);
	if(shading_language != NULL)
	{
		count = strlen(shading_language) + 1;
	}
	
	if (count > 1)
	{
		gldata.glslversion = new char[count];
		strcpy(gldata.glslversion, (char*)glGetString(GL_SHADING_LANGUAGE_VERSION));
	}
	else
	{
		count++;
		gldata.glslversion = new char[count];
		strcpy(gldata.glslversion, "-");
	}

	count = strlen((char*)glGetString(GL_VENDOR)) + 1;
	gldata.implementor = new char[count];
	#if defined(COS_LINUX)
	// Use temp buffer storage
	glXQueryVersion(core.hDis, &maj, &min);
	count = sprintf(gldata.implementor, "%d.%d", maj, min) + 1;
	gldata.glxversion = new char[count];
	strcpy(gldata.glxversion, gldata.implementor);
	#else
	gldata.glxversion = new char[2];
	strcpy(gldata.glxversion, "-");
	#endif
	strcpy(gldata.implementor, (char*)glGetString(GL_VENDOR));
	count = strlen((char*)glGetString(GL_RENDERER)) + 1;
	gldata.renderer = new char[count];
	strcpy(gldata.renderer, (char*)glGetString(GL_RENDERER));
	// Extensions
	gldata.extlen = strlen((char*)glGetString(GL_EXTENSIONS)) + 1;
	if (gldata.extlen > 1)
	{
		char*extstr = new char[gldata.extlen];
		strcpy(extstr,(char*)glGetString(GL_EXTENSIONS));
		unsigned int loop;
		i = count = min = 0;
		gldata.extcnt = 0;
		while (i < gldata.extlen)
		{
			if (extstr[i] == ' ')
				gldata.extcnt++;
			i++;
		}
		gldata.extensions = new char*[gldata.extcnt];
		i = 0;
		// Get sizes and allocate array(s)
		while (i < gldata.extlen)
		{
			if (extstr[i] == ' ')
			{
				loop = i - min + 1;
				gldata.extensions[count] = new char[loop];
				min = ++i;
				count++;
			}
			i++;
		}
		count = min = i = 0;
		// Get all seperate extensions into seperate arrays
		while (i < gldata.extlen)
		{
			if (extstr[i] == ' ')
			{
				maj = 0;
				CLoopc(loop, min, i, 1)
				{
					gldata.extensions[count][maj] = extstr[loop];
					maj++;
				}
				gldata.extensions[count][maj] = '\0';
				min = ++i;
				count++;
			}
			i++;
		}
		delete[] extstr;
	}
	else
		gldata.extcnt = 0;
	// Preparing
	core.CSetCursor(APP_WIDTH / 2, APP_HEIGHT / 2);
	return true;
}

void Main()
{
	if (app.benchmode)
	{
		if (core.focus)
		{
			// Render
			while ((app.lefttime = core.CGetTime() - app.prevtime) < bench.frame)
				BenchRender((float)app.lefttime / bench.frame);
			app.prevtime = core.CGetTime() - (app.lefttime - bench.frame);
			// Process
			BenchProcess();
		}
		else // Pause
		{
			core.CSleep(bench.frame);
			core.CProcessMessage();
			app.prevtime = core.CGetTime() - app.lefttime; // Keep app.lefttime constant
		}
	}
	else
	{
		if (app.prevtime != 0)
			app.lefttime = APP_FRAME - (core.CGetTime() - app.prevtime);
		if (app.lefttime > 0) // Time left
		{
			core.CSleep(app.lefttime);
			app.prevtime = core.CGetTime();
		}
		else
		{
			app.prevtime = core.CGetTime();
			if (app.lefttime < 0) // Too slow
				app.prevtime += app.lefttime;
		}
		core.CProcessMessage();
		if (core.focus)
		{
			glClear(GL_COLOR_BUFFER_BIT);
            if (app.page == P_MENU)
                Menu();
            else if (app.page == P_DATA)
                Data();
            else if (app.page == P_ABOUT)
                About();
            else if (app.page == P_MARKS)
                Benchmarks();
			else if (app.page == P_RESULTS)
				Results();
            else
				Extensions();
			// Processing
			if (!app.benchmode)
			{
				MoveWndSys();
				core.CDrawCursor(app.cursor);
				core.CSwapBuffers();
			}
		}
	}
}

void Quit()
{
	// Memory
	int cnt;
	if (app.benchcnt > 0)
	{
		CLoops(cnt, 0, app.benchcnt)
		{
			delete[] app.benchnames[cnt];
		}
		delete[] app.benchnames;
	}
	// Engine
	core.CClearCursor(&app.cursor);
	CLoops(cnt, 0, APP_FONTS)
	{
		core.CClearFont(&app.font[cnt]);
	}
	core.CClearTexture(&app.mask[0], APP_MASKS);
	core.CClearTexture(&app.credit, 1);
	core.CClearSample(app.sample);
	if (!core.CCleanupEngine())
		core.CMsgBox(core.CGetLastError(), "CEngine error");
}

CMain
{
	// Init
	if (!Init())
		app.status = STATUS_FAIL;
	else
		app.status = STATUS_RUN;
		
	// Main
	while (app.status == STATUS_RUN)
		Main();

	// Quit
	Quit();
	return 0;
}
