# 一次性整理第二步（2026-10-15）：`## 使用` 里那条"命令行参数"把 34 面自测旗标串成了一整段墙。
# 换成一面旗标一行的表，原来那段（实测数字、期望值为什么改过、反向验怎么做到的）**整段搬进文末**，
# 一个字不删 —— 他要的是门口那张别密密麻麻，不是要我把证据丢了。
import io
import re

P = 'README.md'
text = io.open(P, encoding='utf-8', newline='').read()

head = '- 命令行参数：'
start = text.index(head)
end = text.index('\n\n## 网站关键字')
para = text[start:end]
# 段落里夹着一行续行（`--recordui` 那条跨了两行），拼起来再切
flat = ' '.join(para.split('\n'))

SHORT = {
 '--show': '启动即把搜索框显示出来，不是只进托盘。',
 '--smoke': '无界面把索引、目录、插件都点一遍。',
 '--mfttest': '诊断 MFT 枚举（要管理员）。',
 '--searchtime': '`--searchtime 词1 词2`：深索引搜索的耗时 + 前 3 条结果。',
 '--searchdump': '`--searchdump 词1 词2`：每个词前 20 条**按顺序整条**打出来，改搜索实现就拿它做 diff。',
 '--disktime': 'C 盘上 A/B 两种磁盘遍历（DiskScan vs QDirIterator）的耗时。',
 '--diskscale': '拿环境变量 `LTOOL_DISKTHREADS` 把同一棵树按 1/2/4/8/16 线程各扫一遍，量扩展性。',
 '--diskbranch': '4 条：边扫边报的那几个分支数，和最后定稿的树是不是同一回事。',
 '--diskui': '7 条：磁盘页整条链（点「整理」→ 途中就出行 → 定稿 → 换盘让位 → 换回来吃缓存）。慢，要真扫一整盘。',
 '--plugintest': '十二个插件逐个验「元数据 → 真加载 → 建页 → 没显示 → 缓存命中 → 两边一致」+ 卡片墙张数与几何 + 动作路由（现在 136 条）。',
 '--username': '27 条：「Windows 用户名」那一页，**一次都不真的改账户 / 建账户**。',
 '--rowmenu': '9 条：磁盘页两栏列表的右键该弹什么、点开的是不是这一行自己的目录。',
 '--archivetest': '`--archivetest extract|make|probe|progress`：压缩包读写回归；`make` 第 5 个参数是 7z 档位 0-9。',
 '--pinyin': '33 条：拼音表和码点必须对齐、多音字、全拼与首字母，最后真建一个不显示的窗口往输入框里打字。',
 '--archspeed': '`--archspeed [每文件KB] [文件数]`：量压缩包读写吞吐，并把 7z 的 1/3/5/7/9 档整条曲线跑一遍，每档都验「我们能解回来 + `7z t` 退出码 0」。',
 '--ocrtest': '`--ocrtest <图片>`：打印图里认出的文字和耗时。',
 '--ocrlayout': '`--ocrlayout <图片>`：打印每行每词的包围盒和切出来的字格（划词的坐标就靠它验）。',
 '--pinpick': '`--pinpick <图片>`：图上划词 9 条（把鼠标事件直接送进一个不显示的置顶窗，**不合成鼠标**），跑完归还剪贴板。',
 '--recprobe': '`--recprobe [毫秒]`：录屏之前先量地基 —— 系统里有哪些编码器、MP4 写 30 帧再读回来要多久、按窗口取帧能不能用。',
 '--recq': '13 条：录屏那两档（画质 / 画面）真的进了编码器，缩完不许把画面翻过来。',
 '--rectest': '`--rectest [秒] [宽x高]`：录一张自造的会动的卡 → 把 mp4 解回来数帧，末尾再录 1.5 秒整块屏幕。',
 '--recordui': '`--recordui [秒]`：33 条，F2 那条链整条走一遍（列表 → 悬浮条计数 → 程序化按「停止」→ 解出帧 → 「不要了」→ 选择窗四条退路）。',
 '--pindel': '`--pindel <图片>`：10 条，右键往期缩略图删那一张（`APPDATA` 挪进临时目录，不碰你真实的往期）。',
 '--ocrpanel': '`--ocrpanel <图片>`：真开一次「提取文字」结果窗（要人关掉才算跑完）。',
 '--ocrget': '只把 OCR 引擎那三个文件下载好并校验 / 解包（新机器补下载用的入口）。',
 '--ocrchain': '`--ocrchain <图片>`：11 条，从「一个文件都没下」跑到当场认得出那三行字；会真下 26MB，但落在临时沙箱里。',
 '--memloop': '`--memloop [轮数] [宽x高]`：每轮解压一个压不动的 32MB 包 + 录一段指定尺寸的窗，逐步打印私有字节 / 工作集 / 句柄数。',
 '--toggle': '6 条：按 `Alt+Space` 到底出不出来（冷启动跑，会把启动器真弹出来一两秒）。',
 '--pathcfg': '9 条：配置页那两行「存放目录」真的管用（沙箱 `APPDATA`）。',
 '--appdirs': '12 条：应用目录那条过滤（PE 子系统、助手名、版本号/运行时目录、同目录已有主程序）。',
 '--capgeo': '7 条：缩放不是 100% 时截不全全屏那条几何链（真屏 + 一张自造的 DPR=1.5 画布，100% 的机器上照样能抓到这个错）。',
 '--cfgcheck': '离屏建一次配置页，数每一行在不在、那两颗按钮是不是**各只有一颗**、每个分组框能不能从布局走到。',
 '--pintool': '19 条几何断言：置顶窗那两个「收起」开关（离屏，一次都不 `show()`）。',
 '--capflash': '`--capflash [次]`：把 F1 浮层真开一次（每下不到半秒）再走和 Esc 一样的取消路径，量「在不在、铺没铺满、压没压在最上」。',
}

segs = re.split(r'(?<=[；;])', flat)
found = []
for s in segs:
    m = re.search(r'`(--[a-z]+)', s)
    if m:
        found.append(m.group(1))
for f in found:
    assert f in SHORT, f
# SHORT 里可以多一面旗（--plugintest 原来写在「工具插件」那一节，不在段落的清单里）
print('段落里有、表里没的：', [f for f in found if f not in SHORT])
print('表里有、段落里没的：', [f for f in SHORT if f not in found])

appendix = ['## 自测旗标逐条', '',
            '上面那张表只写"这面旗标验什么"。下面这段是前一版 README 里「命令行参数」那**一整条**，',
            '原样搬过来的 —— 每一面旗标量到的数、期望值为什么改过、反向验怎么做到的，都在里面。', '',
            para[len(head):].strip()]

table = ['- 命令行参数 / 无界面自测旗标。跑任何一面都**必须有输出**（空输出 + 退出码 0 = 假绿），',
         '  要在事件循环里跑的那两面绕开单实例守卫：', '',
         '| 旗标 | 一句话 |', '| --- | --- |']
for f in SHORT:
    table.append('| `%s` | %s |' % (f, SHORT[f]))
table += ['',
          '每一面旗标量到的数字、期望值为什么改过、反向验是怎么做到的，原样记在文末 **自测旗标逐条**。']

out = text[:start] + '\n'.join(table) + text[end:]
idx = out.index('## 社区')
out = out[:idx] + '\n'.join(appendix) + '\n\n' + out[idx:]
io.open(P, 'w', encoding='utf-8', newline='').write(out)
print('flags: %d, README %d -> %d 行'
      % (len(SHORT), len(text.split('\n')), len(out.split('\n'))))
