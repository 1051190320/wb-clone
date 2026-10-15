#pragma once
/*
 * wb-clone 显示端状态层 (与渲染无关)
 * 绘制由 ui_lvgl.h (LVGL) 完成; 本文件只保留:
 *   - 屏幕尺寸 / 状态枚举 / 全局 WbState (协议帧解析后写这里)
 *   - 配色 / 状态色 / 日志色 (供 LVGL 样式使用)
 *   - findCN 点阵查找 (lv_font.h 复用)
 */
#include <Arduino.h>
#include "board.h"          // 硬件分区配置 (SCR_W/SCR_H/...), 由 -I boards/<板型> 决定
#include "cn_font.h"

// ---------------- 状态定义 ----------------
enum WbStatus : uint8_t {
  ST_IDLE = 0,   // 空闲
  ST_READY,      // 准备
  ST_THINK,      // 思考
  ST_ANSWER,     // 回答
  ST_EXEC,       // 执行
  ST_WAIT_ANS,   // 等待回答
  ST_WAIT_OK,    // 等待确认
  ST_DONE,       // 已完成
  ST_ERR,        // 异常
  ST_COUNT
};

struct WbLogLine {
  char    t[8];   // 时间 "11:33"
  char    x[8];   // 状态词 "成功"
  uint8_t c;      // 颜色 0g 1b 2y 3r 4n
  char    s[44];  // 内容
};

struct WbState {
  bool     connected = false;
  char     clock[8] = "--:--";
  uint8_t  st = ST_IDLE;
  char     title[24] = "";
  char     sub[48] = "";
  char     agent[12] = "";
  int8_t   cpu = 0;
  int8_t   mem = 0;
  int8_t   disk = 0;         // 存储占用 %
  char     up[20] = "";      // 开机时长
  char     net[24] = "";     // 网速 "↓8 KiB/s ↑6 KiB/s"
  int16_t  pg = -1;          // 进度: -1=无, 0..100, 255=不确定
  // ---- 统计网格 (协议 v1.1 可选字段, 未下发则隐藏对应格子) ----
  char     dur[12] = "";     // 本轮用时 "3:25"
  char     model[20] = "";   // 当前任务模型
  char     task[28] = "";    // 任务名称
  char     bal[20] = "";     // 剩余积分 "8046.86 积分"
  char     spend[20] = "";   // 本轮消耗 "2.92 积分"
  char     cost[20] = "";    // 今日费用 "2.92 积分"
  WbLogLine lines[4];
  uint8_t  nlines = 0;
};

extern WbState wb;

// ---------------- 配色 (24 位 hex, LVGL lv_color_hex 直接用) ----------------
// 色板: 底 #F2F4F1 顶栏 #141A16 墨 #1A1F1C 次要 #6E7671 线 #DEE2DD
//   绿 #3D9A50 蓝 #3E7CB1 青 #2BA8A0 金 #D9A036 红 #C0453E 灰 #969E99
#define C_HEX_BG     0xF2F4F1
#define C_HEX_TOPBAR 0x141A16
#define C_HEX_INK    0x1A1F1C
#define C_HEX_INK2   0x6E7671
#define C_HEX_LINE   0xDEE2DD
#define C_HEX_GREEN  0x3D9A50
#define C_HEX_BLUE   0x3E7CB1
#define C_HEX_TEAL   0x2BA8A0
#define C_HEX_GOLD   0xD9A036
#define C_HEX_RED    0xC0453E
#define C_HEX_GRAY   0x969E99
#define C_HEX_TAGBG  0xE9F0EA   // agent 胶囊底
#define C_HEX_TAGFG  0x2E6E46   // agent 胶囊字

uint32_t statusColorHex(uint8_t st) {
  switch (st) {
    case ST_READY:    return C_HEX_BLUE;
    case ST_THINK:    return C_HEX_TEAL;
    case ST_ANSWER:   return C_HEX_BLUE;
    case ST_EXEC:     return C_HEX_TEAL;
    case ST_WAIT_ANS: return C_HEX_TEAL;
    case ST_WAIT_OK:  return C_HEX_GOLD;
    case ST_DONE:     return C_HEX_GREEN;
    case ST_ERR:      return C_HEX_RED;
    default:          return C_HEX_GRAY;   // idle
  }
}

uint32_t logColorHex(uint8_t c) {
  switch (c) {
    case 0:  return C_HEX_GREEN;
    case 1:  return C_HEX_BLUE;
    case 2:  return C_HEX_GOLD;
    case 3:  return C_HEX_RED;
    default: return C_HEX_GRAY;
  }
}

// 点阵查找 (lv_font.h 复用; 查 cn_font.h 的 CN_FONT 表)
const WbGlyph* findCN(uint16_t code) {
  int lo = 0, hi = CN_FONT_COUNT - 1;
  while (lo <= hi) {
    int mid = (lo + hi) / 2;
    uint16_t c = pgm_read_word(&CN_FONT[mid].code);
    if (c == code) return &CN_FONT[mid];
    if (c < code) lo = mid + 1; else hi = mid - 1;
  }
  return nullptr;
}
