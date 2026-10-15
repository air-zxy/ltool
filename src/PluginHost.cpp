#include "PluginHost.h"

#include "Utils.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QLibrary>
#include <QPluginLoader>
#include <QWidget>

namespace {

QVector<PluginHost::Meta> g_metas;
QHash<QString, QPluginLoader*> g_loaders;      // 只装不卸：卸载会留下悬空的 QWidget
QHash<QString, LToolPlugin*> g_plugins;
QHash<QString, QWidget*> g_pages;
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
    const auto files = dir.entryList({QStringLiteral("*.dll")}, QDir::Files, QDir::Name);
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
            Utils::log(QStringLiteral("plugin: 起不来 %1：%2").arg(m.file, loader->errorString()));
            delete loader;
            return nullptr;
        }
        auto* p = qobject_cast<LToolPlugin*>(obj);
        if (!p) {
            Utils::log(QStringLiteral("plugin: %1 没实现 LToolPlugin 接口").arg(m.file));
            loader->unload();
            delete loader;
            return nullptr;
        }
        // 元数据里写的 Api 只是"声明"，实例自己报的才算数（两边不一致就是插件自己乱写）
        if (p->apiVersion() != LTOOL_PLUGIN_API_VERSION) {
            Utils::log(QStringLiteral("plugin: %1 运行时报 API %2，主程序是 %3，不加载")
                           .arg(id).arg(p->apiVersion()).arg(LTOOL_PLUGIN_API_VERSION));
            loader->unload();
            delete loader;
            return nullptr;
        }
        g_loaders.insert(id, loader);        // 故意留着不卸
        g_plugins.insert(id, p);
        return p;
    }
    return nullptr;
}

QWidget* page(const QString& id, QWidget* parent, QString* err)
{
    if (g_pages.contains(id))
        return g_pages.value(id);
    LToolPlugin* p = plugin(id);
    if (!p) {
        if (err)
            *err = QStringLiteral("插件 %1 没能加载（看日志 plugin: 那几行）").arg(id);
        return nullptr;
    }
    QWidget* w = p->createPage(parent);
    if (!w) {
        if (err)
            *err = QStringLiteral("插件 %1 的 createPage 返回了空").arg(id);
        return nullptr;
    }
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

bool isDisabled(const QString& id)
{
    const QString dir = pluginsDir();
    if (QFileInfo::exists(dir + QStringLiteral("/ltool-") + id + QStringLiteral(".dll.off")))
        return true;
    if (QFileInfo::exists(dir + QLatin1Char('/') + id + QStringLiteral(".dll.off")))
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

void* entry(const QString& id, const char* symbol)
{
    if (!plugin(id))       // 先真加载，符号只在已装载的 dll 上找得到
        return nullptr;
    // Qt6 把 QPluginLoader::resolve 删掉了，所以按同一个文件路径开一份 QLibrary：
    // Windows 的 LoadLibrary 按路径复用模块句柄，这里不会把插件 dll 装第二遍。
    // 对象留着不销毁 —— 析构会把引用计数放掉，下次再 resolve 就可能重新走一遍装载。
    static QHash<QString, QLibrary*> libs;
    QLibrary* lib = libs.value(id, nullptr);
    if (!lib) {
        const Meta* m = meta(id);
        if (!m)
            return nullptr;
        lib = new QLibrary(m->file);
        libs.insert(id, lib);
    }
    return lib->resolve(symbol);
}

} // namespace PluginHost
