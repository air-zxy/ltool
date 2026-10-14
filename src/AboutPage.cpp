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
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonDocument>
#include <QLabel>
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
                       "下载交给内置下载器，进度在「下载器」那一页看。"), this);
    hint->setObjectName(QStringLiteral("tip"));
    hint->setWordWrap(true);
    lay->addWidget(hint);

    m_status = new QLabel(QStringLiteral("还没检查过"), this);
    m_status->setObjectName(QStringLiteral("updateStatus"));
    m_status->setWordWrap(true);
    lay->addWidget(m_status);

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
    connect(m_install, &QPushButton::clicked, this, [this] { installAndRestart(); });
}

QString AboutPage::currentVersion()
{
    return QStringLiteral(LTOOL_VERSION);
}

QString AboutPage::latestApiUrl()
{
    return QStringLiteral("https://api.github.com/repos/%1/%2/releases/latest")
        .arg(QLatin1String(kRepoOwner), QLatin1String(kRepoName));
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
    QNetworkRequest req { QUrl(latestApiUrl()) };
    req.setRawHeader("User-Agent", QByteArrayLiteral("ltool-updater"));   // 没 UA 的 API 请求直接 403
    req.setRawHeader("Accept", QByteArrayLiteral("application/vnd.github+json"));
    req.setTransferTimeout(20000);
    QNetworkReply* reply = nam->get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply, nam] {
        m_checking = false;
        m_check->setEnabled(true);
        const int http = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QByteArray body = reply->readAll();   // QNetworkReply 是 QIODevice，没有 data()
        reply->deleteLater();
        nam->deleteLater();

        if (reply->error() != QNetworkReply::NoError) {
            // 失败要说清是哪一种：连不上 / 被拒 / 限流 / 还没发过版，在他机器上是四件不同的事
            QString why;
            if (http == 404)
                why = QStringLiteral("这个仓库还没在 GitHub 上发过 Release（api 回 404）"
                                     "—— 先发一版才有东西可比");
            else if (http == 403 || http == 429)
                why = QStringLiteral("github 拒绝了这次请求（HTTP %1，未登录的 api 限流是 60 次/小时）")
                          .arg(http);
            else
                why = QStringLiteral("%1%2").arg(reply->errorString(),
                                                 http ? QStringLiteral("（HTTP %1）").arg(http) : QString());
            m_status->setText(QStringLiteral("没问到：%1。可以直接点「打开发布页」看。").arg(why));
            Utils::log(QStringLiteral("update: 检查失败 http=%1 err=%2").arg(http)
                           .arg(reply->errorString()));
            return;
        }
        const QJsonObject o = QJsonDocument::fromJson(body).object();
        const QString tag = o.value(QStringLiteral("tag_name")).toString();
        const QString ver = versionFromTag(tag);
        if (ver.isEmpty()) {
            m_status->setText(QStringLiteral("回复里没有能认的版本号（tag=%1）").arg(tag));
            Utils::log(QStringLiteral("update: tag 认不出版本：%1").arg(tag));
            return;
        }
        const QString published =
            o.value(QStringLiteral("published_at")).toString().left(10);
        // 挑安装包：资产名里带 setup 的 .exe；一个都没有就只报版本号（别猜链接）
        QString assetUrl, assetName;
        qint64 assetSize = 0;
        const QJsonArray assets = o.value(QStringLiteral("assets")).toArray();
        for (const QJsonValue& v : assets) {
            const QJsonObject a = v.toObject();
            const QString name = a.value(QStringLiteral("name")).toString();
            if (name.contains(QStringLiteral("setup"), Qt::CaseInsensitive)
                && name.endsWith(QStringLiteral(".exe"), Qt::CaseInsensitive)) {
                assetName = safeFileName(name);
                assetUrl = a.value(QStringLiteral("browser_download_url")).toString();
                assetSize = qint64(a.value(QStringLiteral("size")).toDouble());
                break;
            }
        }
        m_latestVersion = ver;
        m_assetUrl = assetUrl;
        m_assetName = assetName;

        const int cmp = compareVersions(ver, currentVersion());
        m_lastCheck = QDateTime::currentDateTime();
        const QString note = QStringLiteral("最新 %1（发布 %2%3）· 本机 %4 · 上次检查 %5")
                                 .arg(ver, published.isEmpty() ? QStringLiteral("?") : published,
                                      assetSize ? QStringLiteral(" · %1 MB")
                                                   .arg(assetSize / 1048576.0, 0, 'f', 1)
                                                   : QString(),
                                      currentVersion(),
                                      m_lastCheck.toString("MM-dd hh:mm"));
        if (cmp > 0) {
            m_status->setText(QStringLiteral("有新版：%1").arg(note));
            m_download->setEnabled(!assetUrl.isEmpty() && !assetName.isEmpty());
            Utils::log(QStringLiteral("update: 发现新版 %1（本机 %2）").arg(ver, currentVersion()));
        } else {
            m_status->setText(QStringLiteral("已经是最新（或更高）：%1").arg(note));
            m_download->setEnabled(false);
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
    if (QFileInfo::exists(to)) {
        m_status->setText(QStringLiteral("这个包已经在了：%1（要重装就点「装并重启」）").arg(to));
        m_target = to;
        m_install->setEnabled(true);
        return;
    }
    DownloadManager::instance().add(m_assetUrl, to);
    m_target = to;
    m_status->setText(QStringLiteral("已加进内置下载器：%1 —— 进度在「下载器」那一页看，"
                                     "下完了回来点「装并重启」。").arg(to));
    Utils::log(QStringLiteral("update: 已交给下载器 %1 → %2").arg(m_assetUrl, to));
}

void AboutPage::installAndRestart()
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
    const QString spath = QDir::tempPath()
        + QStringLiteral("/ltool-update-%1.ps1").arg(QCoreApplication::applicationPid());
    QFile f(spath);
    if (!f.open(QIODevice::WriteOnly)) {
        m_status->setText(QStringLiteral("临时脚本写不下去：%1").arg(f.errorString()));
        return;
    }
    // PS 5.1 没有 BOM 就按 GBK 解码这个文件，路径里的中文会变乱码 —— 必须带上
    f.write("\xEF\xBB\xBF");
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
