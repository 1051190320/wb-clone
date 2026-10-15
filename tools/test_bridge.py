#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Bridge 端到端测试: 日志推导 / HTTP 推送 / 帧组装 (无需硬件)"""
import argparse
import json
import os
import sys
import tempfile
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent.parent / "bridge"))
sys.path.insert(0, str(Path(__file__).parent.parent))

from sources import (ST_IDLE, ST_THINK, ST_ANSWER, ST_EXEC, ST_WAIT_OK, ST_DONE,
                     Event, ClaudeLogSource, EventHistory, SystemSource,
                     fmt_duration, fmt_uptime)
from wb_bridge import BRIDGE, WbBridgeApp, load_config, frame_to_event
from adapters import CodexLogSource, GenericJsonlSource, ProcessSource

FAIL = 0


def check(cond, msg):
    global FAIL
    print(("ok  : " if cond else "FAIL: ") + msg)
    if not cond:
        FAIL += 1


def append_jsonl(path, obj):
    with open(path, "a", encoding="utf-8") as f:
        f.write(json.dumps(obj, ensure_ascii=False) + "\n")


# ============ 1. Claude 日志推导 ============
print("--- Claude 日志状态推导 ---")
d = tempfile.mkdtemp()
logf = os.path.join(d, "session.jsonl")
open(logf, "w").close()
src = ClaudeLogSource([d], "HY3")
src.poll()   # 先建立 tail (空文件 -> offset=0), 否则会错过首行

append_jsonl(logf, {"type": "assistant", "message": {"role": "assistant", "content": [
    {"type": "tool_use", "name": "Bash", "input": {"command": "ls -la /tmp"}}]}})
ev = src.poll()
check(ev is not None and ev.st == ST_EXEC and ev.title == "执行命令"
      and "ls -la" in ev.sub, f"tool_use -> 执行命令 ({ev and ev.sub})")

append_jsonl(logf, {"type": "assistant", "message": {"role": "assistant", "content": [
    {"type": "text", "text": "这是生成的回答内容"}]}})
ev = src.poll()
check(ev is not None and ev.st == ST_ANSWER and ev.sub == "这是生成的回答内容",
      "text -> 回答")

append_jsonl(logf, {"type": "user", "message": {"role": "user", "content": "帮我写个报告"}})
ev = src.poll()
check(ev is not None and ev.st == ST_THINK and ev.sub == "帮我写个报告",
      "人类输入 -> 思考")

append_jsonl(logf, {"type": "assistant", "message": {
    "role": "assistant", "content": [{"type": "text", "text": "好的"}],
    "stop_reason": "end_turn"}})
ev = src.poll()
check(ev is not None and ev.st == ST_DONE and ev.ttl == 3.0, "end_turn -> 已完成(ttl=3s)")

check(src.poll() is not None and src.poll().st == ST_DONE,
      "无新行 -> 粘性保留上一状态(已完成)")

with open(logf, "a", encoding="utf-8") as f:
    f.write("this is not json\n")
append_jsonl(logf, {"type": "assistant", "message": {"content": [
    {"type": "tool_use", "name": "Grep", "input": {"pattern": "def main"}}]}})
ev = src.poll()
check(ev is not None and ev.title == "搜索代码", "坏行被跳过, Grep -> 搜索代码")

# 非 dict 的合法 JSON 行 (字符串) 不应崩溃, 且不产生伪事件
with open(logf, "a", encoding="utf-8") as f:
    f.write('"just a string"\n')
try:
    ev = src.poll()
    no_crash = True
except Exception:
    no_crash = False
check(no_crash, "JSON 字符串行 -> 不崩溃 (粘性保留上一状态)")

# ============ 2. HTTP 推送 ============
print("--- HTTP 事件队列 ---")
BRIDGE.push(Event(st=6, title="等待确认", sub="git push?", ttl=5.0,
                  expires=time.time() + 5.0))
ev = BRIDGE.current()
check(ev is not None and ev.st == ST_WAIT_OK and ev.sub == "git push?", "push/current")
BRIDGE.push(Event(st=2, title="思考", sub="ttl=0 持续型", ttl=0.0, expires=0.0))
check(BRIDGE.current() is not None, "ttl=0 持续型事件在静默窗口内有效")
BRIDGE.push(Event(st=7, title="已完成", sub="x", ttl=1.0, expires=time.time() - 1))
check(BRIDGE.current() is None, "已过期事件 -> None")

# ============ 3. 帧组装 ============
print("--- 协议帧组装 ---")
cfg = load_config(Path(__file__).parent.parent / "bridge" / "config.json")
args = argparse.Namespace(port=None, baud=None, agent="HY3", http_port=0,
                          claude_dir=None, no_claude=False, demo=False,
                          heartbeat=None, dry_run=True, once=False,
                          transport=None, broker=None, mqtt_port=None,
                          codex=False, codex_dir=None, jsonl=None, watch=None,
                          verbose=False)
app = WbBridgeApp(cfg, args)

ev = Event(ST_EXEC, "执行命令", "ls -la /tmp", "HY3", pg=255)
sysinfo = {"cpu": 11, "mem": 88, "uptime": 39 * 3600, "disk": 36,
           "net": "↓8 KiB/s ↑6 KiB/s"}   # 1天15小时
frame = app.build_frame(ev, sysinfo, time.time())

check(set(frame.keys()) == {"clock", "st", "title", "sub", "agent",
                            "cpu", "mem", "disk", "up", "net", "dur",
                            "pg", "lines"},
      f"字段集合与协议 v1.1 一致: {sorted(frame.keys())}")
check(frame["st"] == 4 and frame["pg"] == 255, "st=4 exec, pg=255")
check(frame["cpu"] == 11 and frame["mem"] == 88, "cpu/mem 透传")
check(frame["disk"] == 36 and frame["net"] == "↓8 KiB/s ↑6 KiB/s", "disk/net 透传")
check(frame["up"] == "1天15小时", f"uptime 格式化: {frame['up']}")
check("bal" not in frame and "model" not in frame,
      "统计字段缺省 -> 不下发 (屏幕隐藏对应格子)")
check(len(frame["clock"]) == 5 and frame["clock"][2] == ":", "clock HH:MM")
s = json.dumps(frame, ensure_ascii=False)
check("执行命令" in s and "\\u" not in s, "json 序列化保留中文不转义")

# 统计网格: Event 带积分字段 -> 透传进帧
ev2 = Event(ST_EXEC, "正在进行", "迁移 QClaw 资产", "space-bunny",
            model="space-bunny", task="迁移 QClaw 资产",
            bal="8046.86 积分", spend="2.92 积分", cost="2.92 积分")
frame2 = app.build_frame(ev2, sysinfo, time.time())
for k, v in (("model", "space-bunny"), ("task", "迁移 QClaw 资产"),
             ("bal", "8046.86 积分"), ("spend", "2.92 积分"),
             ("cost", "2.92 积分")):
    check(frame2.get(k) == v, f"统计字段 {k}={v} 透传")

# 本轮用时 dur: event_start 前移 3 分 25 秒 -> "3:25"
app.event_start = time.time() - 205
check(app.build_frame(ev, sysinfo, time.time())["dur"] == "3:25", "dur=3:25 (本轮用时)")

# 外部帧 -> Event: 统计字段透传 (任意 AI 可直接发积分数据)
fe = frame_to_event({"st": 4, "title": "执行", "bal": "100 积分",
                     "model": "my-model", "task": "跑测试"})
check(fe is not None and fe.bal == "100 积分" and fe.model == "my-model"
      and fe.task == "跑测试", "frame_to_event 透传统计字段")

# 历史行
app.history.push(Event(ST_THINK, "思考", "分析需求"))
app.history.push(Event(ST_ANSWER, "回答", "这是回复"))
hist = app.history.snapshot()
check(len(hist) == 2 and hist[0]["x"] == "回答", "历史行 LIFO, 最新在上")

# 事件优先级: HTTP > claude > 默认
BRIDGE.push(Event(st=1, title="准备", sub="HTTP 优先", ttl=30.0,
                  expires=time.time() + 30.0))
ev = app.resolve_event(time.time())
check(ev.st == 1 and ev.sub == "HTTP 优先", "resolve_event: HTTP 优先")
BRIDGE.push(Event(st=7, title="已完成", sub="x", ttl=1.0, expires=time.time() - 1))
ev = app.resolve_event(time.time())
check(ev.st == ST_IDLE, "resolve_event: 无源时回空闲")

# ============ 4. 多 AI 适配器层 ============
print("--- 适配器: Codex / 通用 JSONL / 进程监控 ---")
# Codex function_call -> 执行
c = CodexLogSource(None, "Codex")
ev = c._parse_line({"type": "response_item", "payload": {
    "type": "function_call", "name": "exec_command",
    "arguments": '{"command": "cargo test"}'}}, 0)
check(ev is not None and ev.st == ST_EXEC and ev.title == "执行命令"
      and "cargo test" in ev.sub, f"Codex function_call -> 执行 ({ev and ev.sub})")
# Codex event_msg task_complete -> 已完成
ev = c._parse_line({"type": "event_msg", "payload": {"type": "turn_complete"}}, 0)
check(ev is not None and ev.st == ST_DONE and ev.ttl == 3.0,
      "Codex turn_complete -> 已完成(ttl=3s)")

# 通用 JSONL 状态名/数字/字段
g = GenericJsonlSource(["/tmp"], "My")
ev = g._parse_line({"state": "thinking", "message": "分析需求"}, 0)
check(ev is not None and ev.st == ST_THINK and ev.sub == "分析需求",
      "通用JSONL state=thinking")
ev = g._parse_line({"st": 4, "title": "跑测试", "detail": "pytest"}, 0)
check(ev is not None and ev.st == ST_EXEC and ev.title == "跑测试",
      "通用JSONL st=4 数字状态码")
ev = g._parse_line({"phase": "unknown_state"}, 0)
check(ev is None, "通用JSONL 无已知状态 -> None")

# 进程监控: 进程出现 -> 执行; 退出 -> 已完成
p = ProcessSource([{"match": ["doubao"], "title": "豆包执行"}], "HY3")
import adapters as _ad
_orig = _ad.list_processes
_ad.list_processes = lambda: ["/usr/bin/doubao --query hi"]
ev = p.poll(100.0)
check(ev is not None and ev.st == ST_EXEC and ev.title == "豆包执行",
      "进程出现 -> 执行中")
_ad.list_processes = lambda: []          # 进程退出
ev = p.poll(105.0)
check(ev is not None and ev.st == ST_DONE and ev.ttl == 3.0,
      "进程退出 -> 已完成(ttl=3s)")
_ad.list_processes = _orig

# 接线: --watch doubao,dsh 应生成 2 条进程规则
wcfg = json.loads(json.dumps(cfg))
wargs = argparse.Namespace(port=None, baud=None, agent="HY3", http_port=0,
                           claude_dir=None, no_claude=False, demo=False,
                           heartbeat=None, dry_run=True, once=False,
                           transport=None, broker=None, mqtt_port=None,
                           codex=True, codex_dir=None,
                           jsonl=["/tmp/genlog"], watch=["doubao,dsh"],
                           verbose=False)
wapp = WbBridgeApp(wcfg, wargs)
check(wapp.codex is not None, "--codex 启用 Codex 适配器")
check(wapp.jsonl is not None, "--jsonl 启用通用 JSONL 适配器")
check(wapp.proc is not None and len(wapp.proc.specs) == 5,
      f"--watch 解析为进程规则 (specs={wapp.proc.specs and len(wapp.proc.specs)})")

# ============ 5. uptime / duration 格式化 ============
print("--- uptime / duration 格式化 ---")
check(fmt_uptime(30 * 86400 + 3 * 3600) == "30天3小时", "30天3小时")
check(fmt_uptime(2 * 3600 + 25 * 60) == "2小时25分", "2小时25分")
check(fmt_uptime(1800) == "30分钟", "30分钟")
check(fmt_duration(205) == "3:25", "duration 205s -> 3:25 (真机同款)")
check(fmt_duration(59) == "0:59", "duration 59s -> 0:59")
check(fmt_duration(3725) == "1:02:05", "duration 3725s -> 1:02:05")
check(fmt_duration(0) == "0:00", "duration 0 -> 0:00")

# ============ 6. 系统采样 (disk/net) ============
print("--- 系统采样 ---")
syssys = SystemSource()
syssys.sample()                       # 首次建立网速基线
time.sleep(0.05)
sm = syssys.sample()
check(isinstance(sm.get("disk"), int), f"disk 为整数: {sm.get('disk')}")
check(isinstance(sm.get("net"), str) and "KiB/s" in sm["net"],
      f"net 速率串: {sm['net']!r}")

print("\n" + ("=== 全部通过 ===" if FAIL == 0 else f"=== {FAIL} 项失败 ==="))
sys.exit(1 if FAIL else 0)
