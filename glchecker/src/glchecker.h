/*
Copyright (c) 2016, David Liu
All rights reserved. Read the license for more information.
*/

/* Definitions */
#define APP_WIDTH 550
#define APP_HEIGHT 350
#define APP_FRAME 20
#define APP_FONTS 3
#define FONT_PUSH 0
#define FONT_DATA 1
#define FONT_TEXT 2
#define APP_MASKS 4
#define MASK_LPUSH 0
#define MASK_MPUSH 1
#define MASK_RPUSH 2
#define MASK_BUTTON 3
#define STATUS_RUN 0
#define STATUS_FAIL 1
#define STATUS_QUIT 2
#define GLCHECK_VERSION "1.2"

struct Application
{
	unsigned char page, status;
	char temppath[CSIZE_MAXPATH], rootpath[CSIZE_MAXPATH];
	CFont font[APP_FONTS];
	CCursor cursor;
	CSample sample;
	CTexture credit, mask[APP_MASKS];
	unsigned char cmaj, cmin;
	signed char crev;
	float barcol[3];
	float extscrl, benchscrl;
	float rot;
	char extfind[31];
	bool extclick, clickmem;
	unsigned short extpos;
	bool opt1;
	char str1[4];
	int benchcnt;
	char** benchnames;
	bool benchmode;
	int pmx, pmy, savxpos, savypos;
	CByte8u prevtime;
	int lefttime;
};

enum{P_MENU,
P_DATA,
P_GLEXT,
P_MARKS,
P_RESULTS,
P_ABOUT};

#define SCRIPT_PATH 101 // 100 chars + \0
#define SCRIPT_FUNC 16 // 15 chars + \0
#define SCRIPT_VAR 11 // 10 chars + \0

/* Objects */
extern CEngine core;
extern Application app;

/* Function */
extern char *RelativePath(const char *path, const char *directory = NULL);
extern void UpdateLoading(const char *txt);
extern void ReportError(const char *error, const char *path = NULL, bool message = false);