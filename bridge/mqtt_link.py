# -*- coding: utf-8 -*-
"""
mqtt_link — wb-clone MQTT 链路

- RawMqttClient: 裸 socket MQTT 3.1.1 客户端 (CONNECT/SUBSCRIBE/PUBLISH/PING)
  零第三方依赖, 可连内置 broker 或任何标准 broker (mosquitto/EMQX...)
- MqttLink:      Bridge 侧封装, 负责断线重连, 把协议帧 publish 到 topic

与 firmware/wb_display/wb_net.h 的 MiniMQTT 协议互通 (两端都按 MQTT 3.1.1 QoS0 子集)。
"""
import json
import logging
import socket
import time

log = logging.getLogger("wb-mqtt")


# ---------------- 协议编解码 (与 mini_broker 共用同一套实现) ----------------
from mini_broker import (encode_varint, encode_str, read_str,
                         topic_matches, decode_varint)


class RawMqttClient:
    """裸 socket MQTT 客户端 (QoS0 子集)"""

    def __init__(self, host: str, port: int, client_id: str, keepalive: int = 15):
        self.host = host
        self.port = port
        self.client_id = client_id
        self.keepalive = keepalive
        self.sock: socket.socket | None = None
        self._rx = b""
        self._last_ping = 0.0
        self.on_message = None        # 回调 (topic, payload: bytes)
        self._subs: list[str] = []

    # ---- 连接 ----
    def connect(self, timeout: float = 3.0) -> bool:
        try:
            self.sock = socket.create_connection((self.host, self.port),
                                                 timeout=timeout)
        except OSError as e:
            log.debug("MQTT 连接失败 %s:%d: %s", self.host, self.port, e)
            self.sock = None
            return False
        body = (encode_str("MQTT") + bytes([0x04, 0x02])
                + self.keepalive.to_bytes(2, "big")
                + encode_str(self.client_id))
        self._send(0x10, body)
        self._rx = b""
        if not self._wait_packet(2, timeout):      # 等 CONNACK (type=2)
            self.close()
            return False
        self._last_ping = time.time()
        return True

    def close(self):
        if self.sock:
            try:
                self._send(0xE0, b"")               # DISCONNECT
            except OSError:
                pass
            try:
                self.sock.close()
            except OSError:
                pass
        self.sock = None

    @property
    def connected(self) -> bool:
        return self.sock is not None

    # ---- 订阅 ----
    def subscribe(self, topic: str) -> bool:
        if topic not in self._subs:
            self._subs.append(topic)
        if not self.connected:
            return False
        body = (b"\x00\x01" + encode_str(topic) + bytes([0x00]))
        try:
            self._send(0x82, body)
            self._wait_packet(9, 2.0)             # 等 SUBACK (type=9)
            return True
        except OSError:
            return False

    # ---- 发布 ----
    def publish(self, topic: str, payload: bytes, retain: bool = True) -> bool:
        if not self.connected:
            return False
        body = encode_str(topic) + payload
        try:
            self._send(0x30 | (0x01 if retain else 0x00), body)
            return True
        except OSError:
            self.close()
            return False

    def publish_json(self, topic: str, obj: dict, retain: bool = True) -> bool:
        return self.publish(topic,
                            json.dumps(obj, ensure_ascii=False).encode("utf-8"),
                            retain)

    # ---- 保活/收包 ----
    def ping(self):
        if self.connected:
            try:
                self._send(0xC0, b"")
                self._last_ping = time.time()
            except OSError:
                self.close()

    def loop(self):
        """非阻塞: 处理 PINGRESP 与收到的 PUBLISH"""
        if not self.connected:
            return
        if time.time() - self._last_ping > self.keepalive * 0.8:
            self.ping()
        try:
            self.sock.settimeout(0.05)
            chunk = self.sock.recv(4096)
            if not chunk:
                self.close()
                return
            self._rx += chunk
        except socket.timeout:
            return
        except OSError:
            self.close()
            return
        while True:
            pkt, rest = self._try_parse(self._rx)
            if pkt is None:
                break
            self._rx = rest
            self._dispatch(pkt)

    # ---- 内部 ----
    def _send(self, ptype: int, body: bytes):
        self.sock.sendall(bytes([ptype]) + encode_varint(len(body)) + body)

    def _wait_packet(self, want_type: int, timeout: float) -> bool:
        deadline = time.time() + timeout
        while time.time() < deadline:
            # 先处理缓冲区里已有的包: 等候类型 -> 成功; 其余 (如 retained
            # PUBLISH 先于 SUBACK 到达) 必须分发而不是丢弃
            while True:
                pkt, rest = self._try_parse(self._rx)
                if pkt is None:
                    break
                self._rx = rest
                if pkt[0] == want_type:
                    return True
                self._dispatch(pkt)
            try:
                self.sock.settimeout(max(0.05, deadline - time.time()))
                chunk = self.sock.recv(4096)
                if not chunk:
                    return False
                self._rx += chunk
            except socket.timeout:
                continue
            except OSError:
                return False
        return False

    def _dispatch(self, pkt):
        """按类型处理一个已解析的包"""
        ptype, _flags, body = pkt
        if ptype == 3:                          # PUBLISH
            topic, pos = read_str(body, 0)
            payload = body[pos:]
            if self.on_message:
                try:
                    self.on_message(topic, payload)
                except Exception:
                    pass

    @staticmethod
    def _try_parse(buf: bytes):
        if len(buf) < 2:
            return None, buf
        try:
            rem, pos = decode_varint(buf, 1)
        except ValueError:
            return None, buf
        if len(buf) < pos + rem:
            return None, buf
        return (buf[0] >> 4, buf[0] & 0x0F, buf[pos:pos + rem]), buf[pos + rem:]


# ---------------- Bridge 侧封装 ----------------
class MqttLink:
    """
    Bridge 的 MQTT 出口:
      - builtin: 连本进程 MiniBroker
      - 外部:    连 mosquitto/EMQX 等
    断线自动重连 (指数退避), 帧以 retained 消息发布。
    同时订阅自己的 frame topic, 把"其他 AI 直接发到 broker 的帧"转成事件
    —— 这就是"任何 AI 都能接入"的通道。
    """

    def __init__(self, host: str, port: int, topic: str,
                 client_id: str = "wb-bridge"):
        self.topic = topic
        self.client = RawMqttClient(host, port, client_id)
        self.client.on_message = self._on_message
        self._backoff = 1.0
        self._last_try = 0.0
        self.external_event = None       # 回调 (frame_dict) — 外部 AI 发来的帧
        self._last_frame = {}

    def connect(self) -> bool:
        ok = self.client.connect()
        if ok:
            self.client.subscribe(self.topic)      # 旁路监听外部 AI 的帧
            self._backoff = 1.0
            log.info("MQTT 已连接 %s:%d, topic=%s",
                     self.client.host, self.client.port, self.topic)
        return ok

    def publish_frame(self, frame: dict) -> bool:
        # _src 标记防止回环: broker 会把 retained 帧回发给订阅者 (含本客户端)
        payload = dict(frame)
        payload["_src"] = "bridge"
        return self.client.publish_json(self.topic, payload, retain=True)

    def loop(self):
        if not self.client.connected:
            now = time.time()
            if now - self._last_try >= self._backoff:
                self._last_try = now
                self.connect()
                if not self.client.connected:
                    self._backoff = min(self._backoff * 2, 30.0)
            return
        self.client.loop()

    # 其他 AI 直接发布到 broker 的帧 -> 交给 Bridge 主循环
    def _on_message(self, topic: str, payload: bytes):
        if topic != self.topic:
            return
        try:
            obj = json.loads(payload.decode("utf-8"))
        except (ValueError, UnicodeDecodeError):
            return
        if isinstance(obj, dict) and "st" in obj:
            if obj.get("_src") == "bridge":    # 自己 publish 的帧, 防回环
                return
            if self.external_event:
                self.external_event(obj)

    @property
    def connected(self) -> bool:
        return self.client.connected
