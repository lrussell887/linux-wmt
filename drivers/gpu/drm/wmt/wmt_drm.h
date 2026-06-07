/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * WonderMedia WM8505 DRM/KMS Graphics Driver
 *
 * Copyright (C) 2026 Logan Russell <me@lrussell.net>
 */

#ifndef _WMT_DRM_H_
#define _WMT_DRM_H_

#include <linux/spinlock.h>
#include <linux/types.h>
#include <linux/wait.h>
#include <linux/workqueue.h>

#include <drm/drm_device.h>
#include <drm/drm_simple_kms_helper.h>

#include <drm/wmt_drm.h>
#include "wmt_regs.h"

/* GE Async Job Ring Definitions */
#define WMT_GE_RING		16
#define WMT_GE_TIMEOUT_US	100000
#define WMT_GE_RESET_US		10000
#define WMT_GE_WAIT_MAX_US	1000000

struct drm_gem_object;
struct drm_gem_dma_object;
struct drm_fb_helper;

/* Queued GE Job */
struct wmt_ge_job {
	u32 seqno;
	u32 num_ops;
	u32 op_cursor;
	bool errored;
	struct wmt_ge_op *ops;
	struct drm_gem_object *dst;
	struct drm_gem_object *src;
	dma_addr_t dst_addr;
	dma_addr_t src_addr;
};

/* GE Completion Timeline */
static inline bool ge_passed(u32 done, u32 target)
{
	return (s32)(done - target) >= 0;
}

/* Primary Device Context */
struct wmt_drm_device {
	struct drm_device drm;
	struct drm_simple_display_pipe pipe;

	void __iomem *vpp_regs;
	void __iomem *govrh_regs;
	void __iomem *ge_regs;
	int ge_irq;

	struct clk *clk_dvo;
	struct clk *clk_govrh;
	struct clk *clk_vpp;
	struct clk *clk_ge;

	/* GE async job ring */
	spinlock_t ge_lock;
	struct wmt_ge_job ge_ring[WMT_GE_RING];
	u32 ge_head;
	u32 ge_tail;
	u32 ge_rtail;
	u32 ge_seq;
	u32 ge_done;
	bool ge_reset_pending;
	bool ge_console;
	bool ge_dead;
	wait_queue_head_t ge_wait;
	struct work_struct ge_retire_work;
	struct work_struct ge_reset_work;

	/* GOVRH page-flip */
	struct drm_pending_vblank_event *pending_event;
	bool defer_vblank;
};

#define to_wmt_drm(x) container_of(x, struct wmt_drm_device, drm)

/* Display Pipe and VBlank Interrupt */
extern const struct drm_simple_display_pipe_funcs wmt_pipe_funcs;
extern const u32 wmt_formats[2];
irqreturn_t wmt_drm_irq(int irq, void *data);

/* 2D Engine Functions */
irqreturn_t wmt_ge_irq(int irq, void *data);
void wmt_ge_configure(struct wmt_drm_device *wmt);
void wmt_ge_retire_work(struct work_struct *work);
void wmt_ge_reset_work(struct work_struct *work);
void wmt_ge_teardown(void *data);
void wmt_ge_latch_drain(struct wmt_drm_device *wmt, struct drm_gem_object *gem);
int wmt_ge_console_op(struct wmt_drm_device *wmt, struct wmt_ge_op *op,
		      struct drm_gem_dma_object *gem);
void wmt_ge_console_idle(struct wmt_drm_device *wmt);
int wmt_drm_ioctl_ge_submit(struct drm_device *dev, void *data, struct drm_file *file);
int wmt_drm_ioctl_ge_wait(struct drm_device *dev, void *data, struct drm_file *file);

/* FBCon GE Acceleration */
void wmt_fbdev_probe_hook(struct drm_fb_helper *fb_helper);

#endif /* _WMT_DRM_H_ */
