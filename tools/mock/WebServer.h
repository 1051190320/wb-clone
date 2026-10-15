#pragma once
/* WebServer mock — 捕获路由/参数, 支持测试直接模拟请求 */
#include <Arduino.h>
#include <functional>
#include <map>
#include <string>

class WebServer {
public:
  using Handler = std::function<void()>;

  WebServer(int port = 80) { (void)port; }

  void on(const std::string& uri, Handler h) { handlers[uri] = h; }
  void onNotFound(Handler h) { notFound = h; }
  void begin() { running = true; }
  void handleClient() {
    if (pending) { Handler h = pending; pending = nullptr; h(); }
  }
  bool hasArg(const std::string& name) const { return args.count(name) > 0; }
  String arg(const std::string& name) const {
    auto it = args.find(name);
    return String(it != args.end() ? it->second.c_str() : "");
  }
  void send(int code, const std::string& type, const std::string& content) {
    lastCode = code;
    lastType = type;
    lastBody = content;
  }

  // ---- 测试辅助 ----
  std::map<std::string, Handler> handlers;
  Handler notFound;
  Handler pending;
  std::map<std::string, std::string> args;
  bool running = false;
  int lastCode = 0;
  std::string lastType, lastBody;

  void simulateRequest(const std::string& uri,
                       std::map<std::string, std::string> a) {
    args = a;
    auto it = handlers.find(uri);
    pending = it != handlers.end() ? it->second : notFound;
  }
};
