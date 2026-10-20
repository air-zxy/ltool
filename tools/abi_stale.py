#!/usr/bin/env python3
"""abi_stale.py —— 每颗插件 dll 是在"哪一笔改动之前"编的？比 core 那边动过它吃的头还早，就是陈的。

为什么还要这一道（2026-10-20 他报"软件在磁盘清理闪退了"查出来的）：
`tools/abi_check.py` 只比**导出名字**。名字对得上 => 它报"缺 0"、`LoadLibrary` 也真的成功，
可 `struct DiskScan::Tree` 在 10-17 加了两个 QString（`engine`/`engineNote`）之后**大小和字段偏移全变了**，
10-16 编的那颗 `ltool-disk.dll` 还按老偏移读它 —— 读出来的是垃圾指针，
于是引用计数和虚调用打在垃圾上，0xc0000005。事件日志那句
"故障模块 ltool-disk.dll / 偏移 0x706a" 反汇编出来正是 `mov (%rcx),%rax; call *0x20(%rax)`。

**所以"名字齐"不等于"能一起跑"。** 这颗闸数的不是符号，是**时间**：
  dll 的 PE 编译时刻  vs  core 里它 include 的那些头最后一次被改的提交时刻。
后者更新 ⇒ 这颗 dll 是旧的，不管它导入的名字有多齐。

跑法：python tools/abi_stale.py <插件目录> [<插件目录> ...]
      （典型两个都跑：装好的那棵 + 商店 dist 那棵）
退出码 = 判定为陈的颗数。结论行按纯 ASCII 打：STALE_GATE_PLUGINS= / STALE_GATE_BAD=
"""
import glob
import os
import re
import struct
import subprocess
import sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def pe_time(path):
    """dll/exe 的 PE 时间戳（编译时刻），UTC naive。拿不到返回 None。"""
    with open(path, 'rb') as f:
        head = f.read(0x400)
    if len(head) < 0x40 or head[:2] != b'MZ':
        return None
    pe = struct.unpack_from('<I', head, 0x3c)[0]
    f = open(path, 'rb')
    f.seek(pe + 8)
    raw = struct.unpack('<I', f.read(4))[0]
    f.close()
    if raw == 0:
        return None
    # deterministic 构建会写 0x1970-01-01，那不是"什么时候编的"，别拿它比
    if raw < 1000000000:
        return None
    return raw


def git(*args):
    """跑 git 拿文本。**编码必须写死 utf-8**：这台机器的 python 默认按 GBK 解，
    而 diff 里全是中文注释 —— 第一版就是被这个异常吞掉，静默变成"没命中"、整道闸假绿。
    非 0 退出码一律抛出来，不许当成"没有差异"。"""
    r = subprocess.run(['git'] + list(args), cwd=REPO, capture_output=True,
                       encoding='utf-8', errors='replace')
    if r.returncode != 0:
        raise RuntimeError(f'git {args[0]} 退出码 {r.returncode}: '
                           f'{(r.stderr or r.stdout or "").strip()[:200]}')
    return r.stdout


def headers_of_plugin_source(plugin_dir):
    """这颗插件的源码 include 了哪些 core 头（"X.h" 形式，不含它自己目录里的）。"""
    found = set()
    for root, _, files in os.walk(plugin_dir):
        for name in files:
            if not name.endswith(('.cpp', '.h')):
                continue
            try:
                text = open(os.path.join(root, name), encoding='utf-8',
                            errors='replace').read()
            except OSError:
                continue
            for inc in re.findall(r'#include\s+"([\w/.+-]+\.h)"', text):
                cand = os.path.normpath(os.path.join(REPO, 'src', inc))
                if os.path.isfile(cand):
                    found.add(os.path.relpath(cand, REPO).replace('\\', '/'))
                else:
                    cand2 = os.path.normpath(os.path.join(REPO, 'src', inc))
                    if cand2.startswith(os.path.join(REPO, 'src')) and os.path.isfile(cand2):
                        found.add(os.path.relpath(cand2, REPO).replace('\\', '/'))
    return sorted(found)


def fmt(ts):
    import datetime
    return datetime.datetime.utcfromtimestamp(ts).strftime('%m-%d %H:%M') if ts else '?'


# 什么才算"动了布局"。第一版我只要"这头在这颗 dll 之后被改过"就报陈 —— 33 颗里报了 31 颗，
# 全是 `Utils.h` 那种只加函数声明的改动，等于没信号。收窄成两类真会改内存形状的：
#   ① 加/删数据成员：形如 `QString engine;`、`qint64 enumMs = 0;` —— 关键是**行里没有 `(`**，
#      有 `(` 的是函数声明（`bool mftUsable(const QString&, ...)`），函数进出不改偏移；
#   ② 加/删 `virtual`：vtable 槽位会挪，`call *0x20(%rax)` 打错的就是这种。
MEMBER_RE = re.compile(r'^[+-]\s*[^;()]*\b\w+\s*(=[^;]*)?;\s*$')
VIRTUAL_RE = re.compile(r'^[+-].*\bvirtual\b')


def layout_lines(diff_text):
    """一段 unified diff 里"动了布局"的行。判据只有这一份（core_abi_gate.py 也 import 它，
    不在第二个文件里抄一遍 —— 抄了就两份答案，迟早漂）。"""
    hits = []
    for line in diff_text.splitlines():
        if not line.startswith(('+', '-')) or line.startswith(('+++', '---')):
            continue
        body = line[1:]
        if not body.strip() or body.strip().startswith(('//', '*', '/*')):
            continue
        # 括号两头都不许出现：`bool async = false);` 这种是**函数签名的续行**，
        # 只查 `(` 会把它当成数据成员（第一版就是这么误报 Utils.h 的）。
        # `static` / `constexpr` 的成员**不占对象空间**（`static constexpr int kDefaultKeepDays = 7;`
        # 改了也不会让 `Tree` 变大），把它们排掉，不然剪贴板那颗会被一条无关的常量改动误伤。
        plain = body.strip()
        if plain.startswith(('static ', 'constexpr ', 'inline ', 'static constexpr',
                             'inline constexpr')):
            continue
        # `class QProcess;` 这种是**前向声明**，一个字节都不进对象；
        # 真的数据成员一定写成 `类型 名字;`，不会以 class/struct 开头（2026-10-20 平台那几刀
        # 把 Launch.h 建出来时，这道闸被它误报过一行）。
        if re.match(r'^(class|struct|union|enum)\s+\w+\s*(final\s*)?;$', plain):
            continue
        if VIRTUAL_RE.match(line) or ('(' not in line and ')' not in line
                                      and MEMBER_RE.match(line)):
            hits.append(body.strip())
    return hits


def layout_touched(header, built_ts):
    """这颗头在 built_ts 之后有没有"动布局"的改动。返回命中的那几行（空 = 没动）。"""
    base = git('rev-list', '-1', '--before=' + str(int(built_ts)), 'HEAD', '--', header).strip()
    if not base:
        return []
    return layout_lines(git('diff', base, 'HEAD', '--unified=0', '--', header))


def main():
    dirs = sys.argv[1:]
    if not dirs:
        print('用法：python tools/abi_stale.py <插件目录> [<插件目录> ...]')
        return 2
    bad = total = 0
    for d in dirs:
        dlls = sorted(glob.glob(os.path.join(d, 'ltool-*.dll')))
        if not dlls:
            print(f'  （{d} 里没有 ltool-*.dll，跳过）')
            continue
        print(f'== {d}  {len(dlls)} 颗')
        for dll in dlls:
            pid = os.path.basename(dll)[len('ltool-'):-len('.dll')]
            src = os.path.join(REPO, 'plugins', pid)
            built = pe_time(dll)
            if built is None:
                print(f'  ?   {pid:<12} 读不到 PE 时间戳（不是常规构建？）')
                continue
            total += 1
            if not os.path.isdir(src):
                print(f'  跳过 {pid:<12} 仓库里没有 plugins/{pid}，没法知道它吃哪些头')
                continue
            stale = []
            for h in headers_of_plugin_source(src):
                touched = layout_touched(h, built)
                if touched:
                    stale.append((h, touched))
            if stale:
                bad += 1
                print(f'  陈  {pid:<12} 编于 {fmt(built)}，之后这些头**动了布局**：')
                for h, lines in stale:
                    print(f'      {os.path.basename(h)}: '
                          + '；'.join(lines[:6]) + ('…' if len(lines) > 6 else ''))
                print('      -> 这颗 dll 里的字段偏移/vtable 槽是按改动**之前**编的，'
                      '读 core 返回的结构体就是错位 —— 名字齐也救不了')
            else:
                print(f'  OK  {pid:<12} 编于 {fmt(built)}，它吃的头之后没有动布局的改动')
    print(f'STALE_GATE_PLUGINS={total}')
    print(f'STALE_GATE_BAD={bad}')
    return bad


if __name__ == '__main__':
    sys.exit(main())
