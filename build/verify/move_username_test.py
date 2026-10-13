import io, re, textwrap

M = r"D:\project\cpp\ltool\src\main.cpp"
lines = io.open(M, encoding="utf-8").read().split("\n")

# --username 那一段：597..745（1 基）。601 是 if 行，745 是收尾的 }
assert lines[596].strip().startswith("// --username"), repr(lines[596])
assert lines[600].strip() == 'if (args.contains("--username")) {', repr(lines[600])
assert lines[744] == "    }", repr(lines[744])

body_raw = "\n".join(lines[601:720])       # 判据本体
tail_raw = "\n".join(lines[720:745])       # 工具表那段断言 + 汇总，留在主程序里换个说法
assert "工具页那张卡片" in tail_raw and "内嵌页数量" in tail_raw

# 本体原来缩进在 if {} 里（8 空格），搬进函数体只要 4 空格
body = "\n".join(l[4:] if l.startswith("    ") else l for l in body_raw.split("\n"))

flag = '    // --username：验「工具 → Windows 用户名」这一页的判据。页面搬进 DLL 插件之后，\n' \
       '    // 判据跟着页一起进插件（主程序链接不到 UserAccountPage），这儿只负责点名跑它。\n' \
       '    // **一条都不真的去改账户/建账户** —— 那是不可逆级别的动作，只能在界面上由人按下。\n' \
       '    if (args.contains("--username"))\n' \
       '        return runPluginSelftest(QStringLiteral("user"));'
lines[596:745] = flag.split("\n")
txt = "\n".join(lines)

helper = '''// 跑某个插件自带的判据（插件里导出 extern "C" int ltoolPluginSelftest()）。
// 页面代码搬进 DLL 之后主程序链接不到它内部的测试函数，只能这样点名要。
// 找不到符号**必须算失败**，不能当通过 —— 否则"插件没加载起来"会伪装成"测试全绿"。
int runPluginSelftest(const QString& id)
{
    using Fn = int (*)();
    const auto fn = reinterpret_cast<Fn>(PluginHost::selftestEntry(id));
    if (!fn) {
        std::printf("FAIL %s：没有可用的判据入口（插件没加载起来，或没导出 ltoolPluginSelftest）\\n",
                    id.toUtf8().constData());
        std::printf("%s: 失败 1 项\\n", id.toUtf8().constData());
        return 1;
    }
    const int bad = fn();
    std::printf("%s: 失败 %d 项\\n", id.toUtf8().constData(), bad);
    std::fflush(stdout);
    return bad == 0 ? 0 : 1;
}

'''
anchor = "int runAdScan()\n{"
assert txt.count(anchor) == 1
txt = txt.replace(anchor, helper + anchor)

old_rowmenu = '''    if (args.contains("--rowmenu")) {
        const int bad = diskRowMenuSelftest();
        std::printf("rowmenu: 失败 %d 项\\n", bad);
        return bad == 0 ? 0 : 1;
    }'''
assert txt.count(old_rowmenu) == 1, "rowmenu 块没匹配上"
txt = txt.replace(old_rowmenu, '''    if (args.contains("--rowmenu"))
        return runPluginSelftest(QStringLiteral("disk"));''')
txt = txt.replace("所以判据放在 DiskPage.cpp 里、这里只负责跑）。",
                  "所以判据放在插件里 DiskPage.cpp，这里只负责点名跑它）。")

for h in ('#include "Base64Tool.h"\n', '#include "DiskPage.h"\n', '#include "UserAccount.h"\n',
          '#include "JsonTool.h"\n'):
    assert txt.count(h) == 1, h
    txt = txt.replace(h, "")
io.open(M, "w", encoding="utf-8", newline="\n").write(txt)

head = '''// 「Windows 用户名」这一页的判据。
//
// 为什么在插件里而不是 main.cpp：页面类 UserAccountPage 和 UserAccount 那几个拼脚本的函数
// 现在都是这个插件的内部实现，主程序链接不到它们。宿主通过壳里那个 extern "C" 的
// ltoolPluginSelftest 把这段跑一遍（见 src/PluginHost.h 的 selftestEntry）。
#include "UserAccount.h"

#include <QCoreApplication>
#include <QFileInfo>
#include <QLabel>
#include <QLayout>
#include <QSet>
#include <QString>
#include <QThread>
#include <QWidget>

#include <cstdio>
#include <functional>

int userPageSelftest()
{
'''
foot = '''
    return bad;
}
'''
out = head + body + foot
io.open(r"D:\project\cpp\ltool\plugins\user\userselftest.cpp", "w",
        encoding="utf-8", newline="\n").write(out)

# 工具表那段断言（embedded 槽位）现在归 --plugintest 管，先存起来给下一步用
io.open(r"D:\project\cpp\ltool\build\verify\username_tail.txt", "w",
        encoding="utf-8", newline="\n").write(tail_raw)
print("main.cpp 现在的行数:", txt.count("\n"), " userselftest 行数:", out.count("\n"))
