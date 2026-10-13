import io

p = r"D:\project\cpp\ltool\CMakeLists.txt"
txt = io.open(p, encoding="utf-8").read()
lines = txt.split("\n")

# 起点：旧 qt_add_executable 残留的源列表第一行（新的 core 列表里 RegClean 前面还有 Utils/EnvPath）
starts = [i for i, l in enumerate(lines) if l == "    src/RegClean.cpp"]
assert len(starts) == 2, starts          # 一处在新 core 列表，一处在残留块
start = starts[1]
assert lines[start - 1].strip() == "", repr(lines[start - 1])
assert "ltool_target_setup(ltool)" in lines[start - 2], repr(lines[start - 2])

# 终点：残留块最后一条 if(MSVC) target_compile_options(ltool ...) endif()
old_opt = [i for i, l in enumerate(lines) if l.strip().startswith("target_compile_options(ltool PRIVATE")]
assert len(old_opt) == 1, old_opt
end = old_opt[0] + 1                      # 它的下一行是 endif()
assert lines[end] == "endif()", repr(lines[end])
assert lines[end + 1].strip() == "", repr(lines[end + 1])
assert "插件" in lines[end + 2], repr(lines[end + 2])

del lines[start - 1 : end + 1]

txt = "\n".join(lines)
anchor = "    src/AdBlocker.cpp\n"
assert txt.count(anchor) == 1, txt.count(anchor)
txt = txt.replace(anchor, "    src/AdsPage.cpp\n" + anchor)

# 残留块里那些只服务旧 ltool 的行不能留下重复的 find_package
assert txt.count("find_package(LibArchive REQUIRED)") == 1, txt.count("find_package(LibArchive REQUIRED)")
assert txt.count("find_package(unofficial-bit7z CONFIG REQUIRED)") == 1
assert "target_precompile_headers(ltool " not in txt
assert txt.count("qt_add_executable(ltool WIN32") == 1

io.open(p, "w", encoding="utf-8", newline="\n").write(txt)
print("ok lines=%d" % txt.count("\n"))
