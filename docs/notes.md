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
- **每个旗标都必须有输出，空输出算失败**：这套自测里 `--smoke` 曾以"0 字节 + 退出码 0"的形状哑过（命令行被单实例守卫转交给已常驻的实例），CI 这边直接把它判成红。CI 跑的是 `--smoke --plugintest --rowmenu --username --about --cfgcheck --appdirs --pinyin --diskbranch --archivetest`；**跳过的点名写进日志**：`--capgeo --capflash --recq --rectest --recordui`（要真屏幕/DWM 会话）、`--mfttest --searchtime`（要管理员开 USN），别让"CI 绿"看着像"这些也过了"。
- **发出去的包走 `-DFINAL`**（本机同一棵树实测：normal 22.51MiB / 6.8 秒，FINAL 21.79MiB / 10.3 秒 —— 省 0.72MB 多花 3.5 秒；本机的 `deliver.ps1` 不带它，因为每轮闭环少 3.5 秒是天天摸得到的）。开关用**短横不用斜杠**：那一步是 bash，MSYS 会把 `/DFINAL` 当路径转换掉，ISCC 于是报 `You may not specify more than one script filename`（本机实测撞过）。
- **发一版就三步**：① 改 `ltool.iss` 的 `MyAppVersion`（要预发就写 `x.y.z-beta.N`）；② 提交并推 `main`（这一步之后 workflow 文件才在默认分支上，**标签早于 workflow 推上去是不会触发的**）；③ `git tag v<和号一模一样的串>` 再 `git push origin v<那个串>`。第 ③ 步是对外的，由人按下。

## 自测旗标逐条（每面旗量到的数、期望值为什么改过）

## 自测旗标逐条

上面那张表只写"这面旗标验什么"。下面这段是前一版 README 里「命令行参数」那**一整条**，
原样搬过来的 —— 每一面旗标量到的数、期望值为什么改过、反向验怎么做到的，都在里面。

`--show` 启动即显示搜索框；`--smoke` 无界面自检；`--mfttest` 需管理员，诊断 MFT 枚举；`--searchtime 词1 词2` 无界面，加载索引后逐词打印深索引搜索耗时与前 3 条结果（**272 万条下现在 9~38ms/次**：两个字的词 9ms、`chrome` 38ms。改之前是 540~680ms —— 每命中一条就 `fromUtf8` 造一条整路径 QString 收进一个大数组，而绝大多数命中根本活不到前 20 名。现在按 (来源, 分数, 名字长度, 路径) 分 8 个桶、每桶只留 2×limit 条候选，比大小时不造 QString（UTF-8 的字节序就是 BMP 字符的 UTF-16 序，只有含 emoji 那类 4 字节序列才退回 QString 比），首字节用 memchr 跳，每个卷的条目区间切成段丢给最多 8 个线程各扫各的再并桶）；`--searchdump 词1 词2` 无界面，把每个词的前 20 条结果**按顺序整条打出来** —— 改搜索实现最怕「快了但顺序变了」，所以这条就是改前改后各跑一遍、拿 diff 当判据的入口（2026-10-12 那次重做：同一份实现连跑三遍互相比对一致、和老实现逐行一致，只有索引条数那一行随磁盘变化）；`--disktime` 无界面，A/B 对比两种磁盘遍历（`DiskScan` 一次建树 vs `QDirIterator`）在 C 盘上的耗时并列出前 5 大目录，还会把 `DiskScan` 自己那三段（枚举 / 合并 / 排序）连同文件数、目录数一起打出来 —— 加这三段是因为第一次拿它得出了「新遍历比老的慢 0.7×」这个**假结论**：两趟共用一个进程，先跑的那趟把目录项喂进了系统缓存，比的其实是缓存冷热不是实现高低；改成冷/冷、热/热各一轮之后实测 **DiskScan 14.5s（热）/ 20.2s（冷）vs QDirIterator 26.3s（热）/ 29.4s（冷），热态 1.8×**（这台机器 48.1 万个目录 / 142 万个文件 / 254.5GB；三段拆分里"扫盘"占 20198/20244 ms，合并建树 31ms、排序 5ms —— 时间全在枚举上，建树和排序根本不是瓶颈，所以优化方向不在那儿）；`--diskscale` 无界面，拿环境变量 `LTOOL_DISKTHREADS` 把同一棵树按 1/2/4/8/16 线程各扫一遍量扩展性（**连跑两遍**：1 线程 22.0 / 22.9 秒，2~16 线程落在 12.9~17.6 秒之间，第二遍里 4/8/16 的顺序还会互换 —— 也就是加线程最多换到 1.5× 上下、且不稳，目录枚举在这台机器上早就不吃 CPU 了；上一版这里写的「1→4 线程 3.2×」拿第二遍复测**没复现**，已按实测改掉）。结论：**加线程救不了首扫**，所以「磁盘分析太慢」能做的两件事是别重复扫、边扫边出数）；`--diskbranch` 无界面，验「边扫边报的一级分支大小」和最后那棵树是不是同一回事（4 条：分支数齐、每个分支报的数和定稿树逐个对上、一级分支之和 = 全盘减去卷根直属、第一个分支在整趟结束之前就出数 —— 实测 20 个分支 206ms 就出第一个、整趟 13.1s）：这条必须能红，孩子算漏或算两遍都会被第一条抓到，而界面上先给用户看的那几个数就是它报的；`--diskui` 无界面（慢，要真扫一整盘，所以不进 `--plugintest`），把磁盘页那条链整条走一遍（7 条，判据写在插件 `DiskPage.cpp` 里 —— 它要点真按钮、看真行卡片，主程序链接不到那些类型）：控件都在 → 点「整理」→ **途中状态行报了「一级分支已扫完 N / 20」且列表已经摆了行卡片**（实测 1.56s 摆出 13 行，不是空白干等到 16s）→ 定稿后状态换成树上的数、并写下这次扫描的时间 → 换到没扫过的盘列表让位（不拿上一个盘的数当结论）→ 换回来**直接用缓存**把行摆出来（没重扫）；`--username` 无界面验「Windows 用户名」那一页（30 条，**一次都不真的改账户/建账户** —— 那是不可逆级别的动作）：`psQuote` 把单引号翻倍（拿一条注入串试：`x'; Add-LocalGroupMember ...` 拼出来必须首尾各一个引号、里面没有裸单引号）；名字合法性（空/带斜杠/带引号/带换行/账户名 21 字 全被拒，20 字和中文显示名通过）；拼出去的脚本长什么样 —— ADSI 那串路径必须是**拼出来的** `"WinNT://" + '机' + '/' + '号' + ',user'`（整串塞进双引号里再套单引号，单引号就成了名字的一部分，实测错过一次）、新建用户按 SID 找管理员组而不是写死组名、不勾管理员就**整段不加**；真实读取（只读）：当前账户名非空、SID 以 `S-1-` 开头、用户目录真在盘上、查一个不存在的账户得回 `exists=false`；页面三颗按钮**从布局可达**（不是 `findChild` 数得到就算）；再加工具页结构判据：`embedded>=0` 的槽位互不重复且是 0..N-1 连着（`ToolsPage` 的占位数正好盖住，卡片指向一个没建的页就会被这条抓到）。脚本里那 15 个 cmdlet 参数名（`-PasswordNeverExpires`/`-AccountNeverExpires`/`-NewName`…）是拿 `build/verify/psparam.ps1` 在本机 `Get-Command` 逐个验过的，不是凭记忆写的；`--rowmenu` 无界面，验磁盘页两栏列表（「可清理位置」和「占用分析 · 本层目录」共用同一行卡片）右键该弹什么：存在的目录只有一项、叫「打开文件夹」、点得动，且**这项的 data 就是这一行自己的目录**（点开之后界面拿的就是它，所以这条等于验“开的是它、不是别的”）；目录已经不在了就只剩一项灰的、写「这个目录已经不在了」；再加两条几何/结构断言（行卡片的 contextMenuPolicy 是 CustomContextMenu，下面那行路径是它自己的子控件 —— 右键点在路径那行上也要冒得到）。判据写在 DiskPage.cpp 里（行卡片藏在那个文件的匿名 namespace），菜单构建拆成了独立的 `makePathMenu`，因为真按一下就会拉起资源管理器，那一下不能放进自测；`--archivetest extract|make|probe|progress` 无界面跑压缩包读写（回归用，见上；`make` 的第 5 个参数是 7z 档位数字 0-9，zip 那条不吃；`progress <输入> <输出> [7z] [第几次按取消]` 是专门验进度与取消这两条只有界面在用的线的：分母准不准、done 单调不减、回调返回 false 之后有没有当场停下、半成品 `.part` 有没有被删干净）；`--gridtest` 无界面验「图片排版」那张板（34 条，判据在 plugins/grid/gridselftest.cpp）：格数只截断不重排（九宫摆满退回四宫还是那四张）、四格收四张其余算拒收、后缀不是图或盘上没有就一个都不收、拖到空格=挪过去、拖到有图的格=交换、空格拖不动、✕ 只空那一格；合成验到像素（150x150 = 60*2+10*3、四边和缝是背景色、每格中心就是那张图的颜色、空格子是背景色不是黑的）；**板上预览和出图必须同一块构图** —— 拿一张 120x40 的红|绿|蓝横条，中间那条正好 40 见方，整张压扁就会漏出红和蓝（compose 两条 + 离屏 grab() 两条），反向验过：把居中裁正方形换成整张映射，这四条当场点名；再加两条几何（板子再扁格子也是正方形、整块居中而且真留了白 —— 只断言左右对称的话两种写法都绿，所以量的是留白有多大）和一条探针自检（✕ 角标必须比图本身暗，防止离屏 grab 画出空图把全部像素判据一起骗绿）；坏文件那三条验的是界面说实话：后缀对的坏图先进格子（人得看到自己拖错了）、合成时那一格留背景色、画得和空格子一样素（不是一块黑）；`--gridtest` 这一轮又加 5 条盯「生成宫图先弹框展示效果图」：弹框里「保存这张 / 取消（不存）」两颗都在布局里、那行字 == `previewCaption`（写的是**存出去的全尺寸**和落点目录，不是屏上这块缩图）、缩图真有像素、按「取消」这个框回绝；预览**只缩不放大**（图本来就小就按原尺寸摆，放大出来的糊图会让人以为存出去的也是糊的）。顺序那条和它的反向验见上面「自测旗标逐条」的 `--gridtest`；`--linuxdotest` 无界面验「linux.do 盯帖」离线那半条（33 条，判据在 plugins/linuxdo/linuxdoselftest.cpp，跑在沙箱 APPDATA 里因为它要写 linuxdo/* 那一节）：解析（id 乱序不重排、没标题那条丢掉、URL 按 id 算、Cloudflare 那张 HTML 挑战页解出 0 条、空字节不崩）、命中（标题撞 / 只有标签撞 / 撞两个词只算一条 / 上一轮报过的不再报 / 空词不把全部帖子都算命中）、前缀拼法（带不带尾斜杠、两边带空格、留空=只用直连）、要不要弹那一句的决定（第一次只播种不弹、没命中不弹）、配置（默认开着 / 10 分钟 / 三个词 / 中转前缀就是实测那条；0 分钟夹到 3、9999 夹到 240、关键词去空去重 trim、三样都真落盘、再建一张页回填的就是盘上那份）、页面九件控件**从布局可达**（数 findChild 不等于摆在页面上）。反向验过：把 plugin.json 的 `Activate` 摘掉，`--plugintest` 那条「牌和导出的符号一致」当场红成 `got=[0/1]`。`--linuxdonet` 是另一面旗（6 条，要联网、不进 CI）：真去取一趟，实测 4954ms 拿到 30 条、状态行说出走的是中转、第一趟只播种。**这条路是量出来的不是猜的**：直连 linux.do 在这台机器上 DNS 被污染（解到 Facebook 的 IP），走系统代理能到 Cloudflare 但非浏览器客户端一律吃挑战页 —— **带着浏览器导出的 cf_clearance + 完整 Chrome 请求头也一样**（它绑 TLS 指纹），`latest.json` / `session/current.json` / 单个帖子三条实测全 403，所以给 cookie 也换不来登录态；只有 `https://r.jina.ai/<原地址>` + `X-Return-Format: text` 能拿到真 JSON（全站最新 30 条、categories 12 个分类、单个分类流 30 条，4.5~7.3 秒）。发帖速率实测 0.64 帖/分钟（一页 30 条跨 47 分钟），所以 5~30 分钟一轮漏不掉，间隔下限卡 3 分钟；`--pinyin` 无界面验拼音匹配（39 条断言）：表和码点必须对齐（错一位就会把"垃圾"当"任务"）、多音字（重庆 chongqing/zhongqing 都算）、全拼允许中段而首字母只认开头、纯英文名不掺和这档、单字母不启用、耗时（810 个名字首次展开 979us、缓存后扫全表 79us/次），**最后真建一个不显示的启动器窗口往输入框里打字**，验 `mumumoniqi` 能搜到本机的"MuMu模拟器"、`cipanqingli`/`cpql` 能直达"磁盘清理"，再加 `renwu` 那两条（**应用节必须在**、**工具直达那一节不能把应用顶掉**）和 `qingli` 一条（取整不伤直达：它不是「磁盘清理」的开头，但是关键词「清理」的开头），再加 `qq` 两条 —— 半角 `qq` 必须出应用节（这台机器上 `apps.json` 里 `QQ` 排第 1，复算过排序不是问题）、**全角 `ＱＱ` 必须出同一个应用节**（这条第一次跑是红的：偏移写成 `0xFEE` 把 `Ｑ` 变成了 `3`，端到端断言当场抓到，所以没再单加一条纯函数断言）；顺带给 harness 加了一个把结果里出现的小节原样打出来的探针，查「某个词为什么搜不到 X」时看它比看断言红不红有用；`--archspeed [每文件KB] [文件数]` 无界面量压缩包读写的吞吐（默认 4096×15=60MB 伪日志，另可 `--archspeed 2 2000` 量海量小文件）：我们 vs 系统里的 7-Zip，zip/7z 两个方向各打一行 MB/s 和产物大小，并把 7z 的 1/3/5/7/9 档整条曲线跑一遍，**每档都验"我们自己的读取器解得回来 + `7z t` 退出码 0"**（换档位最怕快是快了、别人打不开），夹具全在 `%TEMP%\ltool-archspeed`；`--ocrtest <图片>` 无界面打印图里认出的文字与耗时；`--ocrlayout <图片>` 无界面打印每行每词的包围盒和切出来的字格（划词的坐标就靠它验）；`--pinpick <图片>` 无界面，把鼠标事件直接送给一个不显示的置顶窗，跑「图上划词」的 9 条断言（拖半行/单击整词/跨行/词内半个字/跨词空格/Enter 全部/Esc 退出），跑完归还剪贴板；`--recprobe [ms]` 无界面量录屏的地基：系统里有哪些视频/音频编码器、MP4/H.264 写 30 帧再读回来要多久、`Windows.Graphics.Capture` 按窗口取帧能不能用（会弹一张自己的小窗 1 秒多）；`--recq` 无界面验录屏那两个新选择真的进了编码器（13 条）：`Recorder::encodedSize` 的五种画幅（不缩 / 等比缩 / 奇数先取偶再缩 / **只缩不放大** / 结果永远偶数）、码率随画幅下降，然后**拿真编码器录四局自制卡**并 `inspectMp4` 读回来比：不缩那局文件画幅 = 640x400，选 ≤320 那局文件画幅真是 320x200 且**亮度网格仍然上亮下暗**（缩小不许把画面翻过来），同内容同长度缩小那局文件必须更小；画质两局用**逐像素随机噪声**（第一版用的是 24x12 大块，放大后太平滑，流畅和超清只差 7% —— 那条断言测的是内容不是档位，所以改成噪声并要求"至少大 10%"，实测 +26%~37% 两遍稳定）；`--rectest [秒]` 无界面录一个自造的会动的卡片，然后 `inspectMp4` 把文件解回来数帧（帧数/时长/有没有音轨/画幅），**末尾再录 1.5 秒整块屏幕**（`CreateForMonitor` 那一路：能开起来 / 采到帧 / 全程无错 / 收尾后 `running()` 回 false / **解出来的画幅正好等于这块屏幕的物理尺寸取偶** / 时长对得上），画面变化靠的就是那张每 30ms 换数字的卡，不合成鼠标；`--recordui [秒]` 把 F2 那条链整条走一遍（45 条断言，2026-10-14 加了「画质/画面」两颗下拉框那 7 条，2026-10-15 又加 12 条盯选择窗这三件事：「开始录制」那颗存在且摆在布局里、**单击和双击条目都不再开录**（合成一对鼠标事件送给列表的 viewport，数 `picked` 发过几次）、按那颗才 emit 且 emit 的就是当前选中那条、**按住标题那一行能把窗拖走**（sendEvent 给这个窗自己，pos 必须正好挪过 (60,40)）、**选中一条就真拿到 WGC 帧**（指着那张每 30ms 换数字的自造卡测，它一直在重绘所以这条不飘）、预览尺寸 == `Recorder::encodedSize(采集, 当前档)`、换「画面」档之后预览跟着换成 ≤1280 的画幅（拿整块屏幕量，那张卡只有 360 宽任何档都不缩）、窗一收起就把取帧那一路放掉。选择窗自己用 `WA_DontShowOnScreen` 摆成"可见"——不往他桌面上贴像素，但布局和 isVisible 全走真流程。反向验过：把三处实现逐个掐掉（重新接上 itemActivated / 拖动的 press 短路 / refreshPreview 直接 return），红的正是那 6 条，一条不多一条不少）：在不在布局里、四档齐不齐、**当前档 == 配置里存的档**、程序化改档之后立刻落盘、**再建一个选择窗回填成刚选那两档**、跑完把用户原来的档还回去）：列表第一条必须是「整个屏幕」且带着“这是屏幕不是窗口”的标志位、第二条是普通窗口 → 悬浮条在计数 → 用 `click()` 程序化按「停止」→ 报已保存 → 文件能解出帧 → **再开一局按「不要了」：条上写“已取消，没保存（丢掉 N 帧）”，并且断言 `QFile::exists` 说那个 MP4 真的不在磁盘上了**（判的是文件，不是它自己写的那句话）→ **选择窗的四条退路**（「✕」那条是 `findChild` + **布局可达性** + `click()` 三步：
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
- 搜索框里工具那一节按名字/关键词/拼音匹配，**命中只认开头**（`Pinyin::matchStart`）且绝不独占整页
  —— 否则搜 `renwu` 想要任务管理器会被「开机启动分析」的关键词"计划任务"顶掉。
- **但"整条查询就是这个词"时它确实会 `return` 独占，这一条曾经把网站那一节吃掉**（2026-10-15 他报的
  `gg` 搜不到 Google）：「广告弹窗」的首字母正好是 `gg`，工具直达先命中就整个函数提前返回，
  后面「网站关键字 / 网址直开」那段根本没机会跑。现在的规矩是**显式配在网站上的关键字 > 工具名首字母拼音**
  （`webKeywordWins` 为真时工具那一节让路，不独占也不追加）；`gg` 出 Google、`gg ltool` 多出「在 Google 搜索」那一行，
  而广告弹窗还有 `ggdc`（整串首字母）和 `广告`（关键词松散命中）两条路 —— 三条都钉在 `--pinyin` 里，
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
自测入口 `ltool.exe --plugintest`（202 条，全程离屏、一次都不 `show()`）：十八个插件逐个验
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

输入即搜应用（前缀 > 词首 > **拼音全拼** > **拼音首字母** > 包含，按使用频率排序；**使用次数的键**：有快捷方式记 `lnkPath`、没有的记 `target` —— 以前直接用 `lnkPath`，而目录扫来的那些没有 lnk，**全共用同一个空键**（这台机器涨到 43 次），于是 QQEX 靠别人家的 43 次排在 QQ 前面；那个空键读的时候直接丢掉，它没法归属到具体应用）；**输入先归一：全角折半角**（`ＱＱ` = `qq`，偏移 `0xFEE0`，全角空格 U+3000 也一起收）—— 中文输入法切到全角时打出来的根本不是 ASCII，索引里明明有 QQ 却一行都搜不出来（2026-10-12 他报的）；同一条链上还验 `renwu` 那种直达抢页的情况，判据见下面 `--pinyin`；**`renwu` 出"任务/人物"，`rw` 也出**（多音字每个读音都算：`chongqing` 和 `zhongqing` 都能搜到"重庆"），工具直达同样认拼音（`cipanqingli` / `cpql` → 磁盘清理），**但直达那一档必须从开头命中**（`Pinyin::matchStart`）—— 中段命中只是“名字里正好含这几个字母”，拿它独占整页结果会把人要的那个顶掉：实测踩过搜 `renwu` 想要任务管理器，结果被「开机启动分析」的关键词“计划任务”（jihua+renwu）当成直达，而直达是 `return`，应用那一节整个没了。应用那一档仍然允许中段（打 `guanli` 要能出任务管理器）；表由 `tools/genpinyin.py` 从 pypinyin 的字典生成（2.6 万字，140KB，编进 exe）；文件索引不做拼音（270 万条现算会把 20ms 拖成秒级，要做得预建索引）；同时搜文件（见下）；网址/网站关键字直达浏览器

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

## 自测旗标一览（37 面，全表）

| 旗标 | 一句话 |
| --- | --- |
| `--show` | 启动即把搜索框显示出来，不是只进托盘。 |
| `--smoke` | 无界面把索引、目录、插件都点一遍。 |
| `--mfttest` | 诊断 MFT 枚举（要管理员）。 |
| `--searchtime` | `--searchtime 词1 词2`：深索引搜索的耗时 + 前 3 条结果。 |
| `--searchdump` | `--searchdump 词1 词2`：每个词前 20 条**按顺序整条**打出来，改搜索实现就拿它做 diff。 |
| `--disktime` | C 盘上 A/B 两种磁盘遍历（DiskScan vs QDirIterator）的耗时。 |
| `--diskscale` | 拿环境变量 `LTOOL_DISKTHREADS` 把同一棵树按 1/2/4/8/16 线程各扫一遍，量扩展性。 |
| `--diskbranch` | 4 条：边扫边报的那几个分支数，和最后定稿的树是不是同一回事。 |
| `--diskui` | 7 条：磁盘页整条链（点「整理」→ 途中就出行 → 定稿 → 换盘让位 → 换回来吃缓存）。慢，要真扫一整盘。 |
| `--plugintest` | 十八个插件逐个验「元数据 → 真加载 → 建页 → 没显示 → 缓存命中 → 两边一致」+ 卡片墙张数与几何 + 动作路由（现在 202 条）。 |
| `--username` | 27 条：「Windows 用户名」那一页，**一次都不真的改账户 / 建账户**。 |
| `--rowmenu` | 9 条：磁盘页两栏列表的右键该弹什么、点开的是不是这一行自己的目录。 |
| `--archivetest` | `--archivetest extract / make / probe / progress`：压缩包读写回归；`make` 第 5 个参数是 7z 档位 0-9。 |
| `--pinyin` | 39 条：拼音表和码点必须对齐、多音字、全拼与首字母，最后真建一个不显示的窗口往输入框里打字。
| `--linuxdotest` | 33 条：「linux.do 盯帖」离线那半条 —— 解析 / 命中与去重 / 前缀拼法 / 配置夹紧与回读 / 要不要弹那一句 / 页面九件控件可达。一次网都不上、一个气泡都不弹（跑在沙箱 APPDATA 里）。 |
| `--linuxdonet [秒]` | 6 条：真去取一趟（默认等 60 秒），状态行说出这次走的是直连还是中转、拿到几条、第一趟只播种不弹框。**要联网，不进 CI**。实测一趟 4954ms、30 条。 |
| `--gridtest` | 52 条：「图片排版」的格数与列行、收图与拒收、拖动是挪还是换、✕ 只空那一格、合成尺寸与像素、板上预览与出图同一块构图、坏文件那一格说实话、**效果图弹框先看过才落盘**（`generate()` 里那道 `m_gate` 就是为这条留的门：判据把它换成回绝 → 断言没写文件、状态行说「没存」、「打开这张」没亮；换成同意 → 才真落盘并数到目录里多一个 `grid-*.png`。反向验过：把弹框从 `generate()` 里摘掉，那 2 条当场红成 `got=[0/0/1]`。离屏判据都不 `exec()` 模态框 —— 那里面的返回值不作数，这条在卸载那一步栽过）（离屏，判据在 \`plugins/grid/gridselftest.cpp`）。**点空格子 = 往里放一张**这一组 13 条：`pickInto(那一格)` 只在「空格子 + 原位松开」时发（点有图的格子不发 —— 那是拖动起点；按下去拖出那一格再松也不发；点 ✕ 只拿掉不发选图），`putAt` 只动指定那一格、拒非图片后缀、拒越界格号、能盖掉已有那张；端到端那两条走页面：先给 `setPickGate` 装一道门（默认是 `QFileDialog`，判据换成给一个固定路径），点第 4 格 → 断言 `paths()[3]` 就是那张、状态行说「第 4 格放了」，门回空串（= 选图框按取消）→ 那一格还是空的。反向验过：把页面那条 `connect` 注释掉，红的正是端到端那 2 条。**踩过的坑**：板子层的信号判据一开始走的是页面那块板子，一点就弹真选图框 —— 离屏看不见窗口但进程卡在模态框里，形状是「0 字节输出 + 一直不返回」；改成用一块独立的 `GridBoard` 验信号，页面只留给端到端那两条。 |
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
| `--storetest` | 60 条：插件商店离线那半条链 —— 清单解析（收两条、拒三条，拒的每条说一句）、`localRows`（目录里 `*.dll` 算已装、`*.dll.off` 算已禁用、旁边的 `*.bin` 不算）、和本地目录比（能装/已装/可更新/装不了）、`.part` 验哈希再改名（哈希或字节数不符就删掉半成品）、`<file>`↔`<file>.off` 改名禁用/恢复、索引地址留空回内置。**一次都不联网**。 |
| `--storenet` | 17 条：真拉一次 `dist/index.json` 并离屏点「刷新清单」，**逐张卡**核对「那句状态词 ↔ 那颗按钮」——没装的只给「装」、装了的只给「禁用/启用」、可更新的两颗都要，再对状态行里的卡片数 / 能装数 / 拒收数 / 清单外数；三条新的盯这次改版：卡片上不许留裸字节数、本机装着的那几张都得写出 MB、**每颗按钮都在卡片最底下一行**（离屏量几何）。**要联网，没网就是红，所以不进 CI**。 |
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
