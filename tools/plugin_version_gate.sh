#!/usr/bin/env bash
# 「每颗插件自己递增」那条纪律的闸。形状是他 2026-10-17 拍的：号不跟 core 走，改了哪颗那颗自己 +1。
# 为什么要闸：版本号现在是商店判定的主判据（PluginStore::compare 先比版本、再看哈希），
# 而这轮实测 v1.0.3 之后源码动过的插件有 12 颗、Version 全停在 1.0.0 —— 没有闸，
# 「每颗自己递增」只是文档里一句希望，商店那批永远停在「与商店不同」，用户看不到可更新。
#
# 基线为什么取最近一个 tag：tag 就是「那一版发出去了、用户按那批号装过」的时刻。
# 从它到现在源码改了而号没动 = 这批产物一旦重新发布，商店里那颗还是旧号，判定只能报「内容不同」。
#
# 只管 plugins/<id>/ 里除 plugin.json 之外的文件：core 那边改了不该动插件号
# （插件 dll 是导入 ltoolcore.dll 的，core 变了它的字节也没变），那不在这条纪律的范围里。
#
# 跑法：bash tools/plugin_version_gate.sh [基线ref]   默认基线 = git describe --tags --abbrev=0
# 明细行是中文给人看的，计数行一律 ASCII —— 这台机器 PowerShell 码页 GBK，拿中文行 grep 计数会读出 0。
set -u
cd "$(cd "$(dirname "$0")/.." && pwd)"

BASE=${1:-}
[ -z "$BASE" ] && BASE=$(git describe --tags --abbrev=0 2>/dev/null)
# 标签推的那次 run 里，最近一个 tag 就是 HEAD 自己 —— 拿它当基线等于没比（0 个对象上空转的绿）。
# 所以撞上这种情况要往前退一个 tag：这一版发出去之后到现在改了什么，才是这条纪律管的范围。
if [ -n "$BASE" ] && [ "$(git rev-parse "${BASE}^{commit}")" = "$(git rev-parse HEAD)" ]; then
    BASE=$(git describe --tags --abbrev=0 "${BASE}^" 2>/dev/null)
    [ -n "$BASE" ] && echo "INFO 最近一个 tag 就是 HEAD，基线退到 $BASE"
fi
if [ -z "$BASE" ] || ! git rev-parse -q --verify "${BASE}^{commit}" >/dev/null; then
    echo "GATE_BASELINE=none —— 拿不到 tag 也没传基线，这轮别当数"
    exit 1
fi

# 从 plugin.json 抠 Version；ref 传空串就是读工作树（这样没提交的改动也照样闸得住）。
ver_of() {
    if [ -z "$1" ]; then
        sed -nE 's/.*"Version"[[:space:]]*:[[:space:]]*"([^"]*)".*/\1/p' "$2" | head -1
    else
        git show "$1:$2" 2>/dev/null | sed -nE 's/.*"Version"[[:space:]]*:[[:space:]]*"([^"]*)".*/\1/p' | head -1
    fi
}

# 点号版本比较，输出 -1 / 0 / 1（商店那边用 Utils::compareVersions，这里要个不依赖 Qt 的等价物）
vcmp() {
    awk -v a="$1" -v b="$2" 'BEGIN {
        n = split(a, x, "."); m = split(b, y, ".")
        for (i = 1; i <= (n > m ? n : m); i++) {
            p = (i <= n ? x[i] + 0 : 0); q = (i <= m ? y[i] + 0 : 0)
            if (p < q) { print -1; exit }
            if (p > q) { print 1; exit }
        }
        print 0
    }'
}

total=0 new=0 quiet=0 needs=0 back=0
for d in plugins/*/; do
    id=$(basename "$d")
    if [ ! -f "${d}plugin.json" ]; then
        echo "GATE_NO_MANIFEST $id —— 目录里没 plugin.json，闸没法认它的号"
        needs=$((needs + 1))
        continue
    fi
    total=$((total + 1))
    was=$(ver_of "$BASE" "plugins/$id/plugin.json")
    # 基线上压根没这颗 = tag 之后新建的插件，1.0.0 就是它的第一号，不该被要求递增。
    # 这里有个已知豁免要说清：tag 之后新建、却又已经发进商店的那颗（clipboard 这轮就是），
    # 这一轮归不到它 —— 等下一次发插件产物 + 打 tag，它就在基线里了，从那时起照常管。
    if [ -z "$was" ]; then
        echo "NEW_SINCE_BASE $id（基线 $BASE 上没这颗，号从 $(ver_of "" "${d}plugin.json") 起算）"
        new=$((new + 1))
        continue
    fi
    changed=$(git diff --name-only "$BASE" -- "$d" | grep -v '/plugin\.json$' | wc -l)
    if [ "$changed" -eq 0 ]; then
        quiet=$((quiet + 1))
        continue
    fi
    now=$(ver_of "" "${d}plugin.json")
    if [ "$now" = "$was" ]; then
        echo "NEEDS_BUMP $id 基线是 v$was，这之后源码改了 $changed 个文件，号却一动不动"
        needs=$((needs + 1))
    elif [ "$(vcmp "$now" "$was")" -lt 0 ]; then
        echo "VERSION_WENT_BACK $id v$was → v$now（号只会往前走，退回去商店会把新版当旧的）"
        back=$((back + 1))
    else
        echo "BUMPED $id v$was → v$now，改了 $changed 个文件"
    fi
done

echo "GATE_BASELINE=$BASE GATE_PLUGINS=$total NEW_SINCE_BASE=$new UNCHANGED=$quiet NEEDS_BUMP=$needs WENT_BACK=$back"
if [ "$needs" -gt 0 ] || [ "$back" -gt 0 ]; then
    echo "闸红：改了源码的那颗自己把 Version 末位 +1（接口或行为大改就自己进位），再跑一遍"
    exit 1
fi
echo "闸绿：$total 颗里没有一个「源码改了号没动」的"
