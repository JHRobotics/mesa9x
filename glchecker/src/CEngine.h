/*
Copyright (c) 2016, David Liu
All rights reserved. Read the license for more information.
*/

#ifndef CENGINE_HEADER
#define CENGINE_HEARER

//#define CEXPORT

/* CEngine version */
#define CVERSION_MAJOR 0
#define CVERSION_MINOR 2
#define CVERSION_REVISION 0

/* OS */
#if defined(_WIN32)
#define COS_WIN32
#elif defined(__linux)
#define COS_LINUX
#endif

/* Standard headers */
#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <string.h>
#include <math.h>
#include <limits.h>
//#include <iostream>
//#include <fstream>

/* Windows */
#if defined(COS_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winsock2.h>
//#include <ws2ipdef.h>
// Libs and warnings
#pragma comment (lib, "kernel32.lib")
#pragma comment (lib, "user32.lib")
#pragma comment (lib, "gdi32.lib")
#pragma comment (lib, "winspool.lib")
#pragma comment (lib, "comdlg32.lib")
#pragma comment (lib, "ws2_32.lib")
#pragma warning (disable:4996)
#pragma warning (disable:4244)
#pragma warning (disable:4800)
#pragma warning (disable:4251)
/*#ifdef CEXPORT
#define CAPI __declspec(dllexport)
#else
#define CAPI __declspec(dllimport)
#endif*/
#define CAPI
#define C_ICON 1

/* Linux */
#elif defined(COS_LINUX)
#include <sys/types.h>
#include <sys/time.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <dirent.h>
#include <fnmatch.h>
#include <errno.h>
#include <GL/glx.h>
#include <X11/X.h>
#include <X11/keysym.h>
#include <X11/extensions/xf86vmode.h>
#include <X11/extensions/Xrandr.h>
#define CAPI 
#endif

/* OpenGL */
#include <GL/gl.h>
#include <GL/glext.h>
#if defined(COS_WIN32)
#include <GL/wglext.h>
// WGL ext
extern PFNWGLCREATECONTEXTATTRIBSARBPROC wglCreateContextAttribsARB;
extern PFNWGLCHOOSEPIXELFORMATARBPROC wglChoosePixelFormatARB;
// Libs
#pragma comment (lib, "opengl32.lib")
#elif defined(COS_LINUX)
#define GLX_GLXEXT_PROTOTYPES
#include <GL/glxext.h>
// GLX ext
#endif
// Shaders
extern PFNGLCREATEPROGRAMPROC glCreateProgram;
extern PFNGLDELETEPROGRAMPROC glDeleteProgram;
extern PFNGLUSEPROGRAMPROC glUseProgram;
extern PFNGLATTACHSHADERPROC glAttachShader;
extern PFNGLDETACHSHADERPROC glDetachShader;
extern PFNGLLINKPROGRAMPROC glLinkProgram;
extern PFNGLCREATESHADERPROC glCreateShader;
extern PFNGLDELETESHADERPROC glDeleteShader;
extern PFNGLSHADERSOURCEPROC glShaderSource;
extern PFNGLCOMPILESHADERPROC glCompileShader;
extern PFNGLGETSHADERIVPROC glGetShaderiv;
extern PFNGLGETPROGRAMIVPROC glGetProgramiv;
extern PFNGLGETSHADERINFOLOGPROC glGetShaderInfoLog;
extern PFNGLGETUNIFORMLOCATIONPROC glGetUniformLocation;
extern PFNGLISPROGRAMPROC glIsProgram;
extern PFNGLISSHADERPROC glIsShader;
extern PFNGLUNIFORM1IPROC glUniform1i;
extern PFNGLUNIFORM1IVPROC glUniform1iv;
extern PFNGLUNIFORM2IVPROC glUniform2iv;
extern PFNGLUNIFORM3IVPROC glUniform3iv;
extern PFNGLUNIFORM4IVPROC glUniform4iv;
extern PFNGLUNIFORM1FPROC glUniform1f;
extern PFNGLUNIFORM1FVPROC glUniform1fv;
extern PFNGLUNIFORM2FVPROC glUniform2fv;
extern PFNGLUNIFORM3FVPROC glUniform3fv;
extern PFNGLUNIFORM4FVPROC glUniform4fv;
extern PFNGLUNIFORMMATRIX4FVPROC glUniformMatrix4fv;
extern PFNGLGETATTRIBLOCATIONPROC glGetAttribLocation;
extern PFNGLVERTEXATTRIB1FPROC glVertexAttrib1f;
extern PFNGLVERTEXATTRIB1FVPROC glVertexAttrib1fv;
extern PFNGLVERTEXATTRIB2FVPROC glVertexAttrib2fv;
extern PFNGLVERTEXATTRIB3FVPROC glVertexAttrib3fv;
extern PFNGLVERTEXATTRIB4FVPROC glVertexAttrib4fv;
extern PFNGLENABLEVERTEXATTRIBARRAYPROC glEnableVertexAttribArray;
extern PFNGLDISABLEVERTEXATTRIBARRAYPROC glDisableVertexAttribArray;
extern PFNGLVERTEXATTRIBPOINTERPROC glVertexAttribPointer;
extern PFNGLBINDATTRIBLOCATIONPROC glBindAttribLocation;
extern PFNGLGETACTIVEUNIFORMPROC glGetActiveUniform;
// VBO
extern PFNGLGENBUFFERSPROC glGenBuffers;
extern PFNGLBINDBUFFERPROC glBindBuffer;
extern PFNGLBUFFERDATAPROC glBufferData;
extern PFNGLBUFFERSUBDATAPROC glBufferSubData;
extern PFNGLDELETEBUFFERSPROC glDeleteBuffers;
extern PFNGLGETBUFFERPARAMETERIVPROC glGetBufferParameteriv;
extern PFNGLMAPBUFFERPROC glMapBuffer;
extern PFNGLUNMAPBUFFERPROC glUnmapBuffer;
// VAO
extern PFNGLGENVERTEXARRAYSPROC glGenVertexArrays;
extern PFNGLDELETEVERTEXARRAYSPROC glDeleteVertexArrays;
extern PFNGLBINDVERTEXARRAYPROC glBindVertexArray;
// Mipmap
extern PFNGLGENERATEMIPMAPPROC glGenerateMipmap;
// VBO ext
extern PFNGLGENBUFFERSARBPROC glGenBuffersARB;
extern PFNGLBINDBUFFERARBPROC glBindBufferARB;
extern PFNGLBUFFERDATAARBPROC glBufferDataARB;
extern PFNGLBUFFERSUBDATAARBPROC glBufferSubDataARB;
extern PFNGLDELETEBUFFERSARBPROC glDeleteBuffersARB;
extern PFNGLGETBUFFERPARAMETERIVARBPROC glGetBufferParameterivARB;
extern PFNGLMAPBUFFERARBPROC glMapBufferARB;
extern PFNGLUNMAPBUFFERARBPROC glUnmapBufferARB;
// VA ext
extern PFNGLCOLORPOINTEREXTPROC glColorPointerEXT;
extern PFNGLDRAWARRAYSEXTPROC glDrawArraysEXT;
extern PFNGLEDGEFLAGPOINTEREXTPROC glEdgeFlagPointerEXT;
extern PFNGLGETPOINTERVEXTPROC glGetPointervEXT;
extern PFNGLINDEXPOINTEREXTPROC glIndexPointerEXT;
extern PFNGLNORMALPOINTEREXTPROC glNormalPointerEXT;
extern PFNGLTEXCOORDPOINTEREXTPROC glTexCoordPointerEXT;
extern PFNGLVERTEXPOINTEREXTPROC glVertexPointerEXT;

using namespace std;

/* BASS */
/*
#if defined(COS_LINUX) // Prevent conflict with bass header
#define __OBJC__
#endif
#include "bass.h"
#if defined(COS_WIN32)
#pragma comment (lib, "bass.lib")
#endif
*/

/* Tools */
CAPI extern const double CPI;
CAPI extern const double CTAU;
#define CLoops(LOOP, START, MAX) for (LOOP = START;LOOP < MAX;LOOP++)
#define CLoopc(LOOP, START, MAX, ADD) for (LOOP = START;LOOP < MAX;LOOP += ADD)
#define CLoopr(LOOP, START, MIN, SUBT) for (LOOP = START;LOOP > MIN;LOOP -= SUBT)
#define CBit(DATA, BIT) ((DATA & BIT) == BIT)

/* CEngine data types */
typedef unsigned int CTexture;
typedef unsigned long CSound;
typedef unsigned long CSample;
typedef signed char CByte1s; // one byte
typedef short int CByte2s; // two bytes
typedef long int CByte4s; // four bytes
typedef long long CByte8s; // eight bytes
typedef unsigned char CByte1u; // one byte
typedef unsigned short int CByte2u; // two bytes
typedef unsigned long int CByte4u; // four bytes
typedef unsigned long long CByte8u; // eight bytes
#if defined(COS_WIN32)
typedef SOCKET CSocket;
typedef SOCKADDR_IN CSockAddr;
#elif defined(COS_LINUX)
typedef int CSocket;
typedef sockaddr_in CSockAddr;
#endif
union CBytes // byte container
{
	// Prototypes based on format
	CByte8s svalue;
	CByte8u uvalue;
	float fvalue;
	double dvalue;
	// Storage
	char bytes[8];
};

/* Models */
struct CModel
{
	CModel()
	{
		vertex = NULL;
		texcoord = NULL;
		normal = NULL;
		vertexcnt = 0;
		texture = 0;
		vbo = 0;
	}
	unsigned int vertexcnt;
	CTexture texture;
	float *vertex, *texcoord, *normal;
	unsigned int vbo, vao;
	int primitive, mode;
};

/* Cursor */
struct CCursor
{
	CTexture tex;
	float size[2];
	float origin[2];
};

/* Window */
struct CWindow
{
	unsigned char type;
	char properties;
    char *wndname, *wndtitle, *iconpath;
	unsigned char colorbits, depthbits;
	int pwidth, pheight, posx, posy;
	float background[4];
};
#define CWIN_FULLSCREEN 0
#define CWIN_POPUP 1
#define CWIN_STANDARD 2
#define CWIN_CONSOLE 3
#define CPROP_SOUND 0x01
#define CPROP_SOCKETS 0x02
#define CPROP_GLEXT 0x04
#define CPROP_MODERNGL 0x08

/* Resolution */
struct CResolution
{
	int width, height, freq, bits;
};

/* Fonts */
struct CFont
{
	float width, height;
	CTexture font;
};
#define CFONT_SET 95

/* GUI */
const float CGuicol_s[3]={0.6f, 0.6f, 0.6f}, // Standard (grey)
CGuicol_b[3]={0.1f, 0.1f, 1.0f}, // Blue
CGuicol_g[3]={0.1f, 1.0f, 0.1f}, // Green
CGuicol_r[3]={1.0f, 0.1f, 0.1f}, // Red
CGuicol_y[3]={1.0f, 1.0f, 0.1f}, // Yellow
CGuicol_t[3]={0.6f, 1.0f, 1.0f}, // Turquoise
CGuicol_o[3]={1.0f, 0.7f, 0.2f}, // Orange
CGuicol_br[3]={0.6f, 0.4f, 0.3f}, // Brown
CGuicol_w[3]={1.0f, 1.0f, 1.0f}, // White
CGuicol_d[3]={0.3f, 0.3f, 0.3f}; // Dark
#define CINPUT_ALL 0
#define CINPUT_NONUMBER 1
#define CINPUT_NOLETTER 2
#define CINPUT_NOSYMBOL 4
#define CINPUT_NOSPACE 16
#define CINPUT_PRESSTIME 500
#define CPUSH_IDLE 0
#define CPUSH_CLICKED 1
#define CPUSH_TOUCHED 2
#define CSCROLL_H true
#define CSCROLL_V false

/* Data sizes */
#define CSIZE_MAXPATH 260
#define CSIZE_ERRSTR 256
#define CSIZE_MINUDP 508

/* File IO */
#define CFILE_READ 0
#define CFILE_WRITE 1
#define CFILE_APPEND 2

/* OpenGL */
#define CGL_VBO 0
#define CGL_VAO 1
#define CGL_SHADER 2
#define CGL_VA 3
#define CGL_MIPMAP 4
#define CGL_ANIFILTER 5
#define CGL_COREFEATURES 5
#define CGL_EXTENSIONS 6
#define CSHADER_VERTEX GL_VERTEX_SHADER
#define CSHADER_FRAGMENT GL_FRAGMENT_SHADER
#define CSHADER_GEOMETRY GL_GEOMETRY_SHADER
#define CMODEL_STATIC GL_STATIC_DRAW
#define CMODEL_DYNAMIC GL_DYNAMIC_DRAW
#define CMODEL_TRIANGLES GL_TRIANGLES
#define CMODEL_TRISTRIPS GL_TRIANGLE_STRIP
#define CMODEL_TRIFANS GL_TRIANGLE_FAN
#define CMODEL_LINES GL_LINES
#define CMODEL_POINTS GL_POINTS
#define CTEX_ANIFILTER 1
#define CTEX_MIPMAP 2
#define CTEX_SPRITE 4
#define CTEX_WRAPREPEAT 8

/* Sound */
#define CSOUND_MAXVOL 10000
#define CSOUND_MUTE 0

/* Macros */
#if defined(COS_WIN32)
# ifdef _MSC_VER
#  define CMain int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int iCmdShow)
# else
#  define CMain int main(int args, char** argc)
# endif
#elif defined(COS_LINUX)
# define CMain int main(int args, char** argc)
#endif

/* Sockets */
#define CPROTO_TCP IPPROTO_TCP
#define CPROTO_UDP IPPROTO_UDP
#define CIP4_ADDRSTRLEN 16
#define CIP6_ADDRSTRLEN 46
#define CSOCKET_BLOCKING 0
#define CSOCKET_NONBLOCKING 1
#define CSOCKET_READABLE 0
#define CSOCKET_WRITABLE 1
#if defined(COS_WIN32)
#define WM_SOCKET 104
#define CSOCKET_INVALID INVALID_SOCKET
#define CSOCKET_ERROR SOCKET_ERROR
#define CSOCKET_RECEIVE SD_RECEIVE
#define CSOCKET_SEND SD_SEND
#define CSOCKET_BOTH SD_BOTH
#elif defined(COS_LINUX)
#define CSOCKET_INVALID -1
#define CSOCKET_ERROR -1
#define CSOCKET_RECEIVE SHUT_RD
#define CSOCKET_SEND SHUT_WR
#define CSOCKET_BOTH SHUT_RDWR
#endif
#define CTCP_MSGSIZE 256
#define CTCP_DONE -3
#define CTCP_WAIT -1
#define CTCP_NOCON 0
#define CTCP_ERROR -2
#define CUDP_WAIT -1
#define CUDP_ERROR -2

struct CTcpSession
{
	CSocket tcp;
	bool process;
	char buf[CTCP_MSGSIZE];
	short msgpos, msgsize;
	CTcpSession& operator=(CTcpSession other)
	{
		this->tcp = other.tcp;
		this->process = other.process;
		this->msgpos = other.msgpos;
		this->msgsize = other.msgsize;
		memcpy(this->buf, other.buf, CTCP_MSGSIZE);
		return *this;
	}
};

/* Keys */
#define CKEY_TOTAL 258
#if defined(COS_WIN32)
// Letters
#define CKEY_A 0x41
#define CKEY_B 0x42
#define CKEY_C 0x43
#define CKEY_D 0x44
#define CKEY_E 0x45
#define CKEY_F 0x46
#define CKEY_G 0x47
#define CKEY_H 0x48
#define CKEY_I 0x49
#define CKEY_J 0x4A
#define CKEY_K 0x4B
#define CKEY_L 0x4C
#define CKEY_M 0x4D
#define CKEY_N 0x4E
#define CKEY_O 0x4F
#define CKEY_P 0x50
#define CKEY_Q 0x51
#define CKEY_R 0x52
#define CKEY_S 0x53
#define CKEY_T 0x54
#define CKEY_U 0x55
#define CKEY_V 0x56
#define CKEY_W 0x57
#define CKEY_X 0x58
#define CKEY_Y 0x59
#define CKEY_Z 0x5A
// Numbers
#define CKEY_0 0x30
#define CKEY_1 0x31
#define CKEY_2 0x32
#define CKEY_3 0x33
#define CKEY_4 0x34
#define CKEY_5 0x35
#define CKEY_6 0x36
#define CKEY_7 0x37
#define CKEY_8 0x38
#define CKEY_9 0x39
// Symbols
#define CKEY_SPACE 0x20
#define CKEY_BACKSPACE 0x08
#define CKEY_TAB 0x09
#define CKEY_ENTER 0x0D
#define CKEY_RIGHT 0x27
#define CKEY_LEFT 0x25
#define CKEY_DOWN 0x28
#define CKEY_UP 0x26
#define CKEY_SEMICOLON 0xBA
#define CKEY_DIVIDE 0xBF
#define CKEY_TILDE 0xC0
#define CKEY_RBRACKET 0xDD
#define CKEY_LBRACKET 0xDB
#define CKEY_SLASH 0xBF
#define CKEY_BACKSLASH 0xDC
#define CKEY_QUOTE 0xDE
#define CKEY_MINUS 0xBD
#define CKEY_PLUS 0xBB
#define CKEY_PERIOD 0xBE
#define CKEY_COMMA 0xBC
// Modifier
#define CKEY_RSHIFT 0x10
#define CKEY_LSHIFT 0x10
#define CKEY_RCTRL 0x11
#define CKEY_LCTRL 0x11
#define CKEY_ESC 0x1B
// F-keys
#define CKEY_F1 0x70
#elif defined(COS_LINUX)
// Letters
extern unsigned short CKEY_A, CKEY_B, CKEY_C, CKEY_D, CKEY_E, CKEY_F, CKEY_G, CKEY_H, CKEY_I, CKEY_J, CKEY_K, CKEY_L,
CKEY_M, CKEY_N, CKEY_O, CKEY_P, CKEY_Q, CKEY_R, CKEY_S, CKEY_T, CKEY_U, CKEY_V, CKEY_W, CKEY_X, CKEY_Y, CKEY_Z, CKEY_0,
CKEY_1, CKEY_2, CKEY_3, CKEY_4, CKEY_5, CKEY_6, CKEY_7, CKEY_8, CKEY_9, CKEY_SPACE, CKEY_BACKSPACE, CKEY_TAB, CKEY_ENTER,
CKEY_RIGHT, CKEY_LEFT, CKEY_DOWN, CKEY_UP, CKEY_SEMICOLON, CKEY_DIVIDE, CKEY_TILDE, CKEY_RBRACKET, CKEY_LBRACKET,
CKEY_SLASH, CKEY_BACKSLASH, CKEY_QUOTE, CKEY_MINUS, CKEY_PLUS, CKEY_PERIOD, CKEY_COMMA, CKEY_RSHIFT, CKEY_LSHIFT, CKEY_RCTRL,
CKEY_LCTRL, CKEY_ESC, CKEY_F1;
#endif
// Mouse
#define CKEY_LMOUSE 256
#define CKEY_RMOUSE 257

/* CEngine declaration */
class CAPI CEngine
{
    // Base variables
#if defined(COS_WIN32)
	HINSTANCE hInstance, basslib;
	HWND hWnd;
	HDC hDC;
	HGLRC hRC;
	MSG msg;
	static LRESULT CALLBACK CWndProc(HWND, UINT, WPARAM, LPARAM);
	LRESULT CProcWindow(UINT, WPARAM, LPARAM);
	unsigned long long freq;
	char *wndname;
	bool moderncontext, winsock;
	DEVMODE* mode;
#elif defined(COS_LINUX)
	Display* hDis;
    Window fake, hWin, hRoot;
    XVisualInfo* hVi;
    Colormap cMap;
    XSetWindowAttributes wAtt;
    GLXContext hCx;
    XEvent hEvent;
    int hScr;
    Pixmap hIcon;
    Cursor hide, hCursor;
	XF86VidModeModeInfo** mode;
#endif
	// Engine properties
	unsigned short maxwidth, maxheight;
	float maxAF;
	char inerr[CSIZE_ERRSTR];
	bool fullscreen;
	// GUI variables
	bool toggle, prevclick;
	CByte8u inputtime;
	float rectx, recty, xorg, yorg;
	CModel guimodel;
public:
	// Window management/input
	bool key[CKEY_TOTAL], focus;
	int mx, my;
	float cmx, cmy;
	int width, height;
	int xpos, ypos;
	// Engine properties
	unsigned char glv[2], glslv[2], sndv[4];
	bool glcorefeature[CGL_COREFEATURES], glextensions[CGL_EXTENSIONS];
	int bestres, rescount;
	CResolution* resolution;
	// Engine
	bool CSetupEngine(CWindow* attribs);
	bool CCleanupEngine();
	void CGetVersion(unsigned char *major, unsigned char *minor, signed char *revision);
	void CDeleteResource(unsigned char*, bool isarray);
	int CSearchDir(char *path, const char *format, char*** filestr);
	bool COpenFile(const char *path, char** buf, size_t* bufsize, unsigned char mode, bool binary);
	bool CParseData(char *inbuf, char *outbuf, char *stopchars, unsigned int readsize, unsigned int outsize, unsigned int *progress);
	bool CAbsolutePath(char *storage, size_t size);
	void CShiftCharArray(char *str, int shift, unsigned int size, char fillup = 0);
	char CKeyToChar(unsigned short key, bool shift);
	char *CGetLastError();
	// Window
	void CMoveWindow(int posx, int posy, int pwidth = -1, int pheight = -1);
	void CToggleCursor(bool visible);
	void CProcessMessage();
	void CMsgBox(const char *text, const char *title);
	void CSetCursor(unsigned short x, unsigned short y);
	CByte8u CGetTime();
	void CSwapBuffers();
	void CSleep(unsigned int ms);
	void CMinimize(bool mz);
	// Sound
	bool CLoadSample(const char *path, CSample& sample, unsigned long maxplay);
	bool CCreateSound(CSample sample, CSound& chan, bool loop = false);
	void CClearSound(CSound chan);
	void CClearSample(CSample sample);
	void CStartSound(CSound sound);
	void CPauseSound(CSound sound);
	void CStopSound(CSound sound);
	void CSoundVol(const unsigned short volume);
	// OpenGL
	bool CCheckExtension(const char *name);
	void CResize2D(int swidth, int sheight, double xlength = 10.0f, double ylength = 10.0f, double ox = 0.0, double oy = 0.0);
	void CResize3D(int swidth, int sheight, double fov, double zfar);
	bool CLoadBMP(const char *path, CTexture &texnum);
	bool CLoadTGA(const char *path, CTexture &texnum);
	void CSetTexture(CTexture &texnum, char properties);
	void CClearTexture(CTexture *tex, unsigned int count);
	bool CLoadOBJ(const char *path, CModel& model);
	void CCreateModel(CModel& model, int primitive, int mode);
	void CDeleteModel(CModel* model);
	void CRender3D(CModel& model);
	void CRender2D(CModel& model);
	void CVerticalSync(bool on);
	bool CLoadShader(const char *data, unsigned int type, unsigned int& id, char** infolog);
	bool CCreateShader(unsigned int vshader, unsigned int fshader, unsigned int& shaderprogram);
	void CUseShader(unsigned int shaderprogram);
	void CDeleteShader(unsigned int vshader, unsigned int fshader, unsigned int shaderprogram);
	// Socket
	bool CCreateSocket(int protocol, CSocket *s);
	void CCloseSocket(CSocket s);
	bool CSocketMode(CSocket s, unsigned long mode);
	int CConnectSocket(CSocket s, unsigned short port, char *ipaddress);
	bool CShutdownSocket(CSocket s, int what);
	bool CBindSocket(CSocket s, unsigned short port);
	bool CListenSocket(CSocket s, int connections);
	int CAcceptConnection(CSocket s, CSocket *client, char *ipaddress);
	int CSend(CSocket s, char *buf, int bytes);
	int CReceive(CSocket s, char *buf, int bufsize);
	void CSetSockAddr(CSockAddr* address, unsigned short port, char *ipaddress);
	int CSendTo(CSocket s, CSockAddr* address, char *buf, int bytes);
	int CRecvFrom(CSocket s, CSockAddr* from, char *buf, int bufsize);
	bool CVerifySocket(CSocket *s, unsigned char mode, unsigned int count, long msec);
	bool CSetupTcpSession(CTcpSession* net);
	void CCloseTcpSession(CTcpSession* net);
	int CSendMsg(CTcpSession* net, char *msg, unsigned char size);
	int CRecvMsg(CTcpSession* net, char *msg, unsigned char *size);
	// GUI
	bool CCreateFont(const char *path, float fwidth, float fheight, CFont &font);
	void CClearFont(CFont *font);
	bool CCreateCursor(CCursor &cursor, const char *path, float cwidth, float cheight, float cox = 0.0f, float coy = 0.0f);
	void CDrawCursor(CCursor &cursor);
	void CClearCursor(CCursor* cursor);
	void CPrintValue(const float value, CFont font, unsigned char precision, float xpos, float ypos, const float color[3] = CGuicol_w);
	void CPrintText(const char*str, CFont font, float xpos, float ypos, const float color[3] = CGuicol_w);
	void CScroll(bool type, float xpos, float ypos, float length, CTexture mask, float *bufptr, float minval, float range);
	void CInputbar(float xpos, float ypos, unsigned short maxchar, char *str, CFont font, CTexture masks[3], bool* clicked, unsigned short* charpos, unsigned char mode = CINPUT_ALL, const float barcolor[3] = CGuicol_w, const float textcolor[3] = CGuicol_d);
	unsigned char CPushbutton(const char *str, CFont font, CTexture masks[3], CSample sample, float xpos, float ypos, const float buttoncolor[3] = CGuicol_b, const float textcolor[3] = CGuicol_w);
};

/* Collision detection */
namespace CVector2D
{
	// Types
	struct CVector
	{
		CVector() {};
		CVector(float tx, float ty) : x(tx), y(ty) {};
		void operator+=(CVector other)
		{
			CVector& tmp(*this);
			tmp.x += other.x;
			tmp.y += other.y;
		}
		void operator-=(CVector other)
		{
			CVector& tmp(*this);
			tmp.x -= other.x;
			tmp.y -= other.y;
		}
		void operator/=(float denom)
		{
			CVector& tmp(*this);
			tmp.x /= denom;
			tmp.y /= denom;
		}
		void operator*=(float factor)
		{
			CVector& tmp(*this);
			tmp.x *= factor;
			tmp.y *= factor;
		}
		CVector operator+(CVector other)
		{
			CVector tmp;
			tmp.x = this->x + other.x;
			tmp.y = this->y + other.y;
			return tmp;
		}
		CVector operator-(CVector other)
		{
			CVector tmp;
			tmp.x = this->x - other.x;
			tmp.y = this->y - other.y;
			return tmp;
		}
		CVector operator*(float factor)
		{
			CVector tmp;
			tmp.x = this->x * factor;
			tmp.y = this->y * factor;
			return tmp;
		}
		bool operator!=(const CVector other)
		{
			CVector& tmp(*this);
			if (tmp.x != other.x || tmp.y != other.y)
				return true;
			return false;
		}
		bool operator==(const CVector other)
		{
			CVector& tmp(*this);
			if (tmp.x == other.x && tmp.y == other.y)
				return true;
			return false;
		}
		float magnitude()
		{
			CVector& tmp(*this);
			return sqrt(tmp.x * tmp.x + tmp.y * tmp.y);
		}
		void normalize()
		{
			CVector& tmp(*this);
			float divider = tmp.magnitude();
			if (divider != 1.0f)
				tmp /= divider;
		}
		float dot(CVector other)
		{
			CVector& tmp(*this);
			return tmp.x * other.x + tmp.y * other.y;
		}
		float cross(CVector other)
		{
			CVector& tmp(*this);
			return tmp.x * other.y - tmp.y * other.x;
		}
		CVector project(CVector dir)
		{
			CVector& tmp(*this);
			float a = tmp.dot(dir);
			return dir * a;
		}
		float sqdist(CVector other)
		{
			CVector& tmp(*this);
			float ax = tmp.x - other.x, ay = tmp.y - other.y;
			return ax * ax + ay * ay;
		}
		float x, y;
	};
	struct CLine
	{
		CVector pos, dir;
	};
	struct CCircle
	{
		CVector pos;
		float radius;
	};
	struct CTriangle
	{
		CLine l[3];
	};
	struct CRectangle
	{
		CLine l[4];
	};
	CAPI extern const CVector CNullvector;
	// Functions, (type) collides with (type)
	CAPI extern bool CLine_Line(CLine line, CLine line2, CVector* intersect);
	CAPI extern bool CCircle_Circle(CCircle circle, CCircle circle2, CVector* response);
	CAPI extern bool CTriangle_Triangle(CTriangle triangle, CTriangle triangle2, CVector* response);
	CAPI extern bool CRectangle_Rectangle(CRectangle rectangle, CRectangle rectangle2, CVector* response);
	CAPI extern bool CLine_Circle(CLine line, CCircle circle, CVector* response);
	CAPI extern bool CLine_Rectangle(CLine line, CRectangle rectangle, CVector* intersect);
	CAPI extern bool CLine_Triangle(CLine line, CTriangle triangle, CVector* response);
	CAPI extern bool CCircle_Rectangle(CCircle sphere, CRectangle rectangle, CVector* response);
	CAPI extern bool CCircle_Triangle(CCircle sphere, CTriangle triangle, CVector* response);
}

namespace CVector3D
{
	// Types
	struct CVector
	{
		CVector() {};
		CVector(float tx, float ty, float tz) : x(tx), y(ty), z(tz) {};
		void operator+=(CVector& other)
		{
			CVector& tmp(*this);
			tmp.x += other.x;
			tmp.y += other.y;
			tmp.z += other.z;
		}
		void operator-=(CVector& other)
		{
			CVector& tmp(*this);
			tmp.x -= other.x;
			tmp.y -= other.y;
			tmp.z -= other.z;
		}
		void operator/=(float denom)
		{
			CVector& tmp(*this);
			tmp.x /= denom;
			tmp.y /= denom;
			tmp.z /= denom;
		}
		void operator*=(float factor)
		{
			CVector& tmp(*this);
			tmp.x *= factor;
			tmp.y *= factor;
			tmp.z *= factor;
		}
		CVector operator+(CVector other)
		{
			CVector tmp;
			tmp.x = this->x + other.x;
			tmp.y = this->y + other.y;
			tmp.z = this->z + other.z;
			return tmp;
		}
		CVector operator-(CVector other)
		{
			CVector tmp;
			tmp.x = this->x - other.x;
			tmp.y = this->y - other.y;
			tmp.z = this->z - other.z;
			return tmp;
		}
		CVector operator*(float factor)
		{
			CVector tmp;
			tmp.x = this->x * factor;
			tmp.y = this->y * factor;
			tmp.z = this->z * factor;
			return tmp;
		}
		bool operator!=(const CVector& other)
		{
			CVector& tmp(*this);
			if (tmp.x != other.x || tmp.y != other.y || tmp.z != other.z)
				return true;
			else
				return false;
		}
		float magnitude()
		{
			CVector&tmp(*this);
			return sqrt(tmp.x*tmp.x+tmp.y*tmp.y+tmp.z*tmp.z);
		}
		void normalize()
		{
			CVector& tmp(*this);
			float divider = tmp.magnitude();
			if (divider != 1.0f)
				tmp /= divider;
		}
		float dot(CVector& other)
		{
			CVector& tmp(*this);
			return (tmp.x * other.x) + (tmp.y * other.y) + (tmp.z * other.z);
		}
		CVector cross(CVector& other)
		{
			CVector& tmp(*this);
			CVector result(tmp.y * other.z - tmp.z  * other.y, tmp.z * other.x - tmp.x  * other.z, tmp.x * other.y - tmp.y  * other.x);
			return result;
		}
		float distance(CVector& other)
		{
			CVector& tmp(*this);
			float ax = tmp.x - other.x, ay = tmp.y - other.y, az = tmp.z - other.z;
			return sqrt(ax * ax + ay * ay + az * az);
		}
		float x, y, z;
	};
	struct CTriangle
	{
		CVector a, b, c;
		CVector normal;
		float pconst;
	};
	struct CSphere
	{
		CVector pos;
		float radius;
	};
	CAPI extern const CVector CNullvector;
	// Functions
	CAPI extern CVector CSphere_Triangle(CTriangle& triangle, CSphere& sphere);
	//CAPI CVector CSphere_Rect(CSphere S, cRect R);
}
#endif
