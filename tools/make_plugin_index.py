#!/usr/bin/env python3
"""make_plugin_index.py —— 生成插件商店读的那份清单，和 core **同一次构建、同一个提交**出去。

为什么这道搬回主仓库（2026-10-20 他报"磁盘清理闪退"+"注册表/开机分析点不开"，根因都在这）：
商店原来读的是 `mateo-ki/ltool_plugin` 仓库 main 上手工提交的 `dist/`：
  ① 那批 dll 是**人把本机构建产物拷进 git** 的，最后一次 10-16，而 core 之后一直没停；
  ② 那个仓库的 `plugins/` 是主仓库源码的一份**分叉**（diff 实测：record 少整个 WindowPreview，
     disk/registry/startup/capture/clipboard/language/user 全不同）；
  ③ 于是"发一次商店"和"发一次 core"在物理上就是两件事 —— 漂是必然，不漂是运气。
搬到这里只剩一条流水线：CI 在同一次构建里编出 ltoolcore.dll + 19 颗插件 dll，
本脚本从**那批产物**现算 sha256/字节数，连 index.json 一起当**这次发版的资产**发出去。
商店读 `releases/latest/download/…`，拿到的就是和最新 core 一起编出来的那一套。

sha256 和字节数只能从产物来（手抄的那份第一次重编就过期，装之前还要按它校验）；
名字/描述/关键词/Api/Version 从各插件目录的 plugin.json 读，这里一份都不重写。

用法（主仓库根）：
  python tools/make_plugin_index.py --build build/Release/ltool-plugins --out installer/plugins-index.json
      [--base-url https://github.com/mateo-ki/ltool/releases/latest/download]
结论行按纯 ASCII 打：INDEX_PLUGINS= / INDEX_BAD=（缺产物就非 0，构建没成功就别出清单）
"""
import argparse
import glob
import hashlib
import io
import json
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
# 产物前缀按**这次发版的标签**算，不用 `releases/latest`：
# 标签就是 "v" + ltool.iss 那个版本号（CI 有一步专门验它对得上），而商店那边读的是
# 「我自己这版 core 配套的那一份」—— 两边写同一个式子，天然对齐。
# latest 有两个坑：预发版（标签带 -beta.N）不会被 latest 回，而且 latest 会跟着最新走，
# 老 core 拿到新 dll —— 那正是这次要根治的事。
RELEASE_BASE = 'https://github.com/mateo-ki/ltool/releases/download'


def sha256_of(path):
    h = hashlib.sha256()
    with open(path, 'rb') as f:
        for blk in iter(lambda: f.read(1 << 20), b''):
            h.update(blk)
    return h.hexdigest()


def read(path):
    return io.open(path, encoding='utf-8', errors='replace').read()


def core_label():
    """版本号唯一来源还是 ltool.iss，接口号是头里那个。'来自 ltool 1.3.0（插件接口 API 4）'"""
    ver = re.search(r'#define MyAppVersion "([^"]+)"', read(os.path.join(ROOT, 'ltool.iss'))).group(1)
    api = int(re.search(r'#define\s+LTOOL_PLUGIN_API_VERSION\s+(\d+)',
                        read(os.path.join(ROOT, 'src', 'ltool-plugin.h'))).group(1))
    return ver, api, '来自 ltool %s（插件接口 API %d）' % (ver, api)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--build', required=True, help='编好的 dll 所在目录')
    ap.add_argument('--src', default=os.path.join(ROOT, 'plugins'))
    ap.add_argument('--out', required=True)
    ap.add_argument('--base-url', default='', help='产物下载前缀；留空 = 本次发版标签那一档')
    a = ap.parse_args()

    ver, api_now, core = core_label()
    base = a.base_url.strip() or '%s/v%s' % (RELEASE_BASE, ver)
    rows = []
    problems = []
    for pj in sorted(glob.glob(os.path.join(a.src, '*', 'plugin.json'))):
        meta = json.loads(read(pj))
        name = 'ltool-%s.dll' % meta['Id']
        dll = os.path.join(a.build, name)
        if not os.path.exists(dll):
            problems.append('%s：有 plugin.json，产物里没有 %s —— 构建没成功就别出清单'
                            % (meta['Id'], name))
            continue
        # plugin.json 里那个 Api 是插件自报的接口号，宿主拿它放行；对不上头里的数就是写错了
        if int(meta.get('Api', -1)) != api_now:
            problems.append('%s：plugin.json 写的 Api=%s，头里是 %d'
                            % (meta['Id'], meta.get('Api'), api_now))
            continue
        row = {'id': meta['Id'], 'name': meta['Name'], 'desc': meta['Desc'],
               'api': meta['Api'], 'keywords': meta.get('Keywords', []),
               'actions': meta.get('Actions', []),
               'version': meta['Version'], 'color': meta.get('Color', ''),
               'file': name, 'bytes': os.path.getsize(dll), 'sha256': sha256_of(dll)}
        row['url'] = base.rstrip('/') + '/' + name
        rows.append(row)

    if problems:
        for p in problems:
            print('FAIL ' + p)
        print('INDEX_PLUGINS=%d' % len(rows))
        print('INDEX_BAD=%d' % len(problems))
        return 1

    doc = {'schema': 1, 'core': core, 'plugins': rows}
    out = os.path.abspath(a.out)
    os.makedirs(os.path.dirname(out), exist_ok=True)
    io.open(out, 'w', encoding='utf-8').write(json.dumps(doc, ensure_ascii=False, indent=1))
    print('清单：%d 颗 -> %s（%s）' % (len(rows), out, core))
    for r in rows:
        print('  %-9s v%-7s api=%s %8d B  %s' % (r['id'], r['version'], r['api'],
                                                 r['bytes'], r['sha256'][:12]))
    print('INDEX_PLUGINS=%d' % len(rows))
    print('INDEX_BAD=0')
    return 0


if __name__ == '__main__':
    if hasattr(sys.stdout, 'reconfigure'):
        sys.stdout.reconfigure(encoding='utf-8', errors='replace')
    sys.exit(main())
