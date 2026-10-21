"""从 Actions 的日志 zip 里把「无界面回归」那一圈的每条旗标数出来。

为什么要它：报绿必须连"在几个对象上量的"一起报。本机离屏和 runner 上的条数**本来就不一样**
（有些旗标按环境点名跳过，比如 runner 上 --startuptest 那条 HKLM 是提权跑的），
而 --pinyin 那一节里"跳过"走的还是 `INFO 跳过 …` 这个形状、不是 `SKIP` 打头 ——
只 grep `^SKIP` 就会把"少了几条"看成"莫名少了"。

跑法：python build/verify/ci_log_pathcfg.py <那个 zip> [旗标，默认 --pathcfg]
"""
import sys
import zipfile

want_section = sys.argv[2] if len(sys.argv) > 2 else '--pathcfg'
z = zipfile.ZipFile(sys.argv[1])
for n in z.namelist():
    if '无界面回归' not in n:
        continue
    section = None
    counts = {}
    notes = {}
    for raw in z.read(n).decode('utf-8', 'replace').splitlines():
        line = raw.split('Z ', 1)[-1] if 'Z ' in raw else raw
        if line.startswith('=====') and line.endswith('====='):
            section = line.strip('= ').strip()
            counts.setdefault(section, [0, 0, 0, 0])
            notes.setdefault(section, [])
            continue
        if not section:
            continue
        if line.startswith('PASS') or line.startswith('ok '):
            # --cfgcheck 那一节的行首是 `ok`，不是 `PASS`：不认它的话那一栏会报 PASS=0，
            # 看着就像"这面旗标一个判据都没跑"（这就是被教训过的那种空转）。
            counts[section][0] += 1
        elif line.startswith('FAIL'):
            counts[section][1] += 1
            notes[section].append(line[:170])
        elif line.startswith('SKIP'):
            counts[section][2] += 1
            notes[section].append(line[:170])
        elif line.startswith('INFO'):
            counts[section][3] += 1
            if '跳过' in line or '冷启动' in line:
                notes[section].append(line[:170])
    for k in sorted(counts):
        p, f, s, i = counts[k]
        star = '  ←' if k == want_section else ''
        print('%-16s PASS=%-4d FAIL=%-2d SKIP=%-2d INFO=%-3d%s' % (k, p, f, s, i, star))
    print()
    for k in sorted(notes):
        for l in notes[k]:
            print(k, '|', l)
