#pragma once

#include <QWidget>

class QLabel;
class QPushButton;
class QPlainTextEdit;

// Base64 编解码工具：和 JsonTool 同一套用法（独立窗口，托盘菜单/设置菜单/命令 b64 进入）。
// 默认"解码"模式：粘贴一串 base64 直接看结果；解不出合法文本时给十六进制而不是乱码。
class Base64Tool : public QWidget {
    Q_OBJECT
public:
    static void open(const QString& input = QString());

protected:
    void dropEvent(QDropEvent* e) override;
    void dragEnterEvent(QDragEnterEvent* e) override;

private:
    explicit Base64Tool(QWidget* parent = nullptr);
    void recompute();
    void setMode(bool decode);
    void applyModeUi(bool decode);   // 只同步按钮/标题，不触发重新计算
    void setStatus(const QString& text, bool ok);

    QPlainTextEdit* m_input = nullptr;
    QPlainTextEdit* m_out = nullptr;
    QLabel* m_status = nullptr;
    QPushButton* m_decodeBtn = nullptr;
    QPushButton* m_encodeBtn = nullptr;
    bool m_decode = true;
};
