#!/usr/bin/env python3
r"""make_release_notes.py —— 生成 GitHub Release 那份说明（形状照 mateo-ki/vpn-parser 的发布说明）。

以前 CI 用的是 `gh release create --generate-notes`，GitHub 自己拼出来的是**裸提交标题列表**：
没有重点、没有原因、没有"哪颗文件是干什么的"。而这个仓库的提交信息本来就写着原因和实测数字
（一笔提交动辄几十行），全被 `--generate-notes` 丢了。这份脚本就是把它们重新摆成人看得懂的样子：

    # 🚀 ltool v<X> 发布说明 / Release Notes
    🎉 概述（几笔提交 + 前几条的重点）
    ## 🌟 本次改动        ← 一笔提交一节，正文里那些"- "行原样进来
    ## 📦 下载与安装      ← 资产表：每个文件是什么、多大、sha256 前 8 位
    **Full Changelog**: compare/<上一个标签>...<这个标签>

用法：python tools/make_release_notes.py <标签> <输出文件>
读的全是本地东西（git log + installer/ + build/Release/ltool-plugins/ + plugins/*/plugin.json），
**一次网都不上**；写不出内容就非 0 退出，让 CI 红，而不是发一份空说明。
"""
import glob
import hashlib
import json
import os
import re
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# 一节配一个图标：按提交标题里的关键词挑，第一条命中的算。
# 这不是装饰 —— 一眼能看出那一节是修 bug、加功能还是动界面，正是那份参考说明好用的地方。
ICONS = (
    ('🐛', ('修', '漏', '坑', '假绿', '红', '闪退', '卡', '对不上', '悬空', '崩')),
    ('🧩', ('插件', '商店', 'dll', 'sdk', '清单', '自动更新')),
    ('🎨', ('界面', '样式', '颜色', '日间', '夜间', '卡片', '窗口', '按钮', '图标', '主题')),
    ('⚡', ('慢', '耗时', '性能', '内存', '并行', '缓存', '优化')),
    ('✅', ('闸', '判据', '自测', 'ci', '测试', '反向验')),
    ('📝', ('文档', 'readme', '说明', '注释')),
    ('✨', ('新增', '加了', '接入', '支持', '做成', '改成', '搬进')),
)


def git(*args):
    return subprocess.run(('git',) + args, cwd=ROOT, capture_output=True, text=True,
                          encoding='utf-8', errors='replace').stdout


def pick_icon(subject):
    s = subject.lower()
    for icon, keys in ICONS:
        if any(k in s for k in keys):
            return icon
    return '📦'


def prev_tag(tag):
    """这个标签之前最近的那个 v* 标签；没有就返回 None（第一版，整条历史都算进来）。"""
    # "上一个标签"必须是**历史上在这之前的那一个**，不是"最新的那一个"：
    # 拿 git describe 问 `<这个标签>^` 才对（第一版我用 tag --sort=-creatordate 取第一个不等于自己的，
    # 结果给 v1.0.3 找出个 v1.3.3 当上一个，区间反了 ⇒ 0 笔提交）。
    out = git('describe', '--abbrev=0', '--tags', tag + '^').strip()
    return out or None


def commits(rng):
    """%x1e 分隔每条提交、%x1f 分隔标题和正文 —— 提交信息里全是中文和换行，
    拿换行当分隔符一定会被咬（这仓库的规矩：分隔符挑一个不可能出现在正文里的字符）。"""
    raw = git('log', rng, '--no-merges', '--reverse',
              '--format=%s%x1f%b%x1e')
    out = []
    for chunk in raw.split('\x1e'):
        chunk = chunk.strip('\n')
        if not chunk.strip():
            continue
        head, _, body = chunk.partition('\x1f')
        out.append((head.strip(), body.strip()))
    return out


def digest(path):
    h = hashlib.sha256()
    with open(path, 'rb') as f:
        for blk in iter(lambda: f.read(1 << 20), b''):
            h.update(blk)
    return h.hexdigest()


def human(n):
    return '%.2f MB' % (n / 1048576.0) if n >= 1048576 else '%.0f KB' % (n / 1024.0)


def api_version():
    """插件接口号只有一个来源：src/ltool-plugin.h 里那颗 #define。别在这儿抄一个数。"""
    with open(os.path.join(ROOT, 'src', 'ltool-plugin.h'), encoding='utf-8', errors='replace') as f:
        m = re.search(r'#define\s+LTOOL_PLUGIN_API_VERSION\s+(\d+)', f.read())
    return m.group(1) if m else '?'


def plugin_versions():
    out = {}
    for p in glob.glob(os.path.join(ROOT, 'plugins', '*', 'plugin.json')):
        try:
            with open(p, encoding='utf-8') as f:
                d = json.load(f)
            out[str(d.get('Id'))] = str(d.get('Version'))
        except Exception:
            continue
    return out


def body_lines(body):
    """提交正文原样搬进来，但保证每条都是 markdown 的一项：
    这个仓库的提交信息本来就按 `- ` 一行一条写，少数几行是续行（缩进的），把它们并回上一条。"""
    items = []
    for line in body.splitlines():
        if not line.strip():
            continue
        if line.startswith(('- ', '* ')):
            items.append(line[2:].rstrip())
        elif line.startswith(('  ', '\t')) and items:
            items[-1] += ' ' + line.strip()
        else:
            items.append(line.rstrip())
    if not items:
        items = ['（这一笔没写正文，标题就是全部。）']
    return items


def main():
    if len(sys.argv) != 3:
        print('用法：python tools/make_release_notes.py <标签> <输出文件>')
        return 2
    tag, out_path = sys.argv[1], sys.argv[2]
    base = prev_tag(tag)
    rng = (base + '..' + tag) if base else tag
    cs = commits(rng)
    if not cs:
        print('FAIL %s 这个区间一笔提交都没有（标签指错了？），不发空说明' % rng)
        return 1

    ver = tag.lstrip('v')
    L = []
    L.append('# 🚀 ltool %s 发布说明 / Release Notes' % tag)
    L.append('')
    keys = []
    for subject, _ in cs[-3:][::-1]:          # 最近三笔当"本次重点"
        k = re.split(r'[：:，,——]', subject)[0].strip()
        if len(k) > 30:
            k = k[:29] + '…'
        keys.append(k)
    lead = '🎉 欢迎使用 **ltool %s**！本次共 **%d 笔改动**' % (ver, len(cs))
    if keys:
        lead += '，重点是 ' + '、'.join('**%s**' % k for k in keys)
    lead += '。下面每一节是一笔提交，原因和实测数字都在里面。'
    L.append(lead)
    L.append('')
    L.append('---')
    L.append('')
    L.append('## 🌟 本次改动')
    L.append('')
    for i, (subject, body) in enumerate(cs[::-1], 1):     # 最新的排最前
        L.append('### %s %d. %s' % (pick_icon(subject), i, subject))
        L.append('')
        for item in body_lines(body):
            L.append('- %s' % item)
        L.append('')
    L.append('---')
    L.append('')
    L.append('## 📦 下载与安装')
    L.append('')
    L.append('Windows 10 及以上（安装包已声明 `MinVersion=10.0`）。按用户安装，**不需要管理员**，'
             '默认装到 `%LOCALAPPDATA%\\Programs\\ltool`；双击安装向导走完即可，托盘常驻，`Alt+Space` 呼出。')
    L.append('')
    L.append('| 文件 | 大小 | sha256 前 8 位 | 说明 |')
    L.append('| :--- | :--- | :--- | :--- |')
    full = []      # (发布名, 字节数, 完整 sha256, 说明)
    # 只收**这一版**那两个产物：本地 installer\ 里躺着十几个历史版本的 setup，
    # 全收进表就成了"46 个资产"（CI 那棵目录是干净的，所以这错只会在本机复跑时露出来）。
    def this_version(path):
        return ver in os.path.basename(path)
    setups = [p for p in sorted(glob.glob(os.path.join(ROOT, 'installer', 'ltool-*-setup.exe'))) if this_version(p)]
    sdks = [p for p in sorted(glob.glob(os.path.join(ROOT, 'installer', 'ltool-sdk-*.zip'))) if this_version(p)]
    for p in setups:
        full.append((os.path.basename(p), os.path.getsize(p), digest(p),
                     '**就用这个**：双击安装/升级（约 22 MB，开箱零插件，插件由商店按需拉）'))
    for p in sdks:
        full.append((os.path.basename(p), os.path.getsize(p), digest(p),
                     '插件 SDK 快照：头文件闭包 + 导入库，给独立编插件用'))
    if not setups or not sdks:
        print('FAIL 没找到这一版（%s）的安装包或 SDK：installer 目录里 setup=%d 个 sdk=%d 个'
              % (ver, len(setups), len(sdks)))
        return 1
    idx = os.path.join(ROOT, 'installer', 'plugins-index.json')
    if os.path.exists(idx):
        full.append(('plugins-index.json', os.path.getsize(idx), digest(idx),
                     '插件商店清单：**这一版 app 读的就是它**（地址按版本号钉死，见下面那条）'))
    pvs = plugin_versions()
    api = api_version()
    dlls = sorted(glob.glob(os.path.join(ROOT, 'build', 'Release', 'ltool-plugins', 'ltool-*.dll')))
    for p in dlls:
        name = os.path.basename(p)
        pid = re.sub(r'^ltool-|\.dll$', '', name)
        full.append((name, os.path.getsize(p), digest(p),
                     '商店里的插件 `%s`（v%s，插件接口 %s）' % (pid, pvs.get(pid, '?'), api)))
    # `sha256.txt` 是**这份脚本生成**的：说明里写了"下 sha256.txt 对整批"，那它必须真在资产里，
    # 不能是一句指向不存在文件的话。
    txt = os.path.join(ROOT, 'installer', 'sha256.txt')
    with open(txt, 'w', encoding='utf-8', newline='\n') as f:
        for name, size, sh, desc in full:
            f.write('%s  %s\n' % (sh, name))
    full.append(('sha256.txt', os.path.getsize(txt), '',
                 '上面这些文件的 SHA-256 全表（`Get-FileHash -Algorithm SHA256` 逐行对得上）'))
    for name, size, sh, desc in full:
        L.append('| `%s` | %s | `%s` | %s |' % (name, human(size), sh[:8] or '—', desc))
    L.append('')
    L.append('一共 **%d 个文件**。校验：`Get-FileHash <文件> -Algorithm SHA256`，或下 `sha256.txt` 对整批。' % len(full))
    L.append('')
    L.append('插件商店读的是 `releases/download/%s/plugins-index.json` —— '
             '清单和这 19 颗 dll 都出自**编出这一版 core 的那同一次构建**，'
             '所以"商店里那颗和这台机器的 core 对不上"这件事从源头没了（对不上也只会是被拒绝并说清，不是闪退）。' % tag)
    L.append('')
    L.append('---')
    L.append('')
    if base:
        repo = git('config', '--get', 'remote.origin.url').strip()
        m = re.search(r'github\.com[:/](.+?)(?:\.git)?$', repo)
        if m:
            L.append('**Full Changelog**: https://github.com/%s/compare/%s...%s' % (m.group(1), base, tag))
        else:
            L.append('**Full Changelog**: 上一个标签 %s → %s' % (base, tag))
    else:
        L.append('**Full Changelog**: 这是第一个带说明的版本（前面没有可对比的标签）。')
    L.append('')

    text = '\n'.join(L)
    with open(out_path, 'w', encoding='utf-8', newline='\n') as f:
        f.write(text)
    print('INFO 发布说明写好了：%s（%d 笔提交 · %d 个资产 · %d 字符）'
          % (out_path, len(cs), len(full), len(text)))
    print('NOTES_COMMITS=%d NOTES_ASSETS=%d' % (len(cs), len(full)))
    return 0


if __name__ == '__main__':
    sys.exit(main())
