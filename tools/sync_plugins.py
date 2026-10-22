"""sync_plugins.py —— 把主仓库 plugins/ 的每一颗同步进插件仓库（颗数从主仓库现算，#121）。

为什么要有脚本而不是手抄：这两棵树里同名文件必须**逐字节一致**（插件仓库那份是编译对象，
差一个换行都会让人怀疑"是不是我复制的时候改了什么"）。跑完自己报每颗加了几份、改了几份、
一致几份，剩下的差异（比如插件仓库独有的 CMakeLists.txt）单独点名。

用法（在主仓库根）：
    python tools/sync_plugins.py --check            # 只报告，不写
    python tools/sync_plugins.py --apply            # 真同步
    python tools/sync_plugins.py --apply --dest D:/project/cpp/ltool_plugin
"""
import argparse
import io
import os
import re
import shutil
import sys

SRC_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

CMAKELISTS = """# ltool 插件：{id}
#
# 源文件用 GLOB 而不是手抄清单 —— 这个目录就是这个插件的全部本体，加一个 .cpp 不该再动第二处。
# 类名写死在这儿（和主仓库那份 ltool_add_tool({id} {cls} ...) 一致）：它是插件身份的一部分。
file(GLOB srcs CONFIGURE_DEPENDS ${{CMAKE_CURRENT_SOURCE_DIR}}/*.cpp ${{CMAKE_CURRENT_SOURCE_DIR}}/plugin.json)
ltool_add_plugin({id} {cls} ${{srcs}})
"""


def class_map():
    """从主仓库 CMakeLists 里问每颗的类名 —— 那边是唯一的登记处，这里再抄一份迟早分叉。"""
    text = io.open(os.path.join(SRC_ROOT, 'CMakeLists.txt'), encoding='utf-8').read()
    out = {}
    for m in re.finditer(r'ltool_add_tool\(\s*(\w+)\s+(\w+Plugin)\b', text):
        out[m.group(1)] = m.group(2)
    return out


def files_of(d):
    if not os.path.isdir(d):
        return []
    return sorted(f for f in os.listdir(d)
                  if f.endswith(('.cpp', '.h')) or f == 'plugin.json')


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--dest', default='D:/project/cpp/ltool_plugin')
    ap.add_argument('--apply', action='store_true')
    ap.add_argument('--check', action='store_true')
    a = ap.parse_args()
    cmap = class_map()
    src_base = os.path.join(SRC_ROOT, 'plugins')
    dst_base = os.path.join(a.dest, 'plugins')
    ids = sorted(d for d in os.listdir(src_base) if os.path.isdir(os.path.join(src_base, d)))
    print('主仓库里的插件=%d 颗，CMakeLists 里登记了类名的=%d 颗' % (len(ids), len(cmap)))
    added = updated = same = 0
    problems = []
    for pid in ids:
        if pid not in cmap:
            problems.append('%s：主仓库 CMakeLists 里找不到它那颗 ltool_add_tool（类名问不出来）' % pid)
            continue
        src = os.path.join(src_base, pid)
        dst = os.path.join(dst_base, pid)
        names = files_of(src)
        if not names:
            problems.append('%s：主仓库那目录里没有 .cpp/.h/plugin.json' % pid)
            continue
        new_dir = not os.path.isdir(dst)
        if a.apply and new_dir:
            os.makedirs(dst)
        for f in names:
            s, t = os.path.join(src, f), os.path.join(dst, f)
            sb = io.open(s, 'rb').read()
            tb = io.open(t, 'rb').read() if os.path.exists(t) else None
            if tb == sb:
                same += 1
                continue
            if a.apply:
                io.open(t, 'wb').write(sb)
            if tb is None:
                added += 1
            else:
                updated += 1
        cl = os.path.join(dst, 'CMakeLists.txt')
        if a.apply and not os.path.exists(cl):
            io.open(cl, 'w', encoding='utf-8', newline='').write(
                CMAKELISTS.format(id=pid, cls=cmap[pid]))
            print('  新写 %s/CMakeLists.txt（类名 %s）' % (pid, cmap[pid]))
        elif not os.path.exists(cl):
            print('  缺 %s/CMakeLists.txt（--apply 才会写）' % pid)
        if new_dir:
            print('  %s：这一棵在插件仓库里原来**没有**（%d 份文件）' % (pid, len(names)))
    # 反向：插件仓库里有没有主仓库已经不存在的插件目录（那是"删了没删干净"的形状）
    extra = sorted(d for d in os.listdir(dst_base)
                   if os.path.isdir(os.path.join(dst_base, d)) and d not in ids) if os.path.isdir(dst_base) else []
    print('文件：新放=%d 改动=%d 已经一致=%d' % (added, updated, same))
    if extra:
        print('插件仓库里多出来的（主仓库没有这些 id）：' + '、'.join(extra))
    for p in problems:
        print('PROBLEM ' + p)
    print('SYNC_APPLIED=%s' % ('yes' if a.apply else 'no（这是 --check，一个字节都没写）'))
    return 1 if problems else 0


if __name__ == '__main__':
    sys.exit(main())
