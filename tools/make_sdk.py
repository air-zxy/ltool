# 把"编一个插件 dll 所需的那一份"打成一个 SDK 目录 / zip（#78）。
#
# 为什么要有它：插件 dll 是**链着 ltoolcore.dll 的导入库**编出来的，所以独立编译一个插件
# 需要三样 —— core 公开的那几个头、ltoolcore.lib、还有 Qt6/LibArchive 的定位方式。
# 主仓库把这三样凑齐交给插件仓库（sdk/ 就是一份快照），插件作者不用 clone 整个 ltool。
#
# 用法（在主仓库根）：
#   python tools/make_sdk.py <输出目录>            # 铺出 sdk/
#   python tools/make_sdk.py <输出目录> --zip      # 顺手压成 installer/ltool-sdk-<版本>.zip
# 头文件的清单不是手抄的：从 plugins/ 里真实 #include 的那些出发求**传递闭包**，
# 多一个少一个都会跟着源码变（手写清单迟早会漂成"编不过还找不到原因"）。
import argparse
import hashlib
import io
import os
import re
import shutil
import sys

if hasattr(sys.stdout, "reconfigure"):
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")
if hasattr(sys.stderr, "reconfigure"):
    sys.stderr.reconfigure(encoding="utf-8", errors="replace")

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def plugin_includes():
    """plugins/ 里的源码直接 include 的、且在 src/ 存在的那些头。"""
    seeds = set()
    for dirpath, _, files in os.walk(os.path.join(ROOT, 'plugins')):
        for f in files:
            if not f.endswith(('.cpp', '.h')):
                continue
            text = read(os.path.join(dirpath, f))
            for m in re.findall(r'#include "([^"]+)"', text):
                if os.path.exists(os.path.join(ROOT, 'src', m)):
                    seeds.add(m)
    return seeds


def read(p):
    return io.open(p, encoding='utf-8', errors='replace').read()


def closure(seeds):
    seen = set()
    stack = sorted(seeds)
    while stack:
        h = stack.pop()
        if h in seen:
            continue
        seen.add(h)
        p = os.path.join(ROOT, 'src', h)
        if not os.path.exists(p):
            continue
        for m in re.findall(r'#include "([^"]+)"', read(p)):
            if os.path.exists(os.path.join(ROOT, 'src', m)) and m not in seen:
                stack.append(m)
    return seen


def iss_version():
    text = read(os.path.join(ROOT, 'ltool.iss'))
    m = re.search(r'#define MyAppVersion "([^"]+)"', text)
    assert m, 'ltool.iss 里没读到 #define MyAppVersion'
    return m.group(1)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('out')
    ap.add_argument('--zip', action='store_true')
    a = ap.parse_args()

    lib = os.path.join(ROOT, 'build', 'lib', 'ltoolcore.lib')
    if not os.path.exists(lib):
        sys.exit('缺 %s —— 先在主仓库编一次 Release（导入库是编 core 时产出的）' % lib)

    heads = sorted(closure(plugin_includes()))
    out = os.path.abspath(a.out)
    inc = os.path.join(out, 'include')
    if os.path.isdir(out):
        shutil.rmtree(out)
    os.makedirs(os.path.join(inc, 'onnxruntime'))
    os.makedirs(os.path.join(out, 'lib'))

    for h in heads:
        # 头可以住在子目录里（platform/Paths.h 就是这样）：不先建目录，copy2 直接抛
        dest = os.path.join(inc, h)
        d = os.path.dirname(dest)
        if d and not os.path.isdir(d):
            os.makedirs(d)
        shutil.copy2(os.path.join(ROOT, 'src', h), dest)
    ort = os.path.join(ROOT, 'third_party', 'onnxruntime')
    ort_heads = [f for f in os.listdir(ort) if f.endswith('.h')] if os.path.isdir(ort) else []
    for f in ort_heads:
        shutil.copy2(os.path.join(ort, f), os.path.join(inc, 'onnxruntime', f))
    shutil.copy2(os.path.join(ROOT, 'cmake', 'ltool-plugin.cmake'),
                 os.path.join(out, 'ltool-plugin.cmake'))
    shutil.copy2(lib, os.path.join(out, 'lib', 'ltoolcore.lib'))

    ver = iss_version()
    core_sha = hashlib.sha256(open(lib, 'rb').read()).hexdigest()
    api = re.search(r'#define LTOOL_PLUGIN_API_VERSION (\d+)',
                    read(os.path.join(ROOT, 'src', 'ltool-plugin.h'))).group(1)
    io.open(os.path.join(out, 'INFO.txt'), 'w', encoding='utf-8').write(
        'ltool 插件 SDK —— 独立编一个 ltool-<id>.dll 要的那一份\n'
        '\n'
        '来自 ltool %s（插件接口 API %s）\n'
        'ltoolcore.lib 的 SHA256：%s\n'
        '头文件清单是 plugins/ 里真实 include 的那些求出来的传递闭包（%d 个）：\n  %s\n'
        'onnxruntime 头：%d 个（截图插件的 OCR 用，运行时不链接）\n'
        '\n'
        '这份是**快照**：主仓库改了 core 的导出面，就得重新跑 tools/make_sdk.py 更新它，\n'
        '否则插件里出现"链接找不到符号"或装上去加载失败。ltool-plugin.cmake 里写明了怎么校验。\n'
        % (ver, api, core_sha, len(heads), '\n  '.join(heads), len(ort_heads)))
    print('sdk 铺到 %s：%d 个头 + %d 个 onnxruntime 头 + ltoolcore.lib（API %s，ltool %s）'
          % (out, len(heads), len(ort_heads), api, ver))

    if a.zip:
        import zipfile
        zp = os.path.join(ROOT, 'installer', 'ltool-sdk-%s.zip' % ver)
        os.makedirs(os.path.dirname(zp), exist_ok=True)
        with zipfile.ZipFile(zp, 'w', zipfile.ZIP_DEFLATED) as z:
            for dirpath, _, files in os.walk(out):
                for f in files:
                    full = os.path.join(dirpath, f)
                    z.write(full, os.path.join('sdk', os.path.relpath(full, out)))
        print('打包：%s（%.1f KB）' % (zp, os.path.getsize(zp) / 1024))


main()
