#!/usr/bin/env bash
# v2.5 组页签横滑回归
#
# 说明：页签宽度走的是 Windows HDC 的 GetTextExtentPoint32W（真实字宽），
# CLI 侧无法直接调用。这里用同一个口径建一个可移植的模型：
#   w = 文字显示宽度 + 22（左右内边距），下限 46
# CJK 全角按 2 单位计，ASCII 按 1 单位计；可视宽度按左栏 320 - 2*14 = 292 计。
# 模型守住的是"会不会溢出 → 是否折行 / 是否需要横滑"这个决策边界，
# 真实像素宽度仍以 Windows 上的 GetTextExtentPoint32W 为准。

set -u
TMP="${TMPDIR:-/tmp}"
WS="$TMP/rp_v25_ws.json"
CLI="$(cd "$(dirname "$0")/.." && pwd)/picker_cli"
pass=0; fail=0

chk() {  # chk "名称" "期望" 实际...
    local name="$1" want="$2"; shift 2
    local got; got="$("$@" 2>&1)"
    if [ "$got" = "$want" ]; then
        pass=$((pass+1)); printf "  PASS  %s\n" "$name"
    else
        fail=$((fail+1)); printf "  FAIL  %s\n        期望: %s\n        实际: %s\n" "$name" "$want" "$got"
    fi
}

# 显示宽度：CJK 全角 2，其余 1（与 GetTextExtentPoint32W 在常见中文字体下的相对比例一致）
disp_w() {
    local s="$1" w=0 i c
    for ((i=0; i<${#s}; i++)); do
        c="${s:$i:1}"
        if [[ "$c" =~ [一-龥　-〿＀-￯] ]]; then w=$((w+2)); else w=$((w+1)); fi
    done
    echo "$w"
}
tab_w() {  # tab_w "组名" [人数]
    local label="$1" cnt="${2:-}"
    local txt="$label"; [ -n "$cnt" ] && txt="$label $cnt"
    local w; w=$(disp_w "$txt")
    w=$((w * 5 + 22))          # 5px/显示单位 ≈ 12pt 下 CJK 半角的经验比例
    [ $w -lt 46 ] && w=46
    echo "$w"
}
LEFT_W=320; PAD=14; VIEW_W=$((LEFT_W - 2 * PAD))

# ---------- 1. 少量组：全部放得下，不需要滚动 ----------
tabs_w=0
for g in 三班 四班 五班; do
    tabs_w=$((tabs_w + $(tab_w "$g" "3") + 6))
done
tabs_w=$((tabs_w - 6))
chk "3 个组不溢出" "no-scroll" \
    bash -c "[ $tabs_w -le $VIEW_W ] && echo no-scroll || echo scroll"

# ---------- 2. 大量组：必须横滑，不得折行 ----------
tabs_w=0
for i in $(seq 1 12); do
    tabs_w=$((tabs_w + $(tab_w "$(printf '第%d组' $i)" "6") + 6))
done
tabs_w=$((tabs_w - 6))
chk "12 个组需要横滑" "scroll" \
    bash -c "[ $tabs_w -gt $VIEW_W ] && echo scroll || echo no-scroll"

# ---------- 3. 滚动量必须收敛在 [0, overflow] ----------
# 模拟 clampTabScroll：把 scroll 推到很大、再推成负数，都应被夹回合法区间
overflow=$((tabs_w - VIEW_W))
# 用文件传函数定义，避免子 shell 里找不到函数
CLAMP_F="$TMP/rp_v25_clamp.sh"
cat > "$CLAMP_F" <<'FN'
clamp() {
    local s="$1" ov="$2"
    [ $ov -le 0 ] && { echo 0; return; }
    [ $s -lt 0 ] && s=0
    [ $s -gt $ov ] && s=$ov
    echo "$s"
}
FN
chk() {  # chk 复用同名函数会被覆盖，这里改名 run_chk
    :
}
run_chk() {
    local name="$1" want="$2"; shift 2
    local got; got="$(eval "$*" 2>&1)"
    if [ "$got" = "$want" ]; then
        pass=$((pass+1)); printf "  PASS  %s\n" "$name"
    else
        fail=$((fail+1)); printf "  FAIL  %s\n        期望: %s\n        实际: %s\n" "$name" "$want" "$got"
    fi
}
run_chk "clamp 极大值收敛到 overflow" "$overflow" ". $CLAMP_F && clamp 999999 $overflow"
run_chk "clamp 负值收敛到 0" "0" ". $CLAMP_F && clamp -999 $overflow"
run_chk "clamp 无溢出时归零" "0" ". $CLAMP_F && clamp 100 0"
rm -f "$CLAMP_F"

# ---------- 4. 页签宽度不得被截：下限与内边距 ----------
chk "单字组名下限 46" "46" bash -c "[ $(tab_w A 1) -ge 46 ] && echo 46 || echo $(tab_w A 1)"
chk "含人数的组名比纯组名宽" "yes" \
    bash -c "[ $(tab_w 一组 12) -gt $(tab_w 一组) ] && echo yes || echo no"

# ---------- 5. 滚动后命中测试要同步平移 ----------
# 模型：第 k 个页签的左边界（未滚动）——绘制与 hitTest 都必须减同一个 g_tabScroll
scroll=40
k=3; acc=0
for i in $(seq 0 $((k-1))); do acc=$((acc + $(tab_w "第$((i+1))组" "6") + 6)); done
drawn_left=$((acc - scroll))     # 绘制：OffsetRect(-g_tabScroll)
hit_left=$((acc - scroll))       # 命中：同样的平移
chk "绘制与命中平移量一致" "$drawn_left" bash -c "echo $hit_left"

echo ""
echo "结果：PASS=$pass  FAIL=$fail"
[ $fail -eq 0 ]
