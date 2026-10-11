#pragma once

#include <QVector>

#include "AppScanner.h"

// Windows 内置面板/工具（控制面板、MSC 管理单元、系统 exe、ms-settings 设置页）。
// 以 AppEntry 形式提供，category = "system"，仅供内存索引（不写入 apps.json）。
namespace SysTools {

QVector<AppEntry> systemTools();

} // namespace SysTools
