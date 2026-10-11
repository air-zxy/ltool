import re, struct, subprocess, sys, os

sys.stdout.reconfigure(encoding='utf-8', errors='replace')

EXE = r'D:\project\cpp\ltool\build\Release\ltool.exe'
OBJDUMP = r'D:\software\msys64\mingw64\bin\objdump.exe'
start = sys.argv[1] if len(sys.argv) > 1 else '0x140035800'
stop = sys.argv[2] if len(sys.argv) > 2 else '0x140035d20'

d = open(EXE, 'rb').read()
lf = struct.unpack_from('<I', d, 0x3c)[0]
nsec = struct.unpack_from('<H', d, lf + 6)[0]
optsz = struct.unpack_from('<H', d, lf + 20)[0]
opt = lf + 24
image_base = struct.unpack_from('<Q', d, opt + 24)[0]
secs = []
for i in range(nsec):
    o = opt + optsz + i * 40
    name = d[o:o + 8].rstrip(b'\0').decode()
    vsz, va, rawsz, praw = struct.unpack_from('<IIII', d, o + 8)
    secs.append((name, va, vsz, praw, rawsz))

def vma_to_off(v):
    rva = v - image_base
    for name, va, vsz, praw, rawsz in secs:
        if va <= rva < va + max(vsz, rawsz):
            return praw + (rva - va), name
    return None, None

def show_str(off, vma):
    raw = d[off:off + 128]
    # try UTF-16LE Qt literal (u"..." -> 2 bytes per char) then UTF-8/latin1
    out = []
    for enc, step in (('utf-16-le', 2), ('utf-8', 1)):
        try:
            end = raw.index(b'\0\0' if step == 2 else b'\0')
            s = raw[:end - (end % step)].decode(enc, 'replace')
        except ValueError:
            continue
        if s and all(31 < ord(c) or c in ' \n' for c in s) and len(s) >= 3:
            out.append(f'{enc}: {s[:60]!r}')
    return f'  ; -> 0x{vma:x} ' + ' | '.join(out) if out else f'  ; -> 0x{vma:x}'

txt = subprocess.run([OBJDUMP, '-d', f'--start-address={start}', f'--stop-address={stop}', EXE],
                     capture_output=True, text=True, errors='replace').stdout
for line in txt.splitlines():
    if '\t' not in line and not line.startswith(' '):
        continue
    extra = ''
    for mm in re.finditer(r'# (0x[0-9a-f]+)', line):
        v = int(mm.group(1), 16)
        off, sname = vma_to_off(v)
        if off is None or sname not in ('.rdata', '.data'):
            continue
        s = show_str(off, v)
        if s:
            extra += '\n' + ' ' * 46 + s
    print(line + extra)
