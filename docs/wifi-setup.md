# 无线配网指南（CYD + WiFi/MQTT）

本工程支持两种方式把状态送到屏幕：

- **USB 串口**：插线即用，不需要配网（默认 `transport=serial`）。
- **WiFi + MQTT**：屏幕与电脑在同一 WiFi 下，电脑端 Bridge（内置 broker）推送，屏幕订阅。首次需配网。

本文讲 WiFi 配网。

## 一、工作原理

```
电脑 (Bridge, --broker builtin)  ──MQTT──►  wb/frame  topic
        ▲                                        │
        │ 订阅自身 topic (外部 AI 直发)            │ 订阅
        │                                         ▼
   任意 AI / wb_notify ──MQTT──► 内置 Broker ◄──WiFi── CYD 屏幕
```

- 屏幕固件含手写 MQTT 客户端（QoS0 子集），连接 broker 并订阅 `wb/frame`。
- Bridge 的 **内置 broker**（`mini_broker.py`，零依赖）在本机 `0.0.0.0:1883` 监听，开箱即用。
- 屏幕发布/订阅用同一个 topic；Bridge 发的帧带 `_src:"bridge"` 标记，broker 回发时固件/链路会**丢弃自己的帧**，避免回环。

## 二、首次配网步骤

1. **烧录固件**（`firmware/README.md`），上电。
2. 未配过时屏幕显示：
   ```
   配网 / 手机连接 WiFi 热点
   wb-screen-XXXX
   ```
   同时板子会起一个 AP 热点 `wb-screen-XXXX`。
3. 手机/电脑连上该热点，浏览器打开 `http://192.168.4.1`（ captive portal 会自动弹）。
4. 在网页填：
   - **WiFi SSID / 密码**：你家/办公室的 2.4G WiFi（ESP32 不支持 5G）。
   - **Broker 地址**：运行 Bridge 的电脑在**同一 WiFi 下的 IP**（如 `192.168.1.50`）。
     - 用内置 broker 时填电脑 IP；用外部 broker（mosquitto/EMQX）填那台机器的 IP。
   - **Topic**：默认 `wb/frame`。
5. 点保存 → 设备把配置写入 NVS 并重启。

## 三、启动 Bridge（无线）

```bash
# 内置 broker 开箱即用 (推荐, 屏幕已配网连同一 WiFi)
python3 bridge/wb_bridge.py --transport both --broker builtin
#   both = 串口 + MQTT 双发; 纯无线用 --transport mqtt

# 或连你自己的 broker
python3 bridge/wb_bridge.py --transport mqtt --broker 192.168.1.50:1883
```

配网成功后屏幕顶栏显示 `WiFi` + 实时时钟，状态帧通过 MQTT 刷新；USB 线插着也能用串口通道（双发互不干扰）。

## 四、常见排障

| 现象 | 可能原因 | 处理 |
|---|---|---|
| 屏幕一直显示「配网」 | 上电即未配网（正常首态） | 按第二节配网；配过却仍进配网 → 长按复位或清 NVS |
| 连上 AP 但打不开 `192.168.4.1` | 手机数据流量抢网 | 关蜂窝数据再开浏览器；或手动访问 |
| 配网后连不上 WiFi | 5G WiFi / 密码错 | 改用 2.4G；确认密码 |
| WiFi 连上但屏幕无状态 | Broker IP 填错 / 电脑防火墙 | 确认电脑 IP 与 `--broker builtin` 的机器一致；关防火墙或放行 1883 |
| MQTT 一直重连 | broker 没起 / 端口不对 | 确认 Bridge 用 `--broker builtin` 已启动；`--mqtt-port` 与固件填的一致 |
| 只有插线有画面、无线没有 | 固件在串口模式 | 配网后固件自动进 WiFi 模式；若 `netCfg.valid` 为假会回退配网 |

## 五、清掉已存 WiFi（重配）

NVS 里存了 WiFi 凭证，想重配：
- 临时改 `wb_net.h` 里 `NetConfig::load` 永远返回无效，或
- 在配网页提供一个「清除」入口（当前固件需重新烧录带清除逻辑，或短接相关 GPIO 复位 NVS——见源码 `NetConfig`）。

> 最简：把 `wb_net.h` 中 `PREF_KEY_VALID` 对应 NVS 清掉重新烧录即可回到配网态。
