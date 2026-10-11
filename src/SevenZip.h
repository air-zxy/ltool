#pragma once

#include "Archives.h"

#include <QString>
#include <QStringList>

// 7z 的写出后端：直接用 7-Zip 自己的编码器。
// vcpkg 的 7zip 端口打出来的就是官方那个格式库（CPP/7zip/Bundles/Format7zF，
// 只是文件名叫 7zip.dll），上面套一层 bit7z 当 C++ 接口。
//
// 为什么要换掉 libarchive：它 7z 写出器的 LZMA1/LZMA2 恒为单线程（threads 那个选项
// 只管 zstd），而 7-Zip 自己会把输入切成块分给多个核 —— 同一个 32MB 的 exe，
// 我们 9.6 秒，7-Zip 20 线程 1.6 秒（实测）。
//
// 代价说清楚：软件多带一个 1.8MB 的 7zip.dll，它得和 exe 待在一起（bit7z 是运行期
// 按路径 LoadLibrary 它的，objdump 查过：不在 exe 的导入表里，所以缺了不会让程序
// 起不来 —— 实测缺它时解压/zip/其它功能照跑，只有这一条压缩要给出清楚的失败）。
// CMake 的 POST_BUILD 把它复制进输出目录，打包靠 build\Release\* 一起带走。
//
// 读 7z 没换：还是 libarchive，解压那条不受影响。
namespace SevenZip {

// 把 inputs（文件或目录）打成 7z 写到 destFile。
// level 是 7-Zip 的档位，只认它自己定义的那几档：0/1/3/5/7/9（5 = 7-Zip 的默认档）。
// 先落 .part，整包成功才改名成 destFile；report/cancel 和 Archive::compress 同义。
Archive::Result compress(const QStringList& inputs, const QString& destFile, int level,
                         const Archive::Report& report, const Archive::Cancel& cancel);

} // namespace SevenZip
