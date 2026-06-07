/* SPDX-License-Identifier: (GPL-2.0-only OR BSD-2-Clause) */
/*
 * Device Tree bindings for WonderMedia WM8505 Clock Controller
 *
 * Copyright (C) 2026 Logan Russell <me@lrussell.net>
 */

#ifndef __DT_BINDINGS_CLOCK_WM8505_H
#define __DT_BINDINGS_CLOCK_WM8505_H

/* Fixed Rate Reference Clocks */
#define WM8505_CLK_REF24		0
#define WM8505_CLK_REF25		1

/* Phase-Locked Loops */
#define WM8505_CLK_PLLA			2
#define WM8505_CLK_PLLB			3
#define WM8505_CLK_PLLC			4
#define WM8505_CLK_PLLD			5

/* Pure Divisor Clocks */
#define WM8505_CLK_ARM			6
#define WM8505_CLK_AHB			7
#define WM8505_CLK_APB			8

/* PMCEL_REG (0x250) Gated Clocks */
#define WM8505_CLK_UART0		9
#define WM8505_CLK_UART1		10
#define WM8505_CLK_UART2		11
#define WM8505_CLK_UART3		12
#define WM8505_CLK_I2CSLAVE		13
#define WM8505_CLK_RTC			14
#define WM8505_CLK_GPIO			15
#define WM8505_CLK_I2S			16
#define WM8505_CLK_CIR			17
#define WM8505_CLK_AC97			18
#define WM8505_CLK_SCC			19
#define WM8505_CLK_UART4		20
#define WM8505_CLK_UART5		21
#define WM8505_CLK_AMP			22
#define WM8505_CLK_JENC			23
#define WM8505_CLK_GE			24
#define WM8505_CLK_GOVRHD		25

/* PMCEU_REG (0x254) Gated Clocks */
#define WM8505_CLK_PS2KBDC		26
#define WM8505_CLK_DMA			27
#define WM8505_CLK_UHC			28
#define WM8505_CLK_UDC			29
#define WM8505_CLK_PDMA			30
#define WM8505_CLK_AHBBRIDGE		31
#define WM8505_CLK_SDTV			32
#define WM8505_CLK_SYS			33
#define WM8505_CLK_SAE			34
#define WM8505_CLK_ETHPHY		35
#define WM8505_CLK_SCL444U		36
#define WM8505_CLK_GOVW			37
#define WM8505_CLK_VID			38
#define WM8505_CLK_VPP			39

/* Gated Divisor Clocks (Composite) */
#define WM8505_CLK_DDR			40
#define WM8505_CLK_SFC			41
#define WM8505_CLK_KEYPAD		42
#define WM8505_CLK_SDHC			43
#define WM8505_CLK_MAC0			44
#define WM8505_CLK_NAND			45
#define WM8505_CLK_NORGUP		46
#define WM8505_CLK_SPI0			47
#define WM8505_CLK_SPI1			48
#define WM8505_CLK_SPI2			49
#define WM8505_CLK_PWM			50
#define WM8505_CLK_NA0			51
#define WM8505_CLK_NA12			52
#define WM8505_CLK_I2C0			53
#define WM8505_CLK_I2C1			54
#define WM8505_CLK_DVO			55

#define WM8505_CLK_MAX			56

#endif /* __DT_BINDINGS_CLOCK_WM8505_H */
