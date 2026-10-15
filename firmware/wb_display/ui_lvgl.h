#pragma once
/*
 * ui_lvgl.h — LVGL v9 UI 渲染层 (圆屏适配)
 *
 * 目标硬件 (默认): ESP32-C3 SuperMini + 1.28" GC9A01 圆屏 240x240
 * 同一份代码也适配 CYD 320x240 (boards/cyd_2_8/board.h): 所有元素以屏幕
 * 中心 (UI_CX/UI_CY) 为基准排布, 圆屏上自然落在可视圆内 (安全半径 ~115px)。
 *
 * 布局 (以 240x240 圆屏为例):
 *   y=16..32   时钟 (居中) + 左侧连接圆点 + 右侧通道名
 *   y=42..58   状态大标题 (1.25x, 状态色, 居中)
 *   y=62..78   副标题
 *   y=68..84   agent · 本轮用时 (居中)
 *   外环       进度环 lv_arc: 270° 表盘自顶部顺时针, 有 pg 时显示
 *   y=90..171  统计网格 2 列 x 3 行 (标签+数值); 无统计时改为 4 条日志行
 *   y=182..198 系统行 CPU/内存
 *   y=200..216 网速行 (压缩单位); 无网速时显示存储占用
 * 等待连接 / AP 配网为独立层, 显隐切换。
 * 注: 圆屏空间有限, 开机时长 up 不上屏 (仍按协议解析, 两种板型代码一致)。
 */
#include <Arduino.h>
#include <lvgl.h>
#include "ui.h"
#include "lv_font.h"

extern const char* g_linkMode;   // "USB" / "WiFi"

// ---------------- 布局常量 (以中心为基准) ----------------
// 布局常量说明: 圆屏可视圆约 r=120 (中心 120,120), 各处元素的 y 决定了该行
// 可用的弦长, 因此底部行的标签宽度收窄、两侧留出余量 (数值经圆幂计算校核)。
#define UI_CX       ((int16_t)(SCR_W / 2))
#define UI_CY       ((int16_t)(SCR_H / 2))
#define UI_SHORT    ((int16_t)((SCR_W < SCR_H) ? SCR_W : SCR_H))
#define UI_RING_R   ((int16_t)(UI_SHORT / 2 - 8))   // 112 @240
#define UI_RING_W   6
#define UI_TOP_Y    18      // 时钟行
#define UI_TITLE_Y  40      // 状态大标题 (1.25x)
#define UI_SUB_Y    60      // 副标题
#define UI_AGENT_Y  78      // agent · 本轮用时
#define UI_MAIN_Y   92      // 网格/日志首行
#define UI_GRID_DY  25      // 行距 (标签 -> 数值 15px)
#define UI_GRID_W   96      // 列宽
#define UI_GRID_GUT 8       // 两列间隙
#define UI_LOG_DY   21
#define UI_SYS_Y    178
#define UI_NET_Y    196

// ---------------- 对象指针 (增量更新用) ----------------
static lv_obj_t* ui_scr        = nullptr;
static lv_obj_t* ui_topClock   = nullptr;   // 时钟
static lv_obj_t* ui_topMode    = nullptr;   // USB / WiFi
static lv_obj_t* ui_topDot     = nullptr;   // 连接状态圆点
static lv_obj_t* ui_title      = nullptr;   // 状态大标题 (状态色)
static lv_obj_t* ui_sub        = nullptr;   // 副标题
static lv_obj_t* ui_agentDur   = nullptr;   // agent · 本轮用时
static lv_obj_t* ui_ring       = nullptr;   // 进度环 (lv_arc)
static lv_obj_t* ui_grid       = nullptr;   // 统计网格容器
static lv_obj_t* ui_gridLab[6] = {nullptr};
static lv_obj_t* ui_gridVal[6] = {nullptr};
static lv_obj_t* ui_logs[4]    = {nullptr};  // 日志行 (圆点 + 文本)
static lv_obj_t* ui_logDot[4]  = {nullptr};
static lv_obj_t* ui_sysLine    = nullptr;   // CPU/内存
static lv_obj_t* ui_netLine    = nullptr;   // 网速 / 存储
// 等待连接层
static lv_obj_t* ui_waitLayer  = nullptr;
static lv_obj_t* ui_waitTitle  = nullptr;
static lv_obj_t* ui_waitSub    = nullptr;
// 配网层
static lv_obj_t* ui_portalLayer = nullptr;
static lv_obj_t* ui_portalTxt[4] = {nullptr};

// 小工具: 建一个居中 label
static lv_obj_t* uiLabelC(lv_obj_t* parent, int16_t cx, int16_t y, int16_t w,
                          uint32_t colorHex, const lv_font_t* font = &wb_font_16) {
  lv_obj_t* o = lv_label_create(parent);
  lv_obj_set_pos(o, (int16_t)(cx - w / 2), y);
  lv_obj_set_width(o, w);
  lv_obj_set_style_text_align(o, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_text_color(o, lv_color_hex(colorHex), 0);
  lv_obj_set_style_text_font(o, font, 0);
  lv_label_set_text(o, "");
  return o;
}

// 小工具: 建一个小圆块 (圆点)
static lv_obj_t* uiDot(lv_obj_t* parent, int16_t x, int16_t y, int16_t d,
                       uint32_t colorHex) {
  lv_obj_t* o = lv_obj_create(parent);
  lv_obj_set_pos(o, x, y);
  lv_obj_set_size(o, d, d);
  lv_obj_set_style_radius(o, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_color(o, lv_color_hex(colorHex), 0);
  lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(o, 0, 0);
  return o;
}

// 小工具: 建一个全屏层 (等待/配网)
static lv_obj_t* uiLayer() {
  lv_obj_t* o = lv_obj_create(ui_scr);
  lv_obj_set_pos(o, 0, 0);
  lv_obj_set_size(o, SCR_W, SCR_H);
  lv_obj_set_style_bg_color(o, lv_color_hex(C_HEX_BG), 0);
  lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(o, 0, 0);
  lv_obj_set_style_radius(o, 0, 0);
  lv_obj_set_style_pad_all(o, 0, 0);
  lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE);
  return o;
}

// ---------------- 初始化 (建一次) ----------------
static void uiLvglInit() {
  ui_scr = lv_scr_act();
  lv_obj_set_style_bg_color(ui_scr, lv_color_hex(C_HEX_BG), 0);
  lv_obj_set_style_pad_all(ui_scr, 0, 0);
  lv_obj_remove_flag(ui_scr, LV_OBJ_FLAG_SCROLLABLE);

  // ---- 顶行: 圆点 · 时钟 · 通道名 ----
  ui_topDot   = uiDot(ui_scr, (int16_t)(UI_CX - 46), (int16_t)(UI_TOP_Y + 6), 6, C_HEX_RED);
  ui_topClock = uiLabelC(ui_scr, UI_CX, UI_TOP_Y, 56, 0xFFFFFF);
  lv_label_set_long_mode(ui_topClock, LV_LABEL_LONG_MODE_CLIP);
  lv_label_set_text(ui_topClock, "--:--");
  ui_topMode = lv_label_create(ui_scr);
  lv_obj_set_pos(ui_topMode, (int16_t)(UI_CX + 30), (int16_t)(UI_TOP_Y + 4));
  lv_obj_set_width(ui_topMode, 26);
  lv_obj_set_style_text_align(ui_topMode, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_text_color(ui_topMode, lv_color_hex(C_HEX_INK2), 0);
  lv_obj_set_style_text_font(ui_topMode, &wb_font_16, 0);
  lv_label_set_text(ui_topMode, g_linkMode ? g_linkMode : "");

  // ---- 状态区 ----
  ui_title = uiLabelC(ui_scr, UI_CX, UI_TITLE_Y, (int16_t)(UI_SHORT - 80), C_HEX_GRAY);
  lv_obj_set_style_transform_scale(ui_title, 320, 0);    // 1.25x 强调
  lv_label_set_long_mode(ui_title, LV_LABEL_LONG_MODE_CLIP);
  ui_sub = uiLabelC(ui_scr, UI_CX, UI_SUB_Y, (int16_t)(UI_SHORT - 48), C_HEX_INK2);
  lv_label_set_long_mode(ui_sub, LV_LABEL_LONG_MODE_CLIP);
  ui_agentDur = uiLabelC(ui_scr, UI_CX, UI_AGENT_Y, (int16_t)(UI_SHORT - 40), C_HEX_INK2);
  lv_label_set_long_mode(ui_agentDur, LV_LABEL_LONG_MODE_CLIP);

  // ---- 进度环 (270° 表盘: 顶部起顺时针) ----
  ui_ring = lv_arc_create(ui_scr);
  lv_obj_set_pos(ui_ring, (int16_t)(UI_CX - UI_RING_R), (int16_t)(UI_CY - UI_RING_R));
  lv_obj_set_size(ui_ring, (int16_t)(UI_RING_R * 2), (int16_t)(UI_RING_R * 2));
  lv_arc_set_rotation(ui_ring, 0);
  lv_arc_set_bg_angles(ui_ring, 270, 180);     // 顶部起, 顺时针 270°
  lv_arc_set_value(ui_ring, 0);
  lv_obj_set_style_arc_width(ui_ring, UI_RING_W, LV_PART_MAIN);
  lv_obj_set_style_arc_width(ui_ring, UI_RING_W, LV_PART_INDICATOR);
  lv_obj_set_style_arc_color(ui_ring, lv_color_hex(C_HEX_LINE), LV_PART_MAIN);
  lv_obj_set_style_arc_color(ui_ring, lv_color_hex(C_HEX_GRAY), LV_PART_INDICATOR);
  lv_obj_add_flag(ui_ring, LV_OBJ_FLAG_HIDDEN);

  // ---- 统计网格 (2 列 x 3 行) ----
  static const char* kGridLab[6] = {"剩余积分", "本轮消耗", "今日费用",
                                    "本轮用时", "当前任务模型", "任务名称"};
  ui_grid = lv_obj_create(ui_scr);
  lv_obj_set_pos(ui_grid, 0, 0);
  lv_obj_set_size(ui_grid, SCR_W, SCR_H);
  lv_obj_remove_flag(ui_grid, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_bg_opa(ui_grid, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(ui_grid, 0, 0);
  lv_obj_set_style_pad_all(ui_grid, 0, 0);
  for (uint8_t i = 0; i < 6; i++) {
    int16_t col = i % 2, row = i / 2;
    int16_t off = (int16_t)(UI_GRID_W / 2 + UI_GRID_GUT / 2);   // 列心偏移, 留出间隙
    int16_t cx = (int16_t)(col ? UI_CX + off : UI_CX - off);
    int16_t y  = (int16_t)(UI_MAIN_Y + row * UI_GRID_DY);
    ui_gridLab[i] = uiLabelC(ui_grid, cx, y, UI_GRID_W, C_HEX_INK2);
    lv_label_set_text(ui_gridLab[i], kGridLab[i]);
    ui_gridVal[i] = uiLabelC(ui_grid, cx, (int16_t)(y + 15), UI_GRID_W, C_HEX_INK);
    lv_label_set_long_mode(ui_gridVal[i], LV_LABEL_LONG_MODE_CLIP);
  }

  // ---- 日志行 (无统计网格时显示) ----
  for (uint8_t i = 0; i < 4; i++) {
    int16_t y = (int16_t)(UI_MAIN_Y + i * UI_LOG_DY);
    ui_logDot[i] = uiDot(ui_scr, (int16_t)(UI_CX - UI_GRID_W - 2),
                         (int16_t)(y + 5), 6, C_HEX_GRAY);
    ui_logs[i] = uiLabelC(ui_scr, UI_CX, y, (int16_t)(UI_SHORT - 48), C_HEX_INK);
    lv_label_set_long_mode(ui_logs[i], LV_LABEL_LONG_MODE_CLIP);
  }

  // ---- 系统行 / 网速行 ----
  ui_sysLine = uiLabelC(ui_scr, UI_CX, UI_SYS_Y, (int16_t)(UI_SHORT - 64), C_HEX_INK2);
  lv_label_set_long_mode(ui_sysLine, LV_LABEL_LONG_MODE_CLIP);
  ui_netLine = uiLabelC(ui_scr, UI_CX, UI_NET_Y, (int16_t)(UI_SHORT - 102), C_HEX_INK2);
  lv_label_set_long_mode(ui_netLine, LV_LABEL_LONG_MODE_CLIP);

  // ---- 等待连接层 ----
  ui_waitLayer = uiLayer();
  ui_waitTitle = uiLabelC(ui_waitLayer, UI_CX, 96, 200, C_HEX_INK);
  lv_label_set_text(ui_waitTitle, "等待连接");
  lv_obj_set_style_transform_scale(ui_waitTitle, 320, 0);
  ui_waitSub = uiLabelC(ui_waitLayer, UI_CX, 126, (int16_t)(UI_SHORT - 40), C_HEX_INK2);
  lv_label_set_text(ui_waitSub, "请启动电脑端 Bridge 程序\n检查 USB 数据线 / WiFi");
  lv_obj_add_flag(ui_waitLayer, LV_OBJ_FLAG_HIDDEN);

  // ---- 配网层 ----
  ui_portalLayer = uiLayer();
  ui_portalTxt[0] = uiLabelC(ui_portalLayer, UI_CX, 56, 200, C_HEX_INK);
  lv_label_set_text(ui_portalTxt[0], "WiFi 配网");
  lv_obj_set_style_transform_scale(ui_portalTxt[0], 320, 0);
  ui_portalTxt[1] = uiLabelC(ui_portalLayer, UI_CX, 86, (int16_t)(UI_SHORT - 40), C_HEX_INK2);
  lv_label_set_text(ui_portalTxt[1], "手机连接热点:");
  ui_portalTxt[2] = uiLabelC(ui_portalLayer, UI_CX, 108, (int16_t)(UI_SHORT - 40), C_HEX_TEAL);
  lv_label_set_text(ui_portalTxt[2], "wb-screen-XXXX");
  ui_portalTxt[3] = uiLabelC(ui_portalLayer, UI_CX, 132, (int16_t)(UI_SHORT - 40), C_HEX_INK2);
  lv_label_set_text(ui_portalTxt[3], "浏览器打开 192.168.4.1\n填入 WiFi 与 MQTT Broker\n保存后设备自动重启");
  lv_obj_add_flag(ui_portalLayer, LV_OBJ_FLAG_HIDDEN);
}

// 通道模式名
static void uiSetLinkMode(const char* mode) {
  if (ui_topMode) lv_label_set_text(ui_topMode, mode ? mode : "");
}

// 等待连接层显隐
static void uiShowWaiting(bool show) {
  if (!ui_waitLayer) return;
  if (show) lv_obj_remove_flag(ui_waitLayer, LV_OBJ_FLAG_HIDDEN);
  else lv_obj_add_flag(ui_waitLayer, LV_OBJ_FLAG_HIDDEN);
}

// 配网层显隐
static void uiShowPortal(bool show, const char* apSsid = nullptr) {
  if (!ui_portalLayer) return;
  if (show) {
    if (apSsid && ui_portalTxt[2]) lv_label_set_text(ui_portalTxt[2], apSsid);
    lv_obj_remove_flag(ui_portalLayer, LV_OBJ_FLAG_HIDDEN);
  } else {
    lv_obj_add_flag(ui_portalLayer, LV_OBJ_FLAG_HIDDEN);
  }
}

// 是否有统计网格数据
static bool uiHasStats() {
  return wb.bal[0] || wb.spend[0] || wb.cost[0] ||
         wb.dur[0] || wb.model[0] || wb.task[0];
}

// 网速压缩: "↓8 KiB/s ↑6 KiB/s" -> "↓8K/s ↑6K/s" (圆屏底部宽度有限)
static void uiNetText(char* out, int maxLen) {
  const char* s = wb.net;
  int n = 0;
  for (int i = 0; s[i] && n + 1 < maxLen; i++) {
    if (s[i] == ' ' && s[i + 1] == 'K' && s[i + 2] == 'i' && s[i + 3] == 'B') {
      out[n++] = 'K'; out[n++] = '/'; out[n++] = 's';
      i += 5;                       // 跳过 " KiB/s" 的剩余部分
      continue;
    }
    out[n++] = s[i];
  }
  out[n] = 0;
}

// ---------------- 增量更新 (每帧调用) ----------------
static void uiUpdateFromState() {
  if (!ui_scr) return;
  char line[64];

  // 顶行
  lv_label_set_text(ui_topClock, wb.clock);
  lv_obj_set_style_bg_color(ui_topDot,
      lv_color_hex(wb.connected ? C_HEX_GREEN : C_HEX_RED), 0);

  // 状态区 (标题颜色 = 状态色)
  uint32_t stHex = statusColorHex(wb.st);
  lv_obj_set_style_text_color(ui_title, lv_color_hex(stHex), 0);
  lv_label_set_text(ui_title, wb.title);
  lv_label_set_text(ui_sub, wb.sub);
  snprintf(line, sizeof(line), "%s%s%s", wb.agent,
           (wb.agent[0] && wb.dur[0]) ? " " : "", wb.dur);
  lv_label_set_text(ui_agentDur, line);

  // 主区: 统计网格 vs 日志行
  bool stats = uiHasStats();
  if (stats) {
    lv_obj_remove_flag(ui_grid, LV_OBJ_FLAG_HIDDEN);
    const char* vals[6] = {wb.bal, wb.spend, wb.cost, wb.dur, wb.model, wb.task};
    for (uint8_t i = 0; i < 6; i++) {
      if (vals[i][0]) {
        lv_obj_remove_flag(ui_gridLab[i], LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(ui_gridVal[i], LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text(ui_gridVal[i], vals[i]);
      } else {
        lv_obj_add_flag(ui_gridLab[i], LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(ui_gridVal[i], LV_OBJ_FLAG_HIDDEN);
      }
    }
    for (uint8_t i = 0; i < 4; i++) {
      lv_obj_add_flag(ui_logs[i], LV_OBJ_FLAG_HIDDEN);
      lv_obj_add_flag(ui_logDot[i], LV_OBJ_FLAG_HIDDEN);
    }
  } else {
    lv_obj_add_flag(ui_grid, LV_OBJ_FLAG_HIDDEN);
    for (uint8_t i = 0; i < 4; i++) {
      if (i < wb.nlines) {
        WbLogLine& L = wb.lines[i];
        lv_obj_remove_flag(ui_logs[i], LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(ui_logDot[i], LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_style_bg_color(ui_logDot[i], lv_color_hex(logColorHex(L.c)), 0);
        snprintf(line, sizeof(line), "%s %s %s", L.t, L.x, L.s);
        lv_label_set_text(ui_logs[i], line);
      } else {
        lv_obj_add_flag(ui_logs[i], LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(ui_logDot[i], LV_OBJ_FLAG_HIDDEN);
      }
    }
  }

  // 进度环
  if (wb.pg >= 0) {
    lv_obj_remove_flag(ui_ring, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_style_arc_color(ui_ring, lv_color_hex(stHex), LV_PART_INDICATOR);
    if (wb.pg != 255) lv_arc_set_value(ui_ring, wb.pg);
  } else {
    lv_obj_add_flag(ui_ring, LV_OBJ_FLAG_HIDDEN);
  }

  // 系统行 / 网速行
  snprintf(line, sizeof(line), "CPU %d%% 内存 %d%%", wb.cpu, wb.mem);
  lv_label_set_text(ui_sysLine, line);
  if (wb.net[0]) {
    uiNetText(line, sizeof(line));
  } else {
    snprintf(line, sizeof(line), "存储 %d%%", wb.disk);
  }
  lv_label_set_text(ui_netLine, line);
}

// 不确定进度动画 (pg==255): 数值往复, 由主循环每 ~120ms 调用
static void uiTickProgress(uint32_t tickMs) {
  if (!ui_ring || wb.pg != 255) return;
  if (lv_obj_has_flag(ui_ring, LV_OBJ_FLAG_HIDDEN)) return;
  int32_t span = (int32_t)((tickMs % 1600) * 100 / 1600);
  int32_t v = span <= 100 ? span : 200 - span;   // 1.6s 周期 0->100->0
  lv_arc_set_value(ui_ring, v);
}
