import struct
import sys
import zlib

out = sys.argv[1]

entries = [
    ("说明.txt", b"Z" * 777),
    ("子目录/", b""),
    ("子目录/嵌套.txt", b"W" * 888),
    ("plain.bin", b"Y" * 12),
]

body = bytearray()
central = bytearray()
count = 0
# 2026-10-07 12:00:00 -> DOS 时间日期
dostime = (12 << 11) | (0 << 5) | 0
dosdate = ((2026 - 1980) << 9) | (10 << 5) | 7

for name, data in entries:
    raw = name.encode("gbk")            # 国内老工具的做法：GBK 字节 + 不带 UTF-8 标志位
    crc = zlib.crc32(data) & 0xFFFFFFFF
    offset = len(body)
    body += struct.pack("<IHHHHHIIIHH", 0x04034B50, 20, 0, 0, dostime, dosdate,
                        crc, len(data), len(data), len(raw), 0)
    body += raw
    body += data
    central += struct.pack("<IHHHHHHIIIHHHHHII", 0x02014B50, 20, 20, 0, 0,
                           dostime, dosdate, crc, len(data), len(data),
                           len(raw), 0, 0, 0, 0, 0x10 if name.endswith("/") else 0,
                           offset)
    central += raw
    count += 1

cd_offset = len(body)
cd_size = len(central)
body += central
body += struct.pack("<IHHHHLLH", 0x06054B50, 0, 0, count, count, cd_size, cd_offset, 0)

with open(out, "wb") as f:
    f.write(bytes(body))

print("wrote %s, %d entries, %d bytes, first name bytes=%s"
      % (out, count, len(body), entries[0][0].encode("gbk").hex()))
