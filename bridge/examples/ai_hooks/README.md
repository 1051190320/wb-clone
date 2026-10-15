# AI 接入示例 (ai_hooks)

本目录给出几种「让任意 AI 的状态上屏」的现成接入方式。
核心思路：Bridge 采集状态，屏幕只是显示终端，所以**任何 AI 只要把状态告诉 Bridge 即可**。

---

## 方式一：HTTP 一行推送（最通用，推荐）

任意程序/脚本/CI 用 `wb_notify.py` 或 `curl` 把状态 POST 给 Bridge 的 `/event`：

```bash
# 用自带工具 (需 requests: pip install requests)
python3 ../wb_notify.py --st 4 --title 执行命令 --sub "pip install reportlab" --ttl 5

# 用状态名
python3 ../wb_notify.py --state thinking --sub "分析需求"

# 用 curl (不需要任何依赖)
curl -s -X POST http://127.0.0.1:8788/event \
  -H 'Content-Type: application/json' \
  -d '{"st":3,"title":"回答","sub":"已生成报告","ttl":4}'
```

`st` 取值（也可用中文/英文状态名代替数字）：
`0 空闲 1 准备 2 思考 3 回答 4 执行 5 等待回答 6 等待确认 7 已完成 8 异常`

在 Claude Code / Codex 的 hook 里调用它，就能让它们「自动」上屏，见 `claude_settings_hooks.json`。

> **AI Agent 用户**：如果宿主环境安装了 `wb-notify` skill（`~/.codebuddy/skills/wb-notify/`），
> 直接说"把 XX 推到屏幕"即可，skill 内的 `scripts/wb_push.py` 与 `wb_notify.py` 等价、
> 零第三方依赖（HTTP 走 urllib、MQTT 用内联裸客户端），同样支持 v1.1 统计字段。

---

## 方式二：日志自动推导（零改动接入）

Bridge 直接 tail 这些日志，**无需改 AI 代码**：

| AI | 日志位置 | Bridge 开关 |
|----|----------|-------------|
| Claude Code | `~/.claude/projects/**/*.jsonl` | 默认开（`--no-claude` 关） |
| OpenAI Codex CLI | `~/.codex/sessions/**/rollout-*.jsonl` | `--codex` |
| 自研 Agent | 你指定的 JSONL | `--jsonl /path/to/dir` |

自研 Agent 只要往 JSONL 追加一行即可被识别：
```json
{"state":"thinking","message":"分析需求"}
{"st":4,"title":"跑测试","detail":"pytest -q"}
{"state":"done"}
```
状态键按 `st/state/status/phase/event` 顺序查找，值经中英文映射（见 `adapters.py` 的 `DEFAULT_STATE_MAP`），也可写 `0..8` 数字。

---

## 方式三：进程监控（豆包 / DSH 等无日志 CLI）

豆包 CLI、DSH 这类工具不吐结构化日志，但**进程在跑 = 正在干活**：

```bash
# 监控名字含 doubao 或 dsh 的进程
python3 ../wb_bridge.py --watch doubao,dsh --transport both
```
进程出现 → 屏幕显示「执行中（不确定进度）」；进程退出 → 「已完成」。
规则也可写进 `config.json` 的 `adapters.process.specs`。

---

## 方式四：MQTT 直连（无线、跨机）

任意 AI 直接把协议帧 publish 到 broker 的 `wb/frame` topic，
屏幕（WiFi 模式）或 Bridge 都能收到，**回环已做防护**：

```bash
python3 ../wb_notify.py --mqtt --broker builtin --st 5 --title 等待回答 --sub "A 还是 B?"
```

---

## 优先级

屏幕同一时刻只显示一种状态，Bridge 按以下优先级裁决（高者优先）：
`HTTP/MQTT 推送 > Claude 日志 > Codex 日志 > 通用 JSONL > 进程监控 > 演示 > 空闲`
