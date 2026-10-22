import os
import struct

dump = os.path.expanduser(r"~\AppData\Local\CrashDumps\ltool.exe.12504.dmp")
exe = r"D:\project\cpp\ltool\build\Release\ltool.exe"
d = open(dump, "rb").read()
img = open(exe, "rb").read()

n, off = struct.unpack_from("<II", d, 8)
st = {}
for i in range(n):
    t, sz, ro = struct.unpack_from("<III", d, off + i*12)
    st.setdefault(t, (ro, sz))

mem = []
o, s = st[5]
for i in range(struct.unpack_from("<I", d, o)[0]):
    a, ds, dr = struct.unpack_from("<QII", d, o + 4 + i*16)
    mem.append((a, ds, dr))

def rd(va, ln):
    for a, ds, dr in mem:
        if a <= va and va + ln <= a + ds:
            return d[dr + (va - a): dr + (va - a) + ln]
    return None

# module base + function bounds from .pdata
pe = struct.unpack_from("<I", img, 0x3C)[0]
coff = pe + 4
nsec = struct.unpack_from("<H", img, coff + 2)[0]
sizeopt = struct.unpack_from("<H", img, coff + 16)[0]
opt = coff + 20
dd = opt + 112
exc_rva, exc_sz = struct.unpack_from("<II", img, dd + 24)
secs = []
for i in range(nsec):
    e = img[opt + sizeopt + i*40: opt + sizeopt + (i+1)*40]
    vs, va, rs, rp = struct.unpack_from("<IIII", e, 8)
    secs.append((va, vs, rs, rp))
def r2o(rva):
    for va, vs, rs, rp in secs:
        if va <= rva < va + max(vs, rs):
            return rp + (rva - va)
funcs = []
eo = r2o(exc_rva)
for i in range(exc_sz // 12):
    b, e, u = struct.unpack_from("<III", img, eo + i*12)
    funcs.append((b, e))
funcs.sort()
def func_of(rva):
    lo, hi = 0, len(funcs) - 1
    while lo <= hi:
        m = (lo + hi) // 2
        b, e = funcs[m]
        if rva < b: hi = m - 1
        elif rva >= e: lo = m + 1
        else: return b, e
    return None

BASE = 0x7ff7dda20000
SIZE = 0x118000
RSP = 0x86d30fed10
R12 = 0x86d30ffaf8

print("== closure memory at r12 (the std::function target: captured refs) ==")
blob = rd(R12, 64)
for i in range(0, 64, 8):
    v = struct.unpack_from("<Q", blob, i)[0]
    print("  r12+0x%02x = %016x" % (i, v))

print("== stack scan: return addresses inside ltool.exe ==")
stack = rd(0x86d30fd7d8, 0x2828)
print("  stack bytes:", 0 if stack is None else len(stack))
start = 0x86d30fd7d8
hits = []
for i in range(0, len(stack) - 8, 8):
    v = struct.unpack_from("<Q", stack, i)[0]
    if BASE <= v < BASE + SIZE:
        rva = v - BASE
        f = func_of(rva)
        hits.append((start + i, rva, f))
for addr, rva, f in hits:
    tag = "func %08x..%08x" % (f[0], f[1]) if f else "?"
    mark = "  <== current frame" if abs(addr - RSP) < 16 else ""
    print("  sp[%08x] va=%x -> ltool+0x%06x  %s%s" % (addr - start, addr, rva, tag, mark))
print("candidates:", len(hits))
