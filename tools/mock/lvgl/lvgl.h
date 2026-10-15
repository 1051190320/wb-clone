#pragma once
/*
 * lvgl.h mock — wb-clone 固件 g++ 在环测试用 LVGL 桩
 *
 * 实现 ui_lvgl.h / lv_font.h / wb_display.ino 用到的 LVGL v9 API 子集,
 * 所有对象/文本/颜色/进度值都被记录, 供测试直接断言。
 * 真机编译时本文件被 PlatformIO 的 lvgl 库同名头文件替换 (经 -I 顺序保证)。
 *
 * 结构定义与 lvgl v9.3.0 src/font/lv_font.h 保持一致 (get_glyph_dsc 自定义字体方案)。
 */
#include <cstdint>
#include <cstring>
#include <cstdlib>

// ---------------- 基础类型 ----------------
typedef struct { uint16_t full; } lv_color_t;

typedef enum {
    LV_FONT_GLYPH_FORMAT_NONE = 0,
    LV_FONT_GLYPH_FORMAT_A1   = 0x01,
    LV_FONT_GLYPH_FORMAT_A2   = 0x02,
    LV_FONT_GLYPH_FORMAT_A4   = 0x04,
    LV_FONT_GLYPH_FORMAT_A8   = 0x08,
    LV_FONT_GLYPH_FORMAT_IMAGE = 0x19,
    LV_FONT_GLYPH_FORMAT_VECTOR = 0x1A,
    LV_FONT_GLYPH_FORMAT_SVG    = 0x1B,
    LV_FONT_GLYPH_FORMAT_CUSTOM = 0xFF,
} lv_font_glyph_format_t;

// 与 lvgl v9.3 lv_font_glyph_dsc_t 字段一致 (测试字体回调会写这些字段)
typedef struct {
    const struct _lv_font_t * resolved_font;
    uint16_t adv_w;
    uint16_t box_w;
    uint16_t box_h;
    int16_t  ofs_x;
    int16_t  ofs_y;
    uint16_t stride;
    lv_font_glyph_format_t format;
    uint8_t is_placeholder : 1;
    uint8_t req_raw_bitmap : 1;
    int32_t outline_stroke_width;
    union { uint32_t index; const void * src; } gid;
    void * entry;
} lv_font_glyph_dsc_t;

typedef struct _lv_draw_buf_t { int dummy; } lv_draw_buf_t;

typedef enum { LV_FONT_SUBPX_NONE, LV_FONT_SUBPX_HOR, LV_FONT_SUBPX_VER, LV_FONT_SUBPX_BOTH } lv_font_subpx_t;
typedef enum { LV_FONT_KERNING_NORMAL, LV_FONT_KERNING_NONE } lv_font_kerning_t;

struct _lv_font_t {
    bool (*get_glyph_dsc)(const struct _lv_font_t *, lv_font_glyph_dsc_t *, uint32_t letter, uint32_t letter_next);
    const void * (*get_glyph_bitmap)(lv_font_glyph_dsc_t *, lv_draw_buf_t *);
    void (*release_glyph)(const struct _lv_font_t *, lv_font_glyph_dsc_t *);
    int32_t line_height;
    int32_t base_line;
    uint8_t subpx : 2;
    uint8_t kerning : 1;
    uint8_t static_bitmap : 1;
    int8_t underline_position;
    int8_t underline_thickness;
    const void * dsc;
    const struct _lv_font_t * fallback;
    void * user_data;
};
typedef struct _lv_font_t lv_font_t;

// ---------------- 对象模型 ----------------
struct lv_obj_t;
struct lv_display_t;
struct lv_area_t;

typedef enum {
    LV_OBJ_FLAG_HIDDEN       = 1 << 0,
    LV_OBJ_FLAG_SCROLLABLE   = 1 << 1,
} lv_obj_flag_t;

typedef enum {
    LV_OPA_TRANSP = 0,
    LV_OPA_COVER  = 255,
} lv_opa_t;

typedef enum {
    LV_ALIGN_DEFAULT = 0,
    LV_ALIGN_TOP_LEFT,
    LV_ALIGN_CENTER,
} lv_align_t;

typedef enum {
    LV_TEXT_ALIGN_LEFT = 0,
    LV_TEXT_ALIGN_CENTER,
    LV_TEXT_ALIGN_RIGHT,
} lv_text_align_t;

// 部件 selector (真 LVGL v9: MAIN=0, SCROLLBAR=1, INDICATOR=2, KNOB=4)
#define LV_PART_MAIN       0
#define LV_PART_INDICATOR  2

typedef enum {
    LV_LABEL_LONG_MODE_WRAP = 0,
    LV_LABEL_LONG_MODE_CLIP,
    LV_LABEL_LONG_MODE_SCROLL_CIRCULAR,
} lv_label_long_mode_t;

typedef enum {
    LV_ANIM_OFF = 0,
    LV_ANIM_ON,
} lv_anim_enable_t;

typedef enum {
    LV_DISPLAY_RENDER_MODE_PARTIAL = 0,
    LV_DISPLAY_RENDER_MODE_FULL,
} lv_display_render_mode_t;

#define LV_RADIUS_CIRCLE 0x7FFF

struct lv_obj_t {
    lv_obj_t* parent = nullptr;
    int16_t x = 0, y = 0;
    int16_t w = 0, h = 0;
    uint32_t flags = 0;
    uint16_t bg = 0;
    uint8_t  bg_opa = 255;
    uint16_t text_color = 0;
    const lv_font_t* font = nullptr;
    int16_t radius = 0;
    int16_t transform = 256;      // 256 = 1.0x
    uint8_t text_align = LV_TEXT_ALIGN_LEFT;
    int32_t bar_value = 0;
    bool is_bar = false;
    bool is_label = false;
    // ---- arc (进度环) ----
    bool     is_arc = false;
    int32_t  arc_value = 0;
    uint16_t arc_rot = 0;
    uint16_t arc_bg0 = 0;
    uint16_t arc_bg1 = 0;
    uint16_t arc_color = 0;         // LV_PART_MAIN 轨道色
    uint16_t arc_ind_color = 0;     // LV_PART_INDICATOR 进度色
    int16_t  arc_width_main = 0;
    int16_t  arc_width_ind = 0;
    char text[192] = {0};
};

struct lv_area_t { int16_t x1, y1, x2, y2; };

struct lv_display_t {
    int32_t hor_res = 0, ver_res = 0;
    void (*flush_cb)(lv_display_t*, const lv_area_t*, uint8_t*) = nullptr;
    uint8_t* buf1 = nullptr;
    uint32_t buf_size = 0;
    int render_mode = LV_DISPLAY_RENDER_MODE_PARTIAL;
};

// ---------------- 全局测试状态 ----------------
static lv_display_t* g_mock_last_display = nullptr;
static uint32_t g_mock_tick_ms = 0;
static uint32_t g_mock_timer_handler_calls = 0;

// ---------------- 内联实现 ----------------
static inline lv_color_t lv_color_hex(uint32_t c) {
    lv_color_t r;
    r.full = (uint16_t)(((c >> 8) & 0xF800) | ((c >> 5) & 0x07E0) | ((c >> 3) & 0x001F));
    return r;
}

static inline void lv_init() {}
static inline uint32_t lv_tick_get() { return g_mock_tick_ms; }
static inline void lv_tick_inc(uint32_t ms) { g_mock_tick_ms += ms; }

static inline lv_obj_t* lv_scr_act() {
    static lv_obj_t scr;
    return &scr;
}

static inline lv_obj_t* lv_obj_create(lv_obj_t* parent) {
    lv_obj_t* o = new lv_obj_t();
    o->parent = parent;
    return o;
}
static inline lv_obj_t* lv_label_create(lv_obj_t* parent) {
    lv_obj_t* o = lv_obj_create(parent);
    o->is_label = true;
    return o;
}
static inline lv_obj_t* lv_bar_create(lv_obj_t* parent) {
    lv_obj_t* o = lv_obj_create(parent);
    o->is_bar = true;
    return o;
}
static inline lv_obj_t* lv_arc_create(lv_obj_t* parent) {
    lv_obj_t* o = lv_obj_create(parent);
    o->is_arc = true;
    return o;
}

static inline void lv_label_set_text(lv_obj_t* o, const char* t) {
    if (!o) return;
    if (t) { strncpy(o->text, t, sizeof(o->text) - 1); o->text[sizeof(o->text) - 1] = 0; }
    else o->text[0] = 0;
}
static inline const char* lv_label_get_text(lv_obj_t* o) { return o ? o->text : ""; }
static inline void lv_label_set_long_mode(lv_obj_t* o, lv_label_long_mode_t) { (void)o; }

static inline void lv_obj_set_pos(lv_obj_t* o, int16_t x, int16_t y) { if (o) { o->x = x; o->y = y; } }
static inline void lv_obj_set_size(lv_obj_t* o, int16_t w, int16_t h) { if (o) { o->w = w; o->h = h; } }
static inline void lv_obj_set_width(lv_obj_t* o, int16_t w) { if (o) o->w = w; }
static inline void lv_obj_set_align(lv_obj_t*, lv_align_t) {}

static inline void lv_obj_set_style_bg_color(lv_obj_t* o, lv_color_t c, int) { if (o) o->bg = c.full; }
static inline void lv_obj_set_style_bg_opa(lv_obj_t* o, uint8_t opa, int) { if (o) o->bg_opa = opa; }
static inline void lv_obj_set_style_text_color(lv_obj_t* o, lv_color_t c, int) { if (o) o->text_color = c.full; }
static inline void lv_obj_set_style_text_font(lv_obj_t* o, const lv_font_t* f, int) { if (o) o->font = f; }
static inline void lv_obj_set_style_text_align(lv_obj_t* o, uint8_t a, int) { if (o) o->text_align = a; }
static inline void lv_obj_set_style_radius(lv_obj_t* o, int16_t r, int) { if (o) o->radius = r; }
static inline void lv_obj_set_style_border_width(lv_obj_t* o, int16_t, int) { (void)o; }
static inline void lv_obj_set_style_border_color(lv_obj_t* o, lv_color_t, int) { (void)o; }
static inline void lv_obj_set_style_pad_all(lv_obj_t* o, int16_t, int) { (void)o; }
static inline void lv_obj_set_style_transform_scale(lv_obj_t* o, int16_t s, int) { if (o) o->transform = s; }
static inline void lv_obj_remove_style_all(lv_obj_t*) {}

static inline void lv_obj_add_flag(lv_obj_t* o, uint32_t f) { if (o) o->flags |= f; }
static inline void lv_obj_remove_flag(lv_obj_t* o, uint32_t f) { if (o) o->flags &= ~f; }
static inline bool lv_obj_has_flag(lv_obj_t* o, uint32_t f) { return o && (o->flags & f); }

static inline void lv_bar_set_value(lv_obj_t* o, int32_t v, int) { if (o) o->bar_value = v; }
static inline int32_t lv_bar_get_value(lv_obj_t* o) { return o ? o->bar_value : 0; }

// ---- arc (进度环) ----
static inline void lv_arc_set_value(lv_obj_t* o, int32_t v) { if (o) o->arc_value = v; }
static inline int32_t lv_arc_get_value(const lv_obj_t* o) { return o ? o->arc_value : 0; }
static inline void lv_arc_set_bg_angles(lv_obj_t* o, uint16_t s, uint16_t e) {
    if (o) { o->arc_bg0 = s; o->arc_bg1 = e; }
}
static inline void lv_arc_set_rotation(lv_obj_t* o, uint16_t deg) { if (o) o->arc_rot = deg; }
static inline void lv_obj_set_style_arc_color(lv_obj_t* o, lv_color_t c, int part) {
    if (!o) return;
    if (part == LV_PART_INDICATOR) o->arc_ind_color = c.full;
    else o->arc_color = c.full;
}
static inline void lv_obj_set_style_arc_width(lv_obj_t* o, int16_t w, int part) {
    if (!o) return;
    if (part == LV_PART_INDICATOR) o->arc_width_ind = w;
    else o->arc_width_main = w;
}

static inline void lv_display_set_flush_cb(lv_display_t* d, void (*cb)(lv_display_t*, const lv_area_t*, uint8_t*)) {
    if (d) d->flush_cb = cb;
}
static inline void lv_display_set_buffers(lv_display_t* d, void* b1, void*, uint32_t sz, int mode) {
    if (d) { d->buf1 = (uint8_t*)b1; d->buf_size = sz; d->render_mode = mode; }
}
static inline lv_display_t* lv_display_create(int32_t h, int32_t v) {
    lv_display_t* d = new lv_display_t();
    d->hor_res = h; d->ver_res = v;
    g_mock_last_display = d;
    return d;
}
static inline void lv_display_flush_ready(lv_display_t*) {}

static inline int16_t lv_area_get_width(const lv_area_t* a) { return a->x2 - a->x1 + 1; }
static inline int16_t lv_area_get_height(const lv_area_t* a) { return a->y2 - a->y1 + 1; }

// mock 不渲染; 记录调用次数即可
static inline void lv_timer_handler() { g_mock_timer_handler_calls++; }

// 字体查询辅助 (测试用: 直接调固件字体的回调)
static inline bool lv_font_get_glyph_dsc(const lv_font_t* font, lv_font_glyph_dsc_t* d, uint32_t letter, uint32_t next) {
    return font->get_glyph_dsc(font, d, letter, next);
}
static inline const void* lv_font_get_glyph_static_bitmap(lv_font_glyph_dsc_t* d) {
    return d->resolved_font->get_glyph_bitmap(d, nullptr);
}
