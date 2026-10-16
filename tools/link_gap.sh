#!/usr/bin/env bash
# 把"已经可移植"那批 TU 真去链接一次（-Wl,--no-undefined），让链接器告诉我们**还差谁**。
# 为什么量这个：CMake 里非 Windows 只做 OBJECT 库（只编不链），那是"不假装能用"；
# 但要知道离能链出 core 还差几步，光看"哪些 TU 编不过"不够 —— 编得过的 TU 也会引用
# 编不过的那批（比如 ToolsPage 要 PluginHost），链接期才暴露。这份清单就是剩下的地图。
#
# 跑法（Windows 侧）：wsl -e bash tools/link_gap.sh
set -u
cd "$(cd "$(dirname "$0")/.." && pwd)"

# 这份名单和 CMakeLists.txt 里 LTOOL_CORE_PORTABLE_SRC 保持一致（逐个 TU 实测过能编）
SRC="src/platform/Paths_posix.cpp src/platform/Launch_posix.cpp src/platform/Autostart_posix.cpp
src/platform/FileMeta_posix.cpp src/platform/ProcInfo_posix.cpp
src/Utils.cpp src/Pinyin.cpp src/Archives.cpp src/CaptureHistory.cpp src/DownloadManager.cpp
src/FileTools.cpp src/InfoRow.cpp src/PluginStore.cpp src/SilentInstallLog.cpp src/SysTools.cpp
src/ToolsPage.cpp src/WebShortcuts.cpp src/WebsitesPage.cpp"

D=$(mktemp -d)
trap 'rm -rf "$D"' EXIT
C=$(pkg-config --cflags Qt6Core Qt6Gui Qt6Widgets Qt6Network Qt6Concurrent)
L=$(pkg-config --libs Qt6Core Qt6Gui Qt6Widgets Qt6Network Qt6Concurrent)

n=0
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
