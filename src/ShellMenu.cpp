#include "ShellMenu.h"

#include "PluginHost.h"

#include "Utils.h"

#include <QSet>
#include <QVector>

#include <QSettings>

#include <windows.h>
#include <shlobj.h>

namespace {

// 写完注册表就广播这一次：让资源管理器自己重读那张表，不用重开窗口、不用注销。
// ASSOCCHANGED 会冲掉外壳的关联缓存，而右键菜单是每次弹出时才按 HKCR（= HKLM + 我们这份
// HKCU\Software\Classes）搭的，所以连已经开着的窗口多半也当场就变。
// Windows 不保证"每一张老窗口都立刻变"（个别要按一下 F5）—— 文案就照这个说，别说满。
void notifyShell()
{
    // SHCNF_IDLIST：载荷是两个 NULL（不是针对某个具体文件/目录，是"整张表变了"）。
    // SHCNF_FLUSH：逼它同步发出去，否则我们这边刚对完账、消息还压在队列里没广播到，
    // 界面就抢先说"已通知资源管理器重读"，那句话就成了假的。
    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST | SHCNF_FLUSH, nullptr, nullptr);
}

QString selfPath()
{
    wchar_t buf[MAX_PATH * 2] = {};
    const DWORD n = GetModuleFileNameW(nullptr, buf, MAX_PATH * 2);
    return n && n < MAX_PATH * 2 ? QString::fromWCharArray(buf, int(n)) : QString();
}

// 一条右键项：菜单上显示什么 + 点下去执行我们自己的哪个开关
struct Item {
    const char* key;         // 注册表子键名（英文，避免编码问题）
    const wchar_t* label;    // 菜单文字
    const char* flag;        // ltool 自己的命令行开关
    bool forFiles;           // 出现在"文件"右键里
    bool forDirs;            // 出现在"文件夹"右键里
};

const Item kItems[] = {
    { "extract",      L"解压到当前目录",      "--extract",      true,  false },
    { "extract-sub",  L"解压到同名文件夹",    "--extract-sub",  true,  false },
    { "compress-zip", L"压缩为 .zip",         "--compress-zip", true,  true },
    { "compress-7z",  L"压缩为 .7z",          "--compress-7z",  true,  true },
    { "shred",        L"粉碎（不可恢复）…",   "--shred",        true,  true },
    { "fileinfo",     L"占用分析",            "--fileinfo",     true,  true },
};

bool setStr(HKEY root, const QString& sub, const wchar_t* valueName, const QString& value)
{
    HKEY k = nullptr;
    if (RegCreateKeyExW(root, reinterpret_cast<const wchar_t*>(sub.utf16()), 0, nullptr,
                        REG_OPTION_NON_VOLATILE, KEY_SET_VALUE, nullptr, &k, nullptr) != ERROR_SUCCESS)
        return false;
    const std::wstring w = value.toStdWString();
    const LONG r = RegSetValueExW(k, valueName, 0, REG_SZ,
                                  reinterpret_cast<const BYTE*>(w.c_str()),
                                  DWORD((w.size() + 1) * sizeof(wchar_t)));
    RegCloseKey(k);
    return r == ERROR_SUCCESS;
}

bool keyExists(HKEY root, const QString& sub)
{
    HKEY k = nullptr;
    if (RegOpenKeyExW(root, reinterpret_cast<const wchar_t*>(sub.utf16()), 0, KEY_READ, &k) != ERROR_SUCCESS)
        return false;
    RegCloseKey(k);
    return true;
}

bool hasValue(HKEY root, const QString& sub, const wchar_t* valueName)
{
    HKEY k = nullptr;
    if (RegOpenKeyExW(root, reinterpret_cast<const wchar_t*>(sub.utf16()), 0, KEY_READ, &k) != ERROR_SUCCESS)
        return false;
    const LONG r = RegQueryValueExW(k, valueName, nullptr, nullptr, nullptr, nullptr);
    RegCloseKey(k);
    return r == ERROR_SUCCESS;
}

void delValue(HKEY root, const QString& sub, const wchar_t* valueName)
{
    HKEY k = nullptr;
    if (RegOpenKeyExW(root, reinterpret_cast<const wchar_t*>(sub.utf16()), 0, KEY_SET_VALUE, &k) != ERROR_SUCCESS)
        return;
    RegDeleteValueW(k, valueName);
    RegCloseKey(k);
}

const QString kRootFile = QStringLiteral("HKEY_CURRENT_USER\\Software\\Classes\\*\\shell\\ltool");
const QString kRootDir  = QStringLiteral("HKEY_CURRENT_USER\\Software\\Classes\\Directory\\shell\\ltool");

QString hkcuPath(bool forDir)
{
    return forDir
        ? QStringLiteral("Software\\Classes\\Directory\\shell\\ltool")
        : QStringLiteral("Software\\Classes\\*\\shell\\ltool");
}

// ExtendedSubCommandsKey 里要写的是"相对 Classes 根"的路径，不带 Software\Classes 前缀
QString classPath(bool forDir)
{
    return forDir ? QStringLiteral("Directory\\shell\\ltool")
                  : QStringLiteral("*\\shell\\ltool");
}

// 删掉一个键及其所有子键（Vista 以后的 DELETE 标志只删自身，子键要自己递归）
void deleteTree(HKEY root, const QString& sub)
{
    HKEY k = nullptr;
    if (RegOpenKeyExW(root, reinterpret_cast<const wchar_t*>(sub.utf16()), 0,
                      KEY_READ | KEY_WRITE | DELETE, &k) != ERROR_SUCCESS)
        return;
    wchar_t name[256];
    for (;;) {
        DWORD len = sizeof(name) / sizeof(name[0]);
        if (RegEnumKeyExW(k, 0, name, &len, nullptr, nullptr, nullptr, nullptr) != ERROR_SUCCESS)
            break;
        const QString child = sub + '\\' + QString::fromWCharArray(name, int(len));
        RegDeleteTreeW(k, name);
        Q_UNUSED(child);
    }
    RegCloseKey(k);
    RegDeleteTreeW(root, reinterpret_cast<const wchar_t*>(sub.utf16()));
}

QVector<QString> childNames(HKEY root, const QString& sub)
{
    QVector<QString> out;
    HKEY k = nullptr;
    if (RegOpenKeyExW(root, reinterpret_cast<const wchar_t*>(sub.utf16()), 0, KEY_READ, &k) != ERROR_SUCCESS)
        return out;
    for (DWORD i = 0;; ++i) {
        wchar_t name[256];
        DWORD len = sizeof(name) / sizeof(name[0]);
        if (RegEnumKeyExW(k, i, name, &len, nullptr, nullptr, nullptr, nullptr) != ERROR_SUCCESS)
            break;
        out << QString::fromWCharArray(name, int(len));
    }
    RegCloseKey(k);
    return out;
}

bool isKnownItem(const QString& name)
{
    for (const Item& it : kItems)
        if (name.compare(QLatin1String(it.key), Qt::CaseInsensitive) == 0)
            return true;
    return false;
}

// 把 ltool 键整理成"只有 shell\<项>"这一种形状：
// 早先版本把各项直接写在 ltool 下面（少一层 shell），资源管理器不认那种嵌套，
// 会把 ltool 当成一个没有 command 的普通项，点下去就是"该文件没有与之关联的应用"。
void clearStaleItems(const QString& base)
{
    for (const QString& n : childNames(HKEY_CURRENT_USER, base)) {
        if (n.compare(QLatin1String("shell"), Qt::CaseInsensitive) == 0)
            continue;
        RegDeleteTreeW(HKEY_CURRENT_USER,
                       reinterpret_cast<const wchar_t*>((base + '\\' + n).utf16()));
    }
    for (const QString& n : childNames(HKEY_CURRENT_USER, base + QLatin1String("\\shell"))) {
        if (isKnownItem(n))
            continue;
        RegDeleteTreeW(HKEY_CURRENT_USER,
                       reinterpret_cast<const wchar_t*>((base + "\\shell\\" + n).utf16()));
    }
}

} // namespace

namespace ShellMenu {

QString locationHint()
{
    return QStringLiteral("HKEY_CURRENT_USER\\Software\\Classes 下的 *\\shell\\ltool 和 "
                          "Directory\\shell\\ltool（只影响当前用户，不需要管理员）");
}

bool installed()
{
    // 只认"点得动"的那种形状：父键带 ExtendedSubCommandsKey，子项在 ltool\shell\<项>\command。
    return hasValue(HKEY_CURRENT_USER, hkcuPath(false), L"ExtendedSubCommandsKey")
        || hasValue(HKEY_CURRENT_USER, hkcuPath(true), L"ExtendedSubCommandsKey");
}

QString install()
{
    const QString exe = selfPath();
    if (exe.isEmpty())
        return QStringLiteral("拿不到 ltool 自己的路径，没法写右键菜单");

    int written = 0;
    for (int asDir = 0; asDir < 2; ++asDir) {
        const QString base = hkcuPath(asDir == 1);
        clearStaleItems(base);
        // 形状完全照系统自己那把键（HKLM\...\Directory\shell\UpdateEncryptionSettings）：
        //   父键：默认值必须"未设置"，名字放 MUIVerb，ExtendedSubCommandsKey 指向父键自己；
        //   子项：ltool\shell\<项> 用 MUIVerb 给菜单文字，command 默认值给命令行。
        // 之前把 "ltool" 写在父键默认值上，Explorer 就不把 ltool\shell\ 里的子项当子菜单，
        // 表现先是"ltool 变成一个点不动的普通项"（1155），加 SubCommands 后变成"箭头有了但菜单是空的"。
        if (!setStr(HKEY_CURRENT_USER, base, L"MUIVerb", QStringLiteral("ltool")))
            continue;
        delValue(HKEY_CURRENT_USER, base, nullptr);          // 老版本写过默认值，抹掉
        delValue(HKEY_CURRENT_USER, base, L"SubCommands");    // 上一版的错误尝试，留着只会干扰
        setStr(HKEY_CURRENT_USER, base, L"Icon", QStringLiteral("%1,0").arg(exe));
        setStr(HKEY_CURRENT_USER, base, L"ExtendedSubCommandsKey", classPath(asDir == 1));
        for (const Item& it : kItems) {
            const bool wanted = asDir == 1 ? it.forDirs : it.forFiles;
            if (!wanted)
                continue;
            const QString itemKey = base + QLatin1String("\\shell\\") + QString::fromUtf8(it.key);
            if (!setStr(HKEY_CURRENT_USER, itemKey, L"MUIVerb", QString::fromWCharArray(it.label)))
                continue;
            delValue(HKEY_CURRENT_USER, itemKey, nullptr);
            // %1 由 shell 换成被右键的那个路径；加引号是因为路径里常有空格
            const QString cmd = QStringLiteral("\"%1\" %2 \"%3\"").arg(exe, QString::fromUtf8(it.flag),
                                                                      QStringLiteral("%1"));
            if (setStr(HKEY_CURRENT_USER, itemKey + "\\command", nullptr, cmd))
                ++written;
        }
    }
    Utils::log(QStringLiteral("shellmenu: install wrote %1 items, exe=%2").arg(written).arg(exe));
    if (written == 0)
        return QStringLiteral("写注册表失败了（HKCU 下也写不动？），右键菜单没有变化");
    notifyShell();
    return QStringLiteral("已写入 %1 项（%2）。已经通知资源管理器重读，那几项一般马上就在；"
                          "个别早就开着的窗口要按一下 F5 才刷新。").arg(written).arg(locationHint());
}

QString remove()
{
    deleteTree(HKEY_CURRENT_USER, hkcuPath(false));
    deleteTree(HKEY_CURRENT_USER, hkcuPath(true));
    const bool gone = !installed();
    Utils::log(QStringLiteral("shellmenu: remove, 现在干净了吗=%1").arg(gone ? "yes" : "no"));
    if (!gone)
        return QStringLiteral("删除后仍然能读到我们的键，可能没删干净，请到 %1 里看一眼")
                      .arg(locationHint());
    notifyShell();
    return QStringLiteral("已撤销：右键菜单里的 ltool 子菜单已删除（%1）。"
                          "已经通知资源管理器重读，那几项一般马上就没；个别早就开着的窗口要按一下 F5。")
                      .arg(locationHint());
}

namespace {

QString intentPath()
{
    return Utils::dataDir() + "/config.ini";
}

} // namespace

bool providerPresent()
{
    const PluginHost::Meta* m = PluginHost::meta(QStringLiteral("shell"));
    return m && m->ok() && !PluginHost::isDisabled(QStringLiteral("shell"));
}

bool intent()
{
    // 缺键时**以注册表现状为准**：老机器上右键里已经有 ltool（那时还没有这个开关），
    // 默认成 false 会在下次启动时把人家正在用的菜单悄悄撤掉。
    return QSettings(intentPath(), QSettings::IniFormat)
        .value(QStringLiteral("shell/intent"), installed())
        .toBool();
}

void setIntent(bool on)
{
    QSettings st(intentPath(), QSettings::IniFormat);
    st.setValue(QStringLiteral("shell/intent"), on);
    st.sync();
}

bool intentExplicitlySet()
{
    return QSettings(intentPath(), QSettings::IniFormat).contains(QStringLiteral("shell/intent"));
}

QString syncWithPlugin()
{
    // 先认领意图再算该不该写：缺键时那个默认值以前读的是注册表现状，而现状是这条规则自己
    // 删出来的 —— 卸载一次就把"想要"永久锁成"不想要"（他报的"装了右键动作，文件夹右键里没有 ltool"）。
    // 放在最前面、且只在这里放一次，四个调用点（启动 / 装 / 卸 / 禁用启用）都走这同一条。
    if (shouldAdoptIntent(intentExplicitlySet(), providerPresent())) {
        setIntent(true);
        Utils::log(QStringLiteral("shellmenu: 配置里这个开关从没被人为动过，而「右键动作」插件在 —— "
                                  "把意图写成\"要\"（不写就解不开：缺键的默认值读的是注册表现状，"
                                  "而那正是上一次撤销改掉的）"));
    }
    const bool want = menuShouldBe(intent(), providerPresent());
    if (want && installed()) {
        refreshIfStale();        // 表里加过项/删过项，老机器上不会自己变
        return QString();
    }
    if (want)
        return install();
    if (!installed())
        return QString();
    const QString why = intent()
        ? QStringLiteral("「右键动作」插件不在了（卸载或禁用了），右键菜单里的 ltool 先撤掉；装回来会自动写上")
        : QStringLiteral("配置里没勾这个开关");
    const QString r = remove();
    Utils::log(QStringLiteral("shellmenu: 撤掉右键菜单（%1）→ %2").arg(why, r));
    return why;
}

void refreshIfStale()
{
    if (!installed())
        return;
    // 表里加了项、或者**删了项**，老机器上的右键菜单都不会自己变（install 只在配置页勾选时跑）。
    // 所以启动时把注册表里现有的项名和表里"该有的"对一遍：不一致就整表重写
    // （install 里的 clearStaleItems 会顺手删掉表里没有的，实测 .lzx 那次就是靠这一步清干净的）。
    for (int asDir = 0; asDir < 2; ++asDir) {
        const QString base = hkcuPath(asDir == 1);
        QSet<QString> want;
        for (const Item& it : kItems)
            if (asDir == 1 ? it.forDirs : it.forFiles)
                want.insert(QString::fromUtf8(it.key).toLower());
        const QStringList have = childNames(HKEY_CURRENT_USER, base + QLatin1String("\\shell"));
        QSet<QString> got;
        for (const QString& n : have)
            got.insert(n.toLower());
        if (got == want)
            continue;
        const QString r = install();
        Utils::log(QStringLiteral("shellmenu: 注册表里 %1 项、表里该有 %2 项，重写右键菜单：%3")
                       .arg(have.size()).arg(want.size()).arg(r));
        return;
    }
}

} // namespace ShellMenu
