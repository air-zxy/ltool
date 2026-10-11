// 统计 log.txt 里截图浮层的落地情况：start 了多少次、shown 的有多少没拿到焦点(active=0)、
// 以及"start 之后没有 shown"的次数（=建浮层那一步就没走到）。
const fs = require('fs');
const lines = fs.readFileSync(process.env.APPDATA + '/ltool/log.txt', 'latin1').split('\n');
let start = 0, shown = 0, noFocus = 0, noShown = 0, sel = 0;
let lastStart = null, lastNoFocusStamps = [];
for (const L of lines) {
  const ts = (L.match(/^\[([^\]]+)\]/) || [])[1];
  if (/capture: start/.test(L)) {
    start++;
    if (lastStart && !lastStart.shown) noShown++;
    lastStart = { ts, shown: false };
  } else if (/capture: shown/.test(L)) {
    shown++;
    const a = L.match(/active=(\d)/);
    if (a && a[1] === '0') { noFocus++; lastStart && (lastStart.shown = true); lastNoFocusStamps.push(ts); }
    else if (lastStart) lastStart.shown = true;
  } else if (/capture: selected/.test(L)) sel++;
}
if (lastStart && !lastStart.shown) noShown++;
console.log('capture start=' + start + '  shown=' + shown + '  其中没拿到焦点=' + noFocus
  + '  start后没有shown=' + noShown + '  成功框选=' + sel);
console.log('没拿到焦点的时刻:', lastNoFocusStamps.slice(-8).join(' | '));
