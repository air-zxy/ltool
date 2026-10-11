// 核对生成的拼音表：行数、随机几个字的读音、以及"行号 = 码点 - 0x3400"这个对齐假设。
const fs = require("fs");
const h = fs.readFileSync("src/pinyin_data.h", "utf8");
const start = h.indexOf('R"PY(') + 5;
const end = h.indexOf(')PY"', start);
const blob = h.slice(start, end);
const rows = blob.split("\n");
console.log("行数", rows.length, "期望", 0x9fff - 0x3400 + 1);
const show = (cp) => console.log("U+" + cp.toString(16), String.fromCodePoint(cp), "=", rows[cp - 0x3400]);
[0x4efb, 0x52a1, 0x91cd, 0x5e86, 0x8f6f, 0x4ef6, 0x94f6, 0x5783, 0x573e, 0x8bbe, 0x7f6e].forEach(show);
console.log("首行", JSON.stringify(rows[0].slice(0, 12)), "末行", JSON.stringify(rows[rows.length - 1].slice(0, 12)));
console.log("含裸换行尾巴?", /PY";\n/.test(h) ? "格式正常" : "看结尾");
