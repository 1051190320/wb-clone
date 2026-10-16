# wb-clone 显示端固件

**默认硬件：ESP32-C3 SuperMini + 1.28" GC9A01 圆屏（240×240）**，**LVGL v9 对象化 UI**，**双通道**接收电脑端 Bridge 的 JSON 状态帧并渲染：

- **USB**：C3 原生 USB-Serial-JTAG（插 USB-C 即出现串口）
- **WiFi**：MQTT 订阅（`wb/frame`），首次使用 AP 配网，之后全无线

备选硬件：**2.8" CYD 黄板**（ESP32-WROOM-32 + ILI9341 320×240 横屏），同一套固件，`-e cyd_2_8` 切换。

> 本工程不使用触摸 / SD。GC9A01 圆屏无触摸；CYD 上的 XPT2046 无需初始化。

## 硬件接线（ESP32-C3 SuperMini ↔ GC9A01 圆屏）

| GC9A01 引脚 | ESP32-C3 | 说明 |
|---|---|---|
| VCC | 3V3 | 屏幕供电（务必 3.3V） |
| GND | GND | 共地 |
| SCL (SCLK) | GPIO 4 | SPI 时钟 |
| SDA (MOSI) | GPIO 3 | SPI 数据 |
| CS | GPIO 1 | 片选 |
| DC (RS) | GPIO 10 | 数据/命令 |
| RST | GPIO 0 | 复位（仅初始化时脉冲，不影响启动） |
| BL | 未接 | 该批次模块背光直连 VCC；若模块引出 BL 且偏暗，可接 GPIO 7 并把 `WB_BL_PIN`/`TFT_BL` 改为 7（C3 的 strapping 脚 GPIO2/8/9 不建议用作 BL） |

> 花屏/闪烁：`tft_setup.h` 里把 `SPI_FREQUENCY` 从 40000000 降到 27000000；方向不对：改 `board.h` 的 `WB_ROT`（0~3）。

CYD 2.8" 是二合一开发板，屏幕已焊在板上，引脚固定（无需接线）：`MOSI=13 SCLK=14 CS=15 DC=2 RST=-1 BL=21`。

## 硬件配置按板型分区

每套硬件一个目录，两份配置文件放在一起：

```
firmware/
├── boards/
│   ├── esp32c3_gc9a01/        # 默认硬件
│   │   ├── tft_setup.h        # TFT_eSPI 配置: 驱动 + SPI 引脚
│   │   └── board.h            # 固件侧: SCR_W/SCR_H/旋转/背光/板型名
│   └── cyd_2_8/
│       ├── tft_setup.h        # ILI9341 + CYD 引脚
│       └── board.h            # 320x240 / WB_ROT=1 / BL=21
├── platformio.ini             # 每个 env 用 -I boards/<板型> 选中上面两份
└── scripts/install_tft_setup.sh   # Arduino IDE 用户: 把 tft_setup.h 装进库目录
```

选用方式：**一个 `-I` 开关**同时决定两份配置，无需 `#ifdef` 分支，也**不用改动/复制 TFT_eSPI 库文件**：

```ini
[env:esp32c3_gc9a01]
build_flags =
  -I boards/esp32c3_gc9a01     ; ① TFT_eSPI 内的 `#if __has_include(<tft_setup.h>)` 自动采用它
                               ; ② 固件的 `#include "board.h"` 也命中同目录
```

新增一块板：复制 `boards/esp32c3_gc9a01/` 改名 → 改两份配置 → 在 `platformio.ini` 加一段 env。

## 依赖

- **TFT_eSPI**（v2.5.x）：GC9A01 / ILI9341 SPI 驱动；官方已支持 ESP32-C3（库内有专用 `Processors/TFT_eSPI_ESP32_C3.c`）
- **LVGL v9**（≥9.3）：UI 框架；工程自带精简配置 `include/lv_conf.h`（关掉不用的 widget，SW 渲染）
- WiFi/MQTT/配网仅用 ESP32 核心自带库（`WiFi`/`WiFiClient`/`WebServer`/`DNSServer`/`Preferences`），MQTT 客户端为手写实现（`wb_net.h`，约 250 行，QoS0 子集）
- 中文字库已生成好：`cn_font.h`（217 个 16×16 汉字 + ASCII 8×16，来自 Noto Sans CJK SC）
- JSON 解析为手写零依赖实现（`wb_display.ino` 内 `JParser`），无需 ArduinoJson

## 代码结构

| 文件 | 职责 |
|---|---|
| `wb_display.ino` | 主程序：JSON 帧解析（`JParser`）、双通道调度、LVGL 显示驱动（flush_cb→TFT_eSPI）、主循环 |
| `ui.h` | **状态层**：状态枚举、全局 `WbState`（帧解析后写这里）、配色（屏幕尺寸来自 `board.h`） |
| `ui_lvgl.h` | **渲染层**：LVGL 对象树与增量更新（`uiUpdateFromState`），圆屏布局 |
| `lv_font.h` | 点阵字库→LVGL 字体：自定义 `get_glyph_dsc`/`get_glyph_bitmap` 回调，位图零转换直出 |
| `transport.h` | 通道抽象（USB 串口） |
| `wb_net.h` | 无线：手写 MQTT 客户端 + NetTransport + AP 配网 Portal + NVS 配置 |
| `wb_ota.h` | OTA 无线更新：Web 上传（`/update`）+ ArduinoOTA 双通道，密码 `wbclone` |
| `cn_font.h` | 16×16 中文点阵 + 8×16 ASCII 点阵（`tools/gen_font.py` 生成） |

## 烧录方式（三选一）

### 1. PlatformIO（推荐）
```bash
pio run -e esp32c3_gc9a01 -t upload   # 默认硬件 (圆屏)
pio run -e cyd_2_8 -t upload          # 备选硬件 (CYD 横屏)
pio device monitor                    # 看串口输出 (115200)
```
首次编译会自动下载工具链并拉取 TFT_eSPI + lvgl；`lv_conf.h` 在 `firmware/include/`，由 `-I include -DLV_CONF_INCLUDE_SIMPLE` 生效。

### 2. Arduino IDE
1. 安装 ESP32 开发板包（≥ 2.0.x）；C3 板需要 `USB CDC On Boot: Enabled`
2. 库管理器安装 `TFT_eSPI` 和 `lvgl`（v9.x）
3. 安装硬件配置：`firmware/scripts/install_tft_setup.sh esp32c3_gc9a01`（或 `cyd_2_8`）
4. 把 `firmware/include/lv_conf.h` 复制到 `<库目录>/lvgl/lv_conf.h`
5. 把 `firmware/boards/<板型>/board.h` 所在目录加进 IDE 的 include 路径（最简单：和 `wb_display.ino` 同目录放一份副本）
6. 打开 `wb_display/wb_display.ino`，开发板选 `ESP32C3 Dev Module` / `ESP32 Dev Module`，上传

### 3. arduino-cli
```bash
arduino-cli core install esp32:esp32
arduino-cli lib install "TFT_eSPI@2.5.43" "lvgl@9.3.0"
firmware/scripts/install_tft_setup.sh esp32c3_gc9a01 ~/Arduino/libraries/TFT_eSPI
cp firmware/include/lv_conf.h ~/Arduino/libraries/lvgl/lv_conf.h
arduino-cli compile --fqbn esp32:esp32:esp32c3 --build-property "compiler.cpp.extra_flags=-Ifirmware/boards/esp32c3_gc9a01" --upload firmware/wb_display
```

## 无硬件联调（推荐先跑通）

不需要开发板就能验证整条链路（`tools/mock/lvgl/lvgl.h` 是记录式 LVGL mock 桩，可断言对象/文本/颜色/环值）：

```bash
# 1. 协议解析 + 圆屏 UI 在环测试 (109 项)
g++ -std=c++17 -Wall -Itools/mock -Itools/mock/lvgl -Ifirmware/wb_display \
    -Ifirmware/boards/esp32c3_gc9a01 -Ifirmware/include \
    -x c++ tools/mock/test_main.cpp tools/mock/mock_env.cpp -o /tmp/wbtest
/tmp/wbtest

# 1b. 换成 CYD 板型再跑一遍: 同一份代码, 同一套断言 (布局自适应 320x240)
g++ -std=c++17 -Wall -Itools/mock -Itools/mock/lvgl -Ifirmware/wb_display \
    -Ifirmware/boards/cyd_2_8 -Ifirmware/include \
    -x c++ tools/mock/test_main.cpp tools/mock/mock_env.cpp -o /tmp/wbcyd
/tmp/wbcyd

# 2. 无线模块在环测试 (33 项: MQTT 协议字节级 / 配网 portal / NVS / 退避重连)
g++ -std=c++17 -Wall -Itools/mock -Itools/mock/lvgl -Ifirmware/wb_display \
    -Ifirmware/boards/esp32c3_gc9a01 \
    -x c++ tools/mock/test_net.cpp tools/mock/mock_env.cpp -o /tmp/nettest
/tmp/nettest

# 3. 真实库编译校验 (首次需下载 espressif32 平台 + 工具链, 约 15 分钟,
#    国内网络建议给 pio 挂代理或先把 ~/.platformio 缓存拷贝过来)
cd firmware && pio run -e esp32c3_gc9a01 -t clean && pio run -e esp32c3_gc9a01
# CYD 同理: pio run -e cyd_2_8

# 4. 虚拟串口对 + Bridge 端到端
socat -d -d pty,raw,echo=0 pty,raw,echo=0
python3 bridge/wb_bridge.py --port /dev/pts/3 --dry-run

# 5. 网页模拟器看效果（无需任何硬件）
python3 -m http.server -d simulator 8080   # 浏览器打开 http://localhost:8080
```

> **编译状态说明**：本仓库在离线沙箱内完成开发，沙箱到 GitHub / PlatformIO registry 被限速至 ~7KB/s，`pio run` 下载 espressif32 平台 + 工具链（~400MB）不可行，因此**未在沙箱内跑通真实编译**。替代保障：全部 LVGL v9.3 / TFT_eSPI API 已对照真实头文件逐一核实（见下），mock 测试新增 `lv_conf.h` 一致性断言（曾以此抓到 `LV_USE_ARC=0` 会导致真实构建编不过的问题）。首次在有网环境执行 `pio run -e esp32c3_gc9a01` 即可完成最终编译校验。

### 已核实的 API 兼容性（对照 LVGL v9.3.0 / TFT_eSPI 2.5.43 真实头文件）

| 类别 | 结论 |
|---|---|
| LVGL 进度环 | `lv_arc_create/set_value/set_bg_angles/set_rotation` 签名一致；`bg_angles` 参数为 `lv_value_precise_t`（整数实参合法） |
| LVGL 样式设置 | `arc_color/arc_width/bg_opa/text_align/text_color/text_font/bg_color/border_width/radius` 均存在；`pad_all`/`transform_scale` 为 `lv_obj_style.h` 的 `static inline` 便捷包装（v9.3 仍保留） |
| LVGL 显示驱动 | `lv_display_create/set_buffers/set_flush_cb/flush_ready` 与 flush 回调 typedef `void(lv_display_t*, const lv_area_t*, uint8_t*)` 逐字匹配；`LV_PART_INDICATOR` 为枚举 `0x020000`（代码用符号名，无碍） |
| LVGL 字体 | `lv_font_t` / `lv_font_glyph_dsc_t` 字段逐一匹配（`get_glyph_dsc`/`get_glyph_bitmap` 回调签名、`gid.index`、`req_raw_bitmap`、A1 格式常量） |
| TFT_eSPI | `#if __has_include(<tft_setup.h>)` 外部配置机制确认（源码 59-61 行）；`init/setRotation/fillScreen/startWrite/endWrite/setAddrWindow/pushColors` 签名一致；官方 `Processors/TFT_eSPI_ESP32_C3.c` 存在 |
| lv_conf.h | `LV_USE_ARC=1`、`LV_USE_LABEL=1`、`LV_COLOR_DEPTH=16`（与 flush 的 uint16 推送一致）；`LV_USE_BAR=0`（圆屏已不用 bar） |

## 无线配网（首次使用）

1. 烧录后屏幕显示「配网 / 手机连接 WiFi 热点: **wb-screen-XXXX**」
2. 手机连上该热点，浏览器打开 `192.168.4.1`（自动弹出配置页）
3. 填入家庭 WiFi 密码；Broker 地址填运行 `python3 bridge/wb_bridge.py --broker builtin` 的电脑 IP，Topic 默认 `wb/frame`
4. 保存后设备重启，自动连接 WiFi 并订阅，屏幕右上角显示 `WiFi` + 时钟

> 配置存 NVS，重启不丢失。插 USB 线随时可用串口通道。详细排障见 [`docs/wifi-setup.md`](docs/wifi-setup.md)。

## OTA 无线更新（WiFi 推送固件）

首次用 USB 烧录后，后续固件更新全部走 WiFi，不用插线（`wb_ota.h`，两条通道，密码统一 `wbclone`）：

| 通道 | 用法 |
|---|---|
| Web OTA（推荐） | 连上 WiFi 后浏览器打开 `http://<设备IP>/update`，密码 `wbclone`，上传 app 镜像 `.bin`，传完自动重启 |
| ArduinoOTA | Arduino IDE 网络端口 `wb-screen-XXXX.local` 或 PlatformIO `upload_protocol = espota`，密码 `wbclone` |

- 设备 IP：串口日志打印、路由器 DHCP 列表、或 mDNS `wb-screen-XXXX.local`（XXXX = MAC 后四位）
- 更新写入**非活动 OTA 分区**（Arduino 默认 OTA 分区表），写坏不影响当前固件；成功后自动切分区重启
- 根路径 `/` 为状态页，`/api/status` 返回 JSON（IP/信号/通道/时长/堆）
- 前提：设备已连上 WiFi（AP 配网模式下仅 ArduinoOTA 可用，80 端口归配网 portal）
- 注意：app 分区 1.25MB，后续构建需控制在 1.25MB 以内才能 OTA

## 屏幕 UI 布局（圆屏 240×240，LVGL）

元素以屏幕中心为基准排布，天然落在可视圆内（安全半径经圆幂计算校核）；CYD 320×240 用同一份代码自动居中。

```
y= 18..34    ●(连接)   19:27   USB/WiFi
y= 40..60    状态大标题 (1.25x, 文字颜色 = 状态色)
y= 60..76    副标题
y= 78..94    agent · 本轮用时
   外环      进度环: 270° 表盘自顶部顺时针 (pg>=0 显示; pg=255 为往复动画)
y= 92..173   统计网格 2 列 x 3 行 (标签在上, 数值在下)
             或 任务日志 4 行 (无统计数据时)
y=178..194   系统行: CPU xx% 内存 xx%
y=196..212   网速行: ↓8K/s ↑6K/s (无网速时显示 存储 xx%)
```

9 种状态与配色：`idle 灰 / ready 蓝 / think 青 / answer 蓝 / exec 青 / wait_ans 青 / wait_ok 金 / done 绿 / err 红`

统计网格 6 格（协议 v1.1，任一字段非空即显示网格并隐藏日志行）：`剩余积分 / 本轮消耗 / 今日费用 / 本轮用时 / 当前任务模型 / 任务名称`。

> 圆屏空间有限：开机时长 `up` 仍按协议解析但不上屏；网速单位自动压缩为 `K/s`。网页模拟器（`simulator/index.html`）还原的是**原版竖屏产品外观**，布局不同但内容一致。

## 协议

一行一个 JSON（UTF-8，`\n` 结尾），字段与默认值见 `docs/protocol.md`。缺省字段按快照语义重置；`\uXXXX` 转义自动解码。
