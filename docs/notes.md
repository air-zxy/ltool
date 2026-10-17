# ltool 详细记录

[README](../README.md) 只回答"有什么功能、怎么编、怎么装、插件怎么写"。
这一份放**为什么这么做、实测是多少、踩过什么坑** —— 全部是从 README 里**整段搬过来**的原文，
一个字没删（搬的脚本 `build/verify/readme_slim.py`，跑完会自己验"原文每段还在不在"）。

## 文件索引怎么保持新鲜（原文）

## 文件索引如何保持新鲜

- **快索引（常用目录）**：启动全量扫一遍后，用 `ReadDirectoryChangesW` 实时监听目录，文件新建/删除/改名秒级反映到搜索结果。
- **深索引（全盘）**：
  - 启动时加载上次的全量快照（`deepindex.dat`），搜索立即可用；
  - **管理员权限运行**：读取 NTFS USN 变更日志做增量更新（只处理变化，实测约 0.5 秒），日志失效时才重新枚举 MFT 全量重建（约 8 秒）；
  - **普通权限**：无法读 USN 日志，保留快照使用；快照超过 `refreshHours`（默认 6 小时）后，下次启动/定时刷新时用目录遍历重建（约 17 秒，后台进行）。
- 手动 `rescan` 命令或托盘"重新扫描"会强制全量重建。

## 构建与依赖的细节（vcpkg / 许可证 / 并行编译与 PCH 的实测）

清单依赖是 `qtbase` + `libarchive` + `bit7z`（见 `vcpkg.json`；bit7z 会带进 `7zip` 和 `ghc-filesystem`，`7zip` 打出来的就是 7-Zip 官方那个格式库，只是名字叫 `7zip.dll`）。

- 走 **vcpkg manifest 模式**（根目录 `vcpkg.json`，baseline 与本机 vcpkg 对齐）；依赖会装进 `build\vcpkg_installed`，本机有二进制缓存时 configure 阶段直接恢复，不需要重编 Qt。首次拉 libarchive 约 1~2 分钟。
- **加新依赖就 `cmake -S . -B build`，别在仓库根上裸跑 `vcpkg install`**：命令行那次不读 `build\CMakeCache.txt`，会把整棵树按默认位置重装一份到 `<项目>\vcpkg_installed`（实测 2.2GB 的 Qt6 副本，构建根本不读它，只会让人以为依赖换了地方）。
- 7z 那条后端的许可证：**`7zip.dll` 是 LGPL-2.1**（其中 `CPP/7zip/Compress/Rar*` 与 `Archive/Rar/*` 另带 **unRAR 限制条款** —— vcpkg 的端口把 Rar 解码器也编进去了，所以读 rar 的那条路受这一条约束），**bit7z 是 MPL-2.0**。两份**原文**由 CMake POST_BUILD 从 vcpkg 树的 `share/<端口>/copyright` 复制成 `licenses\7zip-LGPL-2.1-with-unRAR.txt` / `licenses\bit7z-MPL-2.0.txt` 跟着 exe 一起装（不自己抄，抄的迟早和上游不一致）。LGPL 的"库形式使用"这一条是满足的：7zip 是独立的 DLL，换掉它就能重新链接。
- **别手动往 `D:\vcpkg\installed` 装**：那是 classic 树，本项目读的是 manifest 树，装了也链不上。
- **一个目标里的 53 个 .cpp 是并行编的（`/MP`，见 CMakeLists 顶部），并且有预编译头（`src/pch.h`）**。VS 生成器默认把同一目标内的编译排队串行跑，实测改一次 `src/Utils.h`（47 个 TU 包含它）串行要 **114.7 秒**，加 `/MP` 后 **16.4 秒**；再加 PCH 之后（2026-10-12 同一台机器重测）：**改 `Utils.h` 19.9 → 13.7 秒**、**改 `main.cpp` 12.0 → 10.2 秒**、**全量重建 20.2 → 17.9 秒**、**什么都没改 2.5 秒**（这 2.5 秒是 msbuild 启动 + CMake 检查 + AUTOMOC 扫 53 个 TU 的固定税，改一个 5KB 的小 TU 也要 3.1 秒 —— 也就是说日常这一档基本是固定开销，不是编译）。PCH 里故意不放 `windows.h`：33 个文件要它，但录屏那几个要 C++/WinRT 的头、USN 那几个要 `winioctl.h`，放进 PCH 等于把这些顺序约束强加给所有 TU，而真正的大头是 Qt 那几个模块头。
- 构建后自动执行 windeployqt，把 Qt DLL / platform 插件部署到 `build\Release`；但 **windeployqt 不管 vcpkg 的依赖**，`archive.dll` 及其传递依赖（zlib1 / liblzma / zstd / bz2 / lz4 / brotlicommon / brotlidec）是靠 CMake 的 applocal 复制落到 `build\Release` 的，安装包再按 `build\Release\*` 一起收走。（windeployqt 单跑只有 115ms，不是耗时瓶颈，所以留在每次构建里。）

## 打包与交付闭环（原文：一条命令、逐段报耗时、版本号、档位与体积实测）

一条命令跑完整条链，并**逐段报耗时**（哪一步慢直接看数字，不用猜）：

```powershell
powershell -File build\verify\deliver.ps1              :: 构建 → 打包(normal，6.7 秒/22.2MB) → 停旧实例 → 静默装 → 比 SHA256 → 重新起
powershell -File build\verify\deliver.ps1 -Final       :: 打包换成 lzma2/max（9.9 秒，最小的 21.5MB）—— 出正式包
powershell -File build\verify\deliver.ps1 -Quick       :: 打包换成 lzma2/fast（2.4 秒，26.5MB）—— 只想快点拿到一个能装的包
powershell -File build\verify\deliver.ps1 -NoBuild     :: 跳过构建
```

2026-10-11 实测（本机 20 核）：`build 2.5s / package 9.9s / stop 0.3s / install 2.6s / relaunch 0.2s`，**整条闭环 15.6 秒**。

等价的裸命令（脚本内部就是这几步）：

```bat
cmake --build build --config Release
"D:\software\InnoSetup6\ISCC.exe" ltool.iss
installer\ltool-<版本>-setup.exe /VERYSILENT /NORESTART /SUPPRESSMSGBOXES
```

- **版本号只有一个来源：`ltool.iss` 里那条 `#define MyAppVersion`**。CMake 在 `project()` 之前
  用它解析出 `LTOOL_VERSION_STRING`，再编成 `LTOOL_VERSION` 宏传给 core ——
  安装包文件名、卸载表里的 `DisplayVersion`、界面「关于」那一页显示的版本，是同一个数。
  读不到就 `message(FATAL_ERROR)`，宏没传进来就 `#error`：**宁可构建失败，也不要编出一个
  显示旧版本号的界面**（0.5.0 之前它散在 iss 的 #define 和注释、CMakeLists、README、
  五个交付脚本里共七处，升一次全要手动改）。现在升版本只动那一条。
- **`deliver.ps1` 装完按 SHA256 逐个点名比**：`ltool.exe`、`ltoolcore.dll`、
  `ltool-plugins\*.dll`（零插件包：插件目录只报装着几颗，不再逐颗比哈希）。上一版只比 exe —— 而自 2026-10-13 起
  代码主要在那两个 dll 里，"只比 exe"就是假绿；另外插件目标不在 `ltool` 的依赖图上，
  `cmake --build --target ltool` 根本不重编它们，所以要 `add_dependencies` 点名挂上。
- **压缩档位**（2026-10-12 重测，payload 71.1MB / 34 个二进制；括号里是整条 `deliver.ps1` 闭环）：
  `lzma2/normal`+solid **6.7 秒 / 22.22MB（12.9 秒）← 现在的默认**、
  `lzma2/max`+solid 9.9 秒 / 21.52MB（16.5 秒，出最小正式包用 `deliver.ps1 -Final`）、
  `lzma2/max` 非 solid 7.9 秒 / 22.02MB、`lzma2/normal` 非 solid 6.5 秒 / 22.43MB、
  `lzma2/fast`+solid 2.4 秒 / 26.54MB（`deliver.ps1 -Quick`）。选 normal 的理由只有一条：
  安装包是本地装的，多 0.7MB 看不见，而每轮闭环少 3.6 秒天天摸得到。
  **`CompressionThreads` 不用再试**：这次是用耗时量的（上次判"无效"只看产物 md5 相同，那只能证明输出确定），
  不设 10.7 秒、1 线程 20.9 秒、2/4/8/16 全在 10.4~10.5 秒 —— 默认已经吃满多线程。
  **payload 也剪不动**：按 PE 导入表从 `ltool.exe` 走闭包，71.1MB 里只有 4.0MB 不在闭包上，
  而那 4.0MB 全是运行时按目录加载的插件（`platforms/qwindows`、`styles`、`imageformats`、`tls`）
  和按需加载的 `7zip.dll`；最肥的 `icudt78.dll`（31.58MB，占整包 44%）是 Qt6Core 硬导入的 ICU 数据，
  实测从安装目录藏掉它 `ltool.exe` 直接起不来。
  走 `ltool.iss` 里的嵌套 `#ifdef FINAL / #ifdef QUICK`（**别写 `#elifdef`，ISPP 不认，整个编译当场 abort**，
  而 `deliver.ps1` 现在会查 ISCC 退出码 + 安装包时间戳，不会再当成"打包成功、体积没变"）。
  裸写 `Compression=lzma2` 等于 `lzma2/max`。
- 装完确认装的就是刚构建的那份（`deliver.ps1` 会直接把两边 SHA256 比了并打 `MATCH`/`DIFF`）：

```powershell
(Get-FileHash build\Release\ltool.exe).Hash
(Get-FileHash D:\software\ltool\ltool.exe).Hash
```

- 产物：`installer\ltool-<版本>-setup.exe`，**22.5 MB**（走 FINAL 是 21.8 MB）；安装目录 **约 73 MB**（ICU 三个 DLL 就占 37 MB，动不了）。
- 安装行为：默认按用户装到 `%LOCALAPPDATA%\Programs\ltool`（`PrivilegesRequired=lowest`，无需管理员）；本机实际装在 `D:\software\ltool` —— 同一个 `AppId` 第二次安装会**沿用上次的目录**，所以闭环命令里不写 `/DIR` 也会覆盖到原处；全新机器要指定就加 `/DIR="D:\software\ltool"`（与 `config.ini` 里 `[install] path=D:\software` 一致）。含简体中文向导；可选任务"创建桌面快捷方式""开机自动启动"（HKCU Run，卸载时清除）。
- `CloseApplications=yes`：安装会等正在运行的 ltool 退出，**静默安装可能卡几十秒**，不是失败。
- 卸载不会删 `%APPDATA%\ltool` 用户数据（索引/插件/任务配置/日志保留）；卸载器 `unins000.exe /VERYSILENT`。
- 构建时会先生成 `resources\ltool.ico`（由 `tools\genicon.cpp` 编译的小工具绘制，7 个尺寸）并嵌入 exe 资源，exe/快捷方式/安装包共用同一图标。
- Inno 不会自动删掉"已经不在包里"的旧文件，剪依赖时要同时往 `[InstallDelete]` 里点名，否则升级完安装目录还是老的体积。
- 打包前先跑回归（无界面，一行结论）：`build\Release\ltool.exe --archivetest extract <包> <目录>`、`--archivetest make <输入> <输出> [7z] [档位]`、`--archivetest probe <文件>`、`--archivetest progress <输入> <输出> [7z] [第几次按取消]`；整套 16 项在 `build\verify\archtest.ps1`（zip / 7z / tar.gz 解压、GBK 名字的老 zip、自产包两个方向往返、单文件 `.gz`、"是不是压缩包"的三种判定、硬解纯文本，加上进度/取消三项：7z 全程、7z 第 3 次回调按取消、zip 全程当对照；最后用 `7z -t` 验我们自产的两个包外部工具也认）。脚本和临时产物都放 `build\verify\`，`build\` 已在 `.gitignore` 内。

## 打包的等价裸命令（deliver.ps1 内部就是这几步）

等价的裸命令（脚本内部就是这几步）：

```bat
cmake --build build --config Release
"D:\software\InnoSetup6\ISCC.exe" ltool.iss
installer\ltool-<版本>-setup.exe /VERYSILENT /NORESTART /SUPPRESSMSGBOXES
```

## CI 的细节与踩过的坑

### GitHub Actions：推标签就出安装包、发 Release（`.github/workflows/build.yml`）

- **CI 里从 `vcpkg list` 取版本号：按列取，别拿正则抠数字。** 插件仓库那条 CI 第一次红就红在这：
  `grep -oE '[0-9][0-9.]*'` 对整行找数字，第一列 `qtbase:x64-windows` 里的 **64** 先命中，
  `head -1` 取走它，于是把一次正常装好的 6.10.2 判成"版本不对"。正解是
  `vcpkg list | tr -d '
' | awk '/^qtbase:/{print $2; exit}'`（再把 `#3` 那种端口修订号从 `#` 截掉）。
  `tr -d '
'` 不是装饰：vcpkg.exe 在 git-bash 下输出的是 Windows 行尾，带着它比较永远不等。
- **"取不到那一行"和"版本不对"要分两条报错**：前者是上一步的装依赖没真装上，后者是钉的 commit 和本机树不一致，
  混成一句话下次就得重新查一遍。
- **本地能把这一段单独验掉**：把那个 step 的脚本体导出成 .sh，`PLUGINS_VCPKG=D:/vcpkg QT_MUST_BE=6.10.2 bash …`
  跑三种情况（对上 / 对不上 / 根本没有 vcpkg），比推上去等一小时快得多，也才知道它真的会红。

一条 job（`windows-2022`）从头到尾就是本机那条链的翻版：**装依赖 → 编 Release → 跑无界面回归 → ISCC 打包 → 传 artifact →（只有标签推的 run）发 GitHub Release**。

- **触发**：推 `v*` 标签 = 打包并发布；推 `main` 或手动 `workflow_dispatch` = 只打包不发（所以"这版能不能出包"随时可以验，不必先承诺发版）。
- **预发和稳定就靠标签本身分**：`v0.8.1-beta.1` 建的 Release 自动带 `--prerelease`，`v0.8.1` 是稳定版。界面「检查更新」打的是 `/releases/latest`，而 **GitHub 的 `latest` 实测会跳过 draft 和 pre-release**（拿 `microsoft/PowerToys` 对的：列表里最新是 `v0.102.2862.0 pre=True`，`latest` 回的是 `v0.101.2362.0`）—— 所以预发不会推给走稳定通道的人，客户端一行代码都不用改。配套的两条硬规则：`compareVersions` 认后缀且**同数字时带后缀的那个更旧**（`0.8.1-beta.1 < 0.8.1`，装预发的人因此看得见正式版可升；以前整串按 `.` 拆段，`1-beta` 读成 1、尾巴 `.1` 还多一位，预发被当成比稳定新）；`project(VERSION ...)` 只吃纯数字段，所以 CMake 把 `0.8.1` 交给 `project`、把完整的 `0.8.1-beta.1` 交给 `LTOOL_VERSION` 宏（界面显示哪个由宏说了算）。
- **标签必须和包里的版本号对上**：`v<尾巴>` 不等于 `ltool.iss` 里的 `MyAppVersion` 就当场失败，杜绝"发出去的安装包写着上一个号"。
- **依赖用清单模式**（和本机一样）：仓库根有 `vcpkg.json`，装 `qtbase`/`libarchive`/`bit7z`；那棵 vcpkg 树先 `fetch` 到 **baseline 那个 commit**（`--depth 1` 只 clone 分支 tip 会找不到 baseline，那样清单模式直接拒绝）。baseline 从 `vcpkg.json` 里 `grep` 出来，**不在 yml 里抄第二份**，免得两处漂移。
- **装依赖和配 CMake 是两步**（2026-10-15 拆开，为了"慢在哪一眼看得见"）：`vcpkg install --x-install-root=build/vcpkg_installed --clean-after-build` 单独一步，日志就是 `vcpkg-install.log`；配 CMake 那一步因此只对着已经摆好的树，几秒过。vcpkg 树本身改成 `git init` + `git fetch --depth 1 origin <baseline>`（原来那份全量 clone 要拉 300MB+），缓存里已经有 `vcpkg.exe` 就跳过 bootstrap。`--clean-after-build` 清的是几十 GB 的**源码/中间树**，不是 `build/vcpkg_installed`，所以它和下面那条缓存不打架。
- **缓存四样，全在工作区里**（`.ci/vcpkg`、`build/vcpkg_installed`、`.ci/vcpkg_downloads`、`.ci/vcpkg_archives`）：`actions/cache` 对 `GITHUB_WORKSPACE` 之外的路径**静默不保存**，放外面等于每次重编 Qt（约一小时）；只缓存 vcpkg 树而**不缓存安装产物**也一样白干 —— 干净检出后 `build/` 是空的，那棵装了 qtbase 的树还得重解甚至重编。键带 `vcpkg.json` 的哈希，`restore-keys` 回退到旧键，加一个依赖时没动的 qtbase 从二进制缓存里秒级复用。二进制缓存双份：`VCPKG_BINARY_SOURCES=clear;x-gha,readwrite;files,<工作区目录>,readwrite`（云端那份要 `actions: write`；本地文件那份跟着缓存一起存，x-gha 挂了还有它）。
- **`timeout-minutes` 从 360 降到 180**：360 那份余量是把"冷缓存 + 从源码重编 qtbase"当成每天要跑的常态。量到的两个数：老形状那条「配置（vcpkg 在这一步按清单装…）」被下一次推送顶掉时**已经跑了 22.7 分钟还没结束**；新形状第一次跑（缓存 miss、14 秒就撞上面那条二进制缓存报错）整条 job 34 秒。180 是给"冷缓存真要从源码编一遍 qtbase"留的预算，不是量出来的常态。
- **生成器是 Visual Studio 17 2022，不是 Ninja**：CMakeLists 把插件拷进 `${CMAKE_BINARY_DIR}/Release/ltool-plugins`、`ltool.iss` 按 `build\Release\*` 收包 —— 换成 Ninja 就没有那个 `Release` 子目录，配置能过、包是空的。也因此**不用** `ilammy/msvc-dev-cmd`（它会把 `VCPKG_ROOT` 指到 VS 自带那份 vcpkg，后面静默换掉缓存好的树）。
- **本地并发和 CI 并发不是一回事，别互相抄**（2026-10-15 本机 20 逻辑核全量重编实测，四种形状各跑一遍）：默认（无 `--parallel`，`/MP` 吃满）94.0s → `--parallel 8` **38.4s** → 12 = 37.5s → 16 = 36.5s（8 就到平台期）；而 `--parallel 8 + -p:CL_MPcount=2` 反而 68.2s、`16 + CL_MPcount=2` 68.4s —— **限每颗工程的编译线程数在这台机器上是净损失**。所以 `deliver.ps1` 用 `--parallel 8`，CI 那台 2 核的才压到 `--parallel 4 + CL_MPcount=2`。
- **`VCPKG_DEFAULT_BINARY_CACHE` 指着不存在的目录时 vcpkg 不是"当没缓存"，是当场拒绝干活**：`error: <defaults>: error: Environment variable VCPKG_DEFAULT_BINARY_CACHE must be a directory`（run 38648353323 就这么红在 14 秒，因为缓存 miss 时 `actions/cache` 什么都不还原，那棵树根本不在）。修法就是装依赖那一步 `mkdir -p` 把归档目录和下载目录都建出来。本机拿一份只依赖 zlib 的清单**双向验过**：目录不存在 → 一模一样那句 error；先 `mkdir` → 2/2 装完。注意 `--dry-run` 量不出来（它不走二进制缓存那条初始化），要真装。
- **CI 编 Release 的并发压到 `--parallel 4` + `-p:CL_MPcount=2`**：`windows-2022` 只有 2 核 / 7GB，原来 `--parallel 8` 乘上 CMakeLists 里那条全局 `/MP`，同时能起来十几个 `cl.exe` 和好几个 `link.exe` 抢那点 CPU 和内存 —— 抢出来的不是快，是换页和 I/O 堵塞。总并发压在 4~8 之间。
- **MSBuild 那个开关只能写 `-p:`，不能写 `/p:`**：这一步是 `shell: bash`，MSYS 会把 `/p:CL_MPcount=2` 当 Windows 路径处理掉，MSBuild 收到的是 `p:CL_MPcount=2`，当场 `MSB1008 只能指定一个项目`（本机 2026-10-15 实测撞的，和 `/DFINAL` 那条同一个坑）。同理，`tee` 之前都要 `set -o pipefail`，否则最后那段管道把退出码洗成 0，编坏了也不会红。
- **自测跑在 offscreen 平台插件上**：runner 上能不能拿到窗口站不作保证，而这条判据要的是"控件建得出来、布局算得对、点了有反应"。`windeployqt` 不带 `qoffscreen.dll`，所以那一步从 vcpkg 树里**借一颗**进 `build/Release/platforms`；`ltool.iss` 的 `Excludes` 点名排除它，**安装包收不到这颗测试专用的插件**（本机验过：装完 `platforms\` 里只有 `qwindows.dll`）。
- **每个旗标都必须有输出，空输出算失败**：这套自测里 `--smoke` 曾以"0 字节 + 退出码 0"的形状哑过（命令行被单实例守卫转交给已常驻的实例），CI 这边直接把它判成红。CI 跑的是 `--smoke --plugintest --rowmenu --username --about --cfgcheck --appdirs --pinyin --diskbranch --archivetest`；**跳过的点名写进日志**：`--capgeo --capflash --recq --rectest --recordui`（要真屏幕/DWM 会话）、`--searchtime`（整盘扫描太慢），别让"CI 绿"看着像"这些也过了"。（`--mfttest` 原来也在"要管理员所以跳过"那一串里 —— 2026-10-17 查明 runner 是提权的，已挪进真跑那一串。）
- **发出去的包走 `-DFINAL`**（本机同一棵树实测：normal 22.51MiB / 6.8 秒，FINAL 21.79MiB / 10.3 秒 —— 省 0.72MB 多花 3.5 秒；本机的 `deliver.ps1` 不带它，因为每轮闭环少 3.5 秒是天天摸得到的）。开关用**短横不用斜杠**：那一步是 bash，MSYS 会把 `/DFINAL` 当路径转换掉，ISCC 于是报 `You may not specify more than one script filename`（本机实测撞过）。
- **发一版就三步**：① 改 `ltool.iss` 的 `MyAppVersion`（要预发就写 `x.y.z-beta.N`）；② 提交并推 `main`（这一步之后 workflow 文件才在默认分支上，**标签早于 workflow 推上去是不会触发的**）；③ `git tag v<和号一模一样的串>` 再 `git push origin v<那个串>`。第 ③ 步是对外的，由人按下。

## 自测旗标逐条（每面旗量到的数、期望值为什么改过）

## 自测旗标逐条

上面那张表只写"这面旗标验什么"。下面这段是前一版 README 里「命令行参数」那**一整条**，
原样搬过来的 —— 每一面旗标量到的数、期望值为什么改过、反向验怎么做到的，都在里面。

`--show` 启动即显示搜索框；`--smoke` 无界面自检；`--mfttest` 需管理员，诊断 MFT 枚举；`--searchtime 词1 词2` 无界面，加载索引后逐词打印深索引搜索耗时与前 3 条结果（**272 万条下现在 9~38ms/次**：两个字的词 9ms、`chrome` 38ms。改之前是 540~680ms —— 每命中一条就 `fromUtf8` 造一条整路径 QString 收进一个大数组，而绝大多数命中根本活不到前 20 名。现在按 (来源, 分数, 名字长度, 路径) 分 8 个桶、每桶只留 2×limit 条候选，比大小时不造 QString（UTF-8 的字节序就是 BMP 字符的 UTF-16 序，只有含 emoji 那类 4 字节序列才退回 QString 比），首字节用 memchr 跳，每个卷的条目区间切成段丢给最多 8 个线程各扫各的再并桶）；`--searchdump 词1 词2` 无界面，把每个词的前 20 条结果**按顺序整条打出来** —— 改搜索实现最怕「快了但顺序变了」，所以这条就是改前改后各跑一遍、拿 diff 当判据的入口（2026-10-12 那次重做：同一份实现连跑三遍互相比对一致、和老实现逐行一致，只有索引条数那一行随磁盘变化）；`--disktime` 无界面，A/B 对比两种磁盘遍历（`DiskScan` 一次建树 vs `QDirIterator`）在 C 盘上的耗时并列出前 5 大目录，还会把 `DiskScan` 自己那三段（枚举 / 合并 / 排序）连同文件数、目录数一起打出来 —— 加这三段是因为第一次拿它得出了「新遍历比老的慢 0.7×」这个**假结论**：两趟共用一个进程，先跑的那趟把目录项喂进了系统缓存，比的其实是缓存冷热不是实现高低；改成冷/冷、热/热各一轮之后实测 **DiskScan 14.5s（热）/ 20.2s（冷）vs QDirIterator 26.3s（热）/ 29.4s（冷），热态 1.8×**（这台机器 48.1 万个目录 / 142 万个文件 / 254.5GB；三段拆分里"扫盘"占 20198/20244 ms，合并建树 31ms、排序 5ms —— 时间全在枚举上，建树和排序根本不是瓶颈，所以优化方向不在那儿）；`--diskscale` 无界面，拿环境变量 `LTOOL_DISKTHREADS` 把同一棵树按 1/2/4/8/16 线程各扫一遍量扩展性（**连跑两遍**：1 线程 22.0 / 22.9 秒，2~16 线程落在 12.9~17.6 秒之间，第二遍里 4/8/16 的顺序还会互换 —— 也就是加线程最多换到 1.5× 上下、且不稳，目录枚举在这台机器上早就不吃 CPU 了；上一版这里写的「1→4 线程 3.2×」拿第二遍复测**没复现**，已按实测改掉）。结论：**加线程救不了首扫**，所以「磁盘分析太慢」能做的两件事是别重复扫、边扫边出数）；`--diskbranch` 无界面，验「边扫边报的一级分支大小」和最后那棵树是不是同一回事（4 条：分支数齐、每个分支报的数和定稿树逐个对上、一级分支之和 = 全盘减去卷根直属、第一个分支在整趟结束之前就出数 —— 实测 20 个分支 206ms 就出第一个、整趟 13.1s）：这条必须能红，孩子算漏或算两遍都会被第一条抓到，而界面上先给用户看的那几个数就是它报的；`--diskui` 无界面（慢，要真扫一整盘，所以不进 `--plugintest`），把磁盘页那条链整条走一遍（7 条，判据写在插件 `DiskPage.cpp` 里 —— 它要点真按钮、看真行卡片，主程序链接不到那些类型）：控件都在 → 点「整理」→ **途中状态行报了「一级分支已扫完 N / 20」且列表已经摆了行卡片**（实测 1.56s 摆出 13 行，不是空白干等到 16s）→ 定稿后状态换成树上的数、并写下这次扫描的时间 → 换到没扫过的盘列表让位（不拿上一个盘的数当结论）→ 换回来**直接用缓存**把行摆出来（没重扫）；`--username` 无界面验「Windows 用户名」那一页（30 条，**一次都不真的改账户/建账户** —— 那是不可逆级别的动作）：`psQuote` 把单引号翻倍（拿一条注入串试：`x'; Add-LocalGroupMember ...` 拼出来必须首尾各一个引号、里面没有裸单引号）；名字合法性（空/带斜杠/带引号/带换行/账户名 21 字 全被拒，20 字和中文显示名通过）；拼出去的脚本长什么样 —— ADSI 那串路径必须是**拼出来的** `"WinNT://" + '机' + '/' + '号' + ',user'`（整串塞进双引号里再套单引号，单引号就成了名字的一部分，实测错过一次）、新建用户按 SID 找管理员组而不是写死组名、不勾管理员就**整段不加**；真实读取（只读）：当前账户名非空、SID 以 `S-1-` 开头、用户目录真在盘上、查一个不存在的账户得回 `exists=false`；页面三颗按钮**从布局可达**（不是 `findChild` 数得到就算）；再加工具页结构判据：`embedded>=0` 的槽位互不重复且是 0..N-1 连着（`ToolsPage` 的占位数正好盖住，卡片指向一个没建的页就会被这条抓到）。脚本里那 15 个 cmdlet 参数名（`-PasswordNeverExpires`/`-AccountNeverExpires`/`-NewName`…）是拿 `build/verify/psparam.ps1` 在本机 `Get-Command` 逐个验过的，不是凭记忆写的；`--rowmenu` 无界面，验磁盘页两栏列表（「可清理位置」和「占用分析 · 本层目录」共用同一行卡片）右键该弹什么：存在的目录只有一项、叫「打开文件夹」、点得动，且**这项的 data 就是这一行自己的目录**（点开之后界面拿的就是它，所以这条等于验“开的是它、不是别的”）；目录已经不在了就只剩一项灰的、写「这个目录已经不在了」；再加两条几何/结构断言（行卡片的 contextMenuPolicy 是 CustomContextMenu，下面那行路径是它自己的子控件 —— 右键点在路径那行上也要冒得到）。判据写在 DiskPage.cpp 里（行卡片藏在那个文件的匿名 namespace），菜单构建拆成了独立的 `makePathMenu`，因为真按一下就会拉起资源管理器，那一下不能放进自测；`--archivetest extract|make|probe|progress` 无界面跑压缩包读写（回归用，见上；`make` 的第 5 个参数是 7z 档位数字 0-9，zip 那条不吃；`progress <输入> <输出> [7z] [第几次按取消]` 是专门验进度与取消这两条只有界面在用的线的：分母准不准、done 单调不减、回调返回 false 之后有没有当场停下、半成品 `.part` 有没有被删干净）；`--gridtest` 无界面验「图片排版」那张板（34 条，判据在 plugins/grid/gridselftest.cpp）：格数只截断不重排（九宫摆满退回四宫还是那四张）、四格收四张其余算拒收、后缀不是图或盘上没有就一个都不收、拖到空格=挪过去、拖到有图的格=交换、空格拖不动、✕ 只空那一格；合成验到像素（150x150 = 60*2+10*3、四边和缝是背景色、每格中心就是那张图的颜色、空格子是背景色不是黑的）；**板上预览和出图必须同一块构图** —— 拿一张 120x40 的红|绿|蓝横条，中间那条正好 40 见方，整张压扁就会漏出红和蓝（compose 两条 + 离屏 grab() 两条），反向验过：把居中裁正方形换成整张映射，这四条当场点名；再加两条几何（板子再扁格子也是正方形、整块居中而且真留了白 —— 只断言左右对称的话两种写法都绿，所以量的是留白有多大）和一条探针自检（✕ 角标必须比图本身暗，防止离屏 grab 画出空图把全部像素判据一起骗绿）；坏文件那三条验的是界面说实话：后缀对的坏图先进格子（人得看到自己拖错了）、合成时那一格留背景色、画得和空格子一样素（不是一块黑）；`--gridtest` 这一轮又加 5 条盯「生成宫图先弹框展示效果图」：弹框里「保存这张 / 取消（不存）」两颗都在布局里、那行字 == `previewCaption`（写的是**存出去的全尺寸**和落点目录，不是屏上这块缩图）、缩图真有像素、按「取消」这个框回绝；预览**只缩不放大**（图本来就小就按原尺寸摆，放大出来的糊图会让人以为存出去的也是糊的）。顺序那条和它的反向验见上面「自测旗标逐条」的 `--gridtest`；`--linuxdotest` 无界面验「linux.do 盯帖」离线那半条（33 条，判据在 plugins/linuxdo/linuxdoselftest.cpp，跑在沙箱 APPDATA 里因为它要写 linuxdo/* 那一节）：解析（id 乱序不重排、没标题那条丢掉、URL 按 id 算、Cloudflare 那张 HTML 挑战页解出 0 条、空字节不崩）、命中（标题撞 / 只有标签撞 / 撞两个词只算一条 / 上一轮报过的不再报 / 空词不把全部帖子都算命中）、前缀拼法（带不带尾斜杠、两边带空格、留空=只用直连）、要不要弹那一句的决定（第一次只播种不弹、没命中不弹）、配置（默认开着 / 10 分钟 / 三个词 / 中转前缀就是实测那条；0 分钟夹到 3、9999 夹到 240、关键词去空去重 trim、三样都真落盘、再建一张页回填的就是盘上那份）、页面九件控件**从布局可达**（数 findChild 不等于摆在页面上）。反向验过：把 plugin.json 的 `Activate` 摘掉，`--plugintest` 那条「牌和导出的符号一致」当场红成 `got=[0/1]`。`--linuxdonet` 是另一面旗（6 条，要联网、不进 CI）：真去取一趟，实测 4954ms 拿到 30 条、状态行说出走的是中转、第一趟只播种。**这条路是量出来的不是猜的**：直连 linux.do 在这台机器上 DNS 被污染（解到 Facebook 的 IP），走系统代理能到 Cloudflare 但非浏览器客户端一律吃挑战页 —— **带着浏览器导出的 cf_clearance + 完整 Chrome 请求头也一样**（它绑 TLS 指纹），`latest.json` / `session/current.json` / 单个帖子三条实测全 403，所以给 cookie 也换不来登录态；只有 `https://r.jina.ai/<原地址>` + `X-Return-Format: text` 能拿到真 JSON（全站最新 30 条、categories 12 个分类、单个分类流 30 条，4.5~7.3 秒）。发帖速率实测 0.64 帖/分钟（一页 30 条跨 47 分钟），所以 5~30 分钟一轮漏不掉，间隔下限卡 3 分钟。**「盯帖没效果」的根因就是这个**（2026-10-16 实测）：那 90 条新帖里标题含「福利/女装/公益」的 **0 条**，但在分类 36「福利羊毛」里的有 **4 条** —— 「福利」是**分类名**，不在标题里，只比标题和标签就永远 0 命中。现在 `matchNew` 也比分类名（`categories.json` 缓存进 config.ini，一天一趟，拉不到就自己补拿，页面上那行「分类表：已拿到 N 个 / 还没拿到」把降级状态写出来）；`--linuxdonet` 里那条端到端最硬：拿真那一页里**帖子最多的分类**的名字当关键词，必须撞到、且撞到的确实在那个分类里（实测 30 条里「搞七捻三」25 条，撞到 25 条）。**两个自己造的坑也一并修了**：① 启动时帖子和分类表各抓一次、表为空时两条路径又重复触发 = 三个并发请求，把免费中转打到 25 秒全超时（日志里三条一模一样的「直连不通」）—— 现在**一次只飞一趟**，帖子这趟回来了才轮到表（`m_wantCats` 排队）；② 表是这轮**之后**才到的话，那 30 条就永远没被分类名比过 —— `advanceSeen()` 因此规定：没表的时候**不推进 lastSeen**，下一轮拿到表再把同一页比一遍（宁可晚报不要漏）。真机第一发命中：16:25:19「新命中 1 条 · 分类表 12 个」，撞的是「福利」在分类「福利羊毛」，标题是 workbuddy补偿已到位（id 3033129）—— 正是只比标题时永远抓不到的那一类；`--linuxdotest` 另有一条反向断言：分类表传空 → 就是 0 命中（老行为）；`--pinyin` 无界面验拼音匹配（52 条断言）：表和码点必须对齐（错一位就会把"垃圾"当"任务"）、多音字（应用那一档 chongqing/zhongqing 都算，直达那一档只认表里第一条 —— `An` 被「广告」的生僻读音抢页那次收窄的）、全拼允许中段而首字母只认开头、纯英文名不掺和这档、单字母不启用、耗时（810 个名字首次展开 979us、缓存后扫全表 79us/次），**最后真建一个不显示的启动器窗口往输入框里打字**，验 `mumumoniqi` 能搜到本机的"MuMu模拟器"、`cipanqingli`/`cpql` 能直达"磁盘清理"，再加 `renwu` 那两条（**应用节必须在**、**工具直达那一节不能把应用顶掉**）和 `qingli` 一条（取整不伤直达：它不是「磁盘清理」的开头，但是关键词「清理」的开头），再加 2026-10-16 那四条盯"直达不清场"（`cpql` 一条盯"工具还在第一行"、一条盯"末尾那条「用搜索引擎搜索」加得回来"；`disk` 两条是 env-gated，索引里没有 `Disk*` 应用就点名跳过 —— **别拿 `python` 当这一类探针**：他 `webs.json` 里显式配了 `python`/`rust`/`cpp`/`npm`/`pypi`/`qt`/`docker` 这些网站关键字，那几条走的是 `webKeywordWins`「整节让路」，根本到不了直达这一站，用它测等于测错东西）。**这四条里真正盯得住"有人把 return 加回来"的只有两条**（「用搜索引擎搜索」那条 + `disk` 的应用节那条）—— 反向验就是这么定的：把 `rebuildList(); return;` 塞回去重编，`小节` 立刻塌成 `[工具]` 单节、`disk` 应用节=0，红的正是这两条，而"排第一"那两条照绿（独占时工具本来也排第一，它们不区分这件事，别当清场探针）。另：**反向验之前必须先确认补丁真进了二进制** —— 我第一次拿 bash heredoc 里的 python 改 `SearchWindow.cpp`，脚本自己打印了 "patched"、`assert` 也过了，但文件 mtime 没动、`grep` 拿不到那行字，那一轮"红了几条"全是拿旧 dll 测出来的（作废）。再加 `qq` 两条 —— 半角 `qq` 必须出应用节（这台机器上 `apps.json` 里 `QQ` 排第 1，复算过排序不是问题）、**全角 `ＱＱ` 必须出同一个应用节**（这条第一次跑是红的：偏移写成 `0xFEE` 把 `Ｑ` 变成了 `3`，端到端断言当场抓到，所以没再单加一条纯函数断言）；顺带给 harness 加了一个把结果里出现的小节原样打出来的探针，查「某个词为什么搜不到 X」时看它比看断言红不红有用；`--archspeed [每文件KB] [文件数]` 无界面量压缩包读写的吞吐（默认 4096×15=60MB 伪日志，另可 `--archspeed 2 2000` 量海量小文件）：我们 vs 系统里的 7-Zip，zip/7z 两个方向各打一行 MB/s 和产物大小，并把 7z 的 1/3/5/7/9 档整条曲线跑一遍，**每档都验"我们自己的读取器解得回来 + `7z t` 退出码 0"**（换档位最怕快是快了、别人打不开），夹具全在 `%TEMP%\ltool-archspeed`；`--ocrtest <图片>` 无界面打印图里认出的文字与耗时；`--ocrlayout <图片>` 无界面打印每行每词的包围盒和切出来的字格（划词的坐标就靠它验）；`--pinpick <图片>` 无界面，把鼠标事件直接送给一个不显示的置顶窗，跑「图上划词」的 9 条断言（拖半行/单击整词/跨行/词内半个字/跨词空格/Enter 全部/Esc 退出），跑完归还剪贴板；`--recprobe [ms]` 无界面量录屏的地基：系统里有哪些视频/音频编码器、MP4/H.264 写 30 帧再读回来要多久、`Windows.Graphics.Capture` 按窗口取帧能不能用（会弹一张自己的小窗 1 秒多）；`--recq` 无界面验录屏那两个新选择真的进了编码器（13 条）：`Recorder::encodedSize` 的五种画幅（不缩 / 等比缩 / 奇数先取偶再缩 / **只缩不放大** / 结果永远偶数）、码率随画幅下降，然后**拿真编码器录四局自制卡**并 `inspectMp4` 读回来比：不缩那局文件画幅 = 640x400，选 ≤320 那局文件画幅真是 320x200 且**亮度网格仍然上亮下暗**（缩小不许把画面翻过来），同内容同长度缩小那局文件必须更小；画质两局用**逐像素随机噪声**（第一版用的是 24x12 大块，放大后太平滑，流畅和超清只差 7% —— 那条断言测的是内容不是档位，所以改成噪声并要求"至少大 10%"，实测 +26%~37% 两遍稳定）；`--rectest [秒]` 无界面录一个自造的会动的卡片，然后 `inspectMp4` 把文件解回来数帧（帧数/时长/有没有音轨/画幅），**末尾再录 1.5 秒整块屏幕**（`CreateForMonitor` 那一路：能开起来 / 采到帧 / 全程无错 / 收尾后 `running()` 回 false / **解出来的画幅正好等于这块屏幕的物理尺寸取偶** / 时长对得上），画面变化靠的就是那张每 30ms 换数字的卡，不合成鼠标；`--recordui [秒]` 把 F2 那条链整条走一遍（45 条断言，2026-10-14 加了「画质/画面」两颗下拉框那 7 条，2026-10-15 又加 12 条盯选择窗这三件事：「开始录制」那颗存在且摆在布局里、**单击和双击条目都不再开录**（合成一对鼠标事件送给列表的 viewport，数 `picked` 发过几次）、按那颗才 emit 且 emit 的就是当前选中那条、**按住标题那一行能把窗拖走**（sendEvent 给这个窗自己，pos 必须正好挪过 (60,40)）、**选中一条就真拿到 WGC 帧**（指着那张每 30ms 换数字的自造卡测，它一直在重绘所以这条不飘）、预览尺寸 == `Recorder::encodedSize(采集, 当前档)`、换「画面」档之后预览跟着换成 ≤1280 的画幅（拿整块屏幕量，那张卡只有 360 宽任何档都不缩）、窗一收起就把取帧那一路放掉。选择窗自己用 `WA_DontShowOnScreen` 摆成"可见"——不往他桌面上贴像素，但布局和 isVisible 全走真流程。反向验过：把三处实现逐个掐掉（重新接上 itemActivated / 拖动的 press 短路 / refreshPreview 直接 return），红的正是那 6 条，一条不多一条不少）：在不在布局里、四档齐不齐、**当前档 == 配置里存的档**、程序化改档之后立刻落盘、**再建一个选择窗回填成刚选那两档**、跑完把用户原来的档还回去）：列表第一条必须是「整个屏幕」且带着“这是屏幕不是窗口”的标志位、第二条是普通窗口 → 悬浮条在计数 → 用 `click()` 程序化按「停止」→ 报已保存 → 文件能解出帧 → **再开一局按「不要了」：条上写“已取消，没保存（丢掉 N 帧）”，并且断言 `QFile::exists` 说那个 MP4 真的不在磁盘上了**（判的是文件，不是它自己写的那句话）→ **选择窗的四条退路**（「✕」那条是 `findChild` + **布局可达性** + `click()` 三步：
    按钮存在、真的摆在布局里、按下去窗就收起 —— 数得到控件不等于摆在页面上，见「配置页」那条教训）、往列表里 sendEvent 一个 Esc 键能收起（`QLineEdit` 自己会吃掉 Esc，所以产品里是给输入框和列表两个子控件都装了事件过滤器）、给它派一个 `WindowDeactivate` 能收起（=“点到别处就自己收起”那条，不合成鼠标）、`RecordUi::startViaPicker()` 连按两次能收起（第二次就是 F2 的取消分支，走的是全局那个 picker 而不是测试里这个局部对象）；后面还是改大小会收尾、一次失败只写一条日志、自己收尾之后 F2 还能再开一局那几条；`--pindel <图片>` 无界面，跑「右键往期缩略图删除」的 10 条断言（往期计数 / `captures` 里的文件真没了 / 删的正是当前那张时自动挪到邻居 / 越界不炸），它把 `APPDATA` 临时改到 `%TEMP%\ltool-pindel`，不会碰你真实的往期截图；`--ocrpanel <图片>` 真开一次"提取文字"结果窗；`--ocrget` 无界面，只把 OCR 引擎那三个文件下到 `%APPDATA%\ltool\ocr`（每 0.5MB 报一次进度）；`--ocrchain <图片>` 把「首次使用」那条整链跑一遍（11 条断言：沙箱里没有引擎 → `available` 说要下载 → 下载 → 进度只朝前且到 100% → 下载中再要一次不会重开 → 文件齐 → 当场认得出那三行字），它把 `APPDATA` 临时改到 `%TEMP%\ltool-ocrchain`，会真下 26MB、但碰不到你真实的那份；`--memloop [轮数] [宽x高]` 无界面，用来回答"做完某事内存没回落"：每轮先解压一个自造的 32MB 包（随机字节，压不动）再录一段指定尺寸的窗口，逐步打印私有字节 / 工作集 / 句柄数的增量（判读：私有随轮数线性涨=真泄漏；涨两轮封顶=分配器留着页复用，不是泄漏）；`--toggle` 冷启动跑，6 条断言核对"按 Alt+Space 到底出不出来"（冷状态呼出且必须在某个显示器内 → 最小化后再按必须拉回来 → 失焦自动收起 → 收起后再按能再呼出 → 在前台时再按才收起），它会真的把启动器弹出来一两秒；`--pathcfg` 无界面，验配置页那两行存放目录真的管用（沙箱 `APPDATA`，9 条断言：默认值 → 写 `paths/captures` → 新截图落进新目录且不碰老目录 → `CaptureHistory::reload()` 后往期还是那一张 → 录像目录能建能写。其中"光问一句不该把 `captures` 建出来"那条**判的是这一问之前之后有没有变化**，不是"它在不在"：装在软件目录那份的 `captures` 本来就有用户真实截图，2026-10-15 对着安装包跑就红了一项（build 那份绿、装出来的那份红），改成比较开跑前后的状态之后两边都绿、而"光是问一句就 mkdir"照样能红）；`--appdirs` 无界面，验配置页「应用搜索的额外项目」那条路（沙箱 `APPDATA`，12 条断言：层数 0/1/2/3 各收到哪些 exe、中文目录名要进得去、卸载器不算、直接指到一个 exe 上也收、目录不存在只是跳过，**再加过滤那两条**：一棵造好的夹具树里 `gui.exe`（真 PE 头、Subsystem=2）该收，`tool.exe`（Subsystem=3 控制台）、`FooUpdater.exe`（助手名）、`7.4.1/Inside.exe`（版本号目录）、`locales/X.exe`（运行时目录）都不该收；而**点名指到 `tool.exe` 时必须收到它**；再加一条「同目录已有主程序」：夹具里点名 `gui.exe` 之后，同目录的 `gui2.exe`/`notepad.exe` 都不该再被自动收（期望值我第一次写错了 —— 写成还留着 notepad，红了之后确认是**规则本来就该这样**、不是代码错，改的是期望值）。`--pinyin` 那边还有一条端到端：**搜 `qq` 的第一行必须是 QQ 本人**（判的是 `resultAt()` 里的标题，不是「有没有应用节」—— 空键那个 bug 的形状正是有节、顺序错）。PE 头是测试自己按布局拼的 512 字节镜像。**偏移是实测出来的：Subsystem 在可选头 +68**（本机拿 74 个真 exe 验的，+70 紧跟着就是合法的 DllCharacteristics 掩码）—— 第一版按记忆写成 +44，**实现和自造夹具一起错，那条测试当场白过**；所以夹具之外又拷了两个系统里现成的：**这两条第一次跑是白过的**：那两个目录夹具被写到了另一棵树下、根本没建出来，断言照样绿 —— 补真夹具后反向验过：把 `looksLikeHelper` 整个摘掉，断言立刻红成 `got=[FooUpdater,Inside,X,gui]`，夹具建在沙箱数据目录里且删之前先断言那是不是算出来的绝对路径；`--capgeo` 无界面，量截图那条几何链（真屏抓取 + 一张自造的 DPR=1.5 画布 + `drawPixmap` 的 sourceRect 按哪种坐标解释），这是"150% 缩放截不全全屏"的判据；`--cfgcheck` 离屏建一次配置页，数「下载代理」是不是只有一行、那行没生效过的「扫描根目录」有没有被加回来、应用那一栏（项目列表 + 往里几层）在不在、「添加应用…」和「添加目录…」是不是**各只有一颗** —— 配置页是 GUI，我不动他桌面，就只能把「某行出现几次」变成可断言的数字（2026-10-12 那次「下载代理」出现两遍就是这么抓的）。**同一天还补了一条结构断言：每个分组框都必须能从 `page.layout()` 走到**（滚动区里那层不是布局项，要专门进 `QScrollArea::widget()` 再走一层）。因为「应用搜索的额外目录」那一块 new 出来时认了 `this` 当父件、却没 `addWidget` 进那一列，于是标签全在、`--cfgcheck` 一片绿，**界面上却一行都没有**（他报的就是这个）；反向验过：把那行 `addWidget` 删掉，新断言就点名「应用搜索的额外目录」，而旧的三条照样全绿 —— 数标签这条路天生看不见这类问题；`--pintool <图片>` 无界面，跑置顶窗那两个收起开关的 19 条几何断言（沙箱 `APPDATA`）：初始有往期带和工具栏 → 「历史」按钮切往期带的宽 → 隐藏工具栏时窗口抱住图（不保留那条 42px 空白）→ 放回来时**长回原来那么大** → 两个开关叠用窗口正好等于图片本身 → **叠用过之后再依次放开必须回到最初尺寸**（这条抓到过"来回点几下窗口越走越歪"的 bug）→ 图区/缩略图命中坐标跟着新几何走；`--capflash [次]` 同理会真把截图浮层开一次（每下不到半秒）再走和 Esc 一样的取消路径，量的是"浮层在不在、有没有铺满整屏、是不是压在所有窗口最上"——**不判"有没有抢到前台"**，因为脚本进程拿不到前台是 Windows 的规则（前台锁看最后一次真实输入），真实按 F1 的这一支由产品日志 `capture: shown ... 前台=/在最上=/试了N次` 观测。**自测旗标的派发位置本身也是个坑**：凡在事件循环里跑的（`--capgeo`/`--capflash`）必须绕开单实例守卫，否则第二个进程会把命令行转交给已常驻的实例然后当场 `return 0` —— 形状是「0 字节输出 + 退出码 0」，看着全绿其实一行没跑（`--smoke` 就以这个形状哑了很久，2026-10-14 才发现并提到守卫之前；判据：跑任何自测旗标都得拿到输出，拿到空输出就先怀疑派发点，不能当通过）。

## 插件的三份产物（旧表）+ 插件机制、动作型路由与三次搬家记

| 目录 | 是什么 |
| --- | --- |
| `ltool.exe`（264 KB） | 只剩热键、托盘、单实例、命令行自测入口 |
| `ltoolcore.dll`（1.8 MB） | 主程序和插件**共用的一份实现**：`Utils` / `DevEnv` / `SilentInstallLog` / `AdBlocker` / `RegClean` / `InfoRow` / `FileTools` / `SysTools` / `DiskScan` / `StartupAudit` / `CaptureHistory` + 启动器/设置窗那一整套界面 |
| `ltool-plugins\ltool-<id>.dll` | 一个工具一个：页面本体 + 一层 `LToolPlugin` 壳 + 嵌在 dll 里的 `plugin.json` |

`src/ltool-plugin.h` 是两边唯一的契约：`apiVersion / id / name / desc / keywords / createPage / feedText / actions / invoke / save / load`。

**API 3（2026-10-15）加的是「动作型插件」**：截图、录屏、解压/压缩、单文件占用分析这些东西**没有卡片页可开** ——
用户按的是热键或资源管理器右键。拍板的切法是「**入口留在主程序，按下之后转发**」：
`ShellMenu` 照旧往 HKCU 写那几条 verb（它决定"有哪些入口"），点进来起的一次性进程把旗标去掉两个短横当动作名，
交给 `PluginHost::invoke(action, args)`；主程序**不再链接**那些窗口本体。
**「该不该在」= 用户那个勾（落盘 `shell/intent`）× 提供者在不在了（`plugins/shell` 装着且没禁用）**,
四个变更点都调 `syncWithPlugin()`（启动 / 装 / 卸 / 禁用启用），一条通道不分叉。
**这里踩过一个单向闩锁（2026-10-16 他报"装了右键动作，文件夹右键里没有 ltool"）**:
缺键时 `intent()` 的默认值读的是**注册表现状**,而现状正是这条规则自己会改的东西 ——
卸载插件 → `remove()` 删掉注册表 → 装回来时 `syncWithPlugin()` 读不到那个键、回落到"注册表里有没有"
→ 刚被自己删掉 = false → 判定"不写"，于是**一旦撤掉就再也装不回来**。根因一句话:**默认值不能读自己刚写过的东西**。
改法:算"该不该写"之前先**认领意图**（纯函数 `shouldAdoptIntent(键存在过?, 插件在?) = !键存在 && 插件在`）——
开关从没被人为动过且插件在，就把"装了这把插件就是为了右键"落成显式 `intent=true` 并落盘;
键存在（人明确勾掉过）就永远尊重那个 false，绝不替他打开。`--plugintest` 207 → 212 钉这张真值表
（反向验过:把认领整个关掉，红的正好是那两条）。
**写完/删完注册表立刻广播一次 `SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST | SHCNF_FLUSH, …)`**（和 `path add`
广播 `WM_SETTINGCHANGE` 是同一套路，只不过冲的是外壳的关联缓存）：`SHCNF_FLUSH` 逼它同步发出去，否则消息还压在
队列里界面就报"已通知资源管理器重读"，那句话成了假的。这样"撤掉右键菜单"就不用再重开窗口、注销、更不用动 explorer.exe。
**这句的效果在这台机器上还没实测**（要拿证据只有两条路，都不该我擅自动：去点他桌面上的资源管理器右键，
或者真把 ltool 的右键集成关掉再打开 —— 那是他正在用的设置，而 `HKCU\Software\Classes` 没法沙箱），
所以文案按标准机制的分寸说，不说满：新开的窗口一定跟着变；那种"改之前就已经开着"的老窗口每次弹菜单才现搭，
多半也当场就变，不变就按一下 F5。`install()`/`remove()` 返回给人看的那句就是这个口径（"一般马上就没；
个别早就开着的窗口要按一下 F5"）。**没有**用 `taskkill explorer` 这种重手段 —— 那会把他开着的窗口全冲掉，
为一张右键菜单不值当，这也是文案留 F5 那一档兜底的原因。
`actions()` 报这个插件接哪些动作，plugin.json 里那份 `Actions` 只是"门口挂的牌"（不装载就能查，商店和日志用它）。

- 路由最终以**实例自己报的** `actions()` 为准：牌上写了、代码没实现 → 记一行「元数据说接 X，实例自己不认」再找下一家。
  `--plugintest` 把「主程序那张 verb 表 / 插件的分发表 / plugin.json 那块牌」三处对一遍，
  所以「右键点下去什么都不发生」这条最难查的形状有判据。
- **实现只有一张表**：`plugins/shell/shellaction.cpp` 里那个 `kVerbs[]` 既是分发也是 `implemented()`，
  `actions()` 直接拿它，不再第二处抄一份。
- **返回值只能有一个"失败"**: `ShellAction::kNotHandled`（这条动作我根本没实现）。
  压缩包打不开、用户按了取消、窗口正常关闭 —— 那些**已经由弹窗/托盘/日志跟人说清了**，
  主程序再盖一层「这个右键动作做不了」就是第二次报错。这条是我自己踩出来的：
  第一版把 `invoke` 判成 `run(...) == 0`，而那条链的返回值本来就是窗口结果和错误码混在一起，
  于是一次**成功的压缩**（包都生成了）被判成失败，还弹了个警告框到他屏幕上（2026-10-15 实测）。
  修完之后同一条命令 `exit=0`、包正常生成、不弹任何东西。
- **F2 录屏已经整条搬进 `plugins/record`**（2026-10-15）：`Recorder`(758) + `RecordUi`(742，含选窗那一张 `WindowPicker` 和悬浮条) + `WindowPreview`(251，选择窗右边那块实时预览) + `RecProbe`(361) 都不再是 `ltoolcore.dll` 里的东西，`WindowPicker` 上那个 `LTOOL_CORE_API` 也跟着去掉了（它当初只是因为主程序的 `--recordui` 要 `qobject_cast` 它）。热键照旧由主程序注册（`RegisterHotKey(VK_F2)`），按下之后 `PluginHost::invoke("record")`；没人接的时候**只写日志 + 托盘**，不弹模态框（弹框那一次是上一轮的错误，已经改掉了）。跟着实现搬走的还有那五面旗标的判据本体：`--recprobe [ms]` `--recq` `--rectest [秒] [宽x高]` `--recordui [秒]` `--memloop [轮数] [宽x高]` —— 主程序只解析命令行、点名要导出符号 （`ltoolRecprobeRun` / `ltoolRecqSelftest` / `ltoolRectestRun` / `ltoolRecorduiRun` / `ltoolMemloopRun`），**符号找不到就算失败**（`--plugintest` 里有一条专门验 `ltoolRecqSelftest` 解析得到）。「宽x高」那条实测还在：同一秒数下 320x200 录出 7840 字节、500x301 → 9267、900x600 → 12178。画幅/奇偶那几条算法判据本来就在 `--recq` 里，一条没少。
- 没人接住动作时主程序**必须说实话**：打印 `FAIL 右键动作 X：…` + 写日志 + 弹一句「做不了：没装接这个动作的插件」+ 退出码 1，
  绝不静默。
- **F1 截图已经整条搬进 `plugins/capture`**（2026-10-15，第三个动作型插件）：`CaptureOverlay`(854，全屏浮层：框选 / 悬停吸附 / 标注 / 撤销)
  + `PinWindow`(1280，置顶窗：往期带、图上划词取字、标注对象还能改) + `Anno`(233，上面两家共用的那一套工具图标与形状几何)
  + OCR 那一族四个文件（`Ocr` 119 / `OcrPp` 740 / `OcrBundle` 305 / `OcrPanel` 140）都不再是 `ltoolcore.dll` 里的东西。
  **OCR 必须和截图一起走**：`OcrBundle.cpp` 要拿 `Archive::extract` 解引擎包，只搬截图就会多出一条 core→插件的反向依赖。
  热键照旧由主程序注册（`RegisterHotKey(VK_F1)`），按下之后 `PluginHost::invoke("capture")`，和 F2 共用同一句"没人接"的写法
  （日志 + 托盘气泡 + stderr，**一次都不弹模态框**）。
  跟着搬走的还有那十二面旗标的判据本体（`plugins/capture/captureselftest.cpp`，739 行，一个字没改）：
  `--snaprect` `--ocrtest <图>` `--ocrlayout <图>` `--pinpick <图>` `--ocrchain <图>` `--pintool` `--pindel <图>` `--ocrget`
  `--ocrpanel <图>` `--pinshot <图>` `--capgeo` `--capflash [次]` —— 符号名 `ltoolSnaprectRun` … `ltoolCapflashRun`，
  `--plugintest` 把这十二个符号逐个 resolve 一遍（点不到就算红，"插件没加载起来"伪装不成绿）。
  搬的时候踩到两处形状问题：① `--ocrpanel` / `--pinshot` 原来靠主程序的 `app` 局部跑嵌套事件循环，
  在插件里没有那个局部，改成 `qApp` + `QCoreApplication::exec()`（lambda 不能捕获 `qApp`，它是宏，得捕获 nothing）；
  ② `--capgeo` / `--capflash` 的失败数原来直接当退出码，所以 `runPluginSelftest` 加了一个"把原始失败数带回去"的出参，
  这两条仍然留在**单实例守卫之后**（浮层要在事件循环里真开出来才算数）。
- **截图族留在 core 的两样，不是忘了搬**：`src/CaptureHistory.{h,cpp}`（往期列表，配置页改存放目录要 `reload()`
  还要显示"共 N 张"）和 `CaptureState::isActive()`（搜索框靠它决定"浮层举着的时候先别收起"）。
  两个都是 core→插件的反向依赖，只能留在共享的那一份里；`CaptureOverlay::isActive()` 现在就是读这个状态位，
  不存在第二份真相。实测：`--pathcfg`（往期和存放目录那 8 条）0 失败，`--capflash 1` 一次开合 1/1、取消干净。
要点：

- **画卡片只读元数据**（`QPluginLoader::metaData()`，`plugin.json` 的内容嵌在 `MetaData` 那一层下面），
  点开才 `LoadLibrary` + `createPage`；页建过一次就缓存复用。
- **只装不卸**：Qt 插件 unload 之后里面活着的 `QWidget`/定时器就是悬空指针。要禁用某个工具就把 dll 改名或删掉，下次启动生效。
- 版本闸两层：IID 不符 `QPluginLoader` 自己拒；`Api` 不等主程序拒，并在卡片墙那行**说人话**
  （"这个插件是给 API 1 编的，ltool 现在是 API 2"），不让人对着"加载失败"猜。
- 元数据不齐（缺 `Id`/`Name`）或者塞进来一个 Qt 自己的图片格式插件，同样点名说清楚，
  而不是默默少一张卡。目录整个空了 = 这个安装是坏的，界面上也会这么写。
- 搜索框里工具那一节按名字/关键词/拼音匹配，直达那一路现在两条规矩：**命中只认开头**
  （否则搜 `renwu` 想要任务管理器会被「开机启动分析」的关键词"计划任务"顶掉）、
  **只认每个字的第一条读音**（见下一条）。松散命中（查询串里"正好含有"工具名/关键词）并进**同一节**、
  排在强命中后面 —— 不再另起一个"工具"标题。
- **2026-10-16：直达"独占整页"这个特权整个取消了**（原来命中就 `rebuildList(); return;`，
  后面「网站 / 网址直开 / 应用 / 文件 / 系统 / 剪贴板历史」五节整个没跑到）。
  现在工具那一节照样摆最前面（**第一行还是它，回车还是它**），只是不再铲平别人。
  为什么非改不可：拿他真实的 `apps.json`（267 条 / 258 个不同名字）复算旧行为，
  **48 个查询串会独占整页、44 个应用永远搜不出来**，按"这个独占该不该给"分四类：
  只是前缀 24 条（最刺眼的是 `windows` —— 「Windows 用户名」那把工具的名字本身就是 windows 开头，
  于是所有 Windows* 应用没了；还有 `wa`→War3、`go`→Google Chrome、`ding`→DingTalk、`ru`→RustRover、
  `lt`→ltool 自己）、"关键词+空格参数"15 条（`registry edit` 的人有一半是想要 regedit 那个应用）、
  字面撞名 5 条（`ad`/`disk`/`node`/`reg`/`registry`）、两字母整串首字母 4 条（`cl`→CLion、`lx`→LX Music、
  `xf`→Xftp 8、`lt`→ltool）。**注意别把 `python`/`rust` 算进这条**：他 `webs.json` 里显式配了这些网站关键字，
  那几条走的是 `webKeywordWins`（网站赢、工具整节不出），到不了直达这一站。
- 第二条（只认第一条读音）是同一天前因「搜 `An` 搜不到 Antigravity」加上的：`广` 在表里是 `guang,yan,an`，
  于是 `an` 被当成「广告」的全拼开头 → 直达 → 应用那一节整个没了（探针打出来就是 `小节=[工具] 应用行=0`）。
  代价扫过一遍：18 个自带工具的名字 + 关键词在主读音下全部照旧直达
  （`ggdc` / `qingli` / `paiban` / `gongge` / `cipanqingli` / `cpql` 一条没伤），丢的只有"次读音"那种
  （`chongqing` 不算「重庆」的直达，表里第一条是 `zhong`），而应用那一档（`Pinyin::match`）两个读音照收。
- **`webKeywordWins`（显式配在网站上的关键字 > 工具）留着不动**（2026-10-15 他报的
  `gg` 搜不到 Google 那次立的规矩）：那是"这一节整节让路"，和"让路之后还不清场"是两件事，
  各管各的 —— `gg` 看到的还是干净的 Google 一行，不掺广告弹窗。
- 这条 `gg` 规矩的来历（2026-10-15 他报的「搜 `gg` 搜不到 Google」）：那时候「广告弹窗」的首字母 `gg`
  先命中直达、整个函数提前 `return`，后面「网站关键字 / 网址直开」根本没机会跑。当时治法是
  `webKeywordWins`（工具那一节让路，不出现）；2026-10-16 把"提前 return 清场"整个取消之后，
  让路这一条**照旧保留** —— 它管的是"这一节出不出现"，不是"清不清场"。
  结果：`gg` 出 Google、`gg ltool` 多出「在 Google 搜索」那一行，而广告弹窗还有 `ggdc`（整串首字母）
  和 `广告`（关键词松散命中）两条路 —— 三条都钉在 `--pinyin` 里，
  并且**先检查 `webs.json` 里配没配 `gg`，没配就点名跳过**（CI 那台是干净配置，判红就是环境依赖的假故障）。
- `feedText`（API 2）：`json 这串…` / `b64 这串…` 把一段文本递给那个工具的页。

加一个工具（或把现有的拆出去自己维护）只要三样，都在 `plugins/<id>/` 下：

1. `plugin.json`：`{"Id":"...","Name":"...","Desc":"...","Api":2,"Keywords":[...],"Color":"r,g,b"}`
2. 页面本体：一个 `QWidget` 子类（怎么排版、读什么数据都随意，用 `ltoolcore` 的现成件就行）
3. 壳：`QObject` + `LToolPlugin` 两个基类，`Q_PLUGIN_METADATA(IID LTOOL_PLUGIN_IID FILE "plugin.json")`
   + `Q_INTERFACES(LToolPlugin)`，照 `plugins/network/networkplugin.cpp` 抄一份改五个函数

然后在 `CMakeLists.txt` 里 `ltool_add_tool(network NetworkPlugin plugins/network/NetworkPage.cpp plugins/network/networkplugin.cpp)`
加一行（它负责把产物拷进 `build\Release\ltool-plugins\`，安装包按 `build\Release\*` 递归收走）。
`build/verify/gen_plugins.py` 是那九个页面型壳的生成器 —— 名字/描述/关键词/颜色只有一个来源，
`--plugintest` 会把 `plugin.json` 那份和壳里那五个 `override` 对一遍，漂移当场变红。

编译要求（不是建议，是"凑不齐就崩在两边的堆上"）：**同一个 MSVC 工具集、同一份按 /MD 运行的
Qt6\*.dll、同为 Release**；跨边界只递 Qt 类型，不递 `std::string`/`std::vector`。
自测入口 `ltool.exe --plugintest`（221 条，全程离屏、一次都不 `show()`）：十九个插件逐个验
"元数据通过 → 真加载 → 建页 → 没显示 → 缓存命中 → 两边元数据一致"，外加递文本、
卡片墙张数与几何（`WA_DontShowOnScreen` 走完整布局但不往屏幕上贴像素）。
判据反证做过：把 `ltool-disk.dll` 藏掉 → 4 条红（含"九个工具都在卡片墙上 got=[8]"）；
把 `ltoolcore.dll` 藏掉 → `ltool.exe` 连进程都起不来（实测 error while loading shared libraries），
所以交付闭环按 SHA256 比 exe 和 core 那两颗，装错装漏都硬停（插件目录只报数）。

## 数据目录那一页表格的原文说明

| 文件/目录 | 说明 |
| --- | --- |
| `config.ini` | 配置（搜索框齿轮修改，也可手改） |
| `apps.json` | 应用索引 |
| `usage.json` | 使用频率 |
| `icons\` | 应用图标缓存 |
| `plugins\` | 开发环境插件 |
| `webs.json` | 网站关键字 |
| `deepindex.dat` | 深索引全量快照（全盘索引，数百 MB） |
| `usnstate.dat` | USN 增量游标（几十字节） |
| `backups\` | 注册表删除前的 .reg 备份 |
| `captures\`（**默认在软件目录下**，不在 C 盘） | 截图产物：`cap-*.png` 是置顶窗的往期，`capture-*.png` 是手动保存的那张，`ocr-*.txt` 是提取文字存下来的。**存放位置可在配置页改** |
| `recordings\`（**默认在软件目录下**，不在 C 盘） | F2 录屏的 MP4（`rec-时间戳.mp4`）。**存放位置可在配置页改** |
| `clipimg\` | 剪贴板历史里的图片（开了剪贴板历史才有） |
| `ocr\` | 提取文字用的 PaddleOCR 模型 + `onnxruntime.dll`（首次用到时自动下载，之后离线） |
| `log.txt` | 运行日志（热键注册结果、扫描/增量统计） |

## 配置与数据目录（原文：config.ini 的完整注释）

`config.ini` 可配置项：

```ini
[paths]
captures=             ; 截图（含往期）存放到哪；留空 = 软件目录下的 captures（exe 旁边）
recordings=           ; 录屏产物放到哪；留空 = 软件目录下的 recordings（exe 旁边）
                      ; 配置页「数据」分组里可以直接浏览选择，保存时先验"能建、能写"，
                      ; 不行就回退到原值并说明；改完立刻生效（往期列表当场从新目录重读），
                      ; 但只影响以后存的文件 —— 已有文件一律不搬

[web]
engine=baidu          ; 默认搜索引擎：baidu / google / bing / duckduckgo

[index]
deep=true             ; 关闭后只搜常用目录
maxEntries=5000000    ; 配置页那行能改，但 FileIndexer 里的上限是写死的（20 万 / 500 万），
                      ; 也就是说这一行现在**没人读** —— 要么以后接上，要么连控件一起删掉
refreshHours=6        ; 深索引自动刷新间隔（0=不自动）；管理员增量模式无感知
; 原来还有一行 roots=（自定义扫描根目录）：配置页那行从来只有它自己在读写、FileIndexer 根本不读它，
; 深索引一直是「所有固定盘」。已经连控件一起删掉；要「指定文件夹 + 层数」请用下面的 [apps]。

[apps]
; 配置页「应用搜索的额外项目」：一行一个目录（往里找 exe，先过一道「像不像主程序」，
; 命令行工具和子进程不收）或者一行一个 exe 的完整路径（点名要的，无条件收）。
; 这条是真在用的（AppScanner::scan 读它），默认留空 = 只扫桌面/开始菜单那几处。
roots=D:\tools;D:\software\便携软件
depth=3               ; 往里找几层（0=只看目录本身，最多 6）
```

## 已知限制原文（一条一段的那种写法）

- **「压缩为 .7z」依赖 exe 旁边那个 `7zip.dll`（1.8MB，7-Zip 官方的格式库）**。它是运行期按路径加载的（objdump 查过：不在 ltool.exe 的导入表里），所以漏打包**不会**让程序起不来 —— 解压、zip、热键那些照跑，只有这一条给出"7zip.dll 不在软件目录里"并写进行为日志。`build\verify\deliver.ps1` 装完会点名确认它在不在，别只看 exe 的哈希。
- **深索引增量（USN）需要管理员权限运行**；普通权限下自动回退为"快照 + 定时目录遍历重建"，功能不缺失只是新鲜度略降。
- 本机 Windows 版本（26200）的 `FSCTL_ENUM_USN_DATA` 必须把 `MFT_ENUM_DATA` 的 `HighUsn` 设为当前日志最大 USN 才能正常枚举（已适配）。
- `reg clean` 里 HKLM 的项需要管理员权限，无权限时该项会失败并提示（HKCU 项不受影响）。
- `Alt+Space` 被其他程序占用时自动降级到备选热键（日志与托盘提示）。
- 截图"提取文字"的引擎是**自带的 PaddleOCR PP-OCRv4**（det 4.5MB + rec 10.4MB + `onnxruntime.dll` 17.6MB）。这三样**不打进安装包**：第一次用到时从 PyPI 现下（两个固定版本的 wheel，SHA256 钉死，解包只抽这三个成员），落在 `%APPDATA%\ltool\ocr`，之后完全离线；下不到就明说"没装上"。以前用的系统自带 `Windows.Media.Ocr` 已经**完全移除** —— 它取决于对方在 设置→应用→可选功能 里装没装「中文(简体) 光学字符识别」，很多机器上没有。运行时用 `LoadLibrary` + ONNX Runtime 的 C API（`third_party/onnxruntime/*.h` 只要头，不链接），det 找行 → 每行 crop 后 rec 认字 → CTC 贪心解码，字典从 `rec.onnx` 的元数据 `character` 里读（6625 类：0 是 blank，尾巴一个是空格）。**只做正放的文字**：det 的连通域取轴对齐外接矩形，不做 minAreaRect/透视矫正（对外的 `Ocr::Line` 本来就是 QRect）。实测：640×180 三行字 0.5s；2560×1440 整屏 2.1s、219 行；一个字一个框（划词因此不用等宽切）。手工补下：`ltool.exe --ocrget`。

## 功能细节与实测（每个功能为什么这么做、实测多少、拿哪面旗标验）

## 功能细节与实测

上面那张表只回答"有什么功能"。这一节按功能记着**为什么这么做、实测是多少、拿哪面旗标验**，
是原来那张功能表里的字，整段搬下来的，一条没删。

### 应用快速启动

扫描桌面（含一层子目录）、公共桌面、用户/公共开始菜单里的 `.lnk`，解析目标与图标，同目标自动去重。配置页「应用搜索的额外目录」（在**配置那一栏往下滚，「定时扫描」和「剪贴板历史」中间**那一块）还能自己加，**两种条目**：一行一个**目录**（往里若干层找 exe，层数 0-6 可配，默认 3 层，`apps/roots` / `apps/depth`）或一行一个 **exe 的完整路径**（点名要它）—— 界面上是「添加目录…」/「添加应用…」/「移除选中」三颗按钮，那一栏的标题也从「应用搜索的额外目录」改成了**「应用搜索的额外项目」**（现在装的那版就是新标题）；扫出来的和快捷方式走同一套去重，卸载器一律不算应用。**填目录时先过一道「像不像主程序」**：这台机器实测把 `D:/software` 配进去（往里两层）一下捞回 596 个 exe，全是 `QQPCTray / BugReport / pg_ctl / 7z / cmake / git-bash / fsnotifier / restarter / ShellFolder64` 这种子进程和命令行工具，把搜索框彻底淹了，所以两道判据命中任一条就不算应用：① **PE 头是控制台子系统**（Subsystem=3，命令行工具一律排除；头读不出来按「不是命令行」处理，不能让解析失败变成什么都不收）；② **名字或路径像助手**（updater/crash/helper/elevator/service/agent/patch/cli/bash…，路径里有版本号目录如 `18.1.30302.212`，或是 `locales`/`swiftshader`/`plugins`/`redist` 这类运行时目录）。**误杀的出路就是点名加**：那一栏里直接写那个 exe 的完整路径，无条件收。**第三条规则是「同目录已有主程序就不再多收一个」**：桌面/开始菜单的快捷方式指向哪个 exe，那个目录就算被占（点名加进来的 exe 同理），同目录其余 exe 不再自动收 —— 这条杀的是 `QQEX.exe`（QQ 的协议启动器，双击没反应，却和 QQ.exe 同目录，上一版它还排在 QQ 前面；他截图指的就是这个）。本机实测两道 + 这一道之后：`应用目录 2 个（往里 2 层）收了 49 个应用，跳过 398 个不像主程序的、149 个同目录已有主程序的`，索引从 753 → 295。加/改目录要按保存才会重扫（扫盘是重活，不随输入跑），无界面断言在 `ltool.exe --appdirs`

### 全局呼出

`Alt+Space`（被占用时自动降级 Win+Space → Ctrl+Alt+Space）在屏幕中上方弹出搜索框，再按一次隐藏

### 搜索

输入即搜应用（前缀 > 词首 > **拼音全拼** > **拼音首字母** > 包含，按使用频率排序；**使用次数的键**：有快捷方式记 `lnkPath`、没有的记 `target` —— 以前直接用 `lnkPath`，而目录扫来的那些没有 lnk，**全共用同一个空键**（这台机器涨到 43 次），于是 QQEX 靠别人家的 43 次排在 QQ 前面；那个空键读的时候直接丢掉，它没法归属到具体应用）；**输入先归一：全角折半角**（`ＱＱ` = `qq`，偏移 `0xFEE0`，全角空格 U+3000 也一起收）—— 中文输入法切到全角时打出来的根本不是 ASCII，索引里明明有 QQ 却一行都搜不出来（2026-10-12 他报的）；同一条链上还验 `renwu` 那种直达抢页的情况，判据见下面 `--pinyin`；**`renwu` 出"任务/人物"，`rw` 也出**（多音字每个读音都算：`chongqing` 和 `zhongqing` 都能搜到"重庆"），工具直达同样认拼音（`cipanqingli` / `cpql` → 磁盘清理），**但直达那一档必须从开头命中**（`Pinyin::matchStart`）—— 中段命中只是“名字里正好含这几个字母”，拿它独占整页结果会把人要的那个顶掉：实测踩过搜 `renwu` 想要任务管理器，结果被「开机启动分析」的关键词“计划任务”（jihua+renwu）当成直达，而直达是 `return`，应用那一节整个没了。应用那一档仍然允许中段（打 `guanli` 要能出任务管理器）；**同一档还只认每个字的第一条读音**（2026-10-16 他报的 `An` 搜不到 Antigravity：`广` 在表里是 `guang,yan,an`，`an` 于是被当成「广告」的全拼开头，又是直达 `return` 把应用那一节顶掉 —— 生僻读音没有独占整页的权力；代价只有"次读音"那种，`chongqing` 不再直达「重庆」，而应用那一档两个读音照收）；表由 `tools/genpinyin.py` 从 pypinyin 的字典生成（2.6 万字，140KB，编进 exe）；文件索引不做拼音（270 万条现算会把 20ms 拖成秒级，要做得预建索引）；同时搜文件（见下）；网址/网站关键字直达浏览器

### 文件搜索

**快索引**（桌面/文档/下载/图片/音乐/视频，实时监听秒级更新）+ **深索引**（全盘快照 + USN 增量，管理员权限下增量仅需约 0.5 秒）

### 网站直达

`ld`/`linuxdo` → linux.do，`gh` → GitHub，`gg` → Google…；输入网址直接打开；关键字后带参数可站内搜索（`gh qt6`）；无匹配时提供搜索引擎兜底

### 开发环境初始化

`init` 列出插件，`init java` / `init c++` / `init node` / `init python` / `init rust` 检测组件，缺失的按 Enter 用 winget 安装

### 管理 PATH

`path` / `path list` 查看用户 PATH；`path add <目录>` 添加；`path del <目录>` 移除；写入 `HKCU\Environment\Path` 并广播 `WM_SETTINGCHANGE`

### 注册表整理

`reg` 体检：无效卸载残留 / 失效开机启动项 / MuiCache 残留；Enter 删除单项，`reg clean` 全部清理，删除前自动备份 `.reg`

### 配置页面

- **菜单四行**（2026-10-15 收的）：网站 / 插件商店 / 配置 / 关于。`--settingspage <行> [tab] [插件id]` 的行号也跟着换：0 网站、1 插件商店、2 配置、3 关于。
  `openAt` 现在是**先展开卡、再挑 tab** —— 软件那两张页搬进插件之后，那个 `QTabWidget` 在卡片里面，反过来写就永远挑不到。
- **搬走的三张页**：`plugins/software`（推荐软件 + 软件列表两张 tab）、`plugins/downloads`（下载器）、`plugins/tasks`（定时任务）。
  实现里留在 core 的两件是刻意的：`TaskScheduler`（主程序一起来就每 30 秒看一次的时钟）和 `DownloadManager`（「关于」页下载安装包也走它，两处共用同一份任务表）——
  把卡片禁用掉不会让"到点真的执行"跟着消失。
- **`--uptime` 那两条耗时现在走插件符号**（`ltoolSoftwarePage` / `ltoolUninstallPage`，主程序按名字点名要）。
  拿不到符号会打一行"没有导出 …"而不是静默变成 0 ms —— 装出来的 dll 太旧时这条就是证据。
- **`TaskScheduler` / `DownloadManager` 两个类贴了 `LTOOL_CORE_API`**：搬页的时候链接器报的是 `staticMetaObject` 找不到，
  而 `WINDOWS_EXPORT_ALL_SYMBOLS` 只导函数不导数据 —— QObject 子类的 metaobject 是数据，跨模块 `new` 它就得显式导出。
- **商店只有一面列表**（2026-10-15：原本"卡片墙 + 下面一栏清单"是两页并排，他要求合成一面）。
  `ToolsPage::shopRows()` 是这一面唯一的数据入口：没拉清单 → `PluginStore::localRows()`（只报目录里有什么，不猜仓库），
  拉过清单 → `rows(entries)` 后面接 `localOnly(entries)`。一张卡可以有**三颗**按钮（「更新」+「禁用」+「卸载」——
  装了的东西不能因为版本旧就禁不掉、卸不掉），
  它们排在**卡片最底下一行**（2026-10-15：以前和状态词挤同一行，状态那句长一点就把按钮挤得看不见）：
  `ShopCard` 的正文列是 名字 → 说明 → 状态 → stretch → 按钮排，`addButton()` 只管往那颗前置 stretch 后面追加。
  大小一律写成 **MB**（`PluginStore::humanSize()`，两位小数）：卡片上任何一行都不许留"N B"那种裸字节，
  包括下载中途按钮上那行字（总大小不知道时报的就是已下到的 MB）。
  `localOnly` 现在服务的就是 software / downloads / tasks 这三张（`dist/` 还没收它们，#78 做完就没了）。
- **`QFile::rename` 在 Windows 上不覆盖已存在的目标**（2026-10-15 他一点「更新」就露馅）：Qt 那边直接返回失败，
  于是 `finishDownload` 撞在 `renameStaged` 上，报的还是那句"被别的程序挡住（多半是杀毒）"—— 冤枉了杀软。
  「更新」这一条从写出来就没成功过一次（自测只走过"目录里本来没有"那条，`--storetest` 里现在补了"能盖掉已存在的那颗"）。
  正解：落盘之前先 `QFile::remove(dest)`；删不掉（这个进程正用它）就明说"重启之后再点一次更新"，并把 .part 拿掉别留垃圾。
- **「装/更新」现在排队，一次只飞一颗**（2026-10-15）：他连点九颗那次崩在 `scan()` 里 —— 事件日志写着 `ltool.exe` 挂 `Qt6Widgets.dll` / 0xc0000005，程序日志最后九行是九颗「装好」，紧接着重扫的「plugin: 看到 …」数到第 9 条就断了。九条下载 + 九次改名 + 中途 `reload()`/重排叠在一起是根，不是哪一颗的错。`ToolsPage::installEntry` 现在只放行一颗，其余进 `m_queue`（卡片上写"排在第几位"），队列空了才 `finishInstalls()`。**队列里也存 `QPointer`**：第一版存裸指针，判据点九颗那一下当场崩给我看（`rebuildGrid()` 之后 `startNextInstall()` 往没了的按钮上写字）。
- **卸载答复那段拆成了 `ToolsPage::applyUninstall`**：离屏里点不到"是" —— 无窗口平台上 `QMessageBox::question()` 会**立刻返回默认值**（第一版判据因此拿到"没动。"，红了个假的）。拆出来让测试直接调同一段，不抄第二遍。
- **判据的终止条件别拿"字节数不变"当落地**：刚点下去那几秒 `.part` 就是 0KB，三次采样相同不等于装完了（上一版就是这么把一次正常排队读成"留了个 .part 没清"）。要"不变 **且已经有字节**"。
- **离屏自测里 `processEvents()` 不结算 `DeferredDelete`**：`rebuildGrid()` 之后旧卡片其实还活着，
  "回调打到已删控件"这种形状根本量不出来（第一版 `--storemulti` 就是这么绿着放过了访问违例）。
  要 `processEvents()` 之后补一句 `QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete)`，
  旧控件真死了，判据才有牙。
- **拉插件那三级顺序只在 `PluginStore::fetch` 里写一遍**（清单和 dll 两段都用它）：镜像前缀 + 原件走**直连**，
  然后原件走配置里那条代理（留空 = 系统代理），最后原件直连；每条候选原地试两次。
  镜像只给 `raw.githubusercontent.com` 用 —— 指到自己的服务器还套镜像只会拿回 404，让他以为自己的源坏了。
  2026-10-15 实测的形状：`gh-proxy.com` 对这个文件 **403**（10-11 量到 8.3MB/s），代码重试两次后换
  `ghfast.top` 一次成功，状态行就写"走的是 镜像 ghfast.top，一共试了 3 次"。
  内置候选只有这两条：`mirror.ghproxy.com` 21s 超时已死、`raw.gitmirror.com`/`raw.kkgithub.com` 解不到 DNS、
  `cdn.jsdelivr.net` 缓存约 7 天 —— 拿它装插件会一直"哈希对不上"，所以不收。
- **卸载是真删文件**：`<file>` 和它的 `.off` 一起删；本进程已经加载过的那颗 Windows 不放行（我们从不 unload 插件），
  于是记进 `store/pendingRemove`，**下次启动第一件事**（`applyPendingRemoves`，在任何插件被加载之前）删掉并销账。
  `--storetest` 验的是"记上账 → 下次启动真删 → 销账"，**没有**假装验过"删不动"那个分支（那要一颗真加载过的 dll 才进得去）。
- **`定时任务` 那张卡的关键词是 定时/计划/cron，没有单字的「任务」**：`renwu` 是"任务管理器"的全拼，
  加了「任务」这条直达就把应用整节顶掉了（#60 就是为了这个把「计划任务」摘掉的）。`--plugintest` 里那两条 renwu 判据当场红过，改的是关键词不是判据。

语言那一页是**整个搬进**配置页那个「语言与翻译」分组框的（控件一个没抄，所以也不会有两份内存状态、两个保存路径），入口只剩「配置」这一个。

### F1 截图

全屏遮罩框选，鼠标悬停自动吸附窗口（**吸附出来的就是看到的那个边界**：拿 DWM 的 `EXTENDED_FRAME_BOUNDS`，不是 `GetWindowRect` —— 后者在本机实测比可见帧**左/右/下各多 7px、上边多 0px**（那三边是看不见的 resize 边框），旧写法还按左边的余量给上边补了 7px，等于四周各多圈一层桌面；无界面断言在 `ltool.exe --snaprect`）；标注支持矩形/圆/箭头/文字/马赛克/撤销；**提取文字**（自带的 PaddleOCR，第一次用下载一次模型、之后离线）把选区里的字认出来，自动进剪贴板并可编辑/存 txt；完成后自动复制到剪贴板并存到截图目录（**默认在软件目录下：`D:\software\ltool\captures`**，不往 C 盘的 `%APPDATA%` 堆；配置页「数据」里可改，装到写不了的只读位置时自动退回数据目录）。置顶截图窗里画的矩形/圆/箭头/打码块都是**可编辑对象**：切到「拖动/选择」点中就选中，拖内部整体移动、拖 8 个手柄改大小、Delete 删除（打码块挪走后原位置恢复成原图，因为底图和标注分开存）；按住 Alt 拖是自由涂抹，烙进图里改不了。工具栏还有一把**「划词」**：认完字后直接在图上拖拉选字（按词的包围盒切到单个字），松手就进剪贴板、窗不关，单击选整词、`Enter` 复制全部、`Esc` 退出；要改字/存 txt 用图上右键「识别全文」。左侧往期缩略图**右键 = 删除那一张**（内存和 `captures` 里的文件一起删；删的正好是当前显示的那张就自动挪到相邻一张）。**缩放不是 100% 也能截全整块屏**（他这台机器 150%）：`QPixmap` 的 `copy()` 和 `drawPixmap` 的 sourceRect 都按**设备像素**解释，而选区是逻辑坐标，旧写法直接把逻辑矩形递进去，150% 下就只截到 4/9 那块屏；现在统一过一层 `deviceRectFor`（dpr=1 时原样返回，100% 的行为一点没动），裁完再把 DPR 贴回去。无界面断言在 `ltool.exe --capgeo`（含一张自造的 1.5 倍画布，100% 的机器上照样能抓到这个错）；**置顶窗默认就是"只有一张干净的图"**：工具栏和左边那条往期带建窗时都收着（窗口正好等于图片本身），要工具栏就在图上右键「显示工具栏」，换上一张/下一张用图左右两侧的箭头（往期带收着也能用），放出来之后工具栏的「历史」按钮再切往期带。右键「隐藏工具栏」可以随时收回去 —— 收起时窗口抱住图片本身（不留左右黑边、也不留那条 42px 的空白），且图上右键只剩「显示工具栏」和「关闭」两项

### F2 录屏

按 F2 弹出选择列表（输入关键字过滤，回车开录），**不想录了有四条退路**：右上角那颗**「✕」**（看得见的那个，给眼睛用）、`Esc` 收起、点到别的窗口（一失焦就自己收起，它原本会一直压在你要录的那个窗上面）、**再按一次 F2 也收起**（手就放在热键上，这条最自然）；后三条都能从输入框和列表里收到，因为 `QLineEdit` 会自己吃掉 Esc，所以给两个子控件都装了事件过滤器。这窗是 `Qt::Tool` + 无边框，本来没有标题栏，所以「✕」是画在标题那一行的右上角（hover 变红），点它 = `hide()`，和 Esc 一条路。**第一条就是「整个屏幕 · 主显示器 · 2560x1440」**，往下是桌面上看得见的窗口，选谁录谁：录窗口用的是系统自带的 `Windows.Graphics.Capture` 按窗口取帧，别的程序盖在它上面也不会入镜；被虚拟桌面收起 / 没标题 / 小于 80x60 的窗口不列。**录整块屏幕走 `CreateForMonitor`，并且把鼠标指针也画进画面**（`IsCursorCaptureEnabled`；录窗口那一路仍然不含指针，保持合成帧确定性）——注意那条小悬浮条本身在屏幕里，录全屏时它会出现在画面右下角。右下角一条小悬浮条显示 ⏺ 计时 + 帧数，点「停止」收尾，产物是普通 MP4（H.264，Media Foundation 系统编码器，不拉任何外部 DLL），存到录像目录（**默认在软件目录下：`D:\software\ltool\recordings\rec-时间戳.mp4`**，不在 C 盘；配置页「数据」里可改），条上变成"已保存 N 帧 / X KB → 文件名" + 「打开所在目录」。**旁边还有一个「不要了」**：停下这一局并且把那个半成品 MP4 就地删掉（Recorder 没有"只停不写盘"这条路，所以照常收尾再原地删），条上写"已取消，没保存（丢掉 N 帧）"，8 秒后自己消失 —— 录到一半的东西留在录像目录里就是垃圾。**当前只录画面，不录声音**（要加的是 WASAPI 环路采集 + 音画对齐，没做就不放开关）。参数在 `config.ini`：`ui/recFps`（默认 15；画面静止时 WGC 不给帧，不凑帧）、**`ui/recQuality`（画质档，默认 1=标准）**、**`ui/recMaxWidth`（编码画幅长边上限，默认 0=原始）**。 **2026-10-14 起，选择窗列表下面多了两颗下拉框**（改了就记住，下次按 F2 还是那两档，不用每回重选）： 「画质」= 流畅 / 标准 / 高清 / 超清，本质是**每像素每帧给多少 bit**（0.060 / 0.145 / 0.250 / 0.400）， 码率按**编码后的画幅**算：`标准` 在 2560x1440@15 上正好是以前那个固定 **8000kbps**（等效值反推来的，不是随手取的）， 所以全屏录制行为没变；小窗口以前不管多大都按 8Mbps 写，现在按面积给，**小窗录出来的文件会明显比以前小**。 「画面」= 原始 / 长边≤1920 / ≤1280 / ≤854，等比缩小**用面积平均，不是隔行取样**（隔行取样一缩放文字就闪、就出摩尔纹）。 `--recq` 实测：同一张 640x400 的卡，不缩那局文件画幅 640x400 / 13337 字节，选 ≤320 那局文件画幅真是 **320x200**、5853 字节， 并且解回亮度网格仍然"上亮下暗"（缩放没把画面翻过来）；噪声画面两局 271534 vs 371749 字节。 **一个实测事实值得记下**：这台机器的 MF 编码器把 `MF_MT_AVG_BITRATE` 当**参考**而不是硬上限 （低档目标 400kbps，实测 2.5 秒写出约 900kbps），所以自测的判据写成"档位调高之后同内容文件明显不变小"， 而不是"文件大一倍"—— 我第一版按后者写，当场红，量清楚才发现是我的期望不对。 中途改窗口大小 / 把目标窗口最小化或关掉，这一局会自己收尾（已录下的部分照样在文件里，条上会写清是"没录成：原因"），收尾后 F2 立刻可以再用。 H.264 只吃偶数画幅，所以奇数大小的窗口会自动往下取偶数再编

### 修复

检测 .NET Runtime / VC++ 2005~2022 / VSTO / DirectX / UCRT，未安装的一键 winget 安装

### 广告弹窗

按窗口标题/进程名扫描弹窗，可关闭窗口、结束进程、加入拦截名单；勾选自动拦截后每 60 秒自动关闭

### C盘清理

常见可清理位置（临时/更新缓存/DX缓存/pip/npm/浏览器缓存等）扫描大小、一键清理、回收站清空；C 盘顶层目录占用分析。**两个 tab 的列表都支持右键「打开文件夹」**（右键落在整行哪都行 —— 名字、下面那行完整路径、占比条都会冒到行卡片上）：「可清理位置」开的是那个缓存/临时目录本身，「占用分析 · 本层目录」开的是那一行对应的目录；左键在占用分析里是下钻，所以开文件夹只走右键。目录已经不在了（比如清理完之后的 Temp）那一项就摆成灰的、写「这个目录已经不在了」，不会点了没反应。这条只有界面在用，且真按一下就会拉起资源管理器，所以判据在离屏自测 `ltool.exe --rowmenu`（9 条：菜单就一项、文案、灰没灰、**这项的 data 就是这一行自己的目录** —— 点开之后拿的就是它，所以这条等于验"开的是它不是别的"）。**占用分析首扫在这台机器上要 14~19 秒**（48.1 万个目录、142 万个文件，全量枚举的地板，多线程也顶不住：8 线程只比 1 线程快 1.3~1.7×、而且 4/8/16 的顺序在两遍之间还会互换，见 `--diskscale`），所以改成了两件事：**边扫边出数**（一个一级分支定稿就摆一行上来，实测 1.6 秒就有 13 行，不用干等最后一秒）和**每个盘的结果留着**（切走再切回来直接吃缓存、不重扫；状态行写明上次扫描的时间，按「整理」才重算，清理完那个盘会作废该盘的缓存）。判据 `ltool.exe --diskui`（7 条）+ `--diskbranch`（4 条，专门盯"先报给用户的那个数和最后树上的数是不是同一个"）

### Windows 用户名

工具页那张卡片（2026-10-13 起它是 `ltool-user.dll` 这个插件）：先只读地把现状摆出来（账户名 / 机器名 / 显示名 / 本地还是微软账户 / 用户目录 / SID），然后三件事：**改显示名**（SAM 的 FullName，登录界面和锁屏上那个名字，最安全）、**改账户名**（`Rename-LocalUser`；界面上写明白 **C:\Users 那个目录不会跟着改名**、写死旧路径的程序不会自动跟上、要注销重登）、**新建用户**（显示名 + 密码 + 勾选「加到 Administrators 组」= 提权，不勾就是标准用户）。三个动作都是**提权跑 PowerShell**（`runas`），而且**一律不自动执行**：要人在界面上按下，界面上也写清楚后果。判据搬进了插件本身（`--username` 现在跑的是插件导出的 `ltoolPluginSelftest`，27 条：`psQuote` 把单引号翻倍 / 名字合法性 / 拼出去的脚本长什么样 / 页面上三颗按钮真的摆在布局里 / 读回来的现场含真实机器名和 SID）

### 关于（菜单第 4 行 + 托盘「关于」）

显示**版本号（唯一来源是 `ltool.iss` 的 `#define MyAppVersion`，CMake 解析后编进 `LTOOL_VERSION`）**、Qt 运行时版本、程序/数据目录、工具插件数（含没通过检查的个数）和日志路径；下面是**检查更新 → 下载新版 → 装并重启**三颗按钮，一件都不自动：检查走 `https://github.com/<仓库>/releases/latest` 那条 **302**（跟完重定向从 `/releases/tag/<tag>` 里读版本号，带 User-Agent，沿用「配置 → 下载代理」那条），失败按 404/连不上传区别说一句人话；下载交给内置下载器（进度看台在「下载器」那一页，这里不假装有进度）；「装并重启」会**先退出 ltool**，所以要点两下（弹窗默认停在“不”），它写一段临时 PowerShell：等我们这个 pid 退场 → 静默装回**同一个 /DIR** → 再把 ltool 拉起来，日志在 `%TEMP%\ltool-update.log`。服务器给的资产名一律过 `safeFileName`（只留 `[A-Za-z0-9._-]`、去掉开头的点），脚本里每个路径都走单引号翻倍，带单引号直接整段拒绝。无界面断言 `ltool.exe --about`（24 条，含版本比较 `1.10 > 1.9`、`../evil.exe` 变 `evil.exe`、逃逸口、四颗按钮在布局里、页面显示的版本 == 编译期那个），真联网探针 `ltool.exe --updatecheck` 点的是界面同一颗按钮

- **检查更新以前打的是 `api.github.com/.../releases/latest`，那是全局限流的一条**：匿名额度按**出口 IP** 算 60 次/小时，同一台机器上任何脚本（我这轮就拿它反复查 CI 作业）都能把它吃光 —— 他点「检查更新」看到 `HTTP 403` 就是这么来的。1.0.3 起改走网页那条 302：`/releases/latest` → `/releases/tag/v1.0.2`，读最终地址就行，**没有限流**；安装包直链按我们自己的命名推（`releases/download/<tag>/ltool-<版本>-setup.exe`），两条都实测过：curl 拿 302 落点、Range 拿直链回 206。代价是 api 才给的字段没了 —— 发布**日期**和资产**大小**不再预报（宁可少说一项，也不为一行字把限流那条请回来），`--storenet` 里加了一条真跑这条链的判据。

### 网络

网卡 DNS 一键切换预设（阿里/腾讯/114/Google），UAC 提权 netsh 执行，含刷新 DNS 缓存与恢复 DHCP

### 文件占用 / 粉碎

文件与文件夹右键 ltool 子菜单：**占用分析**一个窗口两件事 —— 上面是大小 / 实际占簇 / 簇大小 / 文件系统 / 硬链接（文件夹会递归求和），下面是哪个进程正开着它（Restart Manager，可结束该进程）；**粉碎**为三轮覆写后永久删除，不进回收站

### 压缩包读写

右键解压到当前目录 / 解压到同名文件夹、压缩为 .zip / .7z。读是 libarchive：zip / 7z / tar / gz / bz2 / xz / zstd / iso / cab / cpio，rar 只读且不完全支持；写两条分家 —— zip 用 libarchive 的 deflate，**.7z 用 7-Zip 自己的多线程编码器**（bit7z + 随包的 `7zip.dll`，见下）。**要快就用 zip**：同一个 32MB 的文件夹实测 —— 我们的 .zip 0.73 秒 / 27.1MB（比 7-Zip 自己压 zip 的 0.96 秒还快）。**.7z 那条已经不再走 libarchive**，改成直接请 7-Zip 自己的多线程编码器干活（`src/SevenZip.cpp` + vcpkg 的 `bit7z`/`7zip`，运行时加载随包带的 `7zip.dll`）：同一个文件夹 **10.1 秒 → 1.8 秒**（改前那 10.14 秒是他自己现场日志里的一行），产物 23.46MB 和 7-Zip 命令行逐字节同尺寸（它 `-mx5` 是 1.58 秒，差的三百毫秒是我们启动自己的开销）。以前追不上的原因量清楚了，就是线程数：libarchive 的 7z 写出器 LZMA1/LZMA2 恒为单线程，7-Zip 靠 `-mmt` 把 LZMA2 分块丢给多个核。试过又否掉的两条：把 zstd 塞进 7z（`threads` 唯一支持的编码器）能到 0.3 秒，但产出的包连本机 7-Zip 25.01 都报 `Method = 04F71101 / Unsupported Method`；自己包一层 .lzx（清单 + 标准 .xz 流，用 liblzma 的 `lzma_stream_encoder_mt`）实测 0.48 秒 / 25.7MB 最快，但**别人拿到 .lzx 根本不知道是什么文件**，所以删了。zip 里指定 zstd 也不行：7-Zip 会静默退回 Deflate。中文条目名按格式喂 `hdrcharset`（tar/cpio=UTF-8，zip/rar/lha=本机码页），中文路径走 `_w` 接口。默认后台静默完成 + 托盘提示，配置页可打开进度窗口（带取消）；**进度窗口跑完自己就关**（成功或取消都关，那句话交给托盘去举），只有失败才留着说原因，这时「打开位置」指向那个没改名的半成品；半成品写 `.part`，成功才改名。**吞吐实测（60MB 伪日志，`--archspeed`）**：解压 zip 857MB/s、解压 7z 343MB/s（都比 7-Zip 自己快，解压这条路还是 libarchive）；压缩 zip 118MB/s / 5.9MB；**压缩 7z（新后端）4.17 秒 / 3.6MB，同一个夹具 7-Zip 命令行要 6.30 秒 / 3.4MB —— 我们快 34%**。档位曲线（只认 7-Zip 定义的 0/1/3/5/7/9）：1 档 0.20 秒 / 4.7MB、3 档 0.53 秒 / 4.5MB、5 档 4.43 秒 / 3.6MB、7 档 7.79 秒 / 4.4MB、9 档 8.00 秒 / 4.4MB —— **7 档往上体积反而回涨**（多线程把输入切的块多了，每块各自从零开始建字典，上下文短了），所以默认停在 5 档：它既是 7-Zip 自己的默认档，也是这条曲线上"每秒换多少字节"最划算那一点。每一档都验过两件事：我们自己的读取器解得回来、`7z t` 退出码 0。**右键那一次性进程干完活把托盘气泡交给常驻实例去举**（走单实例那条本地套接字，消息格式 `note\t标题\t正文`）：以前它为了"让气泡显示满 5 秒"自己多活 5 秒，实测解压一个包端到端 5.4 秒而活儿本身只有 0.2 秒；改完端到端 0.36 秒

### 其他

托盘图标、单实例、使用频率统计（空查询显示常用应用）、运行日志

## 自测旗标一览（43 面，全表）

| 旗标 | 一句话 |
| --- | --- |
| `--show` | 启动即把搜索框显示出来，不是只进托盘。 |
| `--smoke` | 无界面把索引、目录、插件都点一遍。 |
| `--mfttest` | MFT 直读那一路的判据（提权机器上会真跑枚举与 $MFT 大小解析，非提权点名跳过）。 |
| `--searchtime` | `--searchtime 词1 词2`：深索引搜索的耗时 + 前 3 条结果。 |
| `--searchdump` | `--searchdump 词1 词2`：每个词前 20 条**按顺序整条**打出来，改搜索实现就拿它做 diff。 |
| `--disktime` | C 盘上 A/B 两种磁盘遍历（DiskScan vs QDirIterator）的耗时。 |
| `--diskscale` | 拿环境变量 `LTOOL_DISKTHREADS` 把同一棵树按 1/2/4/8/16 线程各扫一遍，量扩展性。 |
| `--diskbranch` | 4 条：边扫边报的那几个分支数，和最后定稿的树是不是同一回事。 |
| `--diskui` | 7 条：磁盘页整条链（点「整理」→ 途中就出行 → 定稿 → 换盘让位 → 换回来吃缓存）。慢，要真扫一整盘。 |
| `--plugintest` | 十九个插件逐个验「元数据 → 真加载 → 建页 → 没显示 → 缓存命中 → 两边一致」+ 右键菜单该不该在的四组合 + 意图认领那张真值表 + 卡片墙张数与几何 + 动作路由（现在 221 条）。 |
| `--cliptest` | 「剪贴板」那一页 离屏 56 条 + 要真剪贴板的 5 条（`QT_QPA_PLATFORM=windows` 才跑，离屏点名跳过：offscreen 的 clipboard 是内存桩，跑在那儿只会假绿）（沙箱 APPDATA，一次都不碰你真实的 `clipboard.json`）：过滤那条纯函数 13 格（空关键字=全部、大小写、trim、中文、搜整段而非首行、按日期、图片按「图片」和尺寸、命不中给空、一条命中多处不重复列），剩下盯**行号 ↔ 存储下标**（过滤后第 0 行必须映射回 items[1]，每一行显示的字必须等于它 UserRole 那条的摘要）、状态行说实话（没在记录写「没在记录」并指路开关）、双击复用 / Delete 删这条 / 清空 三条动作真落到数据上。反向验过：把 UserRole 存成行号，当场红 4 条。**两个自己踩的坑也记在这里**：① 幻觉代码 `QListWidgetKeyEater` 和两参 `installEventFilter`（编译当场抓住）；② 第一版把"状态行 setEnabled"那两条排在 Delete **前面**，一打开收集 `capture()` 就往夹具里补一条、把固定 4 条的下标冲乱，于是两条判据假红 —— 夹具型判据里**任何会改全局状态的开关都要排在依赖固定数据的判据之后**。 |
| `--username` | 27 条：「Windows 用户名」那一页，**一次都不真的改账户 / 建账户**。 |
| `--rowmenu` | 9 条：磁盘页两栏列表的右键该弹什么、点开的是不是这一行自己的目录。 |
| `--archivetest` | `--archivetest extract / make / probe / progress`：压缩包读写回归；`make` 第 5 个参数是 7z 档位 0-9。 |
| `--pinyin` | 52 条：拼音表和码点必须对齐、多音字、全拼与首字母，直达那一档只认第一条读音（`An` 被「广告」的生僻读音抢页那次加的），另有 4 条盯"直达不清场"（反向验过：把 `return` 塞回去，红的正是其中 2 条），最后真建一个不显示的窗口往输入框里打字。 |
| `--linuxdotest` | 44 条：「linux.do 盯帖」离线那半条 —— 解析 / 命中与去重 / 前缀拼法 / 配置夹紧与回读 / 要不要弹那一句 / 页面九件控件可达。一次网都不上、一个气泡都不弹（跑在沙箱 APPDATA 里）。 |
| `--linuxdonet [秒]` | 10 条：真去取一趟（默认等 60 秒），状态行说出这次走的是直连还是中转、拿到几条、第一趟只播种不弹框。**要联网，不进 CI**。实测一趟 4954ms、30 条。 |
| `--gridtest` | 64 条：「图片排版」的格数与列行、收图与拒收、拖动是挪还是换、✕ 只空那一格、合成尺寸与像素、板上预览与出图同一块构图、坏文件那一格说实话、**效果图弹框先看过才落盘**（`generate()` 里那道 `m_gate` 就是为这条留的门：判据把它换成回绝 → 断言没写文件、状态行说「没存」、「打开这张」没亮；换成同意 → 才真落盘并数到目录里多一个 `grid-*.png`。反向验过：把弹框从 `generate()` 里摘掉，那 2 条当场红成 `got=[0/0/1]`。离屏判据都不 `exec()` 模态框 —— 那里面的返回值不作数，这条在卸载那一步栽过）（离屏，判据在 \`plugins/grid/gridselftest.cpp`）。**背景色的选择挪进了效果图弹框**（顶栏那颗是盲选）：框里那颗写着当前挑的色，换一下 → `compose` 重算 → 那块缩图和那行字当场跟着换；**没按保存之前页面认的还是老色**（`commitPreview` 才把它定下来），按取消连色都不改。8 条判据盯这个，其中一条是反向断言「顶栏不再有那颗」—— 两处都留迟早对不上。**点空格子 = 往里放一张**这一组 13 条：`pickInto(那一格)` 只在「空格子 + 原位松开」时发（点有图的格子不发 —— 那是拖动起点；按下去拖出那一格再松也不发；点 ✕ 只拿掉不发选图），`putAt` 只动指定那一格、拒非图片后缀、拒越界格号、能盖掉已有那张；端到端那两条走页面：先给 `setPickGate` 装一道门（默认是 `QFileDialog`，判据换成给一个固定路径），点第 4 格 → 断言 `paths()[3]` 就是那张、状态行说「第 4 格放了」，门回空串（= 选图框按取消）→ 那一格还是空的。反向验过：把页面那条 `connect` 注释掉，红的正是端到端那 2 条。**踩过的坑**：板子层的信号判据一开始走的是页面那块板子，一点就弹真选图框 —— 离屏看不见窗口但进程卡在模态框里，形状是「0 字节输出 + 一直不返回」；改成用一块独立的 `GridBoard` 验信号，页面只留给端到端那两条。 |
| `--archspeed` | `--archspeed [每文件KB] [文件数]`：量压缩包读写吞吐，并把 7z 的 1/3/5/7/9 档整条曲线跑一遍，每档都验「我们能解回来 + `7z t` 退出码 0」。 |
| `--ocrtest` | `--ocrtest <图片>`：打印图里认出的文字和耗时。 |
| `--ocrlayout` | `--ocrlayout <图片>`：打印每行每词的包围盒和切出来的字格（划词的坐标就靠它验）。 |
| `--pinpick` | `--pinpick <图片>`：图上划词 9 条（把鼠标事件直接送进一个不显示的置顶窗，**不合成鼠标**），跑完归还剪贴板。 |
| `--recprobe` | `--recprobe [毫秒]`：录屏之前先量地基 —— 系统里有哪些编码器、MP4 写 30 帧再读回来要多久、按窗口取帧能不能用。 |
| `--recq` | 13 条：录屏那两档（画质 / 画面）真的进了编码器，缩完不许把画面翻过来。 |
| `--rectest` | `--rectest [秒] [宽x高]`：录一张自造的会动的卡 → 把 mp4 解回来数帧，末尾再录 1.5 秒整块屏幕。 |
| `--recordui` | `--recordui [秒]`：45 条，F2 那条链整条走一遍（列表 → 悬浮条计数 → 程序化按「停止」→ 解出帧 → 「不要了」→ 选择窗四条退路）。 |
| `--pindel` | `--pindel <图片>`：10 条，右键往期缩略图删那一张（`APPDATA` 挪进临时目录，不碰你真实的往期）。 |
| `--ocrpanel` | `--ocrpanel <图片>`：真开一次「提取文字」结果窗（要人关掉才算跑完）。 |
| `--ocrget` | 只把 OCR 引擎那三个文件下载好并校验 / 解包（新机器补下载用的入口）。 |
| `--ocrchain` | `--ocrchain <图片>`：11 条，从「一个文件都没下」跑到当场认得出那三行字；会真下 26MB，但落在临时沙箱里。 |
| `--memloop` | `--memloop [轮数] [宽x高]`：每轮解压一个压不动的 32MB 包 + 录一段指定尺寸的窗，逐步打印私有字节 / 工作集 / 句柄数。 |
| `--toggle` | 6 条：按 `Alt+Space` 到底出不出来（冷启动跑，会把启动器真弹出来一两秒）。 |
| `--pathcfg` | 9 条：配置页那两行「存放目录」真的管用（沙箱 `APPDATA`）。 |
| `--appdirs` | 12 条：应用目录那条过滤（PE 子系统、助手名、版本号/运行时目录、同目录已有主程序）。 |
| `--capgeo` | 7 条：缩放不是 100% 时截不全全屏那条几何链（真屏 + 一张自造的 DPR=1.5 画布，100% 的机器上照样能抓到这个错）。 |
| `--cfgcheck` | 11 条：离屏建一次配置页，数每一行在不在、那两颗按钮是不是**各只有一颗**、每个分组框能不能从布局走到；另数设置窗菜单那四行（顺序、每行点开有内容、**只有「插件商店」那颗换成了画出来的袋子**），并把四颗图标拼成一条 PNG 存到 `%TEMP%\ltool-cfgshots\menu-icons.png` 供肉眼复核。 |
| `--storetest` | 67 条（2026-10-16 数的；文档原来写 60，加"排队那句写进卡片"三条之前实测已经是 64）：插件商店离线那半条链 —— 清单解析（收两条、拒三条，拒的每条说一句）、`localRows`（目录里 `*.dll` 算已装、`*.dll.off` 算已禁用、旁边的 `*.bin` 不算）、和本地目录比（能装/已装/可更新/装不了）、`.part` 验哈希再改名（哈希或字节数不符就删掉半成品）、`<file>`↔`<file>.off` 改名禁用/恢复、索引地址留空回内置。**一次都不联网**。 |
| `--storenet` | 17 条：真拉一次 `dist/index.json` 并离屏点「刷新清单」，**逐张卡**核对「那句状态词 ↔ 那颗按钮」——没装的只给「装」、装了的只给「禁用/启用」、可更新的两颗都要，再对状态行里的卡片数 / 能装数 / 拒收数 / 清单外数；三条新的盯这次改版：卡片上不许留裸字节数、本机装着的那几张都得写出 MB、**每颗按钮都在卡片最底下一行**（离屏量几何）。**要联网，没网就是红，所以不进 CI**。 |
| `--seamtest` | 平台层那三道缝自己（`src/platform/Paths.h`、`Launch.h`、`Autostart.h`、`ProcInfo.h`、`FileMeta.h`）20 条：四个已知目录问得到且**两两不同**、Windows 上各自落点、占位符换成系统里那个真值、夹在句子里的也换、认不出的原样留着、空路径不许启动/不许提权。Linux 侧 43 条走 `wsl -e bash tools/seam_probe.sh`（拼音 10 + 缝 14 + 自启 9 条真往返：HOME 指临时目录，勾上→文件真在→关掉→文件没了）。细节与反向验见文末 **跨平台移植：平台层那道缝**。 |
| `--storemulti [颗数]` | 5 条：连着点几颗「装/更新」（他那次是九颗），然后**当场把卡片墙拆掉重排** —— 回调连的是页面（还活着）、卡片是 deleteLater 掉的，用裸控件指针就是往已删的 QWidget 上写。回退版跑出来 `rc=-1073741819`（0xC0000005 访问违例），修复版全绿。要联网、会往运行目录真写 dll，所以 `build/verify/storemulti.ps1` 先拷一份临时副本；不进 CI。 |
| `--uninst [id]` | 11 条：卸载那条路整个走到底 —— 先 `PluginHost::page()` 把那张页开出来（dll 就此映射进进程）、回商店、`applyUninstall`、重排、再切回那张页；另一颗没打开过的直接卸。两种都得活着，且"当场删掉"与"排到下次启动"必有一条成立。跑在临时副本里（它会真删文件），不进 CI。 |
| `--pintool` | 19 条几何断言：置顶窗那两个「收起」开关（离屏，一次都不 `show()`）。 |
| `--capflash` | `--capflash [次]`：把 F1 浮层真开一次（每下不到半秒）再走和 Esc 一样的取消路径，量「在不在、铺没铺满、压没压在最上」。 |

每一面旗标量到的数字、期望值为什么改过、反向验是怎么做到的，原样记在文末 **自测旗标逐条**。

## 数据目录与配置项（全表）

| 文件/目录 | 说明 |
| --- | --- |
| `config.ini` | 配置（搜索框齿轮修改，也可手改） |
| `apps.json` | 应用索引 |
| `usage.json` | 使用频率 |
| `icons\` | 应用图标缓存 |
| `plugins\` | 开发环境插件 |
| `webs.json` | 网站关键字 |
| `deepindex.dat` | 深索引全量快照（全盘索引，数百 MB） |
| `usnstate.dat` | USN 增量游标（几十字节） |
| `backups\` | 注册表删除前的 .reg 备份 |
| `captures\`（**默认在软件目录下**，不在 C 盘） | 截图产物：`cap-*.png` 是置顶窗的往期，`capture-*.png` 是手动保存的那张，`ocr-*.txt` 是提取文字存下来的。**存放位置可在配置页改** |
| `recordings\`（**默认在软件目录下**，不在 C 盘） | F2 录屏的 MP4（`rec-时间戳.mp4`）。**存放位置可在配置页改** |
| `clipimg\` | 剪贴板历史里的图片（开了剪贴板历史才有） |
| `ocr\` | 提取文字用的 PaddleOCR 模型 + `onnxruntime.dll`（首次用到时自动下载，之后离线） |
| `log.txt` | 运行日志（热键注册结果、扫描/增量统计） |

## 跨平台移植：平台层那道缝（WP1，2026-10-16）

- 他拍的是**全量移植三个平台**，所以顺序不是选出来的、是编译器定的：core 的 30 个 TU 直接 `#include <windows.h>`，Linux 上连 `ltoolcore` 都编不出来 —— 必须先抽缝，Windows 这边行为零变化，再往缝的另一头接 POSIX 实现。
- 第一轮只动两道最小的缝：**已知目录 + 环境变量展开**（`src/platform/Paths.h` → `Paths_win.cpp` / `Paths_posix.cpp`）、**打开 / 提权打开 / 在文件管理器里定位**（`src/platform/Launch.h` → `Launch_win.cpp` / `Launch_posix.cpp`）。Windows 那两份就是把 `SHGetKnownFolderPath` / `ShellExecuteExW` / `ExpandEnvironmentStringsW` 原样搬过去，一行语义没改；`revealInExplorer` 改名 `reveal`（那三个字在 mac 上是句假话）。
- `Utils::` 里那五个入口**删干净了**，18 个文件（src 8 个 + 插件源 10 个）全换成 `Platform::`：`expandEnv` 18 处、`launch` 11 处、`launchAsAdmin` 3 处、`reveal` 11 处、`knownFolderPath` 10 处、枚举项 13 处（外加 `Utils::KnownFolder` 这个类型名 1 处），另有 Utils.cpp 里 3 处不带前缀的内部调用；脚本按字节替换，跑完断言残留数为 0。
- 枚举项换了名字：`Desktop/PublicDesktop/UserStartMenuPrograms/CommonStartMenuPrograms` → `UserDesktop/SharedDesktop/UserApps/SystemApps`。Linux 对得上的是 XDG 桌面 + `~/.local/share/applications` + `/usr/share/applications`，macOS 是 `~/Desktop` + `~/Applications` + `/Applications`；`SharedDesktop` 在这两个平台上**天生是空的**，扫应用那几处本来就会跳过空根。
- **Wayland / macOS 上的降级是能力缺失，不是 bug**：`reveal` 在 Linux 只能打开父目录（xdg-open 没有「选中这一项」这个语义），带参数又不是可执行文件的目标在 POSIX 起不来（那是 Windows 文件关联才有的语义），这两条都写在头文件里，界面文案要跟着改口径。
- 判据：**`--seamtest` Windows 14 条、`seam_probe.sh` Linux 14 条，两边全绿**（这是第一刀当时数的；到 22:20 复跑已经是 16 / 23，见下面「第三道缝」）。原有那三面对照数一条没动：`--plugintest` 221 / `--pinyin` 52 / `--cfgcheck` 11（`--cliptest` 从 36 涨到 56 是同一天另一件事，见 **剪贴板历史的保留天数**）。
- **这条缝为什么必须新加判据**（反向验的结果）：把 `FOLDERID_PublicDesktop` 换成 `FOLDERID_CommonPrograms` → 红 2 条（「两两不同」、「Public 那一棵」），`INFO` 那行当场把共用桌面打印成 ProgramData 那棵；把 `expandEnv` 改成 `return s` → 红 2 条。但第一版那两条占位符判据是**拿 expandEnv 自己的输出和自己比**，改成直通照样绿 —— 这就是"自己比自己"的判据，等于没判据。换成跟 `qEnvironmentVariable("SystemRoot")` / `QDir::homePath()` 这两个**外部真值**比之后，直通才被抓得住。
- 顺手抓出两个连带问题：① `tools/make_sdk.py` 抄头文件时不建子目录，插件一旦 `#include "platform/Paths.h"`（现在有了）下次发 SDK 会当场抛 `FileNotFoundError`，已加建目录那一步，实跑到 `include/platform/` 两颗头齐；② CI 的判据清单是写死的那串旗标，新旗标不进去就等于只在开发机上跑过，`--seamtest` 已加进 `.github/workflows/build.yml` 那个循环。
- Linux 端**还没到能编整个工程**：configure 停在 `find_package(unofficial-bit7z)`（vcpkg 那个端口不出 Linux  triplet），`ltool_target_setup` 里那 18 个 Win32 导入库已经先收进 `if(WIN32)`。下一刀（WP2）要处理的是依赖分档 + `Archives`/`SevenZip` 那条压缩链，而不是假装缝抽完就能编。

## 剪贴板历史的保留天数（2026-10-16）

- 他要的是「给个 7 天，可设置天数」：配置页「剪贴板历史」那一栏多一行 **保留天数**（`clipboard/keepDays`，默认 7，0=「不限」，范围 0-3650），改一下当场生效、不用点保存。
- **两条上限取交集**：超过 N 天的一律清，没超龄的仍受「最多 200 条」兜底；清掉的条目连 `clipimg\` 里那张 png 一起删（`prune()` 那趟孤儿图清理本来就干这事）。裁剪时机三处：`load()` 读完立刻裁（并把 json 重写一次）、改了天数立刻裁、2 秒轮询里加一道"今天扫过没有"的闸门 —— 最后这道是给**收集开关关着**的那条路留的：`capture()` 第一行就 return，永远走不到 `prune()`，不补这一刀的话超龄条目会一直看得见。磁盘成本是一天一次，不是两秒一次。
- 边界说清楚：7 天 = 整 7 天那天就清（`secsTo(now) >= keepDays*86400`），"保留 7 天"不该留到第 8 天；时间戳读不出来的条目按超龄处理（界面本来也画不出它是哪天复制的）。裁掉就是真删了，改回「不限」不会把它们找回来 —— 这句写在 tooltip 里，界面上不说假话。
- 他真实的历史量过才动：`%APPDATA%\ltool\clipboard.json` 现在 200 条、最老 **1.6 天**，所以 7 天这条规则上线**一条都不会裁掉**（30/90 天同样是 0 条）。
- 判据：`--cliptest` 36 → **47 条**。新增 11 条是 4 条纯谓词（差 1 秒不超龄 / 整 7 天超龄 / 天数 0 永不超龄 / 无效时间戳算超龄）+ 4 条数据层（改成 7 天当场少一条、活下来的是哪两条按 hash 点名、留下的那张图文件还在、天数进了 config.ini）+ 2 条往返（改回「不限」不找回、config 跟着回 0）+ 1 条状态行（天数=0 时必须写「天数不限」，不许只报条数）。
- **夹具里的时间戳全部改成"从现在往前算"**：原来写死 `2026-05-04T23:01:02`，这种日期在按条数封顶的年代没事，一加天数规则就会在某天自己变成超龄条目、把那 36 条踩在一份被动过的数据上。沙箱 config.ini 现在把 `keepDays` 钉成 0，天数自己的行为单独测。
- **我这一轮自己写出来又自己删掉的一次真数据丢失（必须原样记着）**：上面那条"归一化"我只修了一半 —— keep 那一侧转成了正斜杠、`full` 那一侧还是拼出来的混合写法，两边永远不相等，于是**所有还在用的图都被判成孤儿**。更糟的是我为天数规则加了 `load() → prune()`，把这段低频的清理第一次挪到了**启动路径**上：21:05:09 装上的那版一启动就把他 `%APPDATA%\ltool\clipimg` 里 **49 张剪贴板图全删了**（条目里声明的合计 9.9MB）。证据链：clipimg 目录 mtime = `21:05:09.413`，和 log.txt 里 `clipboard: started enabled=1 items=200` 同一秒；json 里那 49 条的 `path` 现在全部 `exists=False`；`QFile::remove` 不进回收站。下一次 `load()` 会撞上"图没了就不留空条目"那条，把这 49 条历史一并丢掉。
- 修法三件，缺一不可：① **两侧**都过 `keepKey()`（正斜杠/反斜杠/连续斜杠/大小写全折叠）再比；② 加一道闸门 `sweepAllowed(itemCount)` —— 一条历史都没 load 到的时候**不许**启动批量删（真要全删走的是 `clear()` 自己那段）；③ 删了几张必须写日志（`clipboard: swept N orphan image file(s), items=… keepDays=…`），静默的批量删除等于没有发生。
- 判据：`--cliptest` 36 → **56 条**。新增 20 条里，11 条是保留天数（4 条纯谓词 + 4 条数据层 + 2 条往返 + 1 条状态行），9 条是这个孤儿判定（`keepKey` 三种写法同判 / 两个不同文件不许被归成一个 / 闸门在 0 条时拦住 / 现场造一张没人引用的 stray → 裁一次它必须走、历史里那张必须留、条目数不许被牵连）。**反向验**：把"只对一侧归一化"这个我真犯过的错还原回去 → 红 2 条；把闸门改成永不拦 → 红 1 条。
- 教训（比上面那条更通用）：**把一段"清理/删除"代码从低频路径挪到启动路径之前，要先说清它删的是谁**。我加 `load() → prune()` 是为了让天数规则在关着收集时也生效，结果顺手把一次字符串比较的错误放大成了一次全量删除。
- 顺手修的一个输出形状：`--cliptest` 以前打两行"失败 N 项"（判据自己打 5、宿主按退出码又打 1），grep 的人拿到的是那个被压成 0/1 的数。按仓库既有规矩给 `--cliptest` 传 `flag="clipboard"`，宿主就不再补那一行。
- 反向验三处故意改动都在**同一轮里一起红**，各归各的：边界符号 `>=`→`>` 红 1 条（整 7 天那条）；按天裁剪整块关掉红 3 条（少一条 / 活下来的名单 / 改回不限不找回）；天数不落盘红 1 条。有一条本来以为会红的没红 —— 「配置文件跟着回到 0」在"不落盘"那一版照样绿，因为夹具开头就写死了 0。教训同上一轮：**没红的那条要能解释**，别当成"这条也验到了"。

## 21:20 那次闪退：点剪贴板那张页 → 卡住 → 0xC0000005（2026-10-16 夜）

- 他报的现象：「在插件点击剪贴板卡，卡死了闪退了」。日志形状：`21:18:48 plugin: 建页 clipboard` → 70 秒空档 → `21:19:58 hotkey 呼出` → `21:20:03 FATAL CRASH: code=0xC0000005 addr=7ffeeae161f4`，转储 `%LOCALAPPDATA%\CrashDumps\ltool.exe.29012.dmp`。
- 转储能确定的：**异常地址在 Qt6Core.dll + 0x61f4**（`0xC0000005`）。这台机器没装 cdb/WinDbg、Release 又没有 PDB，我用 python 手写了一遍 minidump 解析（模块表 + 异常流 + 线程表 + AMD64 CONTEXT），拿到 Rip/Rsp 之后只能做"栈里哪些 qword 落在 ltool 自己模块内"这种残留扫描 —— 那 46 个命中里大部分是残留数据（`最近导出` 的偏移大到 +0xFF000 那种就不是函数边界），**不足以定罪**。归因结论只能到这里：崩在 Qt 内部的一次坏指针解引用，而栈顶残留里有 `qt_static_metacall@ToolsPage@`（商店那张页）→ 两个方向都在。
- **能定罪的是代码本身**：`ClipboardStore::restore()` 原来是 `const Item& it = m_items[index];` 然后往剪贴板写。写这一步在真实 Windows 上会**同步**回发 `dataChanged`（外壳连写入方自己一起通知），我们那个槽当时直接 `capture()` —— 它把这一条先 `removeAt` 再 `prepend`（去重置顶），`m_items` 就此搬家，而 `restore()` 手里那个引用指向已经释放的内存，最后那行日志还在读 `it.isImage` / `it.text`。这个形状完全对得上"异常地址在 Qt6Core 里"（QString 的字段被读在已释放的对象上）。**这条不是天数那轮新写的代码，是剪贴板功能自带的一条老 bug**，只是他现在才走到"复用中间那一行"。
- 修了两处，都在 [src/ClipboardStore.cpp](file:///D:/project/cpp/ltool/src/ClipboardStore.cpp)：① `restore()` 拿**副本**（`const Item it = ...`），引用不再跨过一次剪贴板写入；② `dataChanged` 槽改成 `QTimer::singleShot(0, ...)` 推到下一个事件循环回合，进去先比 `GetClipboardSequenceNumber()` 再决定收不收 —— 这样自己那次写既不重入、也不再白重编码一遍（图片那条实测是几百毫秒，就是他看到的"点一下卡住"）。
- **一条关于离屏测试的硬事实（下次别再踩）**：`QT_QPA_PLATFORM=offscreen` 下 `QApplication::clipboard()` 是**内存桩** —— `setText()` 不发 `dataChanged`、也不碰真剪贴板。所以"自写不重入"这 5 条判据跑在离屏里是必然绿的假绿。现在按平台点名跳过（同 `--recordui` 要真屏幕那条规矩），要真验就 `QT_QPA_PLATFORM=windows ./ltool.exe --cliptest`：那一档 61 条全绿，且反向验（把槽改回同步直收）当场红 1 条 —— 红的是「自我那次写不把历史顺序搅动」。
- 离屏 `--cliptest` 56 条 + 真剪贴板那 5 条 = 61；`--plugintest` 221 照旧。装的那份同步到本次构建后离屏跑 56/0 并打出那行 SKIP。
- **没查完的那一半**：栈残留指向 ToolsPage，而并行那个会话有 2 个未提交的文件（`src/ToolsPage.cpp`、`src/AboutPage.cpp`）**已经随这两个包一起装上了** —— 它的改动里有"卡片自己 mouseReleaseEvent 里先调基类""卸载/启用改成 singleShot 推迟""rebuildGrid 里 `setCurrentIndex(0)`"这几处，形状上都可能在点击卡片时踩到已删控件。我没有证据说就是它，也没有证据排除它。要定罪只能拿"再崩一次的转储"或"在真桌面上点一遍卡片"（后者要动他的屏幕，我没做）。

## 卸载排队那句要写在卡片上（2026-10-16 夜）

- 他报「右键动作卸载不了」。查到的事实：`store/pendingRemove` 记上了 `ltool-shell.dll`，21:47:59 那次启动由 `applyPendingRemoves` **真删掉了 1 个**（现在插件目录里既没有 `.dll` 也没有 `.dll.off`，待删表是空的），`HKCU\Software\Classes` 下 `*\shell\ltool`、`Directory\shell\ltool`、`Directory\Background\shell\ltool` 三颗 `Test-Path` 全是 False，日志里 `shellmenu: remove, 现在干净了吗=yes`。**东西其实卸掉了。**
- 让他说"卸载不了"的是界面：插件被进程加载着的时候点卸载，只会记进待删表、等下次启动才落地，而卡片上那句话原先只写给 `applyUninstall` 传进去的那个 `QLabel` —— 点完马上 `rebuildGrid()` 换整格，那句话跟着旧标签一起没了。他连点四下，屏幕上一条解释都没有。
- 改在 core 侧（[src/PluginStore.cpp:420](file:///D:/project/cpp/ltool/src/PluginStore.cpp#L420) 那一段）：`localRows()` 扫到排过队的文件名，就把它的 `note` 写成「xxx 已安排卸载 · 这个进程还加载着它，下次启动真删掉（138 KB）」—— 不靠任何一次点击的临时标签，重建多少次都在。**这是数据层说话，不是文案补丁**：卡片状态本来就该由"目录 + 待删表"两个事实决定。
- 判据：`--storetest` 64 → **67 条**（新加 3 条：排队那颗写明「下次启动真删掉」、没排队的对照不许被说成排队中、对照那颗必须在列表里——最后这条是防止上一条拿"空列表"假绿）。反向验：把那段 `if` 短路 → 当场红 1 条，且只有那一条红。
- 文档里的旗标数我重新数了：`--storetest` 原来两处写 60，实测本轮之前就已经是 64。

## 第三道缝：开机自启（2026-10-16 夜，跨平台 WP1 续）

- 选它当第三道的理由：那五个"机制级缺口"里**只有它有真 POSIX 对等物** —— 不是降级，是三个平台各有各的机制：Windows `HKCU\...\CurrentVersion\Run` 里那个值（不需要管理员）、Linux `~/.config/autostart/ltool.desktop`（XDG 自动启动规范）、macOS `~/Library/LaunchAgents/com.ltool.autostart.plist` + `RunAtLoad`。`Utils::setAutostart/autostartEnabled` 删干净，4 个调用点（配置页那颗 + 托盘菜单那颗）换成 `Platform::`。
- macOS 那一支**只写文件、不 `launchctl bootstrap`**：文件写上只是"注册"，真装载要么等下次登录、要么另外调 launchctl。所以那句"已开启开机自启"在 macOS 上只能说"注册上了"，界面上别说"已经生效"。这条限制写在 [src/platform/Autostart.h](file:///D:/project/cpp/ltool/src/platform/Autostart.h) 的注释里，不是埋在代码里。
- 两个纯函数是**为了能被问才抽出来的**：`autostartTarget()`（落点）和 `autostartEntry(exe)`（内容）。这条缝的失败模式只有"勾了没反应"，不会报错、不会崩，路径拼错就等于功能不存在。
- `.desktop` 的 `Exec=` 走 unix 引号规则：空格、制表、换行、双引号、反引号都要转义（`xdgQuote`），否则装到带空格的目录里那一行会被切成两个参数；plist 那一支要过 XML 转义（`xmlEscape`）。
- 判据：Windows 侧 `--seamtest` 14 → **16 条**，**只问落点和内容、绝不调 `setAutostart`** —— 它会真去动他机器的 HKCU Run 键（顺手 INFO 打一句这台机器当前是"已开/没开"，让他看见自测没替他改）。Linux 侧 `tools/seam_probe.sh` 14 → **33 条**（2026-10-16 夜又并进拼音那 10 条），其中自启那 9 条是**真往返**：`HOME` 指到一个临时目录 → 勾上 → 文件真在 `autostartTarget()` 说的那个路径上、`Exec=` 里是当前 exe → 关掉 → 文件没了、`autostartEnabled()` 跟着说没开。
- 反向验（三处故意改动，两边各自红自己那几条）：Windows 值名 `ltool`→`ltool-disabled` → 红「落点是 Run 键下的 ltool 值」；条目不转原生分隔符 → 红「条目写的就是当前这个 exe」；Linux `xdgQuote` 直接 return → 红「空格被转义」；文件名 `ltool.desktop`→`ltoolX.desktop` → 红「文件名就是 ltool.desktop」。**最后这条判据是这一轮新加的**：我先只做"写和读都用同一个函数"，那样改文件名是量不出来的（写读一起错就自洽）—— 又是一次"自洽的判据等于没判据"。
- 对照数一条没动：`--plugintest` 221 / `--pinyin` 52 / `--cfgcheck` 11 / `--storetest` 67 / `--cliptest` 56。装的那份同步到本次构建后 `--plugintest` 那 12 红还是"右键动作被他卸掉了"那一族（不是这轮带的）。
- 还欠着的：`Utils` 里剩下的 Windows 依赖（`extractIconToPng` 的 `PrivateExtractIconsW`、`fileTargetMissing` 的 `SearchPathW`、`killProcessTree`、`workingSetMB`、`trimWorkingSet`）＋ `src/pch.h` 之外那 38 个 `#include <windows.h>` 的 TU ＋ WP2 那个 bit7z configure 坎。

## 发版前那道 ABI 闸：`tools/abi_check.py`（2026-10-16 夜）

- 起因就是这一轮那一刀：core 少了一个导出符号名 = 已发布的插件 dll 当场 `LoadLibrary` 失败，而现场只报"加载失败"、不报缺哪个符号。我不想再靠事故回忆，所以把"这份新 core 能不能喂饱商店那批 dll"写成一个能跑的数。
- 做法是纯 PE 解析（python，不需要 dumpbin/WinDbg）：读插件的 `IMAGE_DIRECTORY_ENTRY_IMPORT`，挑出模块名是 `ltoolcore.dll` 的那些符号，再和本机那份 core 的导出表比。**退出码就是缺的符号数**（0 = 都能加载）。
- 拿真东西量了两头：对本机这批（19 颗，都是同一棵树编的）缺 **0**；对商店已经发布的那批（`C:\work\ltool_plugin\dist`，14 颗）缺 **7 个引用槽 = 2 个不同符号**，坏 **6 颗**（capture / language / record / registry / repair / startup）：`Utils::revealInExplorer` 被 5 颗 import，`Utils::launch` 被 2 颗。
- 比我先前口头说的"9 颗 / 3 个名字"更准：他机器上当场坏 9 颗，是因为里面还有 3 颗（downloads / grid / software）是本机构建那份、**商店清单里还没有**（#78 那五颗没进去的坑）；而 `expandEnv` 其实没有已发布 dll 用到它（那三个调用点都在 core 内部）。**口头数字再次因为没实测而偏**，这次是拿导入表实测的。
- 已经挂进交付闭环（[build/verify/deliver.ps1](file:///D:/project/cpp/ltool/build/verify/deliver.ps1) 的 `abi` 那一栏，52-83ms）：目录不在就 `SKIP ... 没查，不是通过`，绝不静默放行。**不 throw** —— 这一栏说的是"发版前要不要重发插件"，装到他自己机器上这件事不受它管。
- 顺带一条 PowerShell 的坑（我第一次就把这栏写成了假数字）：`python` 打出来的中文进了 PS 会被 GBK 码页搅成乱码，用中文行去 `Select-String` 数就数出 **0**，于是那一栏打印"缺 0 个符号"、而它上面刚列出 7 个。改成让 python 额外打一行纯 ASCII 的 `MISSING_SYMBOLS=<n>` / `BAD <dll> <n>`，PS 只数那个。**要跨进程传数就别用中文当键。**
- #91 现在有了数字可以拍：① 重发那 6 颗（要走插件仓库 CI，顺带把 #78 欠的五颗补齐）；② core 里留 `Utils::launch` / `revealInExplorer` 两个转发名（商店里那批永远不断，代价是两个没人调的壳）。我的倾向是 ①，因为 1.1.0 还没发、这一版正好把 #78 一起收掉。

## 台阶一：Linux 上 configure 过了，编出来的第一批 TU 有名单（2026-10-16 夜）

- 做法是把"哪些 TU 现在就可移植"变成**量出来的名单**，不是我印象里的数：在 WSL 上对 core 的 36 个 TU 逐个 `g++ -std=c++20 -fsyntax-only -I src $(pkg-config --cflags Qt6*)` → **13 过 / 23 不过**。过的：`Pinyin` `Archives` `CaptureHistory` `DownloadManager` `FileTools` `InfoRow` `PluginStore` `SilentInstallLog` `SysTools` `ToolsPage` `WebShortcuts` `WebsitesPage`（+ 平台层那三道缝）。不过的 23 个全卡在同一类：`#include <windows.h>`，也就是注册表 / USN-MFT / WGC+MF / HKCU verb / COM 这五族机制还没抽缝。
- `Archives.cpp` 居然过了：它吃的是**系统 libarchive**（WSL 里 `/usr/include/archive.h` 在），bit7z 那颗后端在 `SevenZip.cpp` 里，那一颗照样 FAIL —— 所以"7z 压缩"这一路在 Linux 上还没有后端，别当成已经能用了。
- CMake 的形状：非 Windows 走到 `add_library(ltool_portable OBJECT 缝 + 名单)` 然后 **`return()`**，core / exe / 插件那几段一行都不执行。为什么用 OBJECT：这批名单里还有一堆调用 `Utils::` 那些**还没进缝**的函数，做成 `.so` 要么链接报未定义符号，要么（Linux 默认允许 .so 留未定义符号）**"链接成功"却根本跑不起来** —— 后者比现在老老实实说"编到这里为止"更像假绿。
- 实测（WSL Ubuntu 24.04 / gcc 13.3 / cmake 3.28）：`cmake -S . -B /tmp/lp2` **CONFIGURE OK**（那条 status 打的是"只编平台层那三道缝 + 名单里那 12 个 TU"），`cmake --build --target ltool_portable` **BUILD OK**。Windows 那一整条路径一个字没动：`--seamtest` 16 / `--plugintest` 221 / `--pinyin` 52 / `--cfgcheck` 11 / `--storetest` 67 / `--cliptest` 56 全绿、退出码 0，闭环 20.9s、包 22.09MB、SHA `MATCH 3A5404E9F16D`。
- 顺手让 Linux 那侧第一次**跑起来**验了行为而不是只编译：`tools/seam_probe.sh` 从 23 涨到 **33 条**，新并进去的 10 条是拼音引擎（`usableQuery` 三条闸门、全拼/首字母/中段、`An` 不抢「广告弹窗」的直达、`renwu` 不抢「开机启动分析」、表和码点对齐的"垃圾/任务"抽查、多音字两个读音）—— 全绿。**这十条是有分量的**：搜索框那一整路是可移植的，Linux 上不用重写。

## 第五道缝：进程与文件元信息（2026-10-16 深夜，WP2 第二块砖）

- 动机不是"再抽一道缝好看"，是 **`Utils.cpp` 自己还不能在 Linux 上编** —— 所有人都在调它，它就是一堵墙。里面最后六个 Win32 入口切成分给了两处：`platform/ProcInfo.{h}`（`killProcessTree` / `workingSetMB` / `trimWorkingSet`）和 `platform/FileMeta.{h}`（`extractIconToPng` / `fileVersion`），另外把"只给文件名时系统找不找得到"（`SearchPathW`）归进 `Paths.h` 叫 `findExecutable`。`fileTargetMissing` 那个判断逻辑留在 `Utils`（本来就跨平台），只把平台那一步调出去。
- 三个真对等物 + 两件如实做不到：**`findExecutable`**（POSIX 用 `$PATH`）、**`workingSetMB`**（读 `/proc/self/statm` 第二列乘页大小）、**`killProcessTree`**（`/proc/<pid>/task/<tid>/children` 递归，读不到就扫 `/proc` 比 `PPid`，最后 `SIGKILL`）—— 探针里真的起了一个 `sleep 30` 再杀掉，验 `/proc/<pid>` 从在到没了。做不到的两件：`trimWorkingSet` 在 POSIX 没有对等物（`malloc_trim` 只还堆顶，动不了内核记的常驻页）→ **返回 false**，调用方要说"这平台做不到"；`fileVersion` 普通文件没有版本资源 → **空串**，不许编一个。
- 判据：Windows `--seamtest` 16 → **20 条**，Linux 探针 23 → **43 条**。Windows 那 20 条里有一条顺带量出来的事实：**我们自己的 exe 没有 VERSIONINFO 资源**（`fileVersion(自己)` 是空，`Get-Item .VersionInfo.FileVersion` 也是空）—— 关于页那个版本号是编译期宏，PE 里读不到，所以任务管理器和"属性→详细信息"的版本列是空的。我没有为了让判据变绿去改期望值，而是把判据换成拿 `cmd.exe` 量（真读得到 `10.0.26100.x`），自己那份作为 `INFO` 打出来 + 记成已知缺口。
- **Linux 侧的 TU 名单从 12 涨到 13**：`Utils.cpp` 进来了（WSL 复测 `g++ -fsyntax-only` 逐个 TU 量：还是那 12 个 + Utils，其余 22 个照旧卡在 `windows.h`）。CMake 的 `LTOOL_CORE_PORTABLE_SRC` 跟着加了一行。
- **ABI 闸当场涨了价**：`deliver` 的 `abi` 栏从"缺 7 个符号"变成**缺 8 个**（capture 从缺 1 变缺 2）。这轮又删了六个 `Utils::` 名字，已发布的那批里 import 到的多了一个。#91 现在更贵了 —— 越往后抽缝，要重发的插件越多。这不是停止的理由，但它是"什么时候停手重发一轮产物"的定价信号。
- 对照数与交付：Windows 221 / 52 / 11 / 67 / 56 + `--seamtest` 20 全绿；闭环 18.9s、22.09MB、SHA `MATCH 5B7CA22BD43D`，装的那份同步到本次构建（`--plugintest` 那 12 红还是"右键动作已卸载"那一族，不是新伤）。

## 链接缺口地图：`tools/link_gap.sh`（2026-10-16 深夜）

- 只编不链（CMake 那个 OBJECT 库）回答不了"离能链出 core 还差几步"：编得过的 TU 会引用编不过的那批，链接期才暴露。这个脚本把可移植那 18 个 TU 真去 `-shared -Wl,--no-undefined` 链接一次，让**链接器自己列缺口**。
- 实测结果：**59 个未定义符号**，按归属数下来主要是 —— `PluginHost::*`（`page` / `meta` / `feed` / `reload` / `pluginsDir` …，外加它的 vtable/staticMetaObject）、`FileOps::*`（`whoHas` / `shred` / `killProcess`）、`ShellMenu::syncWithPlugin`、`DevEnv::installLocation` / `applyProxy`、`DownloadManager::changed()` 与 `InfoRow::checkToggled(bool)` 这类 **moc 生成物**（对应 TU 没进来就会缺）。剩下 8 个是 vtable/typeinfo/staticMetaObject —— 那不是函数缺，是"这个 QObject 类的 moc 编译单元没进这批"。
- 所以下一级该做的顺序也清楚了：**先 `PluginHost`**（它是插件能不能加载的核心，而 `QPluginLoader`/`dlopen` 在 Linux 上有真对等物，是最有回报的一道缝），再 `FileOps`（`whoHas` 在 Linux 是 `/proc/*/fd` 扫、`killProcess` 是 `kill(2)`、`shred` 有对等物），`ShellMenu`/`DevEnv` 那两处要么给空实现要么让调用方在别的平台上不引用 —— 那是能力问题，不是工作量问题。
- 用法：`wsl -e bash tools/link_gap.sh`。名单跟 `CMakeLists.txt` 的 `LTOOL_CORE_PORTABLE_SRC` 同源，**改那边要改这边**（脚本里那行注释就是提醒这件事的）。

## 第六道缝：插件产物的命名（2026-10-17 零点）

- 这一刀的规格写错了前提，动手前通读 `src/PluginHost.cpp` 288 行推翻了它：**Win32 装载调用一处都没有**（`LoadLibrary/GetProcAddress` 全仓只剩 `plugins/capture/OcrPp.cpp` 里 onnxruntime 那一颗句柄）。宿主走的是 `QPluginLoader` + `QLibrary`，Qt 自己就跨平台。所以不存在"第六道装载缝"，再包一层 `Platform::PluginLoad` 就是把 Qt 的实现平行做一遍。
- 真正的平台差异只有**文件名**：Windows `ltool-<id>.dll`、Linux `libltool-<id>.so`、macOS `libltool-<id>.dylib`（POSIX 的加载器认 `lib` 前缀）。缝落在 `src/platform/PluginNames.h`，`PluginHost` 那三处写死 `.dll` 的（扫描匹配式 + 两种 `.dll.off` 历史形状）改吃它。做成**头文件内联**而不是 `_win/_posix` 两份 .cpp：每条 OS 就一个常量，拆两份要多两个 TU 还会进 core 导出面 —— 于是 `abi` 栏这轮**还是缺 8 个**，没被这一刀推贵。
- GCC 又当场逮到两处 MSVC 悄悄惯着的：① `QJsonDocument` 用了没 include（靠传递包含编过）；② `QLibrary::resolve` 返回 `QFunctionPointer`，隐式转 `void*` MSVC 当扩展收、GCC 直接 `-fpermissive` 报错，改成显式 `reinterpret_cast`。
- `PluginHost.cpp` 进可移植名单（core 侧 13 → 14 个，连平台五缝共 **19 个 TU，19/19 在 WSL 编过**）。链接缺口 **59 → 54**，掉的那几个正是 `PluginHost::page/meta/feed/reload/pluginsDir` —— 规格里"PluginHost 是最大的一族"这件事本身是对的，只是缺口成因是"这个 TU 没进名单"，不是"它不可移植"。**54 里有 12 个是 vtable/typeinfo/staticMetaObject**：这个探针直接 g++ 不跑 moc，那部分是 AUTOMOC 的活儿，数缺口时先减掉（已在脚本头点名，免得下一轮又把它当实现缺口）。
- `tools/link_gap.sh` 的名单不再手抄，改成从 `CMakeLists.txt` 现读，并加一条**独立取法核对条数**（`grep -oE`）。这条核对第一轮就抓到 awk 解析漏了 2 个 —— 右括号粘在最后一个文件名上（`src/WebsitesPage.cpp)`），漏掉的恰好是 `ProcInfo_posix.cpp`，所以 `Platform::killProcessTree` 还挂在缺口里；而核对用的那条 grep 自己也是错的（字符类里没 `/`，带子目录的路径整条匹配不上）。**两个独立的错互相抓出来才叫证据**。
- 装机复跑抓到一件不在代码里的事：`D:\software\ltool\ltool-plugins` 只有 **17 颗**，比本机构建的 19 颗少了 `network` 和 `shell`。所以那 12 条红不是回归，是"装的这棵缺插件"（其中 7 条是右键七个动作没人认领 + 「工具插件数=19 实到 17」）。同步成 19 颗后：装的那份 `--plugintest 221 / --storetest 67 / --cliptest 56(+1 点名 SKIP) / --seamtest 20`，全 0 红；在跑的实例重启后日志数到 **19 条 `plugin: 看到`**，右键那一路有人接了。这正是 #78 那五颗没进插件仓库的代价 —— 从商店装的新用户同样拿不到这两棵。
- 交付数：`build 7869ms errors=0` / `abi 缺 8 个符号` / 包 **22.09MB** / `install MATCH 0C36B9599360` / 闭环 18.65s。判据（本机构建那棵）：221 / 20 / 67 / 11 / 52 / 56+SKIP / 64 / 44，全 0 红。
- 一条手滑：`powershell -Command "…$_…"` 里的 `$_` 被 bash 吃掉 → **整段解析错误，一条命令都没执行**（我那条"重启实例"就这么静默没跑，回头查 `StartTime` 还是 00:19:58 才发现）；PowerShell 的语法错不是"这一段没跑"，是"全部没跑"。另：一次 `Copy-Item` 目标写成了目录形状，在他插件目录里套出一个空 `ltool-plugins\ltool-plugins\`（确认空之后 `rmdir` 收掉，rmdir 只对空目录生效）。这类事以后一律落成 ASCII `.ps1` 用 `-File` 跑，新增 `build/verify/restart_ltool.ps1`。

## 商店协议去 `.dll` 硬编码（2026-10-17，#78 的第一半）

- 协议侧三处假设一次改齐：`parseIndex` 的文件名校验、`localRows` 的目录扫描匹配式、文件名**反推 id**（原来 `startsWith("ltool-") → mid(6)` 再 `chop(4)`）。命名缝补了 `pluginFilePrefix()` 与 `pluginIdOf()` 两个方向；Windows 侧算出来的字符串逐字等价（判据没动就跑得出等价）。
- 新增一条能红的判据：清单里混进一颗**另一个平台后缀**的 `ltool-foreign.so`，必须被拒，而且拒的理由要点出「这里要 .dll」—— 不是只说"不是 .dll"（那句在 Linux 上等于没说）。`--storetest` 67 → **68**。
- Windows 上"后缀规则改了"这件事本身测不出来（这台机器的后缀就是 dll）。所以去 POSIX 那侧补 **6 条**：`so` / `*.so` / `libltool-shell.so` / 反推回 `shell` / **`ltool-shell.dll` 在这边反推不出 id** / 裸 `.so` 不算插件。`tools/seam_probe.sh` 43 → **49 条，0 红**。
- **真清单实测**：`--storenet` 拉真 index（14 条），新校验下 **拒收 0** —— 这是"协议改了但 Windows 行为没变"最硬的一条证据（假夹具只能证明我自己编的那几条）。
- `--storenet` 另有 **4 条红**，做了真 A/B 才敢定责：把这三个文件退回 HEAD、重编、同一条判据跑出来 **13 过 4 红，红的名字逐字相同** ⇒ 这 4 条在 `25a1e0d` 上就已经红了，CI 不含 storenet 所以一直没被看见（另开任务）。A/B 期间用 `git diff > 补丁` + `git checkout --` + `git apply` 走，不碰 stash。
- 自测夹具里 **57 处** `.dll` 字面量换成从缝里现算。批量替换用的 python 脚本在 raw 字符串里写了 `\'`，**37 处非法转义原样进了源码**，MSVC 当场 `C2017/C2001` 红 —— 幸好报错了：那一轮我差点把**旧产物**跑出来的 536 全绿当成本轮的数（`build rc=1` 之后 exe 的 mtime 才是唯一可信的判据）。
- 装机那条 `其中 0 颗和本机构建同一份` 不是坏信号：core 一重链，19 颗插件的哈希必然全变。新增 `build/verify/sync_plugins.ps1`（停实例 → 拷 → 数哈希 → 重启），装的那棵 19 颗与构建**逐颗一致**，这条用 python 独立核过一遍，没信脚本自报的数。
- 对照数：本机 221 / **68** / 20 / 11 / 52 / 56+SKIP / 64 / 44 = **536 条 0 红**；装的那份 221 / 68 / 20 / 11 / 56+SKIP，`--storenet` 13/4（同 HEAD）；Linux 19/19 TU 编过、链接缺口仍 54。闭环 19.0s、22.1MB、`install MATCH DDA93318A2AB`。

## #94：`--storenet` 那 4 条红（2026-10-17 凌晨定案，红的不是产品，是判据认字法）

- 现象：`有状态 5 / 卡片 19`、`14 张不对（一张没有状态词的卡）`、`可更新 0 张，缺箭头 0 张`、`禁用/启用 19 颗 / 装着 5 张`。
- 取证先加再解释：把"没有状态词的卡"改成**打印那张卡的全部标签**。一跑就定死了 —— 那 14 张写的是 `↑ 可更新 v1.0.0 · 0.10 MB`，界面说实话，**是判据拿 `startsWith("可更新")` 认不出带箭头前缀的写法**。`可更新 0 张、缺箭头 0 张` 那条更是空转：`upCards=0` 让它永远"通过"。
- 修法是回到数据层，不是放宽阈值：`ToolsPage::makeCard` 给卡片挂 `ltoolState` 属性（状态那个枚举），判据**先认属性、再要求界面那句里确实出现了对应的词**（允许 `↑` 前缀与 `(v…)` 后缀）。箭头那条同样改法。两边都要对上：状态有、字没画，照样红。
- 数字：`--storenet` 13 过 4 红 → **17 过 0 红**（`可更新 14 张，缺箭头 0 张` 第一次是真数出来的）；其余判据一字没动（221 / 68 / 20 / 11 / 52 / 56+SKIP / 64 / 44 = 536，0 红）。
- 为什么一直没被发现：`--storenet` 要联网，**不在 CI 那串旗标里**；而且它红的时候退出码非 0、没人跑。教训一条：**"这条判据上次跑是什么时候"要说得出**，否则它是不是在测东西没人知道（这次 `upCards=0` 就是装饰了很久的绿）。

## 链接缺口从 54 掉到 8：#93 的真相是探针少给了两样依赖（2026-10-17 凌晨）

- 起因是 #93 那条"54 个里 12 个是 moc 产物，先脑内减掉"。**减掉不如补上**：脚本现在自己跑 moc（名单里每个 TU 的同名头，带 `Q_OBJECT` 就过一次，实测 5 个），再把 `pkg-config --libs libarchive` 链上 —— CMake 那边 portable 库本来就 `find_package(LibArchive)` 链着，探针不链就会多算十来个 `archive_*`"缺口"。
- 结果：**54 → 8**（元对象族 1 个 = `SettingsDialog::staticMetaObject`，那是那个类的 TU 还没进名单；真缺口 7 个）。剩下就是四个类族：`DevEnv::applyProxy` / `installLocation`、`FileOps::killProcess` / `shred` / `whoHas`、`SevenZip::compress`、`ShellMenu::syncWithPlugin`。
- **我上两轮报出去的 59 和 54，大头是测量方法自己造的**，不是实现缺那么多。归因顺序记一条：报"还差 N 个"之前，先核对**探针的依赖集和真实构建一致不一致**（moc、第三方库、编译旗标），差一样就虚高一截；能补进探针就别写在注释里让人脑内减。
- 顺手核实掉一条我以为的真缺口：`Archives.cpp` 调的 `archive_write_open_filename_w` 这类宽字符入口，**Linux 的 libarchive 也导出**（`nm -D /usr/lib/x86_64-linux-gnu/libarchive.so` 里命中 1 个），所以它不是 #90 的活。写这句前先查过，别再拿"Windows 才有的 API"当直觉结论。
- 这一改**只动 `tools/link_gap.sh`**：没碰任何 C++、没有产品代码变化，所以没跑 deliver（产物与上一轮逐字节相同，跑了只会白杀一次他的实例）。

## 商店「可更新」的语义改成版本优先（2026-10-17，他点了方案 2）

- 实测起因：`--storenet` 修好后第一次对上真实界面 —— 19 张卡里 14 张报「可更新」。查清单：14 条 `version` 全是 `1.0.0`，本机 14 颗哈希与清单 **0 颗一致** ⇒ 旧的 `compare()` **只比哈希**，版本号一次都没参与。后果不是"我机器特殊"：商店 CI 每次跑都重编，MSVC 产物哈希必变，**所有人每次发版都会看到一片「可更新」**。
- 新语义：`Update` 只留给「清单版本号 > 本机版本号」；哈希不同但版本不比本机新 → 新增一档 `Different`，状态词「与商店不同」，按钮「换成商店那份」（不是「更新」），提示分别说"版本号都是 v… 但内容不同"和"本机 v… 比商店还新，按它会降级"。一侧读不到版本号也走 `Different`（说不清就不许当新版卖）。
- 本机版本号从哪来：`PluginHost::metaVersionOf(path)` —— `QPluginLoader::metaData()` 只读嵌在产物里那份 plugin.json，**不装载**。
- 卡片文案顺手改掉一处自相矛盾：`↑ 可更新 v1.0.0` 里那个版本号印的是**本机自己**那份（`localRows` 填的就是本地元数据），读起来像"要更新到 1.0.0"。两档有两个版本号的（可更新/与商店不同）不再拼 `(v…)`，版本对照进悬停。
- 版本号比较从「关于」页挪进 `Utils::compareVersions`（商店也要同一套规矩，抄第二遍迟早分叉；`AboutPage::compareVersions` 与那份 `segNum` 删掉，8 条判据改指 Utils）。
- 判据：`--storetest` 68 → **73**（新增"借一颗真插件读自己版本号" + 清单更高/一样/更旧/读不到 四档）。第一版把借来的真 dll 放进了别人要数插件颗数的那个目录，**红了两条**（`6 应 4`、`5/1 应 3/1`）—— 新夹具改放自己的子目录。`--storenet` 17/0，且那条「可更新有绿箭头」不再假设世界上一定有可更新的卡，改成把**本次量的张数**写进名字与实值（现在是 `可更新 0 张`，诚实的 0）。
- 真清单实测（装好的那份）：状态行 `19 张卡 · 能装或可更新 0 · 拒收 0 · 清单外 5`，14 张走「与商店不同」——**这就是他现在界面会看到的样子**。
- 欠着的一条（#78 落地时要一起拍）：19 颗 `plugin.json` 的 `Version` 全写死 `1.0.0`，没人负责递增；发版纪律不加，这套版本判定就永远是 `Different`。

## 插件版本号改成「每颗自己递增」，并给它立一道闸（2026-10-17，他拍的形状）

- 形状：号不跟 core 走，改了哪颗那颗自己 +1（末位；接口或行为大改自己进位）。core 那边改了不动插件号 —— 插件 dll 是导入 `ltoolcore.dll` 的，core 变了它字节也没变，那不在这条纪律的范围里。
- 闸：`tools/plugin_version_gate.sh [基线ref]`，默认基线 = 最近一个 tag（发出去过、用户按那批号装过的那个时刻）。只管 `plugins/<id>/` 里除 `plugin.json` 之外的文件：源码改了而 `Version` 一模一样 → `NEEDS_BUMP <id>`；号往回走 → `VERSION_WENT_BACK <id>`；基线上没这颗（tag 之后新建）→ `NEW_SINCE_BASE`，1.0.0 就是它第一号，不要求递增。计数行一律 ASCII（`GATE_PLUGINS=` 等），这台机器 PowerShell 码页 GBK，拿中文行 grep 计数会读出 0。
- 标签那次 run 的空转防线：基线 tag 正好指着 HEAD 时（CI 里 tag push 就长这样）自动往前退一个 tag，否则差集永远是空集 —— 那是装饰性的绿。实测 `bash tools/plugin_version_gate.sh $(git rev-parse HEAD)` 打印「基线退到 v1.0.3」，量的还是那 19 颗。
- 一次性基线：v1.0.3 之后源码动过的 12 颗里，9 颗（capture / downloads / language / record / registry / repair / shell / software / startup）1.0.0 → 1.0.1；另 3 颗（clipboard / grid / linuxdo）是 v1.0.3 之后新建的，留在 1.0.0。bump 前闸报 `NEEDS_BUMP=9`，bump 后报 0。
- 反向验证：shell 改回 1.0.0 → 闸点名 shell、`NEEDS_BUMP=1`、退出码 1；capture 改成 0.9.0 → `VERSION_WENT_BACK`、退出码 1；还原后回到 0。两个分支都在真对象上红过，不是写着好看。
- 判据：`--plugintest` 加一条「plugin.json 的版本号是 x.y.z 形状 [实值]」，在 19 颗上量的（221 → **240**）。拿一颗真插件（json）把 Version 写成 `1.0` 重编，那条当场红且把 `[1.0]` 印在名字里；还原重编回到 240/0。为什么要判形状：`PluginStore::compare` 里一侧版本号空或垃圾就走「说不清有没有新版」，那颗永远等不到「可更新」。
- CI：`.github/workflows/build.yml` 的 checkout 改 `fetch-depth: 0` + `fetch-tags: true`（默认那次浅克隆里 `git describe --tags` 直接失败，闸会报 `GATE_BASELINE=none` 而不是假装通过），闸放在装依赖之前，秒级失败。**插件仓库那份 workflow 没装这道闸**：它 `plugins/` 是从本仓库拷过去的快照，基线该跟谁比不归这条纪律管 —— 要装就连 #78 那批对外动作一起定。
- 后果说清：商店清单 14 条版本号还是 1.0.0（现拉的那份 index.json 逐条看过：ads base64 capture clipboard disk json language network record registry repair shell startup user）⇒「与商店不同」那 14 张里 **7 张**现在写的是"本机 v1.0.1 比商店 v1.0.0 还新…按它会降级"：capture / language / record / registry / repair / shell / startup。bump 了的另两颗（software、downloads）清单里根本没有，走「清单外」——这一面 5 张就是 downloads grid linuxdo software tasks。这两个数是 `--storenet` 新加的 INFO 行现量的（各档分布 + 那个 7），不是我拿 9 减出来猜的（猜的那一遍真错了：software 其实不在清单里，startup 在）。等他把插件产物重发一批（#78 对外的下一步），两边同号才回到「已安装」。`Version` 是嵌在 dll 里的元数据 ⇒ 那 9 颗产物字节确实变了，装的那份要重新同步（本轮 sync 后 19/19 与构建逐颗一致）。
- 数字：`--plugintest` 240/0、`--storetest` 73/0、seamtest 20/0、cfgcheck 11/0、gridtest 64/0、pinyin 52/0、linuxdotest 44/0、cliptest 56/0+1 SKIP；`--storenet` 16 过 1 红，红的是一条判据之外的东西 —— `检查更新走网页 302` 拿到 HTTP 0。同一条 curl 分测两条路：直连 8.0s 超时（code=000），经本机 9980 代理 0.83s 拿到 302。同一轮 40 分钟前那条还是 200，所以红的是**这台的出口**，不是这轮的刀；`--storenet` 要联网又不在 CI 里，这条以后还得靠"上次跑是什么时候"盯着。

## 第八刀：FileOps 抽成 platform 那道缝（2026-10-17，链接缺口 8 → 5）

- 为什么开在这里：`src/FileTools.cpp` 早在可移植名单里，而它调的三颗（`shred` / `whoHas` / `killProcess`）实现全在 `src/FileOps.cpp` —— 那个 TU 整个卡在 Win32 上，链接期就缺这三个符号。
- 切法：只抽真的分叉的两个动作 `shredOne` / `removeEmptyDir`；「先走文件、目录从最深往上删、最后删自己」那套顺序两边一模一样，所以写成 `src/platform/FileOpsShred.h` 里一份 `inline shredWalk` —— 抄第二遍迟早分叉这条已经量过代价了。各平台一份的只有 `whoHas`（Windows 问 Restart Manager，POSIX 扫 `/proc/<pid>/fd` 按 st_dev+st_ino 比对）和 `killProcess`。
- `src/FileOps.cpp` 只剩 `info` / `usage` / `human`：那三件要的是盘符、簇、NTFS 压缩大小，POSIX 没有对等概念，所以它不进可移植名单 —— 不是没做完，是不该在那边做。
- POSIX 那三份是真实现不是空壳：覆写走 `open(O_WRONLY)` + `write` + `fsync` + `ftruncate`，删除走 `unlink`，thorough 走 `rename` + `unlink`；占用扫描认得出 `tail -F` 那颗 pid；结束进程先 SIGTERM 等 2 秒再 SIGKILL，僵尸算已经没了（`kill(pid,0)` 对别人的进程返回 EPERM，不能当成"不在了"）。
- 两条平台边界要说清（不是降级）：① 不是 root 只看得到同一用户的进程，读不到别人的 fd 目录就跳过 ⇒ 空表不等于"没人占用"；② Windows 那句 `SetFileAttributesW(NORMAL)` 这边没有对等物也不需要 —— POSIX 删文件看的是**所在目录**的写权限，不是文件属性。还有一条别混：`FileOps::killProcess` 只杀点名的那颗（和 Windows 一致），要连子孙的是 `Platform::killProcessTree`（取消安装那一路）。
- Windows 行为零变化的证据：搬家前后各跑一遍同一批夹具（`--shredtest` 3MB 浅/3MB thorough/整棵目录树/不存在 + `--whohastest` 无主/有人开着并 kill），把 pid 和路径归一之后**逐行相同** —— `erased=3145728`、`erased=12288`、`erased=-1 文件不存在`、`holders=1 → kill ok → 结束后 0`。为了这条能量出来，顺手给 `--whohastest` 加了 `kill` 那个参数（结束逐个占用者再数一遍）。
- 判据：`--seamtest` 20 → **28**（两边共用同一组期望：粉碎报的字节数、真没了、乱名 tmp 不留、目录树 12288、不存在的返回 -1 且有原因、无主是空表）。「自己开着的文件认不认得出自己」只打 INFO 不当判据 —— Restart Manager 和 `/proc` 对"看得见查询者本身"本来就不是同一套约定，本机实测 Windows=1 个。POSIX 探针 49 → **63**（同一批断言 + 拿 `tail -F` 当占用者的端到端：认出 pid、杀得掉、杀完空表、再杀老实报"没有那个进程"）。
- 数字：链接缺口 8 → **5**（元对象族 1 = `SettingsDialog::staticMetaObject`，那个 TU 还没进名单；真缺口 4：`DevEnv::applyProxy`、`DevEnv::installLocation`、`SevenZip::compress`、`ShellMenu::syncWithPlugin`）。离线判据 560 → **568** 条 0 红；装的那份 `--seamtest` 28/0、`--plugintest` 240/0。`abi` 仍缺 8 个符号（这轮只加名字没删名字，商店那批导入的符号一个没少）。闭环 24.7s、22.1MB、`install MATCH 678B269C76CA`，sync 后 19/19 与构建逐颗一致。
- 这轮踩的一条工具链坑（`edit-anchor-newline-hazard` 的新变体）：用 bash heredoc 喂 python 往脚本里插代码时，源码里的「反斜杠 + n」被吃成"真换行"，于是 `seam_probe.sh` 里出现 C 字符串断成两行的 `std::printf` 和 g++ 命令行末尾一个裸的反斜杠 n（`ld: cannot find n`）。两处都是拿 Edit 落的才修好 —— 以后往脚本/源码里插带转义的代码，别走 heredoc-python 这条路。
- 版本判断：**不升**。Windows 这侧是搬家（六个场景逐行相同、abi 没变），POSIX 那侧还没产出用户拿得到的东西。

## 第九刀：DevEnv 的代理与安装根抽成第八道缝（2026-10-17，链接缺口 5 → 3）

- 缝开在哪：`DevEnv::applyProxy` / `installLocation` 的**动作两边共用**，不一样的只有两件事 —— 系统代理从哪读、装东西的根在这台机器上长什么样。所以新增 `src/platform/SysConfig.h`：`systemProxyUrl()` + `defaultInstallRoot()` + `usableInstallRoot()` 各平台一份，`normalizeProxy()`（把 host:port 补成 URL 那条规矩）两边共用一份 inline，不抄第二遍。
- 另外新建一个可移植 TU `src/DevEnvConfig.cpp`（Windows 的 core 名单和 Linux 的可移植名单里都有它）：`proxyUrl` / `applyProxy` / `installLocation` 住这儿。为什么非要新 TU：`src/DownloadManager.cpp` 和 `src/PluginStore.cpp` 早就在可移植名单里，链接期要找得到这几个符号，而 `src/DevEnv.cpp` 整个还卡在注册表那套 Win32 上。
- `DevEnv::systemProxyUrl` 只剩一层转接，为什么不删：配置页那句「系统代理是 X」还在读这个名字，而且 **core 的导出面 = 已发布插件的 ABI，名字可以加不可以缩**（#91 那条）。这轮的 abi 实测仍是缺 8 个符号 —— 没删任何名字的直接证据。
- 两条语义在这轮被明确钉住（都是以前只写在注释里的）：① **Windows 上「安装目录留空」= 不指定 --location**，不是"用默认根"；「没配过」才给默认根。搬家时把这两种情况分开处理（`cfg.contains` 而不是 `cfg.value(带默认)`），就是为了不改这条语义。② POSIX 上没有"不指定装哪"这回事，所以空 / Windows 盘符写法 / 相对路径一律退到 `~/software` —— 装的根必须真的能用，不能跟着工作目录跑。
- POSIX 的代理是真对等不是降级：读 `http_proxy` / `HTTP_PROXY` / `https_proxy` / `HTTPS_PROXY`（curl、wget、pip、git 全认这套）。一条**宁缺不假**：值是 `socks5://…` 时返回空，因为下游 `applyProxy` 只会摆 `QNetworkProxy::HttpProxy`，把它报成 http 代理是骗人。PAC/自动发现两边都解不了（Windows 那半本来也解不了）。
- Windows 行为零变化的证据：`--seamtest` 搬家前后各跑一遍，**逐行相同**（31 条时），包括新加的三条真值断言打出来的 `INFO 生效代理=[] 系统那半=[]` —— 这台机器 `net/proxy` 留空、注册表那个 9980 又没被设成系统代理，所以那三条走的是"没代理"那一支。**这就是为什么还要用夹具量规则本身**：`normalizeProxy` 三条 + `usableInstallRoot` 三条（Windows 的 `Z:` 退 `C:`、留空还是留空、C 盘和网络路径不动），否则"有代理"那一支永远没被人碰过 = 装饰性的绿。
- 判据与工具：`--seamtest` 28 → **37**；POSIX 探针 `tools/seam_probe.sh` 63 → **69**（代理那四条用 `qputenv` 摆现场，不依赖跑的人机器上有没有代理）。链接缺口 **5 → 3**：只剩 `SevenZip::compress`、`ShellMenu::syncWithPlugin`（就是要他拍形状的那两个，#90）+ `SettingsDialog::staticMetaObject`（那个 TU 还没进名单，不算实现欠账）。可移植 TU **19 → 22**（+FileOps_posix、+SysConfig_posix、+DevEnvConfig）。
- 顺手用真 CMake 在 WSL 复核了一遍（不只我那两个探针脚本）：`cmake -S . -B /tmp/... && cmake --build --target ltool_portable` 过了，configure 打出「名单里那 15 个 TU」。
- 数字：离线判据 568 → **577** 条 0 红；装的那份 `--seamtest` 37/0、`--plugintest` 240/0、`--storenet` 16 过 1 红（又是「检查更新走网页 302」那条：GitHub 直连这台机器现在不通，走 9980 代理 0.83 秒就 302 —— 而商店清单那条走镜像是通的，`状态行` 里写着「走的是 镜像 gh-proxy.com」）。闭环 20.0s、22.11MB、`install MATCH A76D3BD0E3CB`，sync 后 19/19 与构建逐颗一致。
- 版本判断：**不升**（Windows 侧行为逐行相同，POSIX 侧还没有用户拿得到的产物；插件号也没动 —— 这轮改的是 core）。

## 第十刀：能进的先进来（可移植 TU 15 → 18，链接缺口仍是 3）

- 先量地图而不是猜：对 core 的每个 TU 跑 `g++ -fsyntax-only`，结果是"能编的 15 个 == 名单里的 15 个"，也就是说没有白捡的；然后按 Win32 触点给剩下 20 个排了个厚度（RegOpenKeyEx/HKEY 一堆的：RegClean 60 处、ShellMenu 12、StartupAudit 20、ConfigPage/EnvPath；只有 HANDLE/DWORD 几个的：DiskScan、AdBlocker）。
- **我第一版结论是错的，被 MSVC 打回来的**：三个 TU（ClipboardStore / InstallLogDialog / TaskScheduler）挂着 `#include <windows.h>` 却"grep 不到任何 Win32 入口"，我就写下"这是早期留下的空壳"。删掉 include 之后 GCC 那边过了、MSVC 当场报 `GetClipboardSequenceNumber 找不到标识符` 和 `CREATE_NO_WINDOW 未声明` —— 我的 token 名单是自己编的，不在名单里的 API 就等于"不存在"，这不算测量。真正的测法是**把 include 删掉、两个编译器各跑一遍**，只有一个编译器说话不算。
- 收的一道新缝：`Platform::runWithoutConsole(QProcess*)`（`platform/Launch.h`）。原来临时起的进程要藏控制台窗口，全项目写了 **8 遍**同一个三行 lambda（DevEnv 6、InstallLogDialog 1、TaskScheduler 1，外加 TaskScheduler 自己那个 `noWindow` 小别名）；现在 8 处都走缝，Windows 那份挂 CREATE_NO_WINDOW，POSIX 那份是**空动作**（这里没有控制台窗口可藏，不是降级）。黑窗弹没弹在无界面下量不到，所以判据只盯能量的一半：过了这道缝的进程照样起得来、输出读得到（`--seamtest` 37 → **38**）。
- 三颗 TU 真进名单：`AboutPage.cpp`（自己一个 Win32 入口都没有，卡的只是 `LTOOL_VERSION` 那条 `#error` —— CMake 那边这个宏本来就 PUBLIC 挂在 ltoolcore 上，portable 目标也得给同一个串，来源仍是 ltool.iss 一处）、`InstallLogDialog.cpp`、`TaskScheduler.cpp`（后两颗要的"不弹控制台"进了缝之后就没别的 Win32 了；计划任务其实是起 schtasks 进程，不是 COM）。
- `ClipboardStore.cpp` **留在名单外**：它靠 `GetClipboardSequenceNumber()` 判"剪贴板到底变了没有"（界面上那条"自己写的不算"就靠它，注释里写得很细 —— restore() 会同步发 dataChanged，序号是写完才变的）。POSIX 没有对等的计数器，硬做等于换一套机制、改行为 ⇒ 记成 #90 要拍形状的第五件。
- 探针跟着修了两处自己的失真：`tools/link_gap.sh` 以前不认名单里的**行尾注释**（我加 TU 时带了说明，awk 的"这行以 ) 结尾"判断被注释带偏，最后一颗整条漏掉，awk 25 / grep 26 —— 幸好那条独立核对还在，把解析漏洞当场抓出来了）；两边现在都先剥 `#` 后面的内容再数。另一处是 AboutPage 进名单后探针不给 `LTOOL_VERSION`，那 1 个 TU 的 `#error` 会被念成实现缺口 —— 和当初 moc / libarchive 是同一类错。
- 数字：可移植 TU core 15 → **18**（+7 道缝文件 = 探针里 25 个），`g++ -fsyntax-only` 全过、**真 CMake 在 WSL 上 `--target ltool_portable` 构建过了**；链接缺口仍是 **3**（`SevenZip::compress`、`ShellMenu::syncWithPlugin` 这两个要拍形状 + `SettingsDialog::staticMetaObject` 那个 TU 还没进名单）。POSIX 探针 69/0。Windows 侧离线判据 **578 条 0 红**（`--plugintest` 240、`--storetest` 73、`--seamtest` 38、cfgcheck 11、gridtest 64、pinyin 52、linuxdotest 44、cliptest 56+1 SKIP）。abi 仍缺 8（没删名字）。闭环 19.0s、22.11MB、`install MATCH FDF38B0E1BD6`，sync 后 19/19 与构建逐颗一致。
- 版本判断：**不升**。Windows 侧 8 处调用是同一句旗标应用搬进缝（进程照常起、输出照常读，578 条判据一字没变），POSIX 侧还没有用户拿得到的产物。

## 跨平台地图更新（2026-10-17 夜，第十刀之后量的，不是印象）

- 现在能编的 core TU 是 18 个，**正好等于名单里的 18 个** —— 也就是说"白捡的"已经捡完了，剩下 20 个每个都要动一次结构。
- 剩下 20 个按卡住的东西分类（每个都是 grep + 两个编译器实测出来的）：注册表那一族 5 个（EnvPath 最薄：只有 RegOpenKeyExW / RegSetValueExW / RegGetValueW；RegClean 60 处 Reg 系列最厚）；机制级要拍形状的 6 个（SevenZip 卡 bit7z、UsnIndex/FileIndexer/DiskScan 卡 USN-MFT 和 FindFirstFileExW 那套快速枚举、AdBlocker 卡 EnumWindows + DWM 取标题、ClipboardStore 卡 GetClipboardSequenceNumber）；纯界面的那几个（ConfigPage / SearchWindow / SettingsDialog / StartupAudit / Firewall / AppScanner / ShellMenu）各带注册表或 COM。
- 一处我读错的规格顺手改掉：`SettingsDialog` 的 showEvent 里除了 `SetWindowPos` 还有 `DwmSetWindowAttribute`（深色标题栏）和 `SetForegroundWindow` —— 只把 SetWindowPos 抽进缝**并不能**让这颗 TU 进名单，那条"消掉 staticMetaObject 缺口"的说法作废。
- 干净的 on/off 置顶其实只剩一处（`PinWindow::applyTopmost`，旗标是 NOMOVE|NOSIZE|NOACTIVATE）；`CaptureOverlay` 那颗和抢焦点那一整套（按 Alt、接前台线程输入队列、重试、写日志）缠在一起，不能跟着一起搬 —— 所以"setTopMost 那道缝"这一刀的价值只是少抄一遍，不推名单，先排在 EnvPath 后面。
- 下一刀定在 **EnvPath**：Windows 那份逐字保住行为，POSIX 那份有真对等做法（`~/.config/environment.d` 那套 + shell rc 追加），做完 core 可移植 TU 18 → 19。它不欠任何人拍板。

## 「Windows 用户名」这页：模态框被隐藏的启动器吞了（2026-10-17 下午，他报"改名失败 + 页面太丑"）

- 他报的两件事里，"失败"这条是有证据的：**整份 log.txt 里 `username:` 一条都没有**（同期 `linuxdo:` 307 行、`clipboard:` 534 行 ⇒ 插件写日志的通路正常），而他答问卷时说的是「没弹授权窗就失败」⇒ 三颗按钮每次都在**提权之前**就 return 了。
- 根因是那颗**模态确认框**：这页是从启动器里叫出来的，而启动器一失焦就把自己藏起来（Alt+Space 那套宽限/最小化判据改过几次）——挂在它里面的 `QMessageBox::question` 跟着看不见，Esc/超时都算「不是 Yes」，于是哪儿也没去，界面还停在原来那句说明文字上（他读成"显示修改成功"）。
- 形状按他挑的**方案 B**重做：单列到底、动作一律在行尾（摘要行 + 重读、显示名 + 改、账户名 + 改、一条分隔、新账户三个输入 + 建、管理员勾选），**说明/结果那一行挪到所有动作行下面并常驻** —— 它会换文字、可能从一行变两行，摆在上面一按就把输入框往下推（他否过"点一下突然多出一块"），摆在底下上面那颗都不动。
- 确认改成**就地两段**：第一下把同一颗按钮变成「确认改成这个」并亮 6 秒，第二下才动手；点另一颗会把前一颗收回（同时只有一颗在武装）；校验没过也写在同一行，不再弹任何框（`QMessageBox` 的 include 已从这两个文件消失）。
- 判据 27 → **36**：四个动作都在行尾（数每行 QHBoxLayout 的最后一项）、摘要行一行摆完、点第一下按钮换了字、`activeModalWidget()` 仍然是空的、**第一下不许把上面任何一颗挪走**（比三行的 y 坐标）、只留一颗武装、收尾那条写明"安全靠结构不靠这条断言"。几何断言两个坑都踩过才修对：① 页面没排开时三个 y 全是 0 ⇒ 加了一条「三行的 y 必须是互不相同的正数」（实测 39/109/139）；② 点完没重排就读坐标 ⇒ 等于拿旧几何比，改成 `invalidate()+activate()+processEvents()` 十轮。
- **我自己造成的两件事，都记在这**：① 上面那条几何判据我第一版是"永远相等"的空转绿，是拿"点一下插一块进去"的破坏试验照出来的（第一次破坏试验还因为我的 sed 没接上 `addWidget(m_hint)`，变成"说明行根本不在布局里"这种假验证）；② **判据里我对同一颗按钮点了第二下 —— 第二下就是"确认"，于是真的对他账户发起了一次「改显示名」**（16:00:28 与 16:01:08 两条 `username: 改显示名 ok=0 ... 退出码不是 0（1）` 都是我）。他的账户**没有被改动**（`Get-LocalUser` 的 `FullName` 现在仍是空，和之前一致），但那是一次不该发生的提权尝试。已把收尾改成"点另一颗来收回"，并在代码里写死这条规矩：**测试里绝不对同一颗两段确认按钮点第二下**。
- 还欠一条（没做，已开任务）：提权失败现在只能说「退出码不是 0（1）」——真实原因在提权那个进程的 stderr 里，被 `ShellExecuteEx` 吃掉了。要把它带回界面得让脚本把 `$_.Exception.Message` 写进一个临时文件（路径走环境变量传，别拼进脚本文本），跑完读回来。
- 数字：`--username` 36/0，离线判据 578 → **614 条 0 红**（578 那半不含 username；622 是我加错过的一次，已按各旗标实测数重算：240+73+38+11+64+52+44+56+36）（plugintest 240 / storetest 73 / seamtest 38 / cfgcheck 11 / gridtest 64 / pinyin 52 / linuxdotest 44 / cliptest 56 / username 36）；这颗插件按新规矩 1.0.1 → **1.0.2**，闸绿；`install MATCH FDF38B0E1BD6`（exe 与上一轮同哈希，改的只有插件），sync 后 19/19 与构建逐颗一致。core 的号不升（1.1.0 仍未发布），这轮是**用户可感知的新形状**，发版时要写进 release note。

## 磁盘清理·第一刀：把 light-c 的地基搬进来（MFT 直读），顺手抓到深索引一条从没生效的分支（2026-10-17 晚）

- 来源是他给的 `Chunyu33/light-c`（Rust+Tauri 的"高性能 C 盘清理"）。它的 `engine_selector.rs` 三条决定照抄了：`提权 && NTFS` 才走 MFT、MFT 失败**自动**降级遍历、**并且把这次实际用了哪条路显示给界面看**。
- 我们没有平行实现第二套枚举：`UsnIndex::enumVolumeMft`（深索引在用）已经给了"谁在谁下面 + 叫什么"。**缺的只有大小** —— USN 记录里不带大小，它在每条 FILE 记录的 `$STANDARD_INFORMATION` 里，所以新增 `UsnIndex::mftSizes()`：把 `$MFT` 顺序读一遍，按记录号 head/next 链表一次填完（硬链接多条名字各拿一份，和遍历口径一致）。
- **抓到一条真 bug**：`openVolume` 拼的是 `\.\C:\`（结尾多一个反斜杠）。实测四种写法：`\.\C:` → err **5 ACCESS_DENIED**（形式对，只差提权）；`\.\C:\` → err **3 PATH_NOT_FOUND**。也就是说**深索引那条「MFT 优先」从来没生效过**，一直在走遍历回退 —— 它日志里那句 `volume open failed ... 回退目录遍历` 就是这个。判据里钉了一条"打不开卷的原因不能是错误码 3"。
- `$MFT` 的打开形式也量过：`\.\C:$MFT` → err 2（文件不存在），`C:\$MFT` → err 5（只差提权）。别拿裸设备路径去拼文件名。
- 记录长度不靠猜 `$Boot` 的偏移（那格有版本差异）：拿 `FILE` 签名在 256/512/1024/2048/4096 里找周期，连续四格都对上才算数。
- 两条扫描路共用同一颗尾巴 `finishTree()`（自下而上求和 → 孩子表 → 每层按大小排序剪枝）。分开的理由写在函数头上：树的形状一旦按引擎不同，界面上的排行和下钻就会表现不同。
- 提权的形状：ltool 是启动器，平时**不该**提权跑。所以「直读 MFT 重扫」起一个提权的自己执行 `ltool.exe --mftree <盘> <文件>` —— 它只做一件事：读、把树写成二进制快照、退出（0=好了 / 2=这个卷走不了 MFT / 1=别的失败）；界面读回来之后把那个临时文件删掉（只删算出来的那一个绝对路径）。快照带魔数 + 版本号，对不上整份丢掉不猜。
- 界面说实话：定稿状态行点名"这次走的是 MFT 直读还是常规遍历"，回退还把**为什么没走快的**一起带上；MFT 途中的进度句子换成"已枚举多少条记录"，不拿"已统计多少个目录"冒充另一条路的数字。
- `--mfttest` 从"一行断言都没有的诊断输出"改成真判据：本机（非提权）**11 条 = 10 绿 + 1 条点名 SKIP**；提权机器上再多跑 7 条（枚举拿到多少记录、$MFT 大小读不读得出来、合计像不像真盘、这次走的是哪条路、整盘树、与文件系统已用量的量级对账、System32 逐目录 ±10% 对账）。
- CI 里 `--mfttest` 从"要管理员，跳过"挪进真跑那一串：runner 是提权的（`--username` 早就读到过 `runneradmin@…`），所以那两条对账判据在 CI 里有真对象。
- 数字：`--mfttest` 本机 11 条（10 绿 + 1 SKIP）、`--diskui` 8/0、`--plugintest` 240/0（改 Desc 时它当场判红了一次：插件自报描述和 plugin.json 必须一字不差，这条闸是有用的）；那九串离线判据 614 → **625**。本机 `mftUsable(C:/)=0 why=[没有管理员权限，打不开卷（错误码 5）]` ⇒ **MFT 那条路本机的实测耗时还欠着**，要么他给一次 UAC，要么看 CI。
- 连带账：`Tree` 多了 engine/engineNote、`Progress` 多了 engine、core 多导出 `mftUsable/writeTree/readTree/engineMft/engineWalk` ⇒ 已发布的那颗 `ltool-disk.dll` 和这版 core **ABI 对不上**（#91/#78 那笔"商店产物要重推"的老账又多一处）。disk 这颗 1.0.0 → **1.0.1**；core 的号不升（1.1.0 还没发布，同批功能吸进同一个号）。
- 下一刀（#103）：清理位补齐 + 按类别分组（回收站现在根本没进清单，而他树上 `C:\$Recycle.Bin` 是 5.19 GB）。
- **CI 第一次跑就红了，红的不是判据是预算**（run 39026813012，`--mfttest` exit **124** = timeout）：runner 确实是提权的（日志第一行 `INFO mftUsable(C:/)=1`），于是我给的那条"整盘再走一遍遍历对账"在 VM 的盘上一分多钟，把 60 秒的步长预算吃穿了。改法两处：① 逐目录对账**只走一个子树**（`C:\Windows\System32`），整盘那遍遍历砍掉 —— 它证明的是同一件事（$MFT 的大小解析对不对），代价从一分多钟降到几秒；② 补一条不花时间的**第三条独立量法**：MFT 整盘合计 vs 文件系统自己报的"已用"，只抓"差一个量级"（口径天生不同：簇分配量 vs 逻辑大小，compact OS 的压缩文件会让逻辑明显更大，所以容差给的是 /2 ~ ×3，不是 ±10%）；③ `--mfttest` 单独给 240 秒。教训记一条：**判据里凡是要真扫整盘的，先量它在最慢的那台机器（CI 的 VM）上要多久**，本机几百毫秒不等于 CI 几百毫秒。
- 装上心跳之后第三次 CI 自己交代了（run 39029141013，日志原文照抄）：`STAGE 枚举到 1669141 条记录（累计 7250ms）` → `STAGE 1398724 个文件要去 $MFT 里问大小` → `STAGE MFT 直读没走通（$MFT 大小没读出来（错误码 5）），回退常规遍历` → 然后 240 秒到死。**三件事一次分清**：① `enumVolumeMft` 是好的（166 万条 / 7.25 秒，这条函数在提权下第一次真跑）；② 卡的是 `C:\$MFT` 这个写法 —— 提权的 runner 上枚举卷能过、开 `$MFT` 却给 ACCESS_DENIED，说明它走的是普通路径名查找，得换 `\.\C:\$MFT`（卷设备那颗，取证工具普遍用的写法；本机非提权下两种都是 5，形式都对，分不出来）；③ **真正烧掉 240 秒的是回退之后那趟遍历** —— CI 那台 VM 上整盘遍历比我们这台 NVMe 上的 14~19 秒慢一个数量级还不止。
- 于是判据的形状也跟着改了：**先把 MFT 那两步单独量、单独判**（枚举出多少条 / 大小读不读得出来 / 合计像不像真盘），两步有一步没成就**不去跑整盘**（点名 SKIP 那三条），因为那个跑法必然掉进四分钟的遍历。教训：**判据不许在"实现失败"这条分支上顺手触发一个更慢的兜底** —— 那等于把一条本该三秒判红的红，变成一次四分钟的超时。
- **追到第九轮才看清的墙**：`$MFT` 在现代 Windows 上**连提权的 Administrators 都不给按名字开**。CI 那台 runner 实测：`\.\C:\$MFT` = 5、`C:\$MFT` = 5、`\.\C:$MFT` = 2，加上 `FILE_FLAG_BACKUP_SEMANTICS`（`SeBackupPrivilege` 确实开上了）再跑一遍**还是同样的三个数**；按记录号开（`NtCreateFile` + `FILE_OPEN_BY_FILE_ID`）拿不到序列号 —— 因为 `FSCTL_ENUM_USN_DATA` **根本不返回 `$MFT` 这条元数据记录**（实测 `mftFrn=0`）。于是加了第三条路：卷句柄按簇裸读 —— 引导扇区里 `$MFT` 起始簇那格偏移各家文档不一致（读 0x30 得到"记录长度 1007616"这种鬼话），改成多候选偏移 + 只信 FILE 签名周期之后**起点找到了（簇 786432，周期验过）**、记录 1 里的非驻留 `$DATA` 也找到了，**卡在盘区段（run list）解出来的首簇对不上起点** —— 下一轮的失败原因里会带那 128 字节属性头，看一眼就能手算出正确的 run-list 偏移，不必再拿 CI 去猜。
- 判据跟着定的规矩：**环境不让读不等于产品坏了**。所以"读不到 `$MFT`"这一档改成**点名 SKIP + 每轮把整条原因印出来**，而"枚举拿得到记录"（真红过就完蛋的那条）和"回退出数 + 界面说出为什么慢"仍然是硬判据。反过来也不许它静默腐烂：SKIP 那行明确写了跳的是哪四条。
- **修好卷路径的连带后果**：`--smoke` 会重建深索引，而深索引一直是"MFT 优先、开不了卷就回退遍历" —— 那句 `\.\C:\` 修对之后**这条优先路径第一次真的跑了**。在 CI 那台 VM 上光 `FSCTL_ENUM_USN_DATA` 枚举 167 万条就要 25~45 秒（同一台机器同一份代码，三轮分别量到 4.5 / 22.7 / 25 / 44.7 秒 —— 波动极大），60 秒的步长预算当场被吃穿（run 39038920886：`--mfttest` 这次 0 红，红的变成 `--smoke` exit 124）。给它和 `--mfttest` 一样 240 秒。**对他本机没有影响**：ltool 平时不提权，开不了卷，走的还是原来那条回退遍历；只有提权跑的时候深索引才会换到 MFT 这一路。
- 盘区段那一格也量到手了：记录 1 里 0x108 那颗属性头 128 字节原样印出来，看到的是"非驻留 $DATA、NameOffset=0x40、名字长 0、值长 0、属性大小 0x1000"，后面 0x40 那格是 `ffffffff...` —— 也就是说**那颗不是 $MFT 的主 $DATA**（值长为 0，而且 0x150 还有一颗一模一样的 $DATA），记录 1 装的是 `$ATTRIBUTE_LIST` 那套扩展结构。要按簇读通 $MFT，得先认 $ATTRIBUTE_LIST、再去扩展记录里找真正带盘区段的那颗 $DATA —— 那是另一件活，不欠这一刀。

## 置顶截图：拖边缩放时图会漂（2026-10-17 深夜，他一句"缩放大小拖动的时候位置不要变"）

- 先分清是哪一层在动。合成一次"按住右下角拖 +60/+40"：**窗口左上角一字没动**（那条判据当场绿），**图区左上角从 184,0 漂到 182,0** —— 漂的是图，不是窗。
- 根因就一行：`PinWindow::imageRect()` 把图**居中**在客户区里。于是拖任意一条边，长宽各分掉一点余量，图就往反方向跳 —— 屏幕上看着就是"一缩放就跑位"。改成**钉在客户区左上**：拖哪条边都只改大小，图的左上角一动不动。
- 顺带把当年那个"半格"坑一起变简单了：居中要用整除自己算，因为 `area.center()` 会往前偏半像素，四舍五入后图的左沿可能落到历史条那一列（点在图上却换成了图）；现在左沿恒等于 `stripW()`，那个条件不可能成立。
- 代价说清楚：往期带 + 工具栏都展开、而图比窗口窄的时候，空白全堆在**右边和下边**（以前是两边各一半）。默认状态（两样都收起、窗口抱住图）一点没变 —— 那条"两样都收起：窗口正好等于图本身"照绿。
- 判据 +6 条（`--pintool` 实测 23 → **29 条 0 红**）：拖右下角之后窗口左上角不变、**图左上角不变**、图确实变大了（防"没生效所以不动"的假绿）、窗口正好 +60/+40；拖左边那条是反向验 —— 窗口左上角**该**跟着走（应有行为），但图必须仍然贴着客户区左上。
- 复测过的邻居：`--capgeo` 0 红、`--plugintest` 240/0、`--pinpick` 拿真夹具（`build/verify/picktest/text.png`）0 红 —— 中途我差点拿一张旧截图当夹具跑它，红了 6 条，那是**我喂错了图**不是回归（判据认的是那三行写在已知坐标上的字）。
