#include "platform/Autostart.h"

#include <QCoreApplication>
#include <QDir>
#include <QSettings>

namespace Platform {

namespace {
QString runKey()
{
    return QStringLiteral("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Run");
}
} // namespace

QString autostartTarget() { return runKey() + QStringLiteral("\\ltool"); }

QString autostartEntry(const QString& exePath)
{
    return QDir::toNativeSeparators(exePath);
}

void setAutostart(bool on)
{
    QSettings run(runKey(), QSettings::NativeFormat);
    if (on)
        run.setValue("ltool", autostartEntry(QCoreApplication::applicationFilePath()));
    else
        run.remove("ltool");
}

bool autostartEnabled()
{
    QSettings run(runKey(), QSettings::NativeFormat);
    return run.contains("ltool");
}

} // namespace Platform
