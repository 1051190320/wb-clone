/* mock 环境定义: 虚拟时钟 / Serial / WiFi / Preferences / ESP */
#include "Arduino.h"
#include "WiFi.h"
#include "Preferences.h"
#include <cstring>
#include <map>
#include <string>

unsigned long g_millis = 0;
int g_restart_count = 0;
int g_wifi_status = WL_CONNECTED;
int g_wifi_begin_count = 0;
int g_connect_count = 0;
std::map<std::string, std::string> g_nvs;   // 全局 NVS 存储
WiFiClass WiFi;
EspRestart ESP;

std::string WiFiClass::g_ap_ssid;
std::string WiFiClass::g_ap_pass;

/* 可编程 Serial mock: 预先喂入缓冲区, readStringUntil 按行消费 */
static char s_buf[4096];
static size_t s_len = 0, s_pos = 0;

void mockSerialFeed(const char *data) {
  strncpy(s_buf, data, sizeof(s_buf) - 1);
  s_buf[sizeof(s_buf) - 1] = 0;
  s_len = strlen(s_buf);
  s_pos = 0;
}

HWCDCMock Serial;

int HWCDCMock::available() { return (int)(s_len - s_pos); }

String HWCDCMock::readStringUntil(char term) {
  String out;
  size_t start = s_pos;
  while (s_pos < s_len && s_buf[s_pos] != term) s_pos++;
  size_t n = s_pos - start;
  if (s_pos < s_len) s_pos++;  // 跳过终止符
  char tmp[512];
  if (n >= sizeof(tmp)) n = sizeof(tmp) - 1;
  memcpy(tmp, s_buf + start, n);
  tmp[n] = 0;
  out = String(tmp);
  return out;
}
