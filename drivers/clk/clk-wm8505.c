// SPDX-License-Identifier: GPL-2.0-only
/*
 * WonderMedia WM8505 Common Clock Framework Driver
 *
 * Copyright (C) 2026 Logan Russell <me@lrussell.net>
 *
 * Based on drivers/clk/clk-vt8500.c
 * Copyright (C) 2012 Tony Prisk <linux@prisktech.co.nz>
 */

#include <linux/kernel.h>
#include <linux/io.h>
#include <linux/iopoll.h>
#include <linux/minmax.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/slab.h>
#include <linux/bitops.h>
#include <linux/clk-provider.h>
#include <dt-bindings/clock/wm8505-clock.h>

/* PMC Registers */
#define PMCEL_REG			0x250
#define PMCEU_REG			0x254

/* PMC Status Masks */
#define WM8505_PMC_BUSY_MASK		0x18

static DEFINE_SPINLOCK(wm8505_clk_lock);
static void __iomem *pmc_base;

/* Hardware Clock Structures */
struct wm8505_clk {
	struct clk_hw	hw;
	u32		div_reg_offset;
	unsigned int	div_mask;
	u32		en_reg_offset;
	int		en_bit;
};

struct wm8505_pll_clk {
	struct clk_hw	hw;
	u32		reg_offset;
};

#define to_wm8505_clk(_hw) container_of(_hw, struct wm8505_clk, hw)
#define to_wm8505_pll_clk(_hw) container_of(_hw, struct wm8505_pll_clk, hw)

/**
 * wm8505_pmc_wait_busy - poll PMC busy status to prevent bus hangs
 */
static void wm8505_pmc_wait_busy(void)
{
	u32 val;
	int ret;

	ret = readl_poll_timeout_atomic(pmc_base, val,
					!(val & WM8505_PMC_BUSY_MASK), 1, 1000);
	WARN_ON(ret);
}

/**
 * wm8505_gated_div_enable - enable a composite gated divisor clock
 */
static int wm8505_gated_div_enable(struct clk_hw *hw)
{
	struct wm8505_clk *cdev = to_wm8505_clk(hw);
	unsigned long flags;

	spin_lock_irqsave(&wm8505_clk_lock, flags);
	writel(readl(pmc_base + cdev->en_reg_offset) | BIT(cdev->en_bit),
	       pmc_base + cdev->en_reg_offset);
	spin_unlock_irqrestore(&wm8505_clk_lock, flags);

	return 0;
}

/**
 * wm8505_gated_div_disable - disable a composite gated divisor clock
 */
static void wm8505_gated_div_disable(struct clk_hw *hw)
{
	struct wm8505_clk *cdev = to_wm8505_clk(hw);
	unsigned long flags;

	spin_lock_irqsave(&wm8505_clk_lock, flags);
	writel(readl(pmc_base + cdev->en_reg_offset) & ~BIT(cdev->en_bit),
	       pmc_base + cdev->en_reg_offset);
	spin_unlock_irqrestore(&wm8505_clk_lock, flags);
}

/**
 * wm8505_gated_div_is_enabled - check composite clock state
 */
static int wm8505_gated_div_is_enabled(struct clk_hw *hw)
{
	struct wm8505_clk *cdev = to_wm8505_clk(hw);

	return readl(pmc_base + cdev->en_reg_offset) & BIT(cdev->en_bit);
}

/**
 * wm8505_clk_recalc_rate - recalculate clock rate from hardware divisor
 */
static unsigned long wm8505_clk_recalc_rate(struct clk_hw *hw, unsigned long parent_rate)
{
	struct wm8505_clk *cdev = to_wm8505_clk(hw);
	u32 div = readl(pmc_base + cdev->div_reg_offset) & cdev->div_mask;
	u32 val;

	/* Custom 64x prescaler handling specifically for SDMMC */
	if (cdev->div_mask == 0x3f) {
		val = div & 0x1f;
		if (!val)
			val = 32;
		return parent_rate / (val * ((div & BIT(5)) ? 64 : 1));
	}

	return parent_rate / (div ? div : cdev->div_mask + 1);
}

/**
 * wm8505_clk_round_rate - round a requested rate to the nearest achievable rate
 */
static long wm8505_clk_round_rate(struct clk_hw *hw, unsigned long rate, unsigned long *prate)
{
	struct wm8505_clk *cdev = to_wm8505_clk(hw);
	u32 divisor;

	if (rate == 0)
		return 0;

	divisor = DIV_ROUND_CLOSEST(*prate, rate);

	if (cdev->div_mask == 0x3f) {
		if (divisor > 32)
			divisor = clamp_val(DIV_ROUND_CLOSEST(divisor, 64), 1U, 32U) * 64;
		else
			divisor = clamp_val(divisor, 1U, 32U);
	} else {
		divisor = clamp_val(divisor, 1U, cdev->div_mask + 1);
	}

	return *prate / divisor;
}

/**
 * wm8505_clk_set_rate - apply a new rate by programming the hardware divisor
 */
static int wm8505_clk_set_rate(struct clk_hw *hw, unsigned long rate, unsigned long parent_rate)
{
	struct wm8505_clk *cdev = to_wm8505_clk(hw);
	u32 divisor, val;
	unsigned long flags;

	if (rate == 0)
		return 0;

	divisor = DIV_ROUND_CLOSEST(parent_rate, rate);

	if (cdev->div_mask == 0x3f) {
		if (divisor > 32) {
			val = DIV_ROUND_CLOSEST(divisor, 64);
			val = 0x20 | (val == 32 ? 0 : val);
		} else {
			val = (divisor == 32) ? 0 : divisor;
		}
	} else {
		val = (divisor == cdev->div_mask + 1) ? 0 : divisor;
	}

	spin_lock_irqsave(&wm8505_clk_lock, flags);
	wm8505_pmc_wait_busy();
	writel((readl(pmc_base + cdev->div_reg_offset) & ~cdev->div_mask) | val,
	       pmc_base + cdev->div_reg_offset);
	wm8505_pmc_wait_busy();
	spin_unlock_irqrestore(&wm8505_clk_lock, flags);

	return 0;
}

static const struct clk_ops wm8505_div_ops = {
	.recalc_rate	= wm8505_clk_recalc_rate,
	.round_rate	= wm8505_clk_round_rate,
	.set_rate	= wm8505_clk_set_rate,
};

static const struct clk_ops wm8505_gated_div_ops = {
	.enable		= wm8505_gated_div_enable,
	.disable	= wm8505_gated_div_disable,
	.is_enabled	= wm8505_gated_div_is_enabled,
	.recalc_rate	= wm8505_clk_recalc_rate,
	.round_rate	= wm8505_clk_round_rate,
	.set_rate	= wm8505_clk_set_rate,
};

/* PLL Configuration Macros */
#define WM8505_PLL_MUL(x)		(((x) & 0x1f) << 1)
#define WM8505_PLL_DIV(x)		(((x) & 0x100) ? 1 : 2)
#define WM8505_BITS_TO_FREQ(r, m, d)	(((r) / (d)) * (m))
#define WM8505_BITS_TO_VAL(m, d)	(((d) == 2 ? 0 : 0x100) | (((m) >> 1) & 0x1f))

/**
 * wm8505_pll_recalc_rate - recalculate PLL output frequency from hardware registers
 */
static unsigned long wm8505_pll_recalc_rate(struct clk_hw *hw, unsigned long parent_rate)
{
	struct wm8505_pll_clk *pll = to_wm8505_pll_clk(hw);
	u32 val = readl(pmc_base + pll->reg_offset);
	u32 mul = WM8505_PLL_MUL(val);
	u32 div = WM8505_PLL_DIV(val);

	return WM8505_BITS_TO_FREQ(parent_rate, mul, div);
}

/**
 * wm8505_find_pll_bits - derive PLL multiplier and predivider for a target rate
 */
static void wm8505_find_pll_bits(unsigned long rate, unsigned long parent_rate,
				 u32 *multiplier, u32 *prediv)
{
	rate = clamp(rate, parent_rate * 4, parent_rate * 62);

	if (rate <= parent_rate * 31) {
		*prediv = 2;
		*multiplier = DIV_ROUND_CLOSEST(rate, parent_rate) * 2;
	} else {
		*prediv = 1;
		*multiplier = DIV_ROUND_CLOSEST(rate, parent_rate * 2) * 2;
	}
}

/**
 * wm8505_pll_round_rate - round PLL frequency to an achievable value
 */
static long wm8505_pll_round_rate(struct clk_hw *hw, unsigned long rate, unsigned long *prate)
{
	u32 mul, div;

	wm8505_find_pll_bits(rate, *prate, &mul, &div);
	return WM8505_BITS_TO_FREQ(*prate, mul, div);
}

/**
 * wm8505_pll_set_rate - reprogram PLL to a new frequency
 */
static int wm8505_pll_set_rate(struct clk_hw *hw, unsigned long rate, unsigned long parent_rate)
{
	struct wm8505_pll_clk *pll = to_wm8505_pll_clk(hw);
	u32 mul, div, reg_val;
	unsigned long flags;

	wm8505_find_pll_bits(rate, parent_rate, &mul, &div);

	spin_lock_irqsave(&wm8505_clk_lock, flags);
	wm8505_pmc_wait_busy();
	reg_val = readl(pmc_base + pll->reg_offset);
	reg_val &= ~0x11f;
	reg_val |= WM8505_BITS_TO_VAL(mul, div);
	writel(reg_val, pmc_base + pll->reg_offset);
	wm8505_pmc_wait_busy();
	spin_unlock_irqrestore(&wm8505_clk_lock, flags);

	return 0;
}

static const struct clk_ops wm8505_pll_ops = {
	.recalc_rate = wm8505_pll_recalc_rate,
	.round_rate  = wm8505_pll_round_rate,
	.set_rate    = wm8505_pll_set_rate,
};

/* Clock Descriptor Tables */
struct wm8505_gate_desc {
	int		id;
	const char	*name;
	const char	*parent_name;
	u32		reg_offset;
	u8		bit_idx;
	unsigned long	flags;
};

struct wm8505_clk_desc {
	int		id;
	const char	*name;
	const char	*parent_name;
	u32		en_reg_offset;
	int		en_bit;
	u32		div_reg_offset;
	unsigned int	div_mask;
	unsigned long	flags;
};

struct wm8505_pll_desc {
	int		id;
	const char	*name;
	const char	*parent_name;
	u32		reg_offset;
};

#define DEF_GATE(_id, _name, _parent, _reg, _bit, _flags) \
	{ .id = _id, .name = _name, .parent_name = _parent, \
	  .reg_offset = _reg, .bit_idx = _bit, .flags = _flags }

#define DEF_DIV(_id, _name, _parent, _div_reg, _div_mask) \
	{ .id = _id, .name = _name, .parent_name = _parent, \
	  .div_reg_offset = _div_reg, .div_mask = _div_mask }

#define DEF_GATED_DIV(_id, _name, _parent, _en_reg, _en_bit, _div_reg, _div_mask, _flags) \
	{ .id = _id, .name = _name, .parent_name = _parent, \
	  .en_reg_offset = _en_reg, .en_bit = _en_bit, \
	  .div_reg_offset = _div_reg, .div_mask = _div_mask, .flags = _flags }

static const struct wm8505_pll_desc wm8505_pll_clks[] __initconst = {
	{ WM8505_CLK_PLLA, "plla", "osc25m", 0x200 },
	{ WM8505_CLK_PLLB, "pllb", "osc25m", 0x204 },
	{ WM8505_CLK_PLLC, "pllc", "osc25m", 0x208 },
	{ WM8505_CLK_PLLD, "plld", "osc25m", 0x20c },
};

static const struct wm8505_gate_desc wm8505_gate_clks[] __initconst = {
	/* PMCEL_REG (0x250) */
	DEF_GATE(WM8505_CLK_UART0, "uart0", "osc24m", PMCEL_REG, 1, 0),
	DEF_GATE(WM8505_CLK_UART1, "uart1", "osc24m", PMCEL_REG, 2, 0),
	DEF_GATE(WM8505_CLK_UART2, "uart2", "osc24m", PMCEL_REG, 3, 0),
	DEF_GATE(WM8505_CLK_UART3, "uart3", "osc24m", PMCEL_REG, 4, 0),
	DEF_GATE(WM8505_CLK_I2CSLAVE, "i2c_slave", "apb", PMCEL_REG, 6, 0),
	DEF_GATE(WM8505_CLK_RTC, "rtc", "apb", PMCEL_REG, 7, CLK_IS_CRITICAL),
	DEF_GATE(WM8505_CLK_GPIO, "gpio", "ahb", PMCEL_REG, 11, CLK_IS_CRITICAL),
	DEF_GATE(WM8505_CLK_I2S, "i2s", "clkaud", PMCEL_REG, 16, 0),
	DEF_GATE(WM8505_CLK_CIR, "cir", "apb", PMCEL_REG, 17, 0),
	DEF_GATE(WM8505_CLK_AC97, "ac97", "clkaud", PMCEL_REG, 19, 0),
	DEF_GATE(WM8505_CLK_SCC, "scc", "ahb", PMCEL_REG, 21, CLK_IS_CRITICAL),
	DEF_GATE(WM8505_CLK_UART4, "uart4", "osc24m", PMCEL_REG, 22, 0),
	DEF_GATE(WM8505_CLK_UART5, "uart5", "osc24m", PMCEL_REG, 23, CLK_IS_CRITICAL),
	DEF_GATE(WM8505_CLK_AMP, "amp", "apb", PMCEL_REG, 24, CLK_IS_CRITICAL),
	DEF_GATE(WM8505_CLK_JENC, "jenc", "ahb", PMCEL_REG, 27, 0),
	DEF_GATE(WM8505_CLK_GE, "ge", "ahb", PMCEL_REG, 29, 0),
	DEF_GATE(WM8505_CLK_GOVRHD, "govrhd", "ahb", PMCEL_REG, 30, 0),

	/* PMCEU_REG (0x254) */
	DEF_GATE(WM8505_CLK_PS2KBDC, "ps2_kbdc", "pllb", PMCEU_REG, 4, 0),
	DEF_GATE(WM8505_CLK_DMA, "dma", "ahb", PMCEU_REG, 5, 0),
	DEF_GATE(WM8505_CLK_UHC, "uhc", "osc24m", PMCEU_REG, 7, CLK_IS_CRITICAL),
	DEF_GATE(WM8505_CLK_UDC, "udc", "osc24m", PMCEU_REG, 8, 0),
	DEF_GATE(WM8505_CLK_PDMA, "pdma", "ahb", PMCEU_REG, 9, 0),
	DEF_GATE(WM8505_CLK_AHBBRIDGE, "ahb_bridge", "ahb", PMCEU_REG, 13, CLK_IS_CRITICAL),
	DEF_GATE(WM8505_CLK_SDTV, "sdtv", "ahb", PMCEU_REG, 14, CLK_IS_CRITICAL),
	DEF_GATE(WM8505_CLK_SYS, "sys", "ahb", PMCEU_REG, 21, 0),
	DEF_GATE(WM8505_CLK_SAE, "sae", "ahb", PMCEU_REG, 24, 0),
	DEF_GATE(WM8505_CLK_ETHPHY, "eth_phy", "ahb", PMCEU_REG, 26, CLK_IS_CRITICAL),
	DEF_GATE(WM8505_CLK_SCL444U, "scl444u", "ahb", PMCEU_REG, 28, 0),
	DEF_GATE(WM8505_CLK_GOVW, "govw", "ahb", PMCEU_REG, 29, 0),
	DEF_GATE(WM8505_CLK_VID, "vid", "ahb", PMCEU_REG, 30, 0),
	DEF_GATE(WM8505_CLK_VPP, "vpp", "ahb", PMCEU_REG, 31, 0),
};

static const struct wm8505_clk_desc wm8505_div_clks[] __initconst = {
	DEF_DIV(WM8505_CLK_ARM, "arm", "plla", 0x300, 0x1f),
	DEF_DIV(WM8505_CLK_AHB, "ahb", "arm", 0x304, 0x7),
	DEF_DIV(WM8505_CLK_APB, "apb", "ahb", 0x350, 0x1f),
};

static const struct wm8505_clk_desc wm8505_gated_div_clks[] __initconst = {
	DEF_GATED_DIV(WM8505_CLK_DDR, "ddr", "plld", PMCEU_REG, 0, 0x310, 0x1f, CLK_IS_CRITICAL),
	DEF_GATED_DIV(WM8505_CLK_SFC, "sfc", "pllb", PMCEU_REG, 23, 0x314, 0x1f, CLK_IS_CRITICAL),
	DEF_GATED_DIV(WM8505_CLK_KEYPAD, "keypad", "pllb", PMCEL_REG, 9, 0x31c, 0x1f, 0),
	DEF_GATED_DIV(WM8505_CLK_SDHC, "sdhc", "pllb", PMCEU_REG, 18, 0x328, 0x3f, 0),
	DEF_GATED_DIV(WM8505_CLK_MAC0, "mac0", "osc25m", PMCEU_REG, 20, 0x32c, 0x1f,
		      CLK_IS_CRITICAL),
	DEF_GATED_DIV(WM8505_CLK_NAND, "nand", "pllb", PMCEU_REG, 16, 0x330, 0x1f, CLK_IS_CRITICAL),
	DEF_GATED_DIV(WM8505_CLK_NORGUP, "nor_gup", "pllb", PMCEU_REG, 3, 0x334, 0x1f,
		      CLK_IS_CRITICAL),
	DEF_GATED_DIV(WM8505_CLK_SPI0, "spi0", "pllb", PMCEL_REG, 12, 0x33c, 0x1f, 0),
	DEF_GATED_DIV(WM8505_CLK_SPI1, "spi1", "pllb", PMCEL_REG, 13, 0x340, 0x1f, 0),
	DEF_GATED_DIV(WM8505_CLK_SPI2, "spi2", "pllb", PMCEL_REG, 14, 0x344, 0x1f, 0),
	DEF_GATED_DIV(WM8505_CLK_PWM, "pwm", "pllb", PMCEL_REG, 10, 0x348, 0x1f, 0),
	DEF_GATED_DIV(WM8505_CLK_NA0, "na0", "pllb", PMCEU_REG, 1, 0x358, 0x1f, CLK_IS_CRITICAL),
	DEF_GATED_DIV(WM8505_CLK_NA12, "na12", "pllb", PMCEU_REG, 2, 0x35c, 0x1f, CLK_IS_CRITICAL),
	DEF_GATED_DIV(WM8505_CLK_I2C0, "i2c0", "pllb", PMCEL_REG, 5, 0x36c, 0x1f, 0),
	DEF_GATED_DIV(WM8505_CLK_I2C1, "i2c1", "pllb", PMCEL_REG, 0, 0x370, 0x1f, 0),
	DEF_GATED_DIV(WM8505_CLK_DVO, "dvo", "pllc", PMCEL_REG, 18, 0x374, 0x1f,
		      CLK_IS_CRITICAL | CLK_SET_RATE_PARENT),
};

/**
 * wm8505_clk_init - initialize the WM8505 common clock tree
 */
static void __init wm8505_clk_init(struct device_node *np)
{
	struct clk_hw_onecell_data *clk_data;
	int i, ret;

	pmc_base = of_iomap(np, 0);
	if (!pmc_base) {
		pr_err("%s: Failed to map PMC base\n", __func__);
		return;
	}

	clk_data = kzalloc(struct_size(clk_data, hws, WM8505_CLK_MAX), GFP_KERNEL);
	if (!clk_data) {
		iounmap(pmc_base);
		return;
	}

	for (i = 0; i < WM8505_CLK_MAX; i++)
		clk_data->hws[i] = ERR_PTR(-ENOENT);

	clk_data->num = WM8505_CLK_MAX;

	/* Initialize phase-locked loops */
	for (i = 0; i < ARRAY_SIZE(wm8505_pll_clks); i++) {
		const struct wm8505_pll_desc *desc = &wm8505_pll_clks[i];
		struct wm8505_pll_clk *pll = kzalloc(sizeof(*pll), GFP_KERNEL);
		struct clk_parent_data pdata = { .name = desc->parent_name };
		struct clk_init_data init = {
			.name = desc->name,
			.ops = &wm8505_pll_ops,
			.parent_data = &pdata,
			.num_parents = 1,
		};

		if (!pll)
			continue;

		pll->reg_offset = desc->reg_offset;
		pll->hw.init = &init;

		ret = clk_hw_register(NULL, &pll->hw);
		if (!ret)
			clk_data->hws[desc->id] = &pll->hw;
		else
			kfree(pll);
	}

	/* Initialize pure divisors */
	for (i = 0; i < ARRAY_SIZE(wm8505_div_clks); i++) {
		const struct wm8505_clk_desc *desc = &wm8505_div_clks[i];
		struct wm8505_clk *cdev = kzalloc(sizeof(*cdev), GFP_KERNEL);
		struct clk_parent_data pdata = { .name = desc->parent_name };
		struct clk_init_data init = {
			.name = desc->name,
			.ops = &wm8505_div_ops,
			.parent_data = &pdata,
			.num_parents = 1,
		};

		if (!cdev)
			continue;

		cdev->div_reg_offset = desc->div_reg_offset;
		cdev->div_mask = desc->div_mask;
		cdev->hw.init = &init;

		ret = clk_hw_register(NULL, &cdev->hw);
		if (!ret)
			clk_data->hws[desc->id] = &cdev->hw;
		else
			kfree(cdev);
	}

	/* Initialize standard gated clocks */
	for (i = 0; i < ARRAY_SIZE(wm8505_gate_clks); i++) {
		const struct wm8505_gate_desc *desc = &wm8505_gate_clks[i];
		struct clk_hw *hw;

		hw = clk_hw_register_gate(NULL, desc->name, desc->parent_name,
					  desc->flags, pmc_base + desc->reg_offset,
					  desc->bit_idx, 0, &wm8505_clk_lock);

		if (!IS_ERR(hw))
			clk_data->hws[desc->id] = hw;
	}

	/* Initialize composite gated divisors */
	for (i = 0; i < ARRAY_SIZE(wm8505_gated_div_clks); i++) {
		const struct wm8505_clk_desc *desc = &wm8505_gated_div_clks[i];
		struct wm8505_clk *cdev = kzalloc(sizeof(*cdev), GFP_KERNEL);
		struct clk_parent_data pdata = { .name = desc->parent_name };
		struct clk_init_data init = {
			.name = desc->name,
			.ops = &wm8505_gated_div_ops,
			.parent_data = &pdata,
			.num_parents = 1,
			.flags = desc->flags,
		};

		if (!cdev)
			continue;

		cdev->en_reg_offset = desc->en_reg_offset;
		cdev->en_bit = desc->en_bit;
		cdev->div_reg_offset = desc->div_reg_offset;
		cdev->div_mask = desc->div_mask;
		cdev->hw.init = &init;

		ret = clk_hw_register(NULL, &cdev->hw);
		if (!ret)
			clk_data->hws[desc->id] = &cdev->hw;
		else
			kfree(cdev);
	}

	/* Publish provider */
	of_clk_add_hw_provider(np, of_clk_hw_onecell_get, clk_data);
}

/* Early init required for core system timer */
CLK_OF_DECLARE(wm8505_pmc, "wm,wm8505-pmc", wm8505_clk_init);
