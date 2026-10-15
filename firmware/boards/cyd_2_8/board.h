#pragma once
/*
 * 硬件分区配置 — 2.8" CYD 黄板 (Sunton ESP32-2432S028R)
 * ======================================================
 * 选用方式: platformio.ini 的 build_flags 里有 `-I boards/cyd_2_8`
 *
 *   MCU  : ESP32-WROOM-32
 *   屏幕 : ILI9341 240x320 (SPI, 横屏 320x240)
 *   USB  : CH340 转串口 (UART0, GPIO1/3) — 编程 + 与 Bridge 通信共用
 * 本工程不使用触摸/SD, 故未配置 XPT2046。
 */
#define WB_BOARD_NAME "cyd_2_8"
#define SCR_W      320      // 横屏 320x240 (ILI9341 原生方向)
#define SCR_H      240
#define WB_ROT     1        // 横屏; 若方向不对改 0 或 3
#define WB_BL_PIN  21       // CYD 背光 GPIO21, 高电平点亮
#define WB_USB_CDC 0        // CH340 串口 (UART0)
