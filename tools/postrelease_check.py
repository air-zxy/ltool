"""发完版对着**公开那一面**复验三件事（不看 CI 日志，直接打 GitHub 的公开 URL）：
1. Release 说明里有没有家目录/用户名 —— 用的就是发版说明那把同一颗图案；
2. 商店那条链是不是真活了：`releases/download/v<这一版>/plugins-index.json` 要 200，
   且条目不少于**主仓库现在插件的颗数**（颗数现算 —— 写死 19 的那版，加一颗插件就变假红/假绿）；
3. `sha256.txt` 里安装包那一行，和 Release 资产 API 报的 digest 对不对得上。

    python tools/postrelease_check.py               # 量"HEAD 上最近那个 tag"那一版
    python tools/postrelease_check.py --tag v1.4.1  # 点名量某一版
    python tools/postrelease_check.py 已经下好的.json  # 离线：拿那份 release JSON，不发请求

版本号以前钉死在 `v1.4.0`：发完 1.4.1 之后它还在复验上一版 —— 那是"绿了但量错了对象"，
所以这里改成从 `git describe --tags` 问出来，不在这儿抄第二份。
"""
import glob
import importlib.util
import json
import os
import subprocess
import sys
import time

here = os.path.dirname(os.path.abspath(__file__))
root = os.path.dirname(here)


def get(url, tries=4):
    """公开 URL 一律走 curl，不走 python 的 urllib。
    这台机上的 msys python 没有可信 CA 那份（`CERTIFICATE_VERIFY_FAILED: unable to get local
    issuer certificate`），而 GitHub 的 443 又时不时直接被掐（`远程主机强迫关闭了一个现有的连接`）——
    两个都是环境的事，不是"那条链没活"。curl 自带 CA 包，所以拿它量，并且重试几次。

    Windows 上还要多一颗：资产那一跳会 302 到 CDN 主机，schannel 拿不到吊销服务器就整条
    `rc=35 CRYPT_E_REVOCATION_OFFLINE` —— 直连和经代理都一样，而 api.github.com 那一跳没事，
    所以很容易被读成"链没活"。只在 Windows 上关吊销检查（证书链本身照样验）。"""
    revoke = ['--ssl-no-revoke'] if sys.platform == 'win32' else []
    last = ''
    for i in range(tries):
        r = subprocess.run(['curl', '-sSL', '--max-time', '40'] + revoke + ['-o', '-', url],
                           capture_output=True, text=True, encoding='utf-8', errors='replace')
        if r.returncode == 0 and r.stdout.strip():
            return r.stdout
        last = 'curl rc=%d %s' % (r.returncode, (r.stderr or '').strip()[:120])
        time.sleep(2 + 2 * i)
    raise RuntimeError(last)


def nearest_tag():
    r = subprocess.run(['git', 'describe', '--tags', '--abbrev=0'], cwd=root,
                       capture_output=True, text=True)
    return (r.stdout or '').strip()


def expected_plugins(tag=''):
    """那一版发出去时有几颗插件：**从那个 tag 数**，不从工作树数。
    第一版这里是现算工作树，于是拿 v1.4.1 那次发版（19 条）比今天（20 颗）报了一条 PROBLEM ——
    那是把"过去的产物"和"未来的树"放在一起比，量错了对象。tag 不在本地时退回现算并说清楚。"""
    if tag:
        r = subprocess.run(['git', 'ls-tree', '-r', '--name-only', tag, '--'], cwd=root,
                           capture_output=True, text=True)
        names = [l for l in (r.stdout or '').splitlines()
                 if l.startswith('plugins/') and l.endswith('/plugin.json')]
        if names:
            return len(names)
    return len(glob.glob(os.path.join(root, 'plugins', '*', 'plugin.json')))


spec = importlib.util.spec_from_file_location('mrn', os.path.join(here, 'make_release_notes.py'))
m = importlib.util.module_from_spec(spec)
spec.loader.exec_module(m)

argv = list(sys.argv[1:])
tag = ''
if '--tag' in argv:
    i = argv.index('--tag')
    tag = argv[i + 1]
    del argv[i:i + 2]

if argv:                                    # 离线：拿已经下好的那份 release JSON
    rel = json.load(open(argv[0], encoding='utf-8'))
    tag = tag or rel.get('tag_name', '')
else:
    tag = tag or nearest_tag()
    if not tag:
        print('PROBLEM 不知道要量哪一版（这里没有 tag，也没给 --tag）')
        sys.exit(1)
    rel = json.loads(get('https://api.github.com/repos/mateo-ki/ltool/releases/tags/' + tag))

want = expected_plugins(tag)
now = expected_plugins()
print('量的是 %s 那一版（那次发出去 %d 颗插件；工作树现在 %d 颗）' % (tag or '（无名）', want, now))
bad = 0

# 1 说明里不许有家目录/用户名
hits = m.USER_HOME_RE.findall(rel.get('body', '') or '')
print('说明里带家目录的行=', len(hits))
for h in hits[:3]:
    print('   残留:', h[:120])
bad += len(hits)

# 2 商店那条链真活了
url = 'https://github.com/mateo-ki/ltool/releases/download/%s/plugins-index.json' % tag
try:
    idx = json.loads(get(url))
    plugins = idx.get('plugins', idx if isinstance(idx, list) else [])
    print('商店清单：HTTP 200，条目=', len(plugins))
    if len(plugins) < want:
        print('   PROBLEM 清单只有 %d 条，而 %s 那一次构建有 %d 颗插件' % (len(plugins), tag, want))
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
