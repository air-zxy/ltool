#!/usr/bin/env python
# -*- coding: utf-8 -*-
"""生成 src/pinyin_data.h —— 汉字 → 拼音（去声调、多音字全留）的紧凑表。

为什么要脚本：这张表 2 万多行，手打必出错（本项目有过"1042 个表常量手抄丢字符"的教训）。
数据源：pypinyin 0.55.0 的 pypinyin/pinyin_dict.json（wheel 解出来的那份，不动用户环境）。

跑法（本机 python 没有 CA 库，所以下载一律走 curl）：
    curl -s -o build/verify/pypinyin.json https://pypi.org/pypi/pypinyin/json
    # 从里面挑 bdist_wheel 的 url，下到 build/verify/pypkg/，用 zipfile 解开
    python tools/genpinyin.py
"""
import json
import os
import sys
import unicodedata

SRC = os.path.join("build", "verify", "pypkg", "pypinyin", "pinyin_dict.json")
OUT = os.path.join("src", "pinyin_data.h")
# 只收这段：扩展 A + 基本区，覆盖 Windows 上能见到的中文名字；其余（生僻平面字、
# 兼容表意文字）不进表，缺字的表现只是"那个字拼音搜不到"，不会误匹配。
LO, HI = 0x3400, 0x9FFF

# ü 系要先手工换成 v：NFD 拆声调时会把元音上那两点一起当 combining mark 剥掉，
# 于是 lǜ → lu（错），必须先变成 lv 再拆。
UMLAUT = {"ǖ": "v", "Ǘ": "v", "ǘ": "v", "Ǚ": "v", "ǚ": "v", "Ǜ": "v", "ü": "v", "Ü": "V"}


def toneless(syl):
    for k, v in UMLAUT.items():
        syl = syl.replace(k, v)
    out = "".join(c for c in unicodedata.normalize("NFD", syl)
                  if unicodedata.category(c) != "Mn")
    return out.lower()


def main():
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    if not os.path.exists(SRC):
        print("找不到 %s —— 先按文件头的说明把 pypinyin 的 wheel 解到 build/verify/pypkg" % SRC)
        return 1
    raw = json.load(open(SRC, encoding="utf-8"))
    rows, bad, kept = [], 0, 0
    for cp in range(LO, HI + 1):
        v = raw.get(str(cp))
        if not v:
            rows.append("")
            continue
        seen, sylls = set(), []
        for part in v.split(","):
            t = toneless(part.strip())
            # 只收纯小写字母的音节，出现数字/符号说明这条读法不该进表
            if not t or not t.isascii() or not t.isalpha():
                continue
            if t not in seen:
                seen.add(t)
                sylls.append(t)
        if not sylls:
            bad += 1
            rows.append("")
            continue
        kept += 1
        rows.append(",".join(sylls))
    blob = "\n".join(rows)
    # MSVC 单个字符串字面量上限 65534 字节（超了就 C2026"字符串太大，已截断尾部字符"，
    # 而且那是**警告式截断**，编出来的表是残的）—— 所以切成多块，每块都在整行边界断开，
    # 用的时候先拼起来再按行号取。
    chunks, cur = [], []
    for row in rows:
        cur.append(row)
        if sum(len(c) + 1 for c in cur) >= 4000:
            chunks.append("\n".join(cur))
            cur = []
    if cur:
        chunks.append("\n".join(cur))
    # 拼回去必须和 blob 一模一样：块与块之间补回被断开的那个换行
    assert "\n".join(chunks) == blob, "分块之后拼不回去了"
    with open(OUT, "w", encoding="utf-8", newline="\n") as f:
        f.write("// 汉字 → 拼音表（去声调，多音字的读音全留）。由 tools/genpinyin.py 从\n"
                "// pypinyin 0.55.0 的 pinyin_dict.json 生成，**别手改**，要改就改生成器再重跑。\n"
                "//\n"
                "// 格式：按 U+%04X..U+%04X 顺序一行一个码点，行号 = 码点 - 0x%X；\n"
                "// 行内是逗号分隔的音节（已小写），空行 = 这个码点没有数据。\n"
                "// 因为 MSVC 的 65534 字节字面量上限，表被切成 %d 块，用之前先拼起来。\n"
                "// 收录 %d 个字，%d 个码点没有可用读法（生僻/异体）。\n"
                "#pragma once\n\n" % (LO, HI, LO, len(chunks), kept, bad)
                + "namespace Pinyin {\n\n"
                + "inline constexpr unsigned kPinyinFirst = 0x%X;\n" % LO
                + "inline constexpr unsigned kPinyinLast = 0x%X;\n" % HI
                + "inline constexpr unsigned kPinyinRowCount = 0x%X; // last - first + 1\n\n" % (HI - LO + 1)
                # 行 i = 码点 (0x3400 + i) 的读法；块与块之间那个换行由拼接方补回来，
                # 所以"数出来的行数"必须等于 kPinyinRowCount，Pinyin.cpp 会当场核对。
                + "inline constexpr const char* kPinyinChunks[] = {\n")
        for c in chunks:
            # 逗号必须有：少了它三块会被当成相邻字面量拼成**一个** 140KB 的字面量，
            # 于是又撞上 C2026 那个上限（实测踩过）
            f.write('R"PY(%s)PY",\n' % c)
        f.write("};\n\n"
                "inline constexpr unsigned kPinyinChunkCount = %d;\n\n" % len(chunks)
                + "} // namespace Pinyin\n")
    print("写出 %s：%d 字有读法，%d 码点空，%d 行 / %d 块（%d 字节）"
          % (OUT, kept, bad, len(rows), len(chunks), len(blob.encode("utf-8"))))
    return 0


if __name__ == "__main__":
    sys.exit(main())
