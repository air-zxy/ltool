#!/usr/bin/env bash
# 把"已经可移植"那批 TU 真去链接一次（-Wl,--no-undefined），让链接器告诉我们**还差谁**。
# 这个探针自己有个已知失真：它直接 g++ 编，**不跑 moc**，所以每个 QObject 类的
# staticMetaObject / vtable / typeinfo 都会报未定义 —— 那部分是 AUTOMOC 的活儿，
# 不是还没移植的实现。数缺口时先把这一族（2026-10-17 实测 54 个里占 12 个）减掉再看。
# 为什么量这个：CMake 里非 Windows 只做 OBJECT 库（只编不链），那是"不假装能用"；
# 但要知道离能链出 core 还差几步，光看"哪些 TU 编不过"不够 —— 编得过的 TU 也会引用
# 编不过的那批（比如 ToolsPage 要 PluginHost），链接期才暴露。这份清单就是剩下的地图。
#
# 跑法（Windows 侧）：wsl -e bash tools/link_gap.sh
set -u
cd "$(cd "$(dirname "$0")/.." && pwd)"

# 名单从 CMakeLists.txt 里现读，不抄第二遍：2026-10-17 之前这里是手抄的一份，
# 加一个 TU 要同时改两处，漏一处这份"地图"就少照一块 —— 抄来的东西一定会漂。
# 取法：两个 set(...) 块里所有 src/*.cpp，丢掉 _win.cpp 那些（本脚本只关心 POSIX 那半）。
SRC=$(awk '
/set\(LTOOL_CORE_PORTABLE_SRC/ { f = 1 }
/set\(LTOOL_PLATFORM_SRC/      { f = 1 }
f {
    for (i = 1; i <= NF; i++) {
        t = $i
        sub(/\)$/, "", t)   # 收尾括号常贴在最后一个文件名上，不剥掉就整条跳过（第一版就漏了它）
        if (t ~ /^src\/.*\.cpp$/ && t !~ /_win\.cpp$/) print t
    }
    if ($0 ~ /\)[[:space:]]*$/) f = 0
}' CMakeLists.txt | sort -u)

# 拿另一条独立取法核对条数（grep -o 抠子串，右括号粘着也抠得出来）。
# 对不上就是解析漏了 TU —— 第一版就这样悄悄少了两个，缺口数字看着正常其实失真了。
N=$(printf '%s\n' "$SRC" | wc -l)
M=$( (sed -n '/set(LTOOL_PLATFORM_SRC src\/platform\/Paths_posix/,/^endif/p' CMakeLists.txt
      sed -n '/set(LTOOL_CORE_PORTABLE_SRC/,/find_package(LibArchive QUIET)/p' CMakeLists.txt) \
     | grep -oE 'src/[A-Za-z0-9_/]+\.cpp' | grep -v _win.cpp | sort -u | wc -l)
if [ "$N" != "$M" ]; then
    echo "名单对不上：awk 取到 $N 个，grep 取到 $M 个 —— 解析漏了，下面那串未定义符号别当数"
    exit 1
fi

D=$(mktemp -d)
trap 'rm -rf "$D"' EXIT
C=$(pkg-config --cflags Qt6Core Qt6Gui Qt6Widgets Qt6Network Qt6Concurrent)
L=$(pkg-config --libs Qt6Core Qt6Gui Qt6Widgets Qt6Network Qt6Concurrent)

n=0
echo "名单现读自 CMakeLists.txt，共 $(echo $SRC | wc -w) 个 TU："
echo "$SRC" | tr '\n' ' '
echo
for f in $SRC; do
  if g++ -std=c++20 -fPIC -I src $C -c "$f" -o "$D/$(basename "$f").o" 2>"$D/err"; then
    n=$((n+1))
  else
    echo "COMPILE-FAIL $f"
    head -3 "$D/err" | sed 's/^/    /'
  fi
done
echo "编过的 TU：$n / $(echo $SRC | wc -w)"

g++ -shared -Wl,--no-undefined "$D"/*.o $L -o "$D/lib.so" 2>"$D/link.log"
rc=$?
if [ $rc -eq 0 ]; then
  echo "链接：过了 —— 说明这批已经自洽（那 CMake 的 OBJECT 限制可以放宽了）"
  exit 0
fi

grep -o "undefined reference to \`[^']*'" "$D/link.log" \
  | sed "s/undefined reference to \`//; s/'$//" | c++filt | sort -u > "$D/undef.txt"
total=$(wc -l < "$D/undef.txt")
echo "链接：没过（退出码 $rc）。未定义符号 $total 个，按被谁缺归一下（要补的就是这些族）："
sed 's/@[A-Za-z0-9_]*@@*.*//; s/(.*//' "$D/undef.txt" \
  | sed 's/^[A-Za-z]*:://; s/^/ /' | awk '{print $1}' | sed 's/^\([A-Za-z_]*\)$/\1/' | sort | uniq -c | sort -rn | head -12
echo
echo "前 12 个未定义符号原文："
head -12 "$D/undef.txt" | sed 's/^/  /'
