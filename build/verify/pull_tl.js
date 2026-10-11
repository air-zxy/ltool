const fs = require('fs');
const rl = require('readline');
const src = 'C:/Users/loukas/.qoder-cn/projects/D--project-cpp-ltool/941a36ee-99ed-4c3e-a872-01c130d8528b.jsonl';
const want = new Set([9323, 9406, 9409, 9408, 16920, 2335, 9351, 9396]);
const inp = fs.createReadStream(src, { encoding: 'utf8' });
const ri = rl.createInterface({ input: inp, crlfDelay: Infinity });
let n = 0;
const texts = [];
ri.on('line', (l) => {
  n++;
  if (!want.has(n)) return;
  const o = JSON.parse(l);
  const parts = [];
  const push = (t) => { if (typeof t === 'string') parts.push(t); };
  const content = o.message ? o.message.content : o.content;
  if (Array.isArray(content)) {
    for (const c of content) {
      if (c.text) push(c.text);
      if (c.content) push(typeof c.content === 'string' ? c.content : JSON.stringify(c.content));
      if (c.output) push(typeof c.output === 'string' ? c.output : JSON.stringify(c.output));
    }
  } else if (typeof content === 'string') parts.push(content);
  const joined = parts.join('\n@@PART@@\n');
  texts.push({ n, len: joined.length, hasLoop: joined.indexOf('insertWidget(m_rowsLay->count() - 1, row)') >= 0,
               hasBtns: joined.indexOf('kBtnsWidth') >= 0, head: joined.slice(0, 120).replace(/\n/g, '|') });
  fs.writeFileSync('D:/project/cpp/ltool/build/verify/tl_' + n + '.txt', joined, 'utf8');
});
ri.on('close', () => {
  texts.sort((a, b) => b.len - a.len);
  texts.forEach((t) => console.log(JSON.stringify(t)));
});
