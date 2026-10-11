#pragma once

#include <QDialog>
#include <QStringList>

// 安装过程弹框：隐藏控制台直接启动程序（不过 shell，避免 --location 引号被剥掉），
// 实时显示输出（搜索框里安装开发组件时用）
class InstallLogDialog : public QDialog {
    Q_OBJECT
public:
    InstallLogDialog(QWidget* parent, const QString& title, const QString& displayCommand,
                     const QString& program, const QStringList& args);
};
