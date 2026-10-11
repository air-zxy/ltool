import struct, sys, os

CRASH = os.path.expandvars(r'%LOCALAPPDATA%\CrashDumps')

def streams(data):
    sig, ver, n, dir_rva = struct.unpack_from('<4sIII', data, 0)
    out = {}
    for i in range(n):
        st, size, rva = struct.unpack_from('<III', data, dir_rva + i * 12)
        out[st] = (size, rva)
    return out

def info(path):
    data = open(path, 'rb').read()
    st = streams(data)
    size, rva = st[4]
    n = struct.unpack_from('<I', data, rva)[0]
    exe = qt = None
    for i in range(n):
        off = rva + 4 + i * 108
        base, img, chk, ts = struct.unpack_from('<QIII', data, off)
        nr = struct.unpack_from('<I', data, off + 20)[0]
        nl = struct.unpack_from('<I', data, nr)[0]
        name = data[nr + 4:nr + 4 + nl].decode('utf-16-le', 'replace')
        if name.split('\\')[-1] == 'ltool.exe':
            exe = (name, ts)
    size, rva = st[6]
    tid = struct.unpack_from('<I', data, rva)[0]
    code = struct.unpack_from('<I', data, rva + 8)[0]
    eaddr = struct.unpack_from('<Q', data, rva + 24)[0]
    npar = struct.unpack_from('<I', data, rva + 32)[0]
    par = struct.unpack_from('<%dQ' % npar, data, rva + 40)
    st = streams(data)
    # locate module containing eaddr
    size, rva = st[4]
    n = struct.unpack_from('<I', data, rva)[0]
    mod, off2 = None, None
    for i in range(n):
        off = rva + 4 + i * 108
        base, img = struct.unpack_from('<QI', data, off)
        if base <= eaddr < base + img:
            nr = struct.unpack_from('<I', data, off + 20)[0]
            nl = struct.unpack_from('<I', data, nr)[0]
            mod = data[nr + 4:nr + 4 + nl].decode('utf-16-le', 'replace').split('\\')[-1]
            off2 = eaddr - base
    hdr = time.ctime(os.path.getmtime(path))
    print(f'{os.path.basename(path)}  mtime={hdr}')
    print(f'   exe={exe[0]}  build_ts=0x{exe[1]:08x}')
    print(f'   code=0x{code:08x} fault={mod}+0x{off2:x} params={[hex(x) for x in par[:2]]}')

for f in sys.argv[1:]:
    info(os.path.join(CRASH, f))
