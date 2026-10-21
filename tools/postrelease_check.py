"""发完版对着**公开那一面**复验三件事（不看 CI 日志，直接打 GitHub 的公开 URL）：
1. Release 说明里有没有家目录/用户名 —— 用的就是发版说明那把同一颗图案；
2. 商店那条链是不是真活了：releases/download/v1.4.0/plugins-index.json 要 200 且能解出 19 条；
3. sha256.txt 里安装包那一行，和 Release 资产 API 报的 digest 对不对得上。
"""
import importlib.util
import json
import os
import subprocess
import sys
import time


def get(url, tries=4):
    """公开 URL 一律走 curl，不走 python 的 urllib。
    这台机上的 msys python 没有可信 CA 那份（`CERTIFICATE_VERIFY_FAILED: unable to get local
    issuer certificate`），而 GitHub 的 443 又时不时直接被掐（`远程主机强迫关闭了一个现有的连接`）——
    两个都是环境的事，不是"那条链没活"。curl 自带 CA 包，所以拿它量，并且重试几次。"""
    last = ''
    for i in range(tries):
        r = subprocess.run(['curl', '-sSL', '--max-time', '40', '-o', '-', url],
                           capture_output=True, text=True, encoding='utf-8', errors='replace')
        if r.returncode == 0 and r.stdout.strip():
            return r.stdout
        last = 'curl rc=%d %s' % (r.returncode, (r.stderr or '').strip()[:120])
        time.sleep(2 + 2 * i)
    raise RuntimeError(last)

here = os.path.dirname(os.path.abspath(__file__))
spec = importlib.util.spec_from_file_location('mrn', os.path.join(here, 'make_release_notes.py'))
m = importlib.util.module_from_spec(spec)
spec.loader.exec_module(m)

rel = json.load(open(sys.argv[1], encoding='utf-8')) if len(sys.argv) > 1 else \
    json.loads(get('https://api.github.com/repos/mateo-ki/ltool/releases/tags/v1.4.0'))
bad = 0

# 1 说明
hits = m.USER_HOME_RE.findall(rel.get('body', '') or '')
print('说明里带家目录的行=', len(hits))
for h in hits[:3]:
    print('   残留:', h[:120])
bad += len(hits)

# 2 商店那条链
url = 'https://github.com/mateo-ki/ltool/releases/download/v1.4.0/plugins-index.json'
try:
    idx = json.loads(get(url))
    plugins = idx.get('plugins', idx if isinstance(idx, list) else [])
    print('商店清单：HTTP 200，条目=', len(plugins))
    if len(plugins) < 19:
        print('   PROBLEM 条目少于 19 颗')
        bad += 1
except Exception as e:
    print('PROBLEM 商店那条链还是不通：', e)
    bad += 1

# 3 sha256 对账
setup = [a for a in rel.get('assets', []) if a['name'].endswith('-setup.exe')]
shapath = [a for a in rel.get('assets', []) if a['name'] == 'sha256.txt']
if setup and shapath:
    txt = get(shapath[0]['browser_download_url'])
    line = [l for l in txt.splitlines() if setup[0]['name'] in l]
    api = (setup[0].get('digest') or '').replace('sha256:', '')
    print('安装包=', setup[0]['name'])
    print('  sha256.txt 那行:', (line[0][:90] if line else '没找到'))
    print('  资产 API 的 digest:', api[:16], '...')
    if not line or api.lower() not in line[0].lower():
        print('   PROBLEM 两边对不上（说明里承诺了要对整批）')
        bad += 1
else:
    print('PROBLEM 缺 setup.exe 或 sha256.txt 资产')
    bad += 1

print('POSTRELEASE_BAD=%d' % bad)
sys.exit(1 if bad else 0)
