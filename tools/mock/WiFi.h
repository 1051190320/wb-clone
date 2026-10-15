#pragma once
/* WiFi mock — 仅用于 g++ 语法/类型检查与协议测试 */
#include <cstdint>
#include <cstring>
#include <string>

#define WL_CONNECTED 3
#define WIFI_STA 1
#define WIFI_AP 2

extern int g_wifi_status;      // 测试可切换 (WL_CONNECTED / 其他)
extern int g_wifi_begin_count;

class WiFiClass {
public:
  int status() { return g_wifi_status; }
  void mode(int m) { g_mode = m; }
  int getMode() const { return g_mode; }
  void begin(const char* ssid, const char* pass = nullptr) {
    (void)ssid; (void)pass; g_wifi_begin_count++;
  }
  bool softAP(const char* ssid, const char* pass = nullptr) {
    g_ap_ssid = ssid ? ssid : ""; g_ap_pass = pass ? pass : ""; return true;
  }
  const char* getApSsid() const { return g_ap_ssid.c_str(); }
  const char* softAPIP() { return "192.168.4.1"; }
  const char* localIP() { return "192.168.1.50"; }
  void macAddress(uint8_t mac[6]) const {
    static const uint8_t m[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0x12, 0x34};
    memcpy(mac, m, 6);
  }
private:
  int g_mode = WIFI_STA;
  static std::string g_ap_ssid, g_ap_pass;
};

extern WiFiClass WiFi;
