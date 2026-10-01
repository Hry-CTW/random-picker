#!/bin/bash
# Random Picker v2 回归脚本（本机 CLI 版，CI 与本机都能跑）
#   用法：./tools/regress_v2.sh
# 覆盖：7 种名单样本导入 / 抽 N 人（正常·超额·非法）/ 按组抽选 / 一键 6 组 / 改组名 / 防重复
cd "$(dirname "$0")/.."
CLI=./picker_cli
[ -x "$CLI" ] || { echo "先 ./build.sh 生成 picker_cli"; exit 1; }

pass=0; fail=0
chk() {  # chk <说明> <期望:ok|fail> <命令...>
    local desc="$1" want="$2"; shift 2
    local out; out=$("$@" 2>&1); local rc=$?
    local got="ok"; [ $rc -ne 0 ] && got="fail"
    if [ "$got" = "$want" ]; then
        pass=$((pass+1)); printf "  PASS  %s\n" "$desc"
    else
        fail=$((fail+1)); printf "  FAIL  %s\n        %s\n" "$desc" "$(echo "$out" | head -2)"
    fi
}

echo "[1] 7 种名单样本导入"
for f in samples/*; do
    chk "导入 $(basename "$f")" ok $CLI import "$f"
done

echo "[2] 抽 N 人"
chk "抽 3 人（正常）"        ok   $CLI pickn 3  --file samples/people.csv
chk "抽 1 人（下界）"        ok   $CLI pickn 1  --file samples/people.csv
chk "抽 99 人（超额→人话提示）" fail $CLI pickn 99 --file samples/people.csv
chk "抽 0 人（非法）"        fail $CLI pickn 0  --file samples/people.csv

echo "[3] 按组抽选"
chk "组内抽 2 人（二组）"     ok   $CLI pickn 2 --file samples/people.csv --group 二组
chk "组内超额（二组抽 99）"   fail $CLI pickn 99 --file samples/people.csv --group 二组
chk "组名不存在（要说出来）"   fail $CLI pickn 2 --file samples/people.csv --group 九组
chk "组名不存在（pick 同样）"  fail $CLI pick 1 --file samples/people.csv --group 九组

echo "[4] 一键 6 组与改组名"
# 临时造一份「已经是 6 组」的名单：CLI 是单进程，跨用例的状态只能靠样本文件带过去
TMP=$(mktemp -d)
printf '\xEF\xBB\xBF姓名,组别,学号\n甲,第1组,1\n乙,第2组,2\n丙,第3组,3\n丁,第4组,4\n戊,第5组,5\n己,第6组,6\n庚,第1组,7\n辛,第2组,8\n' > "$TMP/g6.csv"
chk "重排成 6 组（覆盖原分组）"  ok   $CLI initgroups 6 --file samples/people.csv --force
chk "已经有 6 组再补（应跳过）"  fail $CLI initgroups 6 --file "$TMP/g6.csv"
chk "补空组（3 组→6 组）"      ok   $CLI initgroups 6 --file samples/people.md
chk "改名（第1组→阳光组）"      ok   $CLI rename 第1组 阳光组 --file "$TMP/g6.csv"
chk "改名撞已有组名"            fail $CLI rename 第1组 第2组 --file "$TMP/g6.csv"
chk "改名到不存在的组"          fail $CLI rename 第九组 阳光组 --file samples/people.md
chk "改名后再按新组名抽 2 人"    ok   $CLI pickn 2 --file "$TMP/g6.csv" --group 第1组
rm -rf "$TMP"

echo "[5] 抽组与防重复"
chk "抽 1 个组"               ok   $CLI pickgroup --file samples/people.csv
chk "防重复连抽 20 次"         ok   $CLI pick 20 --file samples/people.csv --no-repeat

echo
echo "结果：PASS=$pass  FAIL=$fail"
[ $fail -eq 0 ] || exit 1
