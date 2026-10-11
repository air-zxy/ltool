// 读 ONNX 的 ModelProto（自己走 protobuf wire format，不装 onnx 包）：
// 打印 graph 的 input/output 名字+形状、initializer 的名字、以及 metadata_props 的键（值太长，只报键名+长度+头几个字）。
const fs = require('fs');

function varint(buf, p) {
  let shift = 0, val = 0;
  for (;;) {
    const b = buf[p.i++];
    val += (b & 0x7f) * Math.pow(2, shift);
    if (!(b & 0x80)) break;
    shift += 7;
  }
  return val;
}
// 遍历一层：回调 (fieldNo, wireType, value)
function walk(buf, start, end, cb) {
  const p = { i: start };
  while (p.i < end) {
    const key = varint(buf, p);
    const fn = Math.floor(key / 8), wt = key % 8;
    if (wt === 0) cb(fn, wt, varint(buf, p));
    else if (wt === 5) { cb(fn, wt, buf.readUInt32LE(p.i)); p.i += 4; }
    else if (wt === 1) { cb(fn, wt, buf.readDoubleLE(p.i)); p.i += 8; }
    else if (wt === 2) {
      const len = varint(buf, p);
      cb(fn, wt, buf.slice(p.i, p.i + len));
      p.i += len;
    } else throw new Error('wiretype ' + wt + ' @' + p.i);
  }
}
const str = (b) => b.toString('utf8');
const nodeOps = {};

function tensorShape(buf) { // TensorShapeProto: field 1 = repeated dim
  const dims = [];
  walk(buf, 0, buf.length, (fn, wt, v) => {
    if (fn === 1 && wt === 2) {
      let dv = null, dp = null;
      walk(v, 0, v.length, (f2, w2, x2) => {
        if (f2 === 1 && w2 === 0) dv = x2;
        if (f2 === 2 && w2 === 2) dp = str(x2);
      });
      dims.push(dv !== null ? String(dv) : (dp || '?'));
    }
  });
  return dims;
}
function valueInfo(buf) { // ValueInfoProto: 1 name, 2 type
  let name = '', elem = null, dims = [];
  walk(buf, 0, buf.length, (fn, wt, v) => {
    if (fn === 1 && wt === 2) name = str(v);
    if (fn === 2 && wt === 2) {
      walk(v, 0, v.length, (f2, w2, x2) => { // TypeProto: 1 = tensor
        if (f2 === 1 && w2 === 2) {
          walk(x2, 0, x2.length, (f3, w3, y3) => {
            if (f3 === 1 && w3 === 0) elem = y3;
            if (f3 === 2 && w3 === 2) dims = tensorShape(y3);
          });
        }
      });
    }
  });
  return { name, elem, dims };
}

for (const path of process.argv.slice(2)) {
  const buf = fs.readFileSync(path);
  console.log('===== ' + path + '  ' + buf.length + ' 字节');
  walk(buf, 0, buf.length, (fn, wt, v) => {
    if (fn === 2 && wt === 2) console.log('producer_name: ' + str(v));
    if (fn === 3 && wt === 2) console.log('producer_version: ' + str(v));
    if (fn === 7 && wt === 2) { // GraphProto
      walk(v, 0, v.length, (g, gw, gv) => {
        if (g === 2 && gw === 2) console.log('graph name: ' + str(gv));
        if (g === 11 && gw === 2) { const t = valueInfo(gv); console.log('INPUT  ' + t.name + ' elem=' + t.elem + ' shape=[' + t.dims.join(',') + ']'); }
        if (g === 12 && gw === 2) { const t = valueInfo(gv); console.log('OUTPUT ' + t.name + ' elem=' + t.elem + ' shape=[' + t.dims.join(',') + ']'); }
        if (g === 1 && gw === 2) { // NodeProto：只数一下 op 类型分布，别刷屏
          let op = '', nIn = 0, nOut = 0;
          walk(gv, 0, gv.length, (nf, nw, nv) => {
            if (nf === 2 && nw === 2) op = str(nv);
            if (nf === 1 && nw === 2) nIn++;
            if (nf === 3 && nw === 2) nOut++;
          });
          (nodeOps[op] = nodeOps[op] || { n: 0, in: nIn, out: nOut }).n++;
        }
      });
    }
    if (fn === 14 && wt === 2) { // StringStringEntryProto {1 key, 2 value}
      let k = '', val = null;
      walk(v, 0, v.length, (f2, w2, x2) => {
        if (f2 === 1 && w2 === 2) k = str(x2);
        if (f2 === 2 && w2 === 2) val = x2;
      });
      console.log('META key=' + k + ' bytes=' + (val ? val.length : 0) +
                  ' 头: ' + (val ? JSON.stringify(str(val).slice(0, 40)) : '-'));
      if (k === 'character' && val) {
        fs.writeFileSync(path + '.character.txt', val);
        console.log('  → 字典已写到 ' + path + '.character.txt');
      }
    }
  });
}
var opsSorted = Object.entries(nodeOps).sort((a, b) => b[1].n - a[1].n);
console.log('op 统计（前 12）: ' + opsSorted.slice(0, 12).map(([k, v]) => k + '×' + v.n).join(', '));
