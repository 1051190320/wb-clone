#pragma once
/*
 * lv_font.h — 点阵字库适配 LVGL v9 字体
 *
 * 直接复用 cn_font.h 的 16x16 中文点阵 (每字 32 字节, 每行 2 字节 MSB first)
 * 与 8x16 ASCII 点阵 (每字 16 字节, 每行 1 字节)。
 * LVGL A1 位图格式 = 每行 (box_w+7)/8 字节, MSB first — 与点阵字节布局天然一致,
 * 零转换, PROGMEM 里的字节直接当位图返回。
 *
 * 通过 lv_font_t 的 get_glyph_dsc / get_glyph_bitmap 自定义回调接入,
 * 不依赖 LVGL 的字体打包格式 (lv_font_fmt_txt), 也不需要 ttf 转换工具链。
 */
#include <Arduino.h>
#include <lvgl.h>
#include "ui.h"          // findCN + WbGlyph (cn_font.h)

// ---- 字形查找: 返回位图指针并填写描述符 ----
static bool wbFontGetDsc(const lv_font_t* font, lv_font_glyph_dsc_t* d,
                         uint32_t letter, uint32_t letter_next) {
  (void)letter_next;
  d->resolved_font = font;
  d->gid.index = letter;
  d->entry = nullptr;
  d->outline_stroke_width = 0;
  if (letter < 0x80) {                       // ASCII 8x16
    if (letter < ASC_FONT_FIRST || letter > ASC_FONT_FIRST + 0x5E) {
      d->is_placeholder = true;              // 控制字符等 -> 占位
      d->req_raw_bitmap = true;
      d->adv_w = 0; d->box_w = 0; d->box_h = 0;
      d->ofs_x = 0; d->ofs_y = 0; d->stride = 0;
      d->format = LV_FONT_GLYPH_FORMAT_A1;
      return false;
    }
    d->adv_w = 8;  d->box_w = 8;  d->box_h = 16;
    d->ofs_x = 0;  d->ofs_y = -13;           // 16px 字基线约 13
    d->stride = 1;                           // A1: 每行 1 字节
  } else {                                   // 中文 16x16
    const WbGlyph* g = findCN((uint16_t)letter);
    if (!g) {
      d->is_placeholder = true;
      d->req_raw_bitmap = true;
      d->adv_w = 0; d->box_w = 0; d->box_h = 0;
      d->ofs_x = 0; d->ofs_y = 0; d->stride = 0;
      d->format = LV_FONT_GLYPH_FORMAT_A1;
      return false;
    }
    d->adv_w = 16; d->box_w = 16; d->box_h = 16;
    d->ofs_x = 0;  d->ofs_y = -13;
    d->stride = 2;                           // A1: 每行 2 字节
  }
  d->is_placeholder = false;
  d->req_raw_bitmap = true;                  // 直接返回 PROGMEM 原始位图
  d->format = LV_FONT_GLYPH_FORMAT_A1;
  return true;
}

static const void* wbFontGetBitmap(lv_font_glyph_dsc_t* d, lv_draw_buf_t* buf) {
  (void)buf;
  uint32_t letter = d->gid.index;
  if (letter < 0x80) {
    if (letter < ASC_FONT_FIRST || letter > ASC_FONT_FIRST + 0x5E) return nullptr;
    return ASC_FONT[letter - ASC_FONT_FIRST];
  }
  const WbGlyph* g = findCN((uint16_t)letter);
  return g ? g->bits : nullptr;
}

// ---- 导出字体 (LVGL v9 lv_font_t) ----
const lv_font_t wb_font_16 = {
  .get_glyph_dsc     = wbFontGetDsc,
  .get_glyph_bitmap  = wbFontGetBitmap,
  .release_glyph     = nullptr,
  .line_height       = 16,
  .base_line         = 13,
  .subpx             = LV_FONT_SUBPX_NONE,
  .kerning           = LV_FONT_KERNING_NONE,
  .static_bitmap     = true,
  .underline_position = -2,
  .underline_thickness = 1,
  .dsc               = nullptr,
  .fallback          = nullptr,
  .user_data         = nullptr,
};
