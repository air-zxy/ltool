"""拿真历史（或直接拿生成好的那份文件）验一遍脱敏：发版说明里不许出现任何用户名/家目录。

    python tools/notes_history_leak_check.py                 # 默认 v1.3.3..HEAD
    python tools/notes_history_leak_check.py v1.3.3..v1.4.0  # 指定区间
    python tools/notes_history_leak_check.py --file release-notes.md   # CI 生成完之后量产物
"""
import importlib.util
import os
import sys

here = os.path.dirname(os.path.abspath(__file__))
spec = importlib.util.spec_from_file_location('mrn', os.path.join(here, 'make_release_notes.py'))
m = importlib.util.module_from_spec(spec)
spec.loader.exec_module(m)

# 判据用的图案**就是脱敏那一颗用的图案**（从 make_release_notes 里拿，不第二份）：
# 第一版这里自己写了一条更宽的（`C:\Users\` 后面接任意非空白），于是它连
# `C:\Users\<你的名字>\AppData` 这种**占位符**都判红 —— 一条真历史里合法的说明文字
# 把闸踩红了，而那正是"脱敏没处理、但也不该处理"的形状。
# 闸要判的是"有一个真用户名活着出来了"，不是"出现了 C:\Users 这几个字"。
HOME_RE = m.USER_HOME_RE

if len(sys.argv) >= 3 and sys.argv[1] == '--file':
    with open(sys.argv[2], encoding='utf-8') as f:
        blob = f.read()
    lines = blob.splitlines()
    src = '文件 ' + sys.argv[2]
else:
    cs = m.commits(sys.argv[1] if len(sys.argv) > 1 else 'v1.3.3..HEAD')
    lines = []
    for subject, body in cs:
        lines.append(subject)
        lines.extend(m.body_lines(body))
    blob = '\n'.join(lines)
    src = '区间 ' + (sys.argv[1] if len(sys.argv) > 1 else 'v1.3.3..HEAD')

# 只按"家目录那一段"的**形状**判，不写死任何一个用户名 ——
# 这个文件本身要进公开仓库，把谁的账号名抄进来就等于又漏了一次。
hits = HOME_RE.findall(blob)
print('来源=%s 行=%d 带家目录的行=%d' % (src, len(lines), len(hits)))
for h in hits[:5]:
    print('  残留:', h[:120])
print('NOTES_REALHISTORY_BAD=%d' % len(hits))
sys.exit(1 if hits else 0)

