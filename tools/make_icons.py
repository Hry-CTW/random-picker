#!/usr/bin/env python3
"""生成 Random Picker 的应用图标（任务栏 ico）与顶栏 logo（两套主题各一份）。

设计：圆角方块底 + 骰子五点（抽签/随机的直觉符号），与「RANDOM PICKER」名称呼应。
  - 极客主题：深底 #0B0E14 + 薄荷绿 #00E5A0
  - ins 主题：白底 #FFFFFF + 蓝紫 #4C6FFF
产物：
  assets/ctw.ico         多尺寸应用图标（256/128/64/48/32/16）
  assets/logo_geek.png   顶栏徽标（深色主题用）
  assets/logo_ins.png    顶栏徽标（浅色主题用）
"""
from PIL import Image, ImageDraw
import os

OUT = os.path.join(os.path.dirname(__file__), "..", "assets")
S = 512
R = 112          # 圆角半径
BORDER = 10      # 描边宽
DOT_R = 34       # 骰子点半径
MID = S // 2
OFF = 118        # 点到中心的偏移


def make(bg, fg, path_png, path_ico=None):
    img = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    d.rounded_rectangle([BORDER, BORDER, S - BORDER, S - BORDER], radius=R,
                        fill=bg, outline=fg, width=BORDER)
    for dx in (-OFF, 0, OFF):
        for dy in (-OFF, 0, OFF):
            if abs(dx) == OFF and abs(dy) == OFF:
                continue  # 五点骰子：四角 + 中心（去掉另外两个角）
            x, y = MID + dx, MID + dy
            r = DOT_R if (dx or dy) else DOT_R + 6
            d.ellipse([x - r, y - r, x + r, y + r], fill=fg)
    img.save(path_png)
    if path_ico:
        img.save(path_ico, sizes=[(256, 256), (128, 128), (64, 64),
                                  (48, 48), (32, 32), (16, 16)])
    print("written", path_png, path_ico or "")


os.makedirs(OUT, exist_ok=True)
make((11, 14, 20, 255), (0, 229, 160, 255),
     os.path.join(OUT, "logo_geek.png"), os.path.join(OUT, "ctw.ico"))
make((255, 255, 255, 255), (76, 111, 255, 255),
     os.path.join(OUT, "logo_ins.png"))
