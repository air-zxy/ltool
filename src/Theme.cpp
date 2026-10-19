// 一套颜色两个模式。规格在 Theme.h，这儿只放"角色 → 两套取值"那张表和它的用法。
#include "Theme.h"

#include "Utils.h"

#include <QApplication>
#include <QEvent>
#include <QPalette>
#include <QRegularExpression>
#include <QSettings>
#include <QWidget>
#include <QPair>
#include <QVector>

namespace Theme {
namespace {

struct Role { const char* name; const char* dark; const char* light; };

// 弹层那两份 QSS 只在这里存一份：`menuStyle()`/`tipStyle()` 是"当场算一次"，
// `bindMenu()`/`applyToApp()` 走的是同一份 token 串，所以切模式时挂过的那批会跟着重挂。
const char* const kMenuQss =
    "QMenu { background: @field; border: 1px solid @border; border-radius: 8px;"
    " padding: 4px; color: @text; font-size: 10pt; }"
    "QMenu::item { padding: 6px 20px 6px 12px; border-radius: 6px; }"
    "QMenu::item:selected { background: @accent; color: @onAccent; }"
    "QMenu::separator { height: 1px; background: @border; margin: 4px 6px; }";

const char* const kTipQss =
    "QToolTip { background: @field; color: @text;"
    " border: 1px solid @border; padding: 6px 8px; }";

// 日间这一份是 2026-10-20 他挑的 **A 冷灰纸 + 青碧强调色**（第一版是"整块接近纯白"，
// 他回"太白了换一个白色"）。左边那列是夜间、一直以来的样子，一个字符都没动。
// 现算的对比度（WCAG 相对亮度）：正文 #191a1e 压窗口 #e9edf3 = 14.76:1、
// 灰字 #525863 = 6.07:1、白字压主色 #0a6c86 = 6.00:1、启动器那圈描边 #6f7783 压窗口 = 3.84:1，
// 前三条过 AA（4.5:1）、最后一条过 1.4.11 非文本（3:1）。
// "不是一片白纸"靠的是三层分开：窗口 #e9edf3 → 面板 #f4f6fa（1.10:1）→ 输入框 #ffffff（1.07:1）。
// 这三档差得小是故意的 —— 再拉大就要往灰里走（那是他没选的 C 石板灰）。
// 主色两个模式下不同值（夜 #3f6ae0 白字压上去 4.83:1 / 日 #0a6c86 = 6.00:1），
// 就是为了白字压上去两头都够。
const Role kRoles[] = {
    { "window",     "#1b1c20", "#e9edf3" },
    { "base",       "#17181c", "#ffffff" },
    { "alt",        "#232429", "#f4f6fa" },
    { "text",       "#e8ebf2", "#191a1e" },
    { "muted",      "#8b93a1", "#474c56" },
    { "groupText",  "#9aa3b2", "#4a5160" },
    { "accent",     "#3f6ae0", "#0a6c86" },
    { "accentUp",   "#4f79e8", "#0d7d9b" },
    { "accentDown", "#3560cc", "#075a70" },
    { "border",     "#33353d", "#bcc3ce" },
    { "line",       "#2c2d34", "#dfe3ea" },
    { "field",      "#232429", "#ffffff" },
    { "group",      "#1f2025", "#ffffff" },
    { "edit",       "#17181c", "#ffffff" },
    { "button",     "#2b2d34", "#f4f6fa" },
    { "hover",      "#33353d", "#e6eaf1" },
    { "pressed",    "#26272c", "#dbe0e9" },
    { "disabled",   "#5b616e", "#9aa3b2" },
    { "disabledLine", "#4d525d", "#c8cdd6" },
    { "okText",     "#9fe0a8", "#12692b" },
    { "ok",         "#10b981", "#0b7a4b" },
    { "infoText",   "#78c8ff", "#0a6c86" },
    { "borderHi",   "#3a3c45", "#a8b0bd" },
    { "borderHiUp", "#4f5360", "#8f98a8" },
    { "buttonUp",   "#363842", "#e6eaf1" },
    { "buttonDown", "#222328", "#dbe0e9" },
    { "editText",   "#eef1f7", "#191a1e" },
    { "dim",        "#a0a6b2", "#6b7280" },
    { "sectionText","#78808c", "#5f6772" },
    { "badgeBg",    "#5a606c", "#c5cbd5" },
    { "selBg",      "#2c3550", "#cfe3ea" },
    { "good",       "#60be78", "#1f7a3d" },
    { "warn",       "#d8a854", "#8a5b0b" },
    { "bad",        "#e06c6c", "#b3261e" },
    { "mutedBadge", "#8c94a0", "#525863" },
    { "neutralBadge", "#7896d2", "#0a6c86" },
    { "cyan",       "#06b6d4", "#0a6c86" },
    // 启动器那扇是 WA_TranslucentBackground + 无边框 —— "搜索框在哪儿"这件事只由这一圈描边
    // 说给他看，所以 panelEdge 必须和 panel 自己差得开（2026-10-20 他第一句反馈就是
    // "这个白色不好，完全看不清搜索框的边框"：那份底是纯白、描边和夜里共用一颗黑压 26/255，
    // grab() 出来量到 1.51:1，等于没有）。#AARRGGBB 是 QColor 和 QSS 都认的写法，
    // 半透明因此能存进表里，不用在调用点补 alpha。
    // 2026-10-20 他要"搜索框来点透明"：两份都从"几乎不透明"（247 / 255）压到 **204/255 = 80%**。
    // 透多少不是凭感觉挑的 —— 见 `--cfgcheck` 里那条"压到纯白和纯黑两种桌面上，正文还过不过 AA"：
    // 80% 是两个模式**正文**都还能在最坏桌面下过 4.5:1 的档位（夜 7.31:1 / 日 9.30:1）。
    // 代价如实说：灰字（那行 Enter/Esc 提示、结果行的副标题）在同一最坏桌面下只有 3.43:1 / 4.61:1，
    // 夜里那条没到 AA —— 再透明一档它就先撑不住。判据把"灰字 ≥3:1"钉住，不许一路透下去。
    // 而且这是**没有毛玻璃**的透明：底下是壁纸原样，不是模糊过的。要那种就得接 DWM
    // `DwmEnableBlurBehindWindow`，而它配 `WA_TranslucentBackground` 在个别驱动上会把窗画成全黑 ——
    // 那个我离屏看不见，得他亲眼过第一版才敢装。
    // 夜间那两份是这一版一直以来的样子（描边白压 26/255，实测 1.86:1）——
    // 他点的是"这个白色不好"、没说要改夜里，所以值原样搬进来，判据只钉"别再变糊"。
    { "panel",      "#d91c1e24", "#cce9edf3" },
    { "panelEdge",  "#1affffff", "#6f7783" },
    { "rowSel",     "#1cffffff", "#330a6c86" },
    { "hairline",   "#ffffff", "#14182a" },
    // 再往下这几颗的值是 CSS 的 rgba() 写法，QColor 解析不了 —— 只准在 QSS 里写 @name，
    // 自绘要半透明请用 color("角色", alpha)。
    { "tintCyan",   "rgba(6, 182, 212, 0.16)", "rgba(6, 182, 212, 0.12)" },
    { "tintCyanLine", "rgba(6, 182, 212, 0.32)", "rgba(6, 182, 212, 0.40)" },
    { "hoverSoft",  "rgba(255,255,255,26)", "rgba(20,24,40,22)" },
    { "card",       "rgba(255,255,255,0.08)", "rgba(20,24,40,0.05)" },
    { "cardLine",   "rgba(255,255,255,0.12)", "rgba(20,24,40,0.14)" },
    { "scrollHandle", "rgba(255,255,255,60)", "rgba(20,24,40,60)" },
    { "onAccent",   "white", "white" },
};

const char* valueOf(const Role& r)
{
    return current() == Mode::Dark ? r.dark : r.light;
}

// 接住发给这个控件的 ThemeChange，把自己那份带 @token 的 QSS 按新模式重算一遍。
class Binder : public QObject
{
public:
    explicit Binder(const char* qssWithTokens) : m_qss(qssWithTokens) {}

protected:
    bool eventFilter(QObject* watched, QEvent* e) override
    {
        if (e->type() == QEvent::ThemeChange) {
            if (auto* w = qobject_cast<QWidget*>(watched))
                w->setStyleSheet(expand(m_qss.constData()));
        }
        return false;
    }

private:
    QByteArray m_qss;
};

const Role* findRole(const QString& name)
{
    for (const Role& r : kRoles)
        if (name == QLatin1String(r.name))
            return &r;
    return nullptr;
}

} // namespace

Mode current()
{
    QSettings cfg(Utils::dataDir() + "/config.ini", QSettings::IniFormat);
    return cfg.value("ui/theme").toString().compare(QLatin1String("light"), Qt::CaseInsensitive) == 0
               ? Mode::Light : Mode::Dark;
}

const char* name(Mode m)
{
    return m == Mode::Light ? "日间" : "夜间";
}

QString token(const char* role)
{
    const Role* r = findRole(QLatin1String(role));
    return r ? QString::fromUtf8(valueOf(*r)) : QString();
}

QColor color(const char* role)
{
    return QColor(token(role));
}

QColor color(const char* role, int alpha)
{
    QColor c = color(role);
    c.setAlpha(alpha);
    return c;
}

QString expand(const char* qssWithTokens)
{
    QString out = QString::fromUtf8(qssWithTokens);
    // 名字必须整段吃掉再查表：按表顺序 replace 会先替 `@card` 把 `@cardLine` 变成
    // `<card 的值>Line`（`@accent`/`@accentUp`、`@disabled`/`@disabledLine`、
    // `@group`/`@groupText` 是同一对陷阱）。不认识的名字原样留着，好排查。
    static const QRegularExpression re(QStringLiteral("@([A-Za-z][A-Za-z0-9]*)"));
    QVector<QPair<int, QString>> hits;
    auto it = re.globalMatch(out);
    while (it.hasNext()) {
        const QRegularExpressionMatch m = it.next();
        if (findRole(m.captured(1)))
            hits.append({int(m.capturedStart()), m.captured(1)});
    }
    for (auto it2 = hits.rbegin(); it2 != hits.rend(); ++it2)
        out.replace(it2->first, it2->second.size() + 1, token(it2->second.toUtf8().constData()));
    return out;
}

QString expand(const QString& qssWithTokens)
{
    return expand(qssWithTokens.toUtf8().constData());
}

QString menuStyle()
{
    return expand(kMenuQss);
}

QString tipStyle()
{
    return expand(kTipQss);
}

void bind(QWidget* w, const char* qssWithTokens)
{
    w->setStyleSheet(expand(qssWithTokens));
    // 过滤器挂在 w 自己身上：w 析构时 Qt 会自动摘掉，不会留悬空指针。
    auto* b = new Binder(qssWithTokens);
    b->setParent(w);
    w->installEventFilter(b);
}

void bindMenu(QWidget* menu)
{
    bind(menu, kMenuQss);
}

void applyToApp(QApplication& app)
{
    const bool light = current() == Mode::Light;
    QPalette pal = app.palette();
    const QColor window = color("window"), base = color("base"), alt = color("alt");
    const QColor text = color("text"), muted = color("muted"), accent = color("accent");
    pal.setColor(QPalette::Window, window);
    pal.setColor(QPalette::Base, light ? base : color("edit"));
    pal.setColor(QPalette::AlternateBase, alt);
    pal.setColor(QPalette::Text, text);
    pal.setColor(QPalette::WindowText, text);
    pal.setColor(QPalette::Button, color("button"));
    pal.setColor(QPalette::ButtonText, text);
    pal.setColor(QPalette::ToolTipBase, alt);
    pal.setColor(QPalette::ToolTipText, text);
    pal.setColor(QPalette::Highlight, accent);
    pal.setColor(QPalette::HighlightedText, Qt::white);
    pal.setColor(QPalette::PlaceholderText, muted);
    pal.setColor(QPalette::Link, accent);
    for (QPalette::ColorGroup g : { QPalette::Disabled, QPalette::Inactive }) {
        pal.setColor(g, QPalette::Text, muted);
        pal.setColor(g, QPalette::WindowText, muted);
        pal.setColor(g, QPalette::ButtonText, muted);
        pal.setColor(g, QPalette::Highlight, color("border"));
    }
    app.setPalette(pal);
    app.setStyleSheet(tipStyle());
}

void setMode(Mode m)
{
    QSettings cfg(Utils::dataDir() + "/config.ini", QSettings::IniFormat);
    const Mode old = current();
    cfg.setValue("ui/theme", m == Mode::Light ? QLatin1String("light") : QLatin1String("dark"));
    cfg.sync();
    if (m == old)
        return;   // 配置页构造时给"当前那档"打勾也会走这儿：值没变就别空转一遍全场广播
    if (auto* app = qobject_cast<QApplication*>(QCoreApplication::instance())) {
        applyToApp(*app);
        // 样式表挂在**具体控件**身上（Theme::bind 挂的那批），改 qApp 传导不下去，
        // 而顶层窗口之外没人会自动收到通知 —— 所以顶层和它的子控件都要发一遍。
        // 没接这个事件的控件走 QWidget 默认处理（不重 polish、不重画），代价只是一次空转。
        for (QWidget* w : app->topLevelWidgets()) {
            QEvent ev(QEvent::ThemeChange);
            QCoreApplication::sendEvent(w, &ev);
            const auto kids = w->findChildren<QWidget*>();
            for (QWidget* k : kids)
                QCoreApplication::sendEvent(k, &ev);
        }
    }
}

} // namespace Theme
