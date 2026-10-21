"""拿真历史（或直接拿生成好的那份文件）验一遍脱敏：发版说明里不许出现任何用户名/家目录。

    python tools/notes_history_leak_check.py                 # 默认 v1.3.3..HEAD
    python tools/notes_history_leak_check.py v1.3.3..v1.4.0  # 指定区间
    python tools/notes_history_leak_check.py --file release-notes.md   # CI 生成完之后量产物
"""
import importlib.util
import os
import re
import sys

here = os.path.dirname(os.path.abspath(__file__))
spec = importlib.util.spec_from_file_location('mrn', os.path.join(here, 'make_release_notes.py'))
m = importlib.util.module_from_spec(spec)
spec.loader.exec_module(m)

HOME_RE = re.compile(r'[A-Za-z]:[\\/]{1,2}[Uu]sers[\\/]{1,2}\S+|(?:/[Uu]sers|/home)/[A-Za-z0-9._-]+')

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

