/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * WonderMedia WM8505 DRM/KMS Graphics Driver
 *
 * Display Register Map
 *
 * Copyright (C) 2026 Logan Russell <me@lrussell.net>
 */

#ifndef _WMT_REGS_H_
#define _WMT_REGS_H_

#include <linux/bits.h>

/* GOVRH Scanout / Page-Flip */
#define WMT_GOVRH_DVO_PIX		0x30
#define WMT_GOVRH_DVO_RGB		BIT(0)
#define WMT_GOVRH_MIF			0x80
#define WMT_GOVRH_MIF_EN		BIT(0)
#define WMT_GOVRH_YSA			0x90
#define WMT_GOVRH_CSA			0x94
#define WMT_GOVRH_PIXWID		0x98
#define WMT_GOVRH_BUFWID		0x9c
#define WMT_GOVRH_WIDTH_MASK		GENMASK(10, 0)
#define WMT_GOVRH_VCROP			0xa0
#define WMT_GOVRH_HCROP			0xa4
#define WMT_GOVRH_FHI			0xa8
#define WMT_GOVRH_FHI_DEFAULT		0xf
#define WMT_GOVRH_REG_STS		0xe4
#define WMT_GOVRH_REG_UPDATE		BIT(0)

#define WMT_GOVRH_TG_ENABLE		0x100
#define WMT_GOVRH_TG_EN			BIT(0)
#define WMT_GOVRH_READ_CYC		0x104
#define WMT_GOVRH_READ_CYC_MASK		GENMASK(6, 0)
#define WMT_GOVRH_H_ALLPXL		0x108
#define WMT_GOVRH_V_ALLLN		0x10c
#define WMT_GOVRH_ACTLN_BG		0x110
#define WMT_GOVRH_ACTLN_END		0x114
#define WMT_GOVRH_ACTPX_BG		0x118
#define WMT_GOVRH_ACTPX_END		0x11c
#define WMT_GOVRH_VBIE_LINE		0x120
#define WMT_GOVRH_PVBI_LINE		0x124
#define WMT_GOVRH_VBISW			0x128
#define WMT_GOVRH_HSYNW			0x12c
#define WMT_GOVRH_DVO_SET		0x148
#define WMT_GOVRH_DVO_ENABLE		BIT(2)
#define WMT_GOVRH_CB_ENABLE		0x150
#define WMT_GOVRH_CONTRAST		0x1b8

#define WMT_GOVRH_CONTRAST_YAF(x)	(((x) & 0xff) << 16)
#define WMT_GOVRH_CONTRAST_PBAF(x)	(((x) & 0xff) << 8)
#define WMT_GOVRH_CONTRAST_PRAF(x)	(((x) & 0xff) << 0)
#define WMT_GOVRH_CONTRAST_VAL(y, pb, pr) \
	(WMT_GOVRH_CONTRAST_YAF(y) | WMT_GOVRH_CONTRAST_PBAF(pb) | WMT_GOVRH_CONTRAST_PRAF(pr))
#define WMT_GOVRH_CONTRAST_DEFAULT	WMT_GOVRH_CONTRAST_VAL(0x80, 0x80, 0x80)

#define WMT_GOVRH_BRIGHTNESS		0x1bc
#define WMT_GOVRH_YUV2RGB		0x1e4
#define WMT_GOVRH_RGB_MODE		BIT(2)
#define WMT_GOVRH_DAC_CLKINV		BIT(3)
#define WMT_GOVRH_BLANK_ZERO		BIT(4)
#define WMT_GOVRH_PANEL_SETTLE_MS	200

/* VPP Shared Interrupt */
#define WMT_VPP_INTSTS			0x4
#define WMT_VPP_INTEN			0x8
#define WMT_VPP_GOVRH_VBIS		BIT(9)

/* VPP Module Reset */
#define WMT_VPP_SW_RESET		0x10
#define WMT_VPP_SW_RESET_GE		BIT(16)

/* GE 2D Graphics Engine */
#define WMT_GE_COMMAND			0x0
#define WMT_GE_COLOR_DEPTH		0x4
#define WMT_GE_HM_SEL			0x8
#define WMT_GE_ROP_CODE			0x14
#define WMT_GE_FIRE			0x18
#define WMT_GE_SRC_BADDR		0x20
#define WMT_GE_SRC_DISP_W		0x24
#define WMT_GE_SRC_DISP_H		0x28
#define WMT_GE_SRC_X_START		0x2c
#define WMT_GE_SRC_Y_START		0x30
#define WMT_GE_SRC_WIDTH		0x34
#define WMT_GE_SRC_HEIGHT		0x38
#define WMT_GE_DES_BADDR		0x3c
#define WMT_GE_DES_DISP_W		0x40
#define WMT_GE_DES_DISP_H		0x44
#define WMT_GE_DES_X_START		0x48
#define WMT_GE_DES_Y_START		0x4c
#define WMT_GE_DES_WIDTH		0x50
#define WMT_GE_DES_HEIGHT		0x54
#define WMT_GE_PAT0_COLOR		0x88
#define WMT_GE_DELAY			0xe8
#define WMT_GE_ENG_EN			0xec
#define WMT_GE_INT_EN			0xf0
#define WMT_GE_INT_FLAG			0xf4
#define WMT_GE_STATUS			0xf8

#define WMT_GE_ENABLE			BIT(0)
#define WMT_GE_FIRE_GO			BIT(0)
#define WMT_GE_CMD_BLIT			0x1
#define WMT_GE_DEPTH_32BPP		0x3
#define WMT_GE_HM_SEL_MEM		0
#define WMT_GE_STATUS_BUSY		BIT(2)
#define WMT_GE_STATUS_RESET		GENMASK(5, 3)
#define WMT_GE_INT_COMPLETE		BIT(8)
#define WMT_GE_INT_TIMEOUT		BIT(9)
#define WMT_GE_INT_CLEAR		GENMASK(31, 0)
#define WMT_GE_DELAY_DEFAULT		0x10001

#endif /* _WMT_REGS_H_ */
