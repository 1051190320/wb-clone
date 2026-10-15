# wb-clone 电脑端 Bridge

把 Agent 状态送到桌面小屏幕的桥接程序。**跨平台**（Windows / macOS / Linux），原版仅支持 Windows。

## 安装

```bash
pip install pyserial psutil
# 仅 HTTP 推送模式需要 requests:  pip install requests
```

## 快速开始

```bash
# 1. 插上屏幕 (2.8" CYD / 通用 ESP32), 自动找串口 (CH340/CP210x), 全部数据源开启
python3 bridge/wb_bridge.py

# 2. 没有硬件?  dry-run 打印协议帧, 或搭配网页模拟器
python3 bridge/wb_bridge.py --dry-run

# 3. 演示模式 (无数据源时循环展示 9 种状态)
python3 bridge/wb_bridge.py --demo

# 4. 无线模式 (内置 broker 开箱即用, 屏幕需先 AP 配网连同一 WiFi)
python3 bridge/wb_bridge.py --transport both --broker builtin
```

常用参数：

| 参数 | 说明 |
|---|---|
| `--port COM5` / `--port /dev/ttyACM0` | 指定串口（默认 `auto` 自动发现 CH340/CP210x/S3） |
| `--transport serial\|mqtt\|both` | 传输通道（默认 `serial`；`both`=串口+MQTT 双发） |
| `--broker builtin\|host[:port]\|none` | MQTT broker（默认 `builtin` 本进程内置；`none` 关） |
| `--mqtt-port 1883` | 内置 broker 端口 |
| `--agent 名字` | 屏幕上显示的 Agent 名 |
| `--claude-dir ~/.claude/projects` | Claude 日志目录（可多次） |
| `--codex` | 启用 Codex CLI 日志适配器（`~/.codex/sessions`） |
| `--jsonl <dir>` | 通用 JSONL 目录（可多次），自研 Agent 写 `{"state":...}` 即上屏 |
| `--watch 名字1,名字2` | 按进程名监控（可多次），进程在跑=执行中（豆包/DSH 等） |
| `--no-claude` | 关闭日志监控 |
| `--http-port 8788` | HTTP API 端口（`0` 关闭） |
| `--heartbeat 2` | 心跳秒数 |
| `--dry-run` | 不接硬件，打印协议帧 |
| `--once` | 发一帧退出（测试用） |
| `-v` | 调试日志 |

## 状态来源（按优先级）

高者优先：

1. **HTTP / MQTT 推送**（最高）——任意程序一行命令推（`wb_notify.py` / `curl`）
2. **Claude Code 日志推导**——自动 tail `~/.claude/projects/**/*.jsonl`
3. **Codex CLI 日志推导**——自动 tail `~/.codex/sessions/**/rollout-*.jsonl`（`--codex`）
4. **通用 JSONL**——自研 Agent 往 JSONL 写 `{"state":"thinking"}`（`--jsonl`）
5. **进程监控**——豆包 / DSH 等不吐日志的 CLI：进程在跑=执行中（`--watch`）
6. **演示模式** `--demo`——无数据源时循环演示
7. **空闲**（兜底）

### 1) HTTP API（接入任意 Agent）

Bridge 运行时监听 `127.0.0.1:8788`：

```bash
# 推送"正在执行命令"
curl -X POST http://127.0.0.1:8788/event -H 'Content-Type: application/json' \
  -d '{"st":4,"title":"执行命令","sub":"pip install reportlab","pg":255}'

# 或用自带一行工具 (支持状态名/中文, 也可走 --mqtt)
python3 bridge/wb_notify.py --st 4 --title 执行命令 --sub "pip install reportlab"

# 查询当前状态 (返回最近一帧, 网页模拟器也用这个接口)
curl http://127.0.0.1:8788/state
```

状态码：`0 空闲 / 1 准备 / 2 思考 / 3 回答 / 4 执行 / 5 等待回答 / 6 等待确认 / 7 已完成 / 8 异常`

字段：`st`(必填), `title`, `sub`, `agent`, `pg`(-1 无进度 / 0-100 / 255 不确定动画), `ttl`(秒, 0=持续到下个事件), `lines`(覆盖屏幕日志行)

统计网格字段（协议 v1.1，可选，非空即显示屏幕下方 2×3 网格）：`bal`(剩余积分, 如 `"8046.86 积分"`), `spend`(本轮消耗), `cost`(今日费用), `model`(当前任务模型), `task`(任务名称)。`dur`(本轮用时 `M:SS`) 由 Bridge 自动计时，无需推送。

Claude Code 用户可把 `examples/claude_settings_hooks.json` 的内容合并进 `~/.claude/settings.json`，用户提问和回复结束时会自动推送。更多接入方式见 `examples/ai_hooks/README.md`。

### 2) Claude Code 日志推导

自动找到最新会话日志并 tail，规则：

| 日志事件 | 屏幕状态 |
|---|---|
| `content[].tool_use` | 执行（工具名中文化：Bash→执行命令、Read→读取文件…） |
| `content[].text` | 回答 |
| 人类输入（user content 为字符串） | 思考 |
| `stop_reason == "end_turn"` | 已完成（3 秒后回空闲） |
| 坏行 / 非 JSON | 跳过（不影响运行） |

### 3) Codex / 通用 JSONL / 进程监控

- `--codex`：tail OpenAI Codex CLI 的 `rollout-*.jsonl`，识别 `function_call`→执行、`message`→回答/`event_msg`→生命周期。
- `--jsonl <dir>`：识别每行 `st/state/status/phase/event` 字段（中英文或 0..8 数字），自研 Agent 零改动接入。
- `--watch 豆包,dsh`：进程存在→执行中（不确定进度），进程退出→已完成（3 秒）。

### 4) 演示模式

`--demo` 循环展示：准备 → 思考 → 执行 → 回答 → 等待回答 → 等待确认 → 已完成 → 空闲。

## 无硬件联调

```bash
# 终端 1: 虚拟串口对
socat -d -d pty,raw,echo=0 pty,raw,echo=0      # 得到 /dev/pts/N 和 /dev/pts/M

# 终端 2: bridge 连一端, dry-run 不占串口时可同时看帧
python3 bridge/wb_bridge.py --port /dev/pts/N --dry-run

# 或者: 浏览器打开 simulator/index.html, 切到"连接真实 Bridge"模式
# (会自动轮询 http://127.0.0.1:8788/state)
```

## 开机自启

**Windows**（任务计划程序）：
```bat
schtasks /create /tn "wb-bridge" /tr "pythonw C:\wb-clone\bridge\wb_bridge.py" /sc onlogon
```

**Linux**（systemd --user）：
```ini
# ~/.config/systemd/user/wb-bridge.service
[Service]
ExecStart=/usr/bin/python3 %h/wb-clone/bridge/wb_bridge.py
Restart=always
```
```bash
systemctl --user enable --now wb-bridge.service
```

## 自检

```bash
python3 tools/test_bridge.py     # 32 项断言: 日志/HTTP/帧组装/uptime/适配器
python3 tools/test_mqtt.py       # 58 项: 内置 broker ↔ 裸客户端/回环防护/端到端
```
