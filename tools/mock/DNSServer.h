#pragma once
/* DNSServer mock */
#include <cstdint>

class DNSServer {
public:
  bool start(uint16_t port, const char* domain, const char* ip) {
    (void)port; (void)domain; (void)ip;
    started = true;
    return true;
  }
  void processNextRequest() {}
  bool started = false;
};
