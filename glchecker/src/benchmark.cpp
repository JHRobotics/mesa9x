/*
Copyright (c) 2016, David Liu
All rights reserved. Read the license for more information.
*/

/* Headers */
#include "CEngine.h"
#include "glchecker.h"
#include "parser.h"
#include "benchmark.h"

/* Objects */
Bench bench;

/* Functions */
void CreateString(char** str, const char *data)
{
	*str = new char[strlen(data) + 1];
	strcpy(*str, data);
}

void SetupScripter()
{
	// Setup
	CreateString(&cmdfunction[CMD_checkversion].name, "CheckVersion");
	CreateString(&cmdfunction[CMD_checkversion].format, "s");
	CreateString(&cmdfunction[CMD_setattributes].name, "SetAttributes");
	CreateString(&cmdfunction[CMD_setattributes].format, "iffff");
	CreateString(&cmdfunction[CMD_loadobj].name, "LoadOBJ");
	CreateString(&cmdfunction[CMD_loadobj].format, "sii");
	CreateString(&cmdfunction[CMD_loadbmp].name, "LoadBMP");
	CreateString(&cmdfunction[CMD_loadbmp].format, "siiiii");
	CreateString(&cmdfunction[CMD_loadtga].name, "LoadTGA");
	CreateString(&cmdfunction[CMD_loadtga].format, "siiiii");
	CreateString(&cmdfunction[CMD_loadsound].name, "LoadSound");
	CreateString(&cmdfunction[CMD_loadsound].format, "sii");
	CreateString(&cmdfunction[CMD_createvariable].name, "CreateVariable");
	CreateString(&cmdfunction[CMD_createvariable].format, "sf");
	CreateString(&cmdfunction[CMD_message].name, "Message");
	CreateString(&cmdfunction[CMD_message].format, "s");
	// Render
	CreateString(&cmdfunction[CMD_rendermodel].name, "RenderModel");
	CreateString(&cmdfunction[CMD_rendermodel].format, "i");
	CreateString(&cmdfunction[CMD_pushmatrix].name, "PushMatrix");
	CreateString(&cmdfunction[CMD_pushmatrix].format, "");
	CreateString(&cmdfunction[CMD_popmatrix].name, "PopMatrix");
	CreateString(&cmdfunction[CMD_popmatrix].format, "");
	CreateString(&cmdfunction[CMD_rotate].name, "Rotate");
	CreateString(&cmdfunction[CMD_rotate].format, "ffff");
	CreateString(&cmdfunction[CMD_translate].name, "Translate");
	CreateString(&cmdfunction[CMD_translate].format, "fff");
	CreateString(&cmdfunction[CMD_scale].name, "Scale");
	CreateString(&cmdfunction[CMD_scale].format, "fff");
	CreateString(&cmdfunction[CMD_texture].name, "Texture");
	CreateString(&cmdfunction[CMD_texture].format, "i");
	CreateString(&cmdfunction[CMD_vertex].name, "Vertex");
	CreateString(&cmdfunction[CMD_vertex].format, "fff");
	CreateString(&cmdfunction[CMD_texcoord].name, "TexCoord");
	CreateString(&cmdfunction[CMD_texcoord].format, "ff");
	CreateString(&cmdfunction[CMD_vertexformat].name, "VertexFormat");
	CreateString(&cmdfunction[CMD_vertexformat].format, "s");
	CreateString(&cmdfunction[CMD_vertexend].name, "VertexEnd");
	CreateString(&cmdfunction[CMD_vertexend].format, "");
	CreateString(&cmdfunction[CMD_enable].name, "Enable");
	CreateString(&cmdfunction[CMD_enable].format, "s");
	CreateString(&cmdfunction[CMD_disable].name, "Disable");
	CreateString(&cmdfunction[CMD_disable].format, "s");
	CreateString(&cmdfunction[CMD_lightattribs].name, "LightAttribs");
	CreateString(&cmdfunction[CMD_lightattribs].format, "sffff");
	CreateString(&cmdfunction[CMD_color].name, "Color");
	CreateString(&cmdfunction[CMD_color].format, "ffff");
	CreateString(&cmdfunction[CMD_intervariable].name, "InterVariable");
	CreateString(&cmdfunction[CMD_intervariable].format, "fff");
	// Process
	CreateString(&cmdfunction[CMD_cursorpos].name, "CursorPos");
	CreateString(&cmdfunction[CMD_cursorpos].format, "ff");
	CreateString(&cmdfunction[CMD_event].name, "Event");
	CreateString(&cmdfunction[CMD_event].format, "ssf");
	CreateString(&cmdfunction[CMD_playsound].name, "PlaySound");
	CreateString(&cmdfunction[CMD_playsound].format, "ii");
	// Universal
	CreateString(&cmdfunction[CMD_procvariable].name, "ProcVariable");
	CreateString(&cmdfunction[CMD_procvariable].format, "ffs");
	CreateString(&cmdfunction[CMD_if].name, "If");
	CreateString(&cmdfunction[CMD_if].format, "fsfi");
	CreateString(&cmdfunction[CMD_jump].name, "Jump");
	CreateString(&cmdfunction[CMD_jump].format, "i");
	CreateString(&cmdfunction[CMD_movecursor].name, "MoveCursor");
	CreateString(&cmdfunction[CMD_movecursor].format, "ii");
}

void CleanBenchmark()
{
	unsigned int i;
	// Exit
	if (bench.lines != NULL)
	{
		CLoops(i, 0, bench.length)
		{
			delete[] bench.lines[i];
		}
		delete[] bench.lines;
	}
	if (bench.setupcmds != NULL)
		delete[] bench.setupcmds;
	if (bench.rendercmds != NULL)
		delete[] bench.rendercmds;
	if (bench.processcmds != NULL)
		delete[] bench.processcmds;
	if (bench.model != NULL)
	{
		CLoops(i, 0, bench.modelcnt)
		{
			core.CDeleteModel(&bench.model[i]);
		}
		delete[] bench.model;
	}
	if (bench.tex != NULL)
	{
		glDeleteTextures(bench.texcnt,bench.tex);
		delete[] bench.tex;
	}
	if (bench.sample != NULL)
	{
		CLoops(i, 0, bench.modelcnt)
		{
			core.CClearSample(bench.sample[i]);
			core.CClearSound(bench.sound[i]);
		}
		delete[] bench.sample;
		delete[] bench.sound;
	}
	if (bench.intsav != NULL)
		delete[] bench.intsav;
	if (bench.charsav != NULL)
	{
		if (bench.charalloc)
		{
			CLoops(i, 0, bench.charsavcnt - 1)
			{
				delete[] bench.charsav[i];
			}
		}
		delete[] bench.charsav;
	}
	if (bench.floatptr != NULL)
		delete[] bench.floatptr;
	if (bench.floatsav != NULL)
		delete[] bench.floatsav;
	if (vars != NULL)
		delete[] vars;
}

inline void SkipCmds(unsigned char *acmd, int l)
{
	int i;
	unsigned char j, cmd;
	if (l > 0)
	{
		for (i = 1; i <= l; i++)
		{
			cmd = *(acmd + i);
			CLoops(j, 0, strlen(cmdfunction[cmd].format))
			{
				if (cmdfunction[cmd].format[j] == 'f')
					bench.floatptrcnt++;
				else if (cmdfunction[cmd].format[j] == 's')
					bench.floatsavcnt++;
				else
					bench.intsavcnt++;
			}
		}
	}
	else
	{
		for (i = -1; i >= l; i--)
		{
			cmd = *(acmd + i);
			CLoops(j, 0, strlen(cmdfunction[cmd].format))
			{
				if (cmdfunction[cmd].format[j] == 'f')
					bench.floatptrcnt--;
				else if (cmdfunction[cmd].format[j] == 's')
					bench.floatsavcnt--;
				else
					bench.intsavcnt--;
			}
		}
	}
}

inline int ExecuteUniCmds(unsigned char cmd) // return new command position
{
	int i;
	if (cmd == CMD_procvariable)
	{
		if (strcmp(bench.charsav[bench.floatsavcnt], "+") == 0)
			(*bench.floatptr[bench.floatptrcnt]) = (*bench.floatptr[bench.floatptrcnt]) + (*bench.floatptr[bench.floatptrcnt + 1]);
		else if (strcmp(bench.charsav[bench.floatsavcnt], "-") == 0)
			(*bench.floatptr[bench.floatptrcnt]) = (*bench.floatptr[bench.floatptrcnt]) - (*bench.floatptr[bench.floatptrcnt + 1]);
		else if (strcmp(bench.charsav[bench.floatsavcnt], "*") == 0)
			(*bench.floatptr[bench.floatptrcnt]) = (*bench.floatptr[bench.floatptrcnt]) * (*bench.floatptr[bench.floatptrcnt + 1]);
		else if (strcmp(bench.charsav[bench.floatsavcnt], "/") == 0)
			(*bench.floatptr[bench.floatptrcnt]) = (*bench.floatptr[bench.floatptrcnt]) / (*bench.floatptr[bench.floatptrcnt + 1]);
		else if (strcmp(bench.charsav[bench.floatsavcnt], "=") == 0)
			(*bench.floatptr[bench.floatptrcnt]) = (*bench.floatptr[bench.floatptrcnt + 1]);
		else if (strcmp(bench.charsav[bench.floatsavcnt], "sin") == 0)
			(*bench.floatptr[bench.floatptrcnt]) = sin((*bench.floatptr[bench.floatptrcnt + 1]) / 180.0f * CPI);
		else if (strcmp(bench.charsav[bench.floatsavcnt], "cos") == 0)
			(*bench.floatptr[bench.floatptrcnt]) = cos((*bench.floatptr[bench.floatptrcnt + 1]) / 180.0f * CPI);
		bench.floatptrcnt += 2;
		bench.floatsavcnt++;
		return 1;
	}
	else if (cmd == CMD_if)
	{
		bool result = false;
		if ((strcmp(bench.charsav[bench.floatsavcnt], "==") == 0 && (*bench.floatptr[bench.floatptrcnt]) == (*bench.floatptr[bench.floatptrcnt + 1])) || 
			(strcmp(bench.charsav[bench.floatsavcnt], "!=") == 0 && (*bench.floatptr[bench.floatptrcnt]) != (*bench.floatptr[bench.floatptrcnt + 1])) || 
			(strcmp(bench.charsav[bench.floatsavcnt], ">") == 0 && (*bench.floatptr[bench.floatptrcnt]) > (*bench.floatptr[bench.floatptrcnt + 1])) || 
			(strcmp(bench.charsav[bench.floatsavcnt], ">=") == 0 && (*bench.floatptr[bench.floatptrcnt]) >= (*bench.floatptr[bench.floatptrcnt + 1])) || 
			(strcmp(bench.charsav[bench.floatsavcnt], "<") == 0 && (*bench.floatptr[bench.floatptrcnt]) < (*bench.floatptr[bench.floatptrcnt + 1])) || 
			(strcmp(bench.charsav[bench.floatsavcnt], "<=") == 0 && (*bench.floatptr[bench.floatptrcnt]) <= (*bench.floatptr[bench.floatptrcnt + 1])))
			result = true;
		bench.floatptrcnt += 2;
		bench.floatsavcnt++;
		if (result)
		{
			bench.intsavcnt++;
			return 1;
		}
		else
			return bench.intsav[bench.intsavcnt++] + 1;
	}
	else if (cmd == CMD_jump)
	{
		i = bench.intsav[bench.intsavcnt];
		if (i >= 0)
			bench.intsavcnt++;
		return i;
	}
	else if (cmd == CMD_movecursor)
	{
		core.CSetCursor(bench.intsav[bench.intsavcnt], bench.intsav[bench.intsavcnt + 1]);
		bench.intsavcnt += 2;
		return 1;
	}
	else if (cmd == CMD_cursorpos)
	{
		(*bench.floatptr[bench.floatptrcnt]) = (float)core.mx;
		(*bench.floatptr[bench.floatptrcnt + 1]) = (float)core.my;
		bench.floatptrcnt += 2;
		return 1;
	}
	else if (cmd == CMD_playsound)
	{
		if ((bool)bench.intsav[bench.intsavcnt + 1])
			core.CStartSound(bench.sound[bench.intsav[bench.intsavcnt]]);
		else
			core.CStopSound(bench.sound[bench.intsav[bench.intsavcnt]]);
		bench.intsavcnt += 2;
		return 1;
	}
	return 0;
}

void StartBenchmark(unsigned char cnt)
{
	// Initialize
	UpdateLoading("Initializing...");
	app.benchmode = true;
	bench.fps  =  bench.avgfps  =  bench.fpspeak  =  0.0f;
	bench.cntfps = 0;
	bench.fpsproc = 1;
	bench.msgbuffer[0] = bench.msgpath[0] = '\0';
	// Default values
	bench.frame = 40; // 25 UPS
	bench.badfps = 200.0f;
	bench.goodfps = 400.0f;
	bench.fov = 75.0;
	bench.zfar = 1000.0;
	// Parse benchmark script
	UpdateLoading("Parsing script...");
	if (!ParseScript(app.benchnames[cnt]))
		app.benchmode = false;
	// Execute setup script
	if (app.benchmode && strcmp(bench.charsav[0], SCRIPT_VERSION) != 0) // CMD_checkversion
	{
		strcpy(bench.msgbuffer, "Script version ");
		strcat(bench.msgbuffer, SCRIPT_VERSION);
		strcat(bench.msgbuffer, " is required.");
		app.benchmode = false;
	}
	if (app.benchmode)
	{
		bench.floatsavcnt = 1;
		bench.floatptrcnt = bench.intsavcnt = 0; // set global code counters, floatsav substitutes charsav, charsav is saved for memory cleaning
		unsigned int i;
		int j;
		unsigned char c = 1;
		if (bench.modelcnt > 0)
			bench.model = new CModel[bench.modelcnt];
		if (bench.texcnt > 0)
			bench.tex = new CTexture[bench.texcnt];
		if (bench.samplecnt > 0)
		{
			bench.sample = new CSample[bench.samplecnt];
			bench.sound = new CSound[bench.samplecnt]; // One channel per sample
		}
		app.prevtime = 0;
		CLoops(i, 1, bench.setupcmdcnt)
		{
			if (core.CGetTime() - app.prevtime > SETUP_ALIVE || app.prevtime == 0) // Check timing
			{
				strcpy(bench.msgpath, "Executing setup commands");
				if (c == 1)
					strcat(bench.msgpath, "...");
				else if (c == 2)
					strcat(bench.msgpath, ".");
				else
				{
					strcat(bench.msgpath, "..");
					c = 0;
				}
				UpdateLoading(bench.msgpath);
				c++;
				app.prevtime = core.CGetTime();
			}
			while (i < bench.setupcmdcnt && (j = ExecuteUniCmds(bench.setupcmds[i])) != 0)
			{
				if (j < 0)
					SkipCmds(&bench.setupcmds[i], j);
				else if (j > 1)
					SkipCmds(&bench.setupcmds[i], j - 1);
				i += j;
			}
			if (i == bench.setupcmdcnt)
				break;
			if (bench.setupcmds[i] == CMD_setattributes)
			{
				bench.frame = bench.intsav[bench.intsavcnt];
				bench.badfps = (*bench.floatptr[bench.floatptrcnt]);
				bench.goodfps = (*bench.floatptr[bench.floatptrcnt + 1]);
				bench.fov = (*bench.floatptr[bench.floatptrcnt + 2]);
				bench.zfar = (*bench.floatptr[bench.floatptrcnt + 3]);
				bench.floatptrcnt += 4;
				bench.intsavcnt++;
			}
			else if (bench.setupcmds[i] == CMD_loadobj)
			{
				if (!core.CLoadOBJ(RelativePath(bench.charsav[bench.floatsavcnt], NULL), bench.model[bench.intsav[bench.intsavcnt]]))
				{
					strcpy(bench.msgpath, bench.charsav[bench.floatsavcnt]);
					app.benchmode = false;
					break;
				}
				if (bench.intsav[bench.intsavcnt + 1] == -1)
					bench.model[bench.intsav[bench.intsavcnt]].texture = 0;
				else
					bench.model[bench.intsav[bench.intsavcnt]].texture = bench.tex[bench.intsav[bench.intsavcnt + 1]];
				bench.intsavcnt += 2;
				bench.floatsavcnt++;
			}
			else if (bench.setupcmds[i] == CMD_loadbmp)
			{
				if (!core.CLoadBMP(RelativePath(bench.charsav[bench.floatsavcnt], NULL), bench.tex[bench.intsav[bench.intsavcnt]]))
				{
					strcpy(bench.msgpath, bench.charsav[bench.floatsavcnt]);
					app.benchmode = false;
					break;
				}
				char props = 0;
				if ((bool)bench.intsav[bench.intsavcnt + 1])
					props += CTEX_ANIFILTER;
				if ((bool)bench.intsav[bench.intsavcnt + 2])
					props += CTEX_MIPMAP;
				if ((bool)bench.intsav[bench.intsavcnt + 3])
					props += CTEX_SPRITE;
				if ((bool)bench.intsav[bench.intsavcnt + 4])
					props += CTEX_WRAPREPEAT;
				core.CSetTexture(bench.tex[bench.intsav[bench.intsavcnt]], props);
				bench.intsavcnt += 5;
				bench.floatsavcnt++;
			}
			else if (bench.setupcmds[i] == CMD_loadtga)
			{
				if (!core.CLoadTGA(RelativePath(bench.charsav[bench.floatsavcnt], NULL), bench.tex[bench.intsav[bench.intsavcnt]]))
				{
					strcpy(bench.msgpath, bench.charsav[bench.floatsavcnt]);
					app.benchmode = false;
					break;
				}
				char props = 0;
				if ((bool)bench.intsav[bench.intsavcnt + 1])
					props += CTEX_ANIFILTER;
				if ((bool)bench.intsav[bench.intsavcnt + 2])
					props += CTEX_MIPMAP;
				if ((bool)bench.intsav[bench.intsavcnt + 3])
					props += CTEX_SPRITE;
				if ((bool)bench.intsav[bench.intsavcnt + 4])
					props += CTEX_WRAPREPEAT;
				core.CSetTexture(bench.tex[bench.intsav[bench.intsavcnt]], props);
				bench.intsavcnt += 5;
				bench.floatsavcnt++;
			}
			else if (bench.setupcmds[i] == CMD_loadsound)
			{
				if (!core.CLoadSample(RelativePath(bench.charsav[bench.floatsavcnt], NULL), bench.sample[bench.intsav[bench.intsavcnt]], 1))
				{
					strcpy(bench.msgpath, bench.charsav[bench.floatsavcnt]);
					app.benchmode = false;
					break;
				}
				core.CCreateSound(bench.sample[bench.intsav[bench.intsavcnt]], bench.sound[bench.intsav[bench.intsavcnt]], (bool)bench.intsav[bench.intsavcnt + 1]);
				bench.intsavcnt += 2;
				bench.floatsavcnt++;
			}
			else if (bench.setupcmds[i] == CMD_createvariable)
			{
				// Already executed in ReadFunction
				bench.floatsavcnt++;
				bench.floatptrcnt++;
			}
			else if (bench.setupcmds[i] == CMD_message)
			{
				char msg[SCRIPT_PATH];
				strcpy(msg, bench.charsav[bench.floatsavcnt]);
				ReportError(msg, bench.msgpath, true);
				bench.floatsavcnt++;
			}
		}
	}
	// Error message
	if (!app.benchmode)
	{
		if (bench.msgbuffer[0] == '\0')
			ReportError(core.CGetLastError(), bench.msgpath);
		else
			ReportError(bench.msgbuffer, bench.msgpath);
		app.page = P_MARKS;
		CleanBenchmark();
		return;
	}
	// Prepare benchmark
	app.savxpos = core.xpos;
	app.savypos = core.ypos;
	app.lefttime = 0;
	core.CMoveWindow(0, 0); // fullscreen
	core.CResize3D(core.width, core.height, bench.fov, bench.zfar);
	glEnable(GL_DEPTH_TEST);
	glEnable(GL_CULL_FACE);
	app.prevtime = bench.savtime = core.CGetTime();
}

void QuitBenchmark()
{
	// Ensure default CEngine OpenGL states
	if (glIsEnabled(GL_DEPTH_TEST))
		glDisable(GL_DEPTH_TEST);
	if (glIsEnabled(GL_CULL_FACE))
		glDisable(GL_CULL_FACE);
	if (!glIsEnabled(GL_TEXTURE_2D))
		glEnable(GL_TEXTURE_2D);
	if (!glIsEnabled(GL_BLEND))
		glEnable(GL_BLEND);
	if (glIsEnabled(GL_LIGHTING))
	{
		glDisable(GL_LIGHTING);
		glDisable(GL_LIGHT0);
	}
	core.CMoveWindow(app.savxpos, app.savypos, APP_WIDTH, APP_HEIGHT);
	core.CResize2D(APP_WIDTH, APP_HEIGHT, 10.0f, 10.0f);
	// Loading frame
	UpdateLoading("Loading...");
	// Clean
	CleanBenchmark();
	// Return
	app.benchmode = false;
	app.page = P_RESULTS;
	app.prevtime = app.lefttime = 0;
}

inline void CalcFPS()
{
	float passed = core.CGetTime() - bench.savtime;
	if (passed > FPS_PERIOD)
	{
		if (bench.cntfps > 0) // actually rendered
			bench.fps = (1000.0f / passed) * (float)bench.cntfps;
		else
			bench.fps = 0.0f;
		if (bench.fps > bench.fpspeak)
			bench.fpspeak = bench.fps;
		bench.avgfps = (bench.avgfps * (bench.fpsproc - 1) + bench.fps) / bench.fpsproc;
		bench.fpsproc++;
		bench.savtime = core.CGetTime();
		bench.cntfps = 0;
	}
}

inline void ShowFPS()
{
	int i = 0;
	glDisable(GL_DEPTH_TEST);
	if (!glIsEnabled(GL_TEXTURE_2D))
	{
		glEnable(GL_TEXTURE_2D);
		i++;
	}
	if (!glIsEnabled(GL_BLEND))
	{
		glEnable(GL_BLEND);
		i += 3;
	}
	if (glIsEnabled(GL_LIGHTING))
	{
		glDisable(GL_LIGHTING);
		i += 5;
	}
	core.CResize2D(core.width, core.height, 20.0f, 20.0f);
	core.CPrintText("FPS:", app.font[FONT_TEXT], 1.0f, 1.7f, CGuicol_s);
	core.CPrintValue(bench.fps, app.font[FONT_TEXT], 0, 2.0f, 1.7f);
	core.CPrintText("Press esc to exit.", app.font[FONT_TEXT], 1.0f, 1.0f, CGuicol_s);
	core.CResize3D(core.width, core.height, bench.fov, bench.zfar);
	if (i == 1 || i == 4 || i == 6 || i == 9)
		glDisable(GL_TEXTURE_2D);
	if (i == 3 || i == 4 || i >= 8)
		glDisable(GL_BLEND);
	if (i == 5 || i == 6 || i >= 8)
		glEnable(GL_LIGHTING);
	glEnable(GL_DEPTH_TEST);
}

/*
Identity matrix:
camera looking into -z axes.
x axes if left/right.
y axes is up/down.
*/

void BenchRender(float interpolate)
{
	unsigned int i;
	int j;
	// Reset command counters
	bench.intsavcnt = bench.sintcnt;
	bench.floatptrcnt = bench.sfloatcnt;
	bench.floatsavcnt = bench.scharcnt;
	// Standard frame code
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	glLoadIdentity();
	glPushMatrix();
	// Execute main commands
	CLoops(i, 0, bench.rendercmdcnt)
	{
		while (i < bench.rendercmdcnt && (j = ExecuteUniCmds(bench.rendercmds[i])) != 0)
		{
			if (j < 0)
				SkipCmds(&bench.rendercmds[i], j);
			else if (j > 1)
				SkipCmds(&bench.rendercmds[i], j - 1);
			i += j;
		}
		if (i == bench.rendercmdcnt)
			break;
		if (bench.rendercmds[i] == CMD_rendermodel)
		{
			core.CRender3D(bench.model[bench.intsav[bench.intsavcnt]]);
			bench.intsavcnt++;
		}
		else if (bench.rendercmds[i] == CMD_pushmatrix)
			glPushMatrix();
		else if (bench.rendercmds[i] == CMD_popmatrix)
			glPopMatrix();
		else if (bench.rendercmds[i] == CMD_rotate)
		{
			glRotatef(*bench.floatptr[bench.floatptrcnt], *bench.floatptr[bench.floatptrcnt + 1], *bench.floatptr[bench.floatptrcnt + 2], *bench.floatptr[bench.floatptrcnt + 3]);
			bench.floatptrcnt += 4;
		}
		else if (bench.rendercmds[i] == CMD_translate)
		{
			glTranslatef(*bench.floatptr[bench.floatptrcnt], *bench.floatptr[bench.floatptrcnt + 1], *bench.floatptr[bench.floatptrcnt + 2]);
			bench.floatptrcnt += 3;
		}
		else if (bench.rendercmds[i] == CMD_scale)
		{
			glScalef(*bench.floatptr[bench.floatptrcnt], *bench.floatptr[bench.floatptrcnt + 1], *bench.floatptr[bench.floatptrcnt + 2]);
			bench.floatptrcnt += 3;
		}
		else if (bench.rendercmds[i] == CMD_texture)
		{
			glBindTexture(GL_TEXTURE_2D, bench.tex[bench.intsav[bench.intsavcnt]]);
			bench.intsavcnt++;
		}
		else if (bench.rendercmds[i] == CMD_vertex)
		{
			glVertex3f(*bench.floatptr[bench.floatptrcnt], *bench.floatptr[bench.floatptrcnt + 1], *bench.floatptr[bench.floatptrcnt + 2]);
			bench.floatptrcnt += 3;
		}
		else if (bench.rendercmds[i] == CMD_texcoord)
		{
			glTexCoord2f(*bench.floatptr[bench.floatptrcnt], *bench.floatptr[bench.floatptrcnt + 1]);
			bench.floatptrcnt += 2;
		}
		else if (bench.rendercmds[i] == CMD_vertexformat)
		{
			if (strcmp(bench.charsav[bench.floatsavcnt], "quads") == 0)
				glBegin(GL_QUADS);
			else if (strcmp(bench.charsav[bench.floatsavcnt], "triangles") == 0)
				glBegin(GL_TRIANGLES);
			else if (strcmp(bench.charsav[bench.floatsavcnt], "tri_strip") == 0)
				glBegin(GL_TRIANGLE_STRIP);
			else if (strcmp(bench.charsav[bench.floatsavcnt], "tri_fans") == 0)
				glBegin(GL_TRIANGLE_FAN);
			else if (strcmp(bench.charsav[bench.floatsavcnt], "polygons") == 0)
				glBegin(GL_POLYGON);
			bench.floatsavcnt++;
		}
		else if (bench.rendercmds[i] == CMD_vertexend)
			glEnd();
		else if (bench.rendercmds[i] == CMD_enable)
		{
			if (strcmp(bench.charsav[bench.floatsavcnt], "blend") == 0)
				glEnable(GL_BLEND);
			else if (strcmp(bench.charsav[bench.floatsavcnt], "depth") == 0)
				glEnable(GL_DEPTH_TEST);
			else if (strcmp(bench.charsav[bench.floatsavcnt], "texture") == 0)
				glEnable(GL_TEXTURE_2D);
			else if (strcmp(bench.charsav[bench.floatsavcnt], "cullface") == 0)
				glEnable(GL_CULL_FACE);
			else if (strcmp(bench.charsav[bench.floatsavcnt], "light") == 0)
			{
				glEnable(GL_LIGHTING);
				glEnable(GL_LIGHT0);
			}
			bench.floatsavcnt++;
		}
		else if (bench.rendercmds[i] == CMD_disable)
		{
			if (strcmp(bench.charsav[bench.floatsavcnt], "blend") == 0)
				glDisable(GL_BLEND);
			else if (strcmp(bench.charsav[bench.floatsavcnt], "depth") == 0)
				glDisable(GL_DEPTH_TEST);
			else if (strcmp(bench.charsav[bench.floatsavcnt], "texture") == 0)
				glDisable(GL_TEXTURE_2D);
			else if (strcmp(bench.charsav[bench.floatsavcnt], "cullface") == 0)
				glDisable(GL_CULL_FACE);
			else if (strcmp(bench.charsav[bench.floatsavcnt], "light") == 0)
			{
				glDisable(GL_LIGHTING);
				glDisable(GL_LIGHT0);
			}
			bench.floatsavcnt++;
		}
		else if (bench.rendercmds[i] == CMD_lightattribs)
		{
			if (strcmp(bench.charsav[bench.floatsavcnt], "position") == 0)
			{
				float pos[4] = {*bench.floatptr[bench.floatptrcnt], *bench.floatptr[bench.floatptrcnt + 1], *bench.floatptr[bench.floatptrcnt + 2], *bench.floatptr[bench.floatptrcnt + 3]};
				glLightfv(GL_LIGHT0, GL_POSITION, pos);
			}
			else if (strcmp(bench.charsav[bench.floatsavcnt], "diffuse") == 0)
			{
				float dif[4] = {*bench.floatptr[bench.floatptrcnt], *bench.floatptr[bench.floatptrcnt + 1], *bench.floatptr[bench.floatptrcnt + 2], *bench.floatptr[bench.floatptrcnt + 3]};
				glLightfv(GL_LIGHT0, GL_DIFFUSE, dif);
			}
			else if (strcmp(bench.charsav[bench.floatsavcnt], "specular") == 0)
			{
				float spec[4] = {*bench.floatptr[bench.floatptrcnt], *bench.floatptr[bench.floatptrcnt + 1], *bench.floatptr[bench.floatptrcnt + 2], *bench.floatptr[bench.floatptrcnt + 3]};
				glLightfv(GL_LIGHT0, GL_SPECULAR, spec);
			}
			else if (strcmp(bench.charsav[bench.floatsavcnt], "ambient") == 0)
			{
				float amb[4] = {*bench.floatptr[bench.floatptrcnt], *bench.floatptr[bench.floatptrcnt + 1], *bench.floatptr[bench.floatptrcnt + 2], *bench.floatptr[bench.floatptrcnt + 3]};
				glLightfv(GL_LIGHT0, GL_AMBIENT, amb);
			}
			bench.floatptrcnt += 4;
			bench.floatsavcnt++;
		}
		else if (bench.rendercmds[i] == CMD_color)
		{
			glColor4f(*bench.floatptr[bench.floatptrcnt], *bench.floatptr[bench.floatptrcnt + 1], *bench.floatptr[bench.floatptrcnt + 2], *bench.floatptr[bench.floatptrcnt + 3]);
			bench.floatptrcnt += 4;
		}
		else if (bench.rendercmds[i] == CMD_intervariable)
		{
			(*bench.floatptr[bench.floatptrcnt]) = (*bench.floatptr[bench.floatptrcnt + 1]) + (*bench.floatptr[bench.floatptrcnt + 2]) * interpolate;
			bench.floatptrcnt += 3;
		}
	}
	glPopMatrix();
	// FPS
	bench.cntfps++;
	ShowFPS();
	// Render
	core.CSwapBuffers();
}

void BenchProcess()
{
	core.CProcessMessage();
	// Storage
	unsigned int i;
	int j ;
	unsigned char loop;
	// Reset command counters
	bench.intsavcnt = bench.sintcnt2;
	bench.floatptrcnt = bench.sfloatcnt2;
	bench.floatsavcnt = bench.scharcnt2;
	// Execute process commands
	CLoops(i, 0, bench.processcmdcnt)
	{
		while (i < bench.processcmdcnt && (j = ExecuteUniCmds(bench.processcmds[i])) != 0)
		{
			if (j < 0)
				SkipCmds(&bench.processcmds[i], j);
			else if (j > 1)
				SkipCmds(&bench.processcmds[i], j - 1);
			i += j;
		}
		if (i == bench.processcmdcnt)
			break;
		if (bench.processcmds[i] == CMD_event)
		{
			if (strcmp(bench.charsav[bench.floatsavcnt], "keypress") == 0)
			{
				CLoops(loop, 0, 256)
				{
					if (core.CKeyToChar(loop, false) == bench.charsav[bench.floatsavcnt + 1][0])
					{
						(*bench.floatptr[bench.floatptrcnt]) = (float)core.key[loop];
						break;
					}
				}
			}
			else if (strcmp(bench.charsav[bench.floatsavcnt], "mouseclick") == 0)
			{
				if (strcmp(bench.charsav[bench.floatsavcnt + 1], "right") == 0)
					(*bench.floatptr[bench.floatptrcnt]) = (float)core.key[CKEY_RMOUSE];
				else if (strcmp(bench.charsav[bench.floatsavcnt + 1], "left") == 0)
					(*bench.floatptr[bench.floatptrcnt]) = (float)core.key[CKEY_LMOUSE];
			}
			bench.floatptrcnt++;
			bench.floatsavcnt += 2;
		}
	}
	CalcFPS();
	if (core.key[CKEY_ESC])
		QuitBenchmark();
}
