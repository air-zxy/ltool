const fs = require('fs');
const p = 'D:/project/cpp/ltool/build/verify/pintest.ps1';
let s = fs.readFileSync(p, 'utf8');
const NL = String.fromCharCode(10);
let count = 0;
function sub(a, b, expect = 1) {
  const cnt = s.split(a).length - 1;
  if (cnt !== expect) throw new Error(`anchor 命中 ${cnt}（要 ${expect}）: ${a.slice(0, 60)}`);
  s = s.split(a).join(b);
  count++;
}

// @($x + 1 + $a, $y + $b) 这种写法 PS5.1 会把逗号左边当成数组，先算进局部变量
sub(`function IP(${'$'}ix, ${'$'}iy) { return @(${'$'}ox + 76 + ${'$'}ix, ${'$'}oy + ${'$'}iy) }`,
    `function IP(${'$'}ix, ${'$'}iy) { ${'$'}lx = ${'$'}ox + 76 + ${'$'}ix; ${'$'}ly = ${'$'}oy + ${'$'}iy; return @(${'$'}lx, ${'$'}ly) }`);

// 三行一组的 InvokePattern → Press（checkable 按钮只有 TogglePattern）
// 先处理"关闭"那一处（它后面没有 Start-Sleep，正则不覆盖）
sub(`  ${'$'}ip = ${'$'}null
  ${'$'}closeBtn.TryGetCurrentPattern([System.Windows.Automation.InvokePattern]::Pattern, [ref]${'$'}ip) | Out-Null
  ${'$'}ip.Invoke()`,
    `  Press ${'$'}closeBtn | Out-Null`);

const re = new RegExp(
  '\\$ip = \\$null' + NL +
  '\\s*(\\$[A-Za-z0-9]+)\\.TryGetCurrentPattern\\(\\[System\\.Windows\\.Automation\\.InvokePattern\\]::Pattern, \\[ref\\]\\$ip\\) \\| Out-Null' + NL +
  '\\s*\\$ip\\.Invoke\\(\\);( Start-Sleep -Milliseconds \\d+)',
  'g');
const hits = s.match(re);
if (!hits) throw new Error('InvokePattern 调用点没匹配到');
s = s.replace(re, (m, btn, sleep) => 'Press ' + btn + ';' + sleep);
count++;
// 失败分支里的 dump 代码里还有 TryGetCurrentPattern 字样是正常的，只要求没有裸的 $ip.Invoke()
if (/^\s*\$ip\.Invoke\(\)/m.test(s)) throw new Error('还有没替换干净的 $ip.Invoke()');

fs.writeFileSync(p, s);
console.log('ok, ' + count + ' 处改动，InvokePattern 替换 ' + hits.length + ' 处');
