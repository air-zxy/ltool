// 预编译头：这 53 个 TU 里绝大多数都要的 Qt/标准库头。
// 实测（2026-10-12，本机 20 核 /MSVC 19.44）加与不加的时间见 README「构建」。
//
// 故意**不放 windows.h**：33 个文件要它，但各文件对它的依赖不一样（录屏那几个要
// C++/WinRT 的头、USN 那几个要 winioctl.h），放进 PCH 等于把这些顺序约束强加给
// 全部 TU；而 windows.h 本身不是这里的大头，Qt 那几个模块头才是。
#pragma once

#include <algorithm>
#include <functional>

#include <QApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QHash>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QSettings>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QUrl>
#include <QVector>
#include <QVBoxLayout>
#include <QWidget>

#include <QtConcurrent/QtConcurrentRun>
