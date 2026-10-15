#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
wb-clone 电脑端 Bridge
========================
把 Agent 状态从电脑送到桌面小屏幕:
  1. 传输: USB 串口 (默认) 或 WiFi/MQTT (--transport mqtt/both)
  2. 自动发现 USB 串口 (ESP32-S3 CDC)
  3. 采集状态:
     - Claude Code 会话日志 tail  (自动推导 思考/执行/回答/已完成)
     - HTTP API 推送 (任意程序/agent, POST /event)
     - MQTT 推送 (任意 AI 直接把协议帧发到 broker)
     - 演示模式 (--demo, 无日志时循环展示)
  4. 附带系统监控 CPU/内存/开机时长
  5. 按协议组帧发给屏幕; 断线自动重连

用法:
  python3 wb_bridge.py                     # USB 串口 + 全部数据源
  python3 wb_bridge.py --transport mqtt    # WiFi/MQTT (内置 broker 开箱即用)
  python3 wb_bridge.py --transport both    # 串口 + MQTT 双发
  python3 wb_bridge.py --dry-run           # 不接硬件, 打印协议帧
  python3 wb_bridge.py --port COM5         # 指定串口 (Windows)
  python3 wb_bridge.py --demo              # 演示模式
"""
import argparse
import json
import logging
import os
import socket
import sys
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from typing import Optional

sys.path.insert(0, str(Path(__file__).parent))
from sources import (ST_IDLE, ST_NAMES, ClaudeLogSource, DemoSource,
                     Event, EventHistory, SystemSource, fmt_duration,
                     fmt_uptime)
from adapters import (CodexLogSource, GenericJsonlSource, ProcessSource)
from mini_broker import MiniBroker
from mqtt_link import MqttLink

try:
    import serial
    import serial.tools.list_ports
except ImportError:
    serial = None

log = logging.getLogger("wb-bridge")

DEFAULT_CONFIG = {
    "serial": {"port": "auto", "baud": 115200,
               # 常见 USB 串口芯片: CH340(1A86) / CP210x(10C4) / ESP32-S3 原生(303A)
               "vid": ["0x1A86", "0x10C4", "0x303A"], "pid": None},
    "transport": "serial",            # serial | mqtt | both
    "mqtt": {"enabled": False,        # True 时即使 transport=serial 也启用 MQTT
             "broker": "builtin",     # builtin | host[:port] | none
             "host": "127.0.0.1",
             "port": 1883,
             "topic": "wb/frame"},
    "http": {"enabled": True, "port": 8788},
    "agent": "HY3",
    "claude_log": {"enabled": True, "dirs": ["~/.claude/projects"]},
    "adapters": {
        "codex":  {"enabled": False, "dirs": []},          # OpenAI Codex CLI 日志
        "jsonl":  {"enabled": False, "dirs": []},          # 自研 Agent 的 JSONL
        "process": {"enabled": False, "specs": [           # 监控进程 (豆包/DSH 等)
            {"match": ["doubao"], "title": "豆包执行", "sub": ""},
            {"match": ["dsh"],     "title": "DSH 执行",  "sub": ""},
            {"match": ["codex"],   "title": "Codex 执行", "sub": ""},
        ]},
    },
    "heartbeat_sec": 2.0,
    "demo": False,
}


def load_config(path: Path) -> dict:
    cfg = json.loads(json.dumps(DEFAULT_CONFIG))   # deep copy
    if path and path.exists():
        with open(path, encoding="utf-8") as f:
            user = json.load(f)
        for k, v in user.items():
            if isinstance(v, dict) and isinstance(cfg.get(k), dict):
                cfg[k].update(v)
            else:
                cfg[k] = v
    return cfg


# ---------------------------------------------------------------- 串口
def find_port(cfg_serial: dict):
    """自动发现串口: 优先 vid 列表匹配, 其次常见 USB 串口关键词"""
    if serial is None:
        return None
    ports = list(serial.tools.list_ports.comports())
    if not ports:
        return None

    def parse(v):
        try:
            return int(str(v), 16)
        except (TypeError, ValueError):
            return None

    raw = cfg_serial.get("vid")
    if isinstance(raw, (list, tuple)):
        vids = [parse(v) for v in raw]
    else:
        vids = [parse(raw)]
    vids = [v for v in vids if v is not None]
    pid = parse(cfg_serial.get("pid"))
    if vids:
        for v in vids:
            for p in ports:
                if p.vid == v and (pid is None or p.pid == pid):
                    return p.device
    KEYS = ("USB SERIAL", "USB-SERIAL", "JTAG", "CP210", "CH340", "ESP32", "FT232")
    for p in ports:
        desc = f"{p.description or ''} {p.hwid or ''}".upper()
        if any(k in desc for k in KEYS):
            return p.device
    return ports[0].device


# ---------------------------------------------------------------- MQTT
def resolve_broker(spec: str, default_port: int):
    """解析 broker 规格: builtin | host[:port] | none
    返回 (mode, host, port): mode in builtin/remote/none"""
    spec = (spec or "builtin").strip()
    if spec in ("none", "off", "disabled"):
        return "none", "", 0
    if spec == "builtin":
        return "builtin", "127.0.0.1", default_port
    host, _, p = spec.partition(":")
    if not host:
        return "builtin", "127.0.0.1", default_port
    try:
        port = int(p) if p else default_port
    except ValueError:
        port = default_port
    return "remote", host, port


def frame_to_event(frame: dict) -> Optional[Event]:
    """外部 AI 发来的协议帧 -> Event; 不合法返回 None"""
    try:
        st = int(frame.get("st", 0))
        if not 0 <= st < len(ST_NAMES):
            return None
    except (TypeError, ValueError):
        return None
    lines = frame.get("lines")
    if not isinstance(lines, list):
        lines = None
    return Event(
        st=st,
        title=str(frame.get("title", ST_NAMES[st]))[:23],
        sub=str(frame.get("sub", ""))[:47],
        agent=str(frame.get("agent", ""))[:11],
        pg=int(frame.get("pg", -1)),
        ttl=0.0,                       # 持续型: 30s 静默窗口后回到日志/空闲
        lines=lines,
        model=str(frame.get("model", ""))[:19],    # 统计网格 (协议 v1.1)
        task=str(frame.get("task", ""))[:27],
        bal=str(frame.get("bal", ""))[:19],
        spend=str(frame.get("spend", ""))[:19],
        cost=str(frame.get("cost", ""))[:19],
    )


# ---------------------------------------------------------------- HTTP API
class Bridge:
    """单例, 供 HTTP handler 访问"""

    def __init__(self):
        self.http_event: Event = Event()
        self.http_event_at = 0.0
        self.last_frame: dict = {}      # 最近一次发送的协议帧 (供 /state 查询)

    def push(self, ev: Event):
        self.http_event = ev
        self.http_event_at = time.time()

    def current(self) -> Optional[Event]:
        """返回当前生效的 HTTP 事件。
        ttl>0: 按 expires 精确过期; ttl=0: 持续型, 30s 静默窗口后失效"""
        ev = self.http_event
        now = time.time()
        if ev.ttl > 0:
            return ev if now < ev.expires else None
        return ev if now - self.http_event_at < 30.0 else None

    def alive_http(self):
        return self.current() is not None


BRIDGE = Bridge()


def make_handler(bridge_ref):
    class Handler(BaseHTTPRequestHandler):
        server_version = "wb-bridge/1.0"

        def _send_json(self, code: int, obj: dict):
            body = json.dumps(obj, ensure_ascii=False).encode("utf-8")
            self.send_response(code)
            self.send_header("Content-Type", "application/json; charset=utf-8")
            self.send_header("Content-Length", str(len(body)))
            self.send_header("Access-Control-Allow-Origin", "*")   # 允许网页模拟器跨域拉取
            self.end_headers()
            self.wfile.write(body)

        def do_OPTIONS(self):
            self.send_response(204)
            self.send_header("Access-Control-Allow-Origin", "*")
            self.send_header("Access-Control-Allow-Methods", "GET, POST, OPTIONS")
            self.send_header("Access-Control-Allow-Headers", "Content-Type")
            self.end_headers()

        def do_POST(self):
            if self.path not in ("/event", "/status"):
                return self._send_json(404, {"ok": False, "error": "not found"})
            try:
                n = int(self.headers.get("Content-Length", 0))
                data = json.loads(self.rfile.read(n) or b"{}")
                if not isinstance(data, dict):
                    raise ValueError("body must be a JSON object")
            except Exception:
                return self._send_json(400, {"ok": False, "error": "bad json"})
            # 与外部 MQTT 帧同一套转换: 支持 v1.1 统计字段
            # (model/task/bal/spend/cost), st 非法时返回 None
            ev = frame_to_event(data)
            if ev is None:
                return self._send_json(400, {"ok": False, "error": "st must be 0..8"})
            ev.ttl = float(data.get("ttl", 0.0))
            ev.expires = time.time() + ev.ttl
            bridge_ref.push(ev)
            log.info("HTTP 事件: st=%d %s %s", ev.st, ev.title, ev.sub)
            self._send_json(200, {"ok": True, "st": ev.st, "title": ev.title})

        def do_GET(self):
            if self.path == "/healthz":
                return self._send_json(200, {"ok": True})
            if self.path == "/state":
                ev = bridge_ref.current()
                return self._send_json(200, {
                    "ok": True,
                    "event": None if ev is None else {
                        "st": ev.st, "title": ev.title, "sub": ev.sub,
                        "agent": ev.agent, "pg": ev.pg},
                    "frame": bridge_ref.last_frame or None,
                })
            self._send_json(404, {"ok": False, "error": "not found"})

        def log_message(self, fmt, *args):   # 静音默认访问日志
            log.debug("http %s", fmt % args)

    return Handler


# ---------------------------------------------------------------- 主类
class WbBridgeApp:
    def __init__(self, cfg: dict, args):
        self.cfg = cfg
        self.args = args
        self.agent = args.agent or cfg["agent"]
        self.heartbeat = args.heartbeat or cfg["heartbeat_sec"]
        self.dry_run = args.dry_run
        self.system = SystemSource()
        self.history = EventHistory(4)
        self.claude = None
        self.demo = None
        self.ser = None
        self.port = args.port
        self.last_key = None
        self.last_send = 0.0
        self.event_start = time.time()   # 本轮事件开始时间 (dur 计时)
        self._stop = threading.Event()

        # 传输通道: CLI > config; mqtt enabled 时也强制启用
        self.transport = args.transport or cfg.get("transport", "serial")
        self.mqtt_cfg = cfg["mqtt"]
        if self.mqtt_cfg.get("enabled") and self.transport == "serial":
            self.transport = "both"
        self.want_serial = self.transport in ("serial", "both") and not self.dry_run
        self.want_mqtt = (self.transport in ("mqtt", "both")
                          and not self.dry_run)

        # 数据源
        cl = cfg["claude_log"]
        if args.claude_dir:
            dirs = args.claude_dir
        else:
            dirs = cl.get("dirs", [])
        if cl.get("enabled", True) and not args.no_claude:
            self.claude = ClaudeLogSource(dirs, self.agent)
        if args.demo or cfg.get("demo"):
            self.demo = DemoSource(self.agent)

        # ---- 多 AI 适配器层 (adapters.py) ----
        ad = cfg.get("adapters", {})
        self.codex = None
        self.jsonl = None
        self.proc = None
        # Codex CLI 日志 (默认目录 ~/.codex/sessions)
        if (args.codex or ad.get("codex", {}).get("enabled", False)) \
                and not args.no_claude:
            cdirs = args.codex_dir or ad.get("codex", {}).get("dirs") or None
            self.codex = CodexLogSource(cdirs, self.agent)
        # 通用 JSONL (自研 Agent 往 JSONL 写 {"state":...} 即可上屏)
        jdirs = args.jsonl or ad.get("jsonl", {}).get("dirs") or []
        if jdirs:
            self.jsonl = GenericJsonlSource(list(jdirs), self.agent)
        # 进程监控 (豆包 CLI / DSH 等不吐结构化日志的工具)
        pspecs = list(ad.get("process", {}).get("specs", []) or [])
        if args.watch:
            # --watch doubao,dsh  (可多次) => 按名字监控, 标题自动 "X 执行"
            names = []
            for item in args.watch:
                names.extend(n.strip() for n in item.split(",") if n.strip())
            for name in names:
                pspecs.append({"match": [name], "title": f"{name} 执行"})
        if args.watch or ad.get("process", {}).get("enabled", False):
            self.proc = ProcessSource(pspecs, self.agent)

        # HTTP
        self.httpd = None
        http_port = args.http_port if args.http_port is not None else cfg["http"]["port"]
        if cfg["http"].get("enabled", True) and http_port:
            try:
                self.httpd = ThreadingHTTPServer(("127.0.0.1", http_port),
                                                 make_handler(BRIDGE))
                threading.Thread(target=self.httpd.serve_forever,
                                 daemon=True).start()
                log.info("HTTP API: http://127.0.0.1:%d (POST /event)", http_port)
            except OSError as e:
                log.warning("HTTP 服务启动失败 (%s), 状态推送不可用", e)

        # MQTT 无线链路 (内置 broker 或外部 broker)
        self.broker = None
        self.mqtt = None
        if self.want_mqtt:
            self.setup_mqtt(args)

        # 串口
        if self.want_serial:
            self.connect_serial()

    # ---- MQTT ----
    def setup_mqtt(self, args):
        spec = args.broker or self.mqtt_cfg.get("broker", "builtin")
        port = args.mqtt_port or self.mqtt_cfg.get("port", 1883)
        mode, host, port = resolve_broker(spec, port)
        topic = self.mqtt_cfg.get("topic", "wb/frame")
        if mode == "none":
            log.warning("broker=none, MQTT 未启用")
            self.want_mqtt = False
            return
        if mode == "builtin":
            self.broker = MiniBroker("0.0.0.0", port)
            try:
                self.broker.start()
            except OSError as e:
                log.warning("内置 broker 启动失败 (%s), 尝试连接外部 broker", e)
                self.broker = None
        self.mqtt = MqttLink(host, port, topic)
        self.mqtt.external_event = self.on_external_frame
        if not self.mqtt.connect():          # 首次同步尝试, 失败由 loop 退避重连
            log.info("MQTT 连接中 (后台重试): %s:%d topic=%s",
                     host, port, topic)

    def on_external_frame(self, frame: dict):
        """其他 AI 直接发到 broker 的协议帧 -> 事件 (最高优先级)"""
        if frame.get("_src") == "bridge":     # 自己的帧, 防回环
            return
        ev = frame_to_event(frame)
        if ev is None:
            return
        BRIDGE.push(ev)
        log.info("MQTT 外部事件: st=%d %s %s (agent=%s)",
                 ev.st, ev.title, ev.sub, ev.agent)

    # ---- 串口 ----
    def connect_serial(self):
        if serial is None:
            log.error("未安装 pyserial: pip install pyserial")
            return
        port = self.port or "auto"
        if port == "auto":
            port = find_port(self.cfg["serial"])
        if not port:
            log.warning("未找到串口设备, 5 秒后重试 (可用 --port 指定)")
            return
        try:
            self.ser = serial.Serial(port, self.cfg["serial"]["baud"], timeout=0,
                                     write_timeout=2)
            log.info("串口已连接: %s @ %d", port, self.cfg["serial"]["baud"])
        except Exception as e:
            log.warning("串口打开失败 %s: %s", port, e)

    # ---- 帧 ----
    def build_frame(self, ev: Event, sysinfo: dict, now: float) -> dict:
        lines = ev.lines if ev.lines is not None else self.history.snapshot()
        frame = {
            "clock": time.strftime("%H:%M", time.localtime(now)),
            "st": ev.st, "title": ev.title, "sub": ev.sub,
            "agent": ev.agent,
            "cpu": sysinfo["cpu"], "mem": sysinfo["mem"],
            "disk": sysinfo.get("disk", 0),
            "up": fmt_uptime(sysinfo["uptime"]),
            "net": sysinfo.get("net", ""),
            "dur": fmt_duration(now - self.event_start),   # 本轮用时
            "pg": ev.pg,
            "lines": lines[:4],
        }
        # 统计网格可选字段 (空串不下发, 屏幕隐藏对应格子)
        for key, val in (("model", ev.model), ("task", ev.task),
                         ("bal", ev.bal), ("spend", ev.spend),
                         ("cost", ev.cost)):
            if val:
                frame[key] = val
        return frame

    def send_frame(self, frame: dict):
        BRIDGE.last_frame = frame
        data = (json.dumps(frame, ensure_ascii=False) + "\n").encode("utf-8")
        if self.dry_run:
            print("[frame]", data.decode("utf-8").strip())
            return
        if self.transport in ("serial", "both") and self.ser and self.ser.is_open:
            try:
                self.ser.write(data)
                self.ser.flush()
            except Exception as e:
                log.warning("串口写入失败: %s", e)
                try:
                    self.ser.close()
                except Exception:
                    pass
        if self.transport in ("mqtt", "both") and self.mqtt:
            if not self.mqtt.publish_frame(frame):
                log.debug("MQTT 发布失败 (将在 loop 中重连)")

    # ---- 主循环 ----
    def resolve_event(self, now: float) -> Event:
        ev = BRIDGE.current()                       # 1. HTTP / 外部 MQTT 推送优先
        if ev is not None:
            return ev
        if self.claude:                             # 2. Claude Code 日志推导
            ev = self.claude.poll(now)
            if ev is not None:
                return ev
        if self.codex:                              # 3. Codex CLI 日志推导
            ev = self.codex.poll(now)
            if ev is not None:
                return ev
        if self.jsonl:                              # 4. 通用 JSONL
            ev = self.jsonl.poll(now)
            if ev is not None:
                return ev
        if self.proc:                               # 5. 进程监控 (豆包/DSH 等)
            ev = self.proc.poll(now)
            if ev is not None:
                return ev
        if self.demo:                               # 6. 演示
            ev = self.demo.poll(now)
            if ev is not None:
                return ev
        return Event(ST_IDLE, "空闲", "等待新的任务", self.agent)   # 7. 默认空闲

    def run(self):
        log.info("wb-bridge 启动 (agent=%s, transport=%s, dry_run=%s)",
                 self.agent, self.transport, self.dry_run)
        try:
            while not self._stop.is_set():
                now = time.time()
                if self.mqtt:
                    self.mqtt.loop()           # MQTT 收发 + 断线重连
                ev = self.resolve_event(now)
                sysinfo = self.system.sample()

                key = (ev.st, ev.title, ev.sub, ev.agent, ev.pg)
                if key != self.last_key:            # 状态变化: 记录日志行 + 立即发送
                    self.last_key = key
                    self.event_start = now          # 本轮用时重新计时
                    if ev.lines is None and ev.st != ST_IDLE:
                        self.history.push(ev)
                    self.send_frame(self.build_frame(ev, sysinfo, now))
                    self.last_send = now
                elif now - self.last_send >= self.heartbeat:   # 心跳
                    self.send_frame(self.build_frame(ev, sysinfo, now))
                    self.last_send = now

                if self.want_serial and (self.ser is None or not self.ser.is_open):
                    self.connect_serial()
                time.sleep(0.2)
        except KeyboardInterrupt:
            pass
        finally:
            if self.ser and self.ser.is_open:
                self.ser.close()
            if self.mqtt:
                self.mqtt.client.close()
            if self.broker:
                self.broker.stop()
            if self.httpd:
                self.httpd.shutdown()
            log.info("已退出")

    def run_once(self):
        """发一帧就退出 (--once, 用于测试)"""
        if self.mqtt and not self.mqtt.connected:
            self.mqtt.connect()
        now = time.time()
        ev = self.resolve_event(now)
        frame = self.build_frame(ev, self.system.sample(), now)
        self.send_frame(frame)


# ---------------------------------------------------------------- CLI
def main():
    ap = argparse.ArgumentParser(description="wb-clone 电脑端 Bridge")
    ap.add_argument("--config", type=Path,
                    default=Path(__file__).parent / "config.json")
    ap.add_argument("--port", default=None, help="串口, 如 COM5 / /dev/ttyACM0 / auto")
    ap.add_argument("--baud", type=int, default=None)
    ap.add_argument("--transport", choices=["serial", "mqtt", "both"],
                    default=None, help="传输通道 (默认 serial, 可用 config.json)")
    ap.add_argument("--broker", default=None,
                    help="MQTT broker: builtin (默认, 本进程内置) | "
                         "host[:port] (外部 mosquitto/EMQX) | none")
    ap.add_argument("--mqtt-port", type=int, default=None,
                    help="内置 broker 监听端口 (默认 1883)")
    ap.add_argument("--agent", default=None, help="屏幕上显示的 Agent 名")
    ap.add_argument("--http-port", type=int, default=None, help="0 关闭 HTTP API")
    ap.add_argument("--claude-dir", action="append",
                    help="Claude 日志目录 (可多次), 默认 ~/.claude/projects")
    ap.add_argument("--no-claude", action="store_true", help="关闭日志监控")
    ap.add_argument("--codex", action="store_true",
                    help="启用 Codex CLI 日志适配器 (~/.codex/sessions)")
    ap.add_argument("--codex-dir", action="append",
                    help="Codex 日志目录 (可多次)")
    ap.add_argument("--jsonl", action="append",
                    help="通用 JSONL 目录 (可多次); 自研 Agent 写 {state:...} 即上屏")
    ap.add_argument("--watch", action="append",
                    help="按进程名监控 (可多次, 逗号分隔), 如 --watch doubao,dsh")
    ap.add_argument("--demo", action="store_true", help="演示模式")
    ap.add_argument("--heartbeat", type=float, default=None, help="心跳秒数")
    ap.add_argument("--dry-run", action="store_true", help="不接硬件, 打印协议帧")
    ap.add_argument("--once", action="store_true", help="发送一帧后退出")
    ap.add_argument("-v", "--verbose", action="store_true")
    args = ap.parse_args()

    logging.basicConfig(
        level=logging.DEBUG if args.verbose else logging.INFO,
        format="%(asctime)s %(levelname)s %(message)s", datefmt="%H:%M:%S")

    cfg = load_config(args.config)
    if args.baud:
        cfg["serial"]["baud"] = args.baud
    app = WbBridgeApp(cfg, args)
    if args.once:
        app.run_once()
        return
    app.run()


if __name__ == "__main__":
    main()
