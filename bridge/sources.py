# -*- coding: utf-8 -*-
"""
wb-clone 状态采集模块
- SystemSource    : CPU / 内存 / 开机时长 (psutil)
- JsonlTailSource : JSONL 日志 tail 基类 (找最新文件/跟随/粘性事件)
- ClaudeLogSource : tail Claude Code 会话日志 (~/.claude/projects/**/*.jsonl)
- EventHistory    : 屏幕日志行的环形缓冲 (最多 4 条)
"""
import json
import os
import time
from dataclasses import dataclass, field
from pathlib import Path
from typing import Optional, List

try:
    import psutil
except ImportError:  # 允许在无 psutil 环境降级运行
    psutil = None

# ---- 状态枚举 (与固件 ui.h 保持一致) ----
ST_IDLE, ST_READY, ST_THINK, ST_ANSWER, ST_EXEC, \
    ST_WAIT_ANS, ST_WAIT_OK, ST_DONE, ST_ERR = range(9)

ST_NAMES = ["空闲", "准备", "思考", "回答", "执行", "等待回答", "等待确认", "已完成", "异常"]

# 屏幕日志行的颜色码 (与固件 logColor 一致): 0g 1b 2y 3r 4n
ST_LOG_COLOR = {
    ST_IDLE: 4, ST_READY: 1, ST_THINK: 1, ST_ANSWER: 0,
    ST_EXEC: 1, ST_WAIT_ANS: 2, ST_WAIT_OK: 2, ST_DONE: 0, ST_ERR: 3,
}

# Claude Code 工具名 -> 中文标题
TOOL_NAMES = {
    "Bash": "执行命令", "Read": "读取文件", "Edit": "编辑代码",
    "Write": "编辑代码", "NotebookEdit": "编辑代码", "Grep": "搜索代码",
    "Glob": "搜索代码", "Task": "调度子任务", "WebFetch": "联网检索",
    "WebSearch": "联网检索", "TodoWrite": "更新任务", "AskUserQuestion": "等待回答",
}


@dataclass
class Event:
    """一个 Agent 状态事件"""
    st: int = ST_IDLE
    title: str = "空闲"
    sub: str = ""
    agent: str = ""
    pg: int = -1                # -1 无进度, 0..100, 255 不确定
    ttl: float = 0.0            # 持续秒数, 0 = 持续到下一个事件
    expires: float = 0.0        # 内部: 过期时间戳
    lines: Optional[List[dict]] = None   # 覆盖屏幕日志行
    # ---- 统计网格 (协议 v1.1, HTTP 推送可带, 空串 = 不下发该格) ----
    model: str = ""             # 当前任务模型
    task: str = ""              # 任务名称
    bal: str = ""               # 剩余积分
    spend: str = ""             # 本轮消耗
    cost: str = ""              # 今日费用

    def is_alive(self, now: float) -> bool:
        return self.ttl > 0 and now < self.expires


def fmt_duration(sec: float) -> str:
    """秒 -> 'M:SS' / 'H:MM:SS' (真机同款 '3:25')"""
    sec = int(max(0, sec))
    h, rem = divmod(sec, 3600)
    m, s = divmod(rem, 60)
    if h:
        return f"{h}:{m:02d}:{s:02d}"
    return f"{m}:{s:02d}"


class SystemSource:
    """系统资源采样: CPU / 内存 / 磁盘 / 网速 / 开机时长"""

    def __init__(self):
        self._boot = time.time() - self._uptime_sec()
        self._net_last: Optional[dict] = None
        self._net_at = 0.0

    @staticmethod
    def _uptime_sec() -> float:
        if psutil is not None:
            return time.time() - psutil.boot_time()
        # 无 psutil: 读 /proc/uptime (Linux)
        try:
            with open("/proc/uptime") as f:
                return float(f.read().split()[0])
        except Exception:
            return 0.0

    def _net_speed(self) -> str:
        """两次采样间的收发速率, 如 '↓8 KiB/s ↑6 KiB/s' (真机同款)"""
        try:
            if psutil is not None:
                io = psutil.net_io_counters()
                cur = {"rx": io.bytes_recv, "tx": io.bytes_sent}
            else:
                with open("/proc/net/dev") as f:
                    lines = f.readlines()[2:]
                cur = {"rx": sum(int(l.split()[1]) for l in lines),
                       "tx": sum(int(l.split()[9]) for l in lines)}
        except Exception:
            return ""
        now = time.time()
        if self._net_last is None or now <= self._net_at:
            self._net_last, self._net_at = cur, now
            return ""
        dt = now - self._net_at
        rx = (cur["rx"] - self._net_last["rx"]) / dt / 1024.0
        tx = (cur["tx"] - self._net_last["tx"]) / dt / 1024.0
        self._net_last, self._net_at = cur, now

        def fmt(v: float) -> str:
            if v >= 1024:
                return f"{v / 1024:.1f} MiB/s"
            return f"{int(round(v))} KiB/s"

        return f"\u2193{fmt(rx)} \u2191{fmt(tx)}"

    def sample(self) -> dict:
        if psutil is not None:
            cpu = int(psutil.cpu_percent(interval=None))
            mem = int(psutil.virtual_memory().percent)
            try:
                disk = int(psutil.disk_usage("/").percent)
            except Exception:
                disk = 0
        else:
            cpu = mem = disk = 0
        return {"cpu": cpu, "mem": mem, "disk": disk,
                "uptime": time.time() - self._boot,
                "net": self._net_speed()}


def fmt_uptime(sec: float) -> str:
    sec = int(max(0, sec))
    d, rem = divmod(sec, 86400)
    h, rem = divmod(rem, 3600)
    m = rem // 60
    if d:
        return f"{d}天{h}小时"
    if h:
        return f"{h}小时{m}分"
    return f"{m}分钟"


class JsonlTailSource:
    """
    JSONL 日志 tail 基类: 每 2 秒找目录下最新的 *.jsonl, 从末尾开始跟随,
    逐行解析成 Event。

    "粘性"语义: 派生出的状态会持续显示, 直到被新事件取代;
    ttl>0 的事件 (如"已完成" 3 秒) 到期后自动清除。
    """

    def __init__(self, dirs: List[str], agent: str = "HY3"):
        self.dirs = [Path(d).expanduser() for d in dirs]
        self.agent = agent
        self._file: Optional[Path] = None
        self._fh = None
        self._offset = 0
        self._last_scan = 0.0
        self._current: Optional[Event] = None

    # ---- 文件发现: 每 2 秒找一次最新 jsonl ----
    def _find_latest(self) -> Optional[Path]:
        latest, mt = None, 0.0
        for d in self.dirs:
            if not d.is_dir():
                continue
            for p in d.rglob("*.jsonl"):
                try:
                    m = p.stat().st_mtime
                except OSError:
                    continue
                if m > mt:
                    latest, mt = p, m
        return latest

    def _open(self, path: Path):
        if self._fh:
            try:
                self._fh.close()
            except Exception:
                pass
        self._fh = open(path, "r", encoding="utf-8", errors="replace")
        self._fh.seek(0, os.SEEK_END)      # 从末尾开始 tail
        self._offset = self._fh.tell()
        self._file = path

    # ---- 子类实现: 单行 -> Event 或 None ----
    def _parse_line(self, obj: dict, now: float) -> Optional[Event]:
        raise NotImplementedError

    # ---- 主入口: 返回当前生效事件 (无则 None) ----
    def poll(self, now: Optional[float] = None) -> Optional[Event]:
        now = now or time.time()
        if now - self._last_scan > 2.0:
            self._last_scan = now
            latest = self._find_latest()
            if latest and latest != self._file:
                self._open(latest)
        if self._fh is not None:
            try:
                if self._file and self._file.stat().st_size < self._offset:
                    self._open(self._file)      # 日志被截断/轮转
                self._fh.seek(self._offset)
                last = None
                for line in self._fh:
                    line = line.strip()
                    if not line:
                        continue
                    try:
                        obj = json.loads(line)
                    except json.JSONDecodeError:
                        continue
                    ev = self._parse_line(obj, now)
                    if ev:
                        last = ev
                self._offset = self._fh.tell()
                if last is not None:
                    self._current = last
            except (OSError, ValueError):
                pass
        return self._expire(now)

    def _expire(self, now: float) -> Optional[Event]:
        cur = self._current
        if cur is not None and cur.ttl > 0 and now >= cur.expires:
            self._current = None
            return None
        return cur


class ClaudeLogSource(JsonlTailSource):
    """
    tail Claude Code 的会话日志, 推导 Agent 状态。

    日志格式 (~/.claude/projects/<项目>/<session>.jsonl, 每行一个 JSON):
      {"type":"assistant","message":{"content":[{"type":"tool_use","name":"Bash",...}],
                                    "stop_reason":"end_turn"}}
      {"type":"user","message":{"content":"用户输入"}}   # 人类输入 -> 即将思考
    """

    # ---- 解析单行 -> Event 或 None ----
    def _parse_line(self, obj: dict, now: float) -> Optional[Event]:
        if not isinstance(obj, dict):
            return None
        mtype = obj.get("type")
        msg = obj.get("message") or {}
        if not isinstance(msg, dict):
            return None
        content = msg.get("content")
        role = msg.get("role", "")

        if mtype == "assistant" or (role == "assistant" and content):
            # 优先判完成: end_turn 行通常也带最终 text
            if msg.get("stop_reason") == "end_turn":
                return Event(ST_DONE, "已完成", "本轮回复结束", self.agent,
                             ttl=3.0, expires=now + 3.0)
            if isinstance(content, list):
                tool = next((b for b in reversed(content)
                             if isinstance(b, dict) and b.get("type") == "tool_use"), None)
                text = next((b for b in reversed(content)
                             if isinstance(b, dict) and b.get("type") == "text"
                             and b.get("text", "").strip()), None)
                if tool:
                    name = tool.get("name", "")
                    title = TOOL_NAMES.get(name, "执行工具")
                    sub = self._tool_summary(name, tool.get("input") or {})
                    return Event(ST_EXEC, title, sub, self.agent, pg=255, expires=now)
                if text:
                    return Event(ST_ANSWER, "回答", text["text"].strip().replace("\n", " ")[:24],
                                 self.agent, expires=now)
            return None

        if mtype == "user" or (role == "user" and content):
            if isinstance(content, str) and content.strip():
                return Event(ST_THINK, "思考", content.strip().replace("\n", " ")[:24],
                             self.agent, expires=now)
            # tool_result 属于 exec 的后续, 不产生新状态
            return None
        return None

    @staticmethod
    def _tool_summary(name: str, inp: dict) -> str:
        for key in ("command", "file_path", "pattern", "path", "query", "prompt", "description"):
            v = inp.get(key)
            if isinstance(v, str) and v.strip():
                return v.strip().replace("\n", " ")[:24]
        return ""


class EventHistory:
    """屏幕日志行环形缓冲 (最多 keep 条)"""

    def __init__(self, keep: int = 4):
        self.keep = keep
        self.items: List[dict] = []

    def push(self, ev: Event):
        t = time.strftime("%H:%M")
        self.items.insert(0, {
            "t": t,
            "x": ST_NAMES[ev.st],
            "c": ST_LOG_COLOR.get(ev.st, 4),
            "s": ev.sub or "",
        })
        del self.items[self.keep:]

    def snapshot(self) -> List[dict]:
        return list(self.items)


class DemoSource:
    """演示模式: 循环展示全部状态 (无 Claude 日志时用)"""

    # (状态, 标题, 摘要) — 统计网格演示值, 模拟一次真实任务的积分消耗
    SCRIPT = [
        (ST_READY,    "准备",     "环境检查完成"),
        (ST_THINK,    "思考",     "正在分析需求"),
        (ST_EXEC,     "执行命令", "pip install reportlab"),
        (ST_ANSWER,   "回答",     "这是生成的第一段回复"),
        (ST_WAIT_ANS, "等待回答", "需要你确认方案 A/B"),
        (ST_WAIT_OK,  "等待确认", "是否允许执行该命令"),
        (ST_DONE,     "已完成",   "任务全部完成"),
        (ST_IDLE,     "空闲",     "等待新的任务"),
    ]
    # 每步对应的统计网格 (模型/任务/剩余积分/本轮消耗/今日费用)
    STATS = [
        ("HY3-Pro",  "部署桌面状态屏", "8046.86 积分", "-",        "-"),
        ("HY3-Pro",  "部署桌面状态屏", "8046.86 积分", "1.24 积分", "2.92 积分"),
        ("HY3-Pro",  "部署桌面状态屏", "8046.86 积分", "2.05 积分", "2.92 积分"),
        ("HY3-Pro",  "部署桌面状态屏", "8046.86 积分", "2.92 积分", "2.92 积分"),
        ("HY3-Pro",  "部署桌面状态屏", "8046.86 积分", "2.92 积分", "2.92 积分"),
        ("HY3-Pro",  "部署桌面状态屏", "8046.86 积分", "2.92 积分", "2.92 积分"),
        ("HY3-Pro",  "部署桌面状态屏", "8046.86 积分", "2.92 积分", "2.92 积分"),
        ("HY3-Pro",  "等待新任务",     "8046.86 积分", "-",        "-"),
    ]

    def __init__(self, agent: str = "HY3"):
        self.agent = agent
        self.i = 0
        self.next_at = 0.0

    def poll(self, now: Optional[float] = None) -> Optional[Event]:
        now = now or time.time()
        if now < self.next_at:
            return None
        st, title, sub = self.SCRIPT[self.i % len(self.SCRIPT)]
        model, task, bal, spend, cost = self.STATS[self.i % len(self.STATS)]
        self.i += 1
        self.next_at = now + 2.5
        return Event(st, title, sub, self.agent,
                     pg=255 if st in (ST_THINK, ST_EXEC) else -1,
                     ttl=2.5, expires=now + 2.5,
                     model=model, task=task, bal=bal, spend=spend, cost=cost)
