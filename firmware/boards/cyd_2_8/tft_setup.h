/*
 * TFT_eSPI 配置 — 2.8" CYD 黄板 (ESP32-WROOM-32 + ILI9341 320x240)
 * =================================================================
 * 文件名必须是 tft_setup.h (机制说明见 boards/esp32c3_gc9a01/tft_setup.h)
 * 选用方式: build_flags 里的 `-I boards/cyd_2_8`
 *
 * 显示 HSPI 引脚 (多份官方资料一致):
 *   CS=15  DC=2  RST=-1(接 EN, 个别批次为 GPIO4)  MOSI=13  SCLK=14  MISO=12  BL=21
 */
#define USER_SETUP_LOADED
#define ILI9341_DRIVER

#define TFT_WIDTH  240      // 逻辑宽 (竖屏)
#define TFT_HEIGHT 320      // 逻辑高

// ---- 显示 SPI 引脚 (CYD 2.8" 固定) ----
#define TFT_MISO  12
#define TFT_MOSI  13
#define TFT_SCLK  14
#define TFT_CS    15
#define TFT_DC    2
#define TFT_RST   -1        // 多数 CYD 接 EN; 少数批次为 GPIO4
#define TFT_BL    21        // 背光, 高电平点亮
#define TFT_BACKLIGHT_ON HIGH

#define SPI_FREQUENCY        40000000   // ILI9341 典型上限 40MHz
#define SPI_READ_FREQUENCY   20000000
#define SPI_TOUCH_FREQUENCY   2500000

// ---- 字体 (本工程主要用自带 16x16 点阵, 此处仅作兜底) ----
#define LOAD_GLCD
#define LOAD_FONT2
#define LOAD_FONT4
#define LOAD_FONT6
#define LOAD_FONT8
#define LOAD_GFXFF
#define SMOOTH_FONT
