# ltool — uTools 风格的 Windows 启动器

C++20 / Qt 6 / vcpkg + CMake，只跑 Windows。界面本体都是 DLL 插件，主程序只剩热键、托盘、单实例、命令行自测。
**每一处"为什么这么做、实测多少"都整段挪到 [docs/notes.md](docs/notes.md)** —— 这里只留一行一条。

## 功能

| 功能 | 一句话 |
| --- | --- |
| 应用快速启动 | 扫桌面与开始菜单的快捷方式，加上你指定的应用目录；拼音全拼和首字母都能搜，按使用频率排序。工具直达排在最前面（回车就是它），但不挡住应用那一节。 |
| 全局呼出 | `Alt+Space` 在屏幕中上方弹搜索框，再按一次收起；被占用时自动降级。 |
| 搜索 | 输入即搜：应用 / 文件 / 网站关键字三路同时出，全角先折半角。 |
| 文件搜索 | 常用目录实时监听（秒级）+ 全盘快照（管理员下 USN 增量约 0.5 秒）。 |
| 网站直达 | `gh` / `ld` / `gg` 直接开浏览器；带上参数就站内搜索。 |
| 开发环境初始化 | `init java` 这类命令检测组件，缺的按 Enter 用 winget 装。 |
| 管理 PATH | `path add <目录>` 改用户 PATH，写完广播 WM_SETTINGCHANGE。 |
| 注册表整理 | `reg` 体检卸载残留 / 失效启动项 / MuiCache，删之前自动备份 .reg。 |
| 设置窗 | 左侧菜单只有四行：网站 / 插件商店 / 配置 / 关于。语言并进「配置」那一页的一节，软件、下载器、定时任务是商店里的卡片。 |
| F1 截图 | 全屏框选、悬停吸附到窗口、标注、置顶、图上划词取字（`plugins/capture`）。 |
| F2 录屏 | 录整块屏幕或某一个窗口，出普通 mp4，画质和画面两档可选（`plugins/record`）。选择窗没标题栏但**按住标题那一行能拖**；选中一条就在右边出**编码后**的实时预览（同一一路 WGC，不是另画一张示意图）；点条目只选中，开录要按中间那颗「开始录制」（或回车）。 |
| linux.do 盯帖 | 每隔几分钟（默认 10）拉一次 linux.do 最新帖，比标题和标签里的关键词（默认 福利/女装/公益 —— **比的是标题、标签和分类名**），命中就在托盘冒一句；**没打开过页面也在后台跑**（`plugins/linuxdo`）。 |
| 图片排版 | 四宫 / 六宫 / 九宫：把图拖进格子、空格子**点一下就能给它选一张图**（也可以整批拖进来，或用「添加图片…」）、从一格拖到另一格换顺序、点右上角 ✕ 拿掉那一张；按「生成宫图」**先把效果图弹出来给你过目**，背景色就在**这个弹框里挑**（换一下当场重画），按「保存这张」才认这个色、才落盘，按取消什么都不改（`plugins/grid`）。 |
| 剪贴板 | 复制过的文本和图片存成历史（**默认关**，配置页那颗开关才收），商店里这张卡打开是一张**列表 + 关键字过滤框**：内容、"图片"、尺寸、日期都筛得动，双击一行就放回剪贴板，选中按 Delete 去掉那一条（`plugins/clipboard`；收集那个时钟留在 core，搜索框里「剪贴板历史」那一节和配置页的开关读的都是同一份）。留多久由两条上限决定：**最多 200 条**，加上「**保留 N 天**」（配置页那个数字框，默认 7、选「不限」就只按条数）—— 超龄的一律清、图片文件跟着删，没超龄的仍受 200 条兜底（`clipboard/keepDays`）。 |
| 修复 | 检测 .NET / VC++ / DirectX / UCRT，未装的一键 winget 安装。 |
| 广告弹窗 | 按标题和进程扫弹窗，可关闭 / 结束进程 / 加拦截名单，勾了每 60 秒自动拦。 |
| C盘清理 | 可清理位置 + 所选盘的占用分析（矩形树图）；边扫边出数，每个盘留着结果。 |
| Windows 用户名 | 先把账户现状只读摆出来，改显示名 / 改账户名 / 新建用户都要人在界面上按下。 |
| 关于 | 版本号 + 检查更新 / 下载 / 装并重启三颗按钮，一件都不自动。检查更新走 `github.com/.../releases/latest` 那条 302（跟完重定向读 tag），**不碰 `api.github.com`** —— 匿名 api 是每台出口 IP 60 次/小时，多查几次就 403。 |
| 网络 | 网卡 DNS 一键切预设，提权执行，含刷新缓存和恢复 DHCP。 |
| 文件占用 / 粉碎 | 右键看谁占用着它、能结束那个进程；粉碎是三轮覆写后永久删除，不进回收站。 |
| 压缩包读写 | 右键解压 / 压缩：读靠 libarchive 十几种格式，写 .7z 用 7-Zip 自己的多线程编码器。 |
| 其他 | 托盘图标、单实例、空查询显示常用应用、运行日志。 |

每一项的实测数字、无界面判据（`--capgeo` / `--recq` / `--diskui` 那些）和踩过的坑，
整段挪到了文末的 **功能细节与实测** —— 门口这一张只说"是什么"。
界面背后是十九个 DLL 插件共用一份 `ltoolcore.dll`，见「工具插件」那一节。

## 构建

依赖：**Visual Studio 2022（MSVC）、CMake ≥ 3.24、vcpkg**（本机 `D:\vcpkg`）。

```bat
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 -DCMAKE_TOOLCHAIN_FILE=D:/vcpkg/scripts/buildsystems/vcpkg.cmake
cmake --build build --config Release
```

- 依赖走清单模式（`vcpkg.json`：`qtbase` + `libarchive` + `bit7z`）。加依赖只改这份 json 再重新 configure，
  别在仓库根裸跑 `vcpkg install`。
- 产物在 `build\Release\`：`ltool.exe` 305KB + `ltoolcore.dll` 1.63MB + `ltool-plugins\*.dll` **15 个**。
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
powershell -File build\verify\deliver.ps1        # 构建 → 打包 → 停旧实例 → 静默装 → 逐文件比 SHA256 → 重起
```

- 约 20~25 秒一圈。档位：默认 normal **22.08MB**（1.0.x 起是「开箱零插件」包：里面一颗插件 dll 都没有，只建一个空 `ltool-plugins\`，插件由商店按需拉）｜ `-Final` 21.8MB 那档是十二个 dll 都在包里时量的，没重测 ｜ `-Quick` 26.9MB ｜ `-NoBuild` 跳过构建。
- 版本号只有一处：`ltool.iss` 的 `#define MyAppVersion`，读不到就直接 FATAL。
- 交付判据是**逐个文件比 SHA256**（`ltool.exe` + `ltoolcore.dll` 两颗按哈希硬比；插件目录只报数 —— 零插件包里那颗 dll 都没有，装好的那些是商店拉的 CI 那份，和本机构建天生不同，逐颗比只会天天红一条假故障）；单独跑一遍：`powershell -File build\verify\shaall.ps1`。
- 装到哪：默认按用户装 `%LOCALAPPDATA%\Programs\ltool`，指定目录加 `/DIR="D:\software\ltool"`。
- 等价裸命令就三步：`cmake --build build --config Release` → `"D:\software\InnoSetup6\ISCC.exe" ltool.iss`
  → `installer\ltool-<版本>-setup.exe /VERYSILENT /NORESTART /SUPPRESSMSGBOXES`。

## CI（`.github/workflows/build.yml`）

编 Release → 跑无界面回归 → 打包 → artifact；**只有推 `v*` 标签才发 Release**。
带 `-beta.N` 的标签自动标 pre-release，而 `/releases/latest` 不认预发 —— 预发和稳定就靠标签分，客户端不用改。
同一分支后推的一次会顶掉前一次在跑的作业。

发一版三步：改 `MyAppVersion` → 推 `main` → `git tag v<一模一样的号> && git push origin v<那个号>`（第三步是对外的，人来按）。

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
## 开发环境插件（JSON）

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

## 插件

十九个自带插件：DLL 修复 / 广告弹窗 / 磁盘清理 / 网络 / 注册表分析 / 开机启动分析 / Windows 用户名 /
JSON 格式化 / Base64 编解码 / **右键动作** / **录屏** / **截图** / **软件** / **下载器** / **定时任务** / **语言** / **图片排版**（四宫/六宫/九宫）/ **linux.do 盯帖**（第一个后台自己会跑的）/ **剪贴板**（复制过的文本和图片，列表能按关键字过滤）——
第三方插件走同一条路，主程序里没有"内置工具"特判。其中软件 / 下载器 / 定时任务那三张是 2026-10-15 从设置页菜单里搬进来的：
菜单只剩 网站 / 插件商店 / 配置 / 关于 四行，语言并进「配置」那一节。

| 位置 | 是什么 |
| --- | --- |
| `ltool.exe`（305KB） | 热键、托盘、单实例、自测入口 |
| `ltoolcore.dll`（1.63MB） | 主程序和插件**共用的一份**实现（`Utils` / `DiskScan` / `InfoRow` / `Archive` / `CaptureHistory` …） |
| `ltool-plugins\ltool-<id>.dll` | 一个工具一个：本体 + 一层 `LToolPlugin` 壳 + 嵌在 dll 里的 `plugin.json` |

- 契约只有一份：`src/ltool-plugin.h`（现在 API 3）—— `apiVersion / id / name / desc / keywords / createPage / feedText / actions / invoke / save / load`。
- 三类插件：**卡片型**（插件商店一张卡，可开可递文本）、**动作型**（截图、录屏、右键那几件：入口留在主程序，按下之后 `PluginHost::invoke(动作名)` 转发）、**后台型**（`plugins/linuxdo`：没人按它也自己跑）。后台型靠 plugin.json 里 `"Activate": true` 挂牌 + dll 导出 `ltoolPluginActivate`，启动时由 `PluginHost::startBackground()` 按名字找它 —— **不走接口虚函数**：加虚函数要动 vtable，等于把 API 推到 4，而「API 不等就拒装」会把用户装好的每一颗插件全挡在门外。没挂牌的插件一个 dll 都不多装；挂了牌没导出符号 = 启动时点名报错，`--plugintest` 那 19 条（每个插件一条"牌与导出的符号一致"）也提前拦住。
- 能禁用也能**卸载**：卸载就是真删那个 dll（连它改名成的 `.off` 一起）。当场删不掉（这个进程已经加载过它 ——
  Qt 插件 unload 之后里面活着的控件就是悬空指针，所以我们不 unload）就记进待删表，下次启动第一件事删掉。
  两种情况界面上说两句不同的话："已卸载" / "已安排卸载 · 下次启动真删掉"。
- **右键那几项跟着「右键动作」插件走**：注册表里那几个静态项不会自己判断，所以"该不该在"= 配置里那个勾（`shell/intent`）**×** 提供者在不在了（`plugins/shell` 装着且没禁用）。卸载或禁用它 → 文件/文件夹右键里的 ltool 当场撤掉；装回来 → 自动写上，勾不用重勾。缺 `shell/intent` 这个键的老机器按注册表现状算（不然下次启动会把人家正在用的菜单悄悄撤掉）。
- **插件商店只有一面列表**，卡片就是条目：没拉清单时列本机 `ltool-plugins\` 里装着的那几个，
  拉过之后以仓库那份 `dist/index.json` 为准（本机装着而清单没列的排在后面，状态词「清单外」）。
  每张卡按自己的状态给按钮，按钮排在**卡片最底下一行**（不和状态词挤同一行）：没装的「装」；装了但哈希不同的「更新」+「禁用」+「卸载」（装了的东西不能因为版本旧就禁不掉、卸不掉）；
  装了的「禁用」+「卸载」；被改名的「启用」+「卸载」。大小一律说 **MB**（两位小数，卡片上不留裸字节数）。点卡片正文 = 装了的打开那张页、没装的就等于按那颗按钮。
- **装插件是排队的，一次只飞一颗**：他连点九颗那次崩了（07:43 的事件日志：`ltool.exe` 挂在 `Qt6Widgets.dll`，
  异常码 0xc0000005；程序日志最后九行是九颗「装好」，紧接着重扫到第 9 条就没了）。九条下载 + 九次改名 + 中途
  重扫/重排叠在一起是这一类的根，所以多出来的那几颗先在卡片上写"排在第几位"，一颗落地接下一颗，队列空了才重扫 + 重排一次。
- **拉插件走三级顺序**（`PluginStore::fetch`，清单和 dll 两段共用这一条）：先镜像前缀 + 原件**走直连**，
  都不成才走「下载代理」那一行，最后是原件直连；每条候选原地试两次（这条线路上"连上但不吐字节"是常态）。
  内置两条是这台机器量过的（`ghfast.top` 0.86s 可用；`gh-proxy.com` 2026-10-15 对这个文件 403，10-11 还是 8.3MB/s ——
  它随时可能换脾气，所以顺序和重试比"选对哪一家"重要）。镜像只在清单地址是 `raw.githubusercontent.com` 时才套，
  指到自己的服务器就不套。配置页「插件镜像前缀」那一行可换；状态行会说出这次**实际走的是哪条、试了几次**。
- `Api` 和主程序不等就拒装，并在界面上说清是哪一侧旧；元数据不齐也是点名说清楚，不默默少一张卡。
- 加一个插件：`plugins/<id>/` 放 `plugin.json` + 源码 + 一层壳（照 `plugins/network/networkplugin.cpp` 抄），
  再在 `CMakeLists.txt` 里 `ltool_add_tool(id IdPlugin 源文件…)` 加一行，并把 `ltool-<id>` 补进下面那条
  `add_dependencies(ltool ...)`（不点名挂上去，`--target ltool` 根本不编它，打包照旧装上一版 —— 2026-10-15
  加 grid 时踩的就是这条），以及 `src/main.cpp` 里 `--plugintest` 那张 `kTools` 名单（少了它，dll 在目录里
  也没人验）。卡片墙、搜索直达、商店全自动认它。
- 第三方仓库：[mateo-ki/ltool_plugin](https://github.com/mateo-ki/ltool_plugin) —— 自带插件的源码 + 独立编 dll 的
  SDK 快照；SDK 由主仓库 `tools/make_sdk.py` 生成，`dist/index.json` 由它的 `tools/make_index.py` 生成。
  那边的 CI 定义在本仓库 `plugin-ci/`（`build.yml` + `ci/annotate-fail.sh`），改完拷进插件仓库的
  `.github/workflows/` 与 `ci/` 再提交 —— 两处都留一份，是为了这条流水线有一处可追溯的来源。

## 常用自测旗标

| 旗标 | 验什么 |
| --- | --- |
| `--smoke` | 无界面一遍过：索引、目录、插件都在 |
| `--plugintest` | 十九个插件逐个验 + 卡片墙张数与几何 + 动作路由 + 挂牌与符号一致（221 条） |
| `--linuxdotest` | 「linux.do 盯帖」离线那半条：解析、命中与去重、前缀拼法、配置夹紧与回读、要不要弹那一句的决定、**分类名参与比对**（没分类表时「福利」这类词永远 0 命中）、页面十件控件可达（44 条，一次网都不上、一个气泡都不弹） |
| `--linuxdonet [秒]` | 真去取一趟：状态行说出走的是直连还是中转、拿到几条、第一趟只播种不弹框、分类表拿到几个，**再拿这一页里帖子最多的那个分类名当关键词必须撞到**（10 条，**要联网，不进 CI**） |
| `--gridtest` | 「图片排版」那张板：格数与列行、收图与拒收、拖动是挪还是换、✕ 只空那一格、合成尺寸与像素、板上预览和出图同一块构图、效果图弹框先看过才落盘、点空格子给它选一张图（64 条，离屏） |
| `--cliptest` | 「剪贴板」那一页：过滤那条纯函数（大小写 / trim / 中文 / 整段而非首行 / 日期 / 图片尺寸都能筛）、**行号 ↔ 存储下标**的映射、每行都对得上自己那条、状态行说实话（没在记录就写「没在记录」）、双击复用 / Delete 删这条 / 清空 三条动作真落到数据上（离屏 56 条 + 5 条要**真剪贴板**（`QT_QPA_PLATFORM=windows`，离屏里点名跳过：offscreen 的 clipboard 是个内存桩，`setText` 不发 `dataChanged`、也不碰真剪贴板，跑在那儿只会假绿）。全程沙箱 APPDATA，不碰你真实的 `clipboard.json`；真剪贴板那一档会短暂动一下剪贴板内容，结尾按原样写回） |
| `--seamtest` | 平台层那两道缝自己（`src/platform/Paths.h`、`Launch.h`）：四个已知目录问得到且两两不同、Windows 上各自落在哪（Desktop / Public Desktop / 用户 Programs / ProgramData Programs）、占位符换成系统里那个**真值**、句子里夹着的也换、认不出的原样留着、没占位符的一字不动、空路径不许启动也不许提权（14 条）。Linux 侧那 14 条走 `wsl -e bash tools/seam_probe.sh`（工程在 Linux 还没编通，先单独编缝） |
| `--storetest` | 离线那半条链：解析清单、比本地、验哈希改名、**更新能盖掉已存在的那颗**、卸载与待删表、镜像→代理→直连的排法（67 条，不联网） |
| `--storenet` | 真拉一次清单并离屏点「刷新清单」，逐张卡核对"状态词 ↔ 那颗按钮"，并验状态行说出走的是哪条，还量了「大小一律 MB」和「按钮排在卡片最底下一行」（17 条，**要联网，不进 CI**） |
| `--storemulti [颗数]` | 连着点九颗「装/更新」再当场把卡片墙拆掉重排 —— 盯的就是"多点几下就闪退"那颗访问违例（回调打到已删的按钮；现在装是排队的，一次只飞一颗）。要联网，且会往运行目录真写 dll，所以 `build/verify/storemulti.ps1` 拿一份临时副本跑；不进 CI |
| `--uninst [id]` | 卸载那条路走到底：先打开那张页（dll 就此映射进进程）再卸载、重排、再切回那张页；另一颗没打开过的直接卸。两种都得活着走完（同样跑在临时副本里，它会真删文件） |
| `--archivetest extract\|make\|probe\|progress` | 压缩包读写、进度与取消 |
| `--capgeo` / `--capflash [次]` | 截图那条几何链 / F1 浮层开合 |
| `--recq` / `--recordui [秒]` | 录屏那两档真进了编码器 / F2 整条链 |
| `--diskui` / `--diskbranch` | 磁盘页整条链 / 边扫边报的数和定稿树一致 |

跑任何一面都**必须有输出**（空输出 + 退出码 0 就是假绿）。全部 43 面、连同各自量到的数字：
[docs/notes.md](docs/notes.md)。

## 配置与数据目录

`%APPDATA%\ltool\`：`config.ini`（配置）、`apps.json`（应用索引）、`usage.json`（使用频率）、
`icons\`、`webs.json`（网站关键字）、`plugins\`（开发环境插件）、`deepindex.dat` + `usnstate.dat`（深索引）、
`backups\`（注册表删除前的 .reg）、`clipimg\`、`ocr\`（取字引擎）、`log.txt`（运行日志）。

截图和录像**默认存在软件目录下**（`captures\`、`recordings\`，不往 C 盘堆）；配置页「数据」可改，
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

## 社区

本开源项目已链接并认可 [LINUX DO 社区](https://linux.do)。
