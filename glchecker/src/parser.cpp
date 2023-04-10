/*
Copyright (c) 2016, David Liu
All rights reserved. Read the license for more information.
*/

/* Headers */
#include "CEngine.h"
#include "glchecker.h"
#include "parser.h"
#include "benchmark.h"

/* Objects*/
Function cmdfunction[TOTALCMDTYPES];
Variable* vars;

/* Functions */
bool CheckFunction(const char *line, unsigned int curline) // Count and initialize functions
{
	char funcname[SCRIPT_FUNC];
	unsigned char i, loop = 0;
	short funcid = -1;
	char atline[14];
	unsigned int k;
	sprintf(atline, "%d", curline + 1);
	// Function name
	CLoops(i, 0, strlen(line) + 1)
	{
		if (isspace(line[i]) || line[i] == '\0')
		{
			funcname[loop] = '\0';
			break;
		}
		if (i == SCRIPT_FUNC - 1)
		{
			strcpy(bench.msgbuffer, "In line ");
			strcat(bench.msgbuffer, atline);
			strcat(bench.msgbuffer, ": Invalid function name.");
			return false;
		}
		funcname[loop] = line[i];
		loop++;
	}
	CLoops(i, 0, TOTALCMDTYPES)
	{
		if (strcmp(cmdfunction[i].name, funcname) == 0)
		{
			funcid = i;
			break;
		}
	}
	if (funcid == -1)
	{
		strcpy(bench.msgbuffer, "In line ");
		strcat(bench.msgbuffer, atline);
		strcat(bench.msgbuffer, ": Function does not exist.");
		return false;
	}
	else if (bench.charsavcnt == 0 && funcid != CMD_checkversion)
	{
		strcpy(bench.msgbuffer, "First line must specify script version.");
		return false;
	}
	i = SETUPCMDTYPES + RENDERCMDTYPES + PROCESSCMDTYPES;
	loop = SETUPCMDTYPES + RENDERCMDTYPES;
	k = bench.setupcmdcnt + bench.rendercmdcnt + 1;
	if (curline > k && funcid >= loop)
	{
		bench.processcmds[bench.intsavcnt] = funcid;
		bench.intsavcnt++;
	}
	else if (curline > bench.setupcmdcnt && curline < k && funcid >= SETUPCMDTYPES && (funcid < loop || funcid >= i))
	{
		bench.rendercmds[bench.floatsavcnt] = funcid;
		bench.floatsavcnt++;
	}
	else if (curline < bench.setupcmdcnt && (funcid < SETUPCMDTYPES || funcid >= i))
	{
		bench.setupcmds[bench.charsavcnt] = funcid;
		bench.charsavcnt++;
	}
	else
	{
		strcpy(bench.msgbuffer, "In line ");
		strcat(bench.msgbuffer, atline);
		strcat(bench.msgbuffer, ": Function is located in wrong scope.");
		return false;
	}
	// Count functions
	if (funcid == CMD_createvariable)
		bench.varcnt++;
	else if (funcid == CMD_loadobj)
		bench.modelcnt++;
	else if (funcid == CMD_loadbmp || funcid == CMD_loadtga)
		bench.texcnt++;
	else if (funcid == CMD_loadsound)
		bench.samplecnt++;
	return true;
}

unsigned char ParseVariable(unsigned char& curchar, bool& number, const char *line, char *argname)
{
	unsigned char loop = 0, newlines = 0, i;
	bool str = false;
	argname[0] = '\0';
	number = true;
	CLoopc(i, curchar, SCRIPT_PATH + curchar + newlines, 1)
	{
		// Parser for safety of multiple white spaces and detecting dedicated strings
		if ((!str && isspace(line[i])) && argname[0] == '\0')
			continue;
		else if (line[i] == '"')
		{
			str = true;
			continue;
		}
		// Reading arguments
		if ((!str && (isspace(line[i]) || line[i] == '#')) || (str && line[i] == '"') || line[i] == '\0')
		{
			argname[loop] = '\0';
			curchar = i + 1;
			break;
		}
		else
		{
			if (line[i - 1] == '\\' && line[i] == 'n') // newline
			{
				loop--;
				argname[loop] = '\n';
				newlines++;
			}
			else
			{
				argname[loop] = line[i];
				if (number && isdigit(argname[loop]) == 0 && argname[loop] != '.' && (loop == 0 && argname[loop] != '-'))
					number = false;
			}
		}
		// Checks
		if (i == SCRIPT_PATH - 1 + curchar - newlines) // Last character, reserved for \0
			return 1;
		loop++;
	}
	if (argname[0] == '\0') // safety for line endings
		return 2;
	if (line[i] == '\0' || line[i] == '#') // Finished
		return 3;
	return 0;
}

bool CountArguments(const char *line, unsigned int curline) // Count and parse/validate arguments
{
	char funcname[SCRIPT_FUNC], argname[SCRIPT_PATH];
	unsigned char curchar, i, loop = 0, cnt = 0, funcid, e;
	char atline[14];
	sprintf(atline, "%d", curline + 1);
	bool number;
	// Function name
	CLoops(i, 0, strlen(line) + 1)
	{
		if (isspace(line[i]) || line[i] == '\0')
		{
			funcname[loop] = '\0';
			curchar = i + 1;
			break;
		}
		funcname[loop] = line[i];
		loop++;
	}
	CLoops(i, 0, TOTALCMDTYPES)
	{
		if (strcmp(cmdfunction[i].name, funcname) == 0)
		{
			funcid = i;
			break;
		}
	}
	// Read the arguments
	i = curchar;
	while (i < strlen(line))
	{
		// Get the argument of function
		e = ParseVariable(curchar, number, line, argname);
		if (e == 1)
		{
			strcpy(bench.msgbuffer, "In line ");
			strcat(bench.msgbuffer, atline);
			strcat(bench.msgbuffer, ": Argument is too long.");
			return false;
		}
		else if (e == 2)
			break;
		// Check argument count
		if (cnt == strlen(cmdfunction[funcid].format))
		{
			strcpy(bench.msgbuffer, "In line ");
			strcat(bench.msgbuffer, atline);
			strcat(bench.msgbuffer, ": Too many arguments.");
			return false;
		}
		// Determine type and process
		if (cmdfunction[funcid].format[cnt] == 'f') // Floating point number
		{
			if (!number)
			{
				if (strlen(argname) > SCRIPT_VAR - 1)
				{
					strcpy(bench.msgbuffer, "In line ");
					strcat(bench.msgbuffer, atline);
					strcat(bench.msgbuffer, ": Variable name is too long.");
					return false;
				}
			}
			else
				bench.floatsavcnt++;
			bench.floatptrcnt++;
		}
		else if (cmdfunction[funcid].format[cnt] == 's') // String
			bench.charsavcnt++;
		else // Integer
		{
			if (!number)
			{
				strcpy(bench.msgbuffer, "In line ");
				strcat(bench.msgbuffer, atline);
				strcat(bench.msgbuffer, ": Argument must be an integer.");
				return false;
			}
			bench.intsavcnt++;
		}
		cnt++;
		if (e == 3)
			break;
	}
	if (cnt < strlen(cmdfunction[funcid].format))
	{
		strcpy(bench.msgbuffer, "In line ");
		strcat(bench.msgbuffer, atline);
		strcat(bench.msgbuffer, ": Missing arguments.");
		return false;
	}
	return true;
}

bool ReadFunction(const char *line, unsigned int curline) // Collect all arguments
{
	// Init
	unsigned char i, loop = 0, curchar, funcid, cnt = 0, e;
	bool number;
	char funcname[SCRIPT_FUNC], argname[SCRIPT_PATH];
	char atline[14];
	unsigned int k, l;
	sprintf(atline, "%d", curline + 1);
	// Function name
	CLoops(i, 0, strlen(line) + 1)
	{
		if (isspace(line[i]) || line[i] == '\0')
		{
			funcname[loop] = '\0';
			curchar = i + 1;
			break;
		}
		funcname[loop] = line[i];
		loop++;
	}
	CLoops(i, 0, TOTALCMDTYPES)
	{
		if (strcmp(cmdfunction[i].name, funcname) == 0)
		{
			funcid = i;
			break;
		}
	}
	// Read the arguments
	i = curchar;
	while (i < strlen(line))
	{
		// Get the argument of function
		e = ParseVariable(curchar, number, line, argname);
		if (e == 1)
		{
			strcpy(bench.msgbuffer, "In line ");
			strcat(bench.msgbuffer, atline);
			strcat(bench.msgbuffer, ": Argument is too long.");
			return false;
		}
		else if (e == 2)
			break;
		// Determine type and process
		if (cmdfunction[funcid].format[cnt] == 'f') // Floating point number
		{
			if (number)
			{
				sscanf(argname, "%f", &bench.floatsav[bench.floatsavcnt]);
				bench.floatptr[bench.floatptrcnt] = &bench.floatsav[bench.floatsavcnt];
				bench.floatsavcnt++;
			}
			else
			{
				CLoops(loop, 0, bench.varcnt)
				{
					if (strcmp(vars[loop].name, argname) == 0)
					{
						bench.floatptr[bench.floatptrcnt] = &vars[loop].value;
						break;
					}
				}
				if (loop == bench.varcnt)
				{
					strcpy(bench.msgbuffer, "In line ");
					strcat(bench.msgbuffer, atline);
					strcat(bench.msgbuffer, ": Variable not defined.");
					return false;
				}
			}
			bench.floatptrcnt++;
		}
		else if (cmdfunction[funcid].format[cnt] == 's') // String
		{
			if (!bench.charalloc)
				bench.charalloc = true;
			bench.charsav[bench.charsavcnt] = new char[strlen(argname) + 1];
			strcpy(bench.charsav[bench.charsavcnt], argname);
			bench.charsavcnt++;
		}
		else // Integer
		{
			sscanf(argname, "%d", &bench.intsav[bench.intsavcnt]);
			bench.intsavcnt++;
		}
		cnt++;
		if (e == 3)
			break;
	}
	if (funcid == CMD_createvariable) // Execute if createvariable
	{
		CLoops(i, 0, bench.varorder) // Check for duplicate
		{
			if (strcmp(bench.charsav[bench.charsavcnt - 1], vars[i].name) == 0)
			{
				strcpy(bench.msgbuffer, "In line ");
				strcat(bench.msgbuffer, atline);
				strcat(bench.msgbuffer, ": Variable already defined.");
				return false;
			}
		}
		strcpy(vars[bench.varorder].name, bench.charsav[bench.charsavcnt - 1]);
		vars[bench.varorder].value = (*bench.floatptr[bench.floatptrcnt - 1]);
		bench.varorder++;
	}
	else if (funcid == CMD_jump || funcid == CMD_if) // Check for illegal code jumps
	{
		// Jump 0 does not do anything dangerous, equivalent to an empty command
		i = SETUPCMDTYPES + RENDERCMDTYPES + PROCESSCMDTYPES;
		loop = SETUPCMDTYPES + RENDERCMDTYPES;
		k = bench.setupcmdcnt + bench.rendercmdcnt + 1;
		l = curline + bench.intsav[bench.intsavcnt - 1];
		if ((curline > k && (l <= k || l > bench.length)) || 
			(curline > bench.setupcmdcnt && curline < k && (l <= bench.setupcmdcnt || l > k)) ||
			(curline < bench.setupcmdcnt && (l < 0 || l > bench.setupcmdcnt)))
		{
			strcpy(bench.msgbuffer, "In line ");
			strcat(bench.msgbuffer, atline);
			strcat(bench.msgbuffer, ": Jump crosses scope boundaries.");
			return false;
		}
		else if (bench.lines[l][0] == '#')
		{
			strcpy(bench.msgbuffer, "In line ");
			strcat(bench.msgbuffer, atline);
			strcat(bench.msgbuffer, ": Illegal jump to comment.");
			return false;
		}
	}
	return true;
}

bool ParseScript(char *name)
{
	// Initialize
	bench.setupcmds = NULL;
	bench.rendercmds = NULL;
	bench.processcmds = NULL;
	bench.floatptr = NULL;
	bench.intsav = NULL;
	bench.floatsav = NULL;
	bench.charsav = NULL;
	bench.model = NULL;
	bench.tex = NULL;
	bench.sample = NULL;
	bench.sound = NULL;
	bench.charalloc = false;
	vars = NULL;
	unsigned int i;
	char *sav = NULL;
	size_t size, beg, j, k;
	bench.lines = NULL;
	// Read file
	if (!core.COpenFile(RelativePath(name, "glchecker/benchmarks/"), &sav, &size, CFILE_READ, true))
	{
		strcpy(bench.msgbuffer, core.CGetLastError());
		return false;
	}
	// Count lines
	bench.length = 0;
	CLoops(k, 0, size)
	{
		if (sav[k] == '\n')
			bench.length++;
		else if (sav[k] == '\r')
		{
			strcpy(bench.msgbuffer, "Script file line endings are DOS type.");
			core.CDeleteResource((unsigned char*)sav, true);
			return false;
		}
	}
	bench.lines = new char*[bench.length];
	// Allocate arrays and turn data into lines
	beg = j = 0;
	CLoops(k, 0, size)
	{
		if (sav[k] == '\n')
		{
			bench.lines[j] = new char[k - beg + 1];
			i = beg;
			beg = k + 1;
			CLoops(k, i, beg - 1)
			{
				bench.lines[j][k - i] = sav[k];
			}
			bench.lines[j][beg - 1 - i] = '\0';
			k = beg - 1;
			j++;
		}
	}
	delete[] sav;
	// Commands and comment count plus allocation
	j = k = bench.length; // Counters
	bench.intsavcnt = bench.floatsavcnt = bench.charsavcnt = 0; // use as comment counters
	CLoops(i, 0, bench.length)
	{
		if (bench.lines[i][0] == '#')
			bench.intsavcnt++;
		else if (strcmp(bench.lines[i], "render:") == 0)
		{
			j = i;
			break;
		}
	}
	if (j == bench.length)
	{
		strcpy(bench.msgbuffer, "No render entry detected.");
		return false;
	}
	CLoops(i, j + 1, bench.length)
	{
		if (bench.lines[i][0] == '#')
			bench.floatsavcnt++;
		else if (strcmp(bench.lines[i], "process:") == 0)
		{
			k = i;
			break;
		}
	}
	if (k == bench.length)
	{
		strcpy(bench.msgbuffer, "No process entry detected.");
		return false;
	}
	CLoops(i, k + 1, bench.length)
	{
		if (bench.lines[i][0] == '#')
			bench.charsavcnt++;
	}
	bench.setupcmdcnt = j;
	bench.rendercmdcnt = k - 1 - j;
	bench.processcmdcnt = bench.length - 1 - k;
	unsigned int s = bench.setupcmdcnt - bench.intsavcnt, r = bench.rendercmdcnt - bench.floatsavcnt, p = bench.processcmdcnt - bench.charsavcnt;
	if (s > 0)
		bench.setupcmds = new unsigned char[s];
	if (r > 0)
		bench.rendercmds = new unsigned char[r];
	if (p > 0)
		bench.processcmds = new unsigned char[p];
	if (s == 0) // allocated first
	{
		strcpy(bench.msgbuffer, "Missing script version specifier.");
		return false;
	}
	// Parse functions
	bench.intsavcnt = bench.floatsavcnt = bench.charsavcnt = 0; // use as temp storage counters
	bench.modelcnt = bench.texcnt = bench.samplecnt = bench.varcnt = 0;
	k = bench.setupcmdcnt + bench.rendercmdcnt + 1;
	CLoops(i, 0, bench.length)
	{
		if (i != bench.setupcmdcnt && i != k && bench.lines[i][0] != '#')
		{
			if (!CheckFunction(bench.lines[i], i))
				return false;
		}
	}
	// Allocate variables
	if (bench.varcnt > 0)
		vars = new Variable[bench.varcnt];
	// Parse arguments
	bench.floatptrcnt = bench.intsavcnt = bench.floatsavcnt = bench.charsavcnt = 0;
	CLoops(i, 0, bench.length)
	{
		if (i != bench.setupcmdcnt && i != k && bench.lines[i][0] != '#')
		{
			if (!CountArguments(bench.lines[i], i))
				return false;
			
		}
		else if (i == bench.setupcmdcnt) // Store counters
		{
			bench.sintcnt = bench.intsavcnt;
			bench.sfloatcnt = bench.floatptrcnt;
			bench.scharcnt = bench.charsavcnt;
		}
		else if (i == k)
		{
			bench.sintcnt2 = bench.intsavcnt;
			bench.sfloatcnt2 = bench.floatptrcnt;
			bench.scharcnt2 = bench.charsavcnt;
		}
	}
	// Allocate argument data
	if (bench.floatptrcnt > 0)
		bench.floatptr = new float*[bench.floatptrcnt];
	if (bench.floatsavcnt > 0)
		bench.floatsav = new float[bench.floatsavcnt];
	if (bench.intsavcnt > 0)
		bench.intsav = new int[bench.intsavcnt];
	if (bench.charsavcnt > 0)
		bench.charsav = new char*[bench.charsavcnt];
	// Assign code
	bench.floatptrcnt = bench.intsavcnt = bench.floatsavcnt = bench.charsavcnt = 0;
	bench.varorder = 0;
	CLoops(i, 0, bench.length)
	{
		if (i != bench.setupcmdcnt && i != k && bench.lines[i][0] != '#')
		{
			if (!ReadFunction(bench.lines[i], i))
				return false;
		}
	}
	// Delete buffer
	CLoops(i, 0, bench.length)
	{
		delete[] bench.lines[i];
	}
	delete[] bench.lines;
	bench.lines = NULL;
	// Set the correct counts
	bench.setupcmdcnt = s;
	bench.rendercmdcnt = r;
	bench.processcmdcnt = p;
	return true;
}
