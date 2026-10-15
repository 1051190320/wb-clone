# wb-clone · 桌面 Agent 状态屏（开源复刻）

复刻一款「猫耳桌面状态屏」：**ESP32-C3 SuperMini + 1.28" GC9A01 圆屏（240×240）小屏 + 电脑端 Bridge 程序**，把 AI Agent 的实时状态（思考/执行/等待确认/已完成…）推到桌面上，抬眼即见。屏幕 **LVGL v9 渲染、240×240 圆屏**，无线（WiFi/MQTT）与 USB 双通道，**兼容任意 AI**。备选硬件：2.8 寸 CYD 黄板 320×240 横屏（同一份固件，换 env 即用）。

```
                 ┌──────────────────────────────────────────────┐
   无线 (MQTT)    │              电脑端 Bridge (Python)          │
  ┌──────────────┤  ┌──────────┐ ┌──────────┐ ┌──────────────┐  │
  │  猫耳小屏    │◄─┤ Claude日志│ │ Codex日志│ │ 通用JSONL    │  │
  │  GC9A01圆屏  │◄─┌──────────┐ ┌──────────┐ ┌──────────────┐  │
  │  ESP32-C3    │  │ 进程监控 │ │ HTTP推送 │ │ 演示/系统监控│  │
  │  WiFi/MQTT   │  └──────────┘ └──────────┘ └──────────────┘  │
  │  状态/日志   │◄─ (USB 串口 CDC, 插线即用, 向后兼容)          │
  │  进度环/统计网格/系统栏 │                                            │
  └──────────────┘  └──────────────────────────────────────────────┘
```

## 从原版产品学到的功能设计

拆解原版（WorkBuddy Bridge）后提炼的核心功能，本复刻全部实现：

| 功能 | 原版 | 本复刻 |
|---|---|---|
| 八类核心状态 | 准备/思考/回答/执行/等待回答/等待确认/已完成/异常 | ✅ 同一套状态机，屏幕/协议/Bridge 三端一致 |
| 三步上手 | 连 USB-C → 启动 Bridge → 抬眼即见 | ✅ 自动发现串口，插线即用 |
| 任务时间线 | 屏幕显示最近事件列表 | ✅ 环形缓冲 4 条，带状态色 |
| UI 框架 | 竖屏点阵绘制 | ✅ **增强**：LVGL v9 对象化渲染，GC9A01 圆屏 240×240（进度环 + 统计网格 2 列 × 3 行）；备选 CYD 320×240 横屏同一份代码 |
| 统计网格 | 剩余积分/本轮消耗/今日费用/本轮用时/当前任务模型/任务名称 | ✅ 协议 v1.1 六格网格，HTTP/外部 AI 推送即显 |
| 系统监控 | CPU/内存/存储/上下行网速/运行时长 | ✅ Bridge 随帧下发 |
| 边界诚实 | 不承诺精确百分比/ETA；不能脱离电脑 | ✅ 同样设计（pg=-1 无进度；断开显示「等待连接」） |
| 无触摸/无按键/无电池/无声音 | 四无纯显示终端 | ✅ 同设计 |
| 仅 Windows / 仅自家 Agent | — | ✅ **增强**：跨平台 + 兼容任意 AI（见下） |

## 兼容任意 AI（核心增强）

Bridge 不绑定任何 Agent，提供 4 种接入方式，按优先级裁决（高者优先）：

| 方式 | 适用 | 开关键 |
|---|---|---|
| **HTTP / MQTT 推送** | 任意程序一行命令推送（`wb_notify.py` / `curl`） | `POST /event` 或 `wb_notify --mqtt` |
| **Claude Code 日志** | 自动 tail `~/.claude/projects/**/*.jsonl` 推导状态 | 默认开（`--no-claude` 关） |
| **Codex CLI 日志** | 自动 tail `~/.codex/sessions/**/rollout-*.jsonl` | `--codex` |
| **通用 JSONL** | 自研 Agent 往 JSONL 写 `{"state":"thinking"}` | `--jsonl <dir>` |
| **进程监控** | 豆包 / DSH 等不吐结构化日志的 CLI：进程在跑=执行中 | `--watch doubao,dsh` |

详细接入示例见 [`bridge/examples/ai_hooks/README.md`](bridge/examples/ai_hooks/README.md)。

## 目录结构

```
wb-clone/
├── firmware/               # 显示端 (默认 ESP32-C3 + GC9A01 圆屏 / 备选 CYD)
│   ├── boards/             # 硬件配置分区: 每个板型独立目录, -I 注入
│   │   ├── esp32c3_gc9a01/ #   默认: tft_setup.h (TFT_eSPI 外部配置) + board.h (边界宏)
│   │   └── cyd_2_8/        #   备选: CYD 横屏同款两份
│   ├── include/
│   │   └── lv_conf.h       # LVGL v9 配置 (platformio.ini 已注入; 手动编译需复制到 lvgl 源码)
│   ├── wb_display/
│   │   ├── wb_display.ino  # 主程序: 双通道/解析/LVGL 驱动对接/主循环
│   │   ├── ui.h            # UI 状态层: 配色/状态机/字库查找 (尺寸来自 board.h)
│   │   ├── lv_font.h       # 点阵字体接入 LVGL (get_glyph_dsc/bitmap 回调, 零转换)
│   │   ├── ui_lvgl.h       # LVGL 渲染层: 对象树 + 增量更新 (圆屏中心基准布局)
│   │   ├── cn_font.h       # 中文字库 (217 字, 已生成)
│   │   ├── wb_net.h        # WiFi + 手写 MiniMQTT + AP 配网 portal
│   │   └── transport.h     # 通道抽象 (串口/MQTT)
│   ├── scripts/
│   │   └── install_tft_setup.sh  # Arduino IDE 用户: 把 boards/<板型>/tft_setup.h 装进 TFT_eSPI 库
│   └── platformio.ini      # 默认 env: esp32c3_gc9a01 (esp32-c3), 备选 env: cyd_2_8
├── bridge/                 # 电脑端 (Python, 跨平台)
│   ├── wb_bridge.py        # 主程序: 多通道传输 + 状态裁决
│   ├── sources.py          # 系统监控 / Claude 日志推导 / 演示
│   ├── adapters.py         # 多 AI 适配器: Codex / 通用JSONL / 进程监控
│   ├── mqtt_link.py        # 裸 MQTT 客户端 + 链路 (防回环)
│   ├── mini_broker.py      # 零依赖内置 MQTT Broker (开箱即用)
│   ├── wb_notify.py        # 一行接入工具 (HTTP/MQTT 推送)
│   ├── config.json
│   └── examples/           # curl / Claude hooks / ai_hooks 接入示例
├── simulator/index.html    # 网页模拟器 (无硬件预览效果, 竖屏复刻原版布局)
├── tools/                  # 字库生成器 + 三套测试 + LVGL mock (约 341 项断言)
├── docs/
│   ├── protocol.md         # 通信协议 v1.1
│   ├── bom.md              # 物料清单 + 3D 外壳尺寸
│   └── wifi-setup.md       # 无线配网指南
└── README.md
```

## 三条路径

### 1. 已有硬件（ESP32-C3 SuperMini + GC9A01 圆屏，≈ ¥25–40）

```bash
# 烧录固件 (PlatformIO, 默认 env: esp32c3_gc9a01)
cd firmware && pio run -e esp32c3_gc9a01 -t upload && pio device monitor

# 启动 Bridge (USB 串口 CDC, 自动发现 CH340/CP210x)
pip install pyserial psutil
python3 bridge/wb_bridge.py

# 或走无线 (内置 broker 开箱即用, 屏幕需先 AP 配网连同一 WiFi)
python3 bridge/wb_bridge.py --transport both --broker builtin
```

**换 CYD 备选板**：`pio run -e cyd_2_8 -t upload`（两份配置互不干扰，各自在 `firmware/boards/<板型>/` 下维护）。

接线、硬件分区机制、烧录细节见 [`firmware/README.md`](firmware/README.md)；物料与外壳见 [`docs/bom.md`](docs/bom.md)。

### 2. 没有硬件

```bash
python3 -m http.server -d simulator 8080   # 打开 http://localhost:8080
```

网页模拟器完整复刻设备外观与屏幕 UI，支持手动切换 9 种状态、自动演示、以及「连接真实 Bridge」模式（轮询 `127.0.0.1:8788/state`）。

### 3. 接入你自己的 Agent

```bash
# 一行命令 (HTTP)
curl -X POST http://127.0.0.1:8788/event -H 'Content-Type: application/json' \
  -d '{"st":4,"title":"执行命令","sub":"正在跑构建","pg":255}'

# 或用自带工具 (支持状态名/中文)
python3 bridge/wb_notify.py --state thinking --sub "分析需求"

# AI Agent 宿主环境可安装 wb-notify skill (~/.codebuddy/skills/wb-notify/):
# 零依赖等价实现, 说一句"把 XX 推到屏幕"即可推送

# 自研 Agent: 往 JSONL 追加一行即可被 --jsonl 识别
echo '{"state":"executing","title":"跑测试","detail":"pytest"}' >> /tmp/myagent.log
python3 bridge/wb_bridge.py --jsonl /tmp
```

Claude Code 用户：把 `bridge/examples/claude_settings_hooks.json` 并入 `~/.claude/settings.json` 即可自动联动。协议细节见 [`docs/protocol.md`](docs/protocol.md)。

## 测试状态

| 测试 | 覆盖 | 结果 |
|---|---|---|
| `tools/mock/test_main.cpp` | 固件协议解析（含 `\uXXXX`/坏行/截断/粘包）+ LVGL UI 层（对象/布局/颜色/进度环/flush 回调/字体字节级）+ lv_conf.h 一致性，**两板型各 109 项** | ✅ 109/109 × 2 |
| `tools/mock/test_net.cpp` | 无线模块（MQTT 字节级 / 配网 portal / NVS / 退避重连） | ✅ 33/33 |
| `tools/test_bridge.py` | 日志推导 / HTTP / 帧组装 / uptime / 适配器 | ✅ 32/32 |
| `tools/test_mqtt.py` | 内置 broker ↔ 裸客户端互通 / 回环防护 / Bridge 端到端 | ✅ 58/58 |
| 字库字形 | 217 汉字点阵逐一渲染验证 | ✅ |

```bash
# 复现全部测试 (LVGL 用 -Itools/mock/lvgl 注入记录式桩, -I 选板型)
# esp32c3_gc9a01 (默认):
g++ -std=c++17 -Wall -Itools/mock -Itools/mock/lvgl -Ifirmware/wb_display \
    -Ifirmware/boards/esp32c3_gc9a01 \
    -x c++ tools/mock/test_main.cpp tools/mock/mock_env.cpp -o /tmp/wbtest && /tmp/wbtest
# cyd_2_8 (备选): 把上面 -Ifirmware/boards/esp32c3_gc9a01 换成 -Ifirmware/boards/cyd_2_8
g++ -std=c++17 -Wall -Itools/mock -Itools/mock/lvgl -Ifirmware/wb_display \
    -x c++ tools/mock/test_net.cpp  tools/mock/mock_env.cpp -o /tmp/nettest && /tmp/nettest
python3 tools/test_bridge.py && python3 tools/test_mqtt.py
```

## 自定义

- **改屏幕文字/布局**：`ui_lvgl.h` 里的 LVGL 对象树（标签/网格坐标均为具名常量，圆屏以屏幕中心为基准）；缺字时运行 `python3 tools/gen_font.py` 重新生成（字符集在脚本顶部 `CN_CHARS`，字体接入见 `lv_font.h`）
- **改引脚/换板型**：每套硬件一个独立目录 `firmware/boards/<板型>/`——`tft_setup.h`（TFT_eSPI 引脚与驱动）+ `board.h`（屏幕尺寸/旋转/背光/板名）；PlatformIO 每个 env 用一行 `-I boards/<板型>` 同时选中库配置与固件边界，互不干扰
- **改心跳/Agent 名/数据源**：`bridge/config.json` 或 CLI 参数
- **加新 AI 适配器**：在 `adapters.py` 仿照 `JsonlTailSource` 写一个解析器，在 `wb_bridge.py` 的 `resolve_event()` 里注册

## 致谢

- 字体：[Noto Sans CJK SC](https://github.com/notofonts/noto-cjk)（SIL OFL 1.1）
- 显示库：[TFT_eSPI](https://github.com/Bodmer/TFT_eSPI)（FreeBSD）+ [LVGL](https://lvgl.io)（MIT）
- 硬件参考：[Sunton ESP32-2432S028R（Cheap Yellow Display）](https://randomnerdtutorials.com/esp32-cheap-yellow-display-cyd-pinout/)
