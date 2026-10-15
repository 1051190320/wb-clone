#pragma once
/* Arduino API mock — 仅用于 g++ 语法/类型检查, 不参与实际固件构建 */
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <string>

#define PROGMEM
#define pgm_read_byte(a)  (*(const uint8_t *)(a))
#define pgm_read_word(a)  (*(const uint16_t *)(a))
#define F(x) (x)
#define FPSTR(x) (x)

#ifndef min
template <typename A, typename B>
constexpr const A& min(const A& a, const B& b) { return a < b ? a : b; }
#endif
#ifndef max
template <typename A, typename B>
constexpr const A& max(const A& a, const B& b) { return a > b ? a : b; }
#endif

typedef uint8_t byte;

#define OUTPUT 1
#define HIGH 1
#define LOW 0

extern unsigned long g_millis;      // 测试可推进的虚拟时钟
extern int g_restart_count;         // ESP.restart 调用计数
inline unsigned long millis() { return g_millis; }

inline void pinMode(int, int) {}
inline void digitalWrite(int, int) {}
inline void delay(unsigned long ms) { g_millis += ms; }
inline size_t strlcpy(char *d, const char *s, size_t n) {
  size_t i = 0;
  for (; i + 1 < n && s[i]; i++) d[i] = s[i];
  if (n) d[i] = 0;
  return strlen(s);
}

class String {
  char buf[1024];
  size_t len = 0;
public:
  String() { buf[0] = 0; }
  String(const char *s) {
    strncpy(buf, s, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = 0;
    len = strlen(buf);
  }
  size_t length() const { return len; }
  const char *c_str() const { return buf; }
  char operator[](size_t i) const { return buf[i]; }
  int toInt() const { return atoi(buf); }
  String operator+(const char *s) const {
    String r(*this);
    strncat(r.buf, s, sizeof(r.buf) - r.len - 1);
    r.len = strlen(r.buf);
    return r;
  }
  void trim() {
    size_t b = 0;
    while (buf[b] == ' ' || buf[b] == '\t' || buf[b] == '\r' || buf[b] == '\n') b++;
    size_t e = len;
    while (e > b && (buf[e-1] == ' ' || buf[e-1] == '\t' ||
                     buf[e-1] == '\r' || buf[e-1] == '\n')) e--;
    memmove(buf, buf + b, e - b);
    buf[e - b] = 0;
    len = e - b;
  }
};

class HWCDCMock {
public:
  void begin(int) {}
  int available();                 // 定义见 mock_serial.cpp
  String readStringUntil(char);    // 定义见 mock_serial.cpp
};
extern HWCDCMock Serial;

/* ESP 重启 (mock: 计数) */
struct EspRestart { void restart() { g_restart_count++; } };
extern EspRestart ESP;
