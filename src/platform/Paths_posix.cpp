#include "platform/Paths.h"

#include <QDir>
#include <QFileInfo>
#include <QProcessEnvironment>
#include <QStandardPaths>
#include <QStringList>

namespace Platform {

namespace {

// standardLocations 而不是 writableLocation：后者会顺手把目录建出来，
// 「问一句在哪儿」不该在盘上留一个空文件夹（Utils::mediaRoot() 里是同一讲究）
QString firstLocation(QStandardPaths::StandardLocation loc)
{
    const QStringList all = QStandardPaths::standardLocations(loc);
    return all.isEmpty() ? QString() : all.first();
}

#ifndef Q_OS_MACOS
// $XDG_DATA_DIRS 里第一个真实存在的 applications；都没配就退回系统默认位置
QString systemApplicationsDir()
{
    const QString raw = QProcessEnvironment::systemEnvironment().value(
        QStringLiteral("XDG_DATA_DIRS"),
        QStringLiteral("/usr/local/share:/usr/share"));
    const QStringList parts = raw.split(QLatin1Char(':'), Qt::SkipEmptyParts);
    for (const QString& p : parts) {
        const QString cand = p + QStringLiteral("/applications");
        if (QFileInfo::exists(cand))
            return cand;
    }
    return QStringLiteral("/usr/share/applications");
}
#endif

} // namespace

QString knownDir(KnownDir d)
{
    switch (d) {
    case KnownDir::UserDesktop:
        return firstLocation(QStandardPaths::DesktopLocation);
    case KnownDir::SharedDesktop:
        return {};   // 没有「所有用户共用一个桌面」这回事
    case KnownDir::UserApps:
#ifdef Q_OS_MACOS
        return QDir::homePath() + QStringLiteral("/Applications");
#else
        return firstLocation(QStandardPaths::GenericDataLocation) + QStringLiteral("/applications");
#endif
    case KnownDir::SystemApps:
#ifdef Q_OS_MACOS
        return QStringLiteral("/Applications");
#else
        return systemApplicationsDir();
#endif
    }
    return {};
}

QString expandEnv(const QString& s)
{
    const QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    QString out;
    out.reserve(s.size());
    for (int i = 0; i < s.size(); ) {
        if (s[i] == QLatin1Char('$') && i + 1 < s.size()) {
            const bool braced = s[i + 1] == QLatin1Char('{');
            const int nameStart = braced ? i + 2 : i + 1;
            int k = nameStart;
            while (k < s.size() && (s[k].isLetterOrNumber() || s[k] == QLatin1Char('_')))
                ++k;
            const bool closed = !braced || (k < s.size() && s[k] == QLatin1Char('}'));
            const QString name = s.mid(nameStart, k - nameStart);
            if (!name.isEmpty() && closed && env.contains(name)) {
                out += env.value(name);
                i = k + (braced ? 1 : 0);
                continue;
            }
        }
        out += s[i];
        ++i;
    }
    return out;
}

} // namespace Platform
