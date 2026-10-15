#include "AboutPage.h"

#include "DevEnv.h"
#include "DownloadManager.h"
#include "PluginHost.h"
#include "Utils.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFormLayout>
#include <QFileInfo>
#include <QGuiApplication>
#include <QLabel>
#include <QProgressBar>
#include <QMessageBox>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPushButton>
#include <QProcess>
#include <QRegularExpression>
#include <QSettings>
#include <QThread>
#include <QDesktopServices>
#include <QUrl>
#include <QVBoxLayout>
#include <QtGlobal>
#include <QLibraryInfo>

// 版本号只准有一个来源（ltool.iss 的 #define MyAppVersion，CMake 解析后传进来）。
// 没定义就编译不过 —— 宁可构建当场失败，也别让界面显示一个"忘了改的旧版本"。
#ifndef LTOOL_VERSION
#  error "LTOOL_VERSION 没传进来：CMake 要从 ltool.iss 的 #define MyAppVersion 解析版本号"
#endif

namespace {

// GitHub 上这个工程的地址。换仓库只改这两行。
const char kRepoOwner[] = "mateo-ki";
const char kRepoName[] = "ltool";

// 版本号某段开头的数字（"10-beta" 取到 10；解不出来按 0 算，不抛出去）
int segNum(const QString& part)
{
    static const QRegularExpression re(QStringLiteral("^(\\d+)"));
    return int(re.match(part).captured(1).toInt());
}

} // namespace

AboutPage::AboutPage(QWidget* parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("aboutPage"));
    auto* lay = new QVBoxLayout(this);
    lay->setContentsMargins(14, 12, 14, 12);
    lay->setSpacing(10);

    auto* head = new QLabel(QStringLiteral("关于 ltool"), this);
    head->setObjectName(QStringLiteral("pageTitle"));
    lay->addWidget(head);

    auto* form = new QFormLayout;
    form->setSpacing(6);
    form->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);
    const auto value = [this](const QString& text) {
        auto* l = new QLabel(text, this);
        l->setTextInteractionFlags(Qt::TextSelectableByMouse);   // 路径要能整段选中复制
        l->setWordWrap(true);
        return l;
    };

    int okPlugins = 0;
    int badPlugins = 0;
    for (const PluginHost::Meta& m : PluginHost::scan())
        m.ok() ? ++okPlugins : ++badPlugins;

    form->addRow(QStringLiteral("版本："), value(QStringLiteral("ltool %1").arg(currentVersion())));
    form->addRow(QStringLiteral("Qt 运行时："),
                 value(QStringLiteral("%1（插件必须用同一份 Qt 和同一个 /MD 运行库编，"
                                      "不然崩在两边的堆上）")
                          .arg(QLibraryInfo::version().toString())));
    form->addRow(QStringLiteral("程序目录："),
                 value(QCoreApplication::applicationDirPath()));
    form->addRow(QStringLiteral("数据目录："), value(Utils::dataDir()));
    form->addRow(QStringLiteral("工具插件："),
                 value(QStringLiteral("%1 个可用 · %2 个没通过检查 · 目录 %3")
                          .arg(okPlugins).arg(badPlugins).arg(PluginHost::pluginsDir())));
    form->addRow(QStringLiteral("运行日志："), value(Utils::dataDir() + "/log.txt"));
    lay->addLayout(form);

    auto* hint = new QLabel(
        QStringLiteral("检查更新只发一个到 GitHub 的小请求；走不走代理看 配置 → 下载代理"
                       "（留空 = 不用代理，这台机器境内直连 github 经常整条挂死，那种情况填 "
                       "http://127.0.0.1:10808 一类本地代理）。"
                       "下载通过内置引擎，可实时查看进度，下载完成后可直接安装。"), this);
    hint->setObjectName(QStringLiteral("tip"));
    hint->setWordWrap(true);
    lay->addWidget(hint);

    m_status = new QLabel(QStringLiteral("还没检查过"), this);
    m_status->setObjectName(QStringLiteral("updateStatus"));
    m_status->setWordWrap(true);
    lay->addWidget(m_status);

    m_progress = new QProgressBar(this);
    m_progress->setObjectName(QStringLiteral("downloadProgress"));
    m_progress->setRange(0, 100);
    m_progress->setValue(0);
    m_progress->setTextVisible(false);
    m_progress->setFixedHeight(8);
    m_progress->setStyleSheet(QStringLiteral(
        "QProgressBar { background: #1f2025; border: 1px solid #2c2d34; border-radius: 4px; }"
        "QProgressBar::chunk { background: #3f6ae0; border-radius: 3px; }"
    ));
    m_progress->setVisible(false);
    lay->addWidget(m_progress);

    m_progressLabel = new QLabel(this);
    m_progressLabel->setObjectName(QStringLiteral("progressLabel"));
    m_progressLabel->setStyleSheet(QStringLiteral("color: #8b93a1; font-size: 8.5pt;"));
    m_progressLabel->setVisible(false);
    lay->addWidget(m_progressLabel);

    auto* btns = new QHBoxLayout;
    m_check = new QPushButton(QStringLiteral("检查更新"), this);
    m_check->setObjectName(QStringLiteral("checkUpdate"));
    m_download = new QPushButton(QStringLiteral("下载新版"), this);
    m_download->setObjectName(QStringLiteral("downloadUpdate"));
    m_download->setEnabled(false);
    m_install = new QPushButton(QStringLiteral("装并重启"), this);
    m_install->setObjectName(QStringLiteral("installUpdate"));
    m_install->setEnabled(false);
    m_page = new QPushButton(QStringLiteral("打开发布页"), this);
    m_page->setObjectName(QStringLiteral("openReleases"));
    btns->addWidget(m_check);
    btns->addWidget(m_download);
    btns->addWidget(m_install);
    btns->addWidget(m_page);
    btns->addStretch(1);
    lay->addLayout(btns);
    lay->addStretch(1);

    connect(m_check, &QPushButton::clicked, this, [this] { startCheck(); });
    connect(m_page, &QPushButton::clicked, this, [] {
        QDesktopServices::openUrl(QUrl(releasesPageUrl()));
    });
    connect(m_download, &QPushButton::clicked, this, [this] { startDownload(); });
    connect(m_install, &QPushButton::clicked, this, [this] { installAndRestart(false); });
    connect(&DownloadManager::instance(), &DownloadManager::changed,
            this, &AboutPage::updateDownloadProgress);
}

QString AboutPage::currentVersion()
{
    return QStringLiteral(LTOOL_VERSION);
}

// 安装包直链是按我们自己的命名推出来的（tag 和版本号一一对上），不用问 api。
QString AboutPage::tagFromReleasesUrl(const QString& finalUrl)
{
    static const QString needle = QStringLiteral("/releases/tag/");
    const int at = finalUrl.indexOf(needle);
    return at < 0 ? QString() : finalUrl.mid(at + needle.size());
}

QString AboutPage::versionFromReleasesUrl(const QString& finalUrl)
{
    return versionFromTag(tagFromReleasesUrl(finalUrl));
}

QString AboutPage::downloadUrlFor(const QString& tag, const QString& version)
{
    return QStringLiteral("https://github.com/%1/%2/releases/download/%3/ltool-%4-setup.exe")
        .arg(QLatin1String(kRepoOwner), QLatin1String(kRepoName), tag, version);
}

QString AboutPage::releasesPageUrl()
{
    return QStringLiteral("https://github.com/%1/%2/releases/latest")
        .arg(QLatin1String(kRepoOwner), QLatin1String(kRepoName));
}

int AboutPage::compareVersions(const QString& a, const QString& b)
{
    // 带预发后缀的号（0.8.1-beta.1）先只比数字段，数字全相等才看后缀，而且**带后缀的那个更旧**
    // —— 0.8.1-beta.1 是"奔着 0.8.1 去"的，不是它之后。
    // 以前直接把整串按 '.' 拆开取段："1-beta" 被 segNum 读成 1、尾巴上那段 ".1" 还多出一位，
    // 于是预发被当成比稳定版**更新**，装预发的人再也收不到"有正式版可升"。
    const int dashA = a.indexOf(QLatin1Char('-'));
    const int dashB = b.indexOf(QLatin1Char('-'));
    const QStringList pa = (dashA < 0 ? a : a.left(dashA)).split(QLatin1Char('.'), Qt::SkipEmptyParts);
    const QStringList pb = (dashB < 0 ? b : b.left(dashB)).split(QLatin1Char('.'), Qt::SkipEmptyParts);
    const int n = qMax(pa.size(), pb.size());
    for (int i = 0; i < n; ++i) {
        // 每段按数字比：当字符串比的话 "10" < "9"，版本一到两位数就反了。
        // 解不出数字（"0-beta"、垃圾输入）按 0 算，绝不抛出去 —— 这是个界面按钮后面的功能。
        const int va = i < pa.size() ? segNum(pa[i]) : 0;
        const int vb = i < pb.size() ? segNum(pb[i]) : 0;
        if (va != vb)
            return va < vb ? -1 : 1;
    }
    if (dashA < 0 && dashB < 0)
        return 0;
    if (dashA < 0)
        return 1;                    // 同数字：稳定版赢
    if (dashB < 0)
        return -1;
    return QString::compare(a.mid(dashA + 1), b.mid(dashB + 1));   // 都带后缀：beta.2 > beta.1
}

QString AboutPage::versionFromTag(const QString& tag)
{
    static const QRegularExpression re(QStringLiteral("(\\d+(?:\\.\\d+){1,3})"));
    const QRegularExpressionMatch m = re.match(tag);
    return m.hasMatch() ? m.captured(1) : QString();
}

QString AboutPage::safeFileName(const QString& raw)
{
    // 只留 [A-Za-z0-9._-]：release 的资产名是服务器给的，不能直接当落盘路径用
    // （一个 "../x.exe" 或者带引号的名字就能写到别处 / 把后面的命令行拆开）
    QString out = raw;
    out.replace(QRegularExpression(QStringLiteral("[^A-Za-z0-9._-]")), QStringLiteral("_"));
    while (out.startsWith(QLatin1Char('.')))
        out.remove(0, 1);          // 开头一个点 = 隐藏文件，也可能整个是 ".."
    return out;
}

QString AboutPage::psQuote(const QString& s)
{
    const QString doubled = QStringList(s.split(QLatin1Char('\''))).join(QStringLiteral("''"));
    return QStringLiteral("'%1'").arg(doubled);
}

QString AboutPage::updateScript(const QString& setupPath, const QString& appDir,
                                qint64 pid, QString* err)
{
    // 这段脚本要在我们退出之后动安装目录，所以拼法照 UserAccount 那套：
    // 每个值都走 psQuote（单引号字面量，里面的引号翻倍），脚本里没有一处裸插值。
    if (setupPath.isEmpty() || appDir.isEmpty() || pid <= 0) {
        if (err)
            *err = QStringLiteral("安装包路径或程序目录是空的");
        return {};
    }
    if (!QFileInfo(setupPath).isExecutable()) {     // .exe 且真的存在
        if (err)
            *err = QStringLiteral("那个安装包不在或者不是可执行文件：%1").arg(setupPath);
        return {};
    }
    const QString exe = QDir(appDir).absoluteFilePath(QStringLiteral("ltool.exe"));
    return QStringLiteral(
        "$ErrorActionPreference = 'Continue'\n"
        "$log = %1\n"
        "Set-Content -LiteralPath $log -Value ('wait pid %2') -Encoding UTF8\n"
        "for ($i = 0; $i -lt 600; $i++) {\n"
        "  if (-not (Get-Process -Id %2 -ErrorAction SilentlyContinue)) { break }\n"
        "  Start-Sleep -Milliseconds 200\n"
        "}\n"
        "& %3 /VERYSILENT /NORESTART /SUPPRESSMSGBOXES /DIR=%4 /LOG=%5\n"
        "Add-Content -LiteralPath $log -Value ('setup exit ' + $LASTEXITCODE) -Encoding UTF8\n"
        "Start-Process -FilePath %6\n"
        "Add-Content -LiteralPath $log -Value 'relaunch sent' -Encoding UTF8\n")
        .arg(psQuote(QDir::tempPath() + QStringLiteral("/ltool-update.log")),
             QString::number(pid),
             psQuote(setupPath),
             psQuote(appDir),
             psQuote(QDir::tempPath() + QStringLiteral("/ltool-setup.log")),
             psQuote(exe));
}

void AboutPage::startCheck()
{
    if (m_checking)
        return;
    m_checking = true;
    m_check->setEnabled(false);
    m_status->setText(QStringLiteral("正在问 github 有没有新版本…"));

    // 管理器挂在页上：页（连带设置窗）销毁时 reply 一起断，回调里再用 this 已经被 Qt 摘掉。
    auto* nam = new QNetworkAccessManager(this);
    DevEnv::applyProxy(nam);
    // 走**网页那一条**，不走 api.github.com：匿名 api 的额度是每台出口 IP 60 次/小时，
    // 一台 NAT 后面多查几次就 403（他这次撞的就是 —— 而且是我拿脚本反复查 CI 把额度用光的）。
    // /releases/latest 会 302 到 /releases/tag/vX.Y.Z，跟完重定向读最终地址就知道最新 tag，
    // 这条路没有限流；安装包直链按我们自己的命名推出来，资产大小不预报（下载时自己报）。
    QNetworkRequest req { QUrl(releasesPageUrl()) };
    req.setRawHeader("User-Agent", QByteArrayLiteral("ltool-updater"));
    req.setTransferTimeout(20000);
    QNetworkReply* reply = nam->get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply, nam] {
        m_checking = false;
        m_check->setEnabled(true);
        const int http = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QString finalUrl = reply->url().toString();
        reply->deleteLater();
        nam->deleteLater();

        if (reply->error() != QNetworkReply::NoError) {
            const QString why = http == 404
                ? QStringLiteral("这个仓库还没在 GitHub 上发过 Release（回 404）—— 先发一版才有东西可比")
                : QStringLiteral("%1%2").arg(reply->errorString(),
                                             http ? QStringLiteral("（HTTP %1）").arg(http) : QString());
            m_status->setText(QStringLiteral("检查更新没走通：%1。可以直接点「打开发布页」看最新版本。").arg(why));
            Utils::log(QStringLiteral("update: 检查失败 http=%1 err=%2").arg(http).arg(reply->errorString()));
            return;
        }
        // 最终地址必须是 .../releases/tag/<tag>：没跳过去就是"最新"这条没指到任何 release
        const QString tag = tagFromReleasesUrl(finalUrl);
        const QString ver = versionFromTag(tag);
        if (ver.isEmpty()) {
            m_status->setText(QStringLiteral("tag 里认不出版本号（tag=%1）").arg(tag));
            Utils::log(QStringLiteral("update: tag 认不出版本：%1").arg(tag));
            return;
        }
        const QString assetName = QStringLiteral("ltool-%1-setup.exe").arg(ver);
        const QString assetUrl = downloadUrlFor(tag, ver);
        m_latestVersion = ver;
        m_assetUrl = assetUrl;
        m_assetName = assetName;
        m_assetSize = 0;      // 网页那条给不出资产大小；下载时 DownloadManager 会报真实的

        const int cmp = compareVersions(ver, currentVersion());
        m_lastCheck = QDateTime::currentDateTime();
        // 以前这句里还有"发布 <日期>"和"· <大小> MB"，那是 api 才给的字段 —— 换成网页那条就没了，
        // 宁可少说一项，也不为一行字把限流的那条路请回来。
        const QString note = QStringLiteral("最新 %1（tag %2）· 本机 %3 · 上次检查 %4")
                                 .arg(ver, tag, currentVersion(), m_lastCheck.toString("MM-dd hh:mm"));
        if (cmp > 0) {
            m_status->setText(QStringLiteral("有新版：%1").arg(note));
            m_download->setEnabled(!assetUrl.isEmpty() && !assetName.isEmpty());
            m_download->setText(QStringLiteral("下载新版"));
            Utils::log(QStringLiteral("update: 发现新版 %1（本机 %2）").arg(ver, currentVersion()));

            if (!assetName.isEmpty()) {
                const QString to = QDir(DownloadManager::instance().saveDir()).absoluteFilePath(assetName);
                if (QFileInfo::exists(to) && QFileInfo(to).size() > 0
                    && (m_assetSize <= 0 || qAbs(QFileInfo(to).size() - m_assetSize) < 1024)) {
                    m_target = to;
                    m_install->setEnabled(true);
                    m_progress->setVisible(true);
                    m_progress->setRange(0, 100);
                    m_progress->setValue(100);
                    m_progressLabel->setVisible(true);
                    m_progressLabel->setText(QStringLiteral("安装包已在本地就绪（%1 MB），随时可点击「装并重启」")
                                                 .arg(QFileInfo(to).size() / 1048576.0, 0, 'f', 1));
                }
            }
        } else {
            m_status->setText(QStringLiteral("已经是最新（或更高）：%1").arg(note));
            m_download->setEnabled(false);
            m_download->setText(QStringLiteral("下载新版"));
            Utils::log(QStringLiteral("update: 最新 %1，本机 %2，不用动").arg(ver, currentVersion()));
        }
    });
}

void AboutPage::startDownload()
{
    if (m_assetUrl.isEmpty() || m_assetName.isEmpty()) {
        m_status->setText(QStringLiteral("那条 release 没给出安装包地址，只能去发布页手动下。"));
        return;
    }
    const QString dir = DownloadManager::instance().saveDir();
    const QString to = QDir(dir).absoluteFilePath(m_assetName);

    // 检查本地是否已有下载好的完整安装包
    if (QFileInfo::exists(to)) {
        const qint64 localSize = QFileInfo(to).size();
        if (localSize > 0 && (m_assetSize <= 0 || qAbs(localSize - m_assetSize) < 1024)) {
            m_target = to;
            m_status->setText(QStringLiteral("安装包已就绪：%1").arg(to));
            m_progress->setVisible(true);
            m_progress->setRange(0, 100);
            m_progress->setValue(100);
            m_progressLabel->setVisible(true);
            m_progressLabel->setText(QStringLiteral("安装包已下载完成（%1 MB）").arg(localSize / 1048576.0, 0, 'f', 1));
            m_install->setEnabled(true);

            if (QGuiApplication::platformName() != QLatin1String("offscreen")) {
                const auto ans = QMessageBox::question(
                    this, QStringLiteral("安装新版本"),
                    QStringLiteral("本地已检测到完整的新版本安装包：%1\n\n是否立即开始安装并重启？")
                        .arg(QFileInfo(to).fileName()),
                    QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes);
                if (ans == QMessageBox::Yes) {
                    installAndRestart(true);
                }
            }
            return;
        }
    }

    m_installPrompted = false;
    m_target = to;
    m_progress->setVisible(true);
    m_progress->setRange(0, 100);
    m_progress->setValue(0);
    m_progressLabel->setVisible(true);
    m_progressLabel->setText(QStringLiteral("正在连接下载…"));
    m_download->setEnabled(false);
    m_download->setText(QStringLiteral("正在下载…"));
    m_status->setText(QStringLiteral("正在下载新版安装包，下载完成后可直接安装。"));

    const auto& tasks = DownloadManager::instance().tasks();
    for (const auto& t : tasks) {
        if (t.file == to || t.url == m_assetUrl) {
            m_downloadTaskId = t.id;
            if (t.status == QStringLiteral("已暂停") || t.status == QStringLiteral("失败")) {
                DownloadManager::instance().resume(t.id);
            }
            updateDownloadProgress();
            return;
        }
    }

    DownloadManager::instance().add(m_assetUrl, to);
    const auto& newTasks = DownloadManager::instance().tasks();
    if (!newTasks.isEmpty()) {
        m_downloadTaskId = newTasks.last().id;
    }
    Utils::log(QStringLiteral("update: 开始下载 %1 → %2").arg(m_assetUrl, to));
    updateDownloadProgress();
}

void AboutPage::updateDownloadProgress()
{
    if (m_downloadTaskId.isEmpty())
        return;

    const auto& tasks = DownloadManager::instance().tasks();
    const DownloadTask* currentTask = nullptr;
    for (const auto& t : tasks) {
        if (t.id == m_downloadTaskId) {
            currentTask = &t;
            break;
        }
    }

    if (!currentTask)
        return;

    const QString status = currentTask->status;
    const qint64 recv = currentTask->receivedTotal();
    const qint64 total = currentTask->total > 0 ? currentTask->total : m_assetSize;

    if (status == QStringLiteral("下载中") || status == QStringLiteral("等待")) {
        m_progress->setVisible(true);
        m_progressLabel->setVisible(true);
        if (total > 0) {
            const int percent = qBound(0, int(double(recv) / double(total) * 100.0), 100);
            m_progress->setRange(0, 100);
            m_progress->setValue(percent);
            m_progressLabel->setText(QStringLiteral("下载进度：%1% (%2 MB / %3 MB)")
                                         .arg(percent)
                                         .arg(recv / 1048576.0, 0, 'f', 1)
                                         .arg(total / 1048576.0, 0, 'f', 1));
        } else {
            m_progress->setRange(0, 0);
            m_progressLabel->setText(QStringLiteral("正在下载：%1 MB")
                                         .arg(recv / 1048576.0, 0, 'f', 1));
        }
    } else if (status == QStringLiteral("完成")) {
        m_progress->setVisible(true);
        m_progress->setRange(0, 100);
        m_progress->setValue(100);
        m_progressLabel->setVisible(true);
        m_progressLabel->setText(QStringLiteral("下载完成！安装包已就绪。"));
        m_status->setText(QStringLiteral("新版本安装包已下载完成，随时可点击「装并重启」进行安装。"));
        m_download->setText(QStringLiteral("下载完成"));
        m_download->setEnabled(false);
        m_install->setEnabled(true);
        m_target = currentTask->file;

        if (!m_installPrompted && QGuiApplication::platformName() != QLatin1String("offscreen")) {
            m_installPrompted = true;
            const auto ans = QMessageBox::question(
                this, QStringLiteral("安装新版本"),
                QStringLiteral("新版本 %1 安装包已下载完成！\n\n是否立即退出并开始安装？\n（稍后安装也可以随时点击「装并重启」按钮）")
                    .arg(m_latestVersion.isEmpty() ? QStringLiteral("") : m_latestVersion),
                QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes);
            if (ans == QMessageBox::Yes) {
                installAndRestart(true);
            }
        }
    } else if (status == QStringLiteral("失败")) {
        m_progress->setVisible(true);
        m_progressLabel->setVisible(true);
        m_progressLabel->setText(QStringLiteral("下载失败：%1").arg(
            currentTask->error.isEmpty() ? QStringLiteral("网络连接中断") : currentTask->error));
        m_status->setText(QStringLiteral("下载失败，请检查网络或代理后点击重试。"));
        m_download->setText(QStringLiteral("重新下载"));
        m_download->setEnabled(true);
    }
}

void AboutPage::installAndRestart(bool bypassConfirm)
{
    // 这一段会退出正在跑的 ltool，所以每一步都要他按下：先确认，再交出去。
    QString err;
    if (m_target.isEmpty() || !QFileInfo::exists(m_target)) {
        m_status->setText(QStringLiteral("安装包还没下完（或者没在这儿）：%1").arg(m_target));
        return;
    }
    const QString appDir = QCoreApplication::applicationDirPath();
    const QString script = updateScript(m_target, appDir, QCoreApplication::applicationPid(), &err);
    if (script.isEmpty()) {
        m_status->setText(QStringLiteral("不能装：%1").arg(err));
        Utils::log(QStringLiteral("update: 拒绝安装：%1").arg(err));
        return;
    }
    if (!bypassConfirm) {
        const auto answer = QMessageBox::question(
            this, QStringLiteral("装并重启"),
            QStringLiteral("这会**先退出 ltool**，然后静默安装 %1 到 %2，装完自动重新打开。\n\n"
                           "定时任务、正在跑的下载会被一起中断（下载器支持断点续传，回来能续）。"
                           "确定吗？")
                .arg(QFileInfo(m_target).fileName(), appDir),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);      // 默认停在"不"
        if (answer != QMessageBox::Yes) {
            m_status->setText(QStringLiteral("没动。"));
            return;
        }
    }
    const QString spath = QDir::tempPath()
        + QStringLiteral("/ltool-update-%1.ps1").arg(QCoreApplication::applicationPid());
    QFile f(spath);
    if (!f.open(QIODevice::WriteOnly)) {
        m_status->setText(QStringLiteral("临时脚本写不下去：%1").arg(f.errorString()));
        return;
    }
    // PS 5.1 没有 BOM 就按 GBK 解码这个文件，路径里的中文会变乱码 —— 必须带上
    f.write("ï»¿");
    f.write(script.toUtf8());
    f.close();

    const bool sent = QProcess::startDetached(QStringLiteral("powershell.exe"),
                                              { QStringLiteral("-NoProfile"),
                                                QStringLiteral("-ExecutionPolicy"),
                                                QStringLiteral("Bypass"),
                                                QStringLiteral("-File"), spath });
    if (!sent) {
        m_status->setText(QStringLiteral("那段脚本没起来（powershell 启动失败），什么都没改。"));
        Utils::log(QStringLiteral("update: 起不了更新脚本 %1").arg(spath));
        return;
    }
    Utils::log(QStringLiteral("update: 交给 %1 代跑，现在退出（装完它会重新拉起）").arg(spath));
    m_status->setText(QStringLiteral("正在退出并安装…（脚本 %1，日志 %2）")
                          .arg(spath, QDir::tempPath() + QStringLiteral("/ltool-update.log")));
    // 让它把"等我们退出"这一步跑起来，再收摊
    QThread::msleep(200);
    QCoreApplication::quit();
}
