#include "DevEnv.h"
#include "EnvPath.h"
#include "InstallLogDialog.h"
#include "SilentInstallLog.h"
#include "Utils.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QMap>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QJsonObject>
#include <QDateTime>
#include <QFutureWatcher>
#include <QSettings>
#include <QProcess>
#include <QRegularExpression>
#include <QtGlobal>
#include <QApplication>

#include <windows.h>

namespace {

const char* kPluginJava = R"JSON({
  "id": "java",
  "name": "Java 开发环境",
  "items": [
    { "label": "JDK (javac)", "cmd": ["javac", "-version"], "wingetId": "EclipseAdoptium.Temurin.21.JDK" },
    { "label": "Maven", "cmd": ["mvn", "-version"], "note": "Maven 不在 winget 源里（实测 Apache.Maven 查无此包）：去 maven.apache.org 下载 zip 解压，再把其中的 bin 加进 PATH" }
  ]
})JSON";

const char* kPluginCpp = R"JSON({
  "id": "c++",
  "name": "C++ 开发环境",
  "items": [
    { "label": "CMake", "cmd": ["cmake", "--version"], "wingetId": "Kitware.CMake" },
    { "label": "Ninja", "cmd": ["ninja", "--version"], "wingetId": "Ninja-build.Ninja" },
    { "label": "Clang", "cmd": ["clang++", "--version"], "wingetId": "LLVM.LLVM" },
    { "label": "MinGW (g++)", "cmd": ["g++", "--version"], "wingetId": "MSYS2.MSYS2" },
    { "label": "MSVC (cl)", "cmd": ["cl"], "note": "需通过 Visual Studio Installer 安装 C++ 生成工具" }
  ]
})JSON";

const char* kPluginNode = R"JSON({
  "id": "node",
  "name": "Node.js 开发环境",
  "items": [
    { "label": "Node.js", "cmd": ["node", "--version"], "wingetId": "OpenJS.NodeJS.LTS" },
    { "label": "npm", "cmd": ["npm.cmd", "--version"], "wingetId": "OpenJS.NodeJS.LTS" }
  ]
})JSON";

const char* kPluginPython = R"JSON({
  "id": "python",
  "name": "Python 开发环境",
  "items": [
    { "label": "Python", "cmd": ["python", "--version"], "wingetId": "Python.Python.3.12" },
    { "label": "pip", "cmd": ["pip", "--version"], "wingetId": "Python.Python.3.12" }
  ]
})JSON";

const char* kPluginRust = R"JSON({
  "id": "rust",
  "name": "Rust 开发环境",
  "items": [
    { "label": "rustc", "cmd": ["rustc", "--version"], "wingetId": "Rustlang.Rustup" },
    { "label": "cargo", "cmd": ["cargo", "--version"], "wingetId": "Rustlang.Rustup" }
  ]
})JSON";

bool writeTextFile(const QString& path, const QString& content)
{
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;
    f.write(content.toUtf8());
    return true;
}

void setHiddenAttribute(const QString& path)
{
    SetFileAttributesW((LPCWSTR)QDir::toNativeSeparators(path).utf16(),
                       FILE_ATTRIBUTE_HIDDEN);
}

} // namespace

namespace DevEnv {

// winget 的命令行代理（--proxy）要管理员先启用 ProxyCommandLineOptions，没启用时它
// 会把整条 install 命令直接拒掉（实测 0x8A150002）。第一次撞到后置 false：
// 宁可慢，也不能"配了代理就什么都装不上"。
static bool g_proxyOptionUsable = true;

// 早期默认插件里写了 winget 源里根本不存在的包（Apache.Maven，实测 --id 精确查无此包），
// 点安装只会得到一句"找不到与输入条件匹配的程序包"，看不出所以然。这里把这类条目
// 换成 note，其余内容原样保留（用户自己加的包不受影响）。
static void repairDeadWingetIds(const QString& path)
{
    static const QMap<QString, QString> dead = {
        { QStringLiteral("Apache.Maven"),
          QStringLiteral("Maven 不在 winget 源里：去 maven.apache.org 下载 zip 解压，再把其中的 bin 加进 PATH") },
    };
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return;
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
    f.close();
    if (!doc.isObject())
        return;
    QJsonObject root = doc.object();
    QJsonArray items = root.value("items").toArray();
    bool changed = false;
    for (int i = 0; i < items.size(); ++i) {
        QJsonObject it = items[i].toObject();
        const QString id = it.value("wingetId").toString();
        if (id.isEmpty() || !dead.contains(id))
            continue;
        it.remove("wingetId");
        it.insert("note", dead.value(id));
        items[i] = it;
        changed = true;
    }
    if (!changed)
        return;
    root.insert("items", items);
    QFile w(path);
    if (w.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        w.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
        w.close();
        Utils::log(QString("plugin %1: 改掉了 winget 里不存在的包").arg(QFileInfo(path).fileName()));
    }
}

void ensureDefaultPlugins()
{
    const QString dir = Utils::pluginsDir();
    QDir().mkpath(dir);
    struct P { const char* file; const char* content; };
    static const P defaults[] = {
        { "java.json",   kPluginJava },
        { "c++.json",    kPluginCpp },
        { "node.json",   kPluginNode },
        { "python.json", kPluginPython },
        { "rust.json",   kPluginRust },
    };
    for (const P& p : defaults) {
        const QString path = dir + "/" + p.file;
        if (!QFile::exists(path))
            writeTextFile(path, QString::fromUtf8(p.content));
        repairDeadWingetIds(path);
    }
    setHiddenAttribute(dir);
}

QVector<DevPlugin> loadPlugins()
{
    QVector<DevPlugin> out;
    QDir dir(Utils::pluginsDir());
    const auto files = dir.entryList({"*.json"}, QDir::Files, QDir::Name);
    for (const QString& f : files) {
        QFile file(dir.filePath(f));
        if (!file.open(QIODevice::ReadOnly))
            continue;
        QJsonParseError perr{};
        const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &perr);
        if (perr.error != QJsonParseError::NoError || !doc.isObject())
            continue;
        const QJsonObject o = doc.object();
        DevPlugin p;
        p.id = o.value("id").toString();
        p.name = o.value("name").toString();
        if (p.id.isEmpty())
            p.id = QFileInfo(f).completeBaseName();
        if (p.name.isEmpty())
            p.name = p.id;
        const QJsonArray items = o.value("items").toArray();
        for (const auto& iv : items) {
            const QJsonObject io = iv.toObject();
            DevItem it;
            it.label = io.value("label").toString();
            const QJsonArray cmd = io.value("cmd").toArray();
            for (const auto& c : cmd)
                it.cmd.append(c.toString());
            it.wingetId = io.value("wingetId").toString();
            it.note = io.value("note").toString();
            if (it.label.isEmpty() || it.cmd.isEmpty())
                continue;
            p.items.append(it);
        }
        if (!p.items.isEmpty())
            out.append(p);
    }
    return out;
}

void detect(DevPlugin* p)
{
    for (DevItem& it : p->items) {
        QProcess proc;
        proc.setProgram(it.cmd.first());
        proc.setArguments(it.cmd.mid(1));
        proc.setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments* a) {
            a->flags |= CREATE_NO_WINDOW;
        });
        proc.start();
        if (!proc.waitForStarted(2000)) {
            it.ok = false;
            it.version = "未找到命令";
            continue;
        }
        if (!proc.waitForFinished(6000)) {
            proc.kill();
            proc.waitForFinished(1000);
            it.ok = false;
            it.version = "检测超时";
            continue;
        }
        const QString out = QString::fromUtf8(proc.readAllStandardOutput()
                                              + proc.readAllStandardError());
        const QStringList lines = out.split('\n', Qt::SkipEmptyParts);
        it.version = lines.isEmpty() ? QString() : lines.first().trimmed();
        if (it.version.size() > 120)
            it.version = it.version.left(120) + "…";
        it.ok = proc.exitStatus() == QProcess::NormalExit && proc.exitCode() == 0
                && !it.version.isEmpty();
        if (!it.ok && it.version.isEmpty())
            it.version = QString("退出码 %1").arg(proc.exitCode());
    }
}

QString whichInRegistryPath(const QString& program)
{
    if (program.isEmpty())
        return {};
    // QProcess 找命令只看进程 env，所以这里自己按注册表里的 PATH 逐个目录试。
    // 同一名字按 Windows 的顺序补 .exe/.cmd/.bat/.com
    static const QStringList exts = {".exe", ".cmd", ".bat", ".com", ""};
    const QStringList dirs = EnvPath::registryPath().split(QLatin1Char(';'), Qt::SkipEmptyParts);
    for (const QString& raw : dirs) {
        const QString dir = QDir::cleanPath(raw);
        for (const QString& e : exts) {
            const QString cand = dir + QLatin1Char('\\') + program + e;
            if (QFileInfo::exists(cand))
                return QDir::toNativeSeparators(cand);
        }
    }
    return {};
}

// 「验证一下」：用注册表里当前的 PATH 起一个新进程跑检测命令，把原样输出带回来。
// 关键是**不能**用本进程的 env —— 刚配好的 PATH 只对"新开的进程"生效，
// 用老 env 跑会得出"没装上"，正是初学者最容易被骗到的那一下。
ProbeResult probe(const QStringList& cmd)
{
    ProbeResult r;
    if (cmd.isEmpty()) {
        r.output = QStringLiteral("没有配置检测命令");
        return r;
    }
    r.program = cmd.first();
    r.args = cmd.mid(1);
    r.resolved = whichInRegistryPath(r.program);

    QProcess proc;
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    const QString regPath = EnvPath::registryPath();
    if (!regPath.isEmpty())
        env.insert(QStringLiteral("PATH"), regPath);
    proc.setProcessEnvironment(env);
    proc.setProgram(r.program);
    proc.setArguments(r.args);
    proc.setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments* a) {
        a->flags |= CREATE_NO_WINDOW;
    });
    proc.start();
    if (!proc.waitForStarted(3000)) {
        r.output = QString("起不来：%1（%2）")
                       .arg(r.program, QString::fromLocal8Bit(proc.errorString().toLocal8Bit()));
        return r;
    }
    if (!proc.waitForFinished(8000)) {
        proc.kill();
        proc.waitForFinished(1500);
        r.output = QStringLiteral("命令 8 秒没返回，已终止");
        return r;
    }
    r.exitCode = proc.exitCode();
    r.ok = proc.exitStatus() == QProcess::NormalExit && r.exitCode == 0;
    const QByteArray out = proc.readAllStandardOutput() + proc.readAllStandardError();
    r.output = Utils::ConsoleLines::decode(out).trimmed();
    if (r.output.isEmpty())
        r.output = r.ok ? QStringLiteral("（命令成功，没有输出）")
                        : QStringLiteral("（没有输出，退出码 %1）").arg(r.exitCode);
    return r;
}

QVector<LangVersion> detectVersions(const QString& langId)
{
    QVector<LangVersion> out;
    const QString home = QDir::homePath();
    const QString localAppData = home + "/AppData/Local";
    const QString roaming = home + "/AppData/Roaming";

    const auto scanSubdirs = [&](const QString& base, const QStringList& filters) {
        QDir d(base);
        if (!d.exists())
            return;
        const auto subs = d.entryList(filters, QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
        for (const QString& sub : subs)
            out.append(LangVersion{sub, QDir::toNativeSeparators(d.filePath(sub))});
    };

    if (langId == "java") {
        const auto addVer = [&](const QString& label, const QString& natPath) {
            for (const LangVersion& v : out)
                if (v.path.compare(natPath, Qt::CaseInsensitive) == 0)
                    return;   // 同一个 JDK 既在 Program Files 又在 JAVA_HOME 里，别列两遍
            out.append(LangVersion{label, natPath});
        };
        scanSubdirs("C:/Program Files/Java", {"jdk-*", "jdk*"});
        scanSubdirs("C:/Program Files/Eclipse Adoptium", {"jdk-*"});
        scanSubdirs("C:/Program Files/Microsoft", {"jdk*"});
        const QString jh = qEnvironmentVariable("JAVA_HOME");
        if (!jh.isEmpty() && QDir(jh).exists())
            addVer(QString("%1 (JAVA_HOME)").arg(QFileInfo(jh).fileName()),
                   QDir::toNativeSeparators(jh));
        // ltool 装的 JDK 落在配置的安装目录下（Temurin 的 MSI 还会再套一层 jdk-x.y.z+bN），
        // 常规位置扫不到就往下找两层。判据不能只看 bin\java.exe / bin\javac.exe：
        // CLion/IDEA/WebStorm 自带的 jbr、STM32CubeMX 自带的 jre 里都有 javac，
        // 那样"本机 Java 版本"会被一堆 IDE 目录刷屏，所以按目录名把它们排除掉。
        const std::function<void(const QString&, int)> findJdk = [&](const QString& base, int depth) {
            if (base.isEmpty() || !QFileInfo(base).isDir())
                return;
            const QString leaf = QFileInfo(base).fileName().toLower();
            if (leaf == QLatin1String("jbr") || leaf == QLatin1String("jre")
                || leaf == QLatin1String("bin"))
                return;
            if (QFileInfo::exists(base + "/bin/javac.exe")) {
                const QString nat = QDir::toNativeSeparators(base);
                for (const LangVersion& v : out)
                    if (v.path.compare(nat, Qt::CaseInsensitive) == 0)
                        return;
                out.append(LangVersion{QFileInfo(base).fileName(), nat});
                return;
            }
            if (depth <= 0)
                return;
            QDir d(base);
            for (const QString& sub : d.entryList({"*"}, QDir::Dirs | QDir::NoDotAndDotDot))
                findJdk(d.filePath(sub), depth - 1);
        };
        findJdk(installLocation(), 2);
    } else if (langId == "c++") {
        for (const QString& root : {"C:/msys64", "D:/msys64", "D:/software/msys64"}) {
            if (QDir(root).exists()) {
                out.append(LangVersion{QString("MSYS2 (%1)").arg(root),
                                QDir::toNativeSeparators(root)});
                break;
            }
        }
    } else if (langId == "node") {
        if (QDir("C:/Program Files/nodejs").exists())
            out.append(LangVersion{"system", "C:\\Program Files\\nodejs"});
        scanSubdirs(roaming + "/nvm", {"v*"});
    } else if (langId == "python") {
        scanSubdirs(localAppData + "/Programs/Python", {"Python*"});
    } else if (langId == "rust") {
        scanSubdirs(home + "/.rustup/toolchains", {"*"});
    }
    return out;
}

// 代理那四颗（normalizeProxy / systemProxyUrl / proxyUrl / applyProxy）搬进了
// src/DevEnvConfig.cpp + src/platform/SysConfig_{win,posix}.cpp（第八道缝）：
// DownloadManager 和 PluginStore 已在可移植名单里，链接期要在那边找得到它们。


// 把某个语言"接进"开发环境：PATH / JAVA_HOME / pyenv / rustup 默认工具链。
// activate=true 表示"把这个版本设为当前使用"（下拉切换版本），false 表示"刚装完，先让它可用"。
// what 回填做了什么（界面状态行和安装日志看台都要显示），失败原因写进 err。
bool applyEnvFor(const QString& langId, const QString& path, const QString& label,
                 bool activate, QString* what, QString* err)
{
    if (path.isEmpty()) {
        if (err)
            *err = QStringLiteral("没有可配置的本机路径");
        return false;
    }
    QStringList done;
    QString fail;
    const auto addToPath = [&done, &fail](const QString& dir) {
        QString e;
        if (EnvPath::addToUserPath(dir, &e))
            done << QString("PATH 追加 %1").arg(dir);
        else if (e.contains(QStringLiteral("已在 PATH")))
            done << QString("PATH 已有 %1").arg(dir);
        else
            fail = e;
    };
    const auto runHidden = [](const QString& exe, const QStringList& args, int ms,
                              QString* outErr) {
        QProcess p;
        p.setProgram(exe);
        p.setArguments(args);
        p.setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments* a) {
            a->flags |= CREATE_NO_WINDOW;      // 不弹黑窗口
        });
        p.start();
        if (!p.waitForStarted(5000)) {
            if (outErr) *outErr = QStringLiteral("无法启动 %1").arg(exe);
            return false;
        }
        p.waitForFinished(ms);
        if (p.exitStatus() != QProcess::NormalExit || p.exitCode() != 0) {
            if (outErr)
                *outErr = QString::fromLocal8Bit(p.readAllStandardError()).trimmed();
            return false;
        }
        return true;
    };

    if (langId == QLatin1String("java")) {
        QString e;
        if (!EnvPath::setUserEnv("JAVA_HOME", path, &e) && !e.isEmpty())
            fail = e;
        else
            done << QString("JAVA_HOME = %1").arg(path);
        addToPath(QDir::toNativeSeparators(path) + QStringLiteral("\bin"));
    } else if (langId == QLatin1String("python")) {
        addToPath(path);
        if (activate) {
            const QString sel = QFileInfo(path).fileName().mid(6, 2);   // Python312 → 31
            if (!sel.isEmpty()) {
                EnvPath::setUserEnv("PY_PYTHON", sel, nullptr);
                done << QString("PY_PYTHON = %1").arg(sel);
            }
        }
    } else if (langId == QLatin1String("node")) {
        const QString nvm = QDir::homePath() + "/AppData/Roaming/nvm/nvm.exe";
        if (activate && QFile::exists(nvm)) {
            const QString ver = QFileInfo(path).fileName();
            QString e;
            if (runHidden(nvm, {"use", ver}, 60000, &e))
                done << QString("nvm use %1").arg(ver);
            else if (fail.isEmpty())
                fail = e.isEmpty() ? QStringLiteral("nvm use 失败") : e;
        } else {
            addToPath(path);
        }
    } else if (langId == QLatin1String("rust")) {
        // rustup 的 cargo/rustc 转发器全在 ~/.cargo\bin，不进 PATH 就等于"装了但没装"
        addToPath(QDir::toNativeSeparators(QDir::homePath() + "/.cargo/bin"));
        if (activate && !label.isEmpty()) {
            const QString rustup = QDir::homePath() + "/.cargo/bin/rustup.exe";
            QString e;
            if (!QFile::exists(rustup))
                fail = QStringLiteral("找不到 rustup.exe");
            else if (runHidden(rustup, {"default", label}, 120000, &e))
                done << QString("rustup default %1").arg(label);
            else if (fail.isEmpty())
                fail = e.isEmpty() ? QStringLiteral("rustup default 失败") : e;
        }
    } else if (langId == QLatin1String("c++")) {
        done << QStringLiteral("MSYS2 工具链由所选环境决定，无需改 PATH");
    } else {
        addToPath(path);
    }

    EnvPath::syncProcessPath();
    if (what)
        *what = done.join(QStringLiteral("；"));
    if (err)
        *err = fail;
    return fail.isEmpty();
}
bool wingetAvailable()
{
    static const bool available = [] {   // 探测一次 ~90ms，别每次点下拉都同步等
        QProcess p;
    p.setProgram("winget");
    p.setArguments({"--version"});
    p.setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments* a) {
        a->flags |= CREATE_NO_WINDOW;
    });
        p.start();
        return p.waitForStarted(3000) && p.waitForFinished(10000) && p.exitCode() == 0;
    }();
    return available;
}

// installLocation 搬进 src/DevEnvConfig.cpp：动作两边共用，只有「根目录长什么样、
// 配的路径在这台机器上能不能用」按平台走（platform::usableInstallRoot）。


// 装之前把目标目录建出来：MSI 的 INSTALLDIR 指向不存在的多级路径会直接失败
static QString ensureInstallDirFor(const QString& subdirName)
{
    if (subdirName.isEmpty())   // 不带 --location 的组件（系统运行库）不需要建目录
        return {};
    const QString dir = installDirFor(subdirName);
    if (!dir.isEmpty())
        QDir().mkpath(dir);
    return dir;
}

QString sanitizeDirName(const QString& name)
{
    // 目录名里不能留空格：VirtualBox 这类 exe 包装的安装器自己按空白拆 --location
    // 参数，"D:\software\Oracle VirtualBox" 会变成 INSTALLDIR=D:\software\Oracle +
    // 一个野参数，msiexec 直接 1639（无效命令行）→ 安装失败
    QString out;
    for (const QChar& c : name) {
        if (QString("\\/:*?\"<>|").contains(c))
            out += QLatin1Char('_');
        else if (c.isSpace())
            out += QLatin1Char('-');
        else
            out += c;
    }
    return out.trimmed();
}

QString installDirFor(const QString& subdirName)
{
    const QString root = installLocation();
    if (root.isEmpty())
        return QString();
    const QString sub = sanitizeDirName(subdirName);
    return QDir::toNativeSeparators(sub.isEmpty() ? root : root + "/" + sub);
}

QStringList wingetInstallArgs(const QString& wingetId, const QString& version,
                              const QString& subdirName, bool useProxy)
{
    // 固定 --source winget：msstore 源在本机联网取不到（实测 32s 超时），
    // 而它超时后 winget 会报"请使用 --source 指定"直接不装。
    // --disable-interactivity：winget 遇到要确认的事情时报错退出，而不是弹一个
    // 等用户点的窗口——一键安装/一键修复要的是全程没人插手。
    QStringList args{"install", "--id", wingetId, "-e", "--silent", "--source", "winget",
                     "--disable-interactivity",
                     "--accept-source-agreements", "--accept-package-agreements"};
    if (!version.isEmpty())
        args << "--version" << version;
    // 空 subdirName = 调用方明确不要 --location（VC++/DirectX 这类系统组件必须装到默认位置）
    const QString loc = subdirName.isEmpty() ? QString() : installDirFor(subdirName);
    if (!loc.isEmpty())
        args << "--location" << loc;   // 部分安装包不支持位置参数会失败
    // GitHub release 直链在国内裸连很慢：有代理（配置页填的或系统代理）就带上。
    // --proxy 是管理员策略开关，没启用时 winget 会连整条命令行一起拒（0x8A150002），
    // 所以撞到一次就不再传（见 startSilentInstall 的重试），否则变成谁都装不上。
    if (const QString px = proxyUrl(); useProxy && g_proxyOptionUsable && !px.isEmpty())
        args << "--proxy" << px;
    return args;
}

QString wingetInstallCommand(const QString& wingetId, const QString& version,
                             const QString& subdirName, bool useProxy)
{
    // 只用于展示：实际执行走 wingetInstallArgs，不经 shell
    QString cmd = "winget";
    for (const QString& a : wingetInstallArgs(wingetId, version, subdirName, useProxy))
        cmd += QLatin1Char(' ')
               + (a.contains(QLatin1Char(' ')) || a.contains(QLatin1Char('\\'))
                      ? QString("\"%1\"").arg(a)
                      : a);
    return cmd;
}

QString wingetCommandLine(const DevItem& item)
{
    return wingetInstallCommand(item.wingetId, QString(), item.label);
}

// 安装失败时，把 winget 指出的 MSI 日志里的关键行摘出来：光看 1603/0x8a150049 这种
// 退出码没法定位，真正的"为什么"在那本日志里（它是 UTF-16，且动辄几万行）。
QString msiLogDigest(const QString& output)
{
    int at = output.indexOf("Installer log is available at:");
    QString marker = QStringLiteral("Installer log is available at:");
    if (at < 0) {
        at = output.indexOf(QStringLiteral("安装程序日志位于:"));
        marker = QStringLiteral("安装程序日志位于:");
    }
    if (at < 0)
        return {};
    QString path = output.mid(at + marker.size()).split(QLatin1Char('\n')).first().trimmed();
    if (path.isEmpty() || !QFileInfo::exists(path))
        return {};

    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return {};
    const QByteArray raw = f.readAll();
    QString text;
    if (raw.startsWith("\xfe\xff") || raw.startsWith("\xff\xfe"))
        text = QString::fromUtf16((const char16_t*)(raw.constData() + 2),
                                  (raw.size() - 2) / 2);
    else
        text = QString::fromLocal8Bit(raw);

    static const QStringList needles = {
        QStringLiteral("MainEngineThread is returning"),
        QStringLiteral("Doing action"),
        QStringLiteral("Error 1"),
        QStringLiteral("Return value 3"),
        QStringLiteral("sufficient privileges"),
        QStringLiteral("Another process is an installer"),
        QStringLiteral("verify file hash"),
    };
    QStringList hits;
    for (const QString& line : text.split(QLatin1Char('\n'))) {
        const QString t = line.trimmed();
        if (t.isEmpty())
            continue;
        for (const QString& n : needles) {
            if (t.contains(n, Qt::CaseInsensitive)) {
                hits.append(t);
                break;
            }
        }
    }
    if (hits.isEmpty())
        return QStringLiteral("MSI 日志（%1）里没有匹配到关键行，请手动查看该文件").arg(path);
    while (hits.size() > 8)
        hits.removeFirst();
    return hits.join(QLatin1Char('\n'));
}

QString postInstallEnvSetup(const QString& langId, const QString& displayName, int code,
                            const QString& output, int jobId)
{
    // 0x8A15002B = 已是最新，也算"装好了"；
    // 0x8A150003 + "删不掉临时文件"（Win32 错误 32，中英文系统的措辞都要认）是 winget
    // 跑完安装器后清理自己的 Temp 失败，东西其实装上了，不能当失败处理
    const bool cleanupFailed = quint32(code) == 0x8A150003
        && (output.contains("cannot access the file")
            || output.contains(QStringLiteral("另一个程序正在使用"))
            || output.contains(QStringLiteral("正在被另一个进程")));
    const bool installed = code == 0 || quint32(code) == 0x8A15002B || cleanupFailed;
    if (SilentInstallLog::wasCancelled(jobId)) {
        SilentInstallLog::append(jobId, "已取消安装，跳过环境变量配置\n");
        return QStringLiteral("已取消");
    }
    if (!installed) {
        SilentInstallLog::append(jobId,
            "安装未成功，跳过环境变量配置" "\n");
        return QString();
    }
    if (code != 0) {
        const QString why = quint32(code) == 0x8A150003
            ? QStringLiteral("说明：winget 只是删不掉下载的临时文件（被占用），安装本身已完成，继续配环境变量")
            : QStringLiteral("说明：winget 报告已是最新版本，按已安装处理");
        SilentInstallLog::append(jobId, (why + QLatin1Char('\n')).toUtf8());
    }
    EnvPath::syncProcessPath();   // 安装器自己改的 PATH 也先吸收进来
    const QVector<LangVersion> vers = detectVersions(langId);
    if (vers.isEmpty()) {
        SilentInstallLog::append(jobId,
            QString("%1 已安装，但没探测到可配置的本机路径，环境变量请手动检查" "\n")
                .arg(displayName).toUtf8());
        return QStringLiteral("没探测到可配置的本机路径");
    }
    QString what, err;
    // activate=false：只保证"能用"，不去改用户当前选中的版本
    const bool ok = applyEnvFor(langId, vers.first().path, vers.first().label, false, &what, &err);
    SilentInstallLog::append(jobId,
        (ok ? QString("环境变量：%1" "\n").arg(what)
            : QString("环境变量配置失败：%1" "\n").arg(err)).toUtf8());
    return ok ? what : err;
}

bool startSilentInstall(const QString& wingetId, const QString& version,
                        const QString& subdirName, QString* msg,
                        std::function<void(int, const QString&, int)> done)
{
    if (wingetId.isEmpty()) {
        if (msg)
            *msg = "该条目没有配置 winget 包";
        return false;
    }
    if (!wingetAvailable()) {
        if (msg)
            *msg = "系统里没有 winget 命令（安装 App Installer 后重试）";
        return false;
    }
    const QString target = ensureInstallDirFor(subdirName);
    const QString cmd = wingetInstallCommand(wingetId, version, subdirName);
    const QString title = version.isEmpty()
                              ? wingetId
                              : QString("%1 %2").arg(wingetId, version);
    const int jobId = SilentInstallLog::begin(title, target, cmd);
    // 把"这次到底走没走代理"写在日志看台第一屏：没代理时 GitHub 包慢半天，得让人看懂原因
    const QString px = proxyUrl();
    SilentInstallLog::append(jobId, (px.isEmpty()
        ? QStringLiteral("代理：未启用。GitHub 直链会非常慢，可在配置页「下载代理」填 http://127.0.0.1:10808\n")
        : (g_proxyOptionUsable
               ? QStringLiteral("代理：%1\n").arg(px)
               : QStringLiteral("代理：%1（但 winget 不接受命令行代理，本次不带）\n").arg(px)))
                                   .toUtf8());
    auto* p = new QProcess(qApp);   // 归 app 管：切页/关日志窗都不影响安装
    p->setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments* a) {
        a->flags |= CREATE_NO_WINDOW;
    });
    p->setProcessChannelMode(QProcess::MergedChannels);
    auto out = std::make_shared<QByteArray>();
    QObject::connect(p, &QProcess::readyReadStandardOutput, p, [p, jobId, out] {
        const QByteArray chunk = p->readAllStandardOutput();
        *out += chunk;
        SilentInstallLog::append(jobId, chunk);
    });
    QObject::connect(p, &QProcess::finished, p,
                     [p, cmd, jobId, out, done, wingetId, version, subdirName](int code) {
        Utils::log(QString("silent install exit=%1 cmd=%2").arg(code).arg(cmd));
        // winget 的输出混着 UTF-8 和本地 GBK，和日志看台用同一套解码
        QString text = QString::fromUtf8(*out);
        if (text.contains(QChar(0xFFFD)))
            text = QString::fromLocal8Bit(*out);
        if (code != 0 && g_proxyOptionUsable && text.contains("ProxyCommandLineOptions")) {
            SilentInstallLog::append(jobId,
                "winget 不接受命令行 --proxy：要管理员先执行一次 "
                "winget settings --enable ProxyCommandLineOptions。"
                "本次起 ltool 不再给 winget 传 --proxy，改用无代理重试。\n");
            g_proxyOptionUsable = false;
            // 这一条要收尾，不然看台里永远挂着个"进行中…"；重试是另一个任务
            SilentInstallLog::finish(jobId, code);
            p->deleteLater();
            startSilentInstall(wingetId, version, subdirName, nullptr, done);
            return;
        }
        // 用户点了「取消安装」：退出码是被我们打断的噪音，别再往下贴失败提示
        const bool cancelled = SilentInstallLog::wasCancelled(jobId);
        if (code != 0 && !cancelled) {
            if (text.contains("1618") || text.contains("installation is already in progress")
                || text.contains("另一个安装"))
                SilentInstallLog::append(jobId,
                    QString("提示：Windows Installer 同一时刻只允许一个安装（1618）。"
                            "等前一个装完再点，别连着点。\n").toUtf8());
            else if (text.contains("1603") || text.contains("run as administrator")
                     || text.contains("Installer failed with exit code"))
                SilentInstallLog::append(jobId,
                    QString("提示：这个包要管理员权限才能装。用管理员身份重新打开 ltool 再点一次，"
                            "或者在管理员终端里执行上面那行 winget 命令。\n").toUtf8());
            const QString digest = msiLogDigest(text);
            if (!digest.isEmpty())
                SilentInstallLog::append(jobId, (digest + QLatin1Char('\n')).toUtf8());
        }
        SilentInstallLog::finish(jobId, code);
        if (done)
            done(code, text, jobId);
        p->deleteLater();
    });
    // 直接 winget + 参数表：不套 cmd.exe，否则 --location "D:\...\7-Zip" 的引号会被
    // cmd 剥掉，winget 转给 msiexec 的 INSTALLDIR 变成非法参数（0x8a15004a）
    p->start("winget", wingetInstallArgs(wingetId, version, subdirName));
    SilentInstallLog::attachProcess(jobId, p);   // 交给看台，托盘上才有「取消安装」可点
    Utils::log(QString("silent install start: %1").arg(cmd));
    if (msg)
        *msg = cmd;
    return true;
}

// ---- winget 版本列表缓存（versions.json）----
// 结构：{ "包ID": { "versions": ["1.2", ...], "fetched": "毫秒时间戳" } }
QJsonObject loadVersionDoc()
{
    QFile f(Utils::dataDir() + "/versions.json");
    if (!f.open(QIODevice::ReadOnly))
        return {};
    return QJsonDocument::fromJson(f.readAll()).object();
}

QStringList cachedVersions(const QString& wingetId)
{
    QStringList out;
    for (const auto& v : loadVersionDoc().value(wingetId).toObject().value("versions").toArray())
        out.append(v.toString());
    return out;
}

qint64 versionCacheTime(const QString& wingetId)
{
    return loadVersionDoc().value(wingetId).toObject().value("fetched").toVariant().toLongLong();
}

void saveVersionCache(const QString& wingetId, const QStringList& versions)
{
    QJsonObject o = loadVersionDoc();
    QJsonArray arr;
    for (const QString& v : versions)
        arr.append(v);
    QJsonObject entry;
    entry.insert("versions", arr);
    entry.insert("fetched", QString::number(QDateTime::currentMSecsSinceEpoch()));
    o.insert(wingetId, entry);
    QFile out(Utils::dataDir() + "/versions.json");
    if (out.open(QIODevice::WriteOnly))
        out.write(QJsonDocument(o).toJson(QJsonDocument::Indented));
}

void fetchVersionsAsync(const QString& wingetId, std::function<void(QStringList)> done)
{
    auto* p = new QProcess(qApp);
    p->setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments* a) {
        a->flags |= CREATE_NO_WINDOW;
    });
    QObject::connect(p, &QProcess::finished, p, [p, wingetId, done](int) {
        const QString out = QString::fromLocal8Bit(p->readAllStandardOutput()
                                                   + p->readAllStandardError());
        static const QRegularExpression verRe("^\\s*v?[0-9]+(\\.[0-9]+)+\\s*$");
        QStringList versions;
        for (const QString& line : out.split(QLatin1Char('\n'))) {
            const QString t = line.trimmed();
            if (verRe.match(t).hasMatch())
                versions.append(t);
        }
        p->deleteLater();
        if (!versions.isEmpty())
            saveVersionCache(wingetId, versions);
        done(versions);   // 失败/空也回调，调用方才能解除"正在抓取"状态
    });
    p->start("winget", {"show", "--id", wingetId, "--versions", "--source", "winget",
                         "--accept-source-agreements"});   // 不指定源会先等 msstore 超时
}

bool runInstall(QWidget* parent, const DevItem& item, QString* err)
{
    if (item.wingetId.isEmpty()) {
        if (err)
            *err = "该组件没有配置 winget 包";
        return false;
    }
    // 隐藏控制台，弹出安装过程对话框实时显示输出
    ensureInstallDirFor(item.label);
    InstallLogDialog dlg(parent, item.label, wingetCommandLine(item),
                         QStringLiteral("winget"),
                         wingetInstallArgs(item.wingetId, QString(), item.label));
    dlg.exec();
    return true;
}

} // namespace DevEnv
