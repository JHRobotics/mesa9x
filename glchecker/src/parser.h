/*
Copyright (c) 2016, David Liu
All rights reserved. Read the license for more information.
*/

/* Definitions */
#define TOTALCMDTYPES 31
#define SETUPCMDTYPES 8
#define RENDERCMDTYPES 16
#define PROCESSCMDTYPES 1
#define UNICMDTYPES 6
#define SCRIPT_VERSION "0.1"

enum{CMD_checkversion // mandatory to call
// Setup commands
, CMD_setattributes
, CMD_loadobj
, CMD_loadbmp
, CMD_loadtga
, CMD_loadsound
, CMD_createvariable
, CMD_message
// Render commands
, CMD_rendermodel
, CMD_pushmatrix
, CMD_popmatrix
, CMD_rotate
, CMD_translate
, CMD_scale
, CMD_texture
, CMD_vertex
, CMD_texcoord
, CMD_vertexformat
, CMD_vertexend
, CMD_enable
, CMD_disable
, CMD_lightattribs
, CMD_color
, CMD_intervariable
// Process commands
, CMD_event
// Universal commands
, CMD_playsound
, CMD_cursorpos
, CMD_procvariable
, CMD_if
, CMD_jump
, CMD_movecursor};

struct Function
{
	char *name, *format;
};

struct Variable
{
	char name[SCRIPT_VAR];
	float value;
};

/* Objects */
extern Function cmdfunction[TOTALCMDTYPES];
extern Variable* vars;

/* Functions */
extern bool ParseScript(char *name);