#!/usr/bin/env bash
# theme_gate.sh —— src/ 里每一处挂样式表的地方，颜色必须写角色名（@text、@accent…），不许写死。
#
# 为什么要它（2026-10-20 加夜间/日间切换时量出来的）：这套颜色原来散在 25 个文件、约 380 处
# 字面量里（`#1b1c20`、`rgba(255,255,255,0.08)` 那一族）。切换到靠两件事同时成立才看得出效果：
# 调色板重刷 + 每个控件自己那份 QSS 重算。前者只有一颗函数，后者是**几十个调用点**——
# 漏一处的表现是"切到日间，别处都白了，就那一块还是黑的"，编译器/链接器/判据全都不会响。
# 这和代理那颗 applyProxy 是同一类事故：不是逻辑错，是漏调。所以数法也一样，把调用点当数打出来。
#
# 数法：只看 `setStyleSheet(` / `Theme::bind(` / `Theme::bindMenu(` 那几行开始的参数，
# 里面出现 `#rrggbb`、`#rgb` 或 `rgba(` 就算漏（注释和日志文案不算，所以只扫左括号之后的部分）。
# 行号是软的（搬家会挪），所以逐处打印出来给人核，别只看那个数。
#
# 已知不在闸里的：plugins/**。那 19 颗 dll 各有一份写死的深色，它们连的是 core 的导出面，
# 改它们=重发全部产物，而商店里那份 dll 和这版 core 的导出面对不上是另一件待拍的事（#91）。
# 所以这一轮切到日间时，插件自己那些页（录屏、置顶窗、宫图…）仍是深色 —— 界面上没装任何东西。
#
# 跑法：bash tools/theme_gate.sh
# 退出码 = 漏掉的调用点数（0 = 齐）。结论行按纯 ASCII 打：THEME_GATE_SITES= / THEME_GATE_BAD=
set -u
cd "$(dirname "$0")/.." || exit 2

sites=$(grep -rn --include=*.cpp --include=*.h -E 'setStyleSheet\(|Theme::bind' src tools 2>/dev/null \
        | grep -v 'tools/theme_gate.sh' | grep -v 'src/Theme\.')
bad=0
total=0
while IFS= read -r hit; do
    [ -z "$hit" ] && continue
    file=$(printf '%s' "$hit" | cut -d: -f1)
    line=$(printf '%s' "$hit" | cut -d: -f2)
    total=$((total + 1))
    # 从这一行往后 12 行是这一处样式表的正文
    body=$(sed -n "${line},$((line + 12))p" "$file" | sed -n '1,/;/p')
    if printf '%s' "$body" | grep -qE '#[0-9a-fA-F]{6}\b|#[0-9a-fA-F]{3}\b|rgba\('; then
        printf '  漏  %s:%s  <- 这处样式表里还有写死的颜色\n' "$file" "$line"
        bad=$((bad + 1))
    else
        printf '  OK  %s:%s\n' "$file" "$line"
    fi
done < <(printf '%s\n' "$sites")

printf 'THEME_GATE_SITES=%d\n' "$total"
printf 'THEME_GATE_BAD=%d\n' "$bad"
exit "$bad"
