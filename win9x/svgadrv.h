#ifndef __SVGADRV_H__INCLUDED__
#define __SVGADRV_H__INCLUDED__

#ifndef SVGA
#define SVGA
#endif

#include "3d_accel.h"

#ifdef __cplusplus
extern "C" {
#endif

struct svga_cotable_entry
{
	SVGACOTableType type;
	uint32_t cbItem;
	uint32_t count;
	uint32_t gmr_id;
};

typedef struct svga_cotable
{
	struct svga_cotable_entry item[SVGA_COTABLE_MAX];
} svga_cotable_t;

#define CMD_BUFFER_COUNT 4

typedef struct _svga_inst_t
{
	FBHDA_t *hda;
	uint32_t ctx_id;
	uint32_t pid;
	
	SVGA_DB_t *db;
	void              *cmd_buf[CMD_BUFFER_COUNT];
	SVGA_CMB_status_t  cmd_stat[CMD_BUFFER_COUNT];
	int                cmd_next;
	int                cmd_act;
	size_t             cmd_pos;
	uint32_t           cmd_fence;
	
	uint32_t last_seen_fence;
	uint32_t last_complete_fence;
	
	uint32_t softblit_gmr_id;
	void    *softblit_gmr_ptr;
	uint32_t softblit_gmr_size;

	BOOL dx;

	/* (pseudo)v-sync variables */
	ULARGE_INTEGER lastframe; /* last frame timestamp (FILETIME) */
	uint64_t delta;           /* difference between Sleep input and real Sleep time */
	/* latch for creating one single and persistant GB content */
	BOOL have_cb_context;
	uint32_t blitsid;
	
	
} svga_inst_t;

BOOL IsSVGA(HDC gdi_ctx);

BOOL SVGAReadReg(svga_inst_t *svga, uint32_t reg, uint32_t *val);

uint32_t SVGAFenceInsert(svga_inst_t *svga);
uint32_t SVGAFenceInsertCB(svga_inst_t *svga);
void SVGAFenceSync(svga_inst_t *svga, uint32_t fence);
BOOL SVGAFenceQuery(svga_inst_t *svga, uint32_t fence, uint32_t *fenceStatus, uint32_t *lastPassed, uint32_t *nextFence);

void SVGAFullSync(svga_inst_t *svga);

BOOL SVGACreate(svga_inst_t *svga);
void SVGADestroy(svga_inst_t *svga);
void SVGACleanup(svga_inst_t *svga, uint32_t pid);

void SVGAStart(svga_inst_t *svga);
void SVGAPush(svga_inst_t *svga, const void *cmd, const size_t size);
void *SVGAPull(svga_inst_t *svga, const size_t size);
void SVGAFinish(svga_inst_t *svga, DWORD flags, DWORD DXCtxId);
void SVGASend(svga_inst_t *svga, const void *cmd, const size_t size, DWORD flags, DWORD DXCtxId);

uint32_t SVGARegionCreate(svga_inst_t *svga, uint32_t size, uint32_t *address);
void SVGARegionDestroy(svga_inst_t *svga, uint32_t regionId);

uint32_t SVGAContextCreate(svga_inst_t *svga);
void SVGACBContextCreate(svga_inst_t *svga);
void SVGAContextDestroy(svga_inst_t *svga, uint32_t cid);

BOOL SVGAContextCotableCreate(svga_inst_t *svga, uint32_t cid);
void SVGAContextCotableDestroy(svga_inst_t *svga, uint32_t cid);
BOOL SVGAContextCotableUpdate(svga_inst_t *svga, uint32_t cid, SVGACOTableType type, uint32_t destId);

BOOL SVGASurfaceInfo(svga_inst_t *svga, uint32_t sid, uint32_t *pWidth, uint32_t *pHeight, uint32_t *pBpp, uint32_t *pPitch);
void SVGASurfaceDestroy(svga_inst_t *svga, uint32_t sid);

void SVGAPresent(svga_inst_t *svga, HDC hDC, uint32_t cid, uint32_t sid);

void SVGAPresentWindow(svga_inst_t *svga, HDC hDC, uint32_t cid, uint32_t sid);
void SVGAPresentWinBlt(svga_inst_t *svga, HDC hDC, uint32_t cid, uint32_t sid);
void SVGACompose(svga_inst_t *svga, uint32_t cid, uint32_t srcSid, uint32_t destSid, LPCRECT pRect);

#ifdef DEBUG
void svga_printf(svga_inst_t *svga, const char *fmt, ...);
#else
#define svga_printf(...)
#endif

void SVGAZombieKiller();

//#define SVGA_CB_CONTEXT_DEFAULT SVGA_CB_CONTEXT_0

#ifndef NO_VBOX_H
BOOL SVGAReadHwInfo(svga_inst_t *ctx, VBOXGAHWINFO *pHwInfo);
BOOL SVGASurfaceGBCreate(svga_inst_t *svga, SVGAGBSURFCREATE *pCreateParms);
BOOL SVGASurfaceCreate(svga_inst_t *svga, GASURFCREATE *pCreateParms, GASURFSIZE *paSizes, uint32_t cSizes, uint32_t *outSid);

struct pipe_screen;

struct pipe_screen *         WINAPI GaDrvScreenCreate(const WDDMGalliumDriverEnv *pEnv);
void                         WINAPI GaDrvScreenDestroy(struct pipe_screen *s);
uint32_t                     WINAPI GaDrvGetSurfaceId(struct pipe_screen *pScreen, struct pipe_resource *pResource);
const WDDMGalliumDriverEnv * WINAPI GaDrvGetWDDMEnv(struct pipe_screen *pScreen);
uint32_t                     WINAPI GaDrvGetContextId(struct pipe_context *pPipeContext);
void                         WINAPI GaDrvContextFlush(struct pipe_context *pPipeContext);
const WDDMGalliumDriverEnv * WINAPI GaDrvCreateEnv(svga_inst_t *svga);

#endif

/******************************************************************************
 *                                                                            *
 *                  some missings due old headers                             *
 *                                                                            *
 ******************************************************************************/

typedef enum {
   SVGA_CB_DX_FLAG_NONE       = 0,
   SVGA_CB_DX_FLAG_NO_IRQ     = 1 << 0,
   SVGA_CB_DX_FLAG_DX_CONTEXT = 1 << 1,
   SVGA_CB_DX_FLAG_MOB        = 1 << 2,
   SVGA_CB_DX_FLAG_FORCE_UINT = MAX_UINT32,
} SVGACBFlagsDX;

#pragma pack(push)
#pragma pack(1)
typedef struct _SVGACBHeaderDX {
	volatile SVGACBStatus status; /* Modified by device. */
	volatile uint32 errorOffset;  /* Modified by device. */
	uint64 id;
	SVGACBFlagsDX flags;
	uint32 length;
	union {
		PA pa;
		struct {
			SVGAMobId mobid;
			uint32 mobOffset;
		} mob;
	} ptr;
	uint32 offset; /* Valid if CMD_BUFFERS_2 cap set, must be zero otherwise, modified by device. */
	uint32 dxContext; /* Valid if DX_CONTEXT flag set, must be zero otherwise */
	uint32 mustBeZero[6];
} SVGACBHeaderDX;

#define SVGA_3D_CMD_DEFINE_GB_SURFACE_V4		1267
/*
 * Defines a guest-backed surface, adding buffer byte stride.
 */
typedef
struct SVGA3dCmdDefineGBSurface_v4 {
   uint32 sid;
   SVGA3dSurfaceAllFlags surfaceFlags;
   SVGA3dSurfaceFormat format;
   uint32 numMipLevels;
   uint32 multisampleCount;
   SVGA3dMSPattern multisamplePattern;
   SVGA3dMSQualityLevel qualityLevel;
   SVGA3dTextureFilter autogenFilter;
   SVGA3dSize size;
   uint32 arraySize;
   uint32 bufferByteStride;
}
SVGA3dCmdDefineGBSurface_v4;   /* SVGA_3D_CMD_DEFINE_GB_SURFACE_V4 */

#if MESA_MAJOR < 23

/* SVGA3dUAView */

#define SVGA3D_UABUFFER_RAW     (1 << 0)
#define SVGA3D_UABUFFER_APPEND  (1 << 1)
#define SVGA3D_UABUFFER_COUNTER (1 << 2)
typedef uint32 SVGA3dUABufferFlags;

typedef
struct {
   union {
      struct {
         uint32 firstElement;
         uint32 numElements;
         SVGA3dUABufferFlags flags;
         uint32 padding0;
         uint32 padding1;
      } buffer;
      struct {
         uint32 mipSlice;
         uint32 firstArraySlice;
         uint32 arraySize;
         uint32 padding0;
         uint32 padding1;
      } tex;  /* 1d, 2d */
      struct {
         uint32 mipSlice;
         uint32 firstW;
         uint32 wSize;
         uint32 padding0;
         uint32 padding1;
      } tex3D;
   };
}
SVGA3dUAViewDesc;

typedef
struct {
   SVGA3dSurfaceId sid;
   SVGA3dSurfaceFormat format;
   SVGA3dResourceType resourceDimension;
   SVGA3dUAViewDesc desc;
   uint32 structureCount;
   uint32 pad[7];
}
SVGACOTableDXUAViewEntry;

#define SVGA_3D_CMD_DX_DEFINE_UA_VIEW 1245

#else
#define SVGA3D_MOBFMT_PTDEPTH_2 SVGA3D_MOBFMT_PT_2
#endif


#pragma pack(pop)


#ifdef __cplusplus
}
#endif

#endif /* __SVGADRV_H__INCLUDED__ */
