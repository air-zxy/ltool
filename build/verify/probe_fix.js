const fs = require('fs');
const p = 'D:/project/cpp/ltool/src/FileOps.cpp';
let s = fs.readFileSync(p, 'utf8');
const bs = String.fromCharCode(92), q = String.fromCharCode(39), d = String.fromCharCode(34);

const oldBlock = [
  '    wchar_t winDir[MAX_PATH] = {};',
  '    // 探针文件：win.ini 在 Windows 目录本身，不在 system32',
  '    if (!GetWindowsDirectoryW(winDir, MAX_PATH))',
  '        return -1;',
  '    std::wstring probePath = winDir;',
  '    probePath += L' + q + bs + bs + 'win.ini' + q + ';'
].join('\n');
const newBlock = [
  '    // 探针文件：挑一个必然存在、且肯定归到 File 类型的文件',
  '    wchar_t sysDir[MAX_PATH] = {};',
  '    if (!GetSystemDirectoryW(sysDir, MAX_PATH))',
  '        return -1;',
  '    std::wstring probePath = sysDir;',
  '    probePath += L' + q + bs + bs + 'kernel32.dll' + q + ';'
].join('\n');

if (s.indexOf(oldBlock) < 0) {
  console.log('--- old block not found; context around probePath: ---');
  const i = s.indexOf('probePath');
  console.log(JSON.stringify(s.slice(i - 260, i + 120)));
  throw new Error('anchor missing');
}
s = s.replace(oldBlock, newBlock);

// 顺手把探针打开失败的原因也报出来：调用方只有 -1 可用，这里加一条日志
const oldRet = '    if (probe == INVALID_HANDLE_VALUE)\n        return -1;';
if (s.indexOf(oldRet) >= 0)
  s = s.replace(oldRet, '    if (probe == INVALID_HANDLE_VALUE) {\n        Utils::log(QStringLiteral("whohas: 探针文件打不开 %1 err=%2").arg(QString::fromStdWString(probePath)).arg(GetLastError()));\n        return -1;');
fs.writeFileSync(p, s, 'utf8');
console.log('probe switched to kernel32.dll');
