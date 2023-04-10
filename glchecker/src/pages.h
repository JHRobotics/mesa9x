/*
Copyright (c) 2016, David Liu
All rights reserved. Read the license for more information.
*/

/* Pages */
inline void Menu()
{
	// Banner
	float col[3] = {0.3f, 0.7f, 1.0f}, w = app.font[FONT_DATA].width, h = app.font[FONT_DATA].height;
	app.font[FONT_DATA].width = 0.65f;
	app.font[FONT_DATA].height = 1.2f;
	core.CPrintText("OpenGL", app.font[FONT_DATA], 0.9f, 7.7f, col);
	col[0] = 0.9f;col[1] = 1.0f;col[2] = 0.2f;
	core.CPrintText("Checker", app.font[FONT_DATA], 4.8f, 7.7f, col);
	app.font[FONT_DATA].width = w;
	app.font[FONT_DATA].height = h;
	// Menu
	if (core.CPushbutton("Properties", app.font[FONT_PUSH], app.mask, app.sample, 1.0f, 5.8f) == CPUSH_CLICKED)
		app.page = P_DATA;
	if (core.CPushbutton("Extensions", app.font[FONT_PUSH], app.mask, app.sample, 1.0f, 4.4f) == CPUSH_CLICKED)
		app.page = P_GLEXT;
	if (core.CPushbutton("Benchmarks", app.font[FONT_PUSH], app.mask, app.sample, 1.0f, 3.0f) == CPUSH_CLICKED)
		app.page = P_MARKS;
	if (core.CPushbutton("About", app.font[FONT_PUSH], app.mask, app.sample, 1.0f, 1.6f) == CPUSH_CLICKED)
		app.page = P_ABOUT;
	if (core.CPushbutton("X", app.font[FONT_TEXT], NULL, app.sample, 0.0f, 9.35f, CGuicol_r) == CPUSH_CLICKED)
	{
		app.status = STATUS_QUIT;
		core.CSleep(200); // let sound finish
	}
	// GL symbol
	glDisable(GL_TEXTURE_2D);
	glPushMatrix();
	glTranslatef(7.6f, 4.2f, 0.0f);
	glRotatef(app.rot, 0.0f, 0.0f, 1.0f);
	glBegin(GL_TRIANGLES);
	glColor3f(0.0f, 1.0f, 0.0f);glVertex2d(1.0f, 1.0f);
	glColor3f(1.0f, 0.0f, 0.0f);glVertex2d(-1.44f, 0.0f);
	glColor3f(0.0f, 0.0f, 1.0f);glVertex2d(1.0f, -1.0f);
	glEnd();
	glPopMatrix();
	glEnable(GL_TEXTURE_2D);
	app.rot += 0.75f;
	if (app.rot > 360.0f)
		app.rot -= 360.0f;
}

inline void Data()
{
	if (core.CPushbutton("<", app.font[FONT_TEXT], NULL, app.sample, 0.0f, 9.35f, CGuicol_r) == CPUSH_CLICKED)
		app.page = P_MENU;
	core.CPrintText("GL version:", app.font[FONT_TEXT], 0.5f, 8.0f, CGuicol_y);
	core.CPrintText(gldata.glversion, app.font[FONT_TEXT], 0.5f, 7.25f);
	core.CPrintText("GLSL version:", app.font[FONT_TEXT], 0.5f, 6.5f, CGuicol_y);
	core.CPrintText(gldata.glslversion, app.font[FONT_TEXT], 0.5f, 5.75f);
	core.CPrintText("GLX version:", app.font[FONT_TEXT], 0.5f, 5.0f, CGuicol_y);
	core.CPrintText(gldata.glxversion, app.font[FONT_TEXT], 0.5f, 4.25f);
	core.CPrintText("Renderer:", app.font[FONT_TEXT], 0.5f, 3.5f, CGuicol_y);
	core.CPrintText(gldata.renderer, app.font[FONT_TEXT], 0.5f, 2.75f);
	core.CPrintText("Implementor:", app.font[FONT_TEXT], 0.5f, 2.0f, CGuicol_y);
	core.CPrintText(gldata.implementor, app.font[FONT_TEXT], 0.5f, 1.25f);
}

inline void About()
{
	if (core.CPushbutton("<", app.font[FONT_TEXT], NULL, app.sample, 0.0f, 9.35f, CGuicol_r) == CPUSH_CLICKED)
		app.page = P_MENU;
	core.CPrintText("OpenGLChecker version:", app.font[FONT_TEXT], 0.5f, 8.0f, CGuicol_y);
	core.CPrintText(GLCHECK_VERSION, app.font[FONT_TEXT], 5.55f, 8.0f, CGuicol_s);
	core.CPrintText("CEngine version:", app.font[FONT_TEXT], 0.5f, 7.4f,CGuicol_y);
	sprintf(app.temppath, "%hu.%hu.%hu", app.cmaj, app.cmin, app.crev < 0 ? 0 : app.crev);
	if (app.crev < 0)
		strcat(app.temppath, " (developmental)");
	core.CPrintText(app.temppath, app.font[FONT_TEXT], 5.55f, 7.4f, CGuicol_s);
	core.CPrintText("OpenGLChecker is an application designed\nto let the user find information about\nhis/her OpenGL implementation easily.\nOpenGLChecker runs on the CEngine, a\ncross platform OpenGL engine written in\nC/C++.\nOpenGLChecker and the CEngine are released\nunder the 4-clause BSD license and developed\nby David Liu - "
		, app.font[FONT_TEXT], 0.5f, 6.0f);
	glColor3f(0.6f, 0.6f, 0.6f);
	glBindTexture(GL_TEXTURE_2D, app.credit);
	glBegin(GL_QUADS);
	glTexCoord2i(0, 0);glVertex2f(3.5f, 2.3f);
	glTexCoord2i(1, 0);glVertex2f(4.75f, 2.3f);
	glTexCoord2i(1, 1);glVertex2f(4.75f, 2.9f);
	glTexCoord2i(0, 1);glVertex2f(3.5f, 2.9f);
	glEnd();
}

inline void Extensions()
{
	unsigned short i;
	double yv;
	if (core.CPushbutton("<", app.font[FONT_TEXT], NULL, app.sample, 0.0f, 9.35f, CGuicol_r) == CPUSH_CLICKED)
		app.page = P_MENU;
	if (gldata.extcnt > 0)
	{
		if (gldata.extcnt > 10)
			core.CScroll(CSCROLL_V, 0.1f, 0.5f, 7.8f, app.mask[MASK_BUTTON], &app.extscrl, 0.0f, (gldata.extcnt - 1) * 0.75f);
		// Lookup function
		if (app.extfind[0] == '\0' && !app.extclick)
		{
			strcpy(app.extfind, "Find...");
			app.clickmem = true;
		}
		core.CInputbar(1.8f, 8.9f, 30, app.extfind, app.font[FONT_TEXT], NULL, &app.extclick, &app.extpos, CINPUT_NOSPACE);
		if (app.extclick)
		{
			if (strcmp(app.extfind, "Find...") == 0 && app.clickmem)
			{
				app.extpos = 0;
				memset(app.extfind, 0, 7);
				app.clickmem = false;
			}
			bool* valid;
			valid = new bool[gldata.extcnt];
			CLoops(i, 0, gldata.extcnt)
			{
				valid[i] = true;
			}
			unsigned short i2;
			int sav;
			CLoops(i2, 0, 30)
			{
				// Check valid extensions and find first one
				if (app.extfind[i2] != '\0')
				{
					sav = -1;
					CLoops(i, 0, gldata.extcnt)
					{
						if (strlen(gldata.extensions[i]) + 1 > i2 && gldata.extensions[i][i2] == app.extfind[i2] && valid[i])
						{
							if (sav == -1)
							{
								sav = i;
								app.extscrl = i * 0.75f;
							}
						}
						else
							valid[i] = false;
					}
					// All extensions invalid?
					CLoops(i, 0, gldata.extcnt + 1)
					{
						if (i < gldata.extcnt && valid[i])
							break;
					}
					if (i == gldata.extcnt)
						break;
				}
				else
					break;
			}
			delete[] valid;
		}
		// Display
		CLoops(i, 0, gldata.extcnt)
		{
			yv = 8.0f + int(app.extscrl / 0.75f) * 0.75f - 0.75f * i;
			if (yv <= 8.0f && yv >= 1.25f)
				core.CPrintText(gldata.extensions[i], app.font[FONT_TEXT], 0.75f, yv);
		}
		core.CPrintText("Total supported extensions:", app.font[FONT_TEXT], 0.75f, 0.35f, CGuicol_y);
		core.CPrintValue(gldata.extcnt, app.font[FONT_TEXT], 0, 6.5f, 0.35f);
	}
	else
		core.CPrintText("No supported extensions.", app.font[FONT_TEXT], 0.5f, 8.0f);
}

inline void Benchmarks()
{
	unsigned char cnt, point, end;
	short start = -1;
	if (core.CPushbutton("<", app.font[FONT_TEXT], NULL, app.sample, 0.0f, 9.35f, CGuicol_r) == CPUSH_CLICKED)
		app.page = P_MENU;
	if (app.benchcnt > 8)
	{
		core.CScroll(CSCROLL_V, 0.1f, 1.2f, 7.1f, app.mask[MASK_BUTTON], &app.benchscrl, 0.0f, app.benchcnt - 8);
		end = (int)app.benchscrl + 8;
	}
	else
		end = app.benchcnt;
	CLoops(cnt, (int)app.benchscrl, end)
	{
		point = strlen(app.benchnames[cnt]) - 4;
		app.benchnames[cnt][point] = '\0';
		core.CPrintText(app.benchnames[cnt], app.font[FONT_TEXT], 0.8f, 8.0f - 1.0f * cnt + (int)app.benchscrl * 1.0f);
		if (core.CPushbutton("Start!", app.font[FONT_TEXT], app.mask, app.sample, 8.0f, 7.9f - 1.0f * cnt + (int)app.benchscrl * 1.0f) == CPUSH_CLICKED)
			start = cnt;
		app.benchnames[cnt][point] = '.';
	}
	if (app.benchcnt == 0)
           core.CPrintText("No benchmark files found.", app.font[FONT_TEXT], 0.5f, 8.0f);
	if (start > - 1)
		StartBenchmark(start);
}

inline void Results()
{
	const float *colbuf;
	core.CPrintText("Results", app.font[FONT_DATA], 1.0f, 8.5f);
	// avg fps
	core.CPrintText("Average frames per second:", app.font[FONT_TEXT], 1.0f, 7.5f, CGuicol_s);
	if (bench.avgfps < bench.badfps)
		colbuf = &CGuicol_r[0];
	else if (bench.avgfps < bench.goodfps)
		colbuf = &CGuicol_y[0];
	else
		colbuf = &CGuicol_g[0];
	core.CPrintValue(bench.avgfps, app.font[FONT_TEXT], 1, 7.5f, 7.5f, colbuf);
	// peak fps
	core.CPrintText("Peak frames per second:", app.font[FONT_TEXT], 1.0f, 6.5f, CGuicol_s);
	if (bench.fpspeak < bench.badfps)
		colbuf = &CGuicol_r[0];
	else if (bench.fpspeak < bench.goodfps)
		colbuf = &CGuicol_y[0];
	else
		colbuf = &CGuicol_g[0];
	core.CPrintValue(bench.fpspeak, app.font[FONT_TEXT], 1, 7.5f, 6.5f, colbuf);
	// ups
	core.CPrintText("Updates per second:", app.font[FONT_TEXT], 1.0f, 5.5f, CGuicol_s);
	core.CPrintValue(1000.0f / (float)bench.frame, app.font[FONT_TEXT], 1, 7.5f, 5.5f);
	if (core.CPushbutton("OK", app.font[FONT_PUSH], app.mask, app.sample, 1.0f, 1.0f) == CPUSH_CLICKED)
		app.page = P_MARKS;
	// Info
	core.CPrintText("Green", app.font[FONT_TEXT], 1.0f, 4.5f, CGuicol_g);
	core.CPrintText("is excellent performance.", app.font[FONT_TEXT], 2.2f, 4.5f, CGuicol_s);
	core.CPrintText("Yellow", app.font[FONT_TEXT], 1.0f, 4.0f, CGuicol_y);
	core.CPrintText("is average performance.", app.font[FONT_TEXT], 2.4f, 4.0f, CGuicol_s);
	core.CPrintText("Red", app.font[FONT_TEXT], 1.0f, 3.5f, CGuicol_r);
	core.CPrintText("is bad performance.", app.font[FONT_TEXT], 1.8f, 3.5f, CGuicol_s);
}