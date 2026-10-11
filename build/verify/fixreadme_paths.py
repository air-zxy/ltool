# 把 README 里"截图/录像默认在 %APPDATA%"的说法改成"默认在软件目录下"。
# 写成文件而不是 bash heredoc：heredoc 里的反斜杠转义会被 python 警告并静默改义，
# 本项目在同样的坑上摔过两次。
import io
import sys

P = "README.md"
SUBS = [
    (r"完成后自动复制到剪贴板并存到截图目录（默认 `%APPDATA%\ltool\captures`，配置页「数据」里可改）",
     r"完成后自动复制到剪贴板并存到截图目录（**默认在软件目录下：`D:\software\ltool\captures`**，"
     r"不往 C 盘的 `%APPDATA%` 堆；配置页「数据」里可改，装到写不了的只读位置时自动退回数据目录）"),
    (r"存到录像目录（默认 `%APPDATA%\ltool\recordings\rec-时间戳.mp4`，配置页「数据」里可改）",
     r"存到录像目录（**默认在软件目录下：`D:\software\ltool\recordings\rec-时间戳.mp4`**，"
     r"不在 C 盘；配置页「数据」里可改）"),
    (r"captures=             ; 截图（含往期）存放到哪；留空 = %APPDATA%\ltool\captures",
     r"captures=             ; 截图（含往期）存放到哪；留空 = 软件目录下的 captures（exe 旁边）"),
    (r"recordings=           ; 录屏产物放到哪；留空 = %APPDATA%\ltool\recordings",
     r"recordings=           ; 录屏产物放到哪；留空 = 软件目录下的 recordings（exe 旁边）"),
    (r"| `captures\` | 截图产物：",
     r"| `captures\`（**默认在软件目录下**，不在 C 盘） | 截图产物："),
    (r"| `recordings\` | F2 录屏的 MP4（`rec-时间戳.mp4`）。**存放位置可在配置页改** |",
     r"| `recordings\`（**默认在软件目录下**，不在 C 盘） | F2 录屏的 MP4（`rec-时间戳.mp4`）。"
     r"**存放位置可在配置页改** |"),
]

s = io.open(P, encoding="utf-8").read()
for old, new in SUBS:
    n = s.count(old)
    if n != 1:
        print("命中 %d 次，不是 1 次：%s" % (n, old[:60]))
        sys.exit(1)
    s = s.replace(old, new)
io.open(P, "w", encoding="utf-8", newline="\n").write(s)
print("README 改了 %d 处" % len(SUBS))
