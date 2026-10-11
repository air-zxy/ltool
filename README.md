# ltool — uTools 风格的 Windows 启动器

C++20 / Qt 6 / vcpkg + CMake 实现的快速启动器。

## 功能

| 功能 | 说明 |
| --- | --- |
| 应用快速启动 | 扫描桌面（含一层子目录）、公共桌面、用户/公共开始菜单里的 `.lnk`，解析目标与图标，同目标自动去重。配置页「应用搜索的额外目录」（在**配置那一栏往下滚，「定时扫描」和「剪贴板历史」中间**那一块）还能自己加：一行一个目录，填 exe 就直接收，填文件夹就**往里若干层找 exe**（层数 0-6 可配，默认 3 层，`apps/roots` / `apps/depth`）；扫出来的和快捷方式走同一套去重，卸载器一律不算应用。加/改目录要按保存才会重扫（扫盘是重活，不随输入跑），无界面断言在 `ltool.exe --appdirs` |
| 全局呼出 | `Alt+Space`（被占用时自动降级 Win+Space → Ctrl+Alt+Space）在屏幕中上方弹出搜索框，再按一次隐藏 |
| 搜索 | 输入即搜应用（前缀 > 词首 > **拼音全拼** > **拼音首字母** > 包含，按使用频率排序）；**`renwu` 出"任务/人物"，`rw` 也出**（多音字每个读音都算：`chongqing` 和 `zhongqing` 都能搜到"重庆"），工具直达同样认拼音（`cipanqingli` / `cpql` → 磁盘清理），**但直达那一档必须从开头命中**（`Pinyin::matchStart`）—— 中段命中只是“名字里正好含这几个字母”，拿它独占整页结果会把人要的那个顶掉：实测踩过搜 `renwu` 想要任务管理器，结果被「开机启动分析」的关键词“计划任务”（jihua+renwu）当成直达，而直达是 `return`，应用那一节整个没了。应用那一档仍然允许中段（打 `guanli` 要能出任务管理器）；表由 `tools/genpinyin.py` 从 pypinyin 的字典生成（2.6 万字，140KB，编进 exe）；文件索引不做拼音（270 万条现算会把 20ms 拖成秒级，要做得预建索引）；同时搜文件（见下）；网址/网站关键字直达浏览器 |
| 文件搜索 | **快索引**（桌面/文档/下载/图片/音乐/视频，实时监听秒级更新）+ **深索引**（全盘快照 + USN 增量，管理员权限下增量仅需约 0.5 秒） |
| 网站直达 | `ld`/`linuxdo` → linux.do，`gh` → GitHub，`gg` → Google…；输入网址直接打开；关键字后带参数可站内搜索（`gh qt6`）；无匹配时提供搜索引擎兜底 |
| 开发环境初始化 | `init` 列出插件，`init java` / `init c++` / `init node` / `init python` / `init rust` 检测组件，缺失的按 Enter 用 winget 安装 |
| 管理 PATH | `path` / `path list` 查看用户 PATH；`path add <目录>` 添加；`path del <目录>` 移除；写入 `HKCU\Environment\Path` 并广播 `WM_SETTINGCHANGE` |
| 注册表整理 | `reg` 体检：无效卸载残留 / 失效开机启动项 / MuiCache 残留；Enter 删除单项，`reg clean` 全部清理，删除前自动备份 `.reg` |
| 配置页面 | 左侧菜单 + 右侧内容：语言（下载/版本切换/关联软件/配置文件）、软件（图标/版本/下载/官网）、网站、定时任务、修复、广告弹窗、C盘清理、网络、配置 |
| F1 截图 | 全屏遮罩框选，鼠标悬停自动吸附窗口（**吸附出来的就是看到的那个边界**：拿 DWM 的 `EXTENDED_FRAME_BOUNDS`，不是 `GetWindowRect` —— 后者在本机实测比可见帧**左/右/下各多 7px、上边多 0px**（那三边是看不见的 resize 边框），旧写法还按左边的余量给上边补了 7px，等于四周各多圈一层桌面；无界面断言在 `ltool.exe --snaprect`）；标注支持矩形/圆/箭头/文字/马赛克/撤销；**提取文字**（自带的 PaddleOCR，第一次用下载一次模型、之后离线）把选区里的字认出来，自动进剪贴板并可编辑/存 txt；完成后自动复制到剪贴板并存到截图目录（**默认在软件目录下：`D:\software\ltool\captures`**，不往 C 盘的 `%APPDATA%` 堆；配置页「数据」里可改，装到写不了的只读位置时自动退回数据目录）。置顶截图窗里画的矩形/圆/箭头/打码块都是**可编辑对象**：切到「拖动/选择」点中就选中，拖内部整体移动、拖 8 个手柄改大小、Delete 删除（打码块挪走后原位置恢复成原图，因为底图和标注分开存）；按住 Alt 拖是自由涂抹，烙进图里改不了。工具栏还有一把**「划词」**：认完字后直接在图上拖拉选字（按词的包围盒切到单个字），松手就进剪贴板、窗不关，单击选整词、`Enter` 复制全部、`Esc` 退出；要改字/存 txt 用图上右键「识别全文」。左侧往期缩略图**右键 = 删除那一张**（内存和 `captures` 里的文件一起删；删的正好是当前显示的那张就自动挪到相邻一张）。**缩放不是 100% 也能截全整块屏**（他这台机器 150%）：`QPixmap` 的 `copy()` 和 `drawPixmap` 的 sourceRect 都按**设备像素**解释，而选区是逻辑坐标，旧写法直接把逻辑矩形递进去，150% 下就只截到 4/9 那块屏；现在统一过一层 `deviceRectFor`（dpr=1 时原样返回，100% 的行为一点没动），裁完再把 DPR 贴回去。无界面断言在 `ltool.exe --capgeo`（含一张自造的 1.5 倍画布，100% 的机器上照样能抓到这个错）；**置顶窗默认就是"只有一张干净的图"**：工具栏和左边那条往期带建窗时都收着（窗口正好等于图片本身），要工具栏就在图上右键「显示工具栏」，换上一张/下一张用图左右两侧的箭头（往期带收着也能用），放出来之后工具栏的「历史」按钮再切往期带。右键「隐藏工具栏」可以随时收回去 —— 收起时窗口抱住图片本身（不留左右黑边、也不留那条 42px 的空白），且图上右键只剩「显示工具栏」和「关闭」两项 |
| F2 录屏 | 按 F2 弹出选择列表（输入关键字过滤，回车开录），**不想录了有三条退路**：`Esc` 收起、点到别的窗口（一失焦就自己收起，它原本会一直压在你要录的那个窗上面）、**再按一次 F2 也收起**（手就放在热键上，这条最自然）；这三条都能从输入框和列表里收到，因为 `QLineEdit` 会自己吃掉 Esc，所以给两个子控件都装了事件过滤器。**第一条就是「整个屏幕 · 主显示器 · 2560x1440」**，往下是桌面上看得见的窗口，选谁录谁：录窗口用的是系统自带的 `Windows.Graphics.Capture` 按窗口取帧，别的程序盖在它上面也不会入镜；被虚拟桌面收起 / 没标题 / 小于 80x60 的窗口不列。**录整块屏幕走 `CreateForMonitor`，并且把鼠标指针也画进画面**（`IsCursorCaptureEnabled`；录窗口那一路仍然不含指针，保持合成帧确定性）——注意那条小悬浮条本身在屏幕里，录全屏时它会出现在画面右下角。右下角一条小悬浮条显示 ⏺ 计时 + 帧数，点「停止」收尾，产物是普通 MP4（H.264，Media Foundation 系统编码器，不拉任何外部 DLL），存到录像目录（**默认在软件目录下：`D:\software\ltool\recordings\rec-时间戳.mp4`**，不在 C 盘；配置页「数据」里可改），条上变成"已保存 N 帧 / X KB → 文件名" + 「打开所在目录」。**旁边还有一个「不要了」**：停下这一局并且把那个半成品 MP4 就地删掉（Recorder 没有"只停不写盘"这条路，所以照常收尾再原地删），条上写"已取消，没保存（丢掉 N 帧）"，8 秒后自己消失 —— 录到一半的东西留在录像目录里就是垃圾。**当前只录画面，不录声音**（要加的是 WASAPI 环路采集 + 音画对齐，没做就不放开关）。参数在 `config.ini`：`ui/recFps`（默认 15；画面静止时 WGC 不给帧，不凑帧）、`ui/recBitrateKbps`（默认 8000）。中途改窗口大小 / 把目标窗口最小化或关掉，这一局会自己收尾（已录下的部分照样在文件里，条上会写清是"没录成：原因"），收尾后 F2 立刻可以再用。H.264 只吃偶数画幅，所以奇数大小的窗口会自动往下取偶数再编 |
| 修复 | 检测 .NET Runtime / VC++ 2005~2022 / VSTO / DirectX / UCRT，未安装的一键 winget 安装 |
| 广告弹窗 | 按窗口标题/进程名扫描弹窗，可关闭窗口、结束进程、加入拦截名单；勾选自动拦截后每 60 秒自动关闭 |
| C盘清理 | 常见可清理位置（临时/更新缓存/DX缓存/pip/npm/浏览器缓存等）扫描大小、一键清理、回收站清空；C 盘顶层目录占用分析。**两个 tab 的列表都支持右键「打开文件夹」**（右键落在整行哪都行 —— 名字、下面那行完整路径、占比条都会冒到行卡片上）：「可清理位置」开的是那个缓存/临时目录本身，「占用分析 · 本层目录」开的是那一行对应的目录；左键在占用分析里是下钻，所以开文件夹只走右键。目录已经不在了（比如清理完之后的 Temp）那一项就摆成灰的、写「这个目录已经不在了」，不会点了没反应。这条只有界面在用，且真按一下就会拉起资源管理器，所以判据在离屏自测 `ltool.exe --rowmenu`（9 条：菜单就一项、文案、灰没灰、**这项的 data 就是这一行自己的目录** —— 点开之后拿的就是它，所以这条等于验"开的是它不是别的"） |
| 网络 | 网卡 DNS 一键切换预设（阿里/腾讯/114/Google），UAC 提权 netsh 执行，含刷新 DNS 缓存与恢复 DHCP |
| 文件占用 / 粉碎 | 文件与文件夹右键 ltool 子菜单：**占用分析**一个窗口两件事 —— 上面是大小 / 实际占簇 / 簇大小 / 文件系统 / 硬链接（文件夹会递归求和），下面是哪个进程正开着它（Restart Manager，可结束该进程）；**粉碎**为三轮覆写后永久删除，不进回收站 |
| 压缩包读写 | 右键解压到当前目录 / 解压到同名文件夹、压缩为 .zip / .7z。读是 libarchive：zip / 7z / tar / gz / bz2 / xz / zstd / iso / cab / cpio，rar 只读且不完全支持；写两条分家 —— zip 用 libarchive 的 deflate，**.7z 用 7-Zip 自己的多线程编码器**（bit7z + 随包的 `7zip.dll`，见下）。**要快就用 zip**：同一个 32MB 的文件夹实测 —— 我们的 .zip 0.73 秒 / 27.1MB（比 7-Zip 自己压 zip 的 0.96 秒还快）。**.7z 那条已经不再走 libarchive**，改成直接请 7-Zip 自己的多线程编码器干活（`src/SevenZip.cpp` + vcpkg 的 `bit7z`/`7zip`，运行时加载随包带的 `7zip.dll`）：同一个文件夹 **10.1 秒 → 1.8 秒**（改前那 10.14 秒是他自己现场日志里的一行），产物 23.46MB 和 7-Zip 命令行逐字节同尺寸（它 `-mx5` 是 1.58 秒，差的三百毫秒是我们启动自己的开销）。以前追不上的原因量清楚了，就是线程数：libarchive 的 7z 写出器 LZMA1/LZMA2 恒为单线程，7-Zip 靠 `-mmt` 把 LZMA2 分块丢给多个核。试过又否掉的两条：把 zstd 塞进 7z（`threads` 唯一支持的编码器）能到 0.3 秒，但产出的包连本机 7-Zip 25.01 都报 `Method = 04F71101 / Unsupported Method`；自己包一层 .lzx（清单 + 标准 .xz 流，用 liblzma 的 `lzma_stream_encoder_mt`）实测 0.48 秒 / 25.7MB 最快，但**别人拿到 .lzx 根本不知道是什么文件**，所以删了。zip 里指定 zstd 也不行：7-Zip 会静默退回 Deflate。中文条目名按格式喂 `hdrcharset`（tar/cpio=UTF-8，zip/rar/lha=本机码页），中文路径走 `_w` 接口。默认后台静默完成 + 托盘提示，配置页可打开进度窗口（带取消）；**进度窗口跑完自己就关**（成功或取消都关，那句话交给托盘去举），只有失败才留着说原因，这时「打开位置」指向那个没改名的半成品；半成品写 `.part`，成功才改名。**吞吐实测（60MB 伪日志，`--archspeed`）**：解压 zip 857MB/s、解压 7z 343MB/s（都比 7-Zip 自己快，解压这条路还是 libarchive）；压缩 zip 118MB/s / 5.9MB；**压缩 7z（新后端）4.17 秒 / 3.6MB，同一个夹具 7-Zip 命令行要 6.30 秒 / 3.4MB —— 我们快 34%**。档位曲线（只认 7-Zip 定义的 0/1/3/5/7/9）：1 档 0.20 秒 / 4.7MB、3 档 0.53 秒 / 4.5MB、5 档 4.43 秒 / 3.6MB、7 档 7.79 秒 / 4.4MB、9 档 8.00 秒 / 4.4MB —— **7 档往上体积反而回涨**（多线程把输入切的块多了，每块各自从零开始建字典，上下文短了），所以默认停在 5 档：它既是 7-Zip 自己的默认档，也是这条曲线上"每秒换多少字节"最划算那一点。每一档都验过两件事：我们自己的读取器解得回来、`7z t` 退出码 0。试过又否掉的两条：把 zstd 塞进 7z（`threads` 唯一支持的编码器）能到 0.3 秒，但产出的包连本机 7-Zip 25.01 都报 `Method = 04F71101 / Unsupported Method`；自己包一层 .lzx（清单 + 标准 .xz 流，用 liblzma 的 `lzma_stream_encoder_mt`）实测 0.48 秒 / 25.7MB 最快，但**别人拿到 .lzx 根本不知道是什么文件**，所以删了。zip 里指定 zstd 也不行：7-Zip 会静默退回 Deflate。**右键那一次性进程干完活把托盘气泡交给常驻实例去举**（走单实例那条本地套接字，消息格式 `note\t标题\t正文`）：以前它为了"让气泡显示满 5 秒"自己多活 5 秒，实测解压一个包端到端 5.4 秒而活儿本身只有 0.2 秒；改完端到端 0.36 秒 |
| 其他 | 托盘图标、单实例、使用频率统计（空查询显示常用应用）、运行日志 |

## 文件索引如何保持新鲜

- **快索引（常用目录）**：启动全量扫一遍后，用 `ReadDirectoryChangesW` 实时监听目录，文件新建/删除/改名秒级反映到搜索结果。
- **深索引（全盘）**：
  - 启动时加载上次的全量快照（`deepindex.dat`），搜索立即可用；
  - **管理员权限运行**：读取 NTFS USN 变更日志做增量更新（只处理变化，实测约 0.5 秒），日志失效时才重新枚举 MFT 全量重建（约 8 秒）；
  - **普通权限**：无法读 USN 日志，保留快照使用；快照超过 `refreshHours`（默认 6 小时）后，下次启动/定时刷新时用目录遍历重建（约 17 秒，后台进行）。
- 手动 `rescan` 命令或托盘"重新扫描"会强制全量重建。

## 构建

依赖：Visual Studio 2022（MSVC）、CMake ≥ 3.24、vcpkg（本机 `D:\vcpkg`）。清单依赖是 `qtbase` + `libarchive` + `bit7z`（见 `vcpkg.json`；bit7z 会带进 `7zip` 和 `ghc-filesystem`，`7zip` 打出来的就是 7-Zip 官方那个格式库，只是名字叫 `7zip.dll`）。

```bat
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 -DCMAKE_TOOLCHAIN_FILE=D:/vcpkg/scripts/buildsystems/vcpkg.cmake
cmake --build build --config Release
```

- 走 **vcpkg manifest 模式**（根目录 `vcpkg.json`，baseline 与本机 vcpkg 对齐）；依赖会装进 `build\vcpkg_installed`，本机有二进制缓存时 configure 阶段直接恢复，不需要重编 Qt。首次拉 libarchive 约 1~2 分钟。
- **加新依赖就 `cmake -S . -B build`，别在仓库根上裸跑 `vcpkg install`**：命令行那次不读 `build\CMakeCache.txt`，会把整棵树按默认位置重装一份到 `<项目>\vcpkg_installed`（实测 2.2GB 的 Qt6 副本，构建根本不读它，只会让人以为依赖换了地方）。
- 7z 那条后端的许可证：**`7zip.dll` 是 LGPL-2.1**（其中 `CPP/7zip/Compress/Rar*` 与 `Archive/Rar/*` 另带 **unRAR 限制条款** —— vcpkg 的端口把 Rar 解码器也编进去了，所以读 rar 的那条路受这一条约束），**bit7z 是 MPL-2.0**。两份**原文**由 CMake POST_BUILD 从 vcpkg 树的 `share/<端口>/copyright` 复制成 `licenses\7zip-LGPL-2.1-with-unRAR.txt` / `licenses\bit7z-MPL-2.0.txt` 跟着 exe 一起装（不自己抄，抄的迟早和上游不一致）。LGPL 的"库形式使用"这一条是满足的：7zip 是独立的 DLL，换掉它就能重新链接。
- **别手动往 `D:\vcpkg\installed` 装**：那是 classic 树，本项目读的是 manifest 树，装了也链不上。
- **一个目标里的 53 个 .cpp 是并行编的（`/MP`，见 CMakeLists 顶部），并且有预编译头（`src/pch.h`）**。VS 生成器默认把同一目标内的编译排队串行跑，实测改一次 `src/Utils.h`（47 个 TU 包含它）串行要 **114.7 秒**，加 `/MP` 后 **16.4 秒**；再加 PCH 之后（2026-10-12 同一台机器重测）：**改 `Utils.h` 19.9 → 13.7 秒**、**改 `main.cpp` 12.0 → 10.2 秒**、**全量重建 20.2 → 17.9 秒**、**什么都没改 2.5 秒**（这 2.5 秒是 msbuild 启动 + CMake 检查 + AUTOMOC 扫 53 个 TU 的固定税，改一个 5KB 的小 TU 也要 3.1 秒 —— 也就是说日常这一档基本是固定开销，不是编译）。PCH 里故意不放 `windows.h`：33 个文件要它，但录屏那几个要 C++/WinRT 的头、USN 那几个要 `winioctl.h`，放进 PCH 等于把这些顺序约束强加给所有 TU，而真正的大头是 Qt 那几个模块头。
- 构建后自动执行 windeployqt，把 Qt DLL / platform 插件部署到 `build\Release`；但 **windeployqt 不管 vcpkg 的依赖**，`archive.dll` 及其传递依赖（zlib1 / liblzma / zstd / bz2 / lz4 / brotlicommon / brotlidec）是靠 CMake 的 applocal 复制落到 `build\Release` 的，安装包再按 `build\Release\*` 一起收走。（windeployqt 单跑只有 115ms，不是耗时瓶颈，所以留在每次构建里。）

## 打包 + 安装（每轮改动的交付闭环）

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
installer\ltool-0.4.0-setup.exe /VERYSILENT /NORESTART /SUPPRESSMSGBOXES
```

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

- 产物：`installer\ltool-<版本>-setup.exe`，**20.7 MB**；安装目录 **约 73 MB**（ICU 三个 DLL 就占 37 MB，动不了）。
- 安装行为：默认按用户装到 `%LOCALAPPDATA%\Programs\ltool`（`PrivilegesRequired=lowest`，无需管理员）；本机实际装在 `D:\software\ltool` —— 同一个 `AppId` 第二次安装会**沿用上次的目录**，所以闭环命令里不写 `/DIR` 也会覆盖到原处；全新机器要指定就加 `/DIR="D:\software\ltool"`（与 `config.ini` 里 `[install] path=D:\software` 一致）。含简体中文向导；可选任务"创建桌面快捷方式""开机自动启动"（HKCU Run，卸载时清除）。
- `CloseApplications=yes`：安装会等正在运行的 ltool 退出，**静默安装可能卡几十秒**，不是失败。
- 卸载不会删 `%APPDATA%\ltool` 用户数据（索引/插件/任务配置/日志保留）；卸载器 `unins000.exe /VERYSILENT`。
- 构建时会先生成 `resources\ltool.ico`（由 `tools\genicon.cpp` 编译的小工具绘制，7 个尺寸）并嵌入 exe 资源，exe/快捷方式/安装包共用同一图标。
- Inno 不会自动删掉"已经不在包里"的旧文件，剪依赖时要同时往 `[InstallDelete]` 里点名，否则升级完安装目录还是老的体积。
- 打包前先跑回归（无界面，一行结论）：`build\Release\ltool.exe --archivetest extract <包> <目录>`、`--archivetest make <输入> <输出> [7z] [档位]`、`--archivetest probe <文件>`、`--archivetest progress <输入> <输出> [7z] [第几次按取消]`；整套 16 项在 `build\verify\archtest.ps1`（zip / 7z / tar.gz 解压、GBK 名字的老 zip、自产包两个方向往返、单文件 `.gz`、"是不是压缩包"的三种判定、硬解纯文本，加上进度/取消三项：7z 全程、7z 第 3 次回调按取消、zip 全程当对照；最后用 `7z -t` 验我们自产的两个包外部工具也认）。脚本和临时产物都放 `build\verify\`，`build\` 已在 `.gitignore` 内。

## 使用

- 运行后在托盘出现图标；`Alt+Space` 呼出/隐藏搜索框（鼠标所在屏幕，中上方）。
- 回车 / 双击运行，`↑↓` 选择，`Esc` 隐藏，右键有更多操作（打开所在位置 / 复制路径 / 加入 PATH）。
- 内置命令：
  - `rescan` — 重新扫描应用与文件索引
  - `init [java|c++|node|python|rust]` — 开发环境检测与安装
  - `path [list|add <目录>|del <目录>]` — 用户 PATH 管理
  - `reg [scan|clean]` — 注册表体检 / 一键清理
- 命令行参数：`--show` 启动即显示搜索框；`--smoke` 无界面自检；`--mfttest` 需管理员，诊断 MFT 枚举；`--searchtime 词1 词2` 无界面，加载索引后逐词打印深索引搜索耗时与前 3 条结果（**272 万条下现在 9~38ms/次**：两个字的词 9ms、`chrome` 38ms。改之前是 540~680ms —— 每命中一条就 `fromUtf8` 造一条整路径 QString 收进一个大数组，而绝大多数命中根本活不到前 20 名。现在按 (来源, 分数, 名字长度, 路径) 分 8 个桶、每桶只留 2×limit 条候选，比大小时不造 QString（UTF-8 的字节序就是 BMP 字符的 UTF-16 序，只有含 emoji 那类 4 字节序列才退回 QString 比），首字节用 memchr 跳，每个卷的条目区间切成段丢给最多 8 个线程各扫各的再并桶）；`--searchdump 词1 词2` 无界面，把每个词的前 20 条结果**按顺序整条打出来** —— 改搜索实现最怕「快了但顺序变了」，所以这条就是改前改后各跑一遍、拿 diff 当判据的入口（2026-10-12 那次重做：同一份实现连跑三遍互相比对一致、和老实现逐行一致，只有索引条数那一行随磁盘变化）；`--disktime` 无界面，A/B 对比两种磁盘遍历（`DiskScan` 一次建树 vs `QDirIterator`）在 C 盘上的耗时并列出前 5 大目录；`--rowmenu` 无界面，验磁盘页两栏列表（「可清理位置」和「占用分析 · 本层目录」共用同一行卡片）右键该弹什么：存在的目录只有一项、叫「打开文件夹」、点得动，且**这项的 data 就是这一行自己的目录**（点开之后界面拿的就是它，所以这条等于验“开的是它、不是别的”）；目录已经不在了就只剩一项灰的、写「这个目录已经不在了」；再加两条几何/结构断言（行卡片的 contextMenuPolicy 是 CustomContextMenu，下面那行路径是它自己的子控件 —— 右键点在路径那行上也要冒得到）。判据写在 DiskPage.cpp 里（行卡片藏在那个文件的匿名 namespace），菜单构建拆成了独立的 `makePathMenu`，因为真按一下就会拉起资源管理器，那一下不能放进自测；`--archivetest extract|make|probe|progress` 无界面跑压缩包读写（回归用，见上；`make` 的第 5 个参数是 7z 档位数字 0-9，zip 那条不吃；`progress <输入> <输出> [7z] [第几次按取消]` 是专门验进度与取消这两条只有界面在用的线的：分母准不准、done 单调不减、回调返回 false 之后有没有当场停下、半成品 `.part` 有没有被删干净）；`--pinyin` 无界面验拼音匹配（31 条断言）：表和码点必须对齐（错一位就会把"垃圾"当"任务"）、多音字（重庆 chongqing/zhongqing 都算）、全拼允许中段而首字母只认开头、纯英文名不掺和这档、单字母不启用、耗时（810 个名字首次展开 979us、缓存后扫全表 79us/次），**最后真建一个不显示的启动器窗口往输入框里打字**，验 `mumumoniqi` 能搜到本机的"MuMu模拟器"、`cipanqingli`/`cpql` 能直达"磁盘清理"，再加 `renwu` 那两条（**应用节必须在**、**工具直达那一节不能把应用顶掉**）和 `qingli` 一条（取整不伤直达：它不是「磁盘清理」的开头，但是关键词「清理」的开头）；`--archspeed [每文件KB] [文件数]` 无界面量压缩包读写的吞吐（默认 4096×15=60MB 伪日志，另可 `--archspeed 2 2000` 量海量小文件）：我们 vs 系统里的 7-Zip，zip/7z 两个方向各打一行 MB/s 和产物大小，并把 7z 的 1/3/5/7/9 档整条曲线跑一遍，**每档都验"我们自己的读取器解得回来 + `7z t` 退出码 0"**（换档位最怕快是快了、别人打不开），夹具全在 `%TEMP%\ltool-archspeed`；`--ocrtest <图片>` 无界面打印图里认出的文字与耗时；`--ocrlayout <图片>` 无界面打印每行每词的包围盒和切出来的字格（划词的坐标就靠它验）；`--pinpick <图片>` 无界面，把鼠标事件直接送给一个不显示的置顶窗，跑「图上划词」的 9 条断言（拖半行/单击整词/跨行/词内半个字/跨词空格/Enter 全部/Esc 退出），跑完归还剪贴板；`--recprobe [ms]` 无界面量录屏的地基：系统里有哪些视频/音频编码器、MP4/H.264 写 30 帧再读回来要多久、`Windows.Graphics.Capture` 按窗口取帧能不能用（会弹一张自己的小窗 1 秒多）；`--rectest [秒]` 无界面录一个自造的会动的卡片，然后 `inspectMp4` 把文件解回来数帧（帧数/时长/有没有音轨/画幅），**末尾再录 1.5 秒整块屏幕**（`CreateForMonitor` 那一路：能开起来 / 采到帧 / 全程无错 / 收尾后 `running()` 回 false / **解出来的画幅正好等于这块屏幕的物理尺寸取偶** / 时长对得上），画面变化靠的就是那张每 30ms 换数字的卡，不合成鼠标；`--recordui [秒]` 把 F2 那条链整条走一遍（23 条断言）：列表第一条必须是「整个屏幕」且带着“这是屏幕不是窗口”的标志位、第二条是普通窗口 → 悬浮条在计数 → 用 `click()` 程序化按「停止」→ 报已保存 → 文件能解出帧 → **再开一局按「不要了」：条上写“已取消，没保存（丢掉 N 帧）”，并且断言 `QFile::exists` 说那个 MP4 真的不在磁盘上了**（判的是文件，不是它自己写的那句话）→ **选择窗的三条退路**：往列表里 sendEvent 一个 Esc 键能收起（`QLineEdit` 自己会吃掉 Esc，所以产品里是给输入框和列表两个子控件都装了事件过滤器）、给它派一个 `WindowDeactivate` 能收起（=“点到别处就自己收起”那条，不合成鼠标）、`RecordUi::startViaPicker()` 连按两次能收起（第二次就是 F2 的取消分支，走的是全局那个 picker 而不是测试里这个局部对象）；后面还是改大小会收尾、一次失败只写一条日志、自己收尾之后 F2 还能再开一局那几条；`--pindel <图片>` 无界面，跑「右键往期缩略图删除」的 10 条断言（往期计数 / `captures` 里的文件真没了 / 删的正是当前那张时自动挪到邻居 / 越界不炸），它把 `APPDATA` 临时改到 `%TEMP%\ltool-pindel`，不会碰你真实的往期截图；`--ocrpanel <图片>` 真开一次"提取文字"结果窗；`--ocrget` 无界面，只把 OCR 引擎那三个文件下到 `%APPDATA%\ltool\ocr`（每 0.5MB 报一次进度）；`--ocrchain <图片>` 把「首次使用」那条整链跑一遍（11 条断言：沙箱里没有引擎 → `available` 说要下载 → 下载 → 进度只朝前且到 100% → 下载中再要一次不会重开 → 文件齐 → 当场认得出那三行字），它把 `APPDATA` 临时改到 `%TEMP%\ltool-ocrchain`，会真下 26MB、但碰不到你真实的那份；`--memloop [轮数] [宽x高]` 无界面，用来回答"做完某事内存没回落"：每轮先解压一个自造的 32MB 包（随机字节，压不动）再录一段指定尺寸的窗口，逐步打印私有字节 / 工作集 / 句柄数的增量（判读：私有随轮数线性涨=真泄漏；涨两轮封顶=分配器留着页复用，不是泄漏）；`--toggle` 冷启动跑，6 条断言核对"按 Alt+Space 到底出不出来"（冷状态呼出且必须在某个显示器内 → 最小化后再按必须拉回来 → 失焦自动收起 → 收起后再按能再呼出 → 在前台时再按才收起），它会真的把启动器弹出来一两秒；`--pathcfg` 无界面，验配置页那两行存放目录真的管用（沙箱 `APPDATA`，9 条断言：默认值 → 写 `paths/captures` → 新截图落进新目录且不碰老目录 → `CaptureHistory::reload()` 后往期还是那一张 → 录像目录能建能写）；`--appdirs` 无界面，验配置页「应用目录」那条路（沙箱 `APPDATA`，7 条断言：层数 0/1/2/3 各收到哪些 exe、中文目录名要进得去、卸载器不算、直接指到一个 exe 上也收、目录不存在只是跳过），夹具建在沙箱数据目录里且删之前先断言那是不是算出来的绝对路径；`--capgeo` 无界面，量截图那条几何链（真屏抓取 + 一张自造的 DPR=1.5 画布 + `drawPixmap` 的 sourceRect 按哪种坐标解释），这是"150% 缩放截不全全屏"的判据；`--cfgcheck` 离屏建一次配置页，数「下载代理」是不是只有一行、那行没生效过的「扫描根目录」有没有被加回来、应用目录那一栏（列表 + 层数）在不在 —— 配置页是 GUI，我不动他桌面，就只能把「某行出现几次」变成可断言的数字（2026-10-12 那次「下载代理」出现两遍就是这么抓的）。**同一天还补了一条结构断言：每个分组框都必须能从 `page.layout()` 走到**（滚动区里那层不是布局项，要专门进 `QScrollArea::widget()` 再走一层）。因为「应用搜索的额外目录」那一块 new 出来时认了 `this` 当父件、却没 `addWidget` 进那一列，于是标签全在、`--cfgcheck` 一片绿，**界面上却一行都没有**（他报的就是这个）；反向验过：把那行 `addWidget` 删掉，新断言就点名「应用搜索的额外目录」，而旧的三条照样全绿 —— 数标签这条路天生看不见这类问题；`--pintool <图片>` 无界面，跑置顶窗那两个收起开关的 19 条几何断言（沙箱 `APPDATA`）：初始有往期带和工具栏 → 「历史」按钮切往期带的宽 → 隐藏工具栏时窗口抱住图（不保留那条 42px 空白）→ 放回来时**长回原来那么大** → 两个开关叠用窗口正好等于图片本身 → **叠用过之后再依次放开必须回到最初尺寸**（这条抓到过"来回点几下窗口越走越歪"的 bug）→ 图区/缩略图命中坐标跟着新几何走；`--capflash [次]` 同理会真把截图浮层开一次（每下不到半秒）再走和 Esc 一样的取消路径，量的是"浮层在不在、有没有铺满整屏、是不是压在所有窗口最上"——**不判"有没有抢到前台"**，因为脚本进程拿不到前台是 Windows 的规则（前台锁看最后一次真实输入），真实按 F1 的这一支由产品日志 `capture: shown ... 前台=/在最上=/试了N次` 观测。

## 网站关键字

首次运行生成 `%APPDATA%\ltool\webs.json`，可自行增删：

```json
{ "keys": ["ld", "linuxdo"], "name": "LINUX DO", "url": "https://linux.do" },
{ "keys": ["gh", "github"],  "name": "GitHub", "url": "https://github.com",
  "searchUrl": "https://github.com/search?q=%s" }
```

- `keys`：触发关键字（输入 `ld` 或 `linuxdo` 回车即打开 linux.do）
- `searchUrl`：可选；配置后输入 `gh qt6` 会提供"在 GitHub 搜索 qt6"
- 输入完整网址（如 `linux.do`）也会直接提供"在浏览器打开"

## 开发环境插件

默认插件是 JSON 文件，放在 `%APPDATA%\ltool\plugins\`（首次运行自动生成）：

```json
{
  "id": "go",
  "name": "Go 开发环境",
  "items": [
    { "label": "Go", "cmd": ["go", "version"], "wingetId": "GoLang.Go" }
  ]
}
```

软件页的"下载/版本"用 winget 实现：配置 `[install] path=D:\software` 后安装会尝试 `--location`（部分安装包不支持位置参数时会失败，属正常）。

- `cmd`：检测命令（是否存在 + 输出版本）
- `wingetId`：缺失时 Enter 触发的 winget 包（可省略，配 `note` 纯提示）

## 配置与数据目录

`%APPDATA%\ltool\`：

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
; 配置页「应用搜索的额外目录」：一行一个目录，填 exe 直接收，填文件夹往里找 exe。
; 这条是真在用的（AppScanner::scan 读它），默认留空 = 只扫桌面/开始菜单那几处。
roots=D:\tools;D:\software\便携软件
depth=3               ; 往里找几层（0=只看目录本身，最多 6）
```

## 已知限制

- **「压缩为 .7z」依赖 exe 旁边那个 `7zip.dll`（1.8MB，7-Zip 官方的格式库）**。它是运行期按路径加载的（objdump 查过：不在 ltool.exe 的导入表里），所以漏打包**不会**让程序起不来 —— 解压、zip、热键那些照跑，只有这一条给出"7zip.dll 不在软件目录里"并写进行为日志。`build\verify\deliver.ps1` 装完会点名确认它在不在，别只看 exe 的哈希。
- **深索引增量（USN）需要管理员权限运行**；普通权限下自动回退为"快照 + 定时目录遍历重建"，功能不缺失只是新鲜度略降。
- 本机 Windows 版本（26200）的 `FSCTL_ENUM_USN_DATA` 必须把 `MFT_ENUM_DATA` 的 `HighUsn` 设为当前日志最大 USN 才能正常枚举（已适配）。
- `reg clean` 里 HKLM 的项需要管理员权限，无权限时该项会失败并提示（HKCU 项不受影响）。
- `Alt+Space` 被其他程序占用时自动降级到备选热键（日志与托盘提示）。
- 截图"提取文字"的引擎是**自带的 PaddleOCR PP-OCRv4**（det 4.5MB + rec 10.4MB + `onnxruntime.dll` 17.6MB）。这三样**不打进安装包**：第一次用到时从 PyPI 现下（两个固定版本的 wheel，SHA256 钉死，解包只抽这三个成员），落在 `%APPDATA%\ltool\ocr`，之后完全离线；下不到就明说"没装上"。以前用的系统自带 `Windows.Media.Ocr` 已经**完全移除** —— 它取决于对方在 设置→应用→可选功能 里装没装「中文(简体) 光学字符识别」，很多机器上没有。运行时用 `LoadLibrary` + ONNX Runtime 的 C API（`third_party/onnxruntime/*.h` 只要头，不链接），det 找行 → 每行 crop 后 rec 认字 → CTC 贪心解码，字典从 `rec.onnx` 的元数据 `character` 里读（6625 类：0 是 blank，尾巴一个是空格）。**只做正放的文字**：det 的连通域取轴对齐外接矩形，不做 minAreaRect/透视矫正（对外的 `Ocr::Line` 本来就是 QRect）。实测：640×180 三行字 0.5s；2560×1440 整屏 2.1s、219 行；一个字一个框（划词因此不用等宽切）。手工补下：`ltool.exe --ocrget`。
