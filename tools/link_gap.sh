#!/usr/bin/env bash
# 把"已经可移植"那批 TU 真去链接一次（-Wl,--no-undefined），让链接器告诉我们**还差谁**。
# 这个探针**会自己补跑 moc**（名单里每个 TU 的同名头，带 Q_OBJECT 就过一次）：
# 2026-10-17 之前不跑，于是每个 QObject 类的 staticMetaObject / vtable / 信号都报未定义，
# 54 个"缺口"里 12 个是探针自己造的（#93）。现在报数分成"元对象族"和"真缺口"两档。
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
    sub(/#.*/, "")   # 名单里可以写行尾注释（2026-10-17 加了四个 TU  each 带说明），
                    # 不先剥掉的话"这行以 ) 结尾"那个收尾判断会被注释带偏，
                    # 于是最后一颗 TU 整条漏掉 —— 两份取法必须看同一份文本才核对得出条数
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
     | sed 's/#.*//' \
     | grep -oE 'src/[A-Za-z0-9_/]+\.cpp' | grep -v _win.cpp | sort -u | wc -l)
if [ "$N" != "$M" ]; then
    echo "名单对不上：awk 取到 $N 个，grep 取到 $M 个 —— 解析漏了，下面那串未定义符号别当数"
    exit 1
fi

D=$(mktemp -d)
trap 'rm -rf "$D"' EXIT
C=$(pkg-config --cflags Qt6Core Qt6Gui Qt6Widgets Qt6Network Qt6Concurrent)
L=$(pkg-config --libs Qt6Core Qt6Gui Qt6Widgets Qt6Network Qt6Concurrent)
# libarchive 也要链：CMake 那边 portable 库是 find_package(LibArchive) 链上的，
# 探针不链就会多算十来个 archive_* "缺口"（2026-10-17 实测 41 个里占 10 个）——
# 和上面 moc 那档是同一类失真：探针少给一样依赖，缺口数字就虚高一截。
LA=$(pkg-config --libs libarchive 2>/dev/null || true)
# AboutPage.cpp 进了可移植名单（2026-10-17 第十刀），它没拿到 LTOOL_VERSION 就当场 #error ——
# CMake 那边这一定义是 PUBLIC 挂在 ltoolcore 上的，探针不给就是"少给一样依赖"，
# 于是 1 个 TU 编不过被念成"实现缺口"。这条规矩和上面 moc / libarchive 是同一类失真。
# 探针只是语法与链接，不是界面，所以版本号在这里是什么不重要。
VER='-DLTOOL_VERSION="0.0.0-linkgap"'
if [ -n "$LA" ]; then
    L="$L $LA"
    echo "libarchive：链上了（$LA）"
else
    echo "libarchive：这台机器上 pkg-config 找不到 —— 下面凡是 archive_* 的缺口都算不清，别当真缺口数"
fi

n=0
echo "名单现读自 CMakeLists.txt，共 $(echo $SRC | wc -w) 个 TU："
echo "$SRC" | tr '\n' ' '
echo
for f in $SRC; do
  if g++ -std=c++20 -fPIC -I src $C $VER -c "$f" -o "$D/$(basename "$f").o" 2>"$D/err"; then
    n=$((n+1))
  else
    echo "COMPILE-FAIL $f"
    head -3 "$D/err" | sed 's/^/    /'
  fi
done
echo "编过的 TU：$n / $(echo $SRC | wc -w)"

# AUTOMOC 那一步自己补上：名单里每个 TU 的同名头，只要写了 Q_OBJECT 就过一次 moc。
# 不跑这一遍，每个 QObject 类的 staticMetaObject / vtable / typeinfo / 信号全报未定义 ——
# 那是探针自己造的失真，不是"实现还没移植"（2026-10-17 那 54 个里占 12 个，#93 就是为了这件事）。
MOC=$(command -v moc || true)
[ -x "$MOC" ] || MOC=/usr/lib/qt6/libexec/moc
m=0
for f in $SRC; do
  b=$(basename "$f" .cpp)
  [ -f "src/$b.h" ] || continue
  grep -q Q_OBJECT "src/$b.h" || continue
  if "$MOC" "src/$b.h" -o "$D/moc_$b.cpp" 2>"$D/mocerr" \
     && g++ -std=c++20 -fPIC -I src $C -c "$D/moc_$b.cpp" -o "$D/moc_$b.o" 2>>"$D/mocerr"; then
    m=$((m+1))
  else
    echo "MOC-FAIL $b：$(head -2 "$D/mocerr" | tr '\n' ' ')"
  fi
done
echo "moc 过的头：$m 个（来自 $MOC）"

g++ -shared -Wl,--no-undefined "$D"/*.o $L -o "$D/lib.so" 2>"$D/link.log"
rc=$?
if [ $rc -eq 0 ]; then
  echo "链接：过了 —— 说明这批已经自洽（那 CMake 的 OBJECT 限制可以放宽了）"
  exit 0
fi

grep -o "undefined reference to \`[^']*'" "$D/link.log" \
  | sed "s/undefined reference to \`//; s/'$//" | c++filt | sort -u > "$D/undef.txt"
total=$(wc -l < "$D/undef.txt")
# 元对象族单独算：moc 已经跑过了，还剩这类符号就说明**那个类的 TU 不在名单里**
# （SettingsDialog / SearchWindow 这种），是"名单还没扩到那儿"，不是"某个函数没移植"。
meta=$(grep -Ec 'staticMetaObject|vtable |typeinfo' "$D/undef.txt")
echo "链接：没过（退出码 $rc）。未定义符号 $total 个 = 元对象族 $meta 个（那个类的 TU 不在名单里）+ 真缺口 $((total - meta)) 个。按被谁缺归一下（要补的就是这些族）："
sed 's/@[A-Za-z0-9_]*@@*.*//; s/(.*//' "$D/undef.txt" \
  | sed 's/^[A-Za-z]*:://; s/^/ /' | awk '{print $1}' | sed 's/^\([A-Za-z_]*\)$/\1/' | sort | uniq -c | sort -rn | head -12
echo
echo "前 12 个未定义符号原文："
head -12 "$D/undef.txt" | sed 's/^/  /'
