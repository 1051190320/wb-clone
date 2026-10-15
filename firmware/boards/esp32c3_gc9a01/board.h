#pragma once
/*
 * 硬件分区配置 — ESP32-C3 SuperMini + 1.28" GC9A01 圆屏
 * =====================================================
 * 选用方式: platformio.ini 的 build_flags 里有 `-I boards/esp32c3_gc9a01`
 *           (同一个 -I 既让 TFT_eSPI 找到 tft_setup.h, 也让固件找到本 board.h)
 *
 * 接线 ( ← 用户实测表):
 *   GC9A01     ESP32-C3 SuperMini
 *   VCC        3V3
 *   GND        GND
 *   RST        GPIO0
 *   CS         GPIO1
 *   DC         GPIO10
 *   SDA(MOSI)  GPIO3
 *   SCL(SCLK)  GPIO4
 *   BL         未接 (该批次模块背光直连 VCC; 若模块有 BL 且偏暗,
 *              可接空闲 GPIO 如 GPIO7 并把 WB_BL_PIN / TFT_BL 改成 7。
 *              C3 的 strapping 脚 GPIO2/8/9 不建议用作 BL)
 */
#define WB_BOARD_NAME "esp32c3_gc9a01"
#define SCR_W      240      // 屏幕逻辑宽 (圆屏)
#define SCR_H      240      // 屏幕逻辑高
#define WB_ROT     0        // 圆屏用原生方向; 显示镜像/倒置时改 1 ~ 3
#define WB_BL_PIN  -1       // 背光 GPIO; -1 = 未接线, 跳过背光控制
#define WB_USB_CDC 1        // C3 原生 USB-Serial-JTAG (Serial 走 USB, 无需 CH340)
