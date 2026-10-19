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

// 日间这一份是 2026-10-20 他挑的 **P1 = iOS 设置页那套「浅灰页面 + 纯白卡片 + 发丝线」**。
// 前面两版都被他否了：第一版"整块接近纯白"（他："太白了换一个白色"）、第二版 A 冷灰纸
//（他："太丑了太难受了"，多半是 `#e9edf3` 那颗带蓝味 —— 蓝减红 +10）。
// 苹果那套的关键不是把底压深，是**中性**（页面 `#f2f2f7` 蓝减红只有 +5）加分组层次：
// 页面 `#f2f2f7` → 卡片 `#ffffff`（1.12:1）→ 发丝线 `#d1d1d6`。
// 现算的对比度（WCAG 相对亮度）：正文 #1d1d1f 压白卡 = 16.83:1、次级 #6e6e73 = 5.07:1、
// 白字压主色 #0066CC = 5.57:1、启动器那圈描边 #868a90 压页面 = 3.11:1。
// **一句要说清的**：苹果自己的规范不过 AA —— iOS 的 secondaryLabel 是 rgba(0,0,0,0.5)
// = 压白底 3.95:1，白字压 iOS 蓝 #007AFF = 4.02:1。所以这两颗没照抄，按 AA 调深了
//（次级用 #6e6e73、主色用官网那颗链接蓝 #0066CC）。`dim` 是唯一保留苹果原味的
// 低对比档（#a1a1a6 = 2.57:1），因为它画的是**按不动的非可执行行** —— 禁用态按 WCAG 免检。
// 夜间那一列是这一版一直以来的样子，一个字符都没动。
const Role kRoles[] = {
    { "window",     "#1b1c20", "#f2f2f7" },
    { "base",       "#17181c", "#ffffff" },
    { "alt",        "#232429", "#f2f2f7" },
    { "text",       "#e8ebf2", "#1d1d1f" },
    // 日间这三档在苹果那套里本来就是一档（secondaryLabel），所以并成一个值；
    // 夜里那三档差得开（#8b93a1 / #9aa3b2 / #78808c），保留各自的名字不动。
    { "muted",      "#8b93a1", "#6e6e73" },
    { "groupText",  "#9aa3b2", "#6e6e73" },
    { "accent",     "#3f6ae0", "#0066cc" },
    { "accentUp",   "#4f79e8", "#0072ce" },
    { "accentDown", "#3560cc", "#0058b0" },
    { "border",     "#33353d", "#d1d1d6" },
    { "line",       "#2c2d34", "#e5e5ea" },
    { "field",      "#232429", "#ffffff" },
    { "group",      "#1f2025", "#ffffff" },
    { "edit",       "#17181c", "#ffffff" },
    { "button",     "#2b2d34", "#f2f2f7" },
    { "hover",      "#33353d", "#e8e8ed" },
    { "pressed",    "#26272c", "#dedee3" },
    { "disabled",   "#5b616e", "#a1a1a6" },
    { "disabledLine", "#4d525d", "#d1d1d6" },
    { "okText",     "#9fe0a8", "#12692b" },
    { "ok",         "#10b981", "#0b7a4b" },
    { "infoText",   "#78c8ff", "#0066cc" },
    { "borderHi",   "#3a3c45", "#b8b8be" },
    { "borderHiUp", "#4f5360", "#a1a1a6" },
    { "buttonUp",   "#363842", "#e8e8ed" },
    { "buttonDown", "#222328", "#dedee3" },
    { "editText",   "#eef1f7", "#1d1d1f" },
    { "dim",        "#a0a6b2", "#a1a1a6" },
    { "sectionText","#78808c", "#6e6e73" },
    { "badgeBg",    "#5a606c", "#6e6e73" },
    { "selBg",      "#2c3550", "#d1e3f6" },
    { "good",       "#60be78", "#1f7a3d" },
    { "warn",       "#d8a854", "#8a5b0b" },
    { "bad",        "#e06c6c", "#b3261e" },
    { "mutedBadge", "#8c94a0", "#6e6e73" },
    { "neutralBadge", "#7896d2", "#0066cc" },
    { "cyan",       "#06b6d4", "#0a6c86" },
    // 启动器那扇是 WA_TranslucentBackground + 无边框 —— "搜索框在哪儿"这件事只由这一圈描边
    // 说给他看，所以 panelEdge 必须和 panel 自己差得开（2026-10-20 他第一句反馈就是
    // "这个白色不好，完全看不清搜索框的边框"：那份底是纯白、描边和夜里共用一颗黑压 26/255，
    // grab() 出来量到 1.51:1，等于没有）。#AARRGGBB 是 QColor 和 QSS 都认的写法，
    // 半透明因此能存进表里，不用在调用点补 alpha。
    // 中间试过一档"真透下去"（日 80% / 夜 85%，拿纯白纯黑两种极端桌面算出来的），
    // 他看完说"先把透明干掉" —— 所以两份都回到不透（夜里那颗 247/255 是这一版一直以来的值，
    // 不是 255；那是它本来的样子，不是残留的透明档）。
    { "panel",      "#f71c1e24", "#fff2f2f7" },
    { "panelEdge",  "#1affffff", "#868a90" },
    { "rowSel",     "#1cffffff", "#2e0066cc" },
    { "hairline",   "#ffffff", "#14182a" },
    // 再往下这几颗的值是 CSS 的 rgba() 写法，QColor 解析不了 —— 只准在 QSS 里写 @name，
    // 自绘要半透明请用 color("角色", alpha)。
    { "tintCyan",   "rgba(6, 182, 212, 0.16)", "rgba(6, 182, 212, 0.12)" },
    { "tintCyanLine", "rgba(6, 182, 212, 0.32)", "rgba(6, 182, 212, 0.40)" },
    { "hoverSoft",  "rgba(255,255,255,26)", "rgba(20,24,40,22)" },
    { "card",       "rgba(255,255,255,0.08)", "#ffffff" },
    { "cardLine",   "rgba(255,255,255,0.12)", "#d1d1d6" },
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
