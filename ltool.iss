; ltool Inno Setup 打包脚本
; 编译: "D:\software\InnoSetup6\ISCC.exe" ltool.iss
; 产物: installer\ltool-0.4.0-setup.exe（按用户安装到 %LOCALAPPDATA%\Programs\ltool，无需管理员）

#define MyAppName "ltool"
#define MyAppVersion "0.4.0"
#define MyAppPublisher "ltool"
#define MyAppExeName "ltool.exe"

[Setup]
AppId={{B7E6F3A1-2C48-4D95-9A21-5F0C8D6E13A7}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppPublisher={#MyAppPublisher}
DefaultDirName={localappdata}\Programs\{#MyAppName}
DisableProgramGroupPage=yes
PrivilegesRequired=lowest
OutputDir=installer
OutputBaseFilename=ltool-{#MyAppVersion}-setup
SetupIconFile=resources\ltool.ico
; 压缩档位（2026-10-11 实测，payload 74MB）：
;   lzma2/max   9.7 秒 / 21.80 MB   —— 默认，交付用的就是这个
;   lzma2/normal 6.5 秒 / 22.53 MB
;   lzma2/fast  2.4 秒 / 26.95 MB   —— 只想快速出一个能装的包时：ISCC.exe /DQUICK ltool.iss
; CompressionThreads 对 max 档实测无效（1/16 线程产物字节完全相同），所以没加。
#ifdef QUICK
Compression=lzma2/fast
#else
Compression=lzma2/max
#endif
SolidCompression=yes
WizardStyle=modern
UninstallDisplayIcon={app}\{#MyAppExeName}
CloseApplications=yes
ChangesEnvironment=no

[Languages]
Name: "chinesesimplified"; MessagesFile: "compiler:Languages\ChineseSimplified.isl"

[Tasks]
Name: "desktopicon"; Description: "创建桌面快捷方式"; \
    GroupDescription: "附加图标："
Name: "autostart"; Description: "开机自动启动 ltool"; \
    GroupDescription: "其他：" ; Flags: unchecked

[Files]
Source: "build\Release\{#MyAppExeName}"; DestDir: "{app}"; Flags: ignoreversion
; "=" 是历史上一次 shell 重定向打错产生的垃圾文件；genicon.exe 是构建期生成图标的工具，
; 都别跟着 * 一起进包
; qjp2/qwebp/qtiff 是死插件：它们硬导入 jasper.dll / libwebp*.dll / tiff.dll，而这些
; 依赖在 vcpkg 树里根本没装，插件永远加载不了，干净系统上还会弹"缺少 xxx.dll"
; D3Dcompiler_47/dxcompiler/dxil/Qt6Svg/qsvg/qsvgicon/qtuiotouchplugin 见 CMakeLists 里
; windeployqt 的 --no-system-d*-compiler / --exclude-plugins，这里是挡历史遗留副本的第二道
; captures / recordings：截图/录像的默认落点就是 exe 旁边这两个文件夹，本机自测要是
; 手滑在 build\Release 里跑过一次 F1，别让那几张图跟着安装包出去
Source: "build\Release\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs; \
    Excludes: "=,genicon.exe,captures,recordings,D3Dcompiler_47.dll,dxcompiler.dll,dxil.dll,Qt6Svg.dll,qsvg.dll,qsvgicon.dll,qtuiotouchplugin.dll,qjp2.dll,qwebp.dll,qtiff.dll"

; VC++ 运行库（vcruntime140/msvcp140*）故意不打包：Qt6Core 等确实硬导入它们，但
; 缺了是"进程根本起不来"，而能弹出设置页的机器一定有；ISCC 是 32 位进程，
; System32 路径还会被重定向到 SysWOW64（那里没有 vcruntime140_1.dll）。
; 真遇到"找不到 MSVCP140.dll"的机器，再改用 {#WinDir}\Sysnative\ 取 64 位副本。

; 老版本装出去的无用文件：Inno 不会自动删掉已经不在包里的东西，得点名清理，
; 否则升级完安装目录还是 90MB
[InstallDelete]
Type: files; Name: "{app}\D3Dcompiler_47.dll"
Type: files; Name: "{app}\dxcompiler.dll"
Type: files; Name: "{app}\dxil.dll"
Type: files; Name: "{app}\Qt6Svg.dll"
Type: files; Name: "{app}\imageformats\qsvg.dll"
Type: files; Name: "{app}\iconengines\qsvgicon.dll"
Type: files; Name: "{app}\generic\qtuiotouchplugin.dll"
Type: files; Name: "{app}\genicon.exe"
Type: files; Name: "{app}\="
Type: files; Name: "{app}\imageformats\qjp2.dll"
Type: files; Name: "{app}\imageformats\qwebp.dll"
Type: files; Name: "{app}\imageformats\qtiff.dll"

[Icons]
Name: "{autoprograms}\{#MyAppName}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"
Name: "{autodesktop}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"; Tasks: desktopicon

[Registry]
Root: HKCU; Subkey: "Software\Microsoft\Windows\CurrentVersion\Run"; ValueType: string; \
    ValueName: "ltool"; ValueData: """{app}\{#MyAppExeName}"""; Flags: uninsdeletevalue; Tasks: autostart

[Run]
Filename: "{app}\{#MyAppExeName}"; Description: "{cm:LaunchProgram,{#MyAppName}}"; \
    Flags: nowait postinstall skipifsilent

[UninstallRun]
; 退出托盘实例由 CloseApplications/系统重启管理器处理，无需额外命令

[Messages]
SelectDirDesc=ltool 安装位置
SelectDirLabel3=安装程序将把 ltool 安装到以下文件夹。
WelcomeLabel2=这将安装 ltool——uTools 风格的快速启动器：%nAlt+Space 呼出搜索框，应用/文件/网站直达，开发环境与定时任务管理。
