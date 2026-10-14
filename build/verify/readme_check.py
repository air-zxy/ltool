# 交付前自查（2026-10-15）：README 拆墙之后，① 表格里不能有没转义的竖线（一行只能有 3 个），
# ② 原文里每一段 ≥40 字的片段都必须还在新文件里 —— 搬版不等于删证据。
import io
import re
import sys

NEW = 'README.md'
OLD = sys.argv[1] if len(sys.argv) > 1 else None


def unescaped_pipes(line):
    n = 0
    for i, ch in enumerate(line):
        if ch == '|' and (i == 0 or line[i - 1] != '\\'):
            n += 1
    return n


lines = io.open(NEW, encoding='utf-8').read().split('\n')
bad = []
for i, l in enumerate(lines):
    if not l.startswith('| ') or l.startswith('| ---'):
        continue
    if unescaped_pipes(l) != 3:
        bad.append((i + 1, l[:90]))
print('表格行数=%d，竖线不对的：%d' % (sum(1 for l in lines if l.startswith('| ') and not l.startswith('| ---')), len(bad)))
for b in bad:
    print('  行', b[0], b[1])

if OLD:
    norm = lambda s: re.sub(r'\s+', ' ', s)
    old = norm(io.open(OLD, encoding='utf-8').read())
    new = norm(io.open(NEW, encoding='utf-8').read())
    frags = [f.strip() for f in re.findall(r'[^|]{40,}', old)]
    lost = [f for f in frags if f not in new]
    print('原文 ≥40 字片段 %d 段，新 README 里找不到的：%d' % (len(frags), len(lost)))
    for f in lost[:10]:
        print('  LOST', f[:90])
