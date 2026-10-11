#pragma once

#include "Archives.h"

#include <QString>
#include <QStringList>

// .lzx：多线程 LZMA2 压缩的容器。
//
// 为什么要自己包一层：追平 7-Zip 的速度靠的是 liblzma 5.4+ 的 lzma_stream_encoder_mt
// （多线程 LZMA2），但它只能产出 .xz 这一个**单流**容器 —— .xz 里没有文件名/目录结构的位置。
// 所以 .lzx = [头部][清单 JSON][一条标准的 .xz 流（所有文件内容首尾相接）]。
// 中间那段是**货真价实的 xz 流**，xz -d / 7-Zip 都能直接解出来（拿到的是拼起来的数据 + 清单）。
// 多线程是按 Block 分活的，所以块大小按"每个线程约 2 个块"算，不按文件切块
// （实测每个文件做一次 FULL_FLUSH 会把十几个小文件退化成串行）。
//
// 已知代价：解的时候要把整包还原进内存，所以超过 kExtractCap 的输入直接拒掉，
// 那种体积用 .zip/.7z（它们是边解边写的）。
namespace Xz {

// 解包时允许吃进内存的上限（超过就拒绝，改用 zip/7z）
constexpr qint64 kExtractCap = 1536LL * 1024 * 1024;

bool looksLike(const QString& path);

// threads<=0 = 按逻辑核数来（上限 16，再多 liblzma 也是白占内存）
Archive::Result compress(const QStringList& inputs, const QString& destFile, int threads,
                         const Archive::Report& report, const Archive::Cancel& cancel);
Archive::Result extract(const QString& archivePath, const QString& destDir,
                        const Archive::Report& report, const Archive::Cancel& cancel);

} // namespace Xz
