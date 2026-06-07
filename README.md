# linux-wmt
This repository is a Linux kernel fork dedicated to bringing modern support to the **WonderMedia WM8505**, specifically targeting the early-2010s ARM netbooks built around this SoC.

This project modernizes WM8505 support by migrating it to current kernel frameworks and writing new drivers for the specific hardware found in these devices.

> **⚠️ Note:** This branch is frequently rebased to track upstream Linux releases and maintain a clean patch stack.

## Supported Hardware & Features

*   **Display & Graphics (DRM/KMS)**
    *   Display driver supporting the built-in LCD panel with hardware vblank synchronization.
    *   2D Graphics Engine (GE) support for asynchronous hardware-accelerated solid fills and bitblts.
    *   Hardware-accelerated framebuffer console (`fbcon`) offloaded to the GE.
*   **Audio (ASoC)**
    *   AC97 audio interface bound to the internal VT1613 codec with DAPM support.
    *   DMA-backed PCM playback and capture, including internal speaker and headphone support.
*   **Input**
    *   PS/2 driver handling the built-in keyboard and touchpad.
*   **Storage**
    *   SD/MMC controller support for the SD card slot.
    *   Serial Flash Controller (SFC) for internal SPI flash access.
*   **Power**
    *   Battery monitor reporting charge, status, and a low-battery alarm.
    *   Voltage gauged without an ADC by measuring an RC circuit's rise time on a GPIO.
*   **Core SoC**
    *   **Clocks:** Centralized Common Clock Framework (CCF) driver managing the full SoC clock tree.
    *   **DMA:** Hardware scatter-gather DMA engine support for peripheral and memory-to-memory transfers.
