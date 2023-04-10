/*
Copyright (c) 2016, David Liu
All rights reserved. Read the license for more information.
*/

#include "CEngine.h"

/* OpenGL functions */
#if defined(COS_WIN32)
// WGL
PFNWGLCREATECONTEXTATTRIBSARBPROC wglCreateContextAttribsARB = 0;
PFNWGLCHOOSEPIXELFORMATARBPROC wglChoosePixelFormatARB = 0;
#endif
// Shaders
PFNGLCREATEPROGRAMPROC glCreateProgram = 0;
PFNGLDELETEPROGRAMPROC glDeleteProgram = 0;
PFNGLUSEPROGRAMPROC glUseProgram = 0;
PFNGLATTACHSHADERPROC glAttachShader = 0;
PFNGLDETACHSHADERPROC glDetachShader = 0;
PFNGLLINKPROGRAMPROC glLinkProgram = 0;
PFNGLCREATESHADERPROC glCreateShader = 0;
PFNGLDELETESHADERPROC glDeleteShader = 0;
PFNGLSHADERSOURCEPROC glShaderSource = 0;
PFNGLCOMPILESHADERPROC glCompileShader = 0;
PFNGLGETSHADERIVPROC glGetShaderiv = 0;
PFNGLGETPROGRAMIVPROC glGetProgramiv = 0;
PFNGLGETSHADERINFOLOGPROC glGetShaderInfoLog = 0;
PFNGLGETUNIFORMLOCATIONPROC glGetUniformLocation = 0;
PFNGLISPROGRAMPROC glIsProgram = 0;
PFNGLISSHADERPROC glIsShader = 0;
PFNGLUNIFORM1IPROC glUniform1i = 0;
PFNGLUNIFORM1IVPROC glUniform1iv = 0;
PFNGLUNIFORM2IVPROC glUniform2iv = 0;
PFNGLUNIFORM3IVPROC glUniform3iv = 0;
PFNGLUNIFORM4IVPROC glUniform4iv = 0;
PFNGLUNIFORM1FPROC glUniform1f = 0;
PFNGLUNIFORM1FVPROC glUniform1fv = 0;
PFNGLUNIFORM2FVPROC glUniform2fv = 0;
PFNGLUNIFORM3FVPROC glUniform3fv = 0;
PFNGLUNIFORM4FVPROC glUniform4fv = 0;
PFNGLUNIFORMMATRIX4FVPROC glUniformMatrix4fv = 0;
PFNGLGETATTRIBLOCATIONPROC glGetAttribLocation = 0;
PFNGLVERTEXATTRIB1FPROC glVertexAttrib1f = 0;
PFNGLVERTEXATTRIB1FVPROC glVertexAttrib1fv = 0;
PFNGLVERTEXATTRIB2FVPROC glVertexAttrib2fv = 0;
PFNGLVERTEXATTRIB3FVPROC glVertexAttrib3fv = 0;
PFNGLVERTEXATTRIB4FVPROC glVertexAttrib4fv = 0;
PFNGLENABLEVERTEXATTRIBARRAYPROC glEnableVertexAttribArray = 0;
PFNGLDISABLEVERTEXATTRIBARRAYPROC glDisableVertexAttribArray = 0;
PFNGLVERTEXATTRIBPOINTERPROC glVertexAttribPointer = 0;
PFNGLBINDATTRIBLOCATIONPROC glBindAttribLocation = 0;
PFNGLGETACTIVEUNIFORMPROC glGetActiveUniform = 0;
// VBO
PFNGLGENBUFFERSPROC glGenBuffers = 0;
PFNGLBINDBUFFERPROC glBindBuffer = 0;
PFNGLBUFFERDATAPROC glBufferData = 0;
PFNGLBUFFERSUBDATAPROC glBufferSubData = 0;
PFNGLDELETEBUFFERSPROC glDeleteBuffers = 0;
PFNGLGETBUFFERPARAMETERIVPROC glGetBufferParameteriv = 0;
PFNGLMAPBUFFERPROC glMapBuffer = 0;
PFNGLUNMAPBUFFERPROC glUnmapBuffer = 0;
// VAO
PFNGLGENVERTEXARRAYSPROC glGenVertexArrays = 0 ;
PFNGLDELETEVERTEXARRAYSPROC glDeleteVertexArrays = 0 ;
PFNGLBINDVERTEXARRAYPROC glBindVertexArray = 0 ;
// Mipmap
PFNGLGENERATEMIPMAPPROC glGenerateMipmap = 0;
// VBO
PFNGLGENBUFFERSARBPROC glGenBuffersARB = 0;
PFNGLBINDBUFFERARBPROC glBindBufferARB = 0;
PFNGLBUFFERDATAARBPROC glBufferDataARB = 0;
PFNGLBUFFERSUBDATAARBPROC glBufferSubDataARB = 0;
PFNGLDELETEBUFFERSARBPROC glDeleteBuffersARB = 0;
PFNGLGETBUFFERPARAMETERIVARBPROC glGetBufferParameterivARB = 0;
PFNGLMAPBUFFERARBPROC glMapBufferARB = 0;
PFNGLUNMAPBUFFERARBPROC glUnmapBufferARB = 0;
// VAO
// VA
PFNGLCOLORPOINTEREXTPROC glColorPointerEXT = 0;
PFNGLDRAWARRAYSEXTPROC glDrawArraysEXT = 0;
PFNGLEDGEFLAGPOINTEREXTPROC glEdgeFlagPointerEXT = 0;
PFNGLGETPOINTERVEXTPROC glGetPointervEXT = 0;
PFNGLINDEXPOINTEREXTPROC glIndexPointerEXT = 0;
PFNGLNORMALPOINTEREXTPROC glNormalPointerEXT = 0;
PFNGLTEXCOORDPOINTEREXTPROC glTexCoordPointerEXT = 0;
PFNGLVERTEXPOINTEREXTPROC glVertexPointerEXT = 0;

/* Functions */
bool CEngine::CCheckExtension(const char *name)
{
	size_t len = strlen(name);
	char *extstr = (char*)glGetString(GL_EXTENSIONS), *ptr = strstr(extstr, name);
	if (ptr != NULL && (ptr == extstr || ptr[-1] == ' ') && (ptr[len] == ' ' || ptr[len] == '\0'))
		return true;
	return false;
}

void CEngine::CResize2D(int w, int h, double xrect, double yrect, double ox, double oy)
{
	width = w;height = h;
	rectx = xrect;recty = yrect;
	xorg = ox;yorg = oy;
	glViewport(0, 0, width, height);
	glMatrixMode(GL_PROJECTION);
	glLoadIdentity();
	glOrtho(ox, ox + xrect, oy, oy + yrect, 0.0, 1.0);
	glMatrixMode(GL_MODELVIEW);
}

void CEngine::CResize3D(int w, int h, double fov, double zfar)
{
	width = w;height = h;
	glViewport(0, 0, width, height);
	glMatrixMode(GL_PROJECTION);
	glLoadIdentity();
	double temp = tan(fov / 360.0 * CPI) * 0.01;
	glFrustum(-temp, temp, -temp, temp, 0.01, zfar);
	glMatrixMode(GL_MODELVIEW);
}

#if defined(COS_WIN32)
void CEngine::CVerticalSync(bool enable)
{
	if (CCheckExtension("WGL_EXT_swap_control"))
	{
		PFNWGLSWAPINTERVALEXTPROC wglSwapIntervalEXT;
		wglSwapIntervalEXT=(PFNWGLSWAPINTERVALEXTPROC)wglGetProcAddress("wglSwapIntervalEXT");
		if(wglSwapIntervalEXT)
		{
			wglSwapIntervalEXT(enable);
		}
	}
}
#elif defined(COS_LINUX)
void CEngine::CVerticalSync(bool enable)
{
	if (CCheckExtension("GLX_EXT_swap_control"));
		//glXSwapIntervalEXT(hDis, gLX_SWAP_INTERVAL_EXT, (int)enable);
}
#endif

bool CEngine::CLoadBMP(const char *path, CTexture &texnum)
{
    unsigned short int ftype;
    long int fbits, imgsize, i;
    short int planes, bitcnt;
    int width, height;
    unsigned char *data, *img;
    FILE* f;
	f = fopen(path, "rb");
	// File reading
	if (f == NULL)
	{
	    strcpy(inerr, "CLoadBMP: Unable to find/open the bitmap.");
	    return false;
	}
    if(!fread(&ftype, 2, 1, f))
    {
        strcpy(inerr, "CLoadBMP: Unable to read the bitmap.");
		fclose(f);
        return false;
    }
    if (ftype != 19778)
    {
        strcpy(inerr, "CLoadBMP: Image file is not a bitmap file.");
		fclose(f);
        return false;
    }
    fseek(f, 8, SEEK_CUR); // Skip header and get to the actual image data
    if (!fread(&fbits, 4, 1, f))
    {
        strcpy(inerr, "CLoadBMP: Unable to successfully read the bitmap data.");
		fclose(f);
        return false;
    }
    fseek(f, 4, SEEK_CUR); // Skip part of header
    fread(&width, 4, 1, f);
    fread(&height, 4, 1, f);
    fread(&planes, 2, 1, f);
    if (planes != 1)
    {
        strcpy(inerr, "CLoadBMP: Number of planes do not equal 1.");
		fclose(f);
        return false;
    }
    if (!fread(&bitcnt, 2, 1, f)) // Bits per pixel
    {
        strcpy(inerr, "CLoadBMP: Unable to successfully read the bitmap data.");
		fclose(f);
        return false;
    }
    if (bitcnt != 24)
    {
        strcpy(inerr, "CLoadBMP: Bits per pixel do not equal 24.");
		fclose(f);
        return false;
    }
    imgsize = width * height * 3;
    data = new unsigned char[imgsize];
    // Actual image data
    fseek(f, fbits, SEEK_SET);
    if (!fread(data, imgsize, 1, f))
    {
		delete[] data;
        strcpy(inerr, "CLoadBMP: Unable to successfully read the bitmap data.");
		fclose(f);
        return false;
    }
    fclose(f);
	img = new unsigned char[width * height * 4];
	// Convert into 4-bytes format, 32-bit is best for most GPU's
	fbits = 0;
    CLoopc(i, 0, imgsize, 3)
    {
        img[fbits++] = data[i];
		img[fbits++] = data[i + 1];
		img[fbits++] = data[i + 2];
		img[fbits++] = 255;
    }
	delete[] data;
    // Generate texture
    glGenTextures(1, &texnum);
    glBindTexture(GL_TEXTURE_2D, texnum);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_BGRA, GL_UNSIGNED_BYTE, img);
	delete[] img;
    return true;
}

bool CEngine::CLoadTGA(const char *path, CTexture &texnum)
{
	unsigned int loop1;
	FILE * f;
	unsigned char tgacmp[12];
	unsigned char tgainfo[6];
	unsigned char tgaheader[12] = {0, 0, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0}; // Uncompressed
	unsigned char *tgasize;
	unsigned int tgadim[2]; // Width and height
	f = fopen(path, "rb");
	if (f == NULL)
	{
		strcpy(inerr, "CLoadTGA: Unable to find/open the tga file.");
		return false;
	}
	if (fread(tgacmp, 1, sizeof(tgacmp), f) == sizeof(tgacmp) && memcmp(tgaheader, tgacmp, sizeof(tgaheader)) == 0 && fread(tgainfo, 1, sizeof(tgainfo), f) == sizeof(tgainfo))
	{
		tgadim[0] = tgainfo[1] * 256 + tgainfo[0];
		tgadim[1] = tgainfo[3] * 256 + tgainfo[2];
		if (tgadim[0] <= 0 || tgadim[1] <= 0 || tgainfo[4] != 32)
		{
			strcpy(inerr, "CLoadTGA: The .tga files must be 32-bit uncompressed.");
			fclose(f);
			return false;
		}
		tgainfo[4] /= 8;
		loop1 = tgadim[0] * tgadim[1] * tgainfo[4]; // Size
		tgasize = new unsigned char[loop1];
		if (fread(tgasize, 1, loop1, f) != loop1)
		{
			delete[] tgasize;
			strcpy(inerr, "CLoadTGA: Failed to successfully read the tga data.");
			fclose(f);
			return false;
		}
		fclose(f);
		// Build texture
		glGenTextures(1, &texnum);
		glBindTexture(GL_TEXTURE_2D, texnum);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, tgadim[0], tgadim[1], 0, GL_BGRA, GL_UNSIGNED_BYTE, tgasize);
		delete[] tgasize;
		return true;
	}
	strcpy(inerr, "CLoadTGA: Incompatible tga header.");
	fclose(f);
	return false;
}

void CEngine::CSetTexture(CTexture &texnum, char properties)
{
	glBindTexture(GL_TEXTURE_2D, texnum);
	if (properties & CTEX_SPRITE)
		glTexEnvi(GL_POINT_SPRITE, GL_COORD_REPLACE, GL_TRUE);
	if ((properties & CTEX_MIPMAP) && glcorefeature[CGL_MIPMAP])
	{
		if (glv[0] > 2)
			glGenerateMipmap(GL_TEXTURE_2D);
		else
			glTexParameteri(GL_TEXTURE_2D, GL_GENERATE_MIPMAP, GL_TRUE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
	}
	else
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	if (properties & CTEX_WRAPREPEAT)
	{
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
	}
	else
	{
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	}
	if ((properties & CTEX_ANIFILTER) && glcorefeature[CGL_ANIFILTER])
		glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY_EXT, maxAF);
}

void CEngine::CClearTexture(CTexture *tex, unsigned int count)
{
	glDeleteTextures(count, tex);
}

bool CEngine::CLoadOBJ(const char *path, CModel& mdl)
{
	// Init
	unsigned int i, v, t, n, c, b;
	unsigned int tempvertexcnt = 0, temptexcoordcnt = 0, tempnormalcnt = 0;
	char buf[CSIZE_MAXPATH];
	FILE* f = fopen(path, "rb");
	if (f == NULL)
	{
		strcpy(inerr, "CLoadOBJ: Failed to find/open the .obj file.");
		return false;
	}
	// Count objects
	while (fgets(buf, CSIZE_MAXPATH, f) != NULL && feof(f) == 0)
    {
		if (buf[0] != '#')
		{
			if (buf[0] == 'v')
			{
				if (buf[1] == ' ')
				{
					if (sscanf(buf + 2, "%f %f %f", &v, &n, &t) != 3)
					{
						strcpy(inerr, "CLoadOBJ: Error in vertex syntax.");
						fclose(f);
						return false;
					}
					tempvertexcnt++;
				}
			   else if (buf[1] == 't')
			   {
				   if (sscanf(buf + 2, "%f %f", &v, &n) != 2)
					{
						strcpy(inerr, "CLoadOBJ: Error in texture coordinate syntax.");
						fclose(f);
						return false;
					}
					temptexcoordcnt++;
				}
				else if (buf[1] == 'n')
				{
					if (sscanf(buf + 2, "%f %f %f", &v, &n, &t) != 3)
					{
						strcpy(inerr, "CLoadOBJ: Error in normal vector syntax.");
						fclose(f);
						return false;
					}
					tempnormalcnt++;
			   }
			}
			else if (buf[0] == 'f')
			{
				mdl.vertexcnt += 3;
				if (sscanf(buf + 2, "%d/%d/%d", &v, &n, &t) == 3)
				{
					if (temptexcoordcnt == 0 && tempnormalcnt == 0)
					{
						strcpy(inerr, "CLoadOBJ: Missing texture coordinates and normals.");
						fclose(f);
						return false;
					}
				}
				else if (sscanf(buf + 2, "%d//%d", &v, &n) == 2)
				{
					if (tempnormalcnt == 0)
					{
						strcpy(inerr, "CLoadOBJ: Missing normals.");
						fclose(f);
						return false;
					}
				}
				else if (sscanf(buf + 2, "%d/%d", &v, &t) == 2)
				{
					if (temptexcoordcnt == 0)
					{
						strcpy(inerr, "CLoadOBJ: Missing texture coordinates.");
						fclose(f);
						return false;
					}
				}
				else if (sscanf(buf + 2, "%d", &v) != 1)
				{
					strcpy(inerr, "CLoadOBJ: Syntax error for vertex data.");
					fclose(f);
					return false;
				}
			}
			else if (strncmp(buf, "mtllib", 6) == 0)
			{
				/*strcpy(buf, path);
				c = strlen(buf);
				buf[c - 3] = 'm';
				buf[c - 2] = 't';
				buf[c - 1] = 'l';
				f2 = fopen(buf, "rb");
				if (f2 == NULL)
				{
					strcpy(inerr, "CLoadOBJ: Unable to open/find the respective .mtl file.");
					fclose(f);
					return false;
				}
				while (fgets(buf2, 100, f2) != NULL && feof(f2) == 0)
				{
					if (strncmp(buf2, "map_Kd", 6) == 0)
					{
						strcpy(buf, rootpath);
						CLoops(t, 0, 100)
						{
							if (buf2[t] == '\n')
							{
								buf2[t] = '\0';
								break;
							}
						}
						strcat(buf, buf2 + 7);
						if (strstr(buf2, ".bmp") != NULL)
							t = CLoadBMP(buf, mdl.texture);
						else if (strstr(buf2, ".tga") != NULL)
							t = CLoadTGA(buf, mdl.texture);
						else
							t = 0;
						if (t == 0)
						{
							fclose(f2);
							fclose(f);
							return false;
						}
						else

						break;
					}
				}
				fclose(f2);*/
			}
		}
	}
	if (tempvertexcnt == 0)
	{
		strcpy(inerr, "CLoadOBJ: No vertex data found.");
		fclose(f);
		return false;
	}
	// Allocate data
	float *temptexcoord;
	unsigned int *temptexindices;
	float *tempnormal;
	unsigned int *tempnormalindices;
	float *tempvertex;
	unsigned int *tempvertexindices;
	mdl.vertex = new float[mdl.vertexcnt * 3];
	tempvertex = new float[tempvertexcnt * 3];
	tempvertexindices = new unsigned int[mdl.vertexcnt];
	if (temptexcoordcnt > 0)
	{
		mdl.texcoord = new float[mdl.vertexcnt * 2];
		temptexcoord = new float[temptexcoordcnt * 2];
		temptexindices = new unsigned int[mdl.vertexcnt];
	}
	if (tempnormalcnt > 0)
	{
		mdl.normal = new float[mdl.vertexcnt * 3];
		tempnormal = new float[tempnormalcnt * 3];
		tempnormalindices = new unsigned int[mdl.vertexcnt];
	}
	// Store data
	v = t = n = c = 0;
	rewind(f);
	while (fgets(buf, CSIZE_MAXPATH, f) != NULL && !feof(f))
	{
		CLoops(i, 0, CSIZE_MAXPATH)
		{
			if (buf[i] == '\n' || i == CSIZE_MAXPATH - 1)
			{
				buf[i] = '\0';
				break;
			}
		}
		if (buf[0] != '#')
		{
			if (buf[0] == 'v')
			{
				if (buf[1] == ' ')
				{
					sscanf(buf + 2, "%f %f %f", &tempvertex[v], &tempvertex[v + 1], &tempvertex[v + 2]);
					v += 3;
				}
				else if (buf[1] == 't')
				{
					sscanf(buf + 3, "%f %f", &temptexcoord[t], &temptexcoord[t + 1]);
					t += 2;
				}
				else if (buf[1] == 'n')
				{
					sscanf(buf + 3, "%f %f %f", &tempnormal[n], &tempnormal[n + 1], &tempnormal[n + 2]);
					n += 3;
				}
			}
			else if (buf[0] == 'f')
			{
				b = 2; // Counter
				CLoops(i, c, c + 3)
				{
					if (sscanf(buf + b, "%d/%d/%d", &tempvertexindices[i], &temptexindices[i], &tempnormalindices[i]) != 3)
					{
						if (sscanf(buf + b, "%d//%d", &tempvertexindices[i], &tempnormalindices[i]) != 2)
						{
							if (sscanf(buf + b, "%d/%d", &tempvertexindices[i], &temptexindices[i]) != 2)
								sscanf(buf + b, "%d", &tempvertexindices[i]);
						}
					}
					tempvertexindices[i]--;
					if (temptexcoordcnt > 0)
						temptexindices[i]--;
					if (tempnormalcnt > 0)
						tempnormalindices[i]--;
					//if (!glcorefeature[CGL_VBO] && !glcorefeature[CGL_VA])
						//tempvertexindices[i]--;
					while (b < strlen(buf) && buf[b] != ' ')
						b++;
					b++;
				}
				c += 3;
			}
		}
	}
	fclose(f);
	// Regroup arrays
	CLoops(i, 0, mdl.vertexcnt)
	{
		mdl.vertex[i * 3] = tempvertex[3 * tempvertexindices[i]];
		mdl.vertex[i * 3 + 1] = tempvertex[3 * tempvertexindices[i] + 1];
		mdl.vertex[i * 3 + 2] = tempvertex[3 * tempvertexindices[i] + 2];
		if (temptexcoordcnt > 0)
		{
			mdl.texcoord[i * 2] = temptexcoord[2 * temptexindices[i]];
			mdl.texcoord[i * 2 + 1] = temptexcoord[2 * temptexindices[i] + 1];
		}
		if (tempnormalcnt > 0)
		{
			mdl.normal[i * 3] = tempnormal[3 * tempnormalindices[i]];
			mdl.normal[i * 3 + 1] = tempnormal[3 * tempnormalindices[i] + 1];
			mdl.normal[i * 3 + 2] = tempnormal[3 * tempnormalindices[i] + 2];
		}
	}
	delete[] tempvertex;
	delete[] tempvertexindices;
	if (temptexcoordcnt > 0)
	{
		delete[] temptexindices;
		delete[] temptexcoord;
	}
	if (tempnormalcnt > 0)
	{
		delete[] tempnormalindices;
		delete[] tempnormal;
	}
	mdl.primitive = CMODEL_TRIANGLES;
	return true;
}

void CEngine::CCreateModel(CModel& mdl, int primitive, int mode)
{
	mdl.mode = mode;
	mdl.primitive = primitive;
	// VBO
	if (glcorefeature[CGL_VBO])
	{
		/*glEnableVertexAttribArray(attribute_v_coord);
		// Describe our vertices array to OpenGL (it can't guess its format automatically)
		glBindBuffer(GL_ARRAY_BUFFER, vbo_mesh_vertices);
		glVertexAttribPointer(
    attribute_v_coord,  // attribute
    4,                  // number of elements per vertex, here (x,y,z,w)
    GL_FLOAT,           // the type of each element
    GL_FALSE,           // take our values as-is
    0,                  // no extra data between each position
    0                   // offset of first element
  );
 
  glBindBuffer(GL_ARRAY_BUFFER, vbo_mesh_normals);
  glVertexAttribPointer(
    attribute_v_normal, // attribute
    3,                  // number of elements per vertex, here (x,y,z)
    GL_FLOAT,           // the type of each element
    GL_FALSE,           // take our values as-is
    0,                  // no extra data between each position
    0                   // offset of first element
  );
 
  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ibo_mesh_elements);
  int size;  glGetBufferParameteriv(GL_ELEMENT_ARRAY_BUFFER, GL_BUFFER_SIZE, &size);  
  glDrawElements(GL_TRIANGLES, size/sizeof(GLushort), GL_UNSIGNED_SHORT, 0);
		//glGenVertexArrays(1, &mdl.vao);
		//glBindVertexArray(mdl.vao);
		glGenBuffers(1, &mdl.vbo);
		glBindBuffer(GL_ARRAY_BUFFER, mdl.vbo);
		glBufferData(GL_ARRAY_BUFFER, sizeof(mdl.vertex) + sizeof(mdl.texcoord) + sizeof(mdl.normal), 0, mdl.mode);
		glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(mdl.vertex), mdl.vertex);
		if (mdl.texcoord != NULL)
			glBufferSubData(GL_ARRAY_BUFFER, sizeof(mdl.vertex), sizeof(mdl.texcoord), mdl.texcoord);
		if (mdl.normal != NULL)
			glBufferSubData(GL_ARRAY_BUFFER, sizeof(mdl.vertex) + sizeof(mdl.texcoord), sizeof(mdl.normal), mdl.normal);
		//glVertexAttribPointer((GLuint)0, 3, GL_FLOAT, GL_FALSE, 0, (void*)0);
		//glVertexAttribPointer((GLuint)0, 3, GL_FLOAT, GL_FALSE, 0, (void*)0);
		//glEnableVertexAttribArray(0);
	}
			/*glGenVertexArrays(1, &VAO);
			//glBindVertexArray(VAO);
			glGenBuffers(2, mdl.vbo);
			glBindBuffer(GL_ARRAY_BUFFER, mdl.vbo[0]);
			// Copy array data into buffer
			glBufferData(GL_ARRAY_BUFFER, sizeof(mdl.vertex) + sizeof(mdl.texcoord) + sizeof(mdl.normal), 0, GL_STATIC_DRAW);
			glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(mdl.vertex), mdl.vertex);
			if (mdl.texcoordcnt > 0)
				glBufferSubData(GL_ARRAY_BUFFER, sizeof(mdl.vertex), sizeof(mdl.texcoord), mdl.texcoord);
			if (mdl.normalcnt > 0)
				glBufferSubData(GL_ARRAY_BUFFER, sizeof(mdl.vertex) + sizeof(mdl.texcoord), sizeof(mdl.normal), mdl.normal);
			glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, mdl.vbo[1]);
			// Copy indice data into buffer
			glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(mdl.vertexindices) + sizeof(mdl.texindices) + sizeof(mdl.normalindices), 0, GL_STATIC_DRAW);
			glBufferSubData(GL_ELEMENT_ARRAY_BUFFER, 0, sizeof(mdl.vertexindices), mdl.vertexindices);
			if (mdl.texcoordcnt > 0)
				glBufferSubData(GL_ELEMENT_ARRAY_BUFFER, sizeof(mdl.vertexindices), sizeof(mdl.texindices), mdl.texindices);
			if (mdl.normalcnt > 0)
				glBufferSubData(GL_ELEMENT_ARRAY_BUFFER, sizeof(mdl.vertexindices) + sizeof(mdl.texindices), sizeof(mdl.normalindices), mdl.normalindices);
			//glBindVertexArray(0);*/
	}
}

/*void CEngine::CUpdateModel(CModel& mdl)
{
	glBindBuffer(GL_ARRAY_BUFFER, mdl.vbo);
	glBufferData(GL_ARRAY_BUFFER, sizeof(mdl.vertex) + sizeof(mdl.texcoord) + sizeof(mdl.normal), 0, mdl.mode);
}*/

void CEngine::CDeleteModel(CModel* mdl)
{
	if (glcorefeature[CGL_VBO])
	{
		if (moderncontext)
			glDeleteBuffers(1, &mdl->vbo);
	}
	if (mdl->vertex != NULL)
		delete[] mdl->vertex;
	if (mdl->texcoord != NULL)
		delete[] mdl->texcoord;
	if (mdl->normal != NULL)
		delete[] mdl->normal;
	// Reset
	mdl->vertex = NULL;
	mdl->texcoord = NULL;
	mdl->normal = NULL;
	mdl->vertexcnt = 0;
}

void CEngine::CRender3D(CModel& mdl)
{
	if (glcorefeature[CGL_VBO])
	{
		glEnableVertexAttribArray(0);
		glBindVertexArray(mdl.vao);
		glBindBuffer(GL_ARRAY_BUFFER, mdl.vbo);
		//glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, mdl.vbo[1]);
		glDrawArrays(GL_TRIANGLES, 0, mdl.vertexcnt);
		glBindVertexArray(0);
		glDisableVertexAttribArray(0);
		/*glBindBuffer(GL_ARRAY_BUFFER, mdl.vbo[0]);
		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, mdl.vbo[1]);
		glEnableClientState(GL_VERTEX_ARRAY);
		glEnableClientState(GL_TEXTURE_COORD_ARRAY);
		glEnableClientState(GL_NORMAL_ARRAY);
		glVertexPointer(3, GL_FLOAT, 0, 0);
		glTexCoordPointer(2, GL_FLOAT, 0, (void*)sizeof(mdl.vertex));
		glNormalPointer(GL_FLOAT, 0, (void*)(sizeof(mdl.vertex) + sizeof(mdl.texcoord)));
		glDrawElements(GL_TRIANGLES, mdl.facecnt * 3, GL_UNSIGNED_INT, mdl.vertexindices);
		glDisableClientState(GL_VERTEX_ARRAY);
		glDisableClientState(GL_TEXTURE_COORD_ARRAY);
		glDisableClientState(GL_NORMAL_ARRAY);
		glBindBuffer(GL_ARRAY_BUFFER, 0);
		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);*/
	}
	else if (glcorefeature[CGL_VA])
	{
		glEnableClientState(GL_VERTEX_ARRAY);
		glVertexPointer(3, GL_FLOAT, 0, mdl.vertex);
		glBindTexture(GL_TEXTURE_2D, mdl.texture);
		if (mdl.texcoord != NULL)
		{
			glEnableClientState(GL_TEXTURE_COORD_ARRAY);
			glTexCoordPointer(2, GL_FLOAT, 0, mdl.texcoord);
		}
		if (mdl.normal != NULL)
		{
			glEnableClientState(GL_NORMAL_ARRAY);
			glNormalPointer(GL_FLOAT, 0, mdl.normal);
		}
		glNormalPointer(GL_FLOAT, 0, mdl.normal);
		glDrawArrays(mdl.primitive, 0, mdl.vertexcnt);
		glDisableClientState(GL_VERTEX_ARRAY);
		if (mdl.texcoord != NULL)
			glDisableClientState(GL_TEXTURE_COORD_ARRAY);
		if (mdl.normal != NULL)
			glDisableClientState(GL_NORMAL_ARRAY);
	}
	else
	{
		unsigned int i;
		glBindTexture(GL_TEXTURE_2D, mdl.texture);
		glBegin(mdl.primitive);
		CLoops(i, 0, mdl.vertexcnt)
		{
			if (mdl.texcoord != NULL)
				glTexCoord2fv(mdl.texcoord + 2 * i);
			if (mdl.normal != NULL)
				glNormal3fv(mdl.normal + 3 * i);
			glVertex3fv(mdl.vertex + 3 * i);
		}
		glEnd();
	}
}

void CEngine::CRender2D(CModel& mdl)
{
	if (glcorefeature[CGL_VBO])
	{
		/*glEnableVertexAttribArray(0);
		//glBindVertexArray(mdl.vao);
		glBindBuffer(GL_ARRAY_BUFFER, mdl.vbo);
		glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, (void*)0);
		glDrawArrays(GL_TRIANGLES, 0, mdl.vertexcnt);
		//glBindVertexArray(0);
		glDisableVertexAttribArray(0);
		glBindBuffer(GL_ARRAY_BUFFER, mdl.vbo[0]);
		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, mdl.vbo[1]);
		glEnableClientState(GL_VERTEX_ARRAY);
		glEnableClientState(GL_TEXTURE_COORD_ARRAY);
		glEnableClientState(GL_NORMAL_ARRAY);
		glVertexPointer(3, GL_FLOAT, 0, 0);
		glTexCoordPointer(2, GL_FLOAT, 0, (void*)sizeof(mdl.vertex));
		glNormalPointer(GL_FLOAT, 0, (void*)(sizeof(mdl.vertex) + sizeof(mdl.texcoord)));
		glDrawElements(GL_TRIANGLES, mdl.facecnt * 3, GL_UNSIGNED_INT, mdl.vertexindices);
		glDisableClientState(GL_VERTEX_ARRAY);
		glDisableClientState(GL_TEXTURE_COORD_ARRAY);
		glDisableClientState(GL_NORMAL_ARRAY);
		glBindBuffer(GL_ARRAY_BUFFER, 0);
		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);*/
	}
	else if (glcorefeature[CGL_VA])
	{
		glEnableClientState(GL_VERTEX_ARRAY);
		glVertexPointer(2, GL_FLOAT, 0, mdl.vertex);
		glBindTexture(GL_TEXTURE_2D, mdl.texture);
		if (mdl.texcoord != NULL)
		{
			glEnableClientState(GL_TEXTURE_COORD_ARRAY);
			glTexCoordPointer(2, GL_FLOAT, 0, mdl.texcoord);
		}
		glDrawArrays(mdl.primitive, 0, mdl.vertexcnt);
		glDisableClientState(GL_VERTEX_ARRAY);
		if (mdl.texcoord != NULL)
			glDisableClientState(GL_TEXTURE_COORD_ARRAY);
	}
	else
	{
		unsigned int i, v = mdl.vertexcnt * 2;
		glBindTexture(GL_TEXTURE_2D, mdl.texture);
		glBegin(mdl.primitive);
		CLoopc(i, 0, v, 2)
		{
			if (mdl.texcoord != NULL)
				glTexCoord2fv(mdl.texcoord + i);
			glVertex2fv(mdl.vertex + i);
		}
		glEnd();
	}
}

bool CEngine::CLoadShader(const char *data, unsigned int type, unsigned int& id, char** infolog)
{
	int param[1];
	id = glCreateShader(type);
	glShaderSource(id, 1, &data, NULL);
	if (id == 0)
	{
		strcpy(inerr, "Failed to create the shader.");
		return false;
	}
	glCompileShader(id);
	glGetShaderiv(id, GL_COMPILE_STATUS, param);
	if (param[0] == GL_FALSE)
	{
		strcpy(inerr, "Failed to successfully compile the shader.");
		int infologlength = 0, chars = 0;
		glGetShaderiv(id, GL_INFO_LOG_LENGTH, &infologlength);
		if (infologlength > 0)
		{
			*infolog = new char[infologlength];
			glGetShaderInfoLog(id, infologlength, &chars, *infolog);
		}
		return false;
	}
	return true;
}

bool CEngine::CCreateShader(unsigned int vshader, unsigned int fshader, unsigned int& shaderprogram)
{
	int param;
	if (!glIsProgram(shaderprogram))
		shaderprogram = glCreateProgram();
	if (shaderprogram == 0)
	{
		strcpy(inerr, "Failed to create the shader program.");
		return false;
	}
	glAttachShader(shaderprogram, vshader);
	glAttachShader(shaderprogram, fshader);
	glLinkProgram(shaderprogram);
	glGetProgramiv(shaderprogram, GL_LINK_STATUS, &param);
	if (param == GL_FALSE)
	{
		strcpy(inerr, "Failed to successfully link the shader program.");
		return false;
	}
	return true;
}

void CEngine::CUseShader(unsigned int shaderprogram)
{
	if(glUseProgram != NULL)
	{
		glUseProgram(shaderprogram);
	}
}

void CEngine::CDeleteShader(unsigned int vshader, unsigned int fshader, unsigned int shaderprogram)
{
	if (shaderprogram != 0)
	{
		glDetachShader(shaderprogram, vshader);
		glDetachShader(shaderprogram, fshader);
		glDeleteProgram(shaderprogram);
	}
	if (vshader != 0)
		glDeleteShader(vshader);
	if (fshader != 0)
		glDeleteShader(fshader);
}
