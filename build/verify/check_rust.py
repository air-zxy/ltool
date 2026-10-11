import io
import re

# 复刻 DevEnv::fetchVersionsAsync 的解析规则，确认 Rust 版本能被列出来
pat = re.compile(r'^\s*v?[0-9]+(\.[0-9]+)+\s*$')
txt = io.open(r'D:\project\cpp\ltool\build\verify\rust_ver.txt', encoding='utf-8-sig').read()
vs = [l.strip() for l in txt.splitlines() if pat.match(l.strip())]
print('parsed versions =', len(vs))
print('first 6 =', vs[:6])
print('has local 1.97.1 =', '1.97.1' in vs)
