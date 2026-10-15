# wb-clone 通信协议 v1.1

电脑端 Bridge → 显示端固件，单工（Bridge 发，屏幕收），**一行一个 JSON 帧**，UTF-8 编码，`\n` (0x0A) 结尾。

- 传输：两种通道，**同一套帧格式**
  - **USB 串口**：CH340/CP210x/S3 虚拟串口，115200 8N1（CDC 模式下波特率无实际意义）
  - **WiFi/MQTT**：屏幕订阅 `wb/frame` topic，Bridge 发布；帧带 `_src:"bridge"` 标记，回环已防护
- 心跳：Bridge 每 `heartbeat_sec`（默认 2s）发送一帧，即使状态空闲
- 断开：屏幕 5s 未收到任何有效帧 → 显示「等待连接」界面，收到即恢复
- 语义：**每帧是完整快照**，未下发的字段重置为默认值（`clock` 例外，保留上次值）
- 兼容性：v1.1 新增字段全部**可选**，旧 Bridge/固件互相忽略未知字段，混用不影响

## 帧字段

### 基础字段

| 字段 | 类型 | 默认 | 说明 |
|---|---|---|---|
| `clock` | string | `"--:--"` | 时间 `HH:MM`，由电脑端下发；屏幕本地走时 |
| `st` | int 0..8 | `0` | 状态码，见下表 |
| `title` | string | `""` | 状态大标题，最长 23 字节（截断） |
| `sub` | string | `""` | 副标题，最长 47 字节 |
| `agent` | string | `""` | Agent 标签（如 `HY3`），空则隐藏标签 |
| `cpu` | int 0..100 | `0` | CPU 占用百分比 |
| `mem` | int 0..100 | `0` | 内存占用百分比 |
| `disk` | int 0..100 | `0` | 存储占用百分比（v1.1 新增） |
| `up` | string | `""` | 开机时长，如 `1天15小时` / `2小时25分` / `30分钟` |
| `net` | string | `""` | 网速，如 `↓8 KiB/s ↑6 KiB/s`（v1.1 新增，空则隐藏） |
| `dur` | string | `""` | 本轮用时 `M:SS` / `H:MM:SS`（v1.1 新增，Bridge 自动计时，随 agent 显示在副标题行） |
| `pg` | int | `-1` | 进度：`-1` 无进度条，`0..100` 确定进度，`255` 不确定动画 |
| `lines` | array | `[]` | 屏幕日志行，最多 4 条，**最新在最上** |

### 统计网格字段（v1.1 新增，全部可选）

屏幕下方统计网格，六个格子对应上述字段（2 列 × 3 行；圆屏 240×240 与 CYD 横屏 320×240 共用同一份 UI 代码——以屏幕中心为基准的布局，网格形状一致）。**任一字段非空即显示网格**，此时隐藏日志行；空字段对应格子留空。

| 字段 | 类型 | 网格位置 | 说明 |
|---|---|---|---|
| `bal` | string | 左列第 1 行 | 剩余积分，如 `8046.86 积分` |
| `spend` | string | 右列第 1 行 | 本轮消耗，如 `2.92 积分` |
| `cost` | string | 左列第 2 行 | 今日费用，如 `2.92 积分` |
| `dur` | string | 右列第 2 行 | 本轮用时（同时显示在 agent 行） |
| `model` | string | 左列第 3 行 | 当前任务模型，如 `space-bunny` |
| `task` | string | 右列第 3 行 | 任务名称，如 `迁移 QClaw 资产` |

> 积分/模型/任务名没有标准采集途径：由 **HTTP 推送**（`POST /event` 带同名字段）、**外部 AI 直发 MQTT** 或演示模式提供；Bridge 的日志/进程适配器不产生这些值。

### lines 元素

| 字段 | 类型 | 说明 |
|---|---|---|
| `t` | string | 时间 `HH:MM` |
| `x` | string | 状态词（如 `成功` / `执行`），最长 7 字节 |
| `c` | int 0..4 | 颜色：`0` 绿 / `1` 蓝 / `2` 金 / `3` 红 / `4` 灰 |
| `s` | string | 内容，最长 43 字节（超长自动截断补 `...`） |

## 状态码

| st | 名称 | 颜色 | 典型场景 |
|---|---|---|---|
| 0 | 空闲 | 灰 | 等待新任务 |
| 1 | 准备 | 蓝 | 环境检查完成 |
| 2 | 思考 | 青 | 分析需求 |
| 3 | 回答 | 蓝 | 生成回复 |
| 4 | 执行 | 青 | 调工具/跑命令 |
| 5 | 等待回答 | 青 | 需要用户回答 |
| 6 | 等待确认 | 金 | 需要用户批准 |
| 7 | 已完成 | 绿 | 任务完成 |
| 8 | 异常 | 红 | 出错 |

## 示例

完整帧（含统计网格）：
```json
{"clock":"08:39","st":4,"title":"正在进行","sub":"迁移 QClaw 资产","agent":"space-bunny","cpu":36,"mem":45,"disk":36,"up":"49天19小时","net":"↓8 KiB/s ↑6 KiB/s","dur":"3:25","model":"space-bunny","task":"迁移 QClaw 资产","bal":"8046.86 积分","spend":"2.92 积分","cost":"2.92 积分","pg":255,"lines":[]}
```

基础帧（v1 风格，无统计网格时屏幕显示日志行）：
```json
{"clock":"19:27","st":0,"title":"空闲","sub":"等待新的任务","agent":"HY3","cpu":11,"mem":88,"up":"1天15小时","pg":-1,"lines":[{"t":"11:33","x":"成功","c":0,"s":"下次执行 08:50"},{"t":"10:02","x":"执行","c":1,"s":"每日全网热点聚..."}]}
```

执行中（不确定进度动画）：
```json
{"clock":"19:28","st":4,"title":"执行命令","sub":"pip install reportlab","agent":"HY3","cpu":34,"mem":61,"up":"1天15小时","pg":255,"lines":[{"t":"19:28","x":"执行","c":1,"s":"pip install reportlab"}]}
```

最小帧（其余字段取默认）：
```json
{"st":7,"title":"已完成"}
```

## 容错规则（固件侧）

1. 非 `{` 开头的行、无法解析的行 → 静默丢弃
2. 未知字段 → 跳过（支持嵌套对象/数组）
3. `\uXXXX` 转义 → 自动解码为 UTF-8（兼容 Python `json.dumps` 默认行为）
4. 字段值超长 → 截断，不溢出缓冲区
5. `lines` 超过 4 条 → 保留前 4 条
6. 帧不完整（无 `\n`）→ 等待后续数据

## 在环测试

协议解析与 LVGL UI 层有 109 项 PC 端断言（两板型各跑一遍，esp32c3_gc9a01 默认 / cyd_2_8 备选）+ 33 项无线模块断言（无需硬件，LVGL 用记录式桩替换，`-Itools/mock/lvgl` 注入）：

```bash
cd wb-clone
# 默认板型 esp32c3_gc9a01:
g++ -std=c++17 -Wall -Itools/mock -Itools/mock/lvgl -Ifirmware/wb_display \
    -Ifirmware/boards/esp32c3_gc9a01 \
    -x c++ tools/mock/test_main.cpp tools/mock/mock_env.cpp -o /tmp/wbtest && /tmp/wbtest
# 备选板型 cyd_2_8: 把上一行的 -Ifirmware/boards/esp32c3_gc9a01 换成 -Ifirmware/boards/cyd_2_8
g++ -std=c++17 -Wall -Itools/mock -Itools/mock/lvgl -Ifirmware/wb_display \
    -x c++ tools/mock/test_net.cpp  tools/mock/mock_env.cpp -o /tmp/nettest && /tmp/nettest
```
