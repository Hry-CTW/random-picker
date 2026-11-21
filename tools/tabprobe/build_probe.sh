#!/bin/bash
# 构建 v2.5 页签布局探针（Windows PE，在 macOS 上交叉编译，用 wine 跑）
#
# 探针做什么：用真实 GDI（GetTextExtentPoint32W）量页签字宽，
# 复现 picker_gui.cpp 的布局算法，输出滚轮步进 / 自动定位 / 命中一致性的判定结果。
# 它跑的是和 GUI 同一套 Windows API，拿到的宽度是真实像素，不是估算。
set -e
cd "$(dirname "$0")"
MINGW="${MINGW:-/Users/han2022/Documents/Hry/mingw-gcc-cross/bin}"
CXX="$MINGW/x86_64-w64-mingw32-g++"

if [[ ! -x "$CXX" ]]; then
  echo "找不到交叉编译器：$CXX"
  echo "用 MINGW=/path/to/bin 指定，或装 mingw-w64。"
  exit 1
fi

echo "[1/2] 编译探针 verify.cpp -> verify.exe"
"$CXX" -std=c++17 -O2 -o verify.exe verify.cpp -mconsole -static -lgdi32 -luser32

echo "[2/2] 运行（有 wine 就现在跑）"
if command -v wine >/dev/null 2>&1; then
  WINEDEBUG=-all wine verify.exe 2>/dev/null
else
  echo "  未装 wine，跳过运行。真机 Windows 上直接双击 verify.exe。"
fi

echo "完成：$(pwd)/verify.exe"
