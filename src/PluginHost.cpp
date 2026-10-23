#include "PluginHost.h"

#include "Utils.h"
#include "platform/PluginNames.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLibrary>
#include <QPointer>
#include <QPluginLoader>
#include <QWidget>

#ifdef Q_OS_WIN
#include <windows.h>        // SEH：LONG / PEXCEPTION_POINTERS / GetExceptionInformation
#endif

namespace {

QVector<PluginHost::Meta> g_metas;
QHash<QString, QPluginLoader*> g_loaders;      // 只装不卸：卸载会留下悬空的 QWidget
QHash<QString, LToolPlugin*> g_plugins;
QHash<QString, QPointer<QWidget>> g_pages;
// ^ 用 QPointer 不是讲究：这些页的命**不在宿主手里** —— 它们是 `createPage(parent)` 时交给
// 调用方那棵树去拥有的（`ToolsPage` 重排卡片墙那一格会把它摘掉的那颗 deleteLater 掉）。
// 裸指针缓存的话，下一次 `page(id)` 就把一块已经释放的 QWidget 发出去，
// 界面上一按就是"读地址 8"那种 0xC0000005（2026-10-22 base64 崩的那一趟正在查这条路）。
// 每个 id **最近一次装载失败的原因**（成功就销掉）。界面要说"这个动作没人接"的时候，
// 得能分出"目录里根本没这个文件"和"文件在、可它起不来" —— 2026-10-19 他按 F1 弹的
// 就是那句"是不是缺 ltool-capture.dll"，而 dll 明明躺着：商店那份产物和这版 core 的
// 导出面对不上，LoadLibrary 直接失败。谎报成"缺文件"会让人去装第二遍。
QHash<QString, QString> g_loadErrors;
bool g_scanned = false;

QString str(const QJsonObject& o, const char* key)
{
    return o.value(QLatin1String(key)).toString();
}

// Q_PLUGIN_METADATA(FILE "plugin.json") 把那个文件的内容**嵌在 "MetaData" 这一层下面**，
// 不是并到顶层（顶层只有 IID/ClassName/Qt 版本）。两层都找，先 MetaData 再顶层，
// 免得以后 Qt 改了嵌套方式又变成"读到空"。
QJsonObject metaOf(const QJsonObject& md)
{
    const QJsonObject inner = md.value(QLatin1String("MetaData")).toObject();
    return inner.isEmpty() ? md : inner;
}

} // namespace

namespace PluginHost {

QString pluginsDir()
{
    return QCoreApplication::applicationDirPath() + QStringLiteral("/ltool-plugins");
}

void reload()
{
    g_metas.clear();
    g_scanned = false;
    scan();
}

const QVector<Meta>& scan()
{
    if (g_scanned)
        return g_metas;
    g_scanned = true;
    const QDir dir(pluginsDir());
    if (!dir.exists())
        return g_metas;
    const auto files = dir.entryList(Platform::pluginFileGlobs(), QDir::Files, QDir::Name);
    for (const QString& f : files) {
        const QString path = dir.absoluteFilePath(f);
        Meta m;
        m.file = path;
        QPluginLoader probe(path);
        const QJsonObject md = probe.metaData();
        if (str(md, "IID") != QLatin1String(LTOOL_PLUGIN_IID)) {
            // 不是 ltool 的插件（可能是 Qt 自己的图片格式插件被人误拷进来）—— 说清楚再跳过
            m.error = QStringLiteral("不是 ltool 插件（iid=%1）").arg(str(md, "IID"));
            g_metas.append(m);
            continue;
        }
        const QJsonObject meta = metaOf(md);
        m.id = str(meta, "Id");
        m.name = str(meta, "Name");
        m.desc = str(meta, "Desc");
        m.version = str(meta, "Version");
        if (m.version.isEmpty())
            m.version = QStringLiteral("1.0.0");
        m.api = meta.value(QLatin1String("Api")).toInt();
        const QJsonArray ks = meta.value(QLatin1String("Keywords")).toArray();
        for (const QJsonValue& v : ks)
            m.keywords << v.toString();
        m.wantsActivate = meta.value(QLatin1String("Activate")).toBool();
        const QJsonArray as = meta.value(QLatin1String("Actions")).toArray();
        for (const QJsonValue& v : as)
            m.actions << v.toString();
        // 卡片图标的颜色：plugin.json 里 "Color":"87,160,40"。没写就别整张卡一个色 ——
        // 按 id 派生一个色相，九个工具摆在一起还是分得开。
        const QString rgb = str(meta, "Color");
        const QStringList parts = rgb.split(QLatin1Char(','), Qt::SkipEmptyParts);
        if (parts.size() == 3)
            m.color = QColor(parts[0].toInt(), parts[1].toInt(), parts[2].toInt());
        else
            m.color = QColor::fromHsv(int(qHash(m.id) % 360), 120, 170);
        if (m.id.isEmpty() || m.name.isEmpty())
            m.error = QStringLiteral("plugin.json 缺 Id/Name，读到的元数据=%1")
                          .arg(QString::fromUtf8(
                              QJsonDocument(md).toJson(QJsonDocument::Compact)));
        else if (m.api != LTOOL_PLUGIN_API_VERSION)
            m.error = QStringLiteral("这个插件是给 API %1 编的，ltool 现在是 API %2")
                          .arg(m.api).arg(LTOOL_PLUGIN_API_VERSION);
        g_metas.append(m);
        if (m.ok())
            Utils::log(QStringLiteral("plugin: 看到 %1 (%2) 关键词=%3 动作=%4")
                           .arg(m.name, m.id, m.keywords.join(","), m.actions.join(",")));
        else
            Utils::log(QStringLiteral("plugin: 拒绝 %1：%2").arg(path, m.error));
    }
    return g_metas;
}

LToolPlugin* plugin(const QString& id)
{
    if (g_plugins.contains(id))
        return g_plugins.value(id);
    const QVector<Meta>& metas = scan();
    for (const Meta& m : metas) {
        if (m.id != id || !m.ok())
            continue;
        auto* loader = new QPluginLoader(m.file);
        QObject* obj = loader->instance();
        if (!obj) {
            g_loadErrors.insert(m.id, loader->errorString());
            Utils::log(QStringLiteral("plugin: 起不来 %1：%2").arg(m.file, loader->errorString()));
            delete loader;
            return nullptr;
        }
        auto* p = qobject_cast<LToolPlugin*>(obj);
        if (!p) {
            g_loadErrors.insert(m.id, QStringLiteral("没实现 LToolPlugin 接口"));
            Utils::log(QStringLiteral("plugin: %1 没实现 LToolPlugin 接口").arg(m.file));
            loader->unload();
            delete loader;
            return nullptr;
        }
        // 元数据里写的 Api 只是"声明"，实例自己报的才算数（两边不一致就是插件自己乱写）
        if (p->apiVersion() != LTOOL_PLUGIN_API_VERSION) {
            g_loadErrors.insert(m.id, QStringLiteral("运行时报 API %1，主程序是 %2")
                                              .arg(p->apiVersion()).arg(LTOOL_PLUGIN_API_VERSION));
            Utils::log(QStringLiteral("plugin: %1 运行时报 API %2，主程序是 %3，不加载")
                           .arg(id).arg(p->apiVersion()).arg(LTOOL_PLUGIN_API_VERSION));
            loader->unload();
            delete loader;
            return nullptr;
        }
        g_loadErrors.remove(m.id);              // 这次起来了，之前那条失败原因就不该再被引用
        g_loaders.insert(id, loader);        // 故意留着不卸
        g_plugins.insert(id, p);
        return p;
    }
    return nullptr;
}

// 只有这一颗真的跨进插件的代码。它自己**不能有任何需要析构的局部对象**
// （MSVC：函数里有 __try 又放着要析构的对象 = C2712），所以参数全是指针、结果也走指针。
#ifdef Q_OS_WIN

static LONG pageSehFilter(PEXCEPTION_POINTERS ep, SehHit* hit)
{
    hit->code = ep->ExceptionRecord->ExceptionCode;
    hit->addr = ep->ExceptionRecord->ExceptionAddress;
    return EXCEPTION_EXECUTE_HANDLER;     // 一律接住：这条边界上"往上传"就是让整个程序陪着它死
}

static bool runCreatePageSeh(LToolPlugin* p, QWidget* parent, QWidget** out, SehHit* hit)
{
    *out = nullptr;
    hit->code = 0;
    hit->addr = nullptr;
    bool cameBack = false;
    __try {
        *out = p->createPage(parent);
        cameBack = true;
    } __except (pageSehFilter(GetExceptionInformation(), hit)) {
        cameBack = false;
    }
    return cameBack;
}

#else

// Linux/macOS 上没有 SEH：那一边是信号（SIGSEGV），在这条边界里接它要 sigsetjmp，
// 而"从信号里继续跑"比 Windows 上的 AV 恢复更不可靠 —— 先**如实不装**这一层，
// 别写一个看起来像守卫、其实什么都挡不住的空壳。移植那一档要真做，走进程隔离那条路。
static bool runCreatePageSeh(LToolPlugin* p, QWidget* parent, QWidget** out, SehHit* hit)
{
    *out = nullptr;
    hit->code = 0;
    hit->addr = nullptr;
    *out = p->createPage(parent);
    return true;
}

#endif

QWidget* buildPage(LToolPlugin* p, const QString& id, QWidget* parent, QString* err)
{
    // 进来先清一次：这一颗的契约是「给出页了就没有原因」。不清的话调用方复用同一个 QString 时，
    // 上一趟失败留下的那句会跟着这一趟成功一起被念出来 —— 那是"读上一轮留下的状态"那一类假话。
    if (err)
        err->clear();
    if (!p) {
        if (err)
            *err = QStringLiteral("插件 %1 没实例化出来（看日志 plugin: 那几行）").arg(id);
        return nullptr;
    }
    QWidget* w = nullptr;
    SehHit hit;
    if (!runCreatePageSeh(p, parent, &w, &hit)) {
        // 这句是要给界面看的：哪颗插件、哪一类异常、落在哪条指令 —— 只说"打不开"等于没说
        const QString why = QStringLiteral("%1 建页时撞在异常 0x%2（指令地址 %3）· 这一趟没给你页，程序没跟着死")
                                .arg(id)
                                .arg(QString::number(hit.code, 16).toUpper())
                                .arg(reinterpret_cast<quintptr>(hit.addr), 0, 16);
        if (err)
            *err = why;
        Utils::log(QStringLiteral("plugin: %1").arg(why));
        return nullptr;
    }
    if (!w && err)
        *err = QStringLiteral("插件 %1 的 createPage 返回了空").arg(id);
    return w;
}

QWidget* page(const QString& id, QWidget* parent, QString* err)
{
    if (g_pages.contains(id)) {
        if (QWidget* cached = g_pages.value(id))
            return cached;
        g_pages.remove(id);        // 那张页已经不在了：留一格永远不会再亮的缓存没有意义，重造一张
    }
    LToolPlugin* p = plugin(id);
    if (!p) {
        if (err)
            *err = QStringLiteral("插件 %1 没能加载（看日志 plugin: 那几行）").arg(id);
        return nullptr;
    }
    QWidget* w = buildPage(p, id, parent, err);
    if (!w)
        return nullptr;      // 原因已经在 err 里（撞异常 / 它自己返回空，是两句话说头）
    g_pages.insert(id, w);
    Utils::log(QStringLiteral("plugin: 建页 %1").arg(id));
    return w;
}

const Meta* meta(const QString& id)
{
    for (const Meta& m : scan())
        if (m.id == id)
            return &m;
    return nullptr;
}

QString metaVersionOf(const QString& path)
{
    // metaData() 只读嵌在产物里那份 plugin.json，不会 LoadLibrary、不会建对象
    const QJsonObject md = QPluginLoader(path).metaData();
    return metaOf(md).value(QLatin1String("Version")).toString().trimmed();
}

bool isDisabled(const QString& id)
{
    const QString dir = pluginsDir();
    const QString off = QLatin1Char('.') + Platform::pluginFileSuffix() + QStringLiteral(".off");
    if (QFileInfo::exists(dir + QLatin1Char('/') + Platform::pluginFileName(id) + QStringLiteral(".off")))
        return true;
    // 早先禁用那一路留下过"文件名就是 id"这一颗，不能因为它换了后缀就看不见
    if (QFileInfo::exists(dir + QLatin1Char('/') + id + off))
        return true;
    if (const Meta* m = meta(id)) {
        if (!m->file.isEmpty()) {
            if (QFileInfo::exists(m->file + QStringLiteral(".off")))
                return true;
            if (!QFileInfo::exists(m->file))
                return true;
        }
    }
    return false;
}

bool feed(const QString& id, const QString& text)
{
    LToolPlugin* p = plugin(id);
    return p && p->feedText(text);
}

QString actionOwner(const QString& action)
{
    for (const Meta& m : scan())
        if (m.ok() && m.actions.contains(action))
            return m.id;
    return {};
}

QString loadErrorOf(const QString& id)
{
    return g_loadErrors.value(id);
}

// "这个动作没人接"到底该怎么说 —— 两种实话是分开的：
//   · 目录里根本没一颗插件声明接它 → 才是"是不是缺那个文件"
//   · 有声明、可它起不来 → 必须说"在，但起不来 + 原因"，不能让人去装第二遍
// 调用前提是 `invoke()` 刚试过（它试过才会有装载记录）。
QString whyNoOwner(const QString& action)
{
    const QString id = actionOwner(action);
    if (id.isEmpty())
        return QStringLiteral("没装接 %1 这个动作的插件（ltool-plugins 里是不是缺 %2）")
            .arg(action, Platform::pluginFileName(action));
    if (plugin(id))
        return QStringLiteral("接 %1 的插件 %2 起来了，可它自己不认这个动作（元数据那份 Actions 和实例报的对不上，见日志）")
            .arg(action, id);
    const QString err = loadErrorOf(id);
    return QStringLiteral("接 %1 的插件 %2 就在目录里，可它起不来：%3")
        .arg(action, id, err.isEmpty() ? QStringLiteral("装载失败（原因见日志）") : err);
}

bool invoke(const QString& action, const QStringList& args, QString* who)
{
    // 元数据里那份 "Actions" 只是"哪家店门口写着做这个活"（不用装载就能查），
    // 真正接不接**以实例自己报的 actions() 为准** —— 两边不一致就是插件自己乱写，
    // 记一行、接着找下一家。
    for (const Meta& m : scan()) {
        if (!m.ok() || !m.actions.contains(action))
            continue;
        LToolPlugin* p = plugin(m.id);       // 起不来它自己已经把原因写进日志
        if (!p)
            continue;
        if (!p->actions().contains(action)) {
            Utils::log(QStringLiteral("plugin: %1 元数据说接 %2，实例自己不认（不路由给它）")
                           .arg(m.id, action));
            continue;
        }
        if (p->invoke(action, args)) {
            if (who)
                *who = m.id;
            Utils::log(QStringLiteral("plugin: 动作 %1 → %2（参数 %3）")
                           .arg(action, m.id, args.join(QLatin1Char(';'))));
            return true;
        }
        if (who)
            *who = m.id;                     // 接了但没干成：调用方要说"是它做的、失败了"
        Utils::log(QStringLiteral("plugin: %1 接了 %2 但返回失败").arg(m.id, action));
    }
    Utils::log(QStringLiteral("plugin: 没人接动作 %1（谁装了什么见上面那几行）").arg(action));
    return false;
}

// 按 id 拿那份 QLibrary（对象留着不销毁 —— 析构会把引用计数放掉，
// 下次再 resolve 就可能重新走一遍装载。Windows 按路径复用句柄，不会装第二遍）
QLibrary* libFor(const QString& id)
{
    static QHash<QString, QLibrary*> libs;
    QLibrary* lib = libs.value(id, nullptr);
    if (!lib) {
        const Meta* m = meta(id);
        if (!m)
            return nullptr;
        lib = new QLibrary(m->file);
        libs.insert(id, lib);
    }
    return lib;
}

void* entry(const QString& id, const char* symbol)
{
    if (!plugin(id))       // 先真加载，符号只在已装载的 dll 上找得到
        return nullptr;
    QLibrary* lib = libFor(id);
    // 这里必须 reinterpret_cast：QLibrary::resolve 给的是 QFunctionPointer（void (*)()），
    // 函数指针转 void* 不是标准 C++ —— MSVC 当扩展收下，GCC 直接 -fpermissive 报错。
    return lib ? reinterpret_cast<void*>(lib->resolve(symbol)) : nullptr;
}

int startBackground()
{
    int started = 0;
    for (const Meta& m : scan()) {
        if (!m.ok() || !m.wantsActivate || isDisabled(m.id))
            continue;
        QLibrary* lib = libFor(m.id);
        auto fn = reinterpret_cast<void (*)()>(lib ? lib->resolve("ltoolPluginActivate") : nullptr);
        if (!fn) {
            // 挂了牌却没导出那个符号：这是插件自己写歪了，必须点名，不能静默少一个后台功能
            Utils::log(QStringLiteral("plugin: %1 声明 Activate 但没导出 ltoolPluginActivate，"
                                      "它后台的活儿没跑起来").arg(m.id));
            continue;
        }
        fn();
        ++started;
        Utils::log(QStringLiteral("plugin: 激活 %1（它自己起了后台的活儿）").arg(m.id));
    }
    return started;
}

} // namespace PluginHost
