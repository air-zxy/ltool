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

// 日间那一份是按 WCAG 对过的（2026-10-20 现算）：正文 #1b1c20 压 #f2f3f6 = 15.34:1、
// 灰字 #5b616e = 5.60:1、输入框底上正文 = 17.02:1、白字压主色 #2f56c9 = 6.37:1，
// 全部过 AA（4.5:1）。夜间那一份是原来就有的：正文 14.26:1、灰字 5.50:1、
// 白字压主色 #3f6ae0 = 4.83:1。主色在两个模式下不同值，就是为了白字压上去都够。
const Role kRoles[] = {
    { "window",     "#1b1c20", "#f2f3f6" },
    { "base",       "#17181c", "#ffffff" },
    { "alt",        "#232429", "#f6f7fa" },
    { "text",       "#e8ebf2", "#1b1c20" },
    { "muted",      "#8b93a1", "#5b616e" },
    { "groupText",  "#9aa3b2", "#4a5160" },
    { "accent",     "#3f6ae0", "#2f56c9" },
    { "accentUp",   "#4f79e8", "#3a63d6" },
    { "accentDown", "#3560cc", "#264aad" },
    { "border",     "#33353d", "#d6d9e0" },
    { "line",       "#2c2d34", "#e3e5ea" },
    { "field",      "#232429", "#ffffff" },
    { "group",      "#1f2025", "#ffffff" },
    { "edit",       "#17181c", "#ffffff" },
    { "button",     "#2b2d34", "#e9ebf0" },
    { "hover",      "#33353d", "#dfe2e9" },
    { "pressed",    "#26272c", "#d3d7e0" },
    { "disabled",   "#5b616e", "#9aa3b2" },
    { "disabledLine", "#4d525d", "#cdd1da" },
    { "okText",     "#9fe0a8", "#12692b" },
    { "ok",         "#10b981", "#0b7a4b" },
    { "infoText",   "#78c8ff", "#1d5fbf" },
    { "borderHi",   "#3a3c45", "#c3c8d2" },
    { "borderHiUp", "#4f5360", "#a9b0be" },
    { "buttonUp",   "#363842", "#dde0e7" },
    { "buttonDown", "#222328", "#cfd4de" },
    { "editText",   "#eef1f7", "#15171c" },
    { "dim",        "#a0a6b2", "#5b616e" },
    { "sectionText","#78808c", "#6b7280" },
    { "badgeBg",    "#5a606c", "#c2c8d2" },
    { "selBg",      "#2c3550", "#d3ddf5" },
    { "good",       "#60be78", "#1f7a3d" },
    { "warn",       "#d8a854", "#8a5b0b" },
    { "bad",        "#e06c6c", "#b3261e" },
    { "mutedBadge", "#8c94a0", "#5b616e" },
    { "neutralBadge", "#7896d2", "#2f56c9" },
    { "cyan",       "#06b6d4", "#0a6c86" },
    // 下面这几个只给 QSS 用（`@name`）：值是 CSS 的 rgba() 写法，QColor 解析不了，
    // 自绘代码要半透明请用 color("角色", alpha)，所以这两类都必须是 #rrggbb。
    // 启动器那扇是 WA_TranslucentBackground + 无边框 —— "搜索框在哪儿"这件事只由这一圈
    // 描边说给他看，所以 panelEdge 必须和 panel 自己差得开（2026-10-20 他第一句反馈就是
    // "这个白色不好，完全看不清搜索框的边框"：日间那份底是**纯白**、描边是黑压 26/255，
    // grab() 出来量到 1.51:1，等于没有）。#AARRGGBB 是 QColor 和 QSS 都认的写法，
    // 半透明因此能存进表里，不用在调用点补 alpha。
    // 夜间那一份是这一版一直以来的样子（白压 26/255，实测 1.86:1）—— 他没说要改，
    // 所以值原样搬进来，只把"别再变差"钉进判据；日间这颗 #848c9a 压 #f7f8fa = 3.15:1，
    // 过 WCAG 1.4.11 那条非文本对比度 ≥3:1。
    { "panel",      "#f71c1e24", "#fff7f8fa" },
    { "panelEdge",  "#1affffff", "#848c9a" },
    { "rowSel",     "#1cffffff", "#4d7f96c8" },
    { "hairline",   "#ffffff", "#14182a" },
    { "good",       "#60be78", "#1f7a3d" },
    { "warn",       "#d8a854", "#8a5b0b" },
    { "bad",        "#e06c6c", "#b3261e" },
    { "mutedBadge", "#8c94a0", "#5b616e" },
    { "neutralBadge", "#7896d2", "#2f56c9" },
    { "cyan",       "#06b6d4", "#0a6c86" },
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
