// SPDX-License-Identifier: GPL-2.0-only
/*
 * WonderMedia WM8505 DRM/KMS Graphics Driver
 *
 * CRTC and Display Pipeline
 *
 * Copyright (C) 2026 Logan Russell <me@lrussell.net>
 */

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/math.h>
#include <linux/minmax.h>
#include <linux/spinlock.h>

#include <drm/drm_fb_dma_helper.h>
#include <drm/drm_fourcc.h>
#include <drm/drm_framebuffer.h>
#include <drm/drm_gem_dma_helper.h>
#include <drm/drm_simple_kms_helper.h>
#include <drm/drm_vblank.h>

#include "wmt_drm.h"

/**
 * wmt_govrh_set_timing - Translate DRM modes into GOVRH timings
 */
static void wmt_govrh_set_timing(struct wmt_drm_device *wmt,
				 const struct drm_display_mode *mode)
{
	unsigned long t_rate = mode->clock * 1000;
	int h_sync, h_bp, h_fp, h_start, h_end, h_all;
	int v_sync, v_bp, v_fp, v_start, v_end, v_all;

	/* Set pixel clock */
	clk_set_rate(wmt->clk_dvo, t_rate);

	/* Apply the divider */
	writel((max_t(u32, DIV_ROUND_CLOSEST(clk_get_rate(wmt->clk_dvo), t_rate), 1) - 1) &
		GENMASK(6, 0), wmt->govrh_regs + WMT_GOVRH_READ_CYC);

	/* Calculate display geometry offsets */
	h_sync = mode->hsync_end - mode->hsync_start;
	h_bp = mode->htotal - mode->hsync_end;
	h_fp = mode->hsync_start - mode->hdisplay;

	v_sync = mode->vsync_end - mode->vsync_start;
	v_bp = mode->vtotal - mode->vsync_end;
	v_fp = mode->vsync_start - mode->vdisplay;

	h_start = h_sync + h_bp;
	h_end = h_start + mode->hdisplay;
	h_all = h_end + h_fp;

	v_start = v_sync + v_bp + 1;
	v_end = v_start + mode->vdisplay;
	v_all = v_end + v_fp - 1;

	writel(h_start, wmt->govrh_regs + WMT_GOVRH_ACTPX_BG);
	writel(h_end, wmt->govrh_regs + WMT_GOVRH_ACTPX_END);
	writel(h_all, wmt->govrh_regs + WMT_GOVRH_H_ALLPXL);
	writel(h_sync, wmt->govrh_regs + WMT_GOVRH_HSYNW);

	writel(v_start, wmt->govrh_regs + WMT_GOVRH_ACTLN_BG);
	writel(v_end, wmt->govrh_regs + WMT_GOVRH_ACTLN_END);
	writel(v_all, wmt->govrh_regs + WMT_GOVRH_V_ALLLN);

	writel(v_sync + 1, wmt->govrh_regs + WMT_GOVRH_VBISW);
	writel(v_sync + 1, wmt->govrh_regs + WMT_GOVRH_VBIE_LINE);
	writel(max(v_fp - 2, 1), wmt->govrh_regs + WMT_GOVRH_PVBI_LINE);
}

/**
 * wmt_drm_irq - VBlank IRQ Handler
 */
irqreturn_t wmt_drm_irq(int irq, void *data)
{
	struct wmt_drm_device *wmt = data;
	u32 status = readl(wmt->vpp_regs + WMT_VPP_INTSTS);
	unsigned long flags;

	if (!(status & WMT_VPP_GOVRH_VBIS))
		return IRQ_NONE;

	spin_lock_irqsave(&wmt->drm.event_lock, flags);

	/* Clear interrupt */
	writel(WMT_VPP_GOVRH_VBIS, wmt->vpp_regs + WMT_VPP_INTSTS);

	if (wmt->pending_event) {
		if (wmt->defer_vblank) {
			/* Shadow register latch cycle missed, defer delivery to next interrupt */
			wmt->defer_vblank = false;
		} else {
			drm_crtc_send_vblank_event(&wmt->pipe.crtc, wmt->pending_event);
			drm_crtc_vblank_put(&wmt->pipe.crtc);
			wmt->pending_event = NULL;
		}
	}

	spin_unlock_irqrestore(&wmt->drm.event_lock, flags);
	drm_crtc_handle_vblank(&wmt->pipe.crtc);

	return IRQ_HANDLED;
}

/**
 * wmt_pipe_enable - CRTC enable callback
 */
static void wmt_pipe_enable(struct drm_simple_display_pipe *pipe,
			    struct drm_crtc_state *crtc_state,
			    struct drm_plane_state *plane_state)
{
	struct wmt_drm_device *wmt = to_wmt_drm(pipe->crtc.dev);
	struct drm_gem_dma_object *gem;

	if (!plane_state->fb)
		return;

	gem = drm_fb_dma_get_gem_obj(plane_state->fb, 0);

	/* Disable hardware pipeline */
	writel(0, wmt->govrh_regs + WMT_GOVRH_DVO_SET);
	writel(0, wmt->govrh_regs + WMT_GOVRH_MIF);
	writel(0, wmt->govrh_regs + WMT_GOVRH_TG_ENABLE);
	writel(0, wmt->govrh_regs + WMT_GOVRH_CB_ENABLE);

	/* Set physical buffer address */
	writel(gem->dma_addr, wmt->govrh_regs + WMT_GOVRH_YSA);
	writel(gem->dma_addr, wmt->govrh_regs + WMT_GOVRH_CSA);

	/* Set format to ARGB8888 */
	writel(WMT_GOVRH_RGB_MODE | WMT_GOVRH_DAC_CLKINV | WMT_GOVRH_BLANK_ZERO,
	       wmt->govrh_regs + WMT_GOVRH_YUV2RGB);
	writel(WMT_GOVRH_DVO_RGB, wmt->govrh_regs + WMT_GOVRH_DVO_PIX);

	/* Set resolution boundaries */
	writel(plane_state->fb->width, wmt->govrh_regs + WMT_GOVRH_PIXWID);
	writel(plane_state->fb->pitches[0] / 4, wmt->govrh_regs + WMT_GOVRH_BUFWID);

	wmt_govrh_set_timing(wmt, &crtc_state->adjusted_mode);

	/* Set contrast and brightness */
	writel(WMT_GOVRH_CONTRAST_DEFAULT, wmt->govrh_regs + WMT_GOVRH_CONTRAST);
	writel(0, wmt->govrh_regs + WMT_GOVRH_BRIGHTNESS);

	/* Set FIFO index */
	writel(WMT_GOVRH_FIFO_IND_DEFAULT, wmt->govrh_regs + WMT_GOVRH_FHI);

	/* Enable hardware pipeline */
	writel(1, wmt->govrh_regs + WMT_GOVRH_REG_STS);
	writel(1,  wmt->govrh_regs + WMT_GOVRH_TG_ENABLE);
	writel(1, wmt->govrh_regs + WMT_GOVRH_MIF);
	writel(WMT_GOVRH_DVO_ENABLE, wmt->govrh_regs + WMT_GOVRH_DVO_SET);

	/* Wait for panel stabilization */
	msleep(200);

	drm_crtc_vblank_on(&pipe->crtc);
}

/**
 * wmt_pipe_disable - CRTC disable callback
 */
static void wmt_pipe_disable(struct drm_simple_display_pipe *pipe)
{
	struct wmt_drm_device *wmt = to_wmt_drm(pipe->crtc.dev);
	struct drm_crtc *crtc = &pipe->crtc;
	unsigned long flags;

	drm_crtc_vblank_off(crtc);

	/* Disable memory fetch */
	writel(0, wmt->govrh_regs + WMT_GOVRH_MIF);

	spin_lock_irqsave(&crtc->dev->event_lock, flags);
	if (wmt->pending_event) {
		drm_crtc_send_vblank_event(crtc, wmt->pending_event);
		drm_crtc_vblank_put(crtc);
		wmt->pending_event = NULL;
	}
	spin_unlock_irqrestore(&crtc->dev->event_lock, flags);
}

/**
 * wmt_pipe_enable_vblank - Vblank hardware interrupt enable
 */
static int wmt_pipe_enable_vblank(struct drm_simple_display_pipe *pipe)
{
	struct wmt_drm_device *wmt = to_wmt_drm(pipe->crtc.dev);
	u32 inten;

	/* Clear stale interrupts */
	writel(WMT_VPP_GOVRH_VBIS, wmt->vpp_regs + WMT_VPP_INTSTS);

	inten = readl(wmt->vpp_regs + WMT_VPP_INTEN);
	inten |= WMT_VPP_GOVRH_VBIS;
	writel(inten, wmt->vpp_regs + WMT_VPP_INTEN);

	return 0;
}

/**
 * wmt_pipe_disable_vblank - Vblank hardware interrupt disable
 */
static void wmt_pipe_disable_vblank(struct drm_simple_display_pipe *pipe)
{
	struct wmt_drm_device *wmt = to_wmt_drm(pipe->crtc.dev);
	u32 inten = readl(wmt->vpp_regs + WMT_VPP_INTEN);

	inten &= ~WMT_VPP_GOVRH_VBIS;
	writel(inten, wmt->vpp_regs + WMT_VPP_INTEN);
}

/**
 * wmt_pipe_update - Plane update callback for atomic page-flips
 */
static void wmt_pipe_update(struct drm_simple_display_pipe *pipe,
			    struct drm_plane_state *old_state)
{
	struct wmt_drm_device *wmt = to_wmt_drm(pipe->crtc.dev);
	struct drm_plane_state *state = pipe->plane.state;
	struct drm_crtc *crtc = &pipe->crtc;
	unsigned long flags;

	/* Ensure GE operations complete before latching address */
	wmt_ge_sync(wmt);

	spin_lock_irqsave(&crtc->dev->event_lock, flags);
	if (state->fb && state->fb != old_state->fb) {
		struct drm_gem_dma_object *gem = drm_fb_dma_get_gem_obj(state->fb, 0);

		writel(gem->dma_addr, wmt->govrh_regs + WMT_GOVRH_YSA);
		writel(gem->dma_addr, wmt->govrh_regs + WMT_GOVRH_CSA);
		writel(state->src_x >> 16, wmt->govrh_regs + WMT_GOVRH_VCROP);
		writel(state->src_y >> 16, wmt->govrh_regs + WMT_GOVRH_HCROP);
	}

	if (crtc->state->event) {
		u32 status = readl(wmt->vpp_regs + WMT_VPP_INTSTS);

		wmt->pending_event = crtc->state->event;
		crtc->state->event = NULL;

		/* Guard against hardware latch misses if actively in the VBI */
		wmt->defer_vblank = status & WMT_VPP_GOVRH_VBIS;

		if (drm_crtc_vblank_get(crtc) != 0) {
			drm_crtc_send_vblank_event(crtc, wmt->pending_event);
			wmt->pending_event = NULL;
			wmt->defer_vblank = false;
		}
	}
	spin_unlock_irqrestore(&crtc->dev->event_lock, flags);
}

const struct drm_simple_display_pipe_funcs wmt_pipe_funcs = {
	.enable			= wmt_pipe_enable,
	.disable		= wmt_pipe_disable,
	.update			= wmt_pipe_update,
	.enable_vblank		= wmt_pipe_enable_vblank,
	.disable_vblank		= wmt_pipe_disable_vblank,
};

const u32 wmt_formats[2] = {
	DRM_FORMAT_XRGB8888,
	DRM_FORMAT_ARGB8888,
};
