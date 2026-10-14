#pragma once

#include <QByteArray>
#include <QString>
#include <QStringList>
#include <QVector>

// 插件商店：读远端那份 index.json，和 `<exe 目录>\ltool-plugins\` 里现有的 dll 比，
// 分清"能装 / 可更新 / 已装 / 已禁用 / 装不了"，装的时候先下到 `<file>.part`，
// 按清单里那条 SHA256 校验过了才改名进去（半成品不许冒充装好的）。
//
// 为什么这份逻辑在 core 而不在某个插件页：要比的是 PluginHost 扫的那同一个目录、
// 要读的是 core 的配置（索引地址、代理），而插件 dll 不能反过来依赖装它这件事。
//
// 解析 / 比对 / 落盘三段是**纯函数**，联网那两段在界面侧（QNetworkAccessManager 挂在页上，
// 页销毁 reply 一起断）。所以 `--storetest` 能拿临时目录 + 真算出来的哈希整条走完，
// 一次都不联网、也不往屏幕上弹东西。
namespace PluginStore {

struct Entry {
    QString id;
    QString name;
    QString desc;
    QString file;       // 落到 ltool-plugins 里的那个文件名（ltool-<id>.dll）
    QString url;        // 从哪儿下
    QString sha256;     // 清单里那条哈希（小写十六进制）
    QString color;      // "r,g,b"
    qint64 bytes = 0;
    int api = 0;
    QStringList keywords;
    QStringList actions;
};

enum class State {
    Missing,        // 没装：可以直接装
    Update,         // 装了，但哈希和清单不一样
    Installed,      // 装了，且哈希就是清单里那个
    Disabled,       // 装过、被改名成 <file>.off
    ApiMismatch,    // 清单里的 Api 和这台机器的宿主对不上，拒装
    Blocked,        // 条目本身不能用（文件名/path 穿越/哈希不对/不是 https），界面要说清是哪一条
    LocalOnly,      // 本机装着、清单里却没有这一条：能禁用，但谈不上"更新"
};

// 商店那一面显示一行用的东西：远端条目 + 本地比出来的状态 + 给人看的那句话
struct Row {
    Entry remote;
    State state = State::Blocked;
    QString note;
};

QString defaultIndexUrl();        // 内置那条 raw 地址（免登录）
QString indexUrl();               // 配置里 store/indexUrl，留空就用上面那条

QVector<Entry> parseIndex(const QByteArray& json, QStringList* problems);
// 拿一条远端条目和 pluginsDir 比。dir 留空 = 用 PluginHost::pluginsDir()，
// --storetest 就是靠传临时目录把它指到自己的沙盒里。
State compare(const Entry& e, QString* note, const QString& dir = QString());
QVector<Row> rows(const QVector<Entry>& entries, const QString& dir = QString());
// 本机装了、清单里却没列的那些插件也得上那一栏 —— 不然它们在卡片墙上看得见、
// 在这上面却连"禁用"都按不到（清单没这条，按钮也就没处生成）。
// 目录里现在有什么：`*.dll` 算已装、`*.dll.off` 算已禁用。不碰网络、不猜清单。
// 名字/描述/颜色从 PluginHost 扫出来的那份元数据里拿（只读嵌入的 plugin.json，不加载 dll）。
QVector<Row> localRows(const QString& dir = QString());
// 本机装着、清单里却没列的那几个 = localRows 减去清单覆盖的那些文件名。
QVector<Row> localOnly(const QVector<Entry>& listed, const QString& dir = QString());

QString stateWord(State s);            // 界面上那一个词：能装/可更新/已装/已禁用/装不了

QString sha256Of(const QString& path);   // 读不出来返回空串

// 把已经下好的 .part 落成插件：先验哈希（不对就删掉 .part 并回报），再 Utils::renameStaged。
// 返回空串 = 成功。
QString finishDownload(const QString& partPath, const Entry& e, const QString& dir = QString());

// 启用/禁用一个已装的插件 = 在 `<file>` 和 `<file>.off` 之间改名。返回空串 = 做完了
// （本来就在那个状态也算做完了，note 里说清）。没有"当场卸掉"这条路 —— Qt 插件 unload
// 之后里面活着的控件就是悬空指针，所以界面必须同时说"下次启动生效"。
QString setEnabled(const QString& file, bool on, const QString& dir = QString());

} // namespace PluginStore
