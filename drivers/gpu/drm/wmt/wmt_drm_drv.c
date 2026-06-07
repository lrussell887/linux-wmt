// SPDX-License-Identifier: GPL-2.0-only
/*
 * WonderMedia WM8505 DRM/KMS Graphics Driver
 *
 * Copyright (C) 2026 Logan Russell <me@lrussell.net>
 */

#include <linux/clk.h>
#include <linux/dma-mapping.h>
#include <linux/err.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>

#include <drm/drm_atomic_helper.h>
#include <drm/drm_bridge.h>
#include <drm/drm_drv.h>
#include <drm/drm_fbdev_dma.h>
#include <drm/drm_gem_dma_helper.h>
#include <drm/drm_gem_framebuffer_helper.h>
#include <drm/drm_managed.h>
#include <drm/drm_of.h>
#include <drm/drm_panel.h>
#include <drm/drm_vblank.h>

#include "wmt_drm.h"

#define DRIVER_DESC "WonderMedia WM8505 DRM Driver"

static const struct drm_ioctl_desc wmt_ioctls[] = {
	DRM_IOCTL_DEF_DRV(WMT_GE_BATCH, wmt_drm_ioctl_ge_batch, DRM_RENDER_ALLOW),
};

DEFINE_DRM_GEM_DMA_FOPS(wmt_drm_fops);

static const struct drm_driver wmt_drm_driver = {
	.driver_features	= DRIVER_GEM | DRIVER_MODESET | DRIVER_ATOMIC | DRIVER_RENDER,
	.ioctls			= wmt_ioctls,
	.num_ioctls		= ARRAY_SIZE(wmt_ioctls),
	.fops			= &wmt_drm_fops,
	.name			= "wmt-drm",
	.desc			= DRIVER_DESC,
	.date			= "20260606",
	.major			= 1,
	.minor			= 0,
	.fbdev_probe_hook	= wmt_fbdev_probe_hook,
	DRM_GEM_DMA_DRIVER_OPS,
};

static const struct drm_mode_config_funcs wmt_mode_config_funcs = {
	.fb_create		= drm_gem_fb_create,
	.atomic_check		= drm_atomic_helper_check,
	.atomic_commit		= drm_atomic_helper_commit,
};

/**
 * wmt_drm_probe - platform device probe
 */
static int wmt_drm_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct wmt_drm_device *wmt;
	struct device_node *ge_node;
	struct drm_panel *panel;
	struct drm_bridge *bridge;
	int irq;
	int ret;

	wmt = devm_drm_dev_alloc(dev, &wmt_drm_driver, struct wmt_drm_device, drm);
	if (IS_ERR(wmt))
		return PTR_ERR(wmt);

	mutex_init(&wmt->ge_mutex);

	/* Map display controller (GOVRH) registers */
	wmt->govrh_regs = devm_platform_ioremap_resource_byname(pdev, "govrh");
	if (IS_ERR(wmt->govrh_regs))
		return PTR_ERR(wmt->govrh_regs);

	/* Map interrupt controller (VPP) registers */
	wmt->vpp_regs = devm_platform_ioremap_resource_byname(pdev, "vpp");
	if (IS_ERR(wmt->vpp_regs))
		return PTR_ERR(wmt->vpp_regs);

	/* Set up hardware interrupts */
	writel(0, wmt->vpp_regs + WMT_VPP_INTEN);
	irq = platform_get_irq(pdev, 0);
	if (irq < 0)
		return irq;

	ret = devm_request_irq(dev, irq, wmt_drm_irq, 0, "wmt-drm", wmt);
	if (ret)
		return ret;

	/* Map 2D engine (GE) registers */
	ge_node = of_parse_phandle(dev->of_node, "wm,ge", 0);
	if (!ge_node) {
		dev_err(dev, "Failed to find linked GE node\n");
		return -ENODEV;
	}

	wmt->ge_regs = devm_of_iomap(dev, ge_node, 0, NULL);
	of_node_put(ge_node);
	if (IS_ERR(wmt->ge_regs))
		return PTR_ERR(wmt->ge_regs);

	/* Enable Graphics Engine */
	writel(WMT_GE_ENABLE, wmt->ge_regs + WMT_GE_CTRL);
	writel(WMT_GE_DELAY_DEFAULT, wmt->ge_regs + WMT_GE_DELAY);

	/* Find panel via device tree */
	ret = drm_of_find_panel_or_bridge(dev->of_node, 0, 0, &panel, NULL);
	if (ret)
		return ret;

	/* Enable clocks */
	wmt->clk_dvo = devm_clk_get_enabled(dev, "dvo");
	if (IS_ERR(wmt->clk_dvo))
		return dev_err_probe(dev, PTR_ERR(wmt->clk_dvo),
				     "Failed to get/enable DVO clock\n");

	wmt->clk_govrh = devm_clk_get_enabled(dev, "govrh");
	if (IS_ERR(wmt->clk_govrh))
		return dev_err_probe(dev, PTR_ERR(wmt->clk_govrh),
				     "Failed to get/enable GOVRH clock\n");

	wmt->clk_vpp = devm_clk_get_enabled(dev, "vpp");
	if (IS_ERR(wmt->clk_vpp))
		return dev_err_probe(dev, PTR_ERR(wmt->clk_vpp),
				     "Failed to get/enable VPP clock\n");

	wmt->clk_ge = devm_clk_get_enabled(dev, "ge");
	if (IS_ERR(wmt->clk_ge))
		return dev_err_probe(dev, PTR_ERR(wmt->clk_ge),
				     "Failed to get/enable GE clock\n");

	/* Configure DMA mask */
	ret = dma_coerce_mask_and_coherent(dev, DMA_BIT_MASK(32));
	if (ret)
		return ret;

	ret = drmm_mode_config_init(&wmt->drm);
	if (ret)
		return ret;

	wmt->drm.mode_config.min_width = 1;
	wmt->drm.mode_config.min_height = 1;
	wmt->drm.mode_config.max_width = WMT_GE_MAX_WIDTH;
	wmt->drm.mode_config.max_height = WMT_GE_MAX_HEIGHT;
	wmt->drm.mode_config.funcs = &wmt_mode_config_funcs;

	/* Initialize simple display pipe */
	ret = drm_simple_display_pipe_init(&wmt->drm, &wmt->pipe, &wmt_pipe_funcs,
					   wmt_formats, ARRAY_SIZE(wmt_formats),
					   NULL, NULL);
	if (ret)
		return ret;

	bridge = drm_panel_bridge_add_typed(panel, DRM_MODE_CONNECTOR_DPI);
	ret = drm_simple_display_pipe_attach_bridge(&wmt->pipe, bridge);
	if (ret)
		return ret;

	drm_mode_config_reset(&wmt->drm);

	ret = drm_vblank_init(&wmt->drm, 1);
	if (ret)
		return ret;

	ret = drm_dev_register(&wmt->drm, 0);
	if (ret)
		return ret;

	drm_fbdev_dma_setup(&wmt->drm, 32);

	return 0;
}

/**
 * wmt_drm_remove - platform device remove
 */
static void wmt_drm_remove(struct platform_device *pdev)
{
	struct drm_device *drm = platform_get_drvdata(pdev);

	drm_dev_unregister(drm);
	drm_atomic_helper_shutdown(drm);
}

static const struct of_device_id wmt_drm_dt_ids[] = {
	{ .compatible = "wm,wm8505-drm", },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, wmt_drm_dt_ids);

static struct platform_driver wmt_drm_platform_driver = {
	.probe		= wmt_drm_probe,
	.remove_new	= wmt_drm_remove,
	.driver		= {
		.name		= "wmt-drm",
		.of_match_table	= wmt_drm_dt_ids,
	},
};
module_platform_driver(wmt_drm_platform_driver);

MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_AUTHOR("Logan Russell <me@lrussell.net>");
MODULE_LICENSE("GPL");
