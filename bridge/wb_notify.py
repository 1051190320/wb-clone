#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
wb_notify — wb-clone 的"一行接入"工具
=====================================
让任意 AI / 脚本 / CI 把状态推到桌面屏, 无需写代码:

  # 1) 走 HTTP (默认, 推给本机 Bridge 的 /event)
  python3 wb_notify.py --st 4 --title 执行命令 --sub "pip install reportlab"
  python3 wb_notify.py --st 3 --title 回答 --sub "已生成报告" --ttl 5

  # 2) 走 MQTT (直接发到 broker, 屏幕或其他 Bridge 都能收到)
  python3 wb_notify.py --mqtt --broker builtin --st 5 --title 等待回答 --sub "A 还是 B?"

  # 3) 用状态名 (中英文均可) 代替数字
  python3 wb_notify.py --state thinking --sub "分析需求"

状态码: 0 空闲 1 准备 2 思考 3 回答 4 执行 5 等待回答 6 等待确认 7 已完成 8 异常
也可用中文/英文: thinking/思考 executing/执行 answering/回答 done/已完成 ...

依赖: requests 仅 HTTP 模式需要; MQTT 模式零依赖 (用内置裸客户端)。
"""
import argparse
import json
import os
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))

# 状态名 -> 数字 (与 sources.DEFAULT_STATE_MAP 保持一致子集)
STATE_ALIAS = {
    "idle": 0, "ready": 1, "thinking": 2, "thought": 2, "reasoning": 2,
    "answer": 3, "answering": 3, "reply": 3, "exec": 4, "executing": 4,
    "tool": 4, "running": 4, "waiting_input": 5, "ask": 5, "confirm": 6,
    "waiting_confirm": 6, "done": 7, "complete": 7, "success": 7,
    "error": 8, "failed": 8,
    "空闲": 0, "准备": 1, "思考": 2, "回答": 3, "执行": 4,
    "等待回答": 5, "等待确认": 6, "已完成": 7, "异常": 8, "错误": 8,
}


def parse_state(raw: str) -> int:
    raw = (raw or "").strip()
    if raw.isdigit():
        v = int(raw)
        return v if 0 <= v <= 8 else 0
    key = raw.lower()
    return STATE_ALIAS.get(key, 0)


def main():
    ap = argparse.ArgumentParser(description="wb_notify — 一行推送状态到桌面屏")
    ap.add_argument("--st", type=int, default=None, help="状态码 0..8")
    ap.add_argument("--state", default=None, help="状态名 (中/英文, 或数字)")
    ap.add_argument("--title", default="", help="标题 (≤11 字)")
    ap.add_argument("--sub", default="", help="副标题 (≤47 字)")
    ap.add_argument("--agent", default="", help="Agent 名")
    ap.add_argument("--pg", type=int, default=-1, help="进度 -1/0..100/255(不确定)")
    ap.add_argument("--ttl", type=float, default=0.0, help="持续秒数, 0=持续")
    # 通道
    ap.add_argument("--mqtt", action="store_true", help="走 MQTT 而非 HTTP")
    ap.add_argument("--broker", default="builtin",
                    help="MQTT broker: builtin | host[:port] | none")
    ap.add_argument("--port", type=int, default=1883, help="内置 broker 端口")
    ap.add_argument("--topic", default="wb/frame", help="MQTT topic")
    ap.add_argument("--host", default="127.0.0.1", help="HTTP Bridge 地址")
    ap.add_argument("--http-port", type=int, default=8788, help="HTTP Bridge 端口")
    args = ap.parse_args()

    st = args.st if args.st is not None else parse_state(args.state or "0")
    if not 0 <= st <= 8:
        st = 0
    payload = {
        "st": st, "title": args.title[:11], "sub": args.sub[:47],
        "agent": args.agent[:11], "pg": args.pg, "ttl": args.ttl,
    }

    if args.mqtt:
        from mqtt_link import RawMqttClient
        from wb_bridge import resolve_broker
        mode, host, port = resolve_broker(args.broker, args.port)
        if mode == "none":
            print("broker=none, 未发送"); return 1
        c = RawMqttClient(host, port, "wb-notify")
        if not c.connect(timeout=3.0):
            print(f"连接 broker 失败 {host}:{port}"); return 1
        ok = c.publish_json(args.topic, payload, retain=False)
        c.close()
        print("MQTT 已发布" if ok else "MQTT 发布失败")
        return 0

    # HTTP
    try:
        import requests
    except ImportError:
        print("HTTP 模式需要 requests: pip install requests"); return 1
    url = f"http://{args.host}:{args.http_port}/event"
    try:
        r = requests.post(url, json=payload, timeout=3)
        print("HTTP", r.status_code, r.text.strip())
        return 0 if r.ok else 1
    except Exception as e:
        print(f"推送失败 ({url}): {e}")
        return 1


if __name__ == "__main__":
    sys.exit(main())
