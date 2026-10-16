/*
 * wb-clone 显示端固件
 * 硬件: 2.8 寸 CYD 黄板 (ESP32-2432S028R, ESP32-WROOM-32 + ILI9341 320x240 横屏)
 * UI:   LVGL v9 (对象化绘制, 点阵字库经 lv_font.h 接入)
 * 双通道:
 *   USB  : CH340 串口 (UART0), 一行一个 JSON 帧 (插线即用, 向后兼容)
 *   WiFi : MQTT 订阅 (wb_net.h), 首次使用 AP 配网
 * 依赖: TFT_eSPI + lvgl (PlatformIO 自动拉取) + ESP32 核心自带库
 * 注: 本工程不接触摸/SD, 用不到 XPT2046; CYD 上的触摸芯片无需初始化。
 */
#include <Arduino.h>
#include <FS.h>          // 必须在 TFT_eSPI.h 之前: 其 SMOOTH_FONT 分支会定义 FS_NO_GLOBALS,
                           // 之后再无 FS 全局别名, WebServer.h 的 ETagFunction 会编译失败
#include <TFT_eSPI.h>
#include <lvgl.h>
#include "board.h"          // 硬件分区配置: WB_BL_PIN / WB_ROT (由 -I boards/<板型> 决定)
#include "ui_lvgl.h"
#include "transport.h"
#include "wb_net.h"
#include "wb_ota.h"

TFT_eSPI tft = TFT_eSPI();
WbState wb;
const char* g_linkMode = "USB";

// 背光 / 屏幕方向来自 boards/<板型>/board.h (WB_BL_PIN = -1 表示未接线)
#define LINK_TIMEOUT_MS 5000

// ================= 极简 JSON 行解析 (协议子集) =================
struct JParser {
  const char* p;
  void ws() { while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++; }
  char peek() { ws(); return *p; }

  // 解析 "..." 到 out (自动截断, 支持 \uXXXX -> UTF-8)
  bool str(char* out, int maxLen) {
    ws();
    if (*p != '"') return false;
    p++;
    int n = 0;
    while (*p && *p != '"') {
      char c = *p;
      if (c == '\\' && p[1]) {
        p++;
        char e = *p++;
        switch (e) {
          case 'n': c = '\n'; break;
          case 't': c = '\t'; break;
          case 'r': c = '\r'; break;
          case '"': c = '"';  break;
          case '\\': c = '\\'; break;
          case '/': c = '/';  break;
          case 'u': {
            uint32_t cp = 0;
            for (int i = 0; i < 4 && *p; i++, p++) {
              char h = *p; cp <<= 4;
              if      (h >= '0' && h <= '9') cp |= (h - '0');
              else if (h >= 'a' && h <= 'f') cp |= (h - 'a' + 10);
              else if (h >= 'A' && h <= 'F') cp |= (h - 'A' + 10);
            }
            if (n + 4 < maxLen) {              // BMP -> UTF-8 (1/2/3 字节)
              if      (cp < 0x80)  { out[n++] = (char)cp; }
              else if (cp < 0x800) { out[n++] = (char)(0xC0 | (cp >> 6));
                                     out[n++] = (char)(0x80 | (cp & 0x3F)); }
              else                 { out[n++] = (char)(0xE0 | (cp >> 12));
                                     out[n++] = (char)(0x80 | ((cp >> 6) & 0x3F));
                                     out[n++] = (char)(0x80 | (cp & 0x3F)); }
            }
            continue;
          }
          default: c = e;
        }
      } else {
        p++;
      }
      if (n + 1 < maxLen) out[n++] = c;
    }
    if (*p == '"') p++;
    out[n] = 0;
    return true;
  }

  long integer() {
    ws();
    bool neg = false;
    if (*p == '-') { neg = true; p++; }
    long v = 0;
    while (*p >= '0' && *p <= '9') v = v * 10 + (*p++ - '0');
    return neg ? -v : v;
  }

  // 跳过任意 JSON 值
  void skipValue() {
    ws();
    if (*p == '"') {                      // 跳过字符串
      p++;
      while (*p && *p != '"') {
        if (*p == '\\' && p[1]) p++;
        p++;
      }
      if (*p == '"') p++;
      return;
    }
    if (*p == '{' || *p == '[') {         // 跳过对象/数组 (含嵌套字符串)
      char open = *p, close = (open == '{') ? '}' : ']';
      int depth = 0;
      while (*p) {
        if (*p == '"') {
          p++;
          while (*p && *p != '"') {
            if (*p == '\\' && p[1]) p++;
            p++;
          }
          if (*p == '"') p++;
          continue;
        }
        if (*p == open) depth++;
        else if (*p == close) { depth--; if (depth == 0) { p++; return; } }
        p++;
      }
      return;
    }
    while (*p && *p != ',' && *p != '}' && *p != ']') p++;  // true/false/null/数字
  }
};

void parseLines(JParser& jp) {
  jp.ws();
  if (*jp.p != '[') { jp.skipValue(); return; }
  jp.p++;
  jp.ws();
  if (*jp.p == ']') { jp.p++; return; }
  while (true) {
    jp.ws();
    if (*jp.p != '{') break;
    jp.p++;
    WbLogLine L{};   // 零初始化
    jp.ws();
    if (*jp.p != '}') {
      while (true) {
        char key[12];
        if (!jp.str(key, sizeof(key))) break;
        if (jp.peek() != ':') break;
        jp.p++;
        jp.ws();
        if      (!strcmp(key, "t")) jp.str(L.t, sizeof(L.t));
        else if (!strcmp(key, "x")) jp.str(L.x, sizeof(L.x));
        else if (!strcmp(key, "c")) L.c = (uint8_t)jp.integer();
        else if (!strcmp(key, "s")) jp.str(L.s, sizeof(L.s));
        else jp.skipValue();
        jp.ws();
        if (*jp.p == ',') { jp.p++; continue; }
        break;
      }
    }
    if (*jp.p == '}') jp.p++;
    if (wb.nlines < 4) wb.lines[wb.nlines++] = L;
    jp.ws();
    if (*jp.p == ',') { jp.p++; continue; }
    break;
  }
  if (*jp.p == ']') jp.p++;
}

// 解析一帧, 成功返回 true (原地更新全局 wb)
// 语义: 每帧是完整快照, 未下发字段重置为默认值 (clock 例外, 保留上次值)
bool parseFrame(const char* line) {
  JParser jp{line};
  if (jp.peek() != '{') return false;
  jp.p++;
  // ---- 快照重置 ----
  wb.st      = ST_IDLE;
  wb.cpu     = 0;
  wb.mem     = 0;
  wb.disk    = 0;
  wb.title[0] = 0;
  wb.sub[0]   = 0;
  wb.agent[0] = 0;
  wb.up[0]    = 0;
  wb.net[0]   = 0;
  wb.dur[0]   = 0;
  wb.model[0] = 0;
  wb.task[0]  = 0;
  wb.bal[0]   = 0;
  wb.spend[0] = 0;
  wb.cost[0]  = 0;
  wb.nlines  = 0;
  wb.pg      = -1;
  jp.ws();
  if (jp.peek() == '}') { jp.p++; return true; }
  while (true) {
    char key[16];
    if (!jp.str(key, sizeof(key))) return false;
    if (jp.peek() != ':') return false;
    jp.p++;
    jp.ws();
    if      (!strcmp(key, "clock")) jp.str(wb.clock, sizeof(wb.clock));
    else if (!strcmp(key, "st"))    wb.st = (uint8_t)jp.integer();
    else if (!strcmp(key, "title")) jp.str(wb.title, sizeof(wb.title));
    else if (!strcmp(key, "sub"))   jp.str(wb.sub, sizeof(wb.sub));
    else if (!strcmp(key, "agent")) jp.str(wb.agent, sizeof(wb.agent));
    else if (!strcmp(key, "cpu"))   wb.cpu = (int8_t)jp.integer();
    else if (!strcmp(key, "mem"))   wb.mem = (int8_t)jp.integer();
    else if (!strcmp(key, "disk"))  wb.disk = (int8_t)jp.integer();
    else if (!strcmp(key, "up"))    jp.str(wb.up, sizeof(wb.up));
    else if (!strcmp(key, "net"))   jp.str(wb.net, sizeof(wb.net));
    else if (!strcmp(key, "dur"))   jp.str(wb.dur, sizeof(wb.dur));
    else if (!strcmp(key, "model")) jp.str(wb.model, sizeof(wb.model));
    else if (!strcmp(key, "task"))  jp.str(wb.task, sizeof(wb.task));
    else if (!strcmp(key, "bal"))   jp.str(wb.bal, sizeof(wb.bal));
    else if (!strcmp(key, "spend")) jp.str(wb.spend, sizeof(wb.spend));
    else if (!strcmp(key, "cost"))  jp.str(wb.cost, sizeof(wb.cost));
    else if (!strcmp(key, "pg"))    wb.pg = (int16_t)jp.integer();
    else if (!strcmp(key, "lines")) parseLines(jp);
    else jp.skipValue();
    jp.ws();
    if (*jp.p == ',') { jp.p++; continue; }
    if (*jp.p == '}') { jp.p++; break; }
    return false;
  }
  return true;
}

// ================= 时钟本地走时 =================
int32_t clockBaseSec = -1;   // 收到帧时的绝对秒 (当日)
uint32_t clockBaseMs = 0;

int32_t parseClock(const char* s) {
  int h = 0, m = 0;
  if (sscanf(s, "%d:%d", &h, &m) == 2) return h * 3600 + m * 60;
  return -1;
}

void clockTick() {
  if (clockBaseSec < 0) return;
  int32_t sec = clockBaseSec + (int32_t)((millis() - clockBaseMs) / 1000);
  sec %= 86400;
  char buf[8];
  snprintf(buf, sizeof(buf), "%02d:%02d", (int)(sec / 3600), (int)((sec % 3600) / 60));
  if (strcmp(buf, wb.clock) != 0) {
    strlcpy(wb.clock, buf, sizeof(wb.clock));
    if (ui_topClock) lv_label_set_text(ui_topClock, wb.clock);  // 局部刷新
  }
}

// ================= 帧到达 (两个通道共用) =================
uint32_t lastFrameMs = 0;
uint32_t lastAnimMs = 0;
bool showingWaiting = true;

void onFrame(const char* line) {
  if (!parseFrame(line)) return;
  wb.connected = true;
  uint32_t now = millis();
  lastFrameMs = now;
  int32_t cs = parseClock(wb.clock);
  if (cs >= 0) { clockBaseSec = cs; clockBaseMs = now; }
  showingWaiting = false;
  uiShowWaiting(false);
  uiShowPortal(false);        // 帧到达 -> 退出配网界面 (USB 通道已在通信)
  uiUpdateFromState();       // LVGL 增量更新
}

// ================= LVGL 显示驱动 (TFT_eSPI 对接) =================
static lv_display_t* g_disp = nullptr;

static void lvglFlushCb(lv_display_t* disp, const lv_area_t* area, uint8_t* px_map) {
  int32_t w = lv_area_get_width(area);
  int32_t h = lv_area_get_height(area);
  tft.startWrite();
  tft.setAddrWindow(area->x1, area->y1, w, h);
  tft.pushColors((uint16_t*)px_map, (uint32_t)(w * h), true);
  tft.endWrite();
  lv_display_flush_ready(disp);
}

static void lvglSetup() {
  lv_init();
  g_disp = lv_display_create(SCR_W, SCR_H);
  static lv_color_t buf1[SCR_W * 40];        // partial 模式, 1/6 屏
  lv_display_set_flush_cb(g_disp, lvglFlushCb);
  lv_display_set_buffers(g_disp, buf1, nullptr,
                         sizeof(buf1), LV_DISPLAY_RENDER_MODE_PARTIAL);
  uiLvglInit();
}

// ================= 主程序 =================
static SerialTransport usbTransport;
static NetTransport* netTransport = nullptr;
static ConfigPortal portal;
static NetConfig netCfg;
static OtaService ota;
static uint32_t lastLvTickMs = 0;

void setup() {
  Serial.begin(115200);            // C3: 原生 USB-Serial-JTAG; CYD: CH340 (UART0)
  if (WB_BL_PIN >= 0) {
    pinMode(WB_BL_PIN, OUTPUT);
    digitalWrite(WB_BL_PIN, HIGH);
  }
  tft.init();
  tft.setRotation(WB_ROT);         // 由 board.h 决定 (圆屏 0 / CYD 横屏 1)
  tft.fillScreen(TFT_BLACK);
  lvglSetup();

  netCfg.load();
  if (netCfg.valid) {              // 已配网 -> 无线模式
    g_linkMode = "WiFi";
    uiSetLinkMode(g_linkMode);
    netTransport = new NetTransport();
    netTransport->cfg = netCfg;
    netTransport->begin();
    ota.begin(netCfg.name);         // WiFi 通道就绪后自动起 Web OTA
    uiShowWaiting(true);
  } else {                         // 未配网 -> AP 配网 (USB 仍可用)
    portal.start(&netCfg);
    uiShowPortal(true, portal.ssid());
    ota.begin(netCfg.name);         // 仅 ArduinoOTA (80 端口归配网 portal)
  }
  usbTransport.begin();
}

void loop() {
  ota.loop();                      // OTA 无线上传 (ArduinoOTA + Web OTA)
  usbTransport.loop();             // USB 通道始终可用 (向后兼容)
  if (netTransport) {
    netTransport->loop();
  } else {
    portal.loop();                 // 配网模式
  }

  // ---- 断开检测 (任一连通即在线; 配网界面优先保持) ----
  if (!portal.running()) {
    bool linkUp = usbTransport.connected() ||
                  (netTransport && netTransport->connected());
    if (!linkUp && !showingWaiting && millis() - lastFrameMs > LINK_TIMEOUT_MS) {
      wb.connected = false;
      showingWaiting = true;
      uiShowWaiting(true);
    }
  }

  // ---- LVGL 心跳: tick + 渲染 ----
  uint32_t nowMs = millis();
  lv_tick_inc(nowMs - lastLvTickMs);
  lastLvTickMs = nowMs;

  // ---- 进度条动画 (不确定态 120ms 一刷) ----
  if (!showingWaiting && wb.pg == 255 && nowMs - lastAnimMs > 120) {
    uiTickProgress(nowMs);
    lastAnimMs = nowMs;
  }

  // ---- 时钟走时 ----
  static uint32_t lastClockMs = 0;
  if (nowMs - lastClockMs > 500) {
    if (!showingWaiting) clockTick();
    lastClockMs = nowMs;
  }

  lv_timer_handler();
}
