/* SPDX-License-Identifier: (GPL-2.0-only OR BSD-2-Clause) */
/*
 * Device Tree bindings for WonderMedia WM8505 Clock Controller
 *
 * Copyright (C) 2026 Logan Russell <me@lrussell.net>
 */

#ifndef __DT_BINDINGS_CLOCK_WM8505_H
#define __DT_BINDINGS_CLOCK_WM8505_H

/* Phase-Locked Loops */
#define WM8505_CLK_PLLA		0
#define WM8505_CLK_PLLB		1
#define WM8505_CLK_PLLC		2
#define WM8505_CLK_PLLD		3

/* Pure Divisor Clocks */
#define WM8505_CLK_ARM		4
#define WM8505_CLK_AHB		5
#define WM8505_CLK_APB		6

/* PMCEL_REG (0x250) Gated Clocks */
#define WM8505_CLK_UART0	7
#define WM8505_CLK_UART1	8
#define WM8505_CLK_UART2	9
#define WM8505_CLK_UART3	10
#define WM8505_CLK_I2CSLAVE	11
#define WM8505_CLK_RTC		12
#define WM8505_CLK_KEYPAD	13
#define WM8505_CLK_GPIO		14
#define WM8505_CLK_I2S		15
#define WM8505_CLK_CIR		16
#define WM8505_CLK_AC97		17
#define WM8505_CLK_SCC		18
#define WM8505_CLK_UART4	19
#define WM8505_CLK_UART5	20
#define WM8505_CLK_AMP		21
#define WM8505_CLK_JENC		22
#define WM8505_CLK_GE		23
#define WM8505_CLK_GOVRHD	24

/* PMCEU_REG (0x254) Gated Clocks */
#define WM8505_CLK_DMA		25
#define WM8505_CLK_UHC		26
#define WM8505_CLK_UDC		27
#define WM8505_CLK_PDMA		28
#define WM8505_CLK_AHBBRIDGE	29
#define WM8505_CLK_SDTV		30
#define WM8505_CLK_SYS		31
#define WM8505_CLK_SAE		32
#define WM8505_CLK_ETHPHY	33
#define WM8505_CLK_SCL444U	34
#define WM8505_CLK_GOVW		35
#define WM8505_CLK_VID		36
#define WM8505_CLK_VPP		37

/* Gated Divisor Clocks (Composite) */
#define WM8505_CLK_DDR		38
#define WM8505_CLK_SFC		39
#define WM8505_CLK_PS2KBDC	40
#define WM8505_CLK_SDHC		41
#define WM8505_CLK_MAC0		42
#define WM8505_CLK_NAND		43
#define WM8505_CLK_NORGUP	44
#define WM8505_CLK_SPI0		45
#define WM8505_CLK_SPI1		46
#define WM8505_CLK_SPI2		47
#define WM8505_CLK_PWM		48
#define WM8505_CLK_NA0		49
#define WM8505_CLK_NA12		50
#define WM8505_CLK_I2C0		51
#define WM8505_CLK_I2C1		52
#define WM8505_CLK_DVO		53

#define WM8505_CLK_MAX		54

#endif /* __DT_BINDINGS_CLOCK_WM8505_H */
