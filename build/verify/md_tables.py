# markdown 表格自查：表格行里**没被反斜杠转义的竖线**必须正好 3 个（一行两列）。
# 为什么专门查这个：单元格里写 `a|b|c`（比如 `--archivetest extract|make|probe`）会当场把表格断开，
# 而 GitHub 渲染出来只是那一行变成垃圾文字，review 时很容易看漏。
import io
import sys


def unescaped_pipes(line):
    n = 0
    for i, ch in enumerate(line):
        if ch == '|' and (i == 0 or line[i - 1] != chr(92)):
            n += 1
    return n


worst = 0
for f in sys.argv[1:]:
    lines = io.open(f, encoding='utf-8').read().split('\n')
    rows = [l for l in lines if l.startswith('| ') and not l.startswith('| ---')]
    bad = [(i + 1, l[:70]) for i, l in enumerate(lines)
           if l.startswith('| ') and not l.startswith('| ---') and unescaped_pipes(l) != 3]
    longest = max(len(l) for l in lines)
    print('%-18s 表格行 %3d ｜竖线不对 %d ｜最长行 %d 字' % (f, len(rows), len(bad), longest))
    for b in bad[:5]:
        print('    行', b[0], b[1])
    worst += len(bad)
sys.exit(1 if worst else 0)
