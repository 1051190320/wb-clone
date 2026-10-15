/*
 * 固件协议解析 + LVGL UI 软件在环测试 (不烧录硬件, 直接在 PC 上验证)
 * 单编译单元: 直接 include .ino, 与 Arduino IDE 的构建方式一致
 * 硬件分区: -Ifirmware/boards/<板型> 决定 SCR_W/SCR_H 等参数 (默认圆屏 240x240)
 * 构建: g++ -std=c++17 -Wall -Wno-unused-variable \
 *         -Itools/mock -Itools/mock/lvgl -Ifirmware/wb_display \
 *         -Ifirmware/boards/esp32c3_gc9a01 -Ifirmware/include \
 *         -x c++ tools/mock/test_main.cpp tools/mock/mock_env.cpp -o /tmp/wbtest
 *       (验证 CYD 320x240 布局: 把板型目录换成 cyd_2_8 再编一次)
 */
#include "Arduino.h"
#include "wb_display.ino"   // 单 TU: setup/loop/wb/parseFrame/ui_* 全部可见
#include "lv_conf.h"        // 真实 LVGL 配置 (mock 不感知 widget 开关, 在此闭环校验)

extern void mockSerialFeed(const char *data);
extern unsigned long g_millis;

static int g_fail = 0;
static int g_total = 0;
#define CHECK(cond, msg) do { \
  g_total++; \
  if (!(cond)) { printf("FAIL: %s\n", msg); g_fail++; } \
  else { printf("ok  : %s\n", msg); } \
} while (0)

// label 是否可见
static bool uiVis(lv_obj_t* o) { return o && !lv_obj_has_flag(o, LV_OBJ_FLAG_HIDDEN); }

int main() {
  setup();   // 未配网 -> portal 启动, UI 已建

  // ---- 配网层 (未配网启动) ----
  CHECK(uiVis(ui_portalLayer), "未配网启动 -> 配网层可见");
  CHECK(strstr(ui_portalTxt[2] ? lv_label_get_text(ui_portalTxt[2]) : "", "wb-screen-") != nullptr,
        "配网层显示 AP 热点名");
  CHECK(g_mock_last_display && g_mock_last_display->hor_res == SCR_W &&
        g_mock_last_display->ver_res == SCR_H, "LVGL 显示尺寸 = board.h 的 SCR_W x SCR_H");
  CHECK(!strcmp(WB_BOARD_NAME, "esp32c3_gc9a01") ? (SCR_W == 240 && SCR_H == 240)
                                                 : (SCR_W == 320 && SCR_H == 240),
        "board.h 尺寸与所选板型一致: " WB_BOARD_NAME);
  CHECK(g_mock_last_display && g_mock_last_display->flush_cb != nullptr,
        "flush_cb 已注册");
  CHECK(g_mock_last_display && g_mock_last_display->buf_size == SCR_W * 40 * 2,
        "渲染缓冲区 240x40 (1/6 屏)");

  // ---- lv_conf.h 一致性 (真实构建时 LV_USE_* 决定 widget 是否编译进来;
  //      mock 不感知这些开关, 若关错真实固件会编不过, 在此闭环) ----
  CHECK(LV_USE_ARC == 1, "lv_conf.h: LV_USE_ARC=1 (进度环 lv_arc)");
  CHECK(LV_USE_LABEL == 1, "lv_conf.h: LV_USE_LABEL=1");
  CHECK(LV_COLOR_DEPTH == 16, "lv_conf.h: RGB565 (flush 推送 uint16 与 TFT_eSPI 一致)");

  // ---- 圆屏布局几何 ----
  CHECK(ui_ring && ui_ring->is_arc, "进度环为 lv_arc");
  CHECK(ui_ring->x == UI_CX - UI_RING_R && ui_ring->y == UI_CY - UI_RING_R,
        "进度环居中 (左上 = 中心 - 半径)");
  CHECK(ui_ring->w == UI_RING_R * 2 && ui_ring->h == UI_RING_R * 2, "进度环直径 = 2R");
  CHECK(ui_ring->arc_bg0 == 270 && ui_ring->arc_bg1 == 180 && ui_ring->arc_rot == 0,
        "进度环: 顶部起顺时针 270°");
  CHECK(ui_ring->arc_color == lv_color_hex(C_HEX_LINE).full, "进度环轨道色 = LINE");

  // ---- 帧1: 完整帧 (无统计网格 -> 显示日志行) ----
  mockSerialFeed(
    "{\"clock\":\"19:27\",\"st\":0,\"title\":\"空闲\",\"sub\":\"等待新的任务\","
    "\"agent\":\"HY3\",\"cpu\":11,\"mem\":88,\"up\":\"1天15小时\",\"pg\":-1,"
    "\"lines\":[{\"t\":\"11:33\",\"x\":\"成功\",\"c\":0,\"s\":\"下次执行 08:50\"},"
    "{\"t\":\"10:02\",\"x\":\"执行\",\"c\":1,\"s\":\"每日全网热点聚...\"}]}\n");
  loop();
  CHECK(!strcmp(wb.clock, "19:27"), "clock=19:27");
  CHECK(wb.st == ST_IDLE, "st=idle(0)");
  CHECK(!strcmp(wb.title, "空闲"), "title=空闲");
  CHECK(!strcmp(wb.sub, "等待新的任务"), "sub=等待新的任务");
  CHECK(!strcmp(wb.agent, "HY3"), "agent=HY3");
  CHECK(wb.cpu == 11 && wb.mem == 88, "cpu=11 mem=88");
  CHECK(!strcmp(wb.up, "1天15小时"), "up=1天15小时 (协议仍解析, 圆屏不上屏)");
  CHECK(wb.pg == -1, "pg=-1(无进度)");
  CHECK(wb.nlines == 2, "nlines=2");
  CHECK(!strcmp(wb.lines[0].t, "11:33") && !strcmp(wb.lines[0].x, "成功") &&
        !strcmp(wb.lines[0].s, "下次执行 08:50") && wb.lines[0].c == 0, "line[0] 正确");
  CHECK(!strcmp(wb.lines[1].s, "每日全网热点聚...") && wb.lines[1].c == 1, "line[1] 正确");
  CHECK(wb.connected == true, "connected=true");

  // ---- LVGL 对象同步 (帧1) ----
  CHECK(!uiVis(ui_portalLayer), "帧到达 -> 配网层隐藏");
  CHECK(!uiVis(ui_waitLayer), "帧到达 -> 等待层隐藏");
  CHECK(ui_topClock && !strcmp(lv_label_get_text(ui_topClock), "19:27"), "UI 时钟=19:27");
  CHECK(ui_title && !strcmp(lv_label_get_text(ui_title), "空闲"), "UI title=空闲");
  CHECK(ui_title && ui_title->text_color == lv_color_hex(C_HEX_GRAY).full,
        "UI title 颜色=灰 (idle 状态色)");
  CHECK(ui_sub && !strcmp(lv_label_get_text(ui_sub), "等待新的任务"), "UI sub");
  CHECK(ui_agentDur && !strcmp(lv_label_get_text(ui_agentDur), "HY3"),
        "UI agent 行=HY3 (无 dur)");
  CHECK(!uiVis(ui_grid), "无统计 -> 网格隐藏");
  CHECK(uiVis(ui_logs[0]) && strstr(lv_label_get_text(ui_logs[0]), "11:33 成功 下次执行 08:50") != nullptr,
        "UI 日志行[0] 文本");
  CHECK(uiVis(ui_logs[1]) && !uiVis(ui_logs[2]), "UI 日志行 2 显 3 隐");
  CHECK(ui_logDot[0] && ui_logDot[0]->bg == lv_color_hex(C_HEX_GREEN).full, "日志圆点=绿");
  CHECK(ui_topDot && ui_topDot->bg == lv_color_hex(C_HEX_GREEN).full, "连接圆点=绿 (已连接)");
  CHECK(!uiVis(ui_ring), "pg=-1 -> 进度环隐藏");
  CHECK(ui_sysLine && !strcmp(lv_label_get_text(ui_sysLine), "CPU 11% 内存 88%"),
        "UI 系统行 CPU/内存");
  CHECK(ui_netLine && !strcmp(lv_label_get_text(ui_netLine), "存储 0%"),
        "UI 网速行 -> 无网速时显示存储占用");

  // ---- 帧2: \uXXXX 转义中文 ----
  mockSerialFeed("{\"st\":2,\"title\":\"\\u601d\\u8003\",\"sub\":\"\\u6b63\\u5728\\u5206\\u6790\",\"pg\":255,\"lines\":[]}\n");
  loop();
  CHECK(wb.st == ST_THINK, "帧2 st=think(2)");
  CHECK(!strcmp(wb.title, "思考"), "帧2 \\uXXXX 解码 title=思考");
  CHECK(!strcmp(wb.sub, "正在分析"), "帧2 \\uXXXX 解码 sub=正在分析");
  CHECK(wb.pg == 255, "帧2 pg=255(不确定进度)");
  CHECK(wb.nlines == 0, "帧2 lines=[] -> nlines=0");
  CHECK(ui_title && !strcmp(lv_label_get_text(ui_title), "思考"), "帧2 UI title=思考");
  CHECK(ui_title && ui_title->text_color == lv_color_hex(C_HEX_TEAL).full,
        "帧2 UI title 颜色=青 (think 状态色)");
  CHECK(uiVis(ui_ring), "帧2 pg=255 -> 进度环可见");

  // ---- 帧3: 未知嵌套字段 + 缺字段 + 超长 title 截断 ----
  mockSerialFeed(
    "{\"foo\":{\"a\":[1,2,{\"b\":\"x, }\"}],\"c\":null},\"st\":8,"
    "\"title\":\"这是一个超级超级超级超级超级超级超级长的标题必须被截断\",\"mem\":55}\n");
  loop();
  CHECK(wb.st == ST_ERR, "帧3 跳过未知嵌套字段 st=err(8)");
  CHECK(strlen(wb.title) < sizeof(wb.title), "帧3 title 被截断不溢出");
  CHECK(wb.mem == 55, "帧3 mem=55");
  CHECK(wb.pg == -1, "帧3 未下发 pg -> 默认 -1");
  CHECK(!uiVis(ui_ring), "帧3 未下发 pg -> 进度环隐藏");
  CHECK(ui_title && ui_title->text_color == lv_color_hex(C_HEX_RED).full,
        "帧3 UI title 颜色=红 (error 状态色)");

  // ---- 帧4: 多行粘包 + 垃圾行 ----
  mockSerialFeed("garbage\n{\"st\":6,\"title\":\"等待确认\"}\nnot-json\n");
  loop();
  CHECK(wb.st == ST_WAIT_OK, "帧4 垃圾行被忽略, st=wait_ok(6)");
  CHECK(ui_title && ui_title->text_color == lv_color_hex(C_HEX_GOLD).full,
        "帧4 UI title 颜色=金 (wait_ok 状态色)");

  // ---- 帧5: lines 超过 4 条只保留前 4 ----
  mockSerialFeed(
    "{\"st\":1,\"lines\":[{\"t\":\"1\",\"x\":\"a\",\"c\":0,\"s\":\"1\"},"
    "{\"t\":\"2\",\"x\":\"b\",\"c\":1,\"s\":\"2\"},{\"t\":\"3\",\"x\":\"c\",\"c\":2,\"s\":\"3\"},"
    "{\"t\":\"4\",\"x\":\"d\",\"c\":3,\"s\":\"4\"},{\"t\":\"5\",\"x\":\"e\",\"c\":0,\"s\":\"5\"}]}\n");
  loop();
  CHECK(wb.nlines == 4, "帧5 lines 截断为 4 条");
  CHECK(!strcmp(wb.lines[3].t, "4"), "帧5 第4条为 t=4");
  CHECK(uiVis(ui_logs[3]) && strstr(lv_label_get_text(ui_logs[3]), "4 d 4") != nullptr,
        "帧5 UI 日志行[3] 文本");

  // ---- 帧6: 缺省字段不污染上一帧残留 ----
  mockSerialFeed("{\"st\":5,\"title\":\"等待回答\"}\n");
  loop();
  CHECK(wb.nlines == 0, "帧6 未下发 lines -> 清空为 0");
  CHECK(wb.cpu == 0 && wb.mem == 0, "帧6 未下发 cpu/mem -> 0");
  CHECK(!uiVis(ui_logs[0]), "帧6 UI 日志行全隐");

  // ---- 帧7: 统计网格 (协议 v1.1) ----
  mockSerialFeed(
    "{\"clock\":\"08:39\",\"st\":4,\"title\":\"正在进行\",\"agent\":\"space-bunny\","
    "\"cpu\":36,\"mem\":45,\"disk\":36,\"net\":\"\u21938 KiB/s \u21916 KiB/s\","
    "\"dur\":\"3:25\",\"model\":\"space-bunny\",\"task\":\"\u8fc1\u79fb QClaw\","
    "\"bal\":\"8046.86 \u79ef\u5206\",\"spend\":\"2.92 \u79ef\u5206\",\"cost\":\"2.92 \u79ef\u5206\","
    "\"pg\":42}\n");
  loop();
  CHECK(wb.disk == 36, "帧7 disk=36 (存储占用)");
  CHECK(!strcmp(wb.net, "\u21938 KiB/s \u21916 KiB/s"), "帧7 net=网速箭头串");
  CHECK(!strcmp(wb.dur, "3:25"), "帧7 dur=3:25 (本轮用时)");
  CHECK(!strcmp(wb.model, "space-bunny"), "帧7 model=space-bunny");
  CHECK(!strcmp(wb.task, "\u8fc1\u79fb QClaw"), "帧7 task=迁移 QClaw");
  CHECK(!strcmp(wb.bal, "8046.86 \u79ef\u5206") && !strcmp(wb.spend, "2.92 \u79ef\u5206")
        && !strcmp(wb.cost, "2.92 \u79ef\u5206"), "帧7 积分三字段");
  CHECK(uiHasStats() == true, "帧7 uiHasStats=true -> 显示统计网格");

  CHECK(uiVis(ui_grid), "帧7 UI 网格可见");
  CHECK(!uiVis(ui_logs[0]), "帧7 有网格 -> 日志行隐藏");
  CHECK(ui_gridLab[0] && !strcmp(lv_label_get_text(ui_gridLab[0]), "剩余积分"), "帧7 网格标签[0]");
  CHECK(ui_gridLab[3] && !strcmp(lv_label_get_text(ui_gridLab[3]), "本轮用时"), "帧7 网格标签[3]");
  CHECK(ui_gridVal[0] && !strcmp(lv_label_get_text(ui_gridVal[0]), "8046.86 \u79ef\u5206"),
        "帧7 UI 网格[0] 剩余积分");
  CHECK(ui_gridVal[1] && !strcmp(lv_label_get_text(ui_gridVal[1]), "2.92 \u79ef\u5206"),
        "帧7 UI 网格[1] 本轮消耗");
  CHECK(ui_gridVal[2] && !strcmp(lv_label_get_text(ui_gridVal[2]), "2.92 \u79ef\u5206"),
        "帧7 UI 网格[2] 今日费用");
  CHECK(ui_gridVal[3] && !strcmp(lv_label_get_text(ui_gridVal[3]), "3:25"),
        "帧7 UI 网格[3] 本轮用时");
  CHECK(ui_gridVal[4] && !strcmp(lv_label_get_text(ui_gridVal[4]), "space-bunny"),
        "帧7 UI 网格[4] 当前任务模型");
  CHECK(ui_gridVal[5] && !strcmp(lv_label_get_text(ui_gridVal[5]), "\u8fc1\u79fb QClaw"),
        "帧7 UI 网格[5] 任务名称");
  // 圆屏 2 列: 偶数索引在左列, 奇数索引在右列
  CHECK(ui_gridVal[0]->x < UI_CX && ui_gridVal[1]->x > UI_CX, "帧7 网格为 2 列布局");
  CHECK(ui_gridVal[0]->y == ui_gridVal[1]->y && ui_gridVal[2]->y > ui_gridVal[0]->y,
        "帧7 网格同行的两列同 y, 逐行下移");
  CHECK(ui_agentDur && !strcmp(lv_label_get_text(ui_agentDur), "space-bunny 3:25"),
        "帧7 UI agent 行=space-bunny 3:25");
  CHECK(uiVis(ui_ring) && lv_arc_get_value(ui_ring) == 42, "帧7 UI 进度环=42");
  CHECK(ui_ring->arc_ind_color == lv_color_hex(C_HEX_TEAL).full,
        "帧7 进度环指示色=青 (exec 状态色)");
  CHECK(ui_sysLine && !strcmp(lv_label_get_text(ui_sysLine), "CPU 36% 内存 45%"),
        "帧7 UI 系统行");
  CHECK(ui_netLine && !strcmp(lv_label_get_text(ui_netLine), "\u21938K/s \u21916K/s"),
        "帧7 UI 网速行单位压缩 (KiB/s -> K/s)");

  // ---- 帧8: 未下发统计字段 -> 快照重置 (网格隐藏) ----
  mockSerialFeed("{\"st\":0,\"title\":\"\u7a7a\u95f2\"}\n");
  loop();
  CHECK(!uiHasStats(), "帧8 未下发统计字段 -> uiHasStats=false (隐藏网格)");
  CHECK(!uiVis(ui_grid), "帧8 UI 网格隐藏");
  CHECK(wb.dur[0] == 0 && wb.model[0] == 0 && wb.bal[0] == 0,
        "帧8 dur/model/bal 被快照重置");
  CHECK(wb.disk == 0 && wb.net[0] == 0, "帧8 disk/net 被快照重置");

  // ---- 单速率网速也能压缩 ----
  wb.net[0] = 0;
  strncpy(wb.net, "\u2193123 KiB/s", sizeof(wb.net) - 1);
  uiUpdateFromState();
  CHECK(!strcmp(lv_label_get_text(ui_netLine), "\u2193123K/s"), "网速压缩: 仅下行也能压缩");
  wb.net[0] = 0;

  // ---- 等待连接层 ----
  uiShowWaiting(true);
  CHECK(uiVis(ui_waitLayer), "等待层可显示");
  CHECK(!strcmp(lv_label_get_text(ui_waitTitle), "等待连接"), "等待层标题");
  uiShowWaiting(false);
  CHECK(!uiVis(ui_waitLayer), "等待层可隐藏");

  // ---- LVGL 字体回调 (点阵直出) ----
  lv_font_glyph_dsc_t d{};
  CHECK(lv_font_get_glyph_dsc(&wb_font_16, &d, 0x4E2D, 0), "字体回调: 中文'闲'命中");
  CHECK(d.box_w == 16 && d.box_h == 16 && d.stride == 2 && d.adv_w == 16,
        "中文字形: 16x16, stride=2, adv=16");
  CHECK(d.format == LV_FONT_GLYPH_FORMAT_A1 && !d.is_placeholder, "中文字形: A1 格式");
  const void* bmp = lv_font_get_glyph_static_bitmap(&d);
  CHECK(bmp == (const void*)findCN(0x4E2D)->bits, "中文字形位图=点阵表直出");
  CHECK(lv_font_get_glyph_dsc(&wb_font_16, &d, 'A', 0), "字体回调: ASCII 'A' 命中");
  CHECK(d.box_w == 8 && d.box_h == 16 && d.stride == 1 && d.adv_w == 8,
        "ASCII 字形: 8x16, stride=1, adv=8");
  CHECK(!lv_font_get_glyph_dsc(&wb_font_16, &d, 0x9FFF, 0), "字体回调: 缺字 -> false");

  // ---- flush 回调 -> TFT_eSPI ----
  uint32_t before = tft.pushed_pixels;
  lv_area_t area{10, 20, 10 + 99, 20 + 39};   // 100x40 区域
  static uint16_t px[100 * 40];
  g_mock_last_display->flush_cb(g_mock_last_display, &area, (uint8_t*)px);
  CHECK(tft.write_count > 0, "flush: startWrite/endWrite 被调用");
  CHECK(tft.ax == 10 && tft.ay == 20 && tft.aw == 100 && tft.ah == 40,
        "flush: setAddrWindow(10,20,100,40)");
  CHECK(tft.pushed_pixels - before == 100 * 40, "flush: pushColors 像素数=区域面积");

  // ---- 不确定进度动画 (pg=255 往复) ----
  mockSerialFeed("{\"st\":2,\"title\":\"\u601d\u8003\",\"pg\":255}\n");
  loop();
  uiTickProgress(400);      // 周期 1600ms: 400ms -> 25
  CHECK(lv_arc_get_value(ui_ring) == 25, "进度动画: t=400ms -> 25");
  uiTickProgress(1200);     // 1200ms -> 75
  CHECK(lv_arc_get_value(ui_ring) == 75, "进度动画: t=1200ms -> 75");
  uiTickProgress(2000);     // 2000ms -> 往复 25
  CHECK(lv_arc_get_value(ui_ring) == 25, "进度动画: t=2000ms -> 25 (往复)");

  // ---- lv_timer_handler 由 loop 驱动 ----
  uint32_t th0 = g_mock_timer_handler_calls;
  loop();
  CHECK(g_mock_timer_handler_calls == th0 + 1, "loop -> lv_timer_handler 每帧调用");

  if (g_fail == 0) printf("\n=== 全部通过 (%d 项) ===\n", g_total);
  else printf("\n=== %d/%d 项失败 ===\n", g_fail, g_total);
  return g_fail == 0 ? 0 : 1;
}
