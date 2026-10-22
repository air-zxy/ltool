# -*- coding: utf-8 -*-
# 把项目记忆里 ".lzx = 多线程压缩那条路" 那条改掉：格式被用户否了（别人拿到不认识），
# 代码已删，但量出来的结论要留下，不然下一个人又会去试 zstd-in-7z。
import io
import os
import sys

P = os.path.expanduser(r"~\.qoder-cn\projects\D--project-cpp-ltool\memory\ltool-verify-and-deploy.md")
Q = chr(34)   # 双引号：正文字符串用单引号写，里面才能放引号
s = io.open(P, encoding="utf-8").read()
start = s.find('- **' + chr(96) + '.lzx' + chr(96) + ' = 多线程压缩那条路')
if start < 0:
    print("没找到那条，不动文件")
    sys.exit(1)
end = s.find("\n- ", start + 5)
old = s[start:end]
new = (
    "- **" + chr(96) + ".lzx" + chr(96) + " 这类自己造容器的路已经走过并否掉（2026-10-11）**："
    "用 liblzma 的 lzma_stream_encoder_mt 确实能做到 0.48 秒 / 25.7MB（同一个 32MB 文件夹："
    "zip 0.73 秒 / 27.1MB，我们的 .7z 10.1 秒 / 25.1MB），但用户一句「大家看不懂 lzx 是什么文件」"
    "就把它毙了 —— **要给出去的东西必须是标准格式**，别为 0.25 秒造私有容器，代码已全删"
    "（连 vcpkg.json 里的 liblzma、右键那一项、--xzspeed 一起删）。另外两条同样堵死的路别再试："
    "zstd 塞进 7z 的包连本机 7-Zip 25.01 都报 Method = 04F71101 / Unsupported Method；"
    "zip 里指定 zstd 会被 7-Zip **静默退回 Deflate**（产物字节和 -mm=Deflate 完全一样，不报错），"
    "所以" + Q + "zip + zstd 多线程" + Q + "根本不存在。"
)
s = s[:start] + new + s[end:]
io.open(P, "w", encoding="utf-8", newline="\n").write(s)
print("替换了 %d 字符的旧条目" % len(old))
