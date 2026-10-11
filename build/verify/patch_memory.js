const fs = require('fs');
const p = 'C:/Users/loukas/.qoder-cn/memory/win-machine-env.md';
const lines = fs.readFileSync(p, 'utf8').split(/\r?\n/);
const start = lines.findIndex(l => l.includes('静态注册表右键"级联子菜单"必须多一层'));
if (start < 0) throw new Error('start line not found');
if (!lines[start + 1].includes('验这个坑不用碰鼠标')) throw new Error('line+1 mismatch: ' + lines[start + 1]);
if (!lines[start + 2].includes('注册表里键值都在')) throw new Error('line+2 mismatch: ' + lines[start + 2]);
const repl = [
  '- **静态注册表右键"级联子菜单"要两样一起才有飞出的子菜单**：① 子项写在 `父键\\shell\\<项>\\command`；② **父键还要有 `SubCommands`（REG_SZ，分号隔开的子项名）**。只补 ① 那一层不够 —— Explorer 仍然把父键当成一个可点的普通项，而父键没有 command，点下去就是"该文件没有与之关联的应用来执行该操作"。',
  '- **不碰鼠标就能判定坏在哪一层**：`Shell.Application` 的 `FolderItem.Verbs()` 里**出现父键名（如 "ltool"）就说明它是可点的普通项 = 形状坏了**；补上 `SubCommands` 之后父键从 Verbs() 里消失（成了子菜单）。这时用 `ShellExecuteExW(lpVerb="ltool\\shell\\<项>", fMask=SEE_MASK_FLAG_NO_UI|SEE_MASK_NOCLOSEPROCESS)` 逐个点子项：坏形状返回 **lasterr=1155 = ERROR_NO_ASSOCIATION**（就是那个弹窗），好形状 ok=True 并真的起进程。',
  '- **"注册表里键值都在"不等于"菜单能用"**：形状（嵌套层级 + SubCommands）也是正确性的一部分；改完要提醒**开一个新的资源管理器窗口**（老窗口缓存旧菜单形状，在老窗口里点还是坏的那一项）。'
];
lines.splice(start, 3, ...repl);
fs.writeFileSync(p, lines.join('\n'));
console.log('replaced at line ' + (start + 1));
