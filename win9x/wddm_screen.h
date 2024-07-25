/* $Id: wddm_screen.h 76563 2019-01-01 03:53:56Z vboxsync $ */
/** @file
 * VirtualBox Windows Guest Mesa3D - VMSVGA hardware driver.
 */

/*
 * Copyright (C) 2016-2019 Oracle Corporation
 *
 * This file is part of VirtualBox Open Source Edition (OSE), as
 * available from http://www.virtualbox.org. This file is free software;
 * you can redistribute it and/or modify it under the terms of the GNU
 * General Public License (GPL) as published by the Free Software
 * Foundation, in version 2 as it comes in the "COPYING" file of the
 * VirtualBox OSE distribution. VirtualBox OSE is distributed in the
 * hope that it will be useful, but WITHOUT ANY WARRANTY of any kind.
 */

#ifndef GA_INCLUDED_SRC_3D_win_VBoxSVGA_wddm_screen_h
#define GA_INCLUDED_SRC_3D_win_VBoxSVGA_wddm_screen_h

#ifdef VBOX

#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include <VBoxGaDriver.h>

#include "vmw_screen.h"

struct vmw_winsys_screen_wddm
{
    struct vmw_winsys_screen base;

    const WDDMGalliumDriverEnv *pEnv;
    VBOXGAHWINFOSVGA HwInfo;
};

#else /* !VBOX */

#include <stdint.h>
#include <Windows.h>

#include <svga3d_types.h>

#pragma pack(1) /* VMSVGA structures are '__packed'. */
#include <svga3d_caps.h>
#include <svga3d_reg.h>
#include <svga3d_cmd.h>
#pragma pack()

#include "vmw_screen.h"

#ifndef DECLCALLBACKMEMBER
#define DECLCALLBACKMEMBER(_type, _name, _params) _type (* _name) _params
#endif

typedef uint32_t SVGA3dSurfaceFlags;
//typedef uint32_t SVGA3dSurfaceAllFlags;
typedef uint64_t SVGA3dSurfaceAllFlags;

#define GA_MAX_SURFACE_FACES 6
#define GA_MAX_MIP_LEVELS 24

/* Gallium virtual hardware supported by the miniport. */
#define VBOX_GA_HW_TYPE_UNKNOWN 0
#define VBOX_GA_HW_TYPE_VMSVGA  1

#define GA_HWINFO_REGS 256
#define GA_HWINFO_FIFO 1024
#define GA_HWINFO_CAPS 512

#pragma pack(push)
#pragma pack(1)

struct _svga_inst_t;

typedef struct VBOXGAHWINFOSVGA
{
    uint32_t cbInfoSVGA;

    /* Copy of SVGA_REG_*, up to 256, currently 58 are used. */
    uint32_t au32Regs[GA_HWINFO_CAPS];

    /* Copy of FIFO registers, up to 1024, currently 290 are used. */
    uint32_t au32Fifo[GA_HWINFO_FIFO];

    /* Currently SVGA has 260 caps, 512 should be ok for near future.
     * This is a copy of SVGA3D_DEVCAP_* values returned by the host.
     * [del]Only valid if SVGA_CAP_GBOBJECTS is set in SVGA_REG_CAPABILITIES.[/del]
     * JH: Load by and parsed by driver, so usable in with gpu9
     */
    uint32_t au32Caps[GA_HWINFO_CAPS];
    struct _svga_inst_t *svga;
} VBOXGAHWINFOSVGA;

typedef struct VBOXGAHWINFO
{
    uint32_t u32HwType; /* VBOX_GA_HW_TYPE_* */
    uint32_t u32Reserved;
    union
    {
        VBOXGAHWINFOSVGA svga;
        uint8_t au8Raw[65536];
    } u;
} VBOXGAHWINFO;

#pragma pack(pop)

typedef struct GASURFCREATE
{
    uint32_t flags;  /* SVGA3dSurfaceFlags */
    uint32_t format; /* SVGA3dSurfaceFormat */
    uint32_t usage;  /* SVGA_SURFACE_USAGE_* */
    uint32_t mip_levels[GA_MAX_SURFACE_FACES];
    uint32_t size;
} GASURFCREATE;

typedef struct GASURFSIZE
{
    uint32_t cWidth;
    uint32_t cHeight;
    uint32_t cDepth;
    uint32_t u32Reserved;
} GASURFSIZE;

#define GA_FENCE_STATUS_NULL      0 /* Fence not found */
#define GA_FENCE_STATUS_IDLE      1
#define GA_FENCE_STATUS_SUBMITTED 2
#define GA_FENCE_STATUS_SIGNALED  3

typedef struct GAFENCEQUERY
{
    /* IN: The miniport's handle of the fence.
     * Assigned by the miniport. Not DXGK fence id!
     */
    uint32_t u32FenceHandle;

    /* OUT: The miniport's sequence number associated with the command buffer.
     */
    uint32_t u32SubmittedSeqNo;

    /* OUT: The miniport's sequence number associated with the last command buffer completed on host.
     */
    uint32_t u32ProcessedSeqNo;

    /* OUT: GA_FENCE_STATUS_*. */
    uint32_t u32FenceStatus;
} GAFENCEQUERY;

typedef struct SVGAGBSURFCREATE
{
    /* Surface data. */
    struct
    {
        SVGA3dSurfaceAllFlags flags;
        SVGA3dSurfaceFormat format;
        unsigned usage;
        SVGA3dSize size;
        uint32_t numFaces;
        uint32_t numMipLevels;
        unsigned sampleCount;
        SVGA3dMSPattern multisamplePattern;
        SVGA3dMSQualityLevel qualityLevel;
    } s;
    uint32_t gmrid; /* In/Out: Backing GMR. */
    uint32_t cbGB; /* Out: Size of backing memory. */
    uint32_t userAddress; /* Out: R3 mapping of the backing memory. */
    uint32_t u32Sid; /* Out: Surface id. */
    BOOL GMRreturn; /* In: GMR address is returned or it's managed with surface */
} SVGAGBSURFCREATE, *PSVGAGBSURFCREATE;

typedef struct WDDMGalliumDriverEnv
{
    /* Size of the structure. */
    DWORD cb;
    /* The environment context pointer to use in the following callbacks. */
    void *pvEnv;
    /* The callbacks to use by the driver. */
    DECLCALLBACKMEMBER(uint32_t, pfnContextCreate,(void *pvEnv,
                                                   boolean extended,
                                                   boolean vgpu10));
    DECLCALLBACKMEMBER(void, pfnContextDestroy,(void *pvEnv,
                                                uint32_t u32Cid));
    DECLCALLBACKMEMBER(int, pfnSurfaceDefine,(void *pvEnv,
                                              GASURFCREATE *pCreateParms,
                                              GASURFSIZE *paSizes,
                                              uint32_t cSizes,
                                              uint32_t *pu32Sid));
    DECLCALLBACKMEMBER(void, pfnSurfaceDestroy,(void *pvEnv,
                                                uint32_t u32Sid));
    DECLCALLBACKMEMBER(int, pfnRender,(void *pvEnv,
                                       uint32_t u32Cid,
                                       void *pvCommands,
                                       uint32_t cbCommands,
                                       GAFENCEQUERY *pFenceQuery));
    DECLCALLBACKMEMBER(void, pfnFenceUnref,(void *pvEnv,
                                            uint32_t u32FenceHandle));
    DECLCALLBACKMEMBER(int, pfnFenceQuery,(void *pvEnv,
                                           uint32_t u32FenceHandle,
                                           GAFENCEQUERY *pFenceQuery));
    DECLCALLBACKMEMBER(int, pfnFenceWait,(void *pvEnv,
                                          uint32_t u32FenceHandle,
                                          uint32_t u32TimeoutUS));
    DECLCALLBACKMEMBER(int, pfnRegionCreate,(void *pvEnv,
                                             uint32_t u32RegionSize,
                                             uint32_t *pu32GmrId,
                                             void **ppvMap));
    DECLCALLBACKMEMBER(void, pfnRegionDestroy,(void *pvEnv,
                                               uint32_t u32GmrId,
                                               void *pvMap));
    /* VGPU10 */
    DECLCALLBACKMEMBER(int, pfnGBSurfaceDefine,(void *pvEnv,
                                                SVGAGBSURFCREATE *pCreateParms));
    
    VBOXGAHWINFO *pHWInfo;
    
} WDDMGalliumDriverEnv;

struct vmw_winsys_screen_wddm
{
    struct vmw_winsys_screen base;

    const WDDMGalliumDriverEnv *pEnv;
    
    VBOXGAHWINFOSVGA HwInfo;
};

#endif

#endif /* !GA_INCLUDED_SRC_3D_win_VBoxSVGA_wddm_screen_h */

