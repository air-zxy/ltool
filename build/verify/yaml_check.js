// 校验 .github/workflows/build.yml 能不能被解析（0 个 job = 这份 yml 没过校验，CI 那栏根本不会跑）。
// 用 $TEMP\jsy 里那份 js-yaml：这台机的 python 是 msys 的、没有 pip，而那份 3.14 随机 SRE 自爆。
const os = require('os');
const path = require('path');
const fs = require('fs');
const y = require(path.join(os.tmpdir(), 'jsy', 'node_modules', 'js-yaml'));
const file = process.argv[2] || '.github/workflows/build.yml';
const d = y.load(fs.readFileSync(file, 'utf8'));
const jobs = Object.keys(d.jobs || {});
console.log('YAML ok  file=' + file + '  jobs=' + jobs.length + '  -> ' + jobs.join(', '));
if (jobs.length === 0) {
  console.log('FAIL 0 个 job：这份 yml 等于没被校验过');
  process.exit(1);
}
for (const j of jobs) {
  const steps = ((d.jobs[j].steps) || []).map((s) => s.name || s.uses || 'run').slice(0, 20);
  console.log('  job ' + j + ' steps=' + ((d.jobs[j].steps || []).length) + ' | ' + steps.join(' / ').slice(0, 240));
}
