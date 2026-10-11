const fs = require('fs');
const p = 'C:/Users/loukas/.qoder-cn/memory/win-machine-env.md';
const lines = fs.readFileSync(p, 'utf8').split(/\r?\n/);
const start = lines.findIndex(l => l.includes('静态注册表右键"级联子菜单"要两样一起'));
if (start < 0) throw new Error('start not found');
if (!lines[start + 1].includes('不碰鼠标就能判定')) throw new Error('line+1 mismatch: ' + lines[start + 1]);
if (!lines[start + 2].includes('注册表里键值都在')) throw new Error('line+2 mismatch: ' + lines[start + 2]);
const repl = [
  '- **Win11 静态注册表右键"级联子菜单"唯一实测可用的形状**（照抄系统自己那把 `HKLM\\SOFTWARE\\Classes\\Directory\\shell\\UpdateEncryptionSettings`）：',
  '  - 父键：**默认值必须"未设置"**（写了名字 Explorer 就不建子菜单），名字放 `MUIVerb`（REG_SZ），再加 `ExtendedSubCommandsKey` = 指向**父键自己**的 Classes 相对路径（如 `*\\shell\\ltool`、`Directory\\shell\\ltool`），`Icon` 可选；',
  '  - 子项：`父键\\shell\\<verb>`，菜单文字也放该子键的 `MUIVerb`（**别写默认值**），命令行放 `父键\\shell\\<verb>\\command` 的默认值。全在 HKCU\\Software\\Classes 下，不需要管理员。',
  '- **两条走过的死路（都实测过，别再试）**：① 子项直接放 `父键\\<verb>`（没有中间 `shell` 层）→ 父键变成一个可点的普通项，点了报 1155；② 用 `SubCommands` → 那是 **CommandStore** 机制，子命令要去 `HKLM\\...\\Explorer\\CommandStore\\shell\\<verb>`（要提权，HKCU 下这台机器没有任何先例），结果就是"箭头有了但子菜单是空的"。',
  '- **传感器能力有限，别高估**：`ShellExecuteExW(lpVerb="ltool\\shell\\<verb>", fMask=SEE_MASK_FLAG_NO_UI|NOCLOSEPROCESS)` 能判定**子项能不能执行**（坏形状 lasterr=1155=ERROR_NO_ASSOCIATION，好形状 ok=True 并真起进程）；但"飞出的子菜单里列了几项"只能真的去看菜单 —— `FolderItem.Verbs()` 不展开级联，UIA 读弹窗需要窗口在前台（被别的窗口挡住时点击守卫会拒绝，别硬来）。',
  '- **改完右键菜单必须让用户开一个新的资源管理器窗口去点**：老窗口缓存旧形状；这一层机器验证不了，最后一米只能人点一次。'
];
lines.splice(start, 3, ...repl);
fs.writeFileSync(p, lines.join('\n'));
console.log('replaced at line ' + (start + 1) + ', now ' + repl.length + ' lines');
