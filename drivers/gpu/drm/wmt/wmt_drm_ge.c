// SPDX-License-Identifier: GPL-2.0-only
/*
 * WonderMedia WM8505 DRM/KMS Graphics Driver
 *
 * 2D Graphics Engine (GE)
 *
 * Copyright (C) 2026 Logan Russell <me@lrussell.net>
 */

#include <linux/cleanup.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/iopoll.h>
#include <linux/overflow.h>
#include <linux/slab.h>
#include <linux/string.h>

#include <drm/drm_file.h>
#include <drm/drm_gem.h>
#include <drm/drm_gem_dma_helper.h>

#include "wmt_drm.h"

/**
 * wmt_ge_sync - Block until GE finishes current command queue
 */
int wmt_ge_sync(struct wmt_drm_device *wmt)
{
	u32 status;

	return readl_poll_timeout_atomic(wmt->ge_regs + WMT_GE_STATUS,
					 status, !(status & WMT_GE_STATUS_BUSY), 1, 100000);
}

/**
 * wmt_ge_check_bounds - Validate coordinates fit within GEM buffer
 */
static bool wmt_ge_check_bounds(struct drm_gem_dma_object *gem,
				u32 pitch, u32 x, u32 y, u32 w, u32 h)
{
	u32 pitch_pixels = pitch / 4;

	if (unlikely(!w || !h || !pitch ||
		     pitch_pixels > WMT_GE_MAX_WIDTH ||
		h > WMT_GE_MAX_HEIGHT ||
		y > WMT_GE_MAX_HEIGHT - h ||
		w > pitch_pixels ||
		x > pitch_pixels - w ||
		(y + h) * pitch > gem->base.size))
		return false;

	return true;
}

/**
 * wmt_ge_hw_fill - Execute hardware-accelerated solid fill
 */
int wmt_ge_hw_fill(struct wmt_drm_device *wmt, struct drm_gem_dma_object *gem,
		   struct wmt_ge_op *req)
{
	void __iomem *regs = wmt->ge_regs;

	if (!wmt_ge_check_bounds(gem, req->dest_pitch, req->dest_x, req->dest_y,
				 req->width, req->height))
		return -EINVAL;

	writel(WMT_GE_DEPTH_32BPP, regs + WMT_GE_COLOR_DEPTH);
	writel(0, regs + WMT_GE_HM_SEL);

	writel(gem->dma_addr, regs + WMT_GE_DES_BADDR);
	writel((req->dest_pitch / 4) - 1, regs + WMT_GE_DES_DISP_W);
	writel((req->dest_y + req->height) - 1, regs + WMT_GE_DES_DISP_H);
	writel(req->dest_x, regs + WMT_GE_DES_X_START);
	writel(req->dest_y, regs + WMT_GE_DES_Y_START);
	writel(req->width - 1, regs + WMT_GE_DES_WIDTH);
	writel(req->height - 1, regs + WMT_GE_DES_HEIGHT);

	writel(req->color, regs + WMT_GE_PAT0_COLOR);
	writel(WMT_GE_CMD_BLIT, regs + WMT_GE_COMMAND);
	writel(req->rop ? req->rop : WMT_GE_ROP_PAT_COPY, regs + WMT_GE_ROP_CODE);
	writel(1, regs + WMT_GE_FIRE);

	return 0;
}

/**
 * wmt_ge_hw_blit - Execute hardware-accelerated BitBlt
 */
int wmt_ge_hw_blit(struct wmt_drm_device *wmt, struct drm_gem_dma_object *src_gem,
		   struct drm_gem_dma_object *dest_gem, struct wmt_ge_op *req)
{
	void __iomem *regs = wmt->ge_regs;

	if (!wmt_ge_check_bounds(src_gem, req->src_pitch, req->src_x, req->src_y,
				 req->width, req->height))
		return -EINVAL;

	if (!wmt_ge_check_bounds(dest_gem, req->dest_pitch, req->dest_x, req->dest_y,
				 req->width, req->height))
		return -EINVAL;

	writel(WMT_GE_DEPTH_32BPP, regs + WMT_GE_COLOR_DEPTH);
	writel(0, regs + WMT_GE_HM_SEL);

	writel(src_gem->dma_addr, regs + WMT_GE_SRC_BADDR);
	writel((req->src_pitch / 4) - 1, regs + WMT_GE_SRC_DISP_W);
	writel((req->src_y + req->height) - 1, regs + WMT_GE_SRC_DISP_H);
	writel(req->src_x, regs + WMT_GE_SRC_X_START);
	writel(req->src_y, regs + WMT_GE_SRC_Y_START);
	writel(req->width - 1, regs + WMT_GE_SRC_WIDTH);
	writel(req->height - 1, regs + WMT_GE_SRC_HEIGHT);

	writel(dest_gem->dma_addr, regs + WMT_GE_DES_BADDR);
	writel((req->dest_pitch / 4) - 1, regs + WMT_GE_DES_DISP_W);
	writel((req->dest_y + req->height) - 1, regs + WMT_GE_DES_DISP_H);
	writel(req->dest_x, regs + WMT_GE_DES_X_START);
	writel(req->dest_y, regs + WMT_GE_DES_Y_START);
	writel(req->width - 1, regs + WMT_GE_DES_WIDTH);
	writel(req->height - 1, regs + WMT_GE_DES_HEIGHT);

	writel(WMT_GE_CMD_BLIT, regs + WMT_GE_COMMAND);
	writel(req->rop ? req->rop : WMT_GE_ROP_SRC_COPY, regs + WMT_GE_ROP_CODE);
	writel(1, regs + WMT_GE_FIRE);

	return 0;
}

/**
 * wmt_drm_ioctl_ge_batch - Submit a batch of GE commands from userspace
 */
int wmt_drm_ioctl_ge_batch(struct drm_device *dev, void *data, struct drm_file *file_priv)
{
	struct wmt_ge_batch_req *req = data;
	struct wmt_ge_op *ops;
	struct wmt_drm_device *wmt;
	struct drm_gem_object *cached_dest_obj = NULL;
	struct drm_gem_object *cached_src_obj = NULL;
	u32 cached_dest_handle = 0;
	u32 cached_src_handle = 0;
	u32 i;
	int ret = 0;

	if (req->num_ops == 0 || req->num_ops > WMT_GE_MAX_OPS)
		return -EINVAL;

	ops = vmemdup_user(req->ops, array_size(req->num_ops, sizeof(*ops)));
	if (IS_ERR(ops))
		return PTR_ERR(ops);

	wmt = to_wmt_drm(dev);

	guard(mutex)(&wmt->ge_mutex);

	for (i = 0; i < req->num_ops; i++) {
		struct wmt_ge_op *op = &ops[i];

		/* Cache GEM handles to avoid excessive lookups */
		if (op->dest_handle != cached_dest_handle || !cached_dest_obj) {
			drm_gem_object_put(cached_dest_obj);

			cached_dest_obj = drm_gem_object_lookup(file_priv, op->dest_handle);
			if (!cached_dest_obj) {
				ret = -ENOENT;
				break;
			}
			cached_dest_handle = op->dest_handle;
		}

		if (op->type == WMT_GE_OP_BLIT &&
		    (op->src_handle != cached_src_handle || !cached_src_obj)) {
			drm_gem_object_put(cached_src_obj);

			cached_src_obj = drm_gem_object_lookup(file_priv, op->src_handle);
			if (!cached_src_obj) {
				ret = -ENOENT;
				break;
			}
			cached_src_handle = op->src_handle;
		}

		ret = wmt_ge_sync(wmt);
		if (ret)
			break;

		if (op->type == WMT_GE_OP_FILL) {
			ret = wmt_ge_hw_fill(wmt, to_drm_gem_dma_obj(cached_dest_obj), op);
		} else if (op->type == WMT_GE_OP_BLIT) {
			ret = wmt_ge_hw_blit(wmt, to_drm_gem_dma_obj(cached_src_obj),
					     to_drm_gem_dma_obj(cached_dest_obj), op);
		} else {
			ret = -EINVAL;
		}

		if (ret)
			break;
	}

	wmt_ge_sync(wmt);

	drm_gem_object_put(cached_dest_obj);
	drm_gem_object_put(cached_src_obj);
	kvfree(ops);

	return ret;
}
