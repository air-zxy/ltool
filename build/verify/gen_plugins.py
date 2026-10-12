import io, json, re, os

# 唯一来源：上一版（repo 里那份，D: 的已经在同步时被改过之前）ToolsPage.cpp 的 makeTools()。
# 名字/描述/关键词/颜色全部从那儿解析，不是我重新打一遍 —— 手打就会漂，
# 而 plugin.json 和插件壳里那五个 override 说的是同一件事，漂了没人发现。
SRC = r"C:\Users\loukas\ltool-work\repo\src\ToolsPage.cpp"
ROOT = r"D:\project\cpp\ltool"

txt = io.open(SRC, encoding="utf-8").read()
body = txt.split("QVector<ToolDef> makeTools()", 1)[1]
entries = re.findall(
    r'QStringLiteral\("([^"]*)"\),\s*'          # name
    r'QStringLiteral\("([^"]*)"\),\s*'          # desc
    r'\{(.*?)\},\s*'                            # keywords
    r'QColor\((\d+), (\d+), (\d+)\), (-?\d+)\}',  # color + embedded
    body, re.S)
assert len(entries) == 9, len(entries)

# embedded 索引 → 插件 id / 目录 / 壳类名 / 页类名 / 头文件
TOOLS = {
    0:  ("repair",   "RepairPlugin",    "RepairPage",    "RepairPage.h"),
    1:  ("ads",      "AdsPlugin",       "AdsPage",       "AdsPage.h"),
    2:  ("disk",     "DiskPlugin",      "DiskPage",      "DiskPage.h"),
    3:  ("network",  "NetworkPlugin",   "NetworkPage",   "NetworkPage.h"),
    4:  ("registry", "RegistryPlugin",  "RegistryPage",  "RegistryPage.h"),
    5:  ("startup",  "StartupPlugin",   "StartupPage",   "StartupPage.h"),
    6:  ("user",     "UserPlugin",      "UserAccountPage", "UserAccount.h"),
    -1: ("json",     "JsonPlugin",      "JsonTool",      "JsonTool.h"),
    -2: ("base64",   "Base64Plugin",    "Base64Tool",    "Base64Tool.h"),
}

def qLiteral(s):
    return 'QStringLiteral("%s")' % s.replace('\\', '\\\\').replace('"', '\\"')

# 工具自己带的判据：搬进插件之后主程序够不着了，靠一个 extern "C" 的导出符号交给宿主跑
# 值是"要调的函数"，前面那串是它在壳里可见所需的额外声明（同目录的头文件里没有时）
SELFTEST = {"disk": ("diskRowMenuSelftest()", ""),
            "user": ("userPageSelftest()", "int userPageSelftest();   // 判据本体在同目录 userselftest.cpp\n")}
# 能把一段文本接进去的工具（原来那条 JsonTool::open(text) / Base64Tool::open(input) 的路）
FEED = {"json": ("setText", "m_page"), "base64": ("setText", "m_page")}

for name, desc, kwraw, cr, cg, cb, emb in entries:
    emb = int(emb)
    tid, cls, page, header = TOOLS[emb]
    kws = re.findall(r'QStringLiteral\("([^"]*)"\)', kwraw)
    assert kws, name
    d = os.path.join(ROOT, "plugins", tid)
    os.makedirs(d, exist_ok=True)

    meta = {"Id": tid, "Name": name, "Desc": desc, "Api": 2,
            "Keywords": kws, "Color": "%s,%s,%s" % (cr, cg, cb)}
    io.open(os.path.join(d, "plugin.json"), "w", encoding="utf-8",
            newline="\n").write(json.dumps(meta, ensure_ascii=False, indent=1) + "\n")

    feed = ""
    if tid in FEED:
        meth, _ = FEED[tid]
        feed = ('    bool feedText(const QString& text) override\n'
                '    {\n'
                '        if (!m_page)\n'
                '            return false;      // 页还没建，宿主先 createPage 再喂\n'
                '        m_page->%s(text);\n'
                '        return true;\n'
                '    }\n' % meth)

    selftest = ""
    if tid in SELFTEST:
        call, decl = SELFTEST[tid]
        selftest = ('\n// 这个工具自己的判据。主程序不再链接页面代码，所以测试跟着页一起搬进来，\n'
                    '// 由 --plugintest 通过 QPluginLoader::resolve 找到这个符号来跑。\n'
                    '%s'
                    'extern "C" Q_DECL_EXPORT int ltoolPluginSelftest()\n'
                    '{\n    return %s;\n}\n' % (decl, call))
    pageptr = "    %s* m_page = nullptr;\n" % page if tid in FEED else ""
    store = ("        m_page = p;\n" if tid in FEED else "")
    create = ('    QWidget* createPage(QWidget* parent) override\n'
              '    {\n' if tid in FEED else
              '    QWidget* createPage(QWidget* parent) override { return new %s(parent); }\n' % page)
    create_tail = ('        auto* p = new %s(parent);\n'
                   '        m_page = p;\n'
                   '        return p;\n'
                   '    }\n' % page) if tid in FEED else ""

    members = create + create_tail + feed + pageptr
    src = ('// ltool 工具插件：%s\n'
           '//\n'
           '// 页面本体是 %s（从 src 搬过来的），这个壳只报元数据、建那张页。\n'
           '// plugin.json 里那份 Name/Desc/Keywords 和下面的 override 必须一致，\n'
           '// --plugintest 会把两边都对一遍。\n'
           '#include "ltool-plugin.h"\n'
           '#include "%s"\n'
           '\n'
           '#include <QObject>\n'
           '#include <QtPlugin>\n'
           '\n'
           'class %s : public QObject, public LToolPlugin\n'
           '{\n'
           '    Q_OBJECT\n'
           '    Q_INTERFACES(LToolPlugin)\n'
           '    Q_PLUGIN_METADATA(IID LTOOL_PLUGIN_IID FILE "plugin.json")\n'
           '\n'
           'public:\n'
           '    int apiVersion() const override { return LTOOL_PLUGIN_API_VERSION; }\n'
           '    QString id() const override { return QStringLiteral("%s"); }\n'
           '    QString name() const override { return %s; }\n'
           '    QString desc() const override { return %s; }\n'
           '    QStringList keywords() const override\n'
           '    {\n'
           '        return { %s };\n'
           '    }\n'
           '%s'
           '};\n'
           '%s'
           '#include "%splugin.moc"\n'
           % (name, page, header, cls, tid, qLiteral(name), qLiteral(desc),
              ", ".join(qLiteral(k) for k in kws), members, selftest, tid))
    io.open(os.path.join(d, "%splugin.cpp" % tid), "w", encoding="utf-8",
            newline="\n").write(src)
    print("%-9s %-14s kw=%d %s" % (tid, name, len(kws), desc[:20]))
