/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * WonderMedia WM8505 DRM/KMS Graphics Driver
 *
 * Copyright (C) 2026 Logan Russell <me@lrussell.net>
 */

#ifndef _WMT_DRM_H_
#define _WMT_DRM_H_

#include <linux/bits.h>
#include <linux/irqreturn.h>
#include <linux/mutex.h>
#include <linux/types.h>

#include <drm/drm_device.h>
#include <drm/drm_simple_kms_helper.h>

/* VPP Register Offsets */
#define WMT_VPP_INTSTS			0x4
#define WMT_VPP_INTEN			0x8

/* VPP Constants */
#define WMT_VPP_GOVRH_VBIS		BIT(9)

/* GOVRH Register Offsets */
#define WMT_GOVRH_DVO_PIX		0x30
#define WMT_GOVRH_MIF			0x80
#define WMT_GOVRH_YSA			0x90
#define WMT_GOVRH_CSA			0x94
#define WMT_GOVRH_PIXWID		0x98
#define WMT_GOVRH_BUFWID		0x9c
#define WMT_GOVRH_VCROP			0xa0
#define WMT_GOVRH_HCROP			0xa4
#define WMT_GOVRH_FHI			0xa8
#define WMT_GOVRH_REG_STS		0xe4
#define WMT_GOVRH_TG_ENABLE		0x100
#define WMT_GOVRH_READ_CYC		0x104
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
#define WMT_GOVRH_CB_ENABLE		0x150
#define WMT_GOVRH_CONTRAST		0x1b8
#define WMT_GOVRH_BRIGHTNESS		0x1bc
#define WMT_GOVRH_YUV2RGB		0x1e4

/* GOVRH Constants */
#define WMT_GOVRH_DVO_RGB		BIT(0)
#define WMT_GOVRH_DVO_ENABLE		BIT(2)
#define WMT_GOVRH_RGB_MODE		BIT(2)
#define WMT_GOVRH_DAC_CLKINV		BIT(3)
#define WMT_GOVRH_BLANK_ZERO		BIT(4)
#define WMT_GOVRH_FIFO_IND_DEFAULT	0xf

#define WMT_GOVRH_CONTRAST_YAF(x)	(((x) & 0xff) << 16)
#define WMT_GOVRH_CONTRAST_PBAF(x)	(((x) & 0xff) << 8)
#define WMT_GOVRH_CONTRAST_PRAF(x)	(((x) & 0xff) << 0)
#define WMT_GOVRH_CONTRAST_VAL(y, pb, pr) \
	(WMT_GOVRH_CONTRAST_YAF(y) | WMT_GOVRH_CONTRAST_PBAF(pb) | WMT_GOVRH_CONTRAST_PRAF(pr))

#define WMT_GOVRH_CONTRAST_DEFAULT	WMT_GOVRH_CONTRAST_VAL(0x80, 0x80, 0x80)

/* GE Register Offsets */
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
#define WMT_GE_CTRL			0xec
#define WMT_GE_STATUS			0xf8

/* GE Constants */
#define WMT_GE_ENABLE			BIT(0)
#define WMT_GE_CMD_BLIT			0x1
#define WMT_GE_STATUS_BUSY		BIT(2)
#define WMT_GE_DEPTH_32BPP		0x3
#define WMT_GE_ROP_XOR			0x5a
#define WMT_GE_ROP_SRC_COPY		0xcc
#define WMT_GE_ROP_PAT_COPY		0xf0
#define WMT_GE_DELAY_DEFAULT		0x10001

#define WMT_GE_MAX_WIDTH		2048
#define WMT_GE_MAX_HEIGHT		2048
#define WMT_GE_MAX_OPS			8192

/* Custom Userspace IOCTLs */
#define WMT_GE_OP_FILL			0x1
#define WMT_GE_OP_BLIT			0x2

/* 2D Graphics Engine operation */
struct wmt_ge_op {
	__u32 type;
	__u32 rop;
	__u32 dest_handle;
	__u32 dest_pitch;
	__u32 dest_x;
	__u32 dest_y;
	__u32 width;
	__u32 height;
	__u32 color;
	__u32 src_handle;
	__u32 src_pitch;
	__u32 src_x;
	__u32 src_y;
};

/* Request wrapper for GE batch processing */
struct wmt_ge_batch_req {
	struct wmt_ge_op __user *ops;
	__u32 num_ops;
};

#define DRM_WMT_GE_BATCH		0x0
#define DRM_IOCTL_WMT_GE_BATCH \
	DRM_IOW(DRM_COMMAND_BASE + DRM_WMT_GE_BATCH, struct wmt_ge_batch_req)

/* Primary device context */
struct wmt_drm_device {
	struct drm_device drm;
	struct drm_simple_display_pipe pipe;

	void __iomem *vpp_regs;
	void __iomem *govrh_regs;

	struct clk *clk_dvo;
	struct clk *clk_govrh;
	struct clk *clk_vpp;
	struct clk *clk_ge;

	void __iomem *ge_regs;
	struct mutex ge_mutex; /* Protects GE hardware */

	struct drm_pending_vblank_event *pending_event;
	bool defer_vblank;
};

#define to_wmt_drm(x) container_of(x, struct wmt_drm_device, drm)

struct drm_gem_dma_object;
struct drm_fb_helper;

/* Internal API */
extern const struct drm_simple_display_pipe_funcs wmt_pipe_funcs;
extern const u32 wmt_formats[2];
irqreturn_t wmt_drm_irq(int irq, void *data);

int wmt_ge_sync(struct wmt_drm_device *wmt);
int wmt_ge_hw_fill(struct wmt_drm_device *wmt, struct drm_gem_dma_object *gem,
		   struct wmt_ge_op *req);
int wmt_ge_hw_blit(struct wmt_drm_device *wmt, struct drm_gem_dma_object *src_gem,
		   struct drm_gem_dma_object *dest_gem, struct wmt_ge_op *req);
int wmt_drm_ioctl_ge_batch(struct drm_device *dev, void *data,
			   struct drm_file *file_priv);

void wmt_fbdev_probe_hook(struct drm_fb_helper *fb_helper);

#endif /* _WMT_DRM_H_ */
