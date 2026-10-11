import struct, sys

dump = r"C:\Users\loukas\AppData\Local\CrashDumps\ltool.exe.12504.dmp"
exe = r"D:\project\cpp\ltool\build\Release\ltool.exe"
data = open(dump, "rb").read()
img = open(exe, "rb").read()

assert data[:4] == b"MDMP"
nstreams, dir_off = struct.unpack_from("<II", data, 8)
print("streams", nstreams)

def rva2off(rva):
    end = rva + struct.unpack_from("<I", data, rva + 8)[0]
    return rva + 12

streams = {}
for i in range(nstreams):
    st, sz, off = struct.unpack_from("<III", data, dir_off + i*12)
    streams.setdefault(st, (off, sz))
print("stream types:", sorted(streams))

# ---- ModuleList (4): find ltool.exe base
ml_off, ml_sz = streams[4]
nmod = struct.unpack_from("<I", data, ml_off)[0]
base = size = None
mods = []
p = ml_off + 4
for i in range(nmod):
    b = struct.unpack_from("<Q", data, p + i*108)[0]
    s = struct.unpack_from("<I", data, p + i*108 + 8)[0]
    name_rva = struct.unpack_from("<I", data, p + i*108 + 20)[0]
    ln = struct.unpack_from("<I", data, name_rva)[0]
    nm = data[name_rva+4:name_rva+4+ln].decode("utf-16-le", "replace")
    mods.append((b, s, nm))
    if nm.lower().endswith("ltool.exe"):
        base, size = b, s
print("modules", nmod, "ltool base=%x size=%x" % (base, size))

# ---- ThreadList (3): authoritative stack ranges + per-thread context
ex_off0 = streams[6][0]
thread_id = struct.unpack_from("<I", data, ex_off0)[0]
tl_off, tl_sz = streams[3]
fault_stack = (0, 0, 0)
nthr = struct.unpack_from("<I", data, tl_off)[0]
print("threads:", nthr)
for i in range(nthr):
    q = tl_off + 4 + i*64
    tid, sus, pc, pri = struct.unpack_from("<IIII", data, q)
    teb, sstart, ssize = struct.unpack_from("<QQQ", data, q + 16)
    st_mem_start, m_sz, m_rva = struct.unpack_from("<QII", data, q + 40)
    c_sz, c_rva = struct.unpack_from("<II", data, q + 56)
    mark = " <== FAULTING" if tid == thread_id else ""
    print("  tid=%d stack=[%x, %x) size=%d ctx@%x%s" % (tid, sstart, sstart + ssize, ssize, c_rva, mark))
    if tid == thread_id:
        fault_stack = (st_mem_start, m_sz, m_rva)
    if tid == thread_id and c_sz >= 0x100:
        c = data[c_rva:c_rva + c_sz]
        print("     ctxflags=%x" % struct.unpack_from("<I", c, 0x30)[0])
        for j, n in enumerate(["Rax","Rcx","Rdx","Rbx","Rsp","Rbp","Rsi","Rdi",
                               "R8","R9","R10","R11","R12","R13","R14","R15"]):
            print("     %s=%x" % (n, struct.unpack_from("<Q", c, 0x78 + j*8)[0]))
        print("     Rip=%x" % struct.unpack_from("<Q", c, 0xF8)[0])

# ---- ExceptionStream (6)
ex_off, ex_sz = streams[6]
thread_id = struct.unpack_from("<I", data, ex_off)[0]
exc_code = struct.unpack_from("<I", data, ex_off + 4)[0]
exc_flags = struct.unpack_from("<I", data, ex_off + 8)[0]
exc_addr = struct.unpack_from("<Q", data, ex_off + 24)[0]
nparams = struct.unpack_from("<I", data, ex_off + 32)[0]
params = struct.unpack_from("<15Q", data, ex_off + 40)
ctx_sz, ctx_rva = struct.unpack_from("<II", data, ex_off + 160)
print("exception: tid=%d code=%08x addr=%x params=%s" %
      (thread_id, exc_code, exc_addr, [hex(x) for x in params[:nparams]]))
print("raw ex stream dwords:", [hex(x) for x in struct.unpack_from("<24I", data, ex_off)])
for cand in (152, 160, 168):
    dsz, drva = struct.unpack_from("<II", data, ex_off + cand)
    print("  ctx candidate +%d -> size=%d rva=%x" % (cand, dsz, drva))

# ---- MemoryList(5) + Memory64List(15) for reading VA ranges
mem = []
if 5 in streams:
    o, s = streams[5]
    cnt = struct.unpack_from("<I", data, o)[0]
    for i in range(cnt):
        start, dsz, drva = struct.unpack_from("<QII", data, o + 4 + i*16)
        mem.append((start, dsz, drva))
if 15 in streams:
    o, s = streams[15]
    cnt = min(struct.unpack_from("<Q", data, o)[0], (s - 16) // 16)
    cur = struct.unpack_from("<Q", data, o + 8)[0]     # BaseRva: all ranges are laid out here
    for i in range(cnt):
        start, dsz = struct.unpack_from("<QQ", data, o + 16 + i*16)
        mem.append((start, dsz, cur))
        cur += dsz

def read_va(va, n):
    for start, sz, off in mem:
        if start <= va and va + n <= start + sz:
            return data[off + (va - start): off + (va - start) + n]
    return None

print("memory segments:", len(mem))

# AMD64 CONTEXT is stored at the location descriptor's RVA (RVA == file offset in minidumps)
ctx = data[ctx_rva:ctx_rva + ctx_sz]
if len(ctx) < 0xF0:
    print("context too short", len(ctx)); sys.exit(1)
# AMD64 CONTEXT: p1..home4 (32), ContextFlags(4), MxCsr(4), SegCs..SegGs(12), pad,
# Rax@0x78 ... R15@0xF8, Rsp@0x98, Rbp@0xA0, Rip@0xF8
def g(off): return struct.unpack_from("<Q", ctx, off)[0]
regs = {n: g(0x78 + i*8) for i, n in enumerate(
    ["Rax","Rcx","Rdx","Rbx","Rsp","Rbp","Rsi","Rdi","R8","R9","R10","R11","R12","R13","R14","R15"])}
rip = g(0xF8)
print("rip=%x rsp=%x rbp=%x" % (rip, regs["Rsp"], regs["Rbp"]))
for n in ["Rax","Rcx","Rdx","Rbx","Rsi","Rdi","R8","R9","R12"]:
    print("   %s=%x" % (n, regs[n]))

# ---- .pdata of the exe for function bounds
pe_off = struct.unpack_from("<I", img, 0x3C)[0]
coff = pe_off + 4
nsec = struct.unpack_from("<H", img, coff + 2)[0]
sizeopt = struct.unpack_from("<H", img, coff + 16)[0]
opt = coff + 20
image_base = struct.unpack_from("<Q", img, opt + 24)[0]
dd = opt + 112
exc_rva, exc_sz = struct.unpack_from("<II", img, dd + 24)
sec_off = opt + sizeopt
secs = []
for i in range(nsec):
    e = img[sec_off + i*40: sec_off + (i+1)*40]
    vs, va, rs, rp = struct.unpack_from("<IIII", e, 8)
    secs.append((va, vs, rs, rp))
def r2o(rva):
    for va, vs, rs, rp in secs:
        if va <= rva < va + max(vs, rs):
            return rp + (rva - va)
    return None
exc_off = r2o(exc_rva)
funcs = []
for i in range(exc_sz // 12):
    b, e, u = struct.unpack_from("<III", img, exc_off + i*12)
    funcs.append((b, e))
funcs.sort()
def func_of_va(va):
    rva = va - base
    lo = 0; hi = len(funcs) - 1
    while lo <= hi:
        m = (lo + hi) // 2
        b, e = funcs[m]
        if rva < b: hi = m - 1
        elif rva >= e: lo = m + 1
        else: return b, e, rva - b
    return None

def show(va, tag):
    f = func_of_va(va)
    print("  %-8s %016x -> %s" % (tag, va, ("ltool+0x%x (func %08x..%08x, +0x%x)" % (va-base, f[0], f[1], f[2])) if f else "not in ltool"))

show(rip, "RIP")
# ---- crude stack scan for return addresses inside ltool
sp = regs["Rsp"]
blob = read_va(sp, 0x2800)
if blob is None and fault_stack[1]:
    fs_addr, fs_sz, fs_rva = fault_stack
    print("using thread stack descriptor: addr=%x size=%d" % (fs_addr, fs_sz))
    whole = data[fs_rva:fs_rva + fs_sz]
    if fs_addr <= sp < fs_addr + fs_sz:
        off = sp - fs_addr
        blob = whole[off:off + 0x2800]
print("stack bytes read:", 0 if blob is None else len(blob))
if blob:
    hits = 0
    for i in range(0, len(blob) - 8, 8):
        v = struct.unpack_from("<Q", blob, i)[0]
        if base <= v < base + size:
            f = func_of_va(v)
            print("  sp+0x%04x = %016x  ltool+0x%x %s" % (i, v, v - base, ("func %08x..%08x" % (f[0], f[1])) if f else ""))
            hits += 1
            if hits > 60: break
    print("return-address candidates:", hits)
