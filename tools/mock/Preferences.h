#pragma once
/* Preferences mock — 全局共享 NVS 存储 (支持跨实例 save/load 往返) */
#include <cstdint>
#include <cstdlib>
#include <map>
#include <string>
#include <Arduino.h>

extern std::map<std::string, std::string> g_nvs;   // 全局 NVS (mock_env.cpp 定义)

class Preferences {
public:
  bool begun = false;

  bool begin(const char* name, bool readOnly) {
    (void)name; (void)readOnly;
    begun = true;
    return true;
  }
  void end() { begun = false; }
  void putString(const char* k, const char* v) { g_nvs[k] = v ? v : ""; }
  void putUShort(const char* k, uint16_t v) { g_nvs[k] = std::to_string(v); }
  String getString(const char* k, const char* def = "") {
    auto it = g_nvs.find(k);
    return String(it != g_nvs.end() ? it->second.c_str() : def);
  }
  uint16_t getUShort(const char* k, uint16_t def) {
    auto it = g_nvs.find(k);
    return it != g_nvs.end() ? (uint16_t)atoi(it->second.c_str()) : def;
  }
};
