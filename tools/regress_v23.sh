#!/bin/bash
# Random Picker v2.3 回归：人员/组织拖动排序
# 用法：./tools/regress_v23.sh
cd "$(dirname "$0")/.."
CLI=./picker_cli
[ -x "$CLI" ] || { echo "先 ./build.sh 生成 picker_cli"; exit 1; }

pass=0; fail=0
chk() {
    local desc="$1" want="$2"; shift 2
    local out; out=$("$@" 2>&1); local rc=$?
    local got="ok"; [ $rc -ne 0 ] && got="fail"
    if [ "$got" = "$want" ]; then
        pass=$((pass+1)); printf "  PASS  %s\n" "$desc"
    else
        fail=$((fail+1)); printf "  FAIL  %s\n        %s\n" "$desc" "$(echo "$out" | head -3)"
    fi
}
chk_contains() {
    local desc="$1" needle="$2"; shift 2
    local out; out=$("$@" 2>&1)
    if echo "$out" | grep -qF "$needle"; then
        pass=$((pass+1)); printf "  PASS  %s\n" "$desc"
    else
        fail=$((fail+1)); printf "  FAIL  %s\n        %s\n" "$desc" "$(echo "$out" | head -6)"
    fi
}

TMP=$(mktemp -d)
F="$TMP/roster.csv"
printf '\xEF\xBB\xBF姓名,组别,学号\n张三,一组,001\n李四,二组,002\n王五,三组,003\n' > "$F"
WS="${F%.csv}.json"

echo "[1] 人员顺序：初始 张三·李四·王五"
chk_contains "初始顺序" "1. 张三" $CLI orgs --file "$F"
echo "[2] 拖动人员 3→1（王五移到最前）"
chk "movep 3 1" ok $CLI movep 3 1 --file "$F"
chk_contains "移动后第1位是王五" "1. 王五" $CLI orgs --file "$WS"
chk_contains "第2位是张三" "2. 张三" $CLI orgs --file "$WS"
echo "[3] 拖动人员 1→2（王五与张三交换回）"
chk "movep 1 2" ok $CLI movep 1 2 --file "$WS"
chk_contains "第1位恢复张三" "1. 张三" $CLI orgs --file "$WS"
echo "[4] 越界/相同位置拒绝"
chk "movep 1 1 位置未变" fail $CLI movep 1 1 --file "$WS"
chk "movep 1 99 越界" fail $CLI movep 1 99 --file "$WS"
echo "[5] 组织顺序：初始 一组·二组·三组"
chk_contains "初始有组序" "一组" $CLI orgs --file "$WS"
echo "[6] 拖动组织 3→1（三组移到最前）"
chk "moveo 3 1" ok $CLI moveo 3 1 --file "$WS"
# 组织顺序体现在 orgs 输出「组织 ↔ 人数」之后的第一行组名
OUT=$($CLI orgs --file "$WS")
FIRST=$(echo "$OUT" | awk '/组织/{f=1;next} f&&NF{print;exit}' | cut -d: -f1)
if [ "$FIRST" = "三组" ]; then
    pass=$((pass+1)); printf "  PASS  组织顺序首位为三组\n"
else
    fail=$((fail+1)); printf "  FAIL  组织顺序首位为三组 (实际: %s)\n" "$FIRST"
fi
echo "[7] 移动后多对多关系不丢"
# 当前第1位是张三（第3步换回）；给他加班委，应属 一组·班委
chk "第1位张三加入班委" ok $CLI join 1 班委 --file "$WS"
chk_contains "张三属 一组·班委" "一组 · 班委" $CLI orgs --file "$WS"

echo "[8] 删组织修复验收（v2.3 真 bug）"
# 8a. 删有成员的组织：成员同步退出，组织不再出现
chk "delorg 班委 成功" ok $CLI delorg 班委 --file "$WS"
chk_contains "张三不再属班委" "1. 张三 [一组]" $CLI orgs --file "$WS"
OUT=$($CLI orgs --file "$WS")
if echo "$OUT" | grep -q "班委"; then
    fail=$((fail+1)); printf "  FAIL  班委已从组织列表消失\n"
else
    pass=$((pass+1)); printf "  PASS  班委已从组织列表消失\n"
fi
# 8b. 再删同一组织：必须报「没找到」而非假成功（迭代器失效 bug 回归点）
chk "重复 delorg 报没找到" fail $CLI delorg 班委 --file "$WS"
# 8c. 空组织也能删（原实现：固定组里没有、人身上的组没有 → 误报没找到）
chk "addorg 空组织" ok $CLI addorg 预备队 --file "$WS"
chk "delorg 空组织" ok $CLI delorg 预备队 --file "$WS"
# 8d. 李四只属二组（二组即主组）：删二组后不得「复活」，应变未分组
chk "delorg 二组(李四唯一主组)" ok $CLI delorg 二组 --file "$WS"
chk_contains "李四变未分组" "李四 [未分组]" $CLI orgs --file "$WS"
OUT=$($CLI orgs --file "$WS")
if echo "$OUT" | awk '/组织/{f=1;next} f&&NF' | grep -q "^二组"; then
    fail=$((fail+1)); printf "  FAIL  二组不得在组织列表复活\n"
else
    pass=$((pass+1)); printf "  PASS  二组不得在组织列表复活\n"
fi

echo ""
echo "结果：PASS=$pass  FAIL=$fail"
[ $fail -eq 0 ]
