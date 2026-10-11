// 直接扒 MP4 的 box：打印 tkhd 的 3x3 变换矩阵和 mdhd 时限、以及视频轨的 stsd 编解码器。
// tkhd matrix 是 16.16 定点，3x3 行主序（最后两列是 0x40000000 的对齐点）——
// 如果 y 方向是 -1，文件里的像素是正的、但播放器会按矩阵翻成倒的。
const fs = require('fs');
const buf = fs.readFileSync(process.argv[2]);
let off = 0;
function cstr(p, n) { return buf.toString('latin1', p, p + n); }
function u32(p) { return buf.readUInt32BE(p); }
function walk(start, end, depth, path) {
  let p = start;
  while (p + 8 <= end) {
    let size = u32(p);
    const type = cstr(p + 4, 4);
    let header = 8;
    if (size === 1) { size = Number(buf.readBigUInt64BE(p + 8)); header = 16; }
    if (size < header || p + size > end) break;
    console.log('  '.repeat(depth) + type + ' @' + p + ' size=' + size);
    if (type === 'tkhd') {
      // 版本无关的算法：width/height 是 box 的最后 8 字节，matrix 是它前面那 36 字节
      const m = [];
      for (let i = 0; i < 9; ++i) m.push(buf.readInt32BE(p + size - 8 - 36 + i * 4) / 65536);
      console.log('  '.repeat(depth) + '  显示尺寸=' + u32(p + size - 8) / 65536 + 'x' +
                  u32(p + size - 4) / 65536 + ' matrix=[a=' + m[0] + ',b=' + m[1] + ',c=' + m[2] +
                  ',d=' + m[3] + ' tx=' + m[6] + ' ty=' + m[7] + ']');
    }
    if (type === 'stsd') {
      const fmt = cstr(p + header + 8 + 4 + 6, 4);
      console.log('  '.repeat(depth) + '  编码=' + fmt);
    }
    if (['moov', 'trak', 'mdia', 'minf', 'stbl', 'edts', 'udta', 'mvex'].includes(type)) {
      walk(p + header, p + size, depth + 1, path + '/' + type);
    }
    p += size;
  }
}
console.log('前 4 字节=' + cstr(0, 4) + '（ftyp 开头才对）');
walk(0, buf.length, 0, '');
