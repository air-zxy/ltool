#pragma once

#include <QWidget>

class QListWidget;
class QCheckBox;
class QComboBox;
class QLabel;
class QSpinBox;
class QLineEdit;
class QKeySequenceEdit;

// 配置 - 配置页：开机启动 / 快捷键 / 搜索引擎 / 定时扫描（索引刷新）/ 应用目录 / 剪贴板 / 微软拼音标点
class ConfigPage : public QWidget {
    Q_OBJECT
public:
    explicit ConfigPage(QWidget* parent = nullptr);

signals:
    // 保存后通知：快捷键变更 / 索引设置变化需要重扫
    void hotkeyChanged(const QString& sequence);
    void rescanRequested();

private slots:
    void onSave();
    void onOpenDataDir();
    void onClearIndexCache();
    void addAppRoot();
    void addAppExe();
    void removeAppRoot();

private:
    void load();
    // 把"现在存在哪、有多少文件、多大"刷到那行说明上（打开页面和保存之后都要刷）
    void refreshPathInfo();

    QCheckBox* m_autostart = nullptr;
    QKeySequenceEdit* m_hotkey = nullptr;
    QLineEdit* m_installPath = nullptr;
    QLineEdit* m_proxy = nullptr;
    QComboBox* m_engine = nullptr;
    QCheckBox* m_deep = nullptr;
    QSpinBox* m_maxEntries = nullptr;
    QSpinBox* m_refreshHours = nullptr;
    // 应用搜索的额外目录（config.ini 的 apps/roots）+ 往里找几层（apps/depth）
    QListWidget* m_appRoots = nullptr;
    QSpinBox* m_appDepth = nullptr;
    QLabel* m_appInfo = nullptr;
    // 截图 / 录像的存放目录（config.ini 的 paths/captures、paths/recordings）
    QLineEdit* m_capDir = nullptr;
    QLineEdit* m_recDir = nullptr;
    QLabel* m_dirInfo = nullptr;
    QCheckBox* m_clip = nullptr;
    QLabel* m_clipInfo = nullptr;
    QCheckBox* m_imePunct = nullptr;
    QCheckBox* m_favorites = nullptr;
    QCheckBox* m_shellMenu = nullptr;
    QCheckBox* m_archiveProgress = nullptr;
    QLabel* m_shellInfo = nullptr;
    QLabel* m_imePunctInfo = nullptr;
};
