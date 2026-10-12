cmake_minimum_required(VERSION 3.24)

project(ltool VERSION 0.4.0 LANGUAGES CXX)

# ---- MSVC：让一个目标里的所有 .cpp 并行编译（/MP）----
# VS 生成器默认把一个目标里的编译任务排队串行跑：本项目 53 个 TU，改一次 src/Utils.h
# （47 个 TU 包含它）串行要 114.7 秒，并行之后 16.4 秒；全量重建 18.3 秒。
# 不设线程上限，cl.exe 自己按逻辑核数开（本机 20 核 / 47GB 内存，峰值远吃不满）。
# 注：CMake 会把 /MP 翻成 vcxproj 里的 <MultiProcessorCompilation>true</...>，
#     所以在 AdditionalOptions 里 grep "/MP" 是找不到的。
if(MSVC)
    add_compile_options(/MP)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC OFF)   # 本项目没有 .qrc（图标走 resources/ltool.rc）

find_package(Qt6 REQUIRED COMPONENTS Widgets Concurrent Network)

qt_standard_project_setup()

# 图标生成工具（构建期产出 resources/ltool.ico，嵌入 exe 与安装包）
add_executable(genicon tools/genicon.cpp)
target_link_libraries(genicon PRIVATE Qt6::Gui)
set(ICON_FILE ${CMAKE_CURRENT_SOURCE_DIR}/resources/ltool.ico)
add_custom_command(
    OUTPUT ${ICON_FILE}
    COMMAND $<TARGET_FILE:genicon> ${ICON_FILE}
    DEPENDS genicon
    COMMENT "genicon: 生成 ltool.ico"
)
add_custom_target(appicon DEPENDS ${ICON_FILE})

# ============================================================================
# ltoolcore.dll —— 主程序和 DLL 插件共用的一份实现
#
# 为什么要有它：工具页搬进插件之后，"插件里也要用"和"主程序自己也要用"的那部分
# 代码只能有一份，否则同一个后台状态会出现两份（界面里关了广告拦截、主程序那份还在拦）。
# 边界是按引用查出来的：AdBlocker 主程序在后台跑、DevEnv/SilentInstallLog 被配置页/语言页/
# 软件页/设置窗用、RegClean 与 SysTools 被搜索框用、FileTools 被搜索框和广告页用、
# InfoRow 被卸载页用 —— 这些都进 core；只有那 9 个工具页往插件里搬。
#
# 导出方式用 WINDOWS_EXPORT_ALL_SYMBOLS（CMake 扫 obj 自动生成 .def），而不是给
# 每个函数贴 __declspec：本项目跨界的几乎全是自由函数和类成员，自动导出够用；
# 查过 src/*.h 里没有 extern 全局数据，所以"数据导不出来"这个已知限制踩不到。
# ============================================================================
qt_add_library(ltoolcore SHARED
    src/Utils.cpp
    src/EnvPath.cpp
    src/RegClean.cpp
    src/RegistryPage.cpp
    src/UserAccount.cpp
    src/StartupAudit.cpp
    src/StartupPage.cpp
    src/AppScanner.cpp
    src/Pinyin.cpp
    src/SysTools.cpp
    src/FileIndexer.cpp
    src/UsnIndex.cpp
    src/WebShortcuts.cpp
    src/DevEnv.cpp
    src/DownloadManager.cpp
    src/DownloadsPage.cpp
    src/TaskScheduler.cpp
    src/LanguagePage.cpp
    src/SoftwarePage.cpp
    src/WebsitesPage.cpp
    src/TasksPage.cpp
    src/CaptureOverlay.cpp
    src/Anno.cpp
    src/PinWindow.cpp
    src/InstallLogDialog.cpp
    src/SilentInstallLog.cpp
    src/FileTools.cpp
    src/RepairPage.cpp
    src/AdsPage.cpp
    src/AdBlocker.cpp
    src/DiskScan.cpp
    src/DiskPage.cpp
    src/NetworkPage.cpp
    src/JsonTool.cpp
    src/Base64Tool.cpp
    src/ClipboardStore.cpp
    src/ToolsPage.cpp
    src/PluginHost.cpp
    src/Firewall.cpp
    src/InfoRow.cpp
    src/UninstallPage.cpp
    src/ShellMenu.cpp
    src/ShellAction.cpp
    src/FileOps.cpp
    src/Archives.cpp
    src/SevenZip.cpp
    src/Ocr.cpp
    src/OcrPanel.cpp
    src/OcrBundle.cpp
    src/OcrPp.cpp
    src/RecProbe.cpp
    src/Recorder.cpp
    src/RecordUi.cpp
    src/ConfigPage.cpp
    src/SettingsDialog.cpp
    src/SearchWindow.cpp
)
set_target_properties(ltoolcore PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS ON)
# core-api.h 靠它决定是 dllexport 还是 dllimport（只有编 core 这一份目标定义）
target_compile_definitions(ltoolcore PRIVATE LTOOL_CORE_BUILD)

# ---- 预编译头 ----
# 53 个 TU 每个都要啃同一批 Qt 头（QApplication/QWidget/QtConcurrent 展开就是几万行），
# /MP 只能并行"各自啃一遍"，PCH 才是根本不啃。内容见 src/pch.h（故意不含 windows.h）。
# 挂在 INTERFACE 上：exe 和九个插件都复用 core 编好的那一份，不用每个目标各啃一遍。
target_precompile_headers(ltoolcore INTERFACE src/pch.h)

find_package(LibArchive REQUIRED)

# ---- 7z 的写出后端：7-Zip 自己的编码器 ----
# vcpkg 的 7zip 端口打的是官方那个格式库（CPP/7zip/Bundles/Format7zF），只是文件名叫
# 7zip.dll；bit7z 是它的 C++ 封装（静态库）。换掉 libarchive 的理由只有一个：libarchive
# 的 7z 写出器 LZMA1/LZMA2 恒为单线程，而 7-Zip 把输入切块分给多个核。
# 代价：bit7z 里引用到的符号会拉进 7zip.lib（导入库）→ 7zip.dll 得跟 exe 待在一起。
# 它是按需加载的（实测藏掉它：解压/zip/热键全照常，只有「压缩为 .7z」失败并说明原因），
# 所以漏打包不会让程序起不来，但那条功能就是坏的 —— 下面 POST_BUILD 复制它，
# 打包靠 build\Release\* 一起带走，交付闭环里再点名确认一次。
# 许可证：7zip.dll 是 LGPL-2.1（其中 Rar* 那几个文件另带 unRAR 限制条款），bit7z 是 MPL-2.0。
find_package(unofficial-bit7z CONFIG REQUIRED)

# 每个 ltool 目标（core / exe / 插件）都要的这一套，收成一个函数免得三处漂移
function(ltool_target_setup tgt)
    target_include_directories(${tgt} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
    # ONNX Runtime 不链接进 core（它是首次用到时下载到 %APPDATA%\ltool\ocr 的那个 dll，
    # 运行时 LoadLibrary + C API 函数指针），编译期只要这一个头
    target_include_directories(${tgt} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/third_party/onnxruntime)
    if(TARGET LibArchive::LibArchive)
        # 用导入目标：包含目录和 debug/release 的库都由它带
        target_link_libraries(${tgt} PRIVATE LibArchive::LibArchive)
    else()
        target_include_directories(${tgt} PRIVATE ${LIBARCHIVE_INCLUDE_DIR})
    endif()
    target_link_libraries(${tgt} PRIVATE
        Qt6::Widgets
        Qt6::Concurrent
        Qt6::Network
        user32
        shell32
        ole32
        advapi32
        uuid
        runtimeobject
        version
        dwmapi
        rstrtmgr
        iphlpapi
        crypt32
        wintrust
        ws2_32
        winhttp
        mfplat
        mf
        mfreadwrite
        mfuuid
        d3d11
        dxgi
        unofficial::bit7z::bit7z64
    )
    target_compile_definitions(${tgt} PRIVATE
        NOMINMAX
        WIN32_LEAN_AND_MEAN
        UNICODE
        _UNICODE
    )
    if(MSVC)
        target_compile_options(${tgt} PRIVATE /utf-8 /permissive- /W3)
    endif()
    # ---- C++/WinRT：F2 录屏按窗口取帧用的 Windows.Graphics.Capture。
    #      投影头在 Windows SDK 的 cppwinrt 目录里，不在 MSVC 默认包含路径上，要自己加；
    #      RoInitialize / 激活工厂这些符号在 runtimeobject.lib（上面已经链了）----
    if(MSVC)
        foreach(_root "C:/Program Files (x86)/Windows Kits/10" "C:/Program Files/Windows Kits/10")
            set(_inc "${_root}/Include/${CMAKE_VS_WINDOWS_TARGET_PLATFORM_VERSION}/cppwinrt")
            if(CMAKE_VS_WINDOWS_TARGET_PLATFORM_VERSION AND EXISTS "${_inc}")
                target_include_directories(${tgt} PRIVATE "${_inc}")
                break()
            endif()
        endforeach()
    endif()
endfunction()

ltool_target_setup(ltoolcore)

if(MSVC)
    set(_winrt_found FALSE)
    foreach(_root "C:/Program Files (x86)/Windows Kits/10" "C:/Program Files/Windows Kits/10")
        if(CMAKE_VS_WINDOWS_TARGET_PLATFORM_VERSION
           AND EXISTS "${_root}/Include/${CMAKE_VS_WINDOWS_TARGET_PLATFORM_VERSION}/cppwinrt")
            set(_winrt_found TRUE)
            break()
        endif()
    endforeach()
    if(NOT _winrt_found)
        message(FATAL_ERROR
            "找不到 Windows SDK 的 C++/WinRT 头（Include/<版本>/cppwinrt/winrt/windows.graphics.capture.h），"
            "F2 录屏需要它。装一下 Windows 10/11 SDK。")
    endif()
endif()

# ---- 主程序：现在只剩 main.cpp（热键、托盘、单实例、命令行自测入口）----
qt_add_executable(ltool WIN32
    src/main.cpp
    resources/ltool.rc
)
add_dependencies(ltool appicon)
target_link_libraries(ltool PRIVATE ltoolcore)
ltool_target_setup(ltool)

# ---- DLL 插件示例：编到 build\Release\plugins\ 下，主程序运行时扫那个目录 ----
# 装进包里靠 ltool.iss 那条 `Source: "build\Release\*"... recursesubdirs`，不用另加一行。
qt_add_plugin(ltool-base64 CLASS_NAME Base64Plugin)
target_sources(ltool-base64 PRIVATE plugins/base64/base64plugin.cpp)
# 产物先落在 Release 树**外面**：Qt 的插件部署会把依赖倒进目标的输出目录，
# 放进 build\Release 里会被 ltool.iss 那条 `build\Release\*` 一起收进安装包（实测多 58MB）。
set_target_properties(ltool-base64 PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY_RELEASE ${CMAKE_BINARY_DIR}/plugin-build
    LIBRARY_OUTPUT_DIRECTORY_RELEASE ${CMAKE_BINARY_DIR}/plugin-build)
# 只把这一个 dll 拷进 exe 旁边的 ltool-plugins\（Qt 自己占 plugins 这个名字，别去抢）
add_custom_command(TARGET ltool-base64 POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/Release/ltool-plugins
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:ltool-base64>
            ${CMAKE_BINARY_DIR}/Release/ltool-plugins/ltool-base64.dll
    COMMENT "ltool-base64: 拷进 Release/ltool-plugins")
target_include_directories(ltool-base64 PRIVATE src)
target_link_libraries(ltool-base64 PRIVATE Qt6::Widgets)
target_compile_definitions(ltool-base64 PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN UNICODE _UNICODE)
if(MSVC)
    target_compile_options(ltool-base64 PRIVATE /utf-8 /permissive- /W3)
endif()

# ---- Qt 运行库部署：把 exe 所需的 Qt DLL / platform 插件复制到输出目录，
#      避免 "no Qt platform plugin could be initialized" ----
find_program(QT_WINDEPLOYQT_EXE windeployqt
    HINTS "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/tools/Qt6/bin"
)
if(QT_WINDEPLOYQT_EXE)
    # --no-system-d*-compiler：D3Dcompiler_47 / dxcompiler / dxil 一共约 20MB，只有
    #   ANGLE/Qt Quick 编译着色器时才用得到，本项目是 Widgets + 光栅绘制，没人导入它们
    # --exclude-plugins：svg 图标插件（Qt6Svg+qsvg+qsvgicon）和 IoT 触摸插件用不到
    add_custom_command(TARGET ltool POST_BUILD
        COMMAND "${QT_WINDEPLOYQT_EXE}"
                "$<IF:$<CONFIG:Debug>,--debug,--release>"
                --no-translations
                --no-system-d3d-compiler
                --no-system-dxc-compiler
                --no-opengl-sw
                --exclude-plugins qsvg,qsvgicon,qtuiotouchplugin
                "$<TARGET_FILE:ltool>"
        COMMENT "windeployqt: 部署 Qt 运行库到输出目录"
        VERBATIM
    )
else()
    message(WARNING "windeployqt 未找到，exe 目录不会自动部署 Qt 运行库")
endif()

# imageformats/qjpeg.dll 硬导入 jpeg62.dll，但 windeployqt 只管 Qt 自己的产物、
# vcpkg 的 applocal 只跟着 exe 的导入表走 —— 不手动补上，qjpeg 就是个加载不了的死插件
add_custom_command(TARGET ltool POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/bin/jpeg62.dll"
            "$<TARGET_FILE_DIR:ltool>/jpeg62.dll"
    COMMENT "copy jpeg62.dll（imageformats/qjpeg 的依赖）"
    VERBATIM
)

# 7z 压缩的后端：exe 硬导入 7zip.dll，它不在 Qt/vcpkg 的 applocal 名单里会漏出去，
# 复制过来（安装包按 build\Release\* 全盘收，所以这一条同时管打包）
add_custom_command(TARGET ltool POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/bin/7zip.dll"
            "$<TARGET_FILE_DIR:ltool>/7zip.dll"
    COMMENT "copy 7zip.dll（7z 压缩后端）"
    VERBATIM
)

# 7zip.dll 是 LGPL-2.1（里面 Rar* 那几个文件另带 unRAR 限制条款）、bit7z 是 MPL-2.0，
# 动态链接/静态链接都要把许可证文本跟着软件一起给。vcpkg 已经把原文落到
# share/<port>/copyright 里了，直接搬 —— 别自己抄一份（抄的总会和上游不一致）。
add_custom_command(TARGET ltool POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/share/7zip/copyright"
            "$<TARGET_FILE_DIR:ltool>/licenses/7zip-LGPL-2.1-with-unRAR.txt"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/share/bit7z/copyright"
            "$<TARGET_FILE_DIR:ltool>/licenses/bit7z-MPL-2.0.txt"
    COMMENT "copy 7zip / bit7z 许可证原文"
    VERBATIM
)
