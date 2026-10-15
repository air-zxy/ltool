#include "TaskScheduler.h"
#include "Utils.h"

#include <QThreadPool>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QSaveFile>
#include <QTimer>
#include <QUuid>

#include <windows.h>

namespace {

QString tasksFile() { return Utils::dataDir() + "/tasks.json"; }
QString taskLogDir() { return Utils::dataDir() + "/tasklogs"; }

void noWindow(QProcess* p)
{
    p->setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments* a) {
        a->flags |= CREATE_NO_WINDOW;
    });
}

QString shortStamp()
{
    return QDateTime::currentDateTime().toString("yyyyMMdd-hhmmss");
}

// 同步编译（c++/rust），返回是否成功；失败信息写入 err
bool compile(const QString& program, const QStringList& args, QString* err)
{
    QProcess p;
    noWindow(&p);
    p.setProgram(program);
    p.setArguments(args);
    p.start();
    if (!p.waitForStarted(5000) || !p.waitForFinished(180000)) {
        p.kill();
        if (err) *err = QString("编译器启动/执行失败：%1").arg(program);
        return false;
    }
    if (p.exitCode() != 0) {
        const QString out = QString::fromUtf8(p.readAllStandardOutput() + p.readAllStandardError());
        if (err) *err = "编译失败：\n" + out.trimmed();
        return false;
    }
    return true;
}

} // namespace

TaskScheduler& TaskScheduler::instance()
{
    static TaskScheduler inst;
    return inst;
}

QStringList TaskScheduler::typeNames()
{
    return {"python", "js", "java", "c++", "rust"};
}

void TaskScheduler::start()
{
    load();
    auto* timer = new QTimer(this);
    connect(timer, &QTimer::timeout, this, &TaskScheduler::tick);
    timer->start(30 * 1000);
    tick();
}

QVector<ScheduledTask> TaskScheduler::tasks() const
{
    return m_tasks;
}

void TaskScheduler::setTasks(const QVector<ScheduledTask>& tasks)
{
    m_tasks = tasks;
    save();
    emit tasksChanged();
}

void TaskScheduler::load()
{
    QFile f(tasksFile());
    if (!f.open(QIODevice::ReadOnly))
        return;
    const QJsonArray arr = QJsonDocument::fromJson(f.readAll()).array();
    for (const auto& v : arr) {
        const QJsonObject o = v.toObject();
        ScheduledTask t;
        t.id = o.value("id").toString();
        t.name = o.value("name").toString();
        t.type = o.value("type").toString();
        t.script = o.value("script").toString();
        t.schedule = o.value("schedule").toString();
        t.enabled = o.value("enabled").toBool(true);
        t.lastRunMs = o.value("lastRunMs").toVariant().toLongLong();
        t.lastDay = o.value("lastDay").toString();
        if (!t.name.isEmpty() && !t.script.isEmpty())
            m_tasks.append(t);
    }
}

void TaskScheduler::save()
{
    QDir().mkpath(Utils::dataDir());
    QJsonArray arr;
    for (const ScheduledTask& t : m_tasks) {
        QJsonObject o;
        o.insert("id", t.id);
        o.insert("name", t.name);
        o.insert("type", t.type);
        o.insert("script", t.script);
        o.insert("schedule", t.schedule);
        o.insert("enabled", t.enabled);
        o.insert("lastRunMs", QString::number(t.lastRunMs));
        o.insert("lastDay", t.lastDay);
        arr.append(o);
    }
    QSaveFile f(tasksFile());
    if (f.open(QIODevice::WriteOnly)) {
        f.write(QJsonDocument(arr).toJson(QJsonDocument::Compact));
        f.commit();
    }
}

bool TaskScheduler::isDue(const ScheduledTask& t) const
{
    if (!t.enabled)
        return false;
    const QDateTime now = QDateTime::currentDateTime();
    if (t.schedule.startsWith("interval:")) {
        const int minutes = t.schedule.mid(9).toInt();
        if (minutes <= 0)
            return false;
        if (t.lastRunMs == 0)
            return true;
        return t.lastRunMs + qint64(minutes) * 60 * 1000 <= now.toMSecsSinceEpoch();
    }
    if (t.schedule.startsWith("daily:")) {
        const QTime at = QTime::fromString(t.schedule.mid(6), "HH:mm");
        if (!at.isValid())
            return false;
        const QString today = now.date().toString("yyyyMMdd");
        return now.time() >= at && t.lastDay != today;
    }
    return false;
}

void TaskScheduler::tick()
{
    bool dirty = false;
    for (ScheduledTask& t : m_tasks) {
        if (!isDue(t))
            continue;
        const QString err = runTask(t, nullptr);
        t.lastRunMs = QDateTime::currentDateTime().toMSecsSinceEpoch();
        t.lastDay = QDateTime::currentDateTime().date().toString("yyyyMMdd");
        dirty = true;
        if (!err.isEmpty())
            Utils::log(QString("task %1 error: %2").arg(t.name, err));
        else
            Utils::log(QString("task %1 triggered").arg(t.name));
    }
    if (dirty)
        save();
}

QString TaskScheduler::runTask(const ScheduledTask& t, QString* err)
{
    QFileInfo fi(t.script);
    if (!fi.exists()) {
        const QString e = QString("脚本不存在：%1").arg(t.script);
        if (err)
            *err = e;
        return e;
    }

    QDir().mkpath(taskLogDir());
    const QString logPath = QString("%1/%2-%3.log")
                                .arg(taskLogDir(), t.name, shortStamp());

    // python / js / java：解释执行
    QString program;
    QStringList args;
    if (t.type == "python") {
        program = "python";
        args = {t.script};
    } else if (t.type == "js") {
        program = "node";
        args = {t.script};
    } else if (t.type == "java") {
        program = "java";
        args = {t.script}; // Java 11+ 单文件运行
    }

    if (!program.isEmpty()) {
        auto* p = new QProcess;
        noWindow(p);
        p->setWorkingDirectory(fi.absolutePath());
        p->setProgram(program);
        p->setArguments(args);
        p->setProcessChannelMode(QProcess::MergedChannels);
        QObject::connect(p, &QProcess::errorOccurred, p, [p, logPath](QProcess::ProcessError) {
            QFile log(logPath);
            if (log.open(QIODevice::Append | QIODevice::Text))
                log.write(QString("process error: %1\n").arg(p->errorString()).toUtf8());
        });
        QObject::connect(p, &QProcess::finished, p, [p, logPath] {
            QFile log(logPath);
            if (log.open(QIODevice::Append | QIODevice::Text)) {
                log.write(QString("exit=%1\n%2\n")
                              .arg(p->exitCode())
                              .arg(QString::fromUtf8(p->readAll()))
                              .toUtf8());
            }
            p->deleteLater();
        });
        p->start();
        return {};
    }

    // c++ / rust：先编译到缓存目录，再运行
    QString compiler, cargs;
    QString exe = QString("%1/%2-%3.exe").arg(taskLogDir(), t.name, shortStamp());
    if (t.type == "c++") {
        compiler = "g++";
        args = {"-O2", t.script, "-o", exe};
    } else if (t.type == "rust") {
        compiler = "rustc";
        args = {"-O", t.script, "-o", exe};
    } else {
        const QString e = QString("未知任务类型：%1").arg(t.type);
        if (err)
            *err = e;
        return e;
    }

    // 编译放后台线程，避免阻塞调度线程
    const ScheduledTask task = t;
    QThreadPool::globalInstance()->start([task, compiler, args, exe, logPath]() {
        QString cerr;
        if (!compile(compiler, args, &cerr)) {
            QFile log(logPath);
            if (log.open(QIODevice::Append | QIODevice::Text))
                log.write(cerr.toUtf8());
            Utils::log(QString("task %1 compile failed").arg(task.name));
            return;
        }
        auto* p = new QProcess;
        noWindow(p);
        p->setWorkingDirectory(QFileInfo(task.script).absolutePath());
        p->setProgram(exe);
        p->setProcessChannelMode(QProcess::MergedChannels);
        QObject::connect(p, &QProcess::finished, p, [p, logPath, exe] {
            QFile log(logPath);
            if (log.open(QIODevice::Append | QIODevice::Text)) {
                log.write(QString("exit=%1\n%2\n")
                              .arg(p->exitCode())
                              .arg(QString::fromUtf8(p->readAll()))
                              .toUtf8());
            }
            QFile::remove(exe);
            p->deleteLater();
        });
        p->start();
    });
    return {};
}

QString TaskScheduler::runNow(const QString& id)
{
    for (const ScheduledTask& t : m_tasks) {
        if (t.id != id)
            continue;
        QString err;
        runTask(t, &err);
        return err;
    }
    return QString("任务不存在");
}
