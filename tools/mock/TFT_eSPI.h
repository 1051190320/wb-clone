#pragma once
/* TFT_eSPI mock — g++ 在环测试用; 记录 flush 相关调用供断言 */
#include <cstdint>

#define TFT_BLACK 0x0000

class TFT_eSPI {
public:
  TFT_eSPI() {}
  void init() {}
  void setRotation(int) {}
  void fillScreen(uint16_t color) { (void)color; }
  uint16_t color565(uint8_t r, uint8_t g, uint8_t b) { (void)r; (void)g; (void)b; return 0; }
  void fillRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t c) {
    (void)x; (void)y; (void)w; (void)h; (void)c;
  }
  void fillRoundRect(int16_t x, int16_t y, int16_t w, int16_t h, int16_t r, uint16_t c) {
    (void)x; (void)y; (void)w; (void)h; (void)r; (void)c;
  }
  void drawRoundRect(int16_t x, int16_t y, int16_t w, int16_t h, int16_t r, uint16_t c) {
    (void)x; (void)y; (void)w; (void)h; (void)r; (void)c;
  }
  void fillCircle(int16_t x, int16_t y, int16_t r, uint16_t c) { (void)x; (void)y; (void)r; (void)c; }
  void drawFastHLine(int16_t x, int16_t y, int16_t w, uint16_t c) { (void)x; (void)y; (void)w; (void)c; }
  void drawPixel(int16_t x, int16_t y, uint16_t c) { (void)x; (void)y; (void)c; }

  // ---- LVGL flush 通道 (记录调用) ----
  void startWrite() { write_count++; }
  void endWrite() {}
  void setAddrWindow(int32_t x, int32_t y, int32_t w, int32_t h) {
    ax = x; ay = y; aw = w; ah = h;
  }
  void pushColors(uint16_t* data, uint32_t len, bool first) {
    pushed_pixels += len; last_data = data; last_len = len; last_first = first;
  }

  // 断言用状态
  int write_count = 0;
  uint32_t pushed_pixels = 0;
  int32_t ax = -1, ay = -1, aw = -1, ah = -1;
  uint16_t* last_data = nullptr;
  uint32_t last_len = 0;
  bool last_first = false;
};
