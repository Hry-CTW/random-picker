#!/bin/bash
# Random Picker v2.2 回归：人员组织多对多 + JSON 工作区持久化
# 用法：./tools/regress_v22.sh
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
# chk_contains <desc> <needle> <cmd...>
chk_contains() {
    local desc="$1" needle="$2"; shift 2
    local out; out=$("$@" 2>&1)
    if echo "$out" | grep -qF "$needle"; then
        pass=$((pass+1)); printf "  PASS  %s\n" "$desc"
    else
        fail=$((fail+1)); printf "  FAIL  %s\n        %s\n" "$desc" "$(echo "$out" | head -3)"
    fi
}

TMP=$(mktemp -d)
F="$TMP/roster.csv"
printf '\xEF\xBB\xBF姓名,组别,学号\n张三,一组,001\n李四,二组,002\n王五,,003\n' > "$F"

echo "[1] 多对多加入"
chk "张三加入二组" ok $CLI join 1 二组 --file "$F"
chk "张三再加入班委" ok $CLI join 1 班委 --file "$F"
echo "[2] 持久化跨调用保留"
chk_contains "张三属于 一组·二组·班委" "一组 · 二组 · 班委" $CLI orgs --file "$F"
chk_contains "二组计数=2（张三+李四）" "二组: 2" $CLI orgs --file "$F"
chk_contains "班委计数=1" "班委: 1" $CLI orgs --file "$F"
echo "[3] 多组织成员在多个组都能被抽到"
chk "按二组抽 2 人（含张三）" ok $CLI pickn 2 --file "${F%.csv}.json" --group 二组
echo "[4] 退出组织"
chk "张三退出二组" ok $CLI leave 1 二组 --file "$F"
chk_contains "退出后已无二组归属(张三剩一组·班委)" "一组 · 班委" $CLI orgs --file "$F"
echo "[5] 改组织名同步多组织成员"
chk "张三加入文体委员" ok $CLI join 1 文体委员 --file "$F"
chk "班委→班委会" ok $CLI rename 班委 班委会 --file "${F%.csv}.json"
chk_contains "张三：一组·班委会·文体委员" "一组 · 班委会 · 文体委员" $CLI orgs --file "$F"
echo "[6] 工作区 save→load round-trip"
chk "另存工作区" ok $CLI workspace "$TMP/saved.json" --file "${F%.csv}.json"
chk_contains "重载后多对多完整" "一组 · 班委会 · 文体委员" $CLI workspace "$TMP/saved.json" --load
echo "[7] 旧 CSV 导入仍单组织"
G="$TMP/legacy.csv"
printf '\xEF\xBB\xBF姓名,组别\n小明,三组\n' > "$G"
chk_contains "旧 CSV 单组织" "三组" $CLI list --file "$G"

rm -rf "$TMP"
echo
echo "结果：PASS=$pass  FAIL=$fail"
[ $fail -eq 0 ] || exit 1
