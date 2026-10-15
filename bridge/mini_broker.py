# -*- coding: utf-8 -*-
"""
mini_broker — wb-clone 内置 MQTT Broker (零第三方依赖)

为什么要有它: 无线模式开箱即用 —— 在跑 wb_bridge 的电脑上起一个 1883 端口,
屏幕配网时 Broker 地址填这台电脑的 IP 即可, 不需要单独装 mosquitto。

同时它让"兼容更多 AI"变得极其简单: 任何程序 (豆包 CLI、Codex、自研 Agent、
Node-RED、Home Assistant...) 只要往 topic 发一条 MQTT 消息, 内容是本协议的
JSON 帧, 就会显示到屏幕上。

支持: MQTT 3.1.1 子集
  客户端: CONNECT / SUBSCRIBE / PUBLISH(QoS0) / PINGREQ / DISCONNECT
  服务端: CONNACK / SUBACK / PUBLISH / PINGRESP
  特性:   QoS0 / retained 消息 / 通配符订阅 (+ 一级, # 多级) / 多客户端
"""
import logging
import socket
import threading
import time

log = logging.getLogger("wb-broker")


# ---------------- 协议编解码 ----------------
def encode_varint(n: int) -> bytes:
    out = bytearray()
    while True:
        b = n % 128
        n //= 128
        if n:
            b |= 0x80
        out.append(b)
        if not n:
            break
    return bytes(out)


def decode_varint(data: bytes, pos: int):
    mult, val, i = 1, 0, pos
    while True:
        if i >= len(data):
            raise ValueError("varint 不完整")
        b = data[i]
        i += 1
        val += (b & 0x7F) * mult
        mult *= 128
        if not (b & 0x80):
            break
        if i - pos > 4:
            raise ValueError("varint 过长")
    return val, i


def encode_str(s: str) -> bytes:
    b = s.encode("utf-8")
    return len(b).to_bytes(2, "big") + b


def read_str(data: bytes, pos: int):
    if pos + 2 > len(data):
        raise ValueError("string 长度字段缺失")
    n = int.from_bytes(data[pos:pos + 2], "big")
    pos += 2
    if pos + n > len(data):
        raise ValueError("string 超长")
    return data[pos:pos + n].decode("utf-8", "replace"), pos + n


def topic_matches(filt: str, topic: str) -> bool:
    """MQTT 通配符匹配: + 单级, # 多级"""
    f = filt.split("/")
    t = topic.split("/")
    for i, part in enumerate(f):
        if part == "#":
            return True
        if i >= len(t):
            return False
        if part != "+" and part != t[i]:
            return False
    return len(f) == len(t)


# ---------------- Broker ----------------
class MiniBroker:
    """极简 MQTT broker: 线程模型为主线程 accept + 每客户端一个读线程"""

    def __init__(self, host: str = "0.0.0.0", port: int = 1883):
        self.host = host
        self.port = port
        self.retained: dict[str, tuple[bytes, float]] = {}
        self._clients: list[dict] = []     # {"conn", "subs", "id"}
        self._lock = threading.Lock()
        self._srv: socket.socket | None = None
        self._running = False
        self.on_message = None          # 回调 (topic, payload) — bridge 用来旁路监听
        self.client_count = 0

    def start(self):
        self._srv = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self._srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self._srv.bind((self.host, self.port))
        self._srv.listen(8)
        self._srv.settimeout(1.0)
        self._running = True
        threading.Thread(target=self._accept_loop, daemon=True,
                         name="mini-broker").start()
        log.info("内置 MQTT broker 监听 %s:%d (屏幕 Broker 地址填本机 IP)",
                 self.host, self.port)

    def stop(self):
        self._running = False
        if self._srv:
            try:
                self._srv.close()
            except OSError:
                pass

    def _accept_loop(self):
        while self._running:
            try:
                conn, addr = self._srv.accept()
            except socket.timeout:
                continue
            except OSError:
                return
            self.client_count += 1
            threading.Thread(target=self._client_loop, args=(conn, addr),
                             daemon=True).start()

    def _client_loop(self, conn: socket.socket, addr):
        conn.settimeout(600.0)          # 10 分钟无数据断开
        subs: list[str] = []
        cid = f"{addr[0]}:{addr[1]}"
        entry = {"conn": conn, "subs": subs, "id": cid}
        with self._lock:
            self._clients.append(entry)
        buf = b""
        try:
            while self._running:
                # ---- 读一个包 ----
                while True:
                    pkt, rest = self._try_parse(buf)
                    if pkt is None:
                        chunk = conn.recv(4096)
                        if not chunk:
                            return
                        buf += chunk
                        continue
                    buf = rest
                    break
                ptype, flags, body = pkt

                if ptype == 1:          # CONNECT
                    _, pos = read_str(body, 0)      # protocol name
                    pos += 4                          # level/flags/keepalive
                    client_id, _ = read_str(body, pos)
                    cid = client_id or cid
                    conn.sendall(bytes([0x20, 0x02, 0x00, 0x00]))   # CONNACK

                elif ptype == 8:        # SUBSCRIBE
                    pkt_id = int.from_bytes(body[0:2], "big")
                    pos = 2
                    granted = bytearray()
                    while pos < len(body):
                        topic, pos = read_str(body, pos)
                        qos = body[pos] if pos < len(body) else 0
                        pos += 1
                        if topic not in subs:
                            subs.append(topic)
                        granted.append(0)            # 只支持 QoS0
                        # 立即投递该 topic 的 retained 消息
                        with self._lock:
                            retained_now = [(rt, p) for rt, (p, _)
                                            in self.retained.items()
                                            if topic_matches(topic, rt)]
                        for rt, p in retained_now:
                            self._send_publish(conn, rt, p)
                    conn.sendall(bytes([0x90]) + encode_varint(2 + len(granted))
                                 + pkt_id.to_bytes(2, "big") + bytes(granted))

                elif ptype == 3:        # PUBLISH
                    retain = flags & 0x01
                    topic, pos = read_str(body, 0)
                    payload = body[pos:]
                    if retain:
                        with self._lock:
                            self.retained[topic] = (payload, time.time())
                    # 转发给所有匹配订阅者 (含自己, 符合 MQTT 语义)
                    with self._lock:
                        targets = [c["conn"] for c in self._clients
                                   if any(topic_matches(f, topic)
                                          for f in c["subs"])]
                    for tconn in targets:
                        self._send_publish(tconn, topic, payload)
                    if self.on_message:
                        try:
                            self.on_message(topic, payload)
                        except Exception:
                            pass
                    log.debug("PUBLISH %s <- %s (%dB) -> %d 订阅者",
                              topic, cid, len(payload), len(targets))

                elif ptype == 12:       # PINGREQ
                    conn.sendall(bytes([0xD0, 0x00]))

                elif ptype == 14:       # DISCONNECT
                    return

                # 其余类型忽略
        except (ConnectionError, socket.timeout, ValueError) as e:
            log.debug("客户端 %s 断开: %s", cid, e)
        finally:
            with self._lock:
                if entry in self._clients:
                    self._clients.remove(entry)
            try:
                conn.close()
            except OSError:
                pass

    def _send_publish(self, conn: socket.socket, topic: str, payload: bytes):
        body = encode_str(topic) + payload
        try:
            conn.sendall(bytes([0x30]) + encode_varint(len(body)) + body)
        except OSError:
            pass

    @staticmethod
    def _try_parse(buf: bytes):
        """返回 (packet, rest); 数据不足返回 (None, buf)"""
        if len(buf) < 2:
            return None, buf
        try:
            rem, pos = decode_varint(buf, 1)
        except ValueError:
            return None, buf
        if len(buf) < pos + rem:
            return None, buf
        return (buf[0] >> 4, buf[0] & 0x0F, buf[pos:pos + rem]), buf[pos + rem:]
