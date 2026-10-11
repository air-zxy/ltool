#pragma once

#include <QWidget>

class QCheckBox;
class QLabel;
class QPlainTextEdit;
class QTreeWidget;
class QTreeWidgetItem;

// JSON 格式化工具：左边可折叠的树，右边可编辑的文本。
// 文本区是唯一事实来源：改完点「格式化」重新解析并刷新树；输入时也会防抖解析，
// 解析失败只在状态栏说一声，不会把上一次的好结果清掉。
// 入口有两个：托盘菜单「JSON 格式化」和启动器命令 json。
class JsonTool : public QWidget {
    Q_OBJECT
public:
    static void open(const QString& text = QString());   // 复用同一个窗口（不叫 show：会遮住 QWidget::show）

protected:
    void dropEvent(QDropEvent* e) override;
    void dragEnterEvent(QDragEnterEvent* e) override;

private:
    explicit JsonTool(QWidget* parent = nullptr);
    void reparse(bool rewriteText);       // rewriteText: 把规范化后的 JSON 写回文本区
    void buildTree(const QJsonValue& root);
    void expandAll(bool on);
    void setStatus(const QString& text, bool ok);
    void loadFile(const QString& path);

    QTreeWidget* m_tree = nullptr;
    QPlainTextEdit* m_edit = nullptr;
    QLabel* m_status = nullptr;
    QCheckBox* m_wrap = nullptr;
};
