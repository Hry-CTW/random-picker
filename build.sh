#!/bin/bash
# 交叉编译 Windows 版 GUI（在本机 macOS + mingw-w64 上直接产出 picker.exe）
set -e
cd "$(dirname "$0")"
MINGW="${MINGW:-/Users/han2022/Documents/Hry/mingw-gcc-cross/bin}"
export PATH="$MINGW:$PATH"

echo "[1/3] 编译资源（CTW 图标 + 徽标 PNG）"
x86_64-w64-mingw32-windres resources.rc -O coff -o resources.o

echo "[2/3] 构建 Windows GUI: picker.exe"
x86_64-w64-mingw32-g++ -std=c++17 -O2 -o picker.exe src/picker_gui.cpp src/roster.cpp resources.o \
  -mwindows -static -static-libgcc -static-libstdc++ \
  -Wno-free-nonheap-object -Wno-address \
  -lgdi32 -luser32 -lcomdlg32 -lbcrypt -lgdiplus -lole32

echo "[3/3] 构建本机 CLI（用于在本机验证解析逻辑；CI 上加 --exe-only 可跳过）"
if [ "$1" = "--exe-only" ]; then
  echo "跳过（--exe-only，仅产出 Windows exe）"
else
  CXX="${CXX:-}"
  [ -z "$CXX" ] && { command -v clang++ >/dev/null 2>&1 && CXX=clang++ || CXX=g++; }
  # iconv：macOS 需要显式链接，Linux/glibc 内置
  LICONV=""
  case "$(uname -s)" in Darwin*) LICONV="-liconv";; esac
  $CXX -std=c++17 -O2 -o picker_cli src/cli.cpp src/roster.cpp $LICONV \
    || echo "（本机 CLI 构建失败，不影响 Windows exe）"
fi

echo "完成：picker.exe（Windows）"
echo "本机验证：./picker_cli import samples/people.csv && ./picker_cli import samples/people.xlsx"
