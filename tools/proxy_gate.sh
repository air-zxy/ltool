#!/usr/bin/env bash
# proxy_gate.sh —— 每一处新建 QNetworkAccessManager，后面必须紧跟一个"代理怎么摆"的决定。
#
# 为什么要它（2026-10-19 他报"下载器要用本地代理下载"查出来的）：代理这件事在 core 里只有
# 一颗 `DevEnv::applyProxy(nam)`，它读 `net/proxy`（留空 = 退回系统代理），八个新建 managers
# 里有七颗老老实实调了它，漏的是 plugins/capture/OcrBundle.cpp 那个**栈上的** —— 于是 OCR 引擎
# 那 27.9MB（从 files.pythonhosted.org，境外源）在这台配了 127.0.0.1:9980 的机器上盲下,
# 下不动，而界面那句失败原因里连"走的哪条路"都没有。这种漏不是逻辑错，是**漏调**，
# 编译器、链接器、判据全都不会响 —— 只有把"调用点齐不齐"当一条数出来才看得见。
#
# 数法：只认 15 行以内出现 applyProxy( 或 setProxy( 的那颗。
# 行号是软的（搬家/moc 会挪），所以逐处打印出来给人核，别只看那个数。
#
# 跑法：bash tools/proxy_gate.sh        （在项目根或任何地方都行）
# 退出码 = 漏掉的调用点数（0 = 齐）。结论行按纯 ASCII 打：PROXY_GATE_SITES= / PROXY_GATE_BAD=
set -u
cd "$(dirname "$0")/.." || exit 2

sites=$(grep -rn --include=*.cpp -E 'new QNetworkAccessManager|QNetworkAccessManager[[:space:]]+[a-zA-Z_]+[[:space:]]*(;|\()' src plugins tools 2>/dev/null | grep -v 'tools/proxy_gate.sh')
bad=0
total=0
while IFS= read -r hit; do
    [ -z "$hit" ] && continue
    file=$(printf '%s' "$hit" | cut -d: -f1)
    line=$(printf '%s' "$hit" | cut -d: -f2)
    total=$((total + 1))
    # 往后 15 行里找代理决定
    window=$(sed -n "${line},$((line + 15))p" "$file")
    if printf '%s' "$window" | grep -qE 'applyProxy\(|setProxy\('; then
        printf '  OK  %s:%s\n' "$file" "$line"
    else
        printf '  漏  %s:%s  <- 这之后的 15 行里没有 applyProxy/setProxy\n' "$file" "$line"
        bad=$((bad + 1))
    fi
done < <(printf '%s\n' "$sites")

printf 'PROXY_GATE_SITES=%d\n' "$total"
printf 'PROXY_GATE_BAD=%d\n' "$bad"
exit "$bad"
