#pragma once
/*
 * wb-clone OTA 无线更新 —「WiFi 推送固件」通道
 *
 * 两条通道, 均带密码 (OTA_PASSWORD):
 *   1) Web OTA    — 连上 WiFi 后, 浏览器打开 http://<设备IP>/update 上传 .bin
 *                    (AP 配网模式下 80 端口归 ConfigPortal, 故仅 STA 模式启用)
 *   2) ArduinoOTA — Arduino IDE / PlatformIO 无线上传, 主机名 <设备名>-<MAC后4位>.local
 *
 * 事件全程 Serial 日志; 更新成功后 1s 自动重启 (Update 库写入非活动 OTA 分区,
 * 失败不影响当前运行中的固件)。
 *
 * 依赖: ESP32 Arduino 核心自带 ArduinoOTA / ESPmDNS / Update / WebServer
 */
#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <Update.h>
#include <ArduinoOTA.h>

extern const char* g_linkMode;   // 定义于 wb_display.ino

class OtaService {
public:
  static constexpr const char* OTA_PASSWORD = "wbclone";

  // name: NetConfig.name (设备名); 内部追加 MAC 后四位避免同名冲突
  void begin(const char* name) {
    uint8_t mac[6];
    WiFi.macAddress(mac);
    char sfx[5];
    snprintf(sfx, sizeof(sfx), "%02X%02X", (unsigned)mac[4], (unsigned)mac[5]);
    _host = String(name) + "-" + sfx;

    // ---- ArduinoOTA (UDP 3232 + mDNS 广播) ----
    ArduinoOTA.setHostname(_host.c_str())
              .setPassword(OTA_PASSWORD)
              .onStart([]() { Serial.println("OTA[A]: start"); Serial.flush(); })
              .onEnd([]() { Serial.println("\nOTA[A]: done, reboot"); Serial.flush(); })
              .onProgress([](unsigned int p, unsigned int total) {
                unsigned pct = total ? (unsigned)(p * 100 / total) : 0;
                Serial.printf("OTA[A]: %u%%\r", pct);
                Serial.flush();
              })
              .onError([](ota_error_t e) {
                Serial.printf("OTA[A]: error %u\n", (unsigned)e);
                Serial.flush();
              });
    ArduinoOTA.begin();
    Serial.printf("OTA[A]: host=%s.local port=3232 pass=%s\n",
                  _host.c_str(), OTA_PASSWORD);
    Serial.flush();
  }

  void loop() {
    if (!_otaBusy) ArduinoOTA.handle();   // Web 上传期间挂起, 避免双通道同写 flash
    // STA 连上后自动起 Web OTA (AP 模式 80 端口归配网 portal)
    if (!_webUp && WiFi.getMode() == WIFI_STA && WiFi.status() == WL_CONNECTED) {
      startWeb();
    }
    if (_webUp) _web.handleClient();
    if (_rebootPending && millis() - _rebootAt > 1000) {
      ESP.restart();
    }
  }

  bool webUp() const { return _webUp; }
  const String& host() const { return _host; }

private:
  String    _host;
  WebServer _web{80};
  bool      _webUp = false;
  bool      _otaBusy = false;     // Web 上传进行中 -> 挂起 ArduinoOTA
  bool      _rebootPending = false;
  bool      _deny = false;          // Web 上传密码校验失败
  uint32_t  _rebootAt = 0;

  // ================= Web OTA =================
  void startWeb() {
    _web.on("/", [this]() { handleRoot(); });
    _web.on("/api/status", [this]() { handleStatus(); });
    _web.on("/update", HTTP_GET, [this]() { handleUpdatePage(); });
    _web.on("/update", HTTP_POST,
            [this]() { handleUpdateResp(); },
            [this]() { handleUpdateUpload(); });
    _web.begin();
    _webUp = true;
    Serial.printf("OTA[W]: ready http://%s/update (pass=%s)\n",
                  WiFi.localIP().toString().c_str(), OTA_PASSWORD);
    Serial.flush();
  }

  static String pageHead(const char* title) {
    return String(F("<!DOCTYPE html><html><head><meta charset=utf-8>"
                    "<meta name=viewport content='width=device-width,initial-scale=1'>"
                    "<title>")) + title + F("</title><style>"
                    "body{font-family:-apple-system,'PingFang SC',sans-serif;background:#f4efe6;"
                    "color:#1a1f1c;max-width:520px;margin:40px auto;padding:0 16px}"
                    "h2{font-size:20px;margin-bottom:4px}p{color:#6e7671;font-size:13px}"
                    "a{color:#1a1f1c}table{font-size:13px;border-collapse:collapse;margin:16px 0}"
                    "td{padding:4px 14px 4px 0;color:#6e7671}td:last-child{color:#1a1f1c}"
                    "input{width:100%;padding:10px;border:1px solid #d8d2c4;border-radius:9px;"
                    "font-size:14px;background:#fff;box-sizing:border-box}"
                    "button{margin-top:16px;width:100%;padding:12px;border:0;border-radius:10px;"
                    "background:#1a1f1c;color:#fff;font-size:15px;cursor:pointer}"
                    "#bar{height:8px;background:#e6e0d4;border-radius:4px;margin-top:14px;display:none}"
                    "#bar>i{display:block;height:100%;width:0;background:#1a1f1c;border-radius:4px}"
                    "#msg{font-size:13px;margin-top:12px;min-height:18px}"
                    "</style></head><body>");
  }

  void handleRoot() {
    String html = pageHead("wb-screen 状态");
    html += F("<h2>桌面状态屏</h2><p>WiFi 已连接, 无线推送在线</p><table>");
    html += "<tr><td>设备名</td><td>" + _host + "</td></tr>";
    html += "<tr><td>IP 地址</td><td>" + WiFi.localIP().toString() + "</td></tr>";
    html += "<tr><td>信号</td><td>" + String(WiFi.RSSI()) + " dBm</td></tr>";
    html += F("<tr><td>通道</td><td>");
    html += (g_linkMode ? g_linkMode : "?");
    html += F("</td></tr>");
    html += "<tr><td>运行时长</td><td>" + String(millis() / 1000) + " s</td></tr>";
    html += "<tr><td>空闲内存</td><td>" + String(esp_get_free_heap_size()) + " B</td></tr>";
    html += "</table><p><a href='/update'>→ 无线更新固件 (OTA)</a></p>";
    html += F("</body></html>");
    _web.send(200, "text/html", html);
  }

  void handleStatus() {   // 机器可读状态 (调试用)
    String j = String(F("{\"host\":\"")) + _host +
               F("\",\"ip\":\"") + WiFi.localIP().toString() +
               F("\",\"rssi\":") + String(WiFi.RSSI()) +
               F(",\"mode\":\"") + (g_linkMode ? g_linkMode : "?") +
               F("\",\"uptime_ms\":") + String(millis()) +
               F(",\"free_heap\":") + String(esp_get_free_heap_size()) +
               F(",\"web_ota\":") + (_webUp ? "true" : "false") + "}";
    _web.send(200, "application/json", j);
  }

  void handleUpdatePage() {
    String html = pageHead("wb-screen OTA");
    html += F("<h2>无线更新固件</h2><p>选择编译产物 .bin (app 分区镜像), 上传后自动重启</p>"
              "<form id='f' method='POST' action='/update' enctype='multipart/form-data'>"
              "<label>OTA 密码</label><input name='pass' type='password' placeholder='wbclone'>"
              "<label style='margin-top:12px'>固件文件</label>"
              "<input name='firmware' type='file' accept='.bin' required>"
              "<button type='submit'>上传并更新</button></form>"
              "<div id=bar><i></i></div><div id=msg></div>"
              "<p style='margin-top:18px;font-size:12px;color:#8a918c'>"
              "更新写入备用分区, 失败不影响当前固件; 成功后设备重启。</p>"
              "<script>"
              "document.getElementById('f').onsubmit=function(e){"
              "e.preventDefault();var f=this,d=new FormData(f),x=new XMLHttpRequest();"
              "document.getElementById('bar').style.display='block';"
              "x.upload.onprogress=function(ev){if(ev.lengthComputable){"
              "document.querySelector('#bar>i').style.width=(ev.loaded*100/ev.total)+'%';}};"
              "x.onload=function(){document.getElementById('msg').textContent="
              "(x.status==200)?'更新成功, 设备重启中... 约 10s 后刷新本页':('失败: '+x.responseText);};"
              "x.onerror=function(){document.getElementById('msg').textContent='网络错误';};"
              "x.open('POST','/update');x.send(d);};"
              "</script></body></html>");
    _web.send(200, "text/html", html);
  }

  // ---- 上传流 (multipart) ----
  void handleUpdateUpload() {
    HTTPUpload& up = _web.upload();
    switch (up.status) {
      case UPLOAD_FILE_START: {
        Serial.printf("OTA[W]: upload start '%s'\n", up.filename.c_str());
        Serial.flush();
        _deny = (_web.arg("pass") != OTA_PASSWORD);
        if (_deny) {
          Serial.println("OTA[W]: bad password, rejected");
          Serial.flush();
          return;
        }
        _otaBusy = true;                          // 挂起 ArduinoOTA, 防双通道同写
        if (!Update.begin(UPDATE_SIZE_UNKNOWN)) {
          Serial.printf("OTA[W]: Update.begin failed (%d)\n", (int)Update.getError());
          Update.printError(Serial);
        }
        break;
      }
      case UPLOAD_FILE_WRITE:
        if (!_deny && Update.isRunning()) {
          if (Update.write(up.buf, up.currentSize) != up.currentSize) {
            Serial.printf("OTA[W]: write failed (%d)\n", (int)Update.getError());
          }
        }
        break;
      case UPLOAD_FILE_END:
        _otaBusy = false;
        if (_deny) break;
        if (Update.end(true)) {
          Serial.printf("OTA[W]: success %u bytes, reboot in 1s\n",
                        (unsigned)up.totalSize);
          Serial.flush();
          _rebootPending = true;
          _rebootAt = millis();
        } else {
          Serial.printf("OTA[W]: Update.end failed (%d)\n", (int)Update.getError());
          Update.printError(Serial);
        }
        break;
      case UPLOAD_FILE_ABORTED:
        _otaBusy = false;
        Update.abort();
        Serial.println("OTA[W]: aborted");
        Serial.flush();
        break;
      default: break;
    }
  }

  void handleUpdateResp() {
    if (_deny) {
      _deny = false;
      _web.send(403, "text/plain", "BAD PASSWORD");
    } else if (Update.hasError()) {
      _web.send(500, "text/plain", "UPDATE FAILED");
    } else {
      _web.send(200, "text/plain", "OK");
    }
  }
};
