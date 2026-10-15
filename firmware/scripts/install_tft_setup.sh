#!/usr/bin/env bash
# install_tft_setup.sh — 把某块板子的 TFT_eSPI 配置装到库目录
# ============================================================================
# PlatformIO 用户无需此脚本: build_flags 里的 `-I boards/<板型>` 会让 TFT_eSPI
# 自动采用 boards/<板型>/tft_setup.h (库内 __has_include 机制)。
#
# Arduino IDE 用户才需要它 (IDE 会把 tft_setup.h 复制到... 不会, 所以要手动装):
#   ./scripts/install_tft_setup.sh esp32c3_gc9a01 [库目录]
#
# 参数:
#   $1 板型目录名 (默认 esp32c3_gc9a01)
#   $2 TFT_eSPI 库根目录; 缺省时自动猜测常见位置
#      macOS: ~/Documents/Arduino/libraries/TFT_eSPI
#      Linux: ~/Arduino/libraries/TFT_eSPI
#      Windows: ~/Documents/Arduino/libraries/TFT_eSPI
set -euo pipefail

BOARD="${1:-esp32c3_gc9a01}"
HERE="$(cd "$(dirname "$0")" && pwd)"
SRC="$HERE/../boards/$BOARD/tft_setup.h"

if [ ! -f "$SRC" ]; then
  echo "找不到 $SRC; 可用板型:"
  ls -1 "$HERE/../boards"
  exit 1
fi

LIBDIR="${2:-}"
if [ -z "$LIBDIR" ]; then
  for c in "$HOME/Documents/Arduino/libraries/TFT_eSPI" \
           "$HOME/Arduino/libraries/TFT_eSPI" \
           "$HOME/OneDrive/Documents/Arduino/libraries/TFT_eSPI"; do
    [ -d "$c" ] && LIBDIR="$c" && break
  done
fi

if [ -z "$LIBDIR" ] || [ ! -d "$LIBDIR" ]; then
  echo "未找到 TFT_eSPI 库目录, 请显式传入:"
  echo "  $0 $BOARD <TFT_eSPI 库路径>"
  exit 1
fi

cp "$LIBDIR/User_Setup.h" "$LIBDIR/User_Setup.h.bak" 2>/dev/null || true
cp "$SRC" "$LIBDIR/User_Setup.h"
echo "已安装 $BOARD -> $LIBDIR/User_Setup.h"
echo "原文件备份为 User_Setup.h.bak (若原本存在)"
