# 一次性整理（2026-10-15）：README 的「功能」那张表被写成一整墙字，他嫌密。
# 做法：功能那一节换成一行一条的总览；原来每个单元格里那些**实测数字、判据旗标、踩过的坑**
# 整段原样搬到文末「功能细节与实测」—— 一个字都不删，只是不再糊在门口。
import io

P = 'README.md'
lines = io.open(P, encoding='utf-8', newline='').read().split('\n')

start = lines.index('| 功能 | 说明 |')
assert lines[start + 1] == '| --- | --- |'

# 一行可能跨多行（F2 那一格就是这么写长的），直到某行以 " |" 收尾才算完
i = start + 2
chunks = []
while i < len(lines) and lines[i].startswith('| '):
    cur = lines[i]
    while not cur.rstrip().endswith('|'):
        i += 1
        cur += '\n' + lines[i]
    chunks.append(cur)
    i += 1
end = i

parsed = []
for c in chunks:
    parts = c.split('\n')
    head = parts[0]
    assert head.startswith('| ') and '|' in head[2:], head[:60]
    name = head[2:head.index('|', 2)]
    rest = [head[head.index('|', 2) + 1:].strip()] + [p.strip() for p in parts[1:]]
    detail = ' '.join(rest)
    assert detail.endswith('|'), detail[-40:]
    detail = detail[:-1].strip()
    assert detail and not detail.startswith('|'), (name, detail[:40])
    parsed.append((name.strip(), detail))

print('rows: %d' % len(parsed))
for n, d in parsed:
    print('  %-24s %d 字' % (n, len(d)))

SUMMARY = [
 ('应用快速启动', '扫桌面与开始菜单的快捷方式，加上你指定的应用目录；拼音全拼和首字母都能搜，按使用频率排序。'),
 ('全局呼出', '`Alt+Space` 在屏幕中上方弹搜索框，再按一次收起；被占用时自动降级。'),
 ('搜索', '输入即搜：应用 / 文件 / 网站关键字三路同时出，全角先折半角。'),
 ('文件搜索', '常用目录实时监听（秒级）+ 全盘快照（管理员下 USN 增量约 0.5 秒）。'),
 ('网站直达', '`gh` / `ld` / `gg` 直接开浏览器；带上参数就站内搜索。'),
 ('开发环境初始化', '`init java` 这类命令检测组件，缺的按 Enter 用 winget 装。'),
 ('管理 PATH', '`path add <目录>` 改用户 PATH，写完广播 WM_SETTINGCHANGE。'),
 ('注册表整理', '`reg` 体检卸载残留 / 失效启动项 / MuiCache，删之前自动备份 .reg。'),
 ('配置页面', '左侧菜单 + 右侧内容：语言、软件、网站、定时任务、清理、网络都在这一扇里。'),
 ('F1 截图', '全屏框选、悬停吸附到窗口、标注、置顶、图上划词取字（`plugins/capture`）。'),
 ('F2 录屏', '录整块屏幕或某一个窗口，出普通 mp4，画质和画面两档可选（`plugins/record`）。'),
 ('修复', '检测 .NET / VC++ / DirectX / UCRT，未装的一键 winget 安装。'),
 ('广告弹窗', '按标题和进程扫弹窗，可关闭 / 结束进程 / 加拦截名单，勾了每 60 秒自动拦。'),
 ('C盘清理', '可清理位置 + 所选盘的占用分析（矩形树图）；边扫边出数，每个盘留着结果。'),
 ('Windows 用户名', '先把账户现状只读摆出来，改显示名 / 改账户名 / 新建用户都要人在界面上按下。'),
 ('关于（菜单第 8 行 + 托盘「关于」）', '版本号 + 检查更新 / 下载 / 装并重启三颗按钮，一件都不自动。'),
 ('网络', '网卡 DNS 一键切预设，提权执行，含刷新缓存和恢复 DHCP。'),
 ('文件占用 / 粉碎', '右键看谁占用着它、能结束那个进程；粉碎是三轮覆写后永久删除，不进回收站。'),
 ('压缩包读写', '右键解压 / 压缩：读靠 libarchive 十几种格式，写 .7z 用 7-Zip 自己的多线程编码器。'),
 ('其他', '托盘图标、单实例、空查询显示常用应用、运行日志。'),
]
names_in_table = [n for n, _ in parsed]
missing = [n for n in names_in_table if n not in [x for x, _ in SUMMARY]]
extra = [n for n, _ in SUMMARY if n not in names_in_table]
assert not missing and not extra, (missing, extra)
# 顺序照原表走，别打乱他熟悉的那份排列
order = {n: k for k, n in enumerate(names_in_table)}
SUMMARY.sort(key=lambda t: order[t[0]])

new_table = ['| 功能 | 一句话 |', '| --- | --- |']
new_table += ['| %s | %s |' % (n, s) for n, s in SUMMARY]
new_table += ['',
              '每一项的实测数字、无界面判据（`--capgeo` / `--recq` / `--diskui` 那些）和踩过的坑，',
              '整段挪到了文末的 **功能细节与实测** —— 门口这一张只说"是什么"。',
              '界面背后是十二个 DLL 插件共用一份 `ltoolcore.dll`，见「工具插件」那一节。']

appendix = ['## 功能细节与实测', '',
            '上面那张表只回答"有什么功能"。这一节按功能记着**为什么这么做、实测是多少、拿哪面旗标验**，',
            '是原来那张功能表里的字，整段搬下来的，一条没删。', '']
for n, d in parsed:
    appendix += ['### %s' % n, '', d, '']

out = lines[:start] + new_table + lines[end:]
idx = out.index('## 社区')
out = out[:idx] + appendix + out[idx:]
io.open(P, 'w', encoding='utf-8', newline='').write('\n'.join(out))
print('README %d -> %d 行' % (len(lines), len(out)))
