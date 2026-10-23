#pragma once

#include <QStringList>
#include <QtGlobal>

// 平台层第五道缝：进程和它占的内存。三个问题各有一个平台答案，
// 其中"把工作集还给系统"这一件只有 Windows 做得到 —— 别在别的平台上假装做过。
namespace Platform {

// 杀掉这个进程**和它全部子孙**（winget 只是壳，真正在装的是它拉起的 msiexec/setup.exe，
// 只杀父进程的话"取消"是假动作）。返回是不是至少杀掉了一个；已提权的子进程杀不动，
// 返回 false 让调用方如实说"没杀掉，请手动"。
bool killProcessTree(qint64 pid);

// 当前进程的工作集 / 常驻内存（MB），-1 = 读不到。诊断"唤出慢"用。
int workingSetMB();

// 让系统立刻回收本进程工作集（任务管理器里那个"内存"当场掉下去）。
// 注意两件事：① 这会让下一次交互把所有页换回来，别在常驻的热键路径上用它；
// ② POSIX 上没有对等机制 → 返回 false，调用方要说"这个平台上做不到"，不许骗人。
bool trimWorkingSet();

// 现在正在跑的进程的**映像名**（`WeChat.exe` 这种，带后缀、原样大小写），按名字去重后排好序。
// 给「广告弹窗 → 编辑规则 → 从正在跑的进程里挑一个忽略」那张表用；
// 规整成名单里那行字（小写、去 .exe）不是这颗的事，那是 `AdBlocker::allowProcFor`。
// **列不出来就返回空** —— 调用方要说"这台机器列不出进程"，不许把空表当成"没有进程在跑"。
QStringList runningImageNames();

} // namespace Platform
