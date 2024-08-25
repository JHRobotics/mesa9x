#ifndef __SVGADRV_CMDS_H__INCLUDED__
#define __SVGADRV_CMDS_H__INCLUDED__

#pragma pack(push)
#pragma pack(1)
typedef struct _cmd_present_t
{
	SVGA3dCmdHeader           header;
	SVGA3dCmdPresent          present;
	SVGA3dCopyRect            rect;
} cmd_present_t;
#define CMD_PRESENT_INIT \
 {{SVGA_3D_CMD_PRESENT, sizeof(SVGA3dCmdPresent) + sizeof(SVGA3dCopyRect)}}
#define CMD_PRESENT_READBACK_INIT \
 {{SVGA_3D_CMD_PRESENT_READBACK, sizeof(SVGA3dCmdPresent) + sizeof(SVGA3dCopyRect)}}

typedef struct _cmd_blit_screen_gmrfb_t
{
	uint32_t header;
	SVGAFifoCmdBlitScreenToGMRFB blit;
} cmd_blit_screen_gmrfb_t;
#define CMD_BLIT_SCREEN_GMRFB_INIT {SVGA_CMD_BLIT_SCREEN_TO_GMRFB}

typedef struct _cmd_blit_gmrfb_screen_t
{
	uint32_t header;
	SVGAFifoCmdBlitGMRFBToScreen blit;
} cmd_blit_gmrfb_screen_t;
#define CMD_BLIT_GMRFB_SCREEN_INIT {SVGA_CMD_BLIT_GMRFB_TO_SCREEN}

typedef struct _cmd_define_gmrfb_t
{
	uint32_t header;
	SVGAFifoCmdDefineGMRFB gmrfb;
} cmd_define_gmrfb_t;
#define CMD_DEFINE_GMRFB_INIT {SVGA_CMD_DEFINE_GMRFB}

typedef struct _cmd_surfacedma_t
{
	SVGA3dCmdHeader           header;
	SVGA3dCmdSurfaceDMA       dma;
	SVGA3dCopyBox             box;
	SVGA3dCmdSurfaceDMASuffix suffix;
} cmd_surfacedma_t;
#define CMD_SURFACEDMA_INIT {{SVGA_3D_CMD_SURFACE_DMA, sizeof(SVGA3dCmdSurfaceDMA) + sizeof(SVGA3dCopyBox) + sizeof(SVGA3dCmdSurfaceDMASuffix)}}

typedef struct _cmd_update_t
{
	uint32_t          header;
	SVGAFifoCmdUpdate rect;
} cmd_update_t;
#define CMD_UPDATE_INIT {SVGA_CMD_UPDATE}

typedef struct _cmd_blit_surface_screen_t
{
	SVGA3dCmdHeader header;
	SVGA3dCmdBlitSurfaceToScreen blit;
	SVGASignedRect clip;
} cmd_blit_surface_screen_t;
#define CMD_BLIT_SURFACE_SCREEN {{SVGA_3D_CMD_BLIT_SURFACE_TO_SCREEN, sizeof(SVGA3dCmdBlitSurfaceToScreen) + sizeof(SVGASignedRect)}};

typedef struct _cmd_readback_gb_surface_t
{
	SVGA3dCmdHeader            header;
	SVGA3dCmdReadbackGBSurface surf;
} cmd_readback_gb_surface_t;
#define CMD_READBACK_GB_SURFACE_INIT {{SVGA_3D_CMD_READBACK_GB_SURFACE,	sizeof(SVGA3dCmdReadbackGBSurface)}}

#pragma pack(pop)

#endif /* __SVGADRV_CMDS_H__INCLUDED__ */
