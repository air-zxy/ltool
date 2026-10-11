# 把 main.cpp 里 --xzspeed 那一整段自测删掉（.lzx 这个格式被否了：别人拿到文件
# 不知道是什么，而且 zip 本来就已经是最快的那条）。
# 用法：python build/verify/strip_xzspeed.py
import io
import sys

P = "src/main.cpp"
s = io.open(P, encoding="utf-8").read()
start = s.find("    // --xzspeed")
if start < 0:
    print("没找到 --xzspeed 的开头，可能已经删过了")
    sys.exit(0)
end = s.find("    // --ocrtest", start)
if end < 0:
    print("没找到 --ocrtest 的边界，先别动文件")
    sys.exit(1)
removed = s[start:end].count("\n")
s = s[:start] + s[end:]
io.open(P, "w", encoding="utf-8", newline="\n").write(s)
print("删掉 %d 行" % removed)
