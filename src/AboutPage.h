#pragma once

#include <QDateTime>
#include <QString>
#include <QWidget>

class QLabel;
class QPushButton;
class QProgressBar;

// 「关于」这一页说三件事：这台机器上装的是哪个版本、装在哪里、有哪些工具插件在跑；
// 然后是「有没有新版」。
//
// 版本号的唯一来源是 ltool.iss 里那条 `#define MyAppVersion` —— CMake 从它解析出
// project(VERSION) 并编成 LTOOL_VERSION 宏传进来（少传就编译不过，见 AboutPage.cpp）。
// 安装包的文件名、卸载表里的 DisplayVersion、这一页显示的版本，从此只能是同一个数。
//
// 更新走 GitHub Releases 的 latest 接口（一个 JSON GET），并且沿用配置页那条下载代理
// （留空 = 不走代理；这台机器境内直连 github 经常整条挂死，代理是唯一的出路）。
//
// 三档动作全都要人按下按钮才做，一件都不自动：
//   检查更新 → 一次小 GET；
//   下载新版 → 交给内置下载器（看台在「下载器」那一页，不在这里假装有进度）；
//   装并重启 → 会先退出 ltool，所以必须他自己点，而且弹窗二次确认。
class AboutPage : public QWidget {
    Q_OBJECT
public:
    explicit AboutPage(QWidget* parent = nullptr);

    // ---- 下面这些都是纯函数：不联网、不起进程，--about 逐条断言 ----
    static QString currentVersion();          // 编译期那个 LTOOL_VERSION
    static QString downloadUrlFor(const QString& tag, const QString& version);   // 安装包那条直链
    // 跟完 /releases/latest 的 302 之后，从最终地址里读版本号：.../releases/tag/<tag> → 版本号。
    // 单独抽出来是因为离屏判据要拿真回复跑一遍这条解析，不想抄第二遍。
    static QString tagFromReleasesUrl(const QString& finalUrl);    // .../releases/tag/<tag> → <tag>
    static QString versionFromReleasesUrl(const QString& finalUrl);
    static QString releasesPageUrl();         // 给人用浏览器打开的那页
    // 逐段按数字比：1.10 必须大于 1.9（当字符串比就反了）；非数字段按 0 处理，不抛异常
    static QString versionFromTag(const QString& tag);   // "v0.6.0"/"ltool 0.6.0-beta" → "0.6.0"
    // release 资产的文件名只留 [A-Za-z0-9._-]，别让它变成 "../" 或者带引号的参数
    static QString safeFileName(const QString& raw);
    // PowerShell 的单引号字面量：整串包住、里面的单引号翻倍（和 UserAccount 那套同一判据）
    static QString psQuote(const QString& s);
    // 生成那段"等我们退出→静默安装→再拉起来"的脚本。任何路径里出现单引号就 *err 拒绝，
    // 因为这段是提权之外唯一会动到安装目录的动作，不能留逃逸口。
    static QString updateScript(const QString& setupPath, const QString& appDir,
                                qint64 pid, QString* err);

private slots:
    void updateDownloadProgress();

private:
    void startCheck();
    void startDownload();
    void installAndRestart(bool bypassConfirm = false);

    QLabel* m_status = nullptr;
    QProgressBar* m_progress = nullptr;
    QLabel* m_progressLabel = nullptr;
    QPushButton* m_check = nullptr;
    QPushButton* m_download = nullptr;
    QPushButton* m_install = nullptr;
    QPushButton* m_page = nullptr;
    QString m_assetUrl;
    QString m_assetName;
    qint64 m_assetSize = 0;
    QString m_downloadTaskId;
    QString m_target;           // 下载完（或本来就存在）的安装包全路径
    QString m_latestVersion;
    QDateTime m_lastCheck;
    bool m_checking = false;    // 同一时刻只准有一个请求在问
    bool m_installPrompted = false;
};
