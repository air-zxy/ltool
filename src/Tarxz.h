#pragma once

#include "Archives.h"

#include <QString>
#include <QStringList>

// .tar.xz：给"要快、又要别人打得开"的那种压缩。
//
// 为什么不是 .lzx：那个是我们自己造的容器，别人拿到文件只知道"不认识"。
// .tar.xz 是公开格式 —— 7-Zip / WinRAR / Bandizip / macOS / Linux 都能直接开，
// 文件名的后缀本身就说明了它是什么（tar 打包 + xz 压缩）。
//
// 快在哪里：xz 那一步用的是 liblzma 5.4+ 的 lzma_stream_encoder_mt（多线程 LZMA2），
// 而 libarchive 的 7z/xz 写出器全是单线程 —— 这就是我们压 .7z 打不过 7-Zip 的原因。
// 流水线：libarchive 的 tar 写出器（不压缩，只是打包）→ 它的输出回调 → 多线程 xz 编码器
// → 目标文件。中间不落任何临时文件。
namespace Tarxz {

// threads<=0 = 按逻辑核数来（上限 16）
Archive::Result compress(const QStringList& inputs, const QString& destFile, int threads,
                         const Archive::Report& report, const Archive::Cancel& cancel);

} // namespace Tarxz
