import os


def rel(root):
    out = {}
    for dp, dn, fn in os.walk(root):
        for f in fn:
            p = os.path.join(dp, f)
            out[os.path.relpath(p, root).replace(os.sep, '/')] = os.path.getsize(p)
    return out


a = rel(os.path.expandvars(r'%APPDATA%\ltool'))
b = rel(r'D:\software\ltool\data')
only_old = sorted(k for k in a if k not in b)
print('only in legacy:', len(only_old))
for k in only_old[:60]:
    print('  ', k, a[k])
diff = [k for k in a if k in b and a[k] != b[k]]
print('size differs:', len(diff))
for k in diff[:20]:
    print('  ', k, a[k], b[k])
print('only in new:', len([k for k in b if k not in a]))
