#pragma once

#include <QWidget>

class QLabel;
class QPushButton;
class Recorder;

// F2 录屏的界面那一层：
//  WindowPicker = 列出"整个屏幕"+ 桌面上看得见的窗口，选一个要录的（不弹就不知道该录谁）
//  RecordBar    = 录制中挂在屏幕上的小条：红点 + 计时 + 帧数 + 停止
// 二者都只是 Recorder 的壳，编码/取帧全在 Recorder 里。
namespace RecordUi {

// F2 的入口：弹选择列表，选完就开录（已经在录时什么都不做）
void startViaPicker();
bool isRecording();

} // namespace RecordUi

class WindowPicker : public QWidget {
    Q_OBJECT
public:
    explicit WindowPicker(QWidget* parent = nullptr);

signals:
    // monitor=true 时 target 是 HMONITOR（录整块屏幕），否则是 HWND（录那一个窗口）
    void picked(WId target, const QString& title, bool monitor);

private:
    QWidget* m_list = nullptr;
};

class RecordBar : public QWidget {
    Q_OBJECT
public:
    RecordBar(Recorder* rec, const QString& path, QWidget* parent = nullptr);

protected:
    void mousePressEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void mouseReleaseEvent(QMouseEvent*) override;

private:
    void poll();
    void finish(const QString& error);

    Recorder* m_rec;
    QString m_path;
    QLabel* m_text = nullptr;
    QPushButton* m_stop = nullptr;
    QPoint m_press;
    bool m_dragging = false;
    bool m_finished = false;
};
