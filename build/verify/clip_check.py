import json
import os

legacy = os.path.expandvars(r'%APPDATA%\ltool')
new = r'D:\software\ltool\data'

for root, tag in ((legacy, 'legacy'), (new, 'new')):
    with open(os.path.join(root, 'clipboard.json'), encoding='utf-8') as f:
        items = json.load(f)
    imgs = [it for it in items if it.get('image')]
    dirs = {}
    missing_here = 0
    exists_somewhere = 0
    for it in imgs:
        p = it.get('path', '')
        base = os.path.basename(p)
        dirs[os.path.dirname(p)] = dirs.get(os.path.dirname(p), 0) + 1
        if not os.path.exists(p):
            missing_here += 1
            if os.path.exists(os.path.join(root, 'clipimg', base)):
                exists_somewhere += 1
    print(tag, 'items=', len(items), 'images=', len(imgs))
    for d, n in dirs.items():
        print('   path 前缀:', d, n)
    print('   path 原样打不开:', missing_here, '（其中在本目录 clipimg 里能找到:', exists_somewhere, '）')
    # 缩略图
    t_missing = sum(1 for it in imgs
                    if not os.path.exists(it.get('path', '').replace('.png', '.t120.png')))
    print('   缩略图按那串绝对路径打不开:', t_missing)
