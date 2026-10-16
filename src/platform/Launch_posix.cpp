#include "platform/Launch.h"

#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <QStringList>

namespace Platform {

namespace {

#ifdef Q_OS_MACOS
// 交给 sh 之前每个词都要独立成引号段：路径里可能带空格、引号甚至换行，
// 拼成一条没引号的命令串就是让别人往这台机器上塞命令的机会。
QString shellWord(const QString& v)
{
    QString out = QStringLiteral("'");
    for (const QChar c : v) {
        if (c == QLatin1Char('\''))
            out += QStringLiteral("'\\''");
        else
            out += c;
    }
    return out + QStringLiteral("'");
}

// 上面那条命令串还要再塞进 AppleScript 的双引号字符串里，反斜杠和双引号得先给它转好
QString appleScriptString(const QString& shellCmd)
{
    QString s = shellCmd;
    s.replace(QLatin1Char('\\'), QStringLiteral("\\\\"));
    s.replace(QLatin1Char('"'), QStringLiteral("\\\""));
    return s;
}
#endif

// shell 那句「一个字符串塞一串参数」的切法，Windows 有 shell 替我们做，POSIX 得自己来。
// 只认成对的双引号，够覆盖调用方实际写的那几种（/S、/VERYSILENT、/GUID={…} 一类）。
QStringList splitArgs(const QString& params)
{
    QStringList out;
    QString cur;
    bool quoted = false;
    for (const QChar c : params) {
        if (c == QLatin1Char('"')) {
            quoted = !quoted;
            continue;
        }
        if (!quoted && (c == QLatin1Char(' ') || c == QLatin1Char('\t'))) {
            if (!cur.isEmpty())
                out << cur;
            cur.clear();
            continue;
        }
        cur += c;
    }
    if (!cur.isEmpty())
        out << cur;
    return out;
}

} // namespace

bool launch(const QString& path, const QString& params, const QString& workingDir, bool async)
{
    Q_UNUSED(async)
    if (path.isEmpty())
        return false;
    QString wd = workingDir;
    if (wd.isEmpty())
        wd = QFileInfo(path).absolutePath();
    if (!QFileInfo(wd).isDir())
        wd.clear();

    const QStringList args = splitArgs(params);
    const QFileInfo fi(path);
    if (fi.isFile() && fi.isExecutable())
        return QProcess::startDetached(fi.absoluteFilePath(), args, wd);

    // 文件夹、URL、没有执行位的文件都归系统 opener 管。.desktop 条目走这条（它们通常没 +x）。
    // opener 不接「附带参数」—— 那种只有 Windows 文件关联才有的语义，POSIX 上如实起不来。
    if (!args.isEmpty())
        return false;
#ifdef Q_OS_MACOS
    return QProcess::startDetached(QStringLiteral("/usr/bin/open"), {path}, wd);
#else
    return QProcess::startDetached(QStringLiteral("xdg-open"), {path}, wd);
#endif
}

bool launchAsAdmin(const QString& path, const QString& params, const QString& workingDir)
{
    if (path.isEmpty())
        return false;
    const QStringList args = splitArgs(params);
    QString wd = workingDir;
    if (!QFileInfo(wd).isDir())
        wd.clear();
#ifdef Q_OS_MACOS
    QStringList words;
    words << shellWord(QFileInfo(path).absoluteFilePath());
    for (const QString& a : args)
        words << shellWord(a);
    const QString script = QStringLiteral("do shell script \"%1\" with administrator privileges")
                               .arg(appleScriptString(words.join(QLatin1Char(' '))));
    return QProcess::startDetached(QStringLiteral("/usr/bin/osascript"),
                                   {QStringLiteral("-e"), script}, wd);
#else
    QStringList argv;
    argv << path << args;
    return QProcess::startDetached(QStringLiteral("pkexec"), argv, wd);
#endif
}

void reveal(const QString& path)
{
    const QFileInfo fi(path);
#ifdef Q_OS_MACOS
    const QStringList argv{QStringLiteral("-R"), fi.absoluteFilePath()};
    QProcess::startDetached(QStringLiteral("/usr/bin/open"), argv);
#else
    // xdg-open 没有「选中这一项」，只能把父目录开出来
    QProcess::startDetached(QStringLiteral("xdg-open"), {fi.absolutePath()});
#endif
}

// 这个问题在这个平台上不存在：POSIX 起子进程不会弹出一个控制台窗口，所以没动作可做。
// （真要"脱离控制终端"是 setsid 那一类事，不归这条缝，也别在这儿假装做了。）
void runWithoutConsole(QProcess*)
{
}

} // namespace Platform
