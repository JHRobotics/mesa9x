#ifndef __SVGADRV_H__INCLUDED__
#define __SVGADRV_H__INCLUDED__

#ifdef __cplusplus
extern "C" {
#endif

#pragma pack(push)
#pragma pack(1)

/* SVGA HDA = hardware direct access */
typedef struct _svga_hda_t
{
	uint8_t        *vram_linear;
	uint32_t        vram_pm16;
	uint32_t        vram_physical;
	uint32_t        vram_size;

	volatile uint32_t *fifo_linear;
	uint32_t        fifo_pm16;
	uint32_t        fifo_physical;
	uint32_t        fifo_size;

  uint32_t        ul_flags_index;
  uint32_t        ul_fence_index;
  uint32_t        ul_gmr_start;
  uint32_t        ul_gmr_count;
  uint32_t        ul_ctx_start;
  uint32_t        ul_ctx_count;
  uint32_t        ul_surf_start;
  uint32_t        ul_surf_count;
  uint32_t        userlist_pm16;
  volatile uint32_t *userlist_linear;
  uint32_t        userlist_length;  
} svga_hda_t;

#pragma pack(pop)

#define ULF_DIRTY  0
#define ULF_WIDTH  1
#define ULF_HEIGHT 2
#define ULF_BPP    3
#define ULF_PITCH  4

typedef struct _svga_surfinfo_t
{
	SVGA3dSurfaceFormat format; /* format of surface */
	SVGA3dSize          size;   /* size of face 0    */
} svga_surfinfo_t;

typedef struct _svga_inst_t
{
	svga_hda_t hda;
	HDC dc;
	uint32_t ctx_id;
	uint32_t pid;
	uint32_t softblit_gmr_id;
	void    *softblit_gmr_ptr;
	uint32_t softblit_gmr_size;
	BOOL dx;
	svga_surfinfo_t *surfinfo;
} svga_inst_t;

BOOL IsSVGA(HDC gdi_ctx);

void SVGAFullSync(svga_inst_t *svga);
BOOL SVGACreate(svga_inst_t *svga, HWND win);
void SVGADestroy(svga_inst_t *svga);
BOOL SVGAReadReg(svga_inst_t *svga, uint32_t reg, uint32_t *val);
uint32_t SVGAFenceInsert(svga_inst_t *svga);
BOOL SVGAFencePassed(svga_inst_t *svga, uint32_t fence);
void SVGAFenceSync(svga_inst_t *svga, uint32_t fence);
BOOL SVGAFenceQuery(svga_inst_t *svga, uint32_t fence, uint32_t *fenceStatus, uint32_t *lastPassed, uint32_t *nextFence);
BOOL SVGAFifoWrite(svga_inst_t *svga, void *cmd, size_t cmd_bytes);
uint32_t SVGARegionCreate(svga_inst_t *svga, uint32_t size, uint32_t *address);
void SVGARegionDestroy(svga_inst_t *svga, uint32_t regionId);
uint32_t SVGAContextCreate(svga_inst_t *svga);
void SVGAContextDestroy(svga_inst_t *svga, uint32_t cid);
void SVGACleanup(svga_inst_t *svga, uint32_t pid);
void SVGASurfaceDestroy(svga_inst_t *svga, uint32_t sid);

uint32_t SVGAContextIDNext(svga_inst_t *svga);
void SVGAContextIDFree(svga_inst_t *svga, uint32_t ctx_id);
uint32_t SVGASurfaceIDNext(svga_inst_t *svga);
void SVGASurfaceIDFree(svga_inst_t *svga, uint32_t surf_id);

void SVGAPresent(svga_inst_t *svga, HDC hDC, uint32_t cid, uint32_t sid);
void SVGAPresentWindow(svga_inst_t *svga, HDC hDC, uint32_t cid, uint32_t sid);
void SVGACompose(svga_inst_t *svga, uint32_t srcSid, uint32_t destSid, LPCRECT pRect);

void SVGAZombieKiller();

#ifndef NO_VBOX_H
BOOL SVGAReadHwInfo(svga_inst_t *ctx, VBOXGAHWINFO *pHwInfo);
BOOL SVGASurfaceGBCreate(svga_inst_t *svga, SVGAGBSURFCREATE *pCreateParms);

struct pipe_screen;

struct pipe_screen *         WINAPI GaDrvScreenCreate(const WDDMGalliumDriverEnv *pEnv);
void                         WINAPI GaDrvScreenDestroy(struct pipe_screen *s);
uint32_t                     WINAPI GaDrvGetSurfaceId(struct pipe_screen *pScreen, struct pipe_resource *pResource);
const WDDMGalliumDriverEnv * WINAPI GaDrvGetWDDMEnv(struct pipe_screen *pScreen);
uint32_t                     WINAPI GaDrvGetContextId(struct pipe_context *pPipeContext);
void                         WINAPI GaDrvContextFlush(struct pipe_context *pPipeContext);
const WDDMGalliumDriverEnv * WINAPI GaDrvCreateEnv(svga_inst_t *svga);

#endif

#ifdef __cplusplus
}
#endif

#endif /* __SVGADRV_H__INCLUDED__ */
