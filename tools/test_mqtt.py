#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
MQTT 链路测试: 内置 broker 与裸客户端互通 / retained / 通配符 / 防回环 /
退避重连 / Bridge 端到端 (transport=mqtt)  —— 无需硬件, 无需第三方 broker
"""
import argparse
import json
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent.parent / "bridge"))
sys.path.insert(0, str(Path(__file__).parent.parent))

from mini_broker import (MiniBroker, decode_varint, encode_str, encode_varint,
                         read_str, topic_matches)
from mqtt_link import MqttLink, RawMqttClient
from wb_bridge import (BRIDGE, WbBridgeApp, frame_to_event, load_config,
                       resolve_broker)
from sources import ST_EXEC, Event

FAIL = 0


def check(cond, msg):
    global FAIL
    print(("ok  : " if cond else "FAIL: ") + msg)
    if not cond:
        FAIL += 1


def drain(client, wait=0.25):
    """等待 broker 转发并驱动客户端 loop 收包"""
    time.sleep(wait)
    for _ in range(4):
        client.loop()
        time.sleep(0.05)


# ============ 1. 协议编解码 ============
print("--- MQTT 协议编解码 ---")
check(encode_varint(0) == b"\x00", "varint 0")
check(encode_varint(127) == b"\x7f", "varint 127 (单字节边界)")
check(encode_varint(128) == b"\x80\x01", "varint 128 (双字节)")
check(encode_varint(16383) == b"\xff\x7f", "varint 16383")
check(encode_varint(16384) == b"\x80\x80\x01", "varint 16384 (三字节)")
for n in (0, 1, 127, 128, 300, 16383, 16384, 2097151, 268435455):
    check(decode_varint(encode_varint(n), 0)[0] == n, f"varint 往返 {n}")
check(encode_str("wb") == b"\x00\x02wb", "string 编码")
check(read_str(encode_str("猫耳屏"), 0)[0] == "猫耳屏", "string 编码(中文)")
try:
    read_str(b"\x00\x05ab", 0)
    check(False, "超长 string 应抛 ValueError")
except ValueError:
    check(True, "超长 string 抛 ValueError")

print("--- topic 通配符匹配 ---")
check(topic_matches("wb/frame", "wb/frame"), "精确匹配")
check(topic_matches("wb/+", "wb/frame"), "+ 匹配单级")
check(not topic_matches("wb/+", "wb/a/b"), "+ 不匹配多级")
check(topic_matches("wb/#", "wb/a/b/c"), "# 匹配多级")
check(topic_matches("#", "anything/goes"), "# 匹配全部")
check(not topic_matches("wb/frame", "wb/other"), "不同 topic 不匹配")
check(not topic_matches("a/b/c", "a/b"), "层级数不同不匹配")

# ============ 2. broker 与裸客户端互通 ============
print("--- MiniBroker <-> RawMqttClient 互通 ---")
broker = MiniBroker("127.0.0.1", 0)
broker.start()
BPORT = broker._srv.getsockname()[1]
check(BPORT > 0, f"broker 监听 127.0.0.1:{BPORT}")

pub = RawMqttClient("127.0.0.1", BPORT, "t-pub")
check(pub.connect(), "客户端 CONNECT -> CONNACK")

sub = RawMqttClient("127.0.0.1", BPORT, "t-sub")
check(sub.connect() and sub.subscribe("wb/frame"), "SUBSCRIBE -> SUBACK")

got = []
sub.on_message = lambda t, p: got.append((t, p))
check(pub.publish("wb/frame", b'{"st":2}', retain=True), "PUBLISH (retain)")
drain(sub)
check(len(got) == 1 and got[0] == ("wb/frame", b'{"st":2}'),
      f"订阅者收到转发帧: {got}")

# 多字节 varint (payload > 127B) 一包多帧
big = json.dumps({"st": 1, "sub": "x" * 500}, ensure_ascii=False).encode()
check(len(big) > 127, f"构造大帧 {len(big)}B (多字节 varint)")
got.clear()
pub.publish("wb/frame", big)
pub.publish("wb/frame", b'{"st":3}')
drain(sub)
check(len(got) == 2 and got[0][1] == big, "大帧 + 连续两帧均正确分发")

# retained: 后订阅的客户端立即拿到最后一帧
sub2 = RawMqttClient("127.0.0.1", BPORT, "t-sub2")
got2 = []
sub2.on_message = lambda t, p: got2.append(p)
check(sub2.connect() and sub2.subscribe("wb/frame"), "第二个客户端订阅")
drain(sub2)
check(len(got2) == 1 and got2[0] == b'{"st":3}',
      f"新订阅者立即收到 retained 帧: {got2}")

# 通配符订阅
subw = RawMqttClient("127.0.0.1", BPORT, "t-subw")
gotw = []
subw.on_message = lambda t, p: gotw.append(t)
check(subw.connect() and subw.subscribe("wb/#"), "通配符订阅 wb/#")
drain(subw)
gotw.clear()
pub.publish("wb/frame", b"{}")
pub.publish("wb/other/deep", b"{}")
drain(subw)
check(sorted(gotw) == ["wb/frame", "wb/other/deep"],
      f"# 通配符收到两级 topic: {gotw}")

# 多订阅者广播
pub.publish("wb/frame", b'{"st":9}')
drain(sub)
drain(sub2)
check(got[-1] == ("wb/frame", b'{"st":9}') and got2[-1] == b'{"st":9}',
      "两个订阅者都收到广播")

# broker on_message 旁路回调
seen = []
broker.on_message = lambda t, p: seen.append(t)
pub.publish("wb/frame", b"{}")
drain(sub)
check("wb/frame" in seen, "broker.on_message 旁路回调触发")
broker.on_message = None

# ============ 3. MqttLink: 发布 / 防回环 / 外部帧 ============
print("--- MqttLink 链路层 ---")
link = MqttLink("127.0.0.1", BPORT, "wb/frame")
check(link.connect(), "MqttLink 连接")
ext = []
link.external_event = ext.append

check(link.publish_frame({"st": 2, "title": "思考"}), "publish_frame 发布")
drain(sub)
last = json.loads(got[-1][1].decode("utf-8"))
check(last.get("_src") == "bridge" and last["st"] == 2,
      f"帧带 _src=bridge 标记: {last}")
check(ext == [], "自带帧不触发 external_event (防回环)")

raw = RawMqttClient("127.0.0.1", BPORT, "t-ai")
raw.connect()
raw.publish("wb/frame",
            json.dumps({"st": 4, "title": "执行命令", "sub": "ls -la",
                        "agent": "DSH"}, ensure_ascii=False).encode())
drain(link.client)
check(len(ext) == 1 and ext[0]["st"] == 4 and ext[0]["agent"] == "DSH",
      f"外部 AI 帧 -> external_event: {ext}")

# 非法帧不崩溃
raw.publish("wb/frame", b"not json")
raw.publish("wb/frame", b'{"no_st": 1}')
drain(link.client)
check(len(ext) == 1, "非法/缺 st 的帧被忽略, 不崩溃")

# ============ 4. 帧 -> 事件转换 ============
print("--- 外部帧转 Event ---")
ev = frame_to_event({"st": 3, "title": "回答", "sub": "你好", "agent": "豆包",
                     "pg": 50})
check(ev is not None and ev.st == 3 and ev.sub == "你好"
      and ev.agent == "豆包" and ev.pg == 50, "完整帧 -> Event")
check(frame_to_event({"st": 99}) is None, "st 越界 -> None")
check(frame_to_event({"st": "abc"}) is None, "st 非数字 -> None")
check(frame_to_event({"st": "3"}) is not None, "st 数字字符串可转换")
ev2 = frame_to_event({})
check(ev2 is not None and ev2.st == 0 and ev2.title == "空闲", "空帧 -> 空闲事件")
check(frame_to_event({"st": 1, "lines": "notalist"}).lines is None,
      "lines 非 list -> None")

# ============ 5. broker 规格解析 ============
print("--- broker 规格解析 ---")
check(resolve_broker("builtin", 1883) == ("builtin", "127.0.0.1", 1883),
      "builtin -> 本机")
check(resolve_broker("192.168.1.50:8883", 1883)
      == ("remote", "192.168.1.50", 8883), "host:port -> 外部")
check(resolve_broker("broker.local", 1883)
      == ("remote", "broker.local", 1883), "仅 host -> 默认端口")
check(resolve_broker("none", 1883)[0] == "none", "none -> 关闭")
check(resolve_broker("", 1883)[0] == "builtin", "空 -> builtin")

# ============ 6. MqttLink 退避重连 ============
print("--- 断线退避重连 ---")
link.client.close()                       # 主动断开
link._last_try = 0.0
link._backoff = 1.0
broker.stop()
time.sleep(0.2)
check(not link.client.connected, "broker 停止后客户端已断开")
link.loop()                               # 尝试重连 -> 失败 -> 退避翻倍
check(not link.connected and link._backoff == 2.0,
      f"重连失败退避翻倍: {link._backoff}")
link.loop()                               # 退避窗口内不重试
check(not link.connected, "退避窗口内不立即重试")

broker2 = MiniBroker("127.0.0.1", BPORT)
broker2.start()
time.sleep(0.2)
link._last_try = 0.0                      # 强制越过退避窗口
link.loop()
check(link.connected, "broker 重启后重连成功")
check(link._backoff == 1.0, "重连成功后退避重置")

# ============ 7. Bridge 端到端 (transport=mqtt) ============
print("--- Bridge 端到端: --transport mqtt ---")
cfg = load_config(Path(__file__).parent.parent / "bridge" / "config.json")
args = argparse.Namespace(port=None, baud=None, agent="HY3", http_port=0,
                          claude_dir=None, no_claude=True, demo=False,
                          heartbeat=None, dry_run=False, once=False,
                          transport="mqtt", broker="builtin", mqtt_port=None,
                          codex=False, codex_dir=None, jsonl=None, watch=None,
                          verbose=False)
app = WbBridgeApp(cfg, args)
check(app.broker is not None, "内置 broker 已启动")
check(app.mqtt is not None and app.mqtt.connected, "Bridge 已连上内置 broker")
check(not app.want_serial, "mqtt 模式不初始化串口")

# Bridge 发帧 -> broker -> 第三方客户端收到
scr = RawMqttClient("127.0.0.1", app.broker._srv.getsockname()[1], "t-screen")
frames = []
scr.on_message = lambda t, p: frames.append(p)
check(scr.connect() and scr.subscribe("wb/frame"), "模拟屏幕订阅 topic")
drain(scr)
frames.clear()
app.send_frame(app.build_frame(Event(ST_EXEC, "执行命令", "ls -la", "HY3",
                                      pg=255),
                               {"cpu": 12, "mem": 66, "uptime": 3600},
                               time.time()))
drain(scr)
check(len(frames) == 1, "Bridge 帧经 broker 送达屏幕订阅者")
fr = json.loads(frames[0].decode("utf-8"))
check(fr["st"] == 4 and fr["pg"] == 255 and fr["up"] == "1小时0分"
      and fr.get("_src") == "bridge", f"帧内容正确: st={fr['st']} pg={fr['pg']}")

# 屏幕视角: 外部 AI 直发 -> Bridge 转为最高优先级事件
ai = RawMqttClient("127.0.0.1", app.broker._srv.getsockname()[1], "t-ai2")
ai.connect()
ai.publish("wb/frame",
           json.dumps({"st": 5, "title": "等待确认", "sub": "git push?",
                       "agent": "Codex"}, ensure_ascii=False).encode())
app.mqtt.loop()
time.sleep(0.2)
app.mqtt.loop()
cur = BRIDGE.current()
check(cur is not None and cur.st == 5 and cur.agent == "Codex",
      f"外部帧进入事件队列: {cur and cur.title}")
ev = app.resolve_event(time.time())
check(ev.st == 5 and ev.sub == "git push?", "resolve_event: MQTT 外部帧优先")

# 清理
ai.close()
scr.close()
app.mqtt.client.close()
app.broker.stop()
link.client.close()
broker2.stop()
for c in (pub, sub, sub2, subw, raw):
    c.close()
check(True, "清理完成")

print("\n" + ("=== 全部通过 ===" if FAIL == 0 else f"=== {FAIL} 项失败 ==="))
sys.exit(1 if FAIL else 0)
