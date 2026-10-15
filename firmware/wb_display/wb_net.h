#pragma once
/*
 * wb-clone 无线通道: WiFi + MQTT + AP 配网 portal
 *
 * 组成:
 *   NetConfig    NVS 配置 (ssid/pass/broker/port/topic/name)
 *   MiniMQTT     手写 MQTT 3.1.1 QoS0 客户端 (零第三方依赖)
 *   ConfigPortal 首次使用 AP 配网 (手机连 wb-screen-XXXX 热点填参数)
 *   NetTransport Transport 实现, 订阅 broker 上的帧 topic
 *
 * 仅依赖 ESP32 Arduino 核心自带库 (WiFi/WebServer/DNSServer/Preferences)
 */
#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClient.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <Preferences.h>
#include "transport.h"

// ================= NVS 配置 =================
struct NetConfig {
  char ssid[33]  = "";
  char pass[65]  = "";
  char broker[40] = "";
  char topic[40] = "wb/frame";
  char name[24]  = "wb-screen";
  uint16_t port  = 1883;
  bool valid = false;

  void load() {
    Preferences p;
    p.begin("wbnet", true);                 // read-only
    String s = p.getString("ssid", "");
    if (s.length() > 0) {
      strlcpy(ssid,  s.c_str(),                              sizeof(ssid));
      strlcpy(pass, p.getString("pass", "").c_str(),         sizeof(pass));
      strlcpy(broker, p.getString("broker", "").c_str(),     sizeof(broker));
      strlcpy(topic, p.getString("topic", "wb/frame").c_str(), sizeof(topic));
      strlcpy(name,  p.getString("name", "wb-screen").c_str(), sizeof(name));
      port = p.getUShort("port", 1883);
      valid = ssid[0] && broker[0];
    }
    p.end();
  }

  void save() {
    Preferences p;
    p.begin("wbnet", false);
    p.putString("ssid", ssid);
    p.putString("pass", pass);
    p.putString("broker", broker);
    p.putString("topic", topic);
    p.putString("name", name);
    p.putUShort("port", port);
    p.end();
    valid = ssid[0] && broker[0];
  }
};

// ================= MiniMQTT: 手写 MQTT 3.1.1 (QoS 0) =================
class MiniMQTT {
public:
  WiFiClient tcp;
  String broker, topic, clientId;
  uint16_t port = 1883;
  bool linked = false;            // TCP+CONNACK 完成且已订阅
  uint16_t keepalive = 15;

  void (*onMessage)(const char* topic, const uint8_t* payload, uint32_t len) = nullptr;

  uint32_t lastPingMs = 0;
  uint32_t lastTryMs = 0;
  uint32_t backoffMs = 1000;      // 重连退避 1s -> 30s
  uint32_t rxLen = 0;
  uint8_t  rx[640];

  void begin(const char* broker_, uint16_t port_,
             const char* clientId_, const char* topic_) {
    broker = broker_; port = port_;
    clientId = clientId_; topic = topic_;
  }

  // ---- 编码 helpers ----
  static int putStr(uint8_t* b, const char* s) {
    int len = strlen(s), n = 0;
    b[n++] = (uint8_t)(len >> 8);
    b[n++] = (uint8_t)(len & 0xFF);
    memcpy(&b[n], s, len);
    return n + len;
  }

  static int putVarint(uint8_t* b, int len) {
    int n = 0;
    do {
      uint8_t byte = len % 128;
      len /= 128;
      if (len > 0) byte |= 0x80;
      b[n++] = byte;
    } while (len > 0);
    return n;
  }

  bool tcpConnect() {
    tcp.stop();
    if (!tcp.connect(broker.c_str(), port)) return false;
    // CONNECT
    uint8_t body[160];
    int n = 0;
    n += putStr(&body[n], "MQTT");
    body[n++] = 0x04;              // level 4
    body[n++] = 0x02;              // clean session
    body[n++] = (uint8_t)(keepalive >> 8);
    body[n++] = (uint8_t)(keepalive & 0xFF);
    n += putStr(&body[n], clientId.c_str());
    uint8_t hdr[8];
    hdr[0] = 0x10;                                   // CONNECT
    int hn = putVarint(&hdr[1], n);
    tcp.write(hdr, hn + 1);
    tcp.write(body, n);
    tcp.flush();
    rxLen = 0;
    lastPingMs = millis();
    return true;
  }

  bool sendSubscribe() {
    uint8_t body[128];
    int n = 0;
    body[n++] = 0; body[n++] = 1;                    // packet id = 1
    n += putStr(&body[n], topic.c_str());
    body[n++] = 0x00;                                // QoS 0
    uint8_t hdr[8];
    hdr[0] = 0x82;                                   // SUBSCRIBE
    int hn = putVarint(&hdr[1], n);
    tcp.write(hdr, hn + 1);
    tcp.write(body, n);
    tcp.flush();
    return true;
  }

  void sendPing() {
    uint8_t pkt[2] = {0xC0, 0x00};
    tcp.write(pkt, 2);
    tcp.flush();
    lastPingMs = millis();
  }

  // ---- 重连管理 (指数退避) ----
  void ensureConnected(uint32_t now) {
    if (tcp.connected()) return;
    linked = false;
    if (now - lastTryMs < backoffMs) return;
    lastTryMs = now;
    if (tcpConnect()) {
      backoffMs = 1000;                             // 成功则重置退避
    } else {
      backoffMs = min(backoffMs * 2, (uint32_t)30000);
    }
  }

  // ---- 收包解析 (QoS0 子集) ----
  void loop(uint32_t now) {
    while (tcp.available() > 0 && rxLen < sizeof(rx)) {
      rx[rxLen++] = (uint8_t)tcp.read();
    }
    while (parsePacket()) {}                        // 可能一包多帧
    if (linked && now - lastPingMs > keepalive * 800) sendPing();  // 0.8*keepalive
  }

  bool parsePacket() {
    if (rxLen < 2) return false;
    uint8_t type = rx[0] >> 4;
    // 可变长度 remaining
    int rem = 0, mul = 1, i = 1;
    while (i < (int)rxLen && i < 5) {
      uint8_t b = rx[i];
      rem += (b & 0x7F) * mul;
      mul *= 128;
      i++;
      if (!(b & 0x80)) break;
    }
    if (i > (int)rxLen || rxLen < (uint32_t)(i + rem)) return false;  // 未收全

    switch (type) {
      case 2:                                       // CONNACK
        linked = true;
        sendSubscribe();
        break;
      case 9:                                       // SUBACK
        break;
      case 13:                                      // PINGRESP
        break;
      case 3: {                                     // PUBLISH (QoS0)
        if (i + 2 > (int)rxLen) break;
        int tl = (rx[i] << 8) | rx[i + 1];
        int ps = i + 2 + tl;
        if (ps > (int)rxLen) break;
        char tpc[64];
        int cl = min(tl, (int)sizeof(tpc) - 1);
        memcpy(tpc, &rx[i + 2], cl);
        tpc[cl] = 0;
        int plen = rem - 2 - tl;
        if (plen >= 0 && onMessage) {
          onMessage(tpc, &rx[ps], (uint32_t)plen);
        }
        break;
      }
      default: break;                               // 其余类型忽略
    }
    int total = i + rem;
    memmove(rx, &rx[total], rxLen - total);
    rxLen -= total;
    return true;
  }

  void disconnect() {
    tcp.stop();
    linked = false;
  }
};

// ================= NetTransport =================
class NetTransport : public Transport {
public:
  NetConfig cfg;
  MiniMQTT mqtt;
  bool wifiUp = false;
  uint32_t lastWifiTry = 0;

  void begin() override {
    WiFi.mode(WIFI_STA);
    WiFi.begin(cfg.ssid, cfg.pass);
    mqtt.begin(cfg.broker, cfg.port, cfg.name, cfg.topic);
    mqtt.onMessage = onMqttMessage;
  }

  void loop() override {
    uint32_t now = millis();
    if (WiFi.status() != WL_CONNECTED) {
      wifiUp = false;
      if (now - lastWifiTry > 5000) {
        lastWifiTry = now;
        WiFi.begin(cfg.ssid, cfg.pass);
      }
      return;
    }
    if (!wifiUp) {                                  // 刚连上: 重置 MQTT 重连节奏
      wifiUp = true;
      mqtt.lastTryMs = 0;
      mqtt.backoffMs = 1000;
    }
    mqtt.ensureConnected(now);
    mqtt.loop(now);
  }

  bool connected() override { return wifiUp && mqtt.linked; }
  const char* modeName() override { return "WiFi"; }

  static void onMqttMessage(const char* topic, const uint8_t* payload, uint32_t len) {
    (void)topic;
    static char buf[640];
    uint32_t n = min(len, (uint32_t)(sizeof(buf) - 1));
    memcpy(buf, payload, n);
    buf[n] = 0;
    // 容忍一包多帧 (按 \n 切分)
    char* p = buf;
    while (p && *p) {
      char* nl = strchr(p, '\n');
      if (nl) *nl = 0;
      if (*p == '{') onFrame(p);
      if (!nl) break;
      p = nl + 1;
    }
  }
};

// ================= AP 配网 Portal =================
class ConfigPortal {
  DNSServer dns;
  WebServer web{80};
  bool active = false;
  NetConfig* outCfg = nullptr;
  String apSsid;

  static const char PORTAL_HTML[];

  void handleRoot() {
    web.send(200, "text/html", FPSTR(PORTAL_HTML));
  }

  void handleSave() {
    NetConfig c;
    strlcpy(c.ssid,  web.arg("ssid").c_str(),  sizeof(c.ssid));
    strlcpy(c.pass,  web.arg("pass").c_str(),  sizeof(c.pass));
    strlcpy(c.broker, web.arg("broker").c_str(), sizeof(c.broker));
    strlcpy(c.topic, web.arg("topic").c_str(), sizeof(c.topic));
    if (!c.topic[0]) strlcpy(c.topic, "wb/frame", sizeof(c.topic));
    strlcpy(c.name,  web.arg("name").c_str(),  sizeof(c.name));
    if (!c.name[0]) strlcpy(c.name, "wb-screen", sizeof(c.name));
    c.port = (uint16_t)web.arg("port").toInt();
    if (c.port == 0) c.port = 1883;

    if (!c.ssid[0] || !c.broker[0]) {
      web.send(200, "text/html", F("<meta charset=utf-8>SSID 和 Broker 不能为空, 请返回重试"));
      return;
    }
    c.save();
    web.send(200, "text/html",
             F("<meta charset=utf-8><h3>保存成功, 设备重启中...</h3>"));
    delay(800);
    ESP.restart();
  }

public:
  void start(NetConfig* cfg) {
    outCfg = cfg;
    uint8_t mac[6];
    WiFi.macAddress(mac);
    char sfx[5];
    snprintf(sfx, sizeof(sfx), "%02X%02X", (unsigned)mac[4], (unsigned)mac[5]);
    apSsid = String("wb-screen-") + sfx;

    WiFi.mode(WIFI_AP);
    WiFi.softAP(apSsid.c_str());
    dns.start(53, "*", WiFi.softAPIP());            // captive portal
    web.on("/", [this]() { handleRoot(); });
    web.on("/save", [this]() { handleSave(); });
    web.onNotFound([this]() { handleRoot(); });
    web.begin();
    active = true;
  }

  void loop() {
    if (!active) return;
    dns.processNextRequest();
    web.handleClient();
  }

  const char* ssid() const { return apSsid.c_str(); }
  bool running() const { return active; }
  WebServer& server() { return web; }   // 暴露 HTTP 服务 (测试/调试用)
};

const char ConfigPortal::PORTAL_HTML[] PROGMEM = R"HTML(
<!DOCTYPE html><html><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>wb-screen 配网</title>
<style>
body{font-family:-apple-system,"PingFang SC",sans-serif;background:#f4efe6;color:#1a1f1c;
     max-width:420px;margin:40px auto;padding:0 16px}
h2{font-size:20px;margin-bottom:4px}
p{color:#6e7671;font-size:13px;margin-bottom:20px}
label{display:block;font-size:12px;color:#6e7671;margin:12px 0 4px}
input{width:100%;padding:10px;border:1px solid #d8d2c4;border-radius:9px;font-size:14px;
      background:#fff;box-sizing:border-box}
button{margin-top:20px;width:100%;padding:12px;border:0;border-radius:10px;
       background:#1a1f1c;color:#fff;font-size:15px;cursor:pointer}
.tip{font-size:12px;color:#8a918c;margin-top:16px;line-height:1.7}
</style></head><body>
<h2>桌面状态屏配网</h2>
<p>填入家庭 WiFi 与 MQTT Broker (填运行 wb_bridge 的电脑 IP 即可)</p>
<label>WiFi 名称 (SSID)</label><input name="ssid" required>
<label>WiFi 密码</label><input name="pass" type="password">
<label>MQTT Broker 地址</label><input name="broker" placeholder="192.168.1.100" required>
<label>MQTT 端口</label><input name="port" value="1883">
<label>订阅 Topic</label><input name="topic" value="wb/frame">
<label>设备名 (Client ID)</label><input name="name" value="wb-screen">
<button onclick="save()">保存并重启</button>
<div class="tip">保存后屏幕会自动连接 WiFi 与 Broker。<br>
Bridge 在同一台电脑上以 <b>--transport mqtt --broker builtin</b> 运行时,
任何发布到该 Topic 的状态都会显示到屏幕。</div>
<script>
function save(){
  var f=document.createElement('form');f.method='POST';f.action='/save';
  ['ssid','pass','broker','port','topic','name'].forEach(function(k){
    var i=document.createElement('input');i.type='hidden';i.name=k;
    i.value=document.querySelector('[name='+k+']').value;f.appendChild(i);});
  document.body.appendChild(f);f.submit();}
</script></body></html>
)HTML";
