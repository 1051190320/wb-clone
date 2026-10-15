#pragma once
/* WiFiClient mock — 内存模拟 TCP: serverPush 注入下行数据, txBuf 捕获上行数据 */
#include <cstdint>
#include <cstring>
#include <cstddef>
#include <algorithm>

extern int g_connect_count;

class WiFiClient {
public:
  static const int CAP = 2048;
  uint8_t rxBuf[CAP];
  uint8_t txBuf[CAP];
  int rxLen = 0, rxPos = 0, txLen = 0;
  bool conn = false;

  int connect(const char* host, uint16_t port) {
    (void)host; (void)port;
    conn = true; rxLen = rxPos = txLen = 0;
    g_connect_count++;
    return 1;
  }
  void stop() { conn = false; }
  bool connected() { return conn; }
  int available() { return rxLen - rxPos; }
  int read() { return rxPos < rxLen ? rxBuf[rxPos++] : -1; }
  size_t write(const uint8_t* buf, size_t size) {
    int n = std::min((int)size, CAP - txLen);
    if (n <= 0) return 0;
    memcpy(txBuf + txLen, buf, n);
    txLen += n;
    return n;
  }
  size_t write(uint8_t b) { return write(&b, 1); }
  void flush() {}

  // ---- 测试辅助 ----
  void serverPush(const uint8_t* data, int len) {
    int n = std::min(len, CAP - rxLen);
    if (n > 0) { memcpy(rxBuf + rxLen, data, n); rxLen += n; }
  }
  void serverPush(const char* s) { serverPush((const uint8_t*)s, (int)strlen(s)); }
  void clearTx() { txLen = 0; }
  // 查找上行数据中的子串
  int txFind(const char* s) {
    int n = (int)strlen(s);
    for (int i = 0; i + n <= txLen; i++)
      if (memcmp(txBuf + i, s, n) == 0) return i;
    return -1;
  }
};
