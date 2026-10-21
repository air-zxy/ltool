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
    QString file;       // 落到 ltool-plugins 里的那个文件名（后缀按平台：ltool-<id>.dll / libltool-<id>.so）
    QString url;        // 从哪儿下
    QString sha256;     // 清单里那条哈希（小写十六进制）
    QString color;      // "r,g,b"
    qint64 bytes = 0;
    int api = 0;
    QString version;    // 插件版本号，如 "1.0.0"（由清单给出）
    QStringList keywords;
    QStringList actions;
};

enum class State {
    Missing,        // 没装：可以直接装
    Update,         // 装了，而且**清单的版本比本机新**（哈希不同只是必要条件，不是"有更新"）
    Installed,      // 装了，且哈希就是清单里那个
    Disabled,       // 装过、被改名成 <file>.off
    ApiMismatch,    // 本机**没有**这颗、而清单那条的 Api 又和宿主对不上：装不了
    Blocked,        // 条目本身不能用（文件名/path 穿越/哈希不对/不是 https），界面要说清是哪一条
    LocalOnly,      // 本机装着、清单里却没有这一条：能禁用，但谈不上"更新"
    Different,      // 装了，哈希与清单不一样，**但清单的版本不比本机新**：内容不同 ≠ 有更新
                    // （开发机上手工同步的那份、或商店那份更旧时都是这一档；给「换成商店那份」，不给「更新」）
};

// 「清单里这一条能不能从商店拿下来装在这台机器上」= 那条的 Api 和宿主对得上。
// 界面只问这一个谓词，别自己再去比 api（比第二处就会和这里说法不一）。
bool installable(const Entry& e);

// 商店那一面显示一行用的东西：远端条目 + 本地比出来的状态 + 给人看的那句话
struct Row {
    Entry remote;
    State state = State::Blocked;
    QString note;
    // 清单这一条在这台机器上装得了吗（= installable(remote)）。**它管按钮，不管那扇页**：
    // 2026-10-20 他报"注册表/开机分析还是点不开"，形状就是旧的 compare 一上来先判这一条、
    // 不对就整张卡锁成"装不了"，而本机那颗其实装着、也起得来。
    bool entryUsable = true;
    // 排进了"下次启动删掉"那张表：文件此刻**还在盘上**（这个进程还加载着它，Windows 不放行）。
    // 不写进数据层的话，重排卡片墙之后那张卡又会说"已装"—— 他连点两次卸载就是这么来的
    // （2026-10-18 11:17:24 / 11:17:35，日志里两条都是"还加载着，删不掉"）。
    bool queuedRemove = false;
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

QByteArray loadCachedIndex();
bool saveCachedIndex(const QByteArray& json);
bool shouldAutoRefresh();
void markFetched();

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
// 目录里现在有什么：本平台那个后缀（`platform/PluginNames.h`）算已装，后面接 `.off` 算已禁用。
// 不碰网络、不猜清单。
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

// ---- 装一颗（2026-10-21 从商店页里搬进来的）----
// 「下到 .part → 验哈希和字节 → 落地」这一段原来长在 `ToolsPage::runInstall` 里，和卡片上的
// 按钮/状态行缠在一起。自动更新那一趟没有界面（启动后十几秒在后台跑），要么复制一遍那条
// 下载路（= 两条路，早晚说法不一），要么搬出来。搬出来了：界面那侧只剩"把进度写成字"。
struct InstallResult {
    bool ok = false;
    QString err;    // 空 = 成了；不空就是给人看的那句话（原样上界面，别自己再拼）
    QString via;    // 成功那条的名字（镜像 / 代理 / 直连）
};

// parent 只用来挂那条 QNetworkReply（parent 没了回复一起断），不要求是窗口。
// onProgress 可能一次都不回调（服务器没给 Content-Length 时）；done 一定会回。
void installEntry(const Entry& e, QObject* parent,
                  const std::function<void(qint64, qint64)>& onProgress,
                  const std::function<void(const InstallResult&)>& done);

// 同一颗 dll 此刻是不是已经有一趟在飞：是就别再起第二趟（两趟写同一个 .part 必坏，
// 而手动那路和自动那路确实可能同时点中同一颗）。
bool installInFlight(const QString& file);

// ---- 每天一次的自动更新（2026-10-21 他点名：「一天自动更新一次，如果有新插件」）----
// 范围是他拍的：**只换已装的那几颗**，清单里新出现的插件仍要他自己点「安装」
// —— 往机器上塞没选过的 dll 是另一件事。
bool autoUpdateEnabled();     // 配置 store/autoUpdate，**默认开**（他这句是"自动做 X"）
bool autoUpdateDue();         // 今天还没跑过（跑过就把 store/autoUpdateDate 写成今天）
// 挑出这一趟要换的：只认 State::Update。抽成纯函数是为了「新插件不许自动装」这条
// 边界能在离屏判据里钉住 —— 它一旦写进联网那段就没人测得到了。
QVector<Entry> autoUpdateTargets(const QVector<Row>& all);
QString autoUpdateNote();     // 上次跑完留给界面看的那句话；没跑过是空串
// 那句 note 怎么拼（纯函数，离屏钉得住）：成了几颗、哪几颗没成、清单是从哪条拿到的。
// 「下次启动用新的」是实话的一半：文件当场就换掉了，但这个进程还加载着老的那颗。
QString autoUpdateSummary(const QStringList& ok, const QStringList& bad, const QString& src);
// 跑一趟：拉清单（拉不到就用本地缓存那份）→ 挑 → 一颗一颗装 → 把结果写成 note。
// 全程不弹任何东西、不 reload 插件（换下来的那颗可能正被这个进程加载着，
// 界面只会说"下次启动生效"）。onDone 一定会回：**空串 = 这一趟没跑**（没开，或今天已经跑过），
// 非空就是那句 note。商店页那一行不靠这个回调，它直接读 `autoUpdateNote()`。
void runDailyUpdate(QObject* parent, const std::function<void(const QString&)>& onDone);

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
