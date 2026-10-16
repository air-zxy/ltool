#include "platform/Autostart.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>

namespace Platform {

namespace {

QString autostartDir()
{
#ifdef Q_OS_MACOS
    return QDir::homePath() + QStringLiteral("/Library/LaunchAgents");
#else
    return QDir::homePath() + QStringLiteral("/.config/autostart");
#endif
}

QString autostartFile()
{
#ifdef Q_OS_MACOS
    return autostartDir() + QStringLiteral("/com.ltool.autostart.plist");
#else
    return autostartDir() + QStringLiteral("/ltool.desktop");
#endif
}

// .desktop 的 Exec 走的是 unix 引号规则：空格、制表、换行、双引号、反引号都要转义，
// 否则装到 "My Apps" 这种带空格的目录里，那一行会被当成两个参数。
QString xdgQuote(const QString& s)
{
    QString out;
    out.reserve(s.size() + 8);
    for (const QChar c : s) {
        switch (c.unicode()) {
        case u' ': case u'\t': case u'\n': case u'"': case u'`':
            out += QLatin1Char('\\');
            out += c;
            break;
        default:
            out += c;
        }
    }
    return out;
}

QString xmlEscape(const QString& s)
{
    QString out = s;
    out.replace(QLatin1Char('&'), QStringLiteral("&amp;"));
    out.replace(QLatin1Char('<'), QStringLiteral("&lt;"));
    out.replace(QLatin1Char('>'), QStringLiteral("&gt;"));
    return out;
}

} // namespace

QString autostartTarget() { return autostartFile(); }

QString autostartEntry(const QString& exePath)
{
#ifdef Q_OS_MACOS
    return QStringLiteral(
               "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
               "<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" "
               "\"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">\n"
               "<plist version=\"1.0\"><dict>\n"
               "  <key>Label</key><string>com.ltool.autostart</string>\n"
               "  <key>ProgramArguments</key><array><string>%1</string></array>\n"
               "  <key>RunAtLoad</key><true/>\n"
               "</dict></plist>\n")
        .arg(xmlEscape(exePath));
#else
    return QStringLiteral(
               "[Desktop Entry]\n"
               "Type=Application\n"
               "Name=ltool\n"
               "Comment=ltool 开机自启（勾掉这颗开关就删掉本文件）\n"
               "Exec=%1\n"
               "Terminal=false\n"
               "X-GNOME-Autostart-enabled=true\n")
        .arg(xdgQuote(exePath));
#endif
}

void setAutostart(bool on)
{
    if (!on) {
        QFile::remove(autostartFile());
        return;
    }
    QDir().mkpath(autostartDir());
    QFile f(autostartFile());
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return;
    f.write(autostartEntry(QCoreApplication::applicationFilePath()).toUtf8());
    f.close();
}

bool autostartEnabled() { return QFileInfo::exists(autostartFile()); }

} // namespace Platform
