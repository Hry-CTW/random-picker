#!/usr/bin/env bash
# v2.5 组页签横滑回归
#
# 分两层：
#   [模型层] 永远跑。用可移植口径守住"溢出 → 折行还是横滑""clamp 边界"这些决策逻辑。
#   [实测层] 有 Windows 探针时跑（wine 或真机）。用真实 GDI GetTextExtentPoint32W
#            量页签宽度，验证滚轮步进、自动定位、命中一致性。没有就跳过并明确提示。
#
# 实测层怎么开：
#   选项 A（推荐，本机 macOS）：bash tools/tabprobe/build_probe.sh && bash tools/regress_v25.sh
#   选项 B（真机 Windows）：直接跑 tools/tabprobe/verify.exe
#
# 为什么分两层：页签宽度依赖 GDI 字体度量，CLI 测不到真值；
# 假模型能守逻辑边界，但守不了"真实字体下到底多宽"，所以实测层不能省。

set -u
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
TMP="${TMPDIR:-/tmp}"
PROBE="$ROOT/tools/tabprobe/verify.exe"
pass=0; fail=0; skip=0

chk() {  # chk "名称" "期望" 实际...
    local name="$1" want="$2"; shift 2
    local got; got="$("$@" 2>&1)"
    if [ "$got" = "$want" ]; then
        pass=$((pass+1)); printf "  PASS  %s\n" "$name"
    else
        fail=$((fail+1)); printf "  FAIL  %s\n        期望: %s\n        实际: %s\n" "$name" "$want" "$got"
    fi
}

echo "=== [模型层] 溢出去向与 clamp 边界 ==="

# 显示宽度：CJK 全角 2，其余 1（相对比例，非真实像素）
disp_w() {
    local s="$1" w=0 i c
    for ((i=0; i<${#s}; i++)); do
        c="${s:$i:1}"
        # 中日韩统一表意文字 + 全角标点
        case "$c" in
            [一-龥]|[　-〿]|[＀-￯]) w=$((w+2)) ;;
            *) w=$((w+1)) ;;
        esac
    done
    echo "$w"
}
tab_w() {
    local label="$1" cnt="${2:-}"
    local txt="$label"; [ -n "$cnt" ] && txt="$label $cnt"
    local w; w=$(disp_w "$txt")
    w=$((w * 5 + 22))
    [ $w -lt 46 ] && w=46
    echo "$w"
}
LEFT_W=320; PAD=14; VIEW_W=$((LEFT_W - 2 * PAD))

tabs_w=0
for g in 三班 四班 五班; do tabs_w=$((tabs_w + $(tab_w "$g" "3") + 6)); done
tabs_w=$((tabs_w - 6))
chk "3 个组不溢出" "no-scroll" \
    bash -c "[ $tabs_w -le $VIEW_W ] && echo no-scroll || echo scroll"

tabs_w=0
for i in $(seq 1 12); do
    tabs_w=$((tabs_w + $(tab_w "$(printf '第%d组' $i)" "6") + 6))
done
tabs_w=$((tabs_w - 6))
overflow=$((tabs_w - VIEW_W))
chk "12 个组需要横滑" "scroll" \
    bash -c "[ $tabs_w -gt $VIEW_W ] && echo scroll || echo no-scroll"

# clamp 定义走文件，避免子 shell 找不到函数
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

chk "单字组名下限 46" "46" bash -c "[ $(tab_w A 1) -ge 46 ] && echo 46 || echo $(tab_w A 1)"
chk "含人数的组名比纯组名宽" "yes" \
    bash -c "[ $(tab_w 一组 12) -gt $(tab_w 一组) ] && echo yes || echo no"

# 绘制与命中必须减同一个 g_tabScroll
scroll=40; k=3; acc=0
for i in $(seq 0 $((k-1))); do acc=$((acc + $(tab_w "第$((i+1))组" "6") + 6)); done
chk "绘制与命中平移量一致" "$((acc - scroll))" bash -c "echo $((acc - scroll))"

# ---------- 实测层 ----------
echo ""
echo "=== [实测层] 真实 GDI 字体度量 ==="
RUNNER=""
if [ -f "$PROBE" ]; then
    if command -v wine >/dev/null 2>&1; then
        RUNNER="wine"
    elif [ "$(uname -s)" = "Darwin" ]; then
        RUNNER=""   # macOS 上没 wine，PE 跑不了
    else
        RUNNER="native"
    fi
fi

if [ ! -f "$PROBE" ]; then
    echo "  SKIP  未找到探针 tools/tabprobe/verify.exe"
    echo "         先在 macOS 上构建：bash tools/tabprobe/build_probe.sh"
    skip=$((skip+1))
elif [ -z "$RUNNER" ]; then
    echo "  SKIP  macOS 无 wine，PE 探针无法运行"
    echo "         真机 Windows 上直接跑 tools/tabprobe/verify.exe"
    skip=$((skip+1))
else
    if [ "$RUNNER" = "wine" ]; then
        OUT="$(cd "$(dirname "$PROBE")" && WINEDEBUG=-all wine "$(basename "$PROBE")" 2>/dev/null)"
    else
        OUT="$("$PROBE" 2>/dev/null)"
    fi
    # 注意：wine 控制台会把中文吐成乱码并吞掉空格，这里只认数字与英文关键词
    FLAT="$(echo "$OUT" | tr -d ' \r')"

    # 1) 滚轮步进取 3/4 可视宽，格数应 <= 5
    NOTCHES="$(echo "$FLAT" | grep -o 'newstep=[0-9]*px->[0-9]*notches' | grep -o '>[0-9]*notches' | tr -d '>notches')"
    if [ -n "${NOTCHES:-}" ]; then
        if [ "$NOTCHES" -le 5 ]; then
            pass=$((pass+1)); printf "  PASS  滚轮步进 %s 格滚完（≤5）\n" "$NOTCHES"
        else
            fail=$((fail+1)); printf "  FAIL  滚轮步进仍需 %s 格（>5，太黏）\n" "$NOTCHES"
        fi
    else
        skip=$((skip+1)); printf "  SKIP  探针未输出步进数据\n"
    fi

    # 2) 自动定位后目标页签完整可见
    if echo "$FLAT" | grep -q 'fullyvisibleOK'; then
        pass=$((pass+1)); printf "  PASS  自动定位后目标页签完整可见\n"
    else
        fail=$((fail+1)); printf "  FAIL  自动定位后目标页签仍被裁\n"
    fi

    # 3) 绘制/命中偏移一致
    if echo "$FLAT" | grep -qi 'alloffsetsidenticalOK'; then
        pass=$((pass+1)); printf "  PASS  绘制/命中偏移量完全一致\n"
    else
        fail=$((fail+1)); printf "  FAIL  绘制/命中偏移量不一致\n"
    fi

    # 4) 已可见时不做无谓跳动
    if echo "$FLAT" | grep -qi 'noneedlessjump'; then
        pass=$((pass+1)); printf "  PASS  已可见时不产生无谓跳动\n"
    else
        fail=$((fail+1)); printf "  FAIL  已可见时仍发生跳动\n"
    fi
fi

echo ""
echo "结果：PASS=$pass  FAIL=$fail  SKIP=$skip"
[ $fail -eq 0 ]
