#!/bin/bash
# 任意脚本 / Agent / CI 都可以用这一行推送状态到屏幕
# 前提: wb_bridge.py 正在运行 (HTTP API 默认 127.0.0.1:8788)

# ---- 状态码 ----
# 0 空闲   1 准备   2 思考   3 回答   4 执行
# 5 等待回答  6 等待确认  7 已完成  8 异常

# 例: 推送"正在执行命令"
curl -s -X POST http://127.0.0.1:8788/event \
  -H 'Content-Type: application/json' \
  -d '{"st":4,"title":"执行命令","sub":"pip install reportlab","agent":"HY3","pg":255}'

# 例: 推送"需要确认" (金色)
curl -s -X POST http://127.0.0.1:8788/event \
  -H 'Content-Type: application/json' \
  -d '{"st":6,"title":"等待确认","sub":"是否允许 git push","agent":"HY3"}'

# 例: 完成后回空闲
curl -s -X POST http://127.0.0.1:8788/event \
  -H 'Content-Type: application/json' \
  -d '{"st":7,"title":"已完成","sub":"报告已生成","ttl":3}'

# 查询当前状态
curl -s http://127.0.0.1:8788/state
