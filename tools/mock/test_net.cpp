/*
 * 无线模块软件在环测试: MiniMQTT 协议 / NetTransport / 配网 Portal
 * 单编译单元 (include .ino) + mock_env.cpp
 * 构建: g++ -std=c++17 -Itools/mock -Ifirmware/wb_display \
 *        -x c++ tools/mock/test_net.cpp tools/mock/mock_env.cpp -o /tmp/nettest
 */
#include "Arduino.h"
#include "wb_display.ino"

extern void mockSerialFeed(const char *data);
extern unsigned long g_millis;
extern int g_restart_count;
extern int g_connect_count;
extern int g_wifi_status;
extern std::map<std::string, std::string> g_nvs;

static int g_fail = 0;
#define CHECK(cond, msg) do { \
  if (!(cond)) { printf("FAIL: %s\n", msg); g_fail++; } \
  else { printf("ok  : %s\n", msg); } \
} while (0)

/* 构造一个 MQTT PUBLISH 包 (QoS0) */
static int makePublish(uint8_t *out, const char *topic, const char *payload) {
  int n = 0;
  int rem = 2 + (int)strlen(topic) + (int)strlen(payload);
  out[n++] = 0x30;                                   // PUBLISH, QoS0
  // varint
  int len = rem;
  do {
    uint8_t b = len % 128;
    len /= 128;
    if (len > 0) b |= 0x80;
    out[n++] = b;
  } while (len > 0);
  out[n++] = (uint8_t)(strlen(topic) >> 8);
  out[n++] = (uint8_t)(strlen(topic) & 0xFF);
  memcpy(&out[n], topic, strlen(topic));
  n += strlen(topic);
  memcpy(&out[n], payload, strlen(payload));
  n += strlen(payload);
  return n;
}

static const char *LONG_FRAME =
  "{\"clock\":\"19:27\",\"st\":4,\"title\":\"执行命令\","
  "\"sub\":\"pip install reportlab 以及其他一长串描述文字用来把包长撑过 127 字节\","
  "\"agent\":\"HY3\",\"cpu\":34,\"mem\":61,\"up\":\"1天15小时\",\"pg\":255,"
  "\"lines\":[{\"t\":\"11:33\",\"x\":\"成功\",\"c\":0,\"s\":\"下次执行 08:50\"},"
  "{\"t\":\"10:02\",\"x\":\"执行\",\"c\":1,\"s\":\"每日全网热点聚...\"},"
  "{\"t\":\"09:15\",\"x\":\"回答\",\"c\":1,\"s\":\"生成报告第一段内容\"},"
  "{\"t\":\"08:40\",\"x\":\"思考\",\"c\":1,\"s\":\"分析需求\"}]}";

int main() {
  setup();

  // ================= 1. CONNECT 编码 (字节级) =================
  printf("--- MiniMQTT 协议编码 ---\n");
  MiniMQTT m;
  m.begin("192.168.1.100", 1883, "wb-screen", "wb/frame");
  g_millis = 1000;
  m.ensureConnected(1000);
  CHECK(g_connect_count == 1, "ensureConnected -> TCP connect");
  CHECK(m.tcp.txLen == 23, "CONNECT 包长 23");
  static const uint8_t EXPECT[] = {
    0x10, 0x15,
    0x00, 0x04, 'M', 'Q', 'T', 'T',
    0x04, 0x02, 0x00, 0x0F,
    0x00, 0x09, 'w', 'b', '-', 's', 'c', 'r', 'e', 'e', 'n'
  };
  CHECK(memcmp(m.tcp.txBuf, EXPECT, sizeof(EXPECT)) == 0, "CONNECT 字节全部正确");
  CHECK(m.tcp.txBuf[11] == 0x0F, "keepalive = 15s");

  // ================= 2. CONNACK -> SUBSCRIBE =================
  m.tcp.clearTx();
  uint8_t connack[] = {0x20, 0x02, 0x00, 0x00};
  m.tcp.serverPush(connack, sizeof(connack));
  m.loop(1000);
  CHECK(m.linked == true, "CONNACK -> linked");
  static const uint8_t SUBS[] = {
    0x82, 0x0D,
    0x00, 0x01,
    0x00, 0x08, 'w', 'b', '/', 'f', 'r', 'a', 'm', 'e',
    0x00
  };
  CHECK(m.tcp.txLen == 15 && memcmp(m.tcp.txBuf, SUBS, sizeof(SUBS)) == 0,
        "SUBSCRIBE 字节全部正确 (topic=wb/frame, QoS0)");

  // ================= 3. PUBLISH 单帧 (多字节 varint) =================
  m.onMessage = NetTransport::onMqttMessage;
  uint8_t pkt[700];
  int plen = makePublish(pkt, "wb/frame", LONG_FRAME);
  CHECK(plen > 200, "PUBLISH 包较长 (触发多字节 varint)");
  m.tcp.serverPush(pkt, plen);
  m.loop(1000);
  CHECK(wb.st == ST_EXEC, "MQTT 帧 -> st=执行(4)");
  CHECK(!strcmp(wb.title, "执行命令"), "title 正确");
  CHECK(wb.cpu == 34 && wb.mem == 61, "cpu/mem 正确");
  CHECK(wb.pg == 255, "pg=255");
  CHECK(wb.nlines == 4, "4 条日志行");
  CHECK(!strcmp(wb.lines[0].t, "11:33") && !strcmp(wb.lines[0].x, "成功"),
        "line[0] 正确");
  CHECK(!strcmp(wb.lines[3].s, "分析需求"), "line[3] 正确");
  CHECK(wb.connected == true, "connected=true");

  // ================= 4. 一包多帧 (\n 分隔) =================
  char two[800];
  snprintf(two, sizeof(two),
    "{\"st\":5,\"title\":\"等待回答\"}\n{\"st\":7,\"title\":\"已完成\"}");
  plen = makePublish(pkt, "wb/frame", two);
  m.tcp.serverPush(pkt, plen);
  m.loop(1000);
  CHECK(wb.st == ST_DONE && !strcmp(wb.title, "已完成"), "一包多帧 -> 最后一帧生效");

  // ================= 5. keepalive PINGREQ =================
  m.tcp.clearTx();
  g_millis = 1000 + 12001;          // > 0.8 * 15s
  m.loop(g_millis);
  CHECK(m.tcp.txLen == 2 && m.tcp.txBuf[0] == 0xC0 && m.tcp.txBuf[1] == 0x00,
        "keepalive PINGREQ");

  // ================= 6. 断线退避重连 =================
  int cnt0 = g_connect_count;
  m.tcp.stop();
  uint32_t base = m.lastTryMs;      // 上次连接尝试的时刻
  g_millis = base + 500;            // < backoff(1000)
  m.ensureConnected(g_millis);
  CHECK(g_connect_count == cnt0, "退避窗口内不重连");
  g_millis = base + 1100;           // > backoff
  m.ensureConnected(g_millis);
  CHECK(g_connect_count == cnt0 + 1, "退避到期后重连");

  // ================= 7. NetTransport 集成 =================
  printf("--- NetTransport / NetConfig ---\n");
  g_nvs.clear();
  NetConfig c;
  strlcpy(c.ssid, "MyWiFi", sizeof(c.ssid));
  strlcpy(c.pass, "pass1234", sizeof(c.pass));
  strlcpy(c.broker, "192.168.1.100", sizeof(c.broker));
  strlcpy(c.topic, "wb/frame", sizeof(c.topic));
  strlcpy(c.name, "wb-screen", sizeof(c.name));
  c.port = 1883;
  c.save();
  NetConfig d;
  d.load();
  CHECK(d.valid && !strcmp(d.ssid, "MyWiFi") && !strcmp(d.broker, "192.168.1.100")
        && d.port == 1883 && !strcmp(d.topic, "wb/frame") && !strcmp(d.name, "wb-screen"),
        "NetConfig save/load 往返");

  NetConfig e;                       // 空配置
  g_nvs.clear();
  e.load();
  CHECK(!e.valid, "无配置 -> valid=false");

  // ================= 8. ConfigPortal =================
  printf("--- AP 配网 Portal ---\n");
  g_nvs.clear();
  g_restart_count = 0;
  NetConfig pc;
  portal.start(&pc);
  CHECK(!strcmp(portal.ssid(), "wb-screen-1234"), "AP SSID = wb-screen-1234 (MAC 尾字节)");
  CHECK(portal.running(), "portal 运行中");

  portal.server().simulateRequest("/", {});
  portal.loop();
  CHECK(portal.server().lastBody.find("配网") != std::string::npos, "GET / 返回配网页面");

  // 缺字段 -> 拒绝
  portal.server().simulateRequest("/save", {{"broker", "1.2.3.4"}});
  portal.loop();
  CHECK(g_restart_count == 0, "缺 SSID -> 拒绝保存");
  CHECK(portal.server().lastBody.find("不能为空") != std::string::npos, "返回错误提示");

  // 完整参数 -> 保存 + 重启
  portal.server().simulateRequest("/save", {
    {"ssid", "HomeWiFi"}, {"pass", "abc"}, {"broker", "192.168.1.50"},
    {"port", "1883"}, {"topic", "wb/frame"}, {"name", "desk-screen"}});
  portal.loop();
  CHECK(g_restart_count == 1, "保存成功 -> ESP.restart");
  CHECK(g_nvs["ssid"] == "HomeWiFi", "NVS 写入 ssid");
  CHECK(g_nvs["broker"] == "192.168.1.50", "NVS 写入 broker");
  NetConfig lc;
  lc.load();
  CHECK(lc.valid && !strcmp(lc.name, "desk-screen") && lc.port == 1883,
        "重启后可加载新配置");

  // ================= 9. SerialTransport 回归 =================
  printf("--- SerialTransport 回归 ---\n");
  mockSerialFeed("{\"st\":2,\"title\":\"思考\",\"sub\":\"分析中\"}\n");
  usbTransport.loop();
  CHECK(wb.st == ST_THINK && !strcmp(wb.title, "思考"), "USB 串口帧 -> 解析正常");
  CHECK(usbTransport.connected(), "USB 通道在线");
  g_millis += 6000;
  CHECK(!usbTransport.connected(), "USB 6s 无帧 -> 离线");

  printf(g_fail == 0 ? "\n=== 全部通过 ===\n" : "\n=== %d 项失败 ===\n", g_fail);
  return g_fail == 0 ? 0 : 1;
}
