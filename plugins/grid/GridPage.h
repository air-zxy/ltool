#pragma once

#include <QColor>
#include <QImage>
#include <QWidget>

class GridBoard;
class QLabel;
class QPushButton;

// 图片排版那一页：选几宫（四/六/九）→ 把图拖进格子 → 拖动换顺序 → 挑一个背景色 → 生成。
//
// 生成出来的是一张正方形格子的合成图：每格居中裁成正方形（和板上预览同一条算法），
// 空格子就是背景色。落点用配置里那个「截图存放目录」，文件名 grid-<时间>-<n>宫.png。
class GridPage : public QWidget
{
    Q_OBJECT
public:
    explicit GridPage(QWidget* parent = nullptr);

    GridBoard* board() const { return m_board; }
    void setSlots(int n);                       // 4 / 6 / 9，同时把那一颗按钮按下去
    int slotCount() const;

    // 合成：每格 cellPx 像素、格子之间留 gap 像素、底色 bg。空格子只留底色。
    QImage compose(int cellPx, int gap, const QColor& bg) const;

    // 生成并落盘，返回写出来的文件路径（失败回空串，话写在状态行里）。
    QString generate();

    void setBackgroundColor(const QColor& c);
    QColor backgroundColor() const { return m_bg; }
    void setCellPixels(int px) { m_cellPx = qBound(128, px, 4096); }

signals:
    void generated(const QString& file);        // 自测拿它确认"真写出来了"

private:
    void pickColor();
    void addFromFile();
    void showStatus(const QString& text);

    GridBoard* m_board = nullptr;
    QLabel* m_status = nullptr;
    QPushButton* m_bgBtn = nullptr;
    QPushButton* m_openBtn = nullptr;
    QPushButton* m_dirBtn = nullptr;
    QVector<QPushButton*> m_slotBtns;           // 索引 0/1/2 = 四/六/九
    QColor m_bg = Qt::white;
    int m_cellPx = 1080;
    QString m_lastFile;
};
