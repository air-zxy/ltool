#include "InstallLogDialog.h"
#include "Theme.h"
#include "Utils.h"
#include "platform/Launch.h"

#include <QLabel>
#include <QPlainTextEdit>
#include <QProcess>
#include <QPushButton>
#include <QVBoxLayout>


#include <memory>

InstallLogDialog::InstallLogDialog(QWidget* parent, const QString& title,
                                   const QString& displayCommand, const QString& program,
                                   const QStringList& args)
    : QDialog(parent)
{
    setWindowTitle("安装 " + title);
    resize(660, 400);

    auto* lay = new QVBoxLayout(this);
    lay->addWidget(new QLabel(QString("正在安装 %1（过程如下，完成后可直接关闭）").arg(title), this));
    auto* cmd = new QLabel("$ " + displayCommand, this);
    cmd->setWordWrap(true);
    Theme::bind(cmd, "color:@muted;");
    lay->addWidget(cmd);
    auto* log = new QPlainTextEdit(this);
    log->setReadOnly(true);
    lay->addWidget(log, 1);
    auto* closeBtn = new QPushButton("关闭", this);
    closeBtn->setDefault(true);
    auto* btns = new QHBoxLayout;
    btns->addStretch(1);
    btns->addWidget(closeBtn);
    lay->addLayout(btns);
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::accept);

    auto* proc = new QProcess(this);
    Platform::runWithoutConsole(proc);
    auto lines = std::make_shared<Utils::ConsoleLines>();
    connect(proc, &QProcess::readyReadStandardOutput, this, [log, proc, lines] {
        const QString out = lines->feed(proc->readAllStandardOutput());
        if (!out.isEmpty())
            log->appendPlainText(out.trimmed());
    });
    connect(proc, &QProcess::finished, this, [log, closeBtn, lines](int code, QProcess::ExitStatus st) {
        const QString rest = lines->tail();
        if (!rest.isEmpty())
            log->appendPlainText(rest.trimmed());
        log->appendPlainText(QString("=== 结束（%1，退出码 %2 / 0x%3）===").arg(
            st == QProcess::NormalExit ? "正常" : "异常",
            QString::number(code), QString::number(quint32(code), 16)));
        closeBtn->setText("完成");
    });
    proc->setProcessChannelMode(QProcess::MergedChannels);
    proc->start(program, args);   // 直接起进程：过 cmd.exe 会剥掉 --location 的引号
}
