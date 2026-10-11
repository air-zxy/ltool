#pragma once

#include <QString>
#include <QStringList>
#include <QVector>

// 搜索框的"网站关键字"：输入 `ld` 直接打开 linux.do，输入 `gh qt6` 提供"在 GitHub 搜 qt6"。
// 列表存在 %APPDATA%\ltool\webs.json（一个 JSON 数组），首次运行写两份默认值，用户可增删。

// 一条网站关键字。它放在命名空间外面：调用处（SearchWindow）一直是裸写 WebEntry 的。
struct WebEntry {
    QStringList keys;       // 触发关键字，一律存小写（搜索框是拿小写首词来比对的）
    QString name;           // 显示名
    QString url;            // 直接打开的地址
    QString searchUrl;      // 可空；里面有 %s，会被换成百分号编码后的查询词
};

namespace WebShortcuts {

// webs.json 不存在就写默认那份（只在启动时调一次）
void ensureDefaultWebs();

QVector<WebEntry> loadWebs();
bool saveWebs(const QVector<WebEntry>& webs);

// "这串东西像不像一个网址"（像就直接给"在浏览器打开"，不再当成本机结果）
bool looksLikeUrl(const QString& text);

// 补上 https://、去掉首尾空白 —— 标题和实际打开的地址都用它，两边必须一模一样
QString normalizeUrl(const QString& text);

// 没命中任何结果时那条"用搜索引擎搜 xxx"用的地址，跟着配置页的 web/engine 走。
// **必须对同一个查询返回同一个串**：调用处拿它和已渲染的兜底行比字符串来定位插入点。
QString engineSearchUrl(const QString& query);

} // namespace WebShortcuts
