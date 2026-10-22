#!/usr/bin/env python3
"""core_abi_gate.py —— core 的公开面变了，插件接口的那个号就必须升。

为什么要有它（2026-10-20 磁盘清理闪退查出来的第二层）：
`LTOOL_PLUGIN_API_VERSION` 从 3 起就没动过，可 10-16 之后 core 公开的头里
`struct DiskScan::Tree` 多了两个 QString、`Utils::launch/revealInExplorer/trimWorkingSet`
搬进了 platform 缝。于是 10-16 编的老插件**照样通过所有版本闸**（它声明 Api 3、宿主也认 3），
`LoadLibrary` 成功，一按磁盘清理就 0xc0000005 —— 因为字段偏移是按改动之前编的。
闸只比名字（abi_check.py）和时间（abi_stale.py）都不够：**升号这件事本身没人盯着**。

这一道判的是"该不该升号"，判据复用 abi_stale 里那条"什么才算动了布局"：
    last = 把 #define LTOOL_PLUGIN_API_VERSION <当前值> 引进来的那一笔提交
    从 last 到**工作树**，core 公开头的闭包（tools/make_sdk.py 那份算法，不另写）里
    任何一处加/删数据成员或加/删 virtual ⇒ 红。
顺带盯另一半手抄的真相：19 份 plugins/*/plugin.json 里那个 "Api" 必须等于头里的数
（那个数是插件自报家门的来源，写错了宿主就按错的放行）。

跑法：python tools/core_abi_gate.py
结论行按纯 ASCII 打：ABI_API_NOW= / ABI_API_GATE_HITS= / ABI_API_META_BAD=
退出码 = 两类命中之和（0 = 公开面自上次升号以来没动布局，且 19 份 Api 都对得上）。
"""
import glob
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import abi_stale          # 复用 MEMBER_RE / VIRTUAL_RE 那条"动了布局"的判据
import make_sdk           # 复用"插件真实 include 的头的传递闭包"，别第二处抄

REPO = make_sdk.ROOT


def git(*args):
    return abi_stale.git(*args)


def api_now():
    text = make_sdk.read(os.path.join(REPO, 'src', 'ltool-plugin.h'))
    m = re.search(r'#define\s+LTOOL_PLUGIN_API_VERSION\s+(\d+)', text)
    if not m:
        raise SystemExit('src/ltool-plugin.h 里读不到 #define LTOOL_PLUGIN_API_VERSION <数字>')
    return int(m.group(1)), text


def api_at_head():
    """HEAD 那一份头里的接口号。和工作树不一致 = 这一轮正在升号。"""
    try:
        text = git('show', 'HEAD:src/ltool-plugin.h')
    except RuntimeError:
        return None
    m = re.search(r'#define\s+LTOOL_PLUGIN_API_VERSION\s+(\d+)', text)
    return int(m.group(1)) if m else None


def check_meta(api, hits=0):
    """19 份 plugin.json 里那个手抄的 Api 必须等于头里的数（它是插件自报家门的来源、
    宿主拿它放行；写错了就等于放行一颗对不上的）。结论行在这儿打，两条路径都要过它。"""
    meta_bad = []
    for json_path in sorted(glob.glob(os.path.join(REPO, 'plugins', '*', 'plugin.json'))):
        meta = make_sdk.read(json_path)
        m = re.search(r'"Api"\s*:\s*(\d+)', meta)
        got = m.group(1) if m else '没有这一键'
        if not m or int(got) != api:
            meta_bad.append((os.path.relpath(json_path, REPO).replace('\\', '/'), got))
    for p, got in meta_bad:
        print(f'  Api 对不上  {p}: 写的是 {got}，头里是 {api}')
    if meta_bad:
        print('  -> plugin.json 里那个 Api 是插件自报的接口号，和宿主比的就是它：手抄的一定要跟着头改。')
    print(f'ABI_API_NOW={api}')
    print(f'ABI_API_GATE_HITS={hits}')
    print(f'ABI_API_META_BAD={len(meta_bad)}')
    return hits + len(meta_bad)


def last_bump_commit(api):
    """把当前这个数写进去的那笔提交。
    -S 找的是"这串出现的次数变了"的提交，所以要连**值**一起搜：只搜 `#define ...API_VERSION`
    这种前缀，改值不改次数，永远找不到（第一版就是这么静默返回空、把闸变成装饰）。"""
    needle = f'#define LTOOL_PLUGIN_API_VERSION {api}'
    for line in git('log', '--format=%H', '-S', needle, '--', 'src/ltool-plugin.h').splitlines():
        if line.strip():
            return line.strip()
    return ''


def main():
    api, _ = api_now()
    heads = sorted(make_sdk.closure(make_sdk.plugin_includes()))
    bad_layout = []
    head_api = api_at_head()
    if head_api is not None and head_api != api:
        # 号在工作树里刚被升上来、还没提交：那"自上次定号以来的布局改动"正好就是这一批，
        # 归到新号这一档 —— 这是**通过**的状态，不是"量不到"。
        # 没有这一段的话升号那一轮必然先把闸跑红，而红的理由是"新号在历史里找不到"，
        # 下一个人只会以为脚本坏了（这道闸要是只能事后生效，人就学着把它跳过了）。
        print(f'工作树把接口号从 {head_api} 升到 {api}（还没提交）：'
              '之前动了布局的头都算升号之前那一批，归到新号这一档 —— 通过。')
        return check_meta(api)
    bump = last_bump_commit(api)
    if not bump:
        print(f'ABI_API_GATE_HITS=? 这一笔把 API={api} 写进来的提交不在历史里'
              '（头文件是刚建的、还是历史被压过？）—— 这道闸量不到，先当没通过')
        return 1
    print(f'上次把接口号定在 API {api} 的提交：{bump[:9]}，'
          f'从它到工作树查 {len(heads)} 个公开头有没有动布局')
    for h in heads:
        path = f'src/{h}'
        # 比到**工作树**不是比到 HEAD：没提交的那几行也是这几行，本地就该拦住
        diff = git('diff', bump, '--unified=0', '--', path)
        lines = abi_stale.layout_lines(diff)
        if lines:
            bad_layout.append((h, lines))

    for h, lines in bad_layout:
        print(f'  动过布局  {h}: ' + '；'.join(lines[:6]) + ('…' if len(lines) > 6 else ''))
    if bad_layout:
        print(f'  -> {len(bad_layout)} 个公开头自 API {api} 之后改了成员或虚函数：'
              '老插件里的字段偏移/vtable 槽是按改之前摆的。'
              f'把 LTOOL_PLUGIN_API_VERSION 从 {api} 升到 {api + 1}，'
              '同时重生成 SDK 快照、每一颗插件重编重发。')
    return check_meta(api, len(bad_layout))


if __name__ == '__main__':
    if hasattr(sys.stdout, 'reconfigure'):
        sys.stdout.reconfigure(encoding='utf-8', errors='replace')
    sys.exit(main())
