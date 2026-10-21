# 一次性取证脚本：给一个 PE 文件和一个 RVA，报出"这个 RVA 落在哪个导出符号里/之后"。
# 用法：python pe_rva.py <dll> <rva-hex>
# 为什么要它：这台机器上 dumpbin/objdump 不保证有，而 Windows 错误报告只给"Qt6Core.dll + 0x1489c9"。
import sys
import struct

sys.stdout.reconfigure(encoding='utf-8', errors='replace')

path, target = sys.argv[1], int(sys.argv[2], 16)
b = open(path, 'rb').read()
nt = struct.unpack_from('<I', b, 0x3c)[0]
opt = nt + 24
magic = struct.unpack_from('<H', b, opt)[0]
dd = opt + (112 if magic == 0x20b else 96)
export_rva = struct.unpack_from('<I', b, dd)[0]
nsec = struct.unpack_from('<H', b, nt + 6)[0]
sizeopt = struct.unpack_from('<H', b, nt + 20)[0]
sec = opt + sizeopt
sections = []
for i in range(nsec):
    o = sec + i * 40
    # IMAGE_SECTION_HEADER: Name(8) VirtualSize(4) VirtualAddress(4) SizeOfRawData(4) PointerToRawData(4)
    vsize, va, rawsize, rawptr = struct.unpack_from('<IIII', b, o + 8)
    sections.append((va, vsize, rawptr, rawsize))


def r2o(rva):
    for va, vs, raw, rs in sections:
        if va <= rva < va + max(vs, rs):
            return rva - va + raw
    raise SystemExit('RVA 0x%x 不在任何节里' % rva)


ed = r2o(export_rva)
# IMAGE_EXPORT_DIRECTORY: Characteristics(4) TimeDateStamp(4) Maj(2) Min(2) Name(4) OrdinalBase(4)
#                         NumberOfFunctions(4) NumberOfNames(4) AddressOfFunctions(4) AddressOfNames(4)
#                         AddressOfNameOrdinals(4)
num_funcs, num_names = struct.unpack_from('<II', b, ed + 20)
addr_tab = r2o(struct.unpack_from('<I', b, ed + 28)[0])
name_tab = r2o(struct.unpack_from('<I', b, ed + 32)[0])
ord_tab = r2o(struct.unpack_from('<I', b, ed + 36)[0])
names = []
for i in range(num_names):
    str_rva = struct.unpack_from('<I', b, name_tab + 4 * i)[0]
    idx = struct.unpack_from('<H', b, ord_tab + 2 * i)[0]
    rva = struct.unpack_from('<I', b, addr_tab + 4 * idx)[0]
    if not rva:
        continue
    o = r2o(str_rva)
    names.append((rva, b[o:b.index(0, o)].decode('latin1')))
names.sort()
print('%s：导出 %d 个，目标 RVA 0x%x' % (path.split('/')[-1], len(names), target))
prev = nxt = None
for rva, nm in names:
    if rva <= target:
        prev = (rva, nm)
    elif nxt is None:
        nxt = (rva, nm)
        break
if prev:
    print('  它前面最近的导出：%s @0x%x（目标在其后 %d 字节）' % (prev[1], prev[0], target - prev[0]))
if nxt:
    print('  它后面最近的导出：%s @0x%x（间隔 %d 字节）' % (nxt[1], nxt[0], nxt[0] - target))
    if nxt[0] - target < 400:
        print('  -> 落在两者之间，多半就是前一个符号的函数体里')
else:
    print('  后面没有导出了')
