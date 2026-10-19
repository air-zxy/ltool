#pragma once

#include <QString>
#include <QVector>

// 平台层的一道缝：开始菜单里那些**打包应用**（MSIX / Appx）怎么列出来。
//
// 为什么要单独问系统一句：这类应用在磁盘上**没有 .lnk**，开始菜单是从 `shell:AppsFolder`
// 那个虚拟文件夹列它们的，身份也不是路径而是一串 AUMID。ltool 的应用索引原来只吃
// 桌面 + 开始菜单的 .lnk + 用户自己加的应用目录（见 AppScanner.cpp），所以这一类
// 一颗都进不来 —— 2026-10-19 实测这台机器：开始菜单 312 个应用里有 **43 个是这种**
// （ChatGPT = `OpenAI.Codex_2p2nqsd0c76g0!App`，还有 Claude / Dev Home / Armoury Crate…），
// 他因此报"开始菜单搜得到 ChatGPT，ltool 搜索框搜不到"。
//
// Windows 这一份真去枚举 AppsFolder；POSIX 上没有这个概念，返回空 —— 是"没有这一类"，
// 不是"没做"（和 Launch / Paths 那道缝同一个处理法，不许在这儿假装能扫）。
namespace Platform {

struct PackagedApp {
    QString name;        // 开始菜单里显示的那个名字
    QString aumid;       // OpenAI.Codex_2p2nqsd0c76g0!App
    QString launchPath;  // shell:AppsFolder\<aumid> —— 丢给 ShellExecute("open") 就能起
    QString iconSrc;     // 抽图标的来源（exe/dll 路径），拿不到就留空
    int iconIndex = 0;
};

// 同步枚举（在扫描线程上调用；那个线程已经 CoInitializeEx 过）
QVector<PackagedApp> packagedApps();

} // namespace Platform
