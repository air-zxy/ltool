# README 瘦身（2026-10-15 第二次）。他第一次说"密密麻麻"时我只是把长表挪到文末，
# 全文还是 459 行、最长一行 5031 字 —— 那不算简化。这一次：
#   ① README 只留一行一条的操作信息（是什么 / 怎么编 / 怎么装 / 插件怎么写 / 配置在哪）；
#   ② 被拿掉的每一段**原文整段**搬进 docs/notes.md（实测数字、判据、踩过的坑，一个字不改）；
#   ③ 跑完自己回头验：原文里每一段 ≥60 字的文字，必须还能在 README 或 notes 里逐字找到。
import io
import re

SRC = 'README.md'
NOTES = 'docs/notes.md'
lines = io.open(SRC, encoding='utf-8', newline='').read().split('\n')


def span(start_text, end_text=None):
    i = next(k for k, l in enumerate(lines) if l.startswith(start_text))
    j = len(lines)
    if end_text:
        j = next(k for k, l in enumerate(lines) if k > i and l.startswith(end_text))
    else:
        for k in range(i + 1, len(lines)):
            if lines[k].startswith('## ') or lines[k].startswith('### '):
                j = k
                break
    while j > i + 1 and not lines[j - 1].strip():
        j -= 1
    return i, j


def cut(start_text, end_text=None):
    i, j = span(start_text, end_text)
    return '\n'.join(lines[i:j]).strip()


# ---- README 里**原样留着**的块（它们本来就是一行一条）----
FEATURES = cut('| 功能 | 一句话 |', '## 文件索引如何保持新鲜')
WEBS = cut('## 网站关键字', '## 开发环境插件')
DEVENV = cut('## 开发环境插件', '## 工具插件')
COMMUNITY = cut('## 社区')

# ---- 搬去 docs/notes.md 的原文 ----
MOVED = [
    ('文件索引怎么保持新鲜（原文）', cut('## 文件索引如何保持新鲜', '## 构建')),
    ('构建与依赖的细节（vcpkg / 许可证 / 并行编译与 PCH 的实测）',
     '清单依赖是 `qtbase` + `libarchive` + `bit7z`（见 `vcpkg.json`；bit7z 会带进 `7zip` 和 `ghc-filesystem`，'
     '`7zip` 打出来的就是 7-Zip 官方那个格式库，只是名字叫 `7zip.dll`）。\n\n'
     + cut('- 走 **vcpkg manifest 模式**', '## 打包')),
    ('打包与交付闭环（原文：一条命令、逐段报耗时、版本号、档位与体积实测）',
     cut('一条命令跑完整条链', '### GitHub Actions')),
    ('打包的等价裸命令（deliver.ps1 内部就是这几步）',
     cut('等价的裸命令（脚本内部就是这几步）', '- **版本号只有一个来源')),
    ('CI 的细节与踩过的坑',
     '### GitHub Actions：推标签就出安装包、发 Release（`.github/workflows/build.yml`）' + '\n\n'
     + cut('一条 job（`windows-2022`）', '## 使用')),
    ('自测旗标逐条（每面旗量到的数、期望值为什么改过）', cut('## 自测旗标逐条', '## 社区')),
    ('插件的三份产物（旧表）+ 插件机制、动作型路由与三次搬家记',
     cut('| 目录 | 是什么 |', '## 配置与数据目录')),
    ('数据目录那一页表格的原文说明', cut('| 文件/目录 | 说明 |', '`config.ini` 可配置项')),
    ('配置与数据目录（原文：config.ini 的完整注释）', cut('`config.ini` 可配置项', '## 已知限制')),
    ('已知限制原文（一条一段的那种写法）', cut('- **「压缩为 .7z」依赖', '## 功能细节与实测')),
    ('功能细节与实测（每个功能为什么这么做、实测多少、拿哪面旗标验）',
     cut('## 功能细节与实测', '## 自测旗标逐条')),
]
for t, b in MOVED:
    assert len(b) > 80, (t, b[:80])

i, j = span('| 旗标 | 一句话 |')
FLAGS_TABLE = '\n'.join(lines[i:j])
DATA_TABLE = cut('| 文件/目录 | 说明 |', '`config.ini` 可配置项')

io.open(NOTES, 'w', encoding='utf-8', newline='').write(
    '# ltool 详细记录\n\n'
    '[README](../README.md) 只回答"有什么功能、怎么编、怎么装、插件怎么写"。\n'
    '这一份放**为什么这么做、实测是多少、踩过什么坑** —— 全部是从 README 里**整段搬过来**的原文，\n'
    '一个字没删（搬的脚本 `build/verify/readme_slim.py`，跑完会自己验"原文每段还在不在"）。\n\n'
    + '\n\n'.join('## %s\n\n%s' % (t, b) for t, b in MOVED)
    + '\n\n## 自测旗标一览（34 面，全表）\n\n' + FLAGS_TABLE
    + '\n\n## 数据目录与配置项（全表）\n\n' + DATA_TABLE + '\n')

README = '''# ltool — uTools 风格的 Windows 启动器

C++20 / Qt 6 / vcpkg + CMake，只跑 Windows。界面本体都是 DLL 插件，主程序只剩热键、托盘、单实例、命令行自测。
**每一处"为什么这么做、实测多少"都整段挪到 [docs/notes.md](docs/notes.md)** —— 这里只留一行一条。

## 功能

''' + FEATURES + '''

## 构建

依赖：**Visual Studio 2022（MSVC）、CMake ≥ 3.24、vcpkg**（本机 `D:\\vcpkg`）。

```bat
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 -DCMAKE_TOOLCHAIN_FILE=D:/vcpkg/scripts/buildsystems/vcpkg.cmake
cmake --build build --config Release
```

- 依赖走清单模式（`vcpkg.json`：`qtbase` + `libarchive` + `bit7z`）。加依赖只改这份 json 再重新 configure，
  别在仓库根裸跑 `vcpkg install`。
- 产物在 `build\\Release\\`：`ltool.exe` 264KB + `ltoolcore.dll` 1.8MB + `ltool-plugins\\*.dll` 12 个。
- 编译已经很快：全量重建 17.9 秒、改一个头 13.7 秒、什么都没改 2.5 秒。
- 文件索引：常用目录实时监听（秒级）；全盘靠快照，管理员权限下用 USN 日志做增量（约 0.5 秒）。

## 使用

- 托盘常驻；`Alt+Space` 呼出/隐藏搜索框（在鼠标所在那块屏的中上方），再按一次收起。
- 回车 / 双击运行，`↑↓` 选择，`Esc` 隐藏，右键有更多操作（打开所在位置 / 复制路径 / 加入 PATH）。
- 内置命令：`rescan` 重扫索引 ｜ `init [java|c++|node|python|rust]` 开发环境检测与安装 ｜
  `path [list|add <目录>|del <目录>]` 用户 PATH ｜ `reg [scan|clean]` 注册表体检 / 清理。
- 热键：`F1` 截图 ｜ `F2` 录屏 ｜ 文件与文件夹右键里有 ltool 那一组（占用分析 / 粉碎 / 解压 / 压缩）。

## 打包 + 安装

```powershell
powershell -File build\\verify\\deliver.ps1        # 构建 → 打包 → 停旧实例 → 静默装 → 逐文件比 SHA256 → 重起
```

- 约 26 秒一圈。档位：默认 normal 22.45MB ｜ `-Final` 21.8MB（出正式包）｜ `-Quick` 26.9MB ｜ `-NoBuild` 跳过构建。
- 版本号只有一处：`ltool.iss` 的 `#define MyAppVersion`，读不到就直接 FATAL。
- 交付判据是**逐个文件比 SHA256**（exe + core + 12 个插件）；单独跑一遍：`powershell -File build\\verify\\shaall.ps1`。
- 装到哪：默认按用户装 `%LOCALAPPDATA%\\Programs\\ltool`，指定目录加 `/DIR="D:\\software\\ltool"`。
- 等价裸命令就三步：`cmake --build build --config Release` → `"D:\\software\\InnoSetup6\\ISCC.exe" ltool.iss`
  → `installer\\ltool-<版本>-setup.exe /VERYSILENT /NORESTART /SUPPRESSMSGBOXES`。

## CI（`.github/workflows/build.yml`）

编 Release → 跑无界面回归 → 打包 → artifact；**只有推 `v*` 标签才发 Release**。
带 `-beta.N` 的标签自动标 pre-release，而 `/releases/latest` 不认预发 —— 预发和稳定就靠标签分，客户端不用改。
同一分支后推的一次会顶掉前一次在跑的作业。

发一版三步：改 `MyAppVersion` → 推 `main` → `git tag v<一模一样的号> && git push origin v<那个号>`（第三步是对外的，人来按）。

''' + WEBS + '\n' + DEVENV + '''

## 插件

十二个自带插件：DLL 修复 / 广告弹窗 / 磁盘清理 / 网络 / 注册表分析 / 开机启动分析 / Windows 用户名 /
JSON 格式化 / Base64 编解码 / **右键动作** / **录屏** / **截图** —— 第三方插件走同一条路，主程序里没有"内置工具"特判。

| 位置 | 是什么 |
| --- | --- |
| `ltool.exe`（264KB） | 热键、托盘、单实例、自测入口 |
| `ltoolcore.dll`（1.8MB） | 主程序和插件**共用的一份**实现（`Utils` / `DiskScan` / `InfoRow` / `Archive` / `CaptureHistory` …） |
| `ltool-plugins\\ltool-<id>.dll` | 一个工具一个：本体 + 一层 `LToolPlugin` 壳 + 嵌在 dll 里的 `plugin.json` |

- 契约只有一份：`src/ltool-plugin.h`（现在 API 3）—— `apiVersion / id / name / desc / keywords / createPage / feedText / actions / invoke / save / load`。
- 两类插件：**卡片型**（工具页一张卡，可开可递文本）和**动作型**（截图、录屏、右键那几件：入口留在主程序，按下之后 `PluginHost::invoke(动作名)` 转发）。
- 只装不卸：Qt 插件 unload 之后里面活着的控件就是悬空指针；要禁用就把 dll 改名，下次启动生效。
- `Api` 和主程序不等就拒装，并在界面上说清是哪一侧旧；元数据不齐也是点名说清楚，不默默少一张卡。
- 加一个插件：`plugins/<id>/` 放 `plugin.json` + 源码 + 一层壳（照 `plugins/network/networkplugin.cpp` 抄），
  再在 `CMakeLists.txt` 里 `ltool_add_tool(id IdPlugin 源文件…)` 加一行。卡片墙、搜索直达、商店全自动认它。
- 第三方仓库：[mateo-ki/ltool_plugin](https://github.com/mateo-ki/ltool_plugin) —— 那十二个自带插件的源码 + 独立编 dll 的
  SDK 快照；SDK 由主仓库 `tools/make_sdk.py` 生成。

## 常用自测旗标

| 旗标 | 验什么 |
| --- | --- |
| `--smoke` | 无界面一遍过：索引、目录、插件都在 |
| `--plugintest` | 十二个插件逐个验 + 卡片墙张数与几何 + 动作路由（136 条） |
| `--archivetest extract\\|make\\|probe\\|progress` | 压缩包读写、进度与取消 |
| `--capgeo` / `--capflash [次]` | 截图那条几何链 / F1 浮层开合 |
| `--recq` / `--recordui [秒]` | 录屏那两档真进了编码器 / F2 整条链 |
| `--diskui` / `--diskbranch` | 磁盘页整条链 / 边扫边报的数和定稿树一致 |

跑任何一面都**必须有输出**（空输出 + 退出码 0 就是假绿）。全部 34 面、连同各自量到的数字：
[docs/notes.md](docs/notes.md)。

## 配置与数据目录

`%APPDATA%\\ltool\\`：`config.ini`（配置）、`apps.json`（应用索引）、`usage.json`（使用频率）、
`icons\\`、`webs.json`（网站关键字）、`plugins\\`（开发环境插件）、`deepindex.dat` + `usnstate.dat`（深索引）、
`backups\\`（注册表删除前的 .reg）、`clipimg\\`、`ocr\\`（取字引擎）、`log.txt`（运行日志）。

截图和录像**默认存在软件目录下**（`captures\\`、`recordings\\`，不往 C 盘堆）；配置页「数据」可改，
改完立即生效、只影响以后存的文件。在用的键：

```ini
[web]    engine=baidu          ; 默认搜索引擎 baidu / google / bing / duckduckgo
[index]  deep=true             ; 关掉就只搜常用目录；refreshHours=6 定时重建
[apps]   roots=…               ; 应用搜索的额外项目，一行一个目录或 exe 全路径；depth=3 往里几层
[paths]  captures=             ; 留空 = 软件目录下的 captures；recordings= 同理
```

## 已知限制

- 深索引增量（USN）要管理员权限；普通权限自动回退成"快照 + 定时遍历重建"，只是新鲜度略降。
- `reg clean` 里 HKLM 那部分要管理员（HKCU 不受影响）。
- `.7z` 那条依赖 exe 旁边的 `7zip.dll`（运行期按路径加载）：漏打包只有这一条报错，不会整个起不来。
- 截图取字的引擎（PaddleOCR det/rec + onnxruntime，约 28MB）不打进安装包，第一次用到时下载、之后离线；下不到就明说"没装上"。
- `Alt+Space` 被别的程序占了就自动降级到备选热键（日志和托盘都会说）。

''' + COMMUNITY + '\n'

io.open('README.new.md', 'w', encoding='utf-8', newline='').write(README)
print('README %d -> %d 行｜notes %d 行'
      % (len(lines), len(README.split('\n')), len(io.open(NOTES, encoding='utf-8').read().split('\n'))))
