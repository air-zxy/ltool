import hashlib
import os
import shutil

legacy = os.path.expandvars(r'%APPDATA%\ltool\clipimg')
new = r'D:\software\ltool\data\clipimg'


def sha(p):
    h = hashlib.sha256()
    with open(p, 'rb') as f:
        for chunk in iter(lambda: f.read(1 << 20), b''):
            h.update(chunk)
    return h.hexdigest()


copied = skipped_same = refused = 0
for name in sorted(os.listdir(legacy)):
    src = os.path.join(legacy, name)
    if not os.path.isfile(src):
        continue
    dst = os.path.join(new, name)
    if os.path.exists(dst):
        if sha(src) == sha(dst):
            skipped_same += 1
        else:
            refused += 1          # 同名不同内容：一个字都不许动新目录那份
        continue
    shutil.copy2(src, dst)
    if sha(src) != sha(dst):
        os.remove(dst)
        print('HASH MISMATCH, rolled back:', name)
    else:
        copied += 1

print('copied=', copied, 'already-there-identical=', skipped_same, 'same-name-different-bytes=', refused)

# 读一遍现在这份历史：那些绝对路径打不开的条目，按文件名能不能在新目录里认回来
import json
with open(r'D:\software\ltool\data\clipboard.json', encoding='utf-8') as f:
    items = json.load(f)
imgs = [it for it in items if it.get('image')]
bad = [it for it in imgs if not os.path.exists(it.get('path', ''))]
rescued = [it for it in bad if os.path.exists(os.path.join(new, os.path.basename(it.get('path', ''))))]
print('历史里图片条目=', len(imgs), '记着的路径打不开=', len(bad), '新目录里有同名可认回=', len(rescued))
