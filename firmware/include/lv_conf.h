/**
 * @file lv_conf.h
 * wb-clone 精简 LVGL v9 配置 — 默认 ESP32-C3 + GC9A01 圆屏 240x240
 * (备选 CYD 320x240 横屏共用)
 * 只保留本工程用到的子系统: obj / label / arc / style / SW draw / 默认主题
 * 中文字体由 lv_font.h 自定义提供, 不使用内置 montserrat。
 */

#ifndef LV_CONF_H
#define LV_CONF_H

#include <stdint.h>

/*====================
   颜色与深度
 *====================*/
#define LV_COLOR_DEPTH 16
#define LV_COLOR_CHROMA_KEY 0x00FF00

/*====================
   内存 (静态, ESP32 无外部 PSRAM 也可)
 *====================*/
#define LV_MEM_CUSTOM 0
#define LV_MEM_SIZE (32U * 1024U)
/* lvgl v9.3.0 不兼容: lv_conf_internal.h 仅在未定义时才 undef,
   定义为空串会导致 lv_mem_core_builtin.c 的 #include "" 报错;
   不使用自定义内存池头文件时不应定义此宏 */
/* #define LV_MEM_POOL_INCLUDE "" */
#define LV_MEM_ADR 0

/*====================
   HAL (tick 由主循环 lv_tick_inc 喂)
 *====================*/
#define LV_DEF_REFR_PERIOD 30
#define LV_TICK_CUSTOM 0
#define LV_DPI_DEF 130

/*====================
   绘图 (软件渲染)
 *====================*/
#define LV_DRAW_BUF_ALIGN 4
#define LV_DRAW_BUF_STRIDE_ALIGN 1
#define LV_DRAW_SW_SUPPORT_RGB565 1
#define LV_DRAW_SW_SUPPORT_RGB565_SWAPPED 1
#define LV_DRAW_SW_SUPPORT_RGB888 0
#define LV_DRAW_SW_SUPPORT_XRGB8888 0
#define LV_DRAW_SW_SUPPORT_ARGB8888 0
#define LV_DRAW_SW_SUPPORT_L8 0
#define LV_DRAW_SW_SUPPORT_AL88 0
#define LV_DRAW_SW_SUPPORT_A8 1
#define LV_DRAW_SW_SUPPORT_I1 1
#define LV_DRAW_SW_DRAW_UNIT_CNT 1
#define LV_DRAW_SW_COMPLEX 1
#define LV_DRAW_SW_SHADOW_CACHE_SIZE 0
#define LV_USE_DRAW_SW 1

#define LV_DRAW_TRANSFORM_USE_MATRIX 0

/*====================
   日志/断言/性能
 *====================*/
#define LV_USE_LOG 0
#define LV_USE_ASSERT_NULL 1
#define LV_USE_ASSERT_MALLOC 1
#define LV_USE_ASSERT_STYLE 0
#define LV_USE_ASSERT_MEM_INTEGRITY 0
#define LV_USE_ASSERT_OBJ 0
#define LV_ASSERT_HANDLER while (1);
#define LV_USE_PERF_MONITOR 0
#define LV_USE_MEM_MONITOR 0
#define LV_USE_REFR_DEBUG 0

/*====================
   字体 (全部关闭, 用 lv_font.h 自定义点阵字体)
 *====================*/
#define LV_FONT_MONTSERRAT_8 0
#define LV_FONT_MONTSERRAT_10 0
#define LV_FONT_MONTSERRAT_12 0
#define LV_FONT_MONTSERRAT_14 1   /* 默认主题/兜底字体 (英文), 中文一律用 wb_font_16 */
#define LV_FONT_MONTSERRAT_16 0
#define LV_FONT_MONTSERRAT_18 0
#define LV_FONT_MONTSERRAT_20 0
#define LV_FONT_MONTSERRANT_22 0
#define LV_FONT_MONTSERRAT_24 0
#define LV_FONT_MONTSERRAT_26 0
#define LV_FONT_MONTSERRAT_28 0
#define LV_FONT_MONTSERRAT_30 0
#define LV_FONT_MONTSERRAT_32 0
#define LV_FONT_MONTSERRAT_34 0
#define LV_FONT_MONTSERRAT_36 0
#define LV_FONT_MONTSERRAT_38 0
#define LV_FONT_MONTSERRAT_40 0
#define LV_FONT_MONTSERRAT_42 0
#define LV_FONT_MONTSERRAT_44 0
#define LV_FONT_MONTSERRAT_46 0
#define LV_FONT_MONTSERRAT_48 0
#define LV_FONT_DEFAULT &lv_font_montserrat_14
#define LV_FONT_FMT_TXT_LARGE 0
#define LV_USE_FONT_COMPRESSED 0
#define LV_USE_FONT_SUBPX 0
#define LV_FONT_SUBPX_BGR 0

/*====================
   文本
 *====================*/
#define LV_TXT_ENC LV_TXT_ENC_UTF8
#define LV_TXT_BREAK_CHARS " ,.;:-_)]}"
#define LV_TXT_LINE_BREAK_LONG_LEN 0
#define LV_TXT_LINE_BREAK_LONG_PRE_MIN_LEN 3
#define LV_TXT_LINE_BREAK_LONG_POST_MIN_LEN 3
#define LV_TXT_LINE_BREAK_LONG_BUF_SIZE 0
#define LV_USE_BIDI 0
#define LV_USE_ARABIC_PERSIAN_CHARS 0

/*====================
   主题 (默认主题, 供基础样式)
 *====================*/
#define LV_USE_THEME_DEFAULT 1
#define LV_THEME_DEFAULT_DARK 0
#define LV_THEME_DEFAULT_GROW 1
#define LV_THEME_DEFAULT_TRANSITION_TIME 80

/*====================
   对象/布局
 *====================*/
#define LV_USE_OBJ_PROPERTY 0

/*====================
   Widget 开关 (只用 obj/label/arc)
 *====================*/
#define LV_USE_ARC 1
#define LV_USE_ANIMIMG 0
#define LV_USE_BAR 0
#define LV_USE_BUTTON 0
#define LV_USE_BUTTONMATRIX 0
#define LV_USE_CALENDAR 0
#define LV_USE_CANVAS 0
#define LV_USE_CHART 0
#define LV_USE_CHECKBOX 0
#define LV_USE_DROPDOWN 0
#define LV_USE_IMAGE 0
#define LV_USE_IMAGEBUTTON 0
#define LV_USE_KEYBOARD 0
#define LV_USE_LABEL 1
#define LV_USE_LED 0
#define LV_USE_LINE 0
#define LV_USE_LIST 0
#define LV_USE_MENU 0
#define LV_USE_MSGBOX 0
#define LV_USE_ROLLER 0
#define LV_USE_SCALE 0
#define LV_USE_SLIDER 0
#define LV_USE_SPAN 0
#define LV_USE_SPINBOX 0
#define LV_USE_SPINNER 0
#define LV_USE_SWITCH 0
#define LV_USE_TABLE 0
#define LV_USE_TABVIEW 0
#define LV_USE_TEXTAREA 0
#define LV_USE_TILEVIEW 0
#define LV_USE_WIN 0

/*====================
   其他
 *====================*/
#define LV_USE_FLEX 1
#define LV_USE_GRID 0
#define LV_USE_IME_PINYIN 0
#define LV_USE_OBSERVER 0
#define LV_USE_FILE_EXPLORER 0
#define LV_USE_FRAGMENT 0
#define LV_USE_IMGFONT 0
#define LV_USE_QRCODE 0
#define LV_USE_FREETYPE 0
#define LV_USE_TINY_TTF 0
#define LV_USE_LODEPNG 0
#define LV_USE_TJPGD 0
#define LV_USE_GIF 0
#define LV_USE_BIN_DECODER 0
#define LV_USE_RLOTTIE 0
#define LV_USE_VECTOR_GRAPHIC 0
#define LV_USE_SNAPSHOT 0
#define LV_USE_MONKEY 0
#define LV_USE_SYSMON 0
#define LV_USE_PERF_PROFILER 0
#define LV_USE_TEST 0
#define LV_USE_DEMO 0
#define LV_BUILD_EXAMPLES 0
#define LV_CACHE_DEF_SIZE 0

#endif /* LV_CONF_H */
