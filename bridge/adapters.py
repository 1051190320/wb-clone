# -*- coding: utf-8 -*-
"""
adapters — 多 AI 适配器层: 让任意 Agent 的状态上屏

已内置:
  CodexLogSource     OpenAI Codex CLI  (~/.codex/sessions/**/rollout-*.jsonl)
  GenericJsonlSource 通用 JSONL 规则适配 (自研 Agent 往 JSONL 写一行状态即可)
  ProcessSource      进程监控 (豆包 CLI / DSH 等不吐结构化日志的工具:
                     进程出现 -> 执行中, 进程退出 -> 已完成)

加上已有的接入方式:
  ClaudeLogSource    Claude Code 日志自动推导 (sources.py)
  HTTP POST /event   任意程序主动推送 (wb_notify.py / curl)
  MQTT publish       任意 AI 直接把协议帧发到 broker
"""
import json
import os
import subprocess
import time
from pathlib import Path
from typing import List, Optional

from sources import (ST_ANSWER, ST_DONE, ST_ERR, ST_EXEC, ST_IDLE, ST_NAMES,
                     ST_THINK, Event, JsonlTailSource)

try:
    import psutil
except ImportError:      # 允许在无 psutil 环境降级运行
    psutil = None


# ================================================================ Codex CLI
class CodexLogSource(JsonlTailSource):
    """
    tail OpenAI Codex CLI 的 rollout 日志, 推导 Agent 状态。

    文件: ~/.codex/sessions/YYYY/MM/DD/rollout-*.jsonl
          (或 $CODEX_HOME/sessions; .jsonl.zst 压缩版跳过)
    每行一个事件信封:
      {"timestamp":"...","type":"response_item","payload":{...}}
    外层 type: session_meta | turn_context | response_item | event_msg | input_item
    response_item.payload.type:
      message           {"role":"user"|"assistant",
                         "content":[{"type":"output_text","text":"..."}]}
                         (旧版扁平格式: payload.role / payload.message)
      function_call     {"name":"exec_command",
                         "arguments":"{\\"command\\":\\"cargo test\\"}",  # JSON 字符串!
                         "call_id":"..."}
      function_call_output  工具结果, 不产生状态
    event_msg.payload.type: user_message | agent_message | task_started |
                            task_complete | turn_complete | error | token_count
    注意: 新版 rollout 中同一轮对话在 response_item 与 event_msg 两条流里
    各有一份, 映射到相同状态故重复无害。
    """

    TOOL_NAMES = {
        "exec_command": "执行命令", "shell_command": "执行命令",
        "shell": "执行命令", "bash": "执行命令",
        "apply_patch": "编辑代码", "edit": "编辑代码", "write": "编辑代码",
        "update_plan": "更新任务", "todo_write": "更新任务",
        "read_file": "读取文件", "view_image": "读取文件",
        "web_search": "联网检索",
    }

    def __init__(self, dirs: Optional[List[str]] = None, agent: str = "HY3"):
        if not dirs:
            dirs = [os.environ.get("CODEX_HOME",
                                   str(Path.home() / ".codex")) + "/sessions"]
        super().__init__(dirs, agent)

    # ---- 解析单行 ----
    def _parse_line(self, obj: dict, now: float) -> Optional[Event]:
        if not isinstance(obj, dict):
            return None
        mtype = obj.get("type")
        payload = obj.get("payload")
        if not isinstance(payload, dict):
            payload = obj              # 旧版扁平信封
        ptype = payload.get("type", "")

        # ---- function_call -> 执行 ----
        if ptype == "function_call":
            name = str(payload.get("name", ""))
            args = self._parse_args(payload.get("arguments"))
            title = self.TOOL_NAMES.get(name, "执行工具")
            return Event(ST_EXEC, title, self._arg_summary(args),
                         self.agent, pg=255, expires=now)

        # ---- message -> 用户输入 / 助手回答 ----
        if ptype == "message":
            role = payload.get("role", "")
            text = self._content_text(payload)
            if not text:
                return None
            if role == "assistant":
                return Event(ST_ANSWER, "回答", text[:24], self.agent,
                             expires=now)
            if role == "user":
                return Event(ST_THINK, "思考", text[:24], self.agent,
                             expires=now)
            return None

        # ---- event_msg 生命周期 ----
        if ptype == "user_message":
            text = self._msg_text(payload)
            return Event(ST_THINK, "思考", text[:24], self.agent,
                         expires=now) if text else None
        if ptype == "agent_message":
            text = self._msg_text(payload)
            return Event(ST_ANSWER, "回答", text[:24], self.agent,
                         expires=now) if text else None
        if ptype in ("task_started", "turn_started"):
            return Event(ST_THINK, "思考", "正在处理任务", self.agent,
                         expires=now)
        if ptype in ("task_complete", "turn_complete"):
            return Event(ST_DONE, "已完成", "本轮任务结束", self.agent,
                         ttl=3.0, expires=now + 3.0)
        if ptype == "error":
            text = self._msg_text(payload) or "执行出错"
            return Event(ST_ERR, "异常", text[:24], self.agent,
                         ttl=5.0, expires=now + 5.0)

        # ---- input_item: 用户提示 ----
        if mtype == "input_item":
            text = self._content_text(payload)
            if text:
                return Event(ST_THINK, "思考", text[:24], self.agent,
                             expires=now)
        return None

    # ---- 字段提取助手 ----
    @staticmethod
    def _parse_args(raw) -> dict:
        """function_call.arguments 是 JSON 字符串, 需二次解码"""
        if isinstance(raw, dict):
            return raw
        if isinstance(raw, str):
            try:
                v = json.loads(raw)
                return v if isinstance(v, dict) else {}
            except (ValueError, TypeError):
                return {}
        return {}

    @staticmethod
    def _arg_summary(args: dict) -> str:
        for key in ("command", "cmd", "shell_command", "file_path",
                    "path", "query", "pattern"):
            v = args.get(key)
            if isinstance(v, str) and v.strip():
                return v.strip().replace("\n", " ")[:24]
        return ""

    @staticmethod
    def _content_text(payload: dict) -> str:
        """content 可能是字符串 / [{type:output_text,text}] / 旧版 message"""
        content = payload.get("content")
        if isinstance(content, str):
            return content.strip()
        if isinstance(content, list):
            for b in reversed(content):
                if isinstance(b, dict):
                    t = b.get("text")
                    if isinstance(t, str) and t.strip():
                        return t.strip()
                elif isinstance(b, str) and b.strip():
                    return b.strip()
        msg = payload.get("message")
        if isinstance(msg, str) and msg.strip():
            return msg.strip()
        if isinstance(msg, dict):
            return CodexLogSource._content_text(msg)
        return ""

    @staticmethod
    def _msg_text(payload: dict) -> str:
        for key in ("message", "text", "msg"):
            v = payload.get(key)
            if isinstance(v, str) and v.strip():
                return v.strip()
        return ""


# ================================================================ 通用 JSONL
# 常见状态值 -> 状态码 (中英文; 用户可用 config 的 state_map 覆盖/扩充)
DEFAULT_STATE_MAP = {
    # 英文
    "idle": ST_IDLE, "ready": 1, "thinking": ST_THINK, "thought": ST_THINK,
    "reasoning": ST_THINK, "answer": ST_ANSWER, "answering": ST_ANSWER,
    "reply": ST_ANSWER, "responding": ST_ANSWER, "exec": ST_EXEC,
    "executing": ST_EXEC, "tool_use": ST_EXEC, "tool": ST_EXEC,
    "running": ST_EXEC, "command": ST_EXEC, "waiting_input": 5,
    "ask": 5, "question": 5, "confirm": 6, "waiting_confirm": 6,
    "approval": 6, "done": ST_DONE, "complete": ST_DONE,
    "completed": ST_DONE, "finished": ST_DONE, "success": ST_DONE,
    "error": ST_ERR, "failed": ST_ERR, "fail": ST_ERR,
    # 中文
    "空闲": ST_IDLE, "准备": 1, "就绪": 1, "思考": ST_THINK, "回答": ST_ANSWER,
    "执行": ST_EXEC, "运行": ST_EXEC, "等待回答": 5, "等待确认": 6,
    "已完成": ST_DONE, "完成": ST_DONE, "异常": ST_ERR, "错误": ST_ERR,
    "失败": ST_ERR,
}


class GenericJsonlSource(JsonlTailSource):
    """
    通用 JSONL 适配器: 不认识具体 Agent, 但认识常见状态字段。

    每行 JSON 中按顺序查找状态键: st / state / status / phase / event,
    值经 state_map 映射到 0..8 (直接写 0..8 的数字也行);
    标题取 title/summary, 副标题取 sub/detail/message/text/content。
    自研 Agent 只要往 JSONL 追加 {"state":"thinking","message":"..."} 即可上屏。
    """

    STATE_KEYS = ("st", "state", "status", "phase", "event")
    TITLE_KEYS = ("title", "summary")
    SUB_KEYS = ("sub", "detail", "message", "text", "content", "desc")

    def __init__(self, dirs: List[str], agent: str = "HY3",
                 state_map: Optional[dict] = None):
        super().__init__(dirs, agent)
        self.state_map = dict(DEFAULT_STATE_MAP)
        if state_map:
            for k, v in state_map.items():
                try:
                    self.state_map[str(k).strip().lower()] = int(v)
                except (TypeError, ValueError):
                    pass

    def _parse_line(self, obj: dict, now: float) -> Optional[Event]:
        if not isinstance(obj, dict):
            return None
        st = None
        for key in self.STATE_KEYS:
            v = obj.get(key)
            if v is None:
                continue
            mapped = self.state_map.get(str(v).strip().lower())
            if mapped is None and isinstance(v, int) and 0 <= v < 9:
                mapped = v          # 直接写状态码
            if mapped is not None:
                st = mapped
                break
        if st is None:
            return None
        title = ""
        for key in self.TITLE_KEYS:
            v = obj.get(key)
            if isinstance(v, str) and v.strip():
                title = v.strip()[:11]
                break
        sub = ""
        for key in self.SUB_KEYS:
            v = obj.get(key)
            if isinstance(v, str) and v.strip():
                sub = v.strip().replace("\n", " ")[:47]
                break
        pg = obj.get("pg", obj.get("progress", -1))
        if not isinstance(pg, int) or not -1 <= pg <= 255:
            pg = -1
        return Event(st, title or ST_NAMES[st], sub, self.agent,
                     pg=pg, expires=now)


# ================================================================ 进程监控
def list_processes() -> List[str]:
    """所有进程的 "名字 命令行" 小写串 (跨平台, 尽力而为)"""
    if psutil is not None:
        out = []
        for p in psutil.process_iter(["name", "cmdline"]):
            try:
                name = p.info.get("name") or ""
                cmd = " ".join(p.info.get("cmdline") or [])
                out.append(f"{name} {cmd}".lower())
            except Exception:
                continue
        return out
    # Linux: /proc
    if os.path.isdir("/proc"):
        out = []
        for pid in os.listdir("/proc"):
            if not pid.isdigit():
                continue
            try:
                with open(f"/proc/{pid}/cmdline", "rb") as f:
                    cmd = f.read().replace(b"\x00", b" ").decode("utf-8", "replace")
                with open(f"/proc/{pid}/comm") as f:
                    comm = f.read().strip()
                out.append(f"{comm} {cmd}".lower())
            except OSError:
                continue
        return out
    # macOS/其他: ps
    try:
        r = subprocess.run(["ps", "-Ao", "args="], capture_output=True,
                           text=True, timeout=3)
        return [l.lower() for l in r.stdout.splitlines()]
    except Exception:
        return []


class ProcessSource:
    """
    进程监控适配器: 适配豆包 CLI / DSH 等不吐结构化日志的工具。

    配置 specs: [{"match": ["doubao"], "title": "执行命令"}, ...]
    进程出现 -> 执行中 (pg=255); 进程退出 -> 已完成 (ttl=3s)
    """

    def __init__(self, specs: List[dict], agent: str = "HY3"):
        self.specs = [s for s in specs if s.get("match")]
        self.agent = agent
        self._since: dict = {}          # spec index -> 首次发现时间

    def poll(self, now: Optional[float] = None) -> Optional[Event]:
        now = now or time.time()
        if not self.specs:
            return None
        procs = list_processes()
        for i, spec in enumerate(self.specs):
            pats = [str(m).lower() for m in spec.get("match", [])]
            hit = None
            for p in procs:
                if "wb_bridge.py" in p:      # 排除 Bridge 自身
                    continue
                if any(pat and pat in p for pat in pats):
                    hit = p
                    break
            if hit is not None:
                if i not in self._since:
                    self._since[i] = now
                    return Event(ST_EXEC,
                                 str(spec.get("title", "执行命令")),
                                 str(spec.get("sub", hit.strip()))[:47],
                                 self.agent, pg=255, expires=now)
            elif i in self._since:
                dur = int(now - self._since.pop(i))
                title = str(spec.get("title", "执行"))
                return Event(ST_DONE, "任务完成", f"{title}结束 ({dur}s)",
                             self.agent, ttl=3.0, expires=now + 3.0)
        return None
