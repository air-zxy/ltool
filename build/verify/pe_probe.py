import struct, sys

path = r"D:\project\cpp\ltool\build\Release\ltool.exe"
data = open(path, "rb").read()

pe_off = struct.unpack_from("<I", data, 0x3C)[0]
assert data[pe_off:pe_off+4] == b"PE\0\0"
coff = pe_off + 4
nsec = struct.unpack_from("<H", data, coff + 2)[0]
sizeopt = struct.unpack_from("<H", data, coff + 16)[0]
opt = coff + 20
magic = struct.unpack_from("<H", data, opt)[0]
print("PE32+" if magic == 0x20b else "PE32", "sections", nsec)
image_base = struct.unpack_from("<Q", data, opt + 24)[0]
dd_off = opt + 112
import_rva, import_size = struct.unpack_from("<II", data, dd_off + 8)
except_rva, except_size = struct.unpack_from("<II", data, dd_off + 24)

sections = []
sec_off = opt + sizeopt
for i in range(nsec):
    e = data[sec_off + i*40: sec_off + (i+1)*40]
    name = e[:8].rstrip(b"\0").decode()
    vsize, va, rawsize, rawptr = struct.unpack_from("<IIII", e, 8)
    sections.append((name, va, vsize, rawptr, rawsize))

def r2o(rva):
    for name, va, vsize, rawptr, rawsize in sections:
        if va <= rva < va + max(vsize, rawsize):
            return rawptr + (rva - va)
    raise KeyError(hex(rva))

def cstr(o):
    end = data.index(b"\0", o)
    return data[o:end].decode("latin1")

# ---- IAT: rva -> "dll!func"
iat = {}
p = r2o(import_rva)
while True:
    orig, tstamp, fwd, name_rva, thunk = struct.unpack_from("<IIIII", data, p)
    if name_rva == 0 and thunk == 0 and orig == 0:
        break
    dll = cstr(r2o(name_rva))
    t = r2o(orig if orig else thunk)
    while True:
        entry = struct.unpack_from("<Q", data, t)[0]
        if entry == 0:
            break
        if entry & (1 << 63):
            nm = "%s!#%d" % (dll, entry & 0xFFFF)
        else:
            hint = r2o(entry & 0x7FFFFFFF)
            nm = dll + "!" + cstr(hint + 2)
        iat[image_base + thunk] = nm
        t += 8
        if orig:
            thunk += 8
    p += 20

print("iat entries:", len(iat))
for a in (0x1400bc050, 0x1400bbcb0, 0x1400bba00, 0x1400bbcf0, 0x1400bbe50, 0x1400bbd30):
    print("  ", hex(a), "->", iat.get(a, "??"))

# ---- .pdata: function containing a given RVA
def func_of(rva):
    q = r2o(except_rva)
    n = except_size // 12
    for i in range(n):
        b, e, u = struct.unpack_from("<III", data, q + i*12)
        if b <= rva < e:
            return b, e, u, i, n
    return None

for target in (0x1e8d7,):
    r = func_of(target)
    if r:
        b, e, u, i, n = r
        print("RVA %08x -> function [%08x, %08x) len=%d  unwind=%08x  (#%d of %d)" %
              (target, b, e, e-b, u, i, n))
    else:
        print("RVA %08x not found in .pdata" % target)
