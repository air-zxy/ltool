# 把 README 里 .lzx 那段换成"为什么快的那条就是 zip、7z 为什么追不上"的实测结论。
# 写成文件而不是 bash heredoc：heredoc 里的反斜杠会被 python 改义，本项目摔过两次。
import io
import sys

P = "README.md"
OLD = (r"写只写 zip、7z 和 .lzx（多线程 LZMA2，见下）。**要快就压 .lzx**：右键「压缩为 .lzx（多线程快）」，"
       r"同一个 32MB 的文件夹实测 —— .lzx 0.48 秒 / 25.7MB，我们的 .7z 10.1 秒 / 25.1MB，"
       r"7-Zip 自己的 .7z 1.69 秒 / 24.6MB，也就是**比 7-Zip 还快 3.5 倍**、包只大 4.6%。"
       r"为什么 7z 那条追不上：libarchive 的 7z 写出器 LZMA 恒为单线程（它的 `threads` 只作用于 zstd），"
       r"而 .lzx 是直接用已经链接进来的 liblzma 5.8.3 的 `lzma_stream_encoder_mt` 写的。"
       r"代价要说清楚：**.lzx 是我们自己的容器**（头部 + JSON 清单 + 一条货真价实的 .xz 流），"
       r"只有装了 ltool 的人能一键解，别人得手敲 `xz -d` 再按清单切；给别人传的包还是用 zip。"
       r"解包要一次把整包还原进内存，所以超过 1.5GB 的输入直接拒掉并提示改用 zip/7z。")
NEW = (r"写只写 zip 和 7z。**要快就用 zip**：同一个 32MB 的文件夹实测 —— 我们的 .zip 0.73 秒 / 27.1MB"
       r"（比 7-Zip 自己压 zip 的 0.96 秒还快），我们的 .7z 10.1 秒 / 25.1MB。"
       r"**7z 那条追不上 7-Zip，原因量清楚了**：libarchive 的 7z 写出器 LZMA1/LZMA2 恒为单线程，"
       r"而 7-Zip 靠 `-mmt` 把 LZMA2 分块丢给多个核 —— 同一个包 7-Zip 单线程 7.4 秒、20 线程 1.6 秒，"
       r"所以差距是线程数不是编码器。试过又否掉的两条：把 zstd 塞进 7z（`threads` 唯一支持的编码器）能到 0.3 秒，"
       r"但产出的包连本机 7-Zip 25.01 都报 `Method = 04F71101 / Unsupported Method`；"
       r"自己包一层 .lzx（清单 + 标准 .xz 流，用 liblzma 的 `lzma_stream_encoder_mt`）实测 0.48 秒 / 25.7MB 最快，"
       r"但**别人拿到 .lzx 根本不知道是什么文件**，所以删了。zip 里指定 zstd 也不行：7-Zip 会静默退回 Deflate。")

s = io.open(P, encoding="utf-8").read()
if s.count(OLD) != 1:
    print("命中 %d 次，不是 1 次，先不动文件" % s.count(OLD))
    sys.exit(1)
io.open(P, "w", encoding="utf-8", newline="\n").write(s.replace(OLD, NEW))
print("README 已改")
