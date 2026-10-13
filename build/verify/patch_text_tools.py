import io, re

ROOT = r"D:\project\cpp\ltool\plugins"

def rd(p):
    return io.open(p, encoding="utf-8").read()

def wr(p, t):
    io.open(p, "w", encoding="utf-8", newline="\n").write(t)

for tid, cls, input_member, recalc in (("json", "JsonTool", "m_edit", "reparse(true)"),
                                       ("base64", "Base64Tool", "m_input", "recompute()")):
    cp = "%s\\%s\\%s.cpp" % (ROOT, tid, cls)
    hp = "%s\\%s\\%s.h" % (ROOT, tid, cls)
    c, h = rd(cp), rd(hp)
    if "void setText(const QString& text);" in h:
        print("skip (already patched)", tid)
        continue

    # 1) 那个"复用同一个窗口"的单例没用了：工具现在是卡片墙里的一张页，生命周期归宿主
    m = re.search(r"\nvoid %s::open\(const QString& \w+\)\n\{.*?\n\}\n" % cls, c, re.S)
    assert m, cp
    c = c[:m.start()] + "\nvoid %s::setText(const QString& text)\n" \
        "{\n    %s->setPlainText(text);\n    %s;\n" \
        "    Utils::log(QStringLiteral(\"%s: 收到 %%1 个字符\").arg(text.size()));\n}\n" \
        % (cls, input_member, recalc, cls.lower()) + c[m.end():]
    swin = "QPointer<%s> s_win;\n" % cls
    assert c.count(swin) == 1, cp
    c = c.replace(swin, "")

    # 2) 头文件：私有构造 + static open() → 公有构造 + setText（插件壳要 new 它、喂它）
    old_pub = ("    static void open(const QString& %s = QString());   // 复用同一个窗口（不叫 show：会遮住 QWidget::show）\n"
               % ("text" if tid == "json" else "input")
               if tid == "json" else
               "    static void open(const QString& input = QString());\n")
    assert c.count(old_pub) == 0 and h.count(old_pub) == 1, hp
    ctor = "    explicit %s(QWidget* parent = nullptr);" % cls
    assert h.count(ctor) == 1, (hp, h.count(ctor))
    h = h.replace(old_pub, "    // 把一段文本接进来（搜索框/剪贴板那条「在工具里打开这段」）\n"
                           "    void setText(const QString& text);\n\n"
                           "    explicit %s(QWidget* parent = nullptr);\n" % cls)
    # 原来的 private: 段里那行构造已经搬到 public，删掉
    old_priv = "private:\n    explicit %s(QWidget* parent = nullptr);\n" % cls
    assert h.count(old_priv) == 1, (hp, "ctor still private")
    h = h.replace(old_priv, "private:\n")
    # 类注释里"独立窗口/入口有两个"那套说法跟着改
    h = h.replace("// 入口有两个：托盘菜单「JSON 格式化」和启动器命令 json。",
                  "// 作为 DLL 插件的一张内嵌页出去（工具页卡片墙 + 搜索框直达），不再有独立窗口。")
    h = h.replace("// Base64 编解码工具：和 JsonTool 同一套用法（独立窗口，托盘菜单/设置菜单/命令 b64 进入）。",
                  "// Base64 编解码工具：和 JsonTool 同一套用法，作为 DLL 插件的内嵌页出去。")
    wr(cp, c)
    wr(hp, h)
    print("patched", tid)
