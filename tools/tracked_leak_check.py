"""git 真带走的每一张文件里，都不许活着一个**真**用户名（#129 那批探针就是这么漏的）。

    python tools/tracked_leak_check.py

为什么不是把 .gitignore 补上就完事：`.gitignore` 对**已跟踪**的文件不生效 ——
`build/verify/*.sh` 那条一直只挡了一半（挡住 .sh，挡不住 .ps1/.py/.js），
于是那 16 个一次性探针从 10 月就一直躺在公开仓库里，带的是真账号名。
所以这里立的是一条会红的闸，不是一条看起来像规则的注释。

图案和脱敏那颗**不共用**：`make_release_notes.USER_HOME_RE` 判的是"出现了家目录那种形状"，
它连 `C:/Users/x`、`zzname` 这种占位都算命中（这一轮扫下来形状命中 11 行、真用户名 0 行）。
闸要判的是"有一个不是占位的用户名活着出来了"，所以这里在它的基础上加一张占位白名单。
"""
import importlib.util
import os
import re
import subprocess
import sys

here = os.path.dirname(os.path.abspath(__file__))
spec = importlib.util.spec_from_file_location('mrn', os.path.join(here, 'make_release_notes.py'))
m = importlib.util.module_from_spec(spec)
spec.loader.exec_module(m)
HOME_RE = m.USER_HOME_RE

# 占位：这些"用户名"位置写的根本不是任何人的账号，出现在探针/文档/测试夹具里是合法的。
PLACEHOLDER = re.compile(r'^(x|X|zzname|someone|user|name|Public|<[^>]+>|\{[^}]+\}|\\?%[^%]+%\\?)$')
# 用户名那一段只吃"账号名能出现的字符"：把反引号、顿号也算进 token，文档里写一句
# 「占位（`x`、`zzname`）」就会被判成一个叫 `x`、`zzname` 的用户 —— 那条真的是我自己判红我自己。
NAME_RE = re.compile(r'(?:[Uu]sers|[Hh]ome)[/\\]+([A-Za-z0-9._][A-Za-z0-9._-]*)', re.M)


def is_placeholder(token: str) -> bool:
    if PLACEHOLDER.match(token):
        return True
    low = token.lower()
    return low.startswith('[') or low.startswith('~')   # 变量展开写法（$env:USERPROFILE 之类）不进白名单也一样不是用户名


def main() -> int:
    files = subprocess.run(['git', 'ls-files'], capture_output=True, text=True).stdout.splitlines()
    hits = []
    scanned = 0
    for f in files:
        try:
            blob = open(f, encoding='utf-8', errors='replace').read()
        except Exception:
            continue
        scanned += 1
        for ln, line in enumerate(blob.splitlines(), 1):
            if not HOME_RE.findall(line):
                continue
            for token in NAME_RE.findall(line):
                if not is_placeholder(token):
                    hits.append((f, ln, token))
    print('扫过 %d 张被 git 跟踪的文件（图案与脱敏那颗同源，占位白名单在这文件里）' % scanned)
    for f, ln, tok in hits[:10]:
        print('  残留: %s:%d  用户名=%s' % (f, ln, tok))
    print('TRACKED_LEAK_HITS=%d' % len(hits))
    return 1 if hits else 0


if __name__ == '__main__':
    sys.exit(main())
