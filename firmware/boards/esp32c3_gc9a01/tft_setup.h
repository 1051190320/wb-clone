/*
 * TFT_eSPI 配置 — ESP32-C3 SuperMini + 1.28" GC9A01 圆屏 (240x240, SPI)
 * ======================================================================
 * 文件名必须是 tft_setup.h: TFT_eSPI.h 里有
 *     #if __has_include(<tft_setup.h>)  ->  include  ->  USER_SETUP_LOADED
 * 只要 PlatformIO 的 build_flags 带 `-I boards/esp32c3_gc9a01`, 库就会自动
 * 采用本文件并跳过自带的 User_Setup.h, **无需改动/复制库文件**。
 *
 * Arduino IDE 用户: 把本文件内容复制到 TFT_eSPI 库根目录的 User_Setup.h 即可
 *                   (或用 firmware/scripts/install_tft_setup.sh 自动完成)
 */
#define USER_SETUP_LOADED   // 明确标记: 配置已由此文件提供
#define GC9A01_DRIVER

#define TFT_WIDTH  240
#define TFT_HEIGHT 240

// ---- 显示 SPI 引脚 (用户实测接线) ----
#define TFT_MOSI  3         // SDA
#define TFT_SCLK  4         // SCL
#define TFT_CS    1
#define TFT_DC    10
#define TFT_RST   0         // C3 strapping 脚; TFT_eSPI 仅初始化时脉冲驱动, 不影响启动
#define TFT_BL    -1        // 未接背光 (见 board.h 说明)
#define TFT_BACKLIGHT_ON HIGH

#define SPI_FREQUENCY        40000000   // GC9A01 常见上限; 花屏/闪烁降到 27000000
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
