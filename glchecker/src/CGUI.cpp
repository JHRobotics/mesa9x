/*
Copyright (c) 2016, David Liu
All rights reserved. Read the license for more information.
*/

#include "CEngine.h"

/* Functions */
bool CEngine::CCreateFont(const char *font, float fwidth, float fheight, CFont &store)
{
	store.width = fwidth;
	store.height = fheight;
	// Load texture
	if (!CLoadTGA(font, store.font))
	{
		memmove(inerr + 13, inerr, strlen(inerr) + 1);
		strncpy(inerr, "CCreateFont: ", 13);
		return false;
	}
	CSetTexture(store.font, 0);
	return true;
}

void CEngine::CClearFont(CFont *store)
{
	glDeleteTextures(1, &store->font);
}

bool CEngine::CCreateCursor(CCursor &cursor, const char *path, float cwidth, float cheight, float cox, float coy)
{
	if (!CLoadTGA(path, cursor.tex))
		return false;
	CSetTexture(cursor.tex, 0);
	cursor.size[0] = cwidth;
	cursor.size[1] = cheight;
	cursor.origin[0] = cox;
	cursor.origin[1] = coy;
	return true;
}

void CEngine::CDrawCursor(CCursor &cursor)
{
	float x = cmx - cursor.origin[0], x2 = x + cursor.size[0];
	float y = cmy + cursor.origin[1], y2 = y - cursor.size[1];
	glColor3f(1.0f, 1.0f, 1.0f);
	guimodel.texture = cursor.tex;
	guimodel.vertex[0] = x;guimodel.vertex[1] = y2;
	guimodel.vertex[2] = x2;guimodel.vertex[3] = y2;
	guimodel.vertex[4] = x;guimodel.vertex[5] = y;
	guimodel.vertex[6] = x2;guimodel.vertex[7] = y;
	CRender2D(guimodel);
}

void CEngine::CClearCursor(CCursor* cursor)
{
	glDeleteTextures(1, &cursor->tex);
}

void CEngine::CPrintText(const char *text, CFont font, float xpos, float ypos, const float color[3])
{
	size_t loop;
	float temp1, temp2, *p, tex[8], ox = xpos;
	unsigned char code;
	p = guimodel.texcoord;
	guimodel.texcoord = tex;
	guimodel.texture = font.font;
	glColor3fv(color);
	CLoops(loop, 0, strlen(text))
	{
		if (loop != 0)
		{
			if (text[loop] == '\n')
			{
				xpos = ox;
				ypos -= font.height;
				if (loop == strlen(text) - 1)
					break;
				loop++;
			}
			else
				xpos += font.width;
		}
		code = text[loop] - 32;
		temp1 = float(code % 16) / 16.0f;
		temp2 = float(code / 16) / 8.0f;
		tex[0] = temp1;tex[1] = 1.0f - temp2 - 0.12f;
		tex[2] = temp1 + 0.0625f;tex[3] = 1.0f - temp2 - 0.12f;
		tex[4] = temp1;tex[5] = 1.0f - temp2 - 0.0075f;
		tex[6] = temp1 + 0.0625f;tex[7] = 1.0f - temp2 - 0.0075f;
		guimodel.vertex[0] = xpos;guimodel.vertex[1] = ypos;
		guimodel.vertex[2] = xpos + font.width;guimodel.vertex[3] = ypos;
		guimodel.vertex[4] = xpos;guimodel.vertex[5] = ypos + font.height;
		guimodel.vertex[6] = xpos + font.width;guimodel.vertex[7] = ypos + font.height;
		CRender2D(guimodel);
	}
	guimodel.texcoord = p;
}

void CEngine::CPrintValue(const float value, CFont font, unsigned char decimals, float xpos, float ypos, const float color[3])
{
	char de[2];
	char text[12], temp[6];
	sprintf(de, "%d", decimals);
	strcpy(temp, "%1.");strcat(temp, de);strcat(temp, "f");
	sprintf(text, temp, value);
	CPrintText(text, font, xpos, ypos, color);
}

unsigned char CEngine::CPushbutton(const char *text, CFont font, CTexture masks[3], CSample sample, float x, float y, const float color[3], const float tcolor[3])
{
	// Variables
	unsigned char result;
	float pwidth = strlen(text) * font.width + 0.2f;
	float pheight = font.height + 0.2f;
	// Pusbutton
	if (cmx >= x && 
		cmx <= x + pwidth && 
		cmy >= y  && 
		cmy <= y + pheight)
	{
		result = CPUSH_TOUCHED;
		if (key[CKEY_LMOUSE] && !prevclick)
		{
			result = CPUSH_CLICKED;
			prevclick = true;
			if (sample != 0)
			{
				CSound sound;
				CCreateSound(sample, sound);
				CStartSound(sound);
			}
		}
		glColor3f(0.7f, 0.7f, 0.7f);
	}
	else
	{
		glColor3fv(color);
		result = CPUSH_IDLE;
	}
	if (masks != NULL)
	{
		float a = x + 0.1f, b = x + pwidth, c = x + pwidth - 0.1f;
		guimodel.texture = masks[0];
		guimodel.vertex[0] = x;guimodel.vertex[1] = y;
		guimodel.vertex[2] = a;guimodel.vertex[3] = y;
		guimodel.vertex[4] = x;guimodel.vertex[5] = y + pheight;
		guimodel.vertex[6] = a;guimodel.vertex[7] = y + pheight;
		CRender2D(guimodel);
		guimodel.texture = masks[1];
		guimodel.vertex[0] = a;guimodel.vertex[1] = y;
		guimodel.vertex[2] = c;guimodel.vertex[3] = y;
		guimodel.vertex[4] = a;guimodel.vertex[5] = y + pheight;
		guimodel.vertex[6] = c;guimodel.vertex[7] = y + pheight;
		CRender2D(guimodel);
		guimodel.texture = masks[2];
		guimodel.vertex[0] = c;guimodel.vertex[1] = y;
		guimodel.vertex[2] = b;guimodel.vertex[3] = y;
		guimodel.vertex[4] = c;guimodel.vertex[5] = y + pheight;
		guimodel.vertex[6] = b;guimodel.vertex[7] = y + pheight;
		CRender2D(guimodel);
	}
	else
	{
		guimodel.texture = 0;
		float *p = guimodel.texcoord;
		guimodel.texcoord = NULL;
		guimodel.vertex[0] = x;guimodel.vertex[1] = y;
		guimodel.vertex[2] = x + pwidth;guimodel.vertex[3] = y;
		guimodel.vertex[4] = x;guimodel.vertex[5] = y + pheight;
		guimodel.vertex[6] = x + pwidth;guimodel.vertex[7] = y + pheight;
		CRender2D(guimodel);
		guimodel.texcoord = p;
	}
	CPrintText(text, font, x + 0.1, y + 0.1, tcolor);
	return result;
}

void CEngine::CInputbar(float x, float y, unsigned short maxchar, char *textstr, CFont font, CTexture masks[3], bool* clicked, unsigned short* pos, unsigned char mode, const float color[3], const float tcolor[3])
{
	// Variables
	float pwidth = maxchar * font.width + 0.2f, pheight = font.height + 0.2f;
	// Inputbar
	if (cmx >= x && cmx <= x + pwidth && cmy >= y && cmy <= y + pheight)
	{
		if (key[CKEY_LMOUSE] && !prevclick)
			*clicked = prevclick = true;
	}
	else if (*clicked)
		*clicked = false;
	if (*clicked)
		glColor3f(0.8f, 0.8f, 0.8f);
	else
		glColor3fv(color);
	if (masks != NULL)
	{
		float a = x + 0.1f, b = x + pwidth, c = x + pwidth - 0.1f;
		guimodel.texture = masks[0];
		guimodel.vertex[0] = x;guimodel.vertex[1] = y;
		guimodel.vertex[2] = a;guimodel.vertex[3] = y;
		guimodel.vertex[4] = x;guimodel.vertex[5] = y + pheight;
		guimodel.vertex[6] = a;guimodel.vertex[7] = y + pheight;
		CRender2D(guimodel);
		guimodel.texture = masks[1];
		guimodel.vertex[0] = a;guimodel.vertex[1] = y;
		guimodel.vertex[2] = c;guimodel.vertex[3] = y;
		guimodel.vertex[4] = a;guimodel.vertex[5] = y + pheight;
		guimodel.vertex[6] = c;guimodel.vertex[7] = y + pheight;
		CRender2D(guimodel);
		guimodel.texture = masks[2];
		guimodel.vertex[0] = c;guimodel.vertex[1] = y;
		guimodel.vertex[2] = b;guimodel.vertex[3] = y;
		guimodel.vertex[4] = c;guimodel.vertex[5] = y + pheight;
		guimodel.vertex[6] = b;guimodel.vertex[7] = y + pheight;
		CRender2D(guimodel);
	}
	else
	{
		guimodel.texture = 0;
		float *p = guimodel.texcoord;
		guimodel.texcoord = NULL;
		guimodel.vertex[0] = x;guimodel.vertex[1] = y;
		guimodel.vertex[2] = x + pwidth;guimodel.vertex[3] = y;
		guimodel.vertex[4] = x;guimodel.vertex[5] = y + pheight;
		guimodel.vertex[6] = x + pwidth;guimodel.vertex[7] = y + pheight;
		CRender2D(guimodel);
		guimodel.texcoord = p;
	}
	if (*clicked)
	{
		unsigned short strend = strlen(textstr), loop;
		if ((strend < maxchar && ((((!(mode & CINPUT_NONUMBER) && !key[CKEY_LSHIFT]) || (!(mode & CINPUT_NOSYMBOL) && key[CKEY_LSHIFT])) && (key[CKEY_0] || 
			key[CKEY_1] || key[CKEY_2] || key[CKEY_3] || key[CKEY_4] || key[CKEY_5] || key[CKEY_6] || key[CKEY_7] || key[CKEY_8] || key[CKEY_9]) || 
			((key[CKEY_PERIOD] || key[CKEY_MINUS]) && !key[CKEY_LSHIFT])) || 
			(!(mode & CINPUT_NOLETTER) && (key[CKEY_A] || key[CKEY_B] || key[CKEY_C] || key[CKEY_D] || key[CKEY_E] || key[CKEY_F] || 
			key[CKEY_G] || key[CKEY_H] || key[CKEY_I] || key[CKEY_J] || key[CKEY_K] || key[CKEY_L] || key[CKEY_M] || key[CKEY_N] || key[CKEY_O] || key[CKEY_P] || 
			key[CKEY_Q] || key[CKEY_R] || key[CKEY_S] || key[CKEY_T] || key[CKEY_U] || key[CKEY_V] || key[CKEY_W] || key[CKEY_X] || key[CKEY_Y] || key[CKEY_Z])) || 
			(!(mode & CINPUT_NOSYMBOL) && (key[CKEY_SEMICOLON] || key[CKEY_DIVIDE] || key[CKEY_TILDE] || key[CKEY_RBRACKET] || key[CKEY_LBRACKET] || 
			key[CKEY_SLASH] || key[CKEY_BACKSLASH] || key[CKEY_PLUS] || key[CKEY_MINUS] || key[CKEY_COMMA] || key[CKEY_PERIOD] || key[CKEY_QUOTE])) || 
			(!(mode & CINPUT_NOSPACE) && key[CKEY_SPACE]))) || 
			(key[CKEY_BACKSPACE] && *pos > 0) || 
			(key[CKEY_RIGHT] && *pos < strend || key[CKEY_LEFT] && *pos > 0)) // any allowed key being pressed
		{
			if (inputtime == 0 || (inputtime != 0 && CGetTime() - inputtime > CINPUT_PRESSTIME))
			{
				if (key[CKEY_BACKSPACE])
				{
					(*pos)--;
					CLoopc(loop, *pos, strend + 1, 1)
					{
						if (loop + 1 < strend)
							textstr[loop] = textstr[loop + 1];
						else
							textstr[loop] = '\0';
					}
				}
				else if (key[CKEY_RIGHT] && *pos < strend)
					(*pos)++;
				else if (key[CKEY_LEFT] && *pos > 0)
					(*pos)--;
				else
				{
					CLoopr(loop, strend, *pos, 1)
					{
						textstr[loop] = textstr[loop - 1];
					}
					CLoops(loop, 0, CKEY_TOTAL)
					{
						if (loop != CKEY_LSHIFT && loop != CKEY_LCTRL && key[loop])
						{
							textstr[*pos] = CKeyToChar(loop, key[CKEY_LSHIFT]);
							break;
						}
					}
					(*pos)++;
				}
				if (inputtime == 0) // reset timer
					inputtime = CGetTime();
			}
		}
		else if (inputtime != 0) // unset timer
			inputtime = 0;
		guimodel.texture = 0;
		guimodel.primitive = CMODEL_LINES;
		guimodel.vertexcnt = 2;
		float *p = guimodel.texcoord;
		guimodel.texcoord = NULL;
		guimodel.vertex[0] = guimodel.vertex[2] = x + 0.1f + (*pos) * font.width;
		guimodel.vertex[1] = y + 0.1f;
		guimodel.vertex[3] = y + font.height + 0.1f;
		glColor4f(0.0f, 0.0f, 0.0f, 1.0f);
		CRender2D(guimodel);
		guimodel.texcoord = p;
		guimodel.vertexcnt = 4;
		guimodel.primitive = CMODEL_TRISTRIPS;
	}
	CPrintText(textstr, font, x + 0.1f, y + 0.1f, tcolor);
}

void CEngine::CScroll(bool type, float x, float y, float length, CTexture mask, float *valptr, float minv, float vrange)
{
	float pos, value, sav;
	if (type) // Horizontal
	{
		sav = x + length;
		if ((cmx >= x - 0.1f && cmx <= sav + 0.1f && cmy >= y  && 
			cmy <= y + 0.5f && !prevclick || toggle) && key[CKEY_LMOUSE])
		{
			if (!toggle)
				toggle = true;
			pos = mx * rectx / width;
			if (pos < x)
			{
				pos = x;
				*valptr = value = minv;
			}
			else if (pos > sav)
			{
				pos = sav;
				*valptr = value = minv + vrange;
			}
			else
				*valptr = value = minv + (pos - x) / length * vrange;
		}
		else
		{
			if (toggle)
				toggle = false;
			value = *valptr;
			if (vrange == 0.0f)
				pos = length / 2.0f + x;
			else
				pos = (value - minv) / vrange * length + x;
		}
		// Line
		glColor4f(0.45f, 0.65f, 0.4f, 1.0f);
		guimodel.texture = 0;
		float *p = guimodel.texcoord;
		guimodel.texcoord = NULL;
		guimodel.vertex[0] = x;guimodel.vertex[1] = y + 0.3f;
		guimodel.vertex[2] = x;guimodel.vertex[3] = y + 0.2f;
		guimodel.vertex[4] = sav;guimodel.vertex[5] = y + 0.3f;
		guimodel.vertex[6] = sav;guimodel.vertex[7] = y + 0.2f;
		CRender2D(guimodel);
		guimodel.texcoord = p;
		// Box
		glColor3f(0.7f, 0.7f, 0.7f);
		if (mask != 0)
		{
			guimodel.texture = mask;
			guimodel.texcoord = p;
		}
		guimodel.vertex[0] = pos - 0.1f;guimodel.vertex[1] = y;
		guimodel.vertex[2] = pos + 0.1f;guimodel.vertex[3] = y;
		guimodel.vertex[4] = pos - 0.1f;guimodel.vertex[5] = y + 0.5f;
		guimodel.vertex[6] = pos + 0.1f;guimodel.vertex[7] = y + 0.5f;
		CRender2D(guimodel);
		if (mask == 0)
			guimodel.texcoord = p;
	}
	else
	{
		sav = y + length;
		if ((cmx >= x && cmx <= x + 0.5f && cmy >= y - 0.1f && 
			cmy <= sav + 0.1f && !prevclick || toggle) && key[CKEY_LMOUSE])
		{
			if (!toggle)
				toggle = true;
			pos = my * recty / height;
			if (pos < y)
			{
				pos = y;
				*valptr = value = minv;
			}
			else if (pos > sav)
			{
				pos = sav;
				*valptr = value = minv + vrange;
			}
			else
				*valptr = value = minv + (pos - y) / length * vrange;
		}
		else
		{
			if (toggle)
				toggle = false;
			value = *valptr;
			if (vrange == 0.0f)
				pos = length / 2.0f + y;
			else
				pos = (value - minv) / vrange * length + y;
		}
		// Line
		glColor4f(0.45f, 0.65f, 0.4f, 1.0f);
		guimodel.texture = 0;
		float *p = guimodel.texcoord;
		guimodel.texcoord = NULL;
		guimodel.vertex[0] = x + 0.2f;guimodel.vertex[1] = y;
		guimodel.vertex[2] = x + 0.2f;guimodel.vertex[3] = sav;
		guimodel.vertex[4] = x + 0.3f;guimodel.vertex[5] = y;
		guimodel.vertex[6] = x + 0.3f;guimodel.vertex[7] = sav;
		CRender2D(guimodel);
		// Box
		glColor3f(0.7f, 0.7f, 0.7f);
		if (mask != 0)
		{
			guimodel.texture = mask;
			guimodel.texcoord = p;
		}
		guimodel.vertex[0] = x;guimodel.vertex[1] = pos - 0.1f;
		guimodel.vertex[2] = x + 0.5f;guimodel.vertex[3] = pos - 0.1f;
		guimodel.vertex[4] = x;guimodel.vertex[5] = pos + 0.1f;
		guimodel.vertex[6] = x + 0.5f;guimodel.vertex[7] = pos + 0.1f;
		CRender2D(guimodel);
		if (mask == 0)
			guimodel.texcoord = p;
	}
}