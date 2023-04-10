/*
Copyright (c) 2016, David Liu
All rights reserved. Read the license for more information.
*/

/* Definitions */
#define SETUP_ALIVE 1000
#define FPS_PERIOD 100

struct Bench
{
	// Data
	char** names;
	char msgbuffer[CSIZE_ERRSTR], msgpath[SCRIPT_PATH];
	unsigned int setupcmdcnt, rendercmdcnt, processcmdcnt;
	size_t length;
	unsigned char *setupcmds, *rendercmds, *processcmds;
	unsigned int modelcnt, texcnt, samplecnt;
	unsigned char varcnt;
	CTexture* tex;
	CSound* sound;
	CSample* sample;
	int frame;
	CModel* model;
	char** lines;
	float fps, avgfps, fpspeak;
	double fov, zfar;
	CByte8u savtime;
	int reswidth, resheight;
	unsigned int cntfps, fpsproc;
	float badfps, goodfps;
	// Storage for variables
	unsigned int floatptrcnt;
	float** floatptr;
	unsigned char varorder;
	// Storage for arguments
	unsigned int intsavcnt, floatsavcnt, charsavcnt;
	unsigned int sintcnt, sfloatcnt, scharcnt;
	unsigned int sintcnt2, sfloatcnt2, scharcnt2;
	bool charalloc;
	int *intsav;
	float *floatsav;
	char** charsav;
};

/* Objects */
extern Bench bench;

/* Functions */
extern void SetupScripter();
extern void CleanBenchmark();
extern void StartBenchmark(unsigned char cnt);
extern void QuitBenchmark();
extern void BenchRender(float interpolate);
extern void BenchProcess();

/*
Identity matrix:
camera looking into -z axes.
x axes if left/right.
y axes is up/down.
*/