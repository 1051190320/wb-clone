#pragma once
/*
 * wb-clone 传输通道抽象
 *   SerialTransport : USB CDC 串口 (原版体验, 插线即用)
 *   NetTransport    : WiFi + MQTT (无线, 见 wb_net.h)
 * 两个通道可同时存在; USB 插着优先显示 USB 链路状态。
 */
#include <Arduino.h>

// 收到一帧后的处理 (主程序实现): 解析 + 重绘 + 时钟基准
void onFrame(const char* line);

class Transport {
public:
  virtual ~Transport() {}
  virtual void begin() = 0;
  virtual void loop() = 0;
  virtual bool connected() = 0;        // 链路是否就绪 (用于断开检测)
  virtual const char* modeName() = 0;  // "USB" / "WiFi"
};

// ---------------- USB CDC 串口通道 ----------------
class SerialTransport : public Transport {
  uint32_t lastRxMs = 0;

public:
  void begin() override {}
  void loop() override {
    while (Serial.available() > 0) {
      String line = Serial.readStringUntil('\n');
      line.trim();
      if (line.length() >= 2 && line[0] == '{') {
        onFrame(line.c_str());
        lastRxMs = millis();
      }
    }
  }
  bool connected() override { return millis() - lastRxMs < 5000; }
  const char* modeName() override { return "USB"; }
};
