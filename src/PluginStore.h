#pragma once

#include <QByteArray>
#include <QObject>
#include <functional>
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

// 「上哪儿拉」这条顺序，三级形状是硬编码的，镜像列表可以在配置里换：
//   1. 镜像前缀 + 原件地址，**走直连**（镜像都是境内 CDN，再套一层代理只会更慢更挂）
//   2. 原件地址，走配置里那条代理（留空 = Windows 系统代理）
//   3. 原件地址，直连
struct Attempt {
    QString url;
    bool useProxy = false;
    QString label;    // 界面上那句"是从哪条拿到的"就靠它
};

QString defaultIndexUrl();        // 内置那条 raw 地址（免登录）
QString mirrorList();             // 配置 store/mirrors，留空 = 内置那两条（这台机器实测过还活着）
QVector<Attempt> attemptsFor(const QString& originUrl);

// 一次拉取的收尾。ok=false 时 err 是最后那条的错误，via 是成功那条的名字，tries 是一共试了几次
struct Fetched {
    QByteArray body;      // 没给 onChunk 时，整份在这里
    bool ok = false;
    QString err;
    QString via;
    int tries = 0;
};

// 按上面那条顺序拉一个 URL，每条候选原地重试两次 —— 这条线路上"连上但不吐字节"是常态，
// 2026-10-05 实测同一 URL 单发 3/8 中、最多三发 8/8 中，只发一次会把还活着的镜像判死。
//   beforeAttempt：每次尝试之前调（流式写盘那路用它把 .part 截断重来）
//   onChunk：收到一段字节；返回 false = 本地写不下去，换下一条
//   onProgress：(已收, 总数)，总数可能是 0（服务器没给 Content-Length）
//   done：一定会回调，不会静默不回
void fetch(const QString& originUrl, QObject* parent,
           const std::function<void()>& beforeAttempt,
           const std::function<bool(const QByteArray&)>& onChunk,
           const std::function<void(qint64, qint64)>& onProgress,
           const std::function<void(const Fetched&)>& done);

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
QString humanSize(qint64 bytes);       // 商店里的大小一律 "0.28 MB" 这种形状

QString sha256Of(const QString& path);   // 读不出来返回空串

// 把已经下好的 .part 落成插件：先验哈希（不对就删掉 .part 并回报），再 Utils::renameStaged。
// 返回空串 = 成功。
QString finishDownload(const QString& partPath, const Entry& e, const QString& dir = QString());

// 启用/禁用一个已装的插件 = 在 `<file>` 和 `<file>.off` 之间改名。返回空串 = 做完了
// （本来就在那个状态也算做完了，note 里说清）。没有"当场卸掉"这条路 —— Qt 插件 unload
// 之后里面活着的控件就是悬空指针，所以界面必须同时说"下次启动生效"。
QString setEnabled(const QString& file, bool on, const QString& dir = QString());

// 卸载 = 把那个文件从目录里真删掉（`<file>` 和它的 `.off` 都删）。返回：
//   ""       已经删掉了
//   "pending" 删不动 —— 这个 dll 本进程已经加载过，Windows 不会让删一个正在用的镜像；
//             这种把它记进待删表，**下次启动第一件事**就是把它删掉（applyPendingRemoves）
//   其它      失败原因（界面上原样说出来）
// 为什么不当场 unload：Qt 插件 unload 之后里面活着的 QWidget/定时器就是悬空指针，
// 而这张卡可能正开着。所以"当场能删的就删，删不动的说清什么时候生效"，不假装已经卸了。
QString uninstall(const QString& file, const QString& dir = QString());
QVector<QString> pendingRemoves();               // 表里排着的文件名
int applyPendingRemoves(const QString& dir = QString());   // 启动时调，返回真删掉了几个

} // namespace PluginStore
