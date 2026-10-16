#!/usr/bin/env python
# -*- coding: utf-8 -*-
"""abi_check.py —— 拿一份 ltoolcore.dll 去对一批插件 dll 的**导入表**，把对不上的符号点名列出。

为什么要它（2026-10-16 实测踩过）：core 是用 WINDOWS_EXPORT_ALL_SYMBOLS 自动导出的，
插件 dll 在链接期把这些符号名写死进了自己的导入表。于是 core 里**删掉一个函数名**
= 已发布的那批 dll 当场 LoadLibrary 失败，而且报错只有"加载失败"、不给缺哪个符号。
那天删了 Utils::launch / revealInExplorer / expandEnv 三个名字，装好的那份里 9 颗
商店 dll 全起不来 —— 本机 build 树 221 条判据一条都没红（那些 dll 是刚编的）。
所以这条检查必须在"发 core 之前"跑，而不是在事故之后回忆。

跑法：
    python tools/abi_check.py <ltoolcore.dll> <插件目录或若干 dll...>
退出码 = 缺失的符号种数（0 = 都能加载）。
"""
import glob
import os
import struct
import sys


def sections(f, pe, opt_size):
    out = []
    p = pe + 24 + opt_size
    for _ in range(struct.unpack_from('<H', f, pe + 6)[0]):
        vsz, va, rawsz, praw = struct.unpack_from('<IIII', f, p + 8)
        out.append((va, max(vsz, rawsz), praw))
        p += 40
    return out


def rva_to_off(sec, rva):
    for va, sz, praw in sec:
        if va <= rva < va + sz:
            return praw + (rva - va)
    return None


def read_cstr(f, off):
    end = f.index(b'\x00', off)
    return f[off:end].decode('latin1')


def imports_of(path):
    """返回 {被导入的 dll 名: set(符号名)}；序数导入按 '#<n>' 记。"""
    f = open(path, 'rb').read()
    if f[:2] != b'MZ':
        return {}
    pe = struct.unpack_from('<I', f, 0x3c)[0]
    magic = struct.unpack_from('<H', f, pe + 24)[0]
    opt = pe + 24
    dd = opt + (112 if magic == 0x20b else 96)
    sec = sections(f, pe, struct.unpack_from('<H', f, pe + 20)[0])
    imp_rva = struct.unpack_from('<I', f, dd + 8)[0]      # 数据目录第 1 项 = Import
    imp_off = rva_to_off(sec, imp_rva) if imp_rva else None
    out = {}
    if imp_off is None:
        return out
    i = 0
    while True:
        d = imp_off + i * 20
        orig, _ts, _fc, name_rva, first = struct.unpack_from('<IIIII', f, d)
        if name_rva == 0 and orig == 0 and first == 0:
            break
        dll = read_cstr(f, rva_to_off(sec, name_rva)).lower()
        thunks_rva = orig or first
        thunks = rva_to_off(sec, thunks_rva)
        names = out.setdefault(dll, set())
        j = 0
        while thunks is not None:
            val = struct.unpack_from('<Q', f, thunks + j * 8)[0]
            if val == 0:
                break
            if val & (1 << 63):
                names.add('#%d' % (val & 0xFFFF))
            else:
                nb = rva_to_off(sec, val & 0x7FFFFFFFFFFFFFFF)
                names.add(read_cstr(f, nb + 2))
            j += 1
        i += 1
    return out


def exports_of(path):
    f = open(path, 'rb').read()
    pe = struct.unpack_from('<I', f, 0x3c)[0]
    magic = struct.unpack_from('<H', f, pe + 24)[0]
    opt = pe + 24
    dd = opt + (112 if magic == 0x20b else 96)
    sec = sections(f, pe, struct.unpack_from('<H', f, pe + 20)[0])
    exp_rva = struct.unpack_from('<I', f, dd)[0]
    eo = rva_to_off(sec, exp_rva)
    if eo is None:
        return set()
    nnames, _ordbase = struct.unpack_from('<II', f, eo + 20)
    addrs_rva, names_rva, ords_rva = struct.unpack_from('<III', f, eo + 28)
    ao, no, npr = rva_to_off(sec, addrs_rva), rva_to_off(sec, names_rva), rva_to_off(sec, ords_rva)
    out = set()
    for i in range(nnames):
        nrva = struct.unpack_from('<I', f, no + 4 * i)[0]
        ordv = struct.unpack_from('<H', f, npr + 2 * i)[0]
        arva = struct.unpack_from('<I', f, ao + 4 * ordv)[0]
        name = read_cstr(f, rva_to_off(sec, nrva))
        # 前向声明（导出名指向另一个 dll 的符号）没有本地地址可判，照算导出
        out.add(name)
        if arva == 0:
            continue
    return out


def main(argv):
    if len(argv) < 3:
        sys.exit(__doc__)
    core = argv[1]
    if not os.path.exists(core):
        sys.exit('core 不在：%s' % core)
    dlls = []
    for a in argv[2:]:
        if os.path.isdir(a):
            dlls += sorted(glob.glob(os.path.join(a, '*.dll')))
        else:
            dlls.append(a)
    dlls = [d for d in dlls if os.path.basename(d).lower() != os.path.basename(core).lower()]
    core_name = os.path.basename(core).lower()
    have = exports_of(core)
    print('core %s：%d 个导出符号' % (os.path.basename(core), len(have)))
    missing_total = 0
    for d in dlls:
        need = imports_of(d).get(core_name, set())
        if not need:
            print('  %-28s 不 import core（独立 dll，跳过）' % os.path.basename(d))
            continue
        miss = sorted(need - have)
        missing_total += len(miss)
        flag = 'OK ' if not miss else '坏 '
        print('  %s%-28s import core %d 个符号，缺 %d' % (flag, os.path.basename(d), len(need), len(miss)))
        if miss:
            print('  BAD %s %d' % (os.path.basename(d), len(miss)))   # 同一件事的 ASCII 版，给脚本数
        for m in miss:
            print('      缺：%s' % m)
    print('—— 合计 %d 个符号对不上：%s' % (
        missing_total, '这批插件在这份 core 上都能加载' if missing_total == 0 else '**换上去就是加载失败**'))
    # 再打一行纯 ASCII 的机器可读结论：PowerShell 那边按中文行去数是数不出来的
    # （控制台默认 GBK，python 的 UTF-8 中文到了它嘴里就是乱码，正则匹配 0 条 ——
    #  实测就这么让交付闭环打出"缺 0 个符号"，而它上面刚列了 7 个）。
    print('MISSING_SYMBOLS=%d' % missing_total)
    return missing_total


if __name__ == '__main__':
    sys.exit(0 if main(sys.argv) == 0 else 1)
