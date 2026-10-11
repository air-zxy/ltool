#include "ConfigPage.h"
#include "ShellMenu.h"
#include "ClipboardStore.h"
#include "DevEnv.h"
#include "PinWindow.h"
#include "Utils.h"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QFrame>
#include <QGroupBox>
#include <QScrollArea>
#include <QKeySequenceEdit>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QSettings>
#include <QSpinBox>
#include <QUrl>
#include <QVBoxLayout>

#include <windows.h>

namespace {

// 微软拼音"中文输入时使用英文标点"不是 ltool 自己的设置，它是系统的一个 DWORD：
// HKCU\Software\Microsoft\InputMethod\Settings\CHS。写它 = 去
// 设置 → 时间和语言 → 中文(中国) → 微软拼音 → 常规 里点那一下，值名是本机实测出来的。
// 用 Win32 注册表 API 而不是 QSettings：后者在 Windows 上可能按自己的规则改值名大小写。
// 返回 -1 表示这台机器上没有这个值（不是微软拼音，或系统版本不一样）。
constexpr wchar_t kImeChsKey[] = L"Software\\Microsoft\\InputMethod\\Settings\\CHS";
constexpr wchar_t kImePunctValue[] = L"UseEnglishPunctuationsInChineseInputMode";

int imePunctState()
{
    HKEY hk = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kImeChsKey, 0, KEY_READ, &hk) != ERROR_SUCCESS)
        return -1;
    DWORD v = 0, size = sizeof(v), type = 0;
    const LONG r = RegQueryValueExW(hk, kImePunctValue, nullptr, &type,
                                    reinterpret_cast<LPBYTE>(&v), &size);
    RegCloseKey(hk);
    if (r != ERROR_SUCCESS || type != REG_DWORD || size != sizeof(v))
        return -1;
    return v ? 1 : 0;
}

bool setImePunct(bool on)
{
    HKEY hk = nullptr;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, kImeChsKey, 0, nullptr, 0, KEY_WRITE,
                        nullptr, &hk, nullptr) != ERROR_SUCCESS)
        return false;
    const DWORD v = on ? 1u : 0u;
    const LONG r = RegSetValueExW(hk, kImePunctValue, 0, REG_DWORD,
                                  reinterpret_cast<const BYTE*>(&v), sizeof(v));
    RegCloseKey(hk);
    return r == ERROR_SUCCESS;
}

void showImeState(QCheckBox* box, QLabel* info)
{
    const int st = imePunctState();
    box->setEnabled(st >= 0);
    box->setChecked(st > 0);
    info->setText(st < 0
        ? QStringLiteral("没在系统里找到这个开关（可能用的不是微软拼音）")
        : QStringLiteral("注册表 UseEnglishPunctuationsInChineseInputMode = %1（%2）")
              .arg(st)
              .arg(st ? QStringLiteral("开") : QStringLiteral("关")));
}

// 这个目录能不能用。空串代表能用（空值 = 用默认位置，永远合法）；否则返回给人看的原因。
QString dirProblem(const QString& path)
{
    if (path.isEmpty())
        return {};
    QDir().mkpath(path);
    if (!QFileInfo::exists(path))
        return QStringLiteral("建不出来");
    const QString probe = path + "/.ltool-write-test";
    QFile f(probe);
    if (!f.open(QIODevice::WriteOnly))
        return QStringLiteral("写不进去（没权限 / 只读 / 盘满）");
    f.close();
    QFile::remove(probe);
    return {};
}

// "现在这个目录里有多少个、多大" —— 只数 ltool 自己命名的那批文件
QString dirSummary(const QString& dir, const QStringList& patterns)
{
    QDir d(dir);
    if (!d.exists())
        return QStringLiteral("（目录还没建）");
    const QFileInfoList files = d.entryInfoList(patterns, QDir::Files);
    qint64 bytes = 0;
    for (const QFileInfo& fi : files)
        bytes += fi.size();
    return QStringLiteral("%1 个 · %2 MB").arg(files.size())
        .arg(bytes / 1048576.0, 0, 'f', 1);
}

} // namespace

ConfigPage::ConfigPage(QWidget* parent)
    : QWidget(parent)
{
    // 配置项越加越多，早就超出窗口高度了：整页放进滚动区，不然下面的分组看不见也点不到
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    auto* scroller = new QScrollArea(this);
    scroller->setObjectName(QStringLiteral("cfgScroll"));
    scroller->setWidgetResizable(true);
    scroller->setFrameShape(QFrame::NoFrame);
    scroller->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    auto* body = new QWidget;
    body->setObjectName(QStringLiteral("cfgBody"));
    scroller->setWidget(body);
    outer->addWidget(scroller);
    auto* lay = new QVBoxLayout(body);
    lay->setContentsMargins(12, 4, 4, 4);

    auto* general = new QGroupBox("通用", this);
    auto* generalLay = new QFormLayout(general);
    m_autostart = new QCheckBox("开机自动启动 ltool", general);
    generalLay->addRow(m_autostart);
    m_hotkey = new QKeySequenceEdit(general);
    m_hotkey->setToolTip("呼出/隐藏搜索框的全局热键（至少包含一个修饰键）");
    generalLay->addRow("呼出快捷键：", m_hotkey);
    m_installPath = new QLineEdit(general);
    m_installPath->setPlaceholderText("D:\\software");
    generalLay->addRow("软件安装路径：", m_installPath);
    m_proxy = new QLineEdit(general);
    if (const QString sys = DevEnv::systemProxyUrl(); !sys.isEmpty()) {
        m_proxy->setPlaceholderText(QString("留空 = 自动用系统代理（当前检测到 %1）").arg(sys));
        m_proxy->setToolTip("填别的地址可覆盖系统代理。");
    } else {
        m_proxy->setPlaceholderText("例：http://127.0.0.1:10808（GitHub 包下载慢时有用）");
        m_proxy->setToolTip("没检测到系统代理。填上本地代理后，winget 安装和下载器都会走它。");
    }
    m_proxy->setClearButtonEnabled(true);
    generalLay->addRow("下载代理：", m_proxy);
    m_proxy = new QLineEdit(general);
    if (const QString sys = DevEnv::systemProxyUrl(); !sys.isEmpty()) {
        m_proxy->setPlaceholderText(QString("留空 = 自动用系统代理（当前检测到 %1）").arg(sys));
        m_proxy->setToolTip("填别的地址可覆盖系统代理。\n"
                            "内置下载器一直走它；winget 要用命令行代理，需管理员先执行一次\n"
                            "  winget settings --enable ProxyCommandLineOptions\n"
                            "没启用时 ltool 会自动不带代理装（否则 winget 会直接拒绝整条命令）。");
    } else {
        m_proxy->setPlaceholderText("例：http://127.0.0.1:10808（GitHub 包下载慢时有用）");
        m_proxy->setToolTip("没检测到系统代理。填上本地代理后，内置下载器会走它；"
                            "winget 还需要管理员先执行 winget settings --enable ProxyCommandLineOptions "
                            "才认命令行代理。");
    }
    m_proxy->setClearButtonEnabled(true);
    generalLay->addRow("下载代理：", m_proxy);
    m_engine = new QComboBox(general);
    m_engine->addItem("百度", "baidu");
    m_engine->addItem("Google", "google");
    m_engine->addItem("必应", "bing");
    m_engine->addItem("DuckDuckGo", "duckduckgo");
    generalLay->addRow("默认搜索引擎：", m_engine);
    m_favorites = new QCheckBox("呼出时显示常用列表（应用 / 网站 / 工具 / 文件）", general);
    m_favorites->setToolTip("打开：按下热键就展开常用列表，和以前一样。\n"
                            "关闭（默认）：呼出后只有一个搜索框，输入才出结果。");
    generalLay->addRow(m_favorites);

    auto* shell = new QGroupBox("资源管理器右键菜单", this);
    auto* shellLay = new QFormLayout(shell);
    m_shellMenu = new QCheckBox("在文件 / 文件夹的右键里加入 ltool 子菜单", shell);
    m_shellMenu->setToolTip(QStringLiteral("子菜单里是：解压到当前目录、解压到同名文件夹、压缩为 .zip、压缩为 .7z、\n"
                                          "粉碎（不可恢复）、占用分析。\n"
                                          "后端是自带的 libarchive，zip / 7z / tar / gz / bz2 / xz / zstd / iso 都能解，"
                                          "写只写 zip 和 7z。"));
    shellLay->addRow(m_shellMenu);
    m_archiveProgress = new QCheckBox("压缩 / 解压时弹出进度窗口", shell);
    m_archiveProgress->setToolTip(QStringLiteral("关闭（默认）：后台静默做完，不弹任何窗口；只有失败才弹框告诉你，"
                                                 "结果照常写进日志。\n"
                                                 "打开：弹一个进度窗口，能看到正在处理哪个文件、百分比，并且可以取消。"));
    shellLay->addRow(m_archiveProgress);
    m_shellInfo = new QLabel(shell);
    m_shellInfo->setObjectName("muted");
    m_shellInfo->setWordWrap(true);
    shellLay->addRow("当前：", m_shellInfo);

    auto* ime = new QGroupBox("微软拼音 · 中文输入时的标点", this);
    auto* imeLay = new QFormLayout(ime);
    m_imePunct = new QCheckBox("中文输入使用英文标点（, . ? ! 而不是 ，。？！）", ime);
    m_imePunct->setToolTip("改的是系统里微软拼音自己的开关，等价于去\n"
                           "设置 → 时间和语言 → 中文(中国) → 微软拼音 → 常规 → 中文使用英文标点。\n"
                           "保存后按一次 Shift 切一下中英文（或重选一次输入法）就生效。");
    imeLay->addRow(m_imePunct);
    m_imePunctInfo = new QLabel(ime);
    m_imePunctInfo->setObjectName("muted");
    m_imePunctInfo->setWordWrap(true);
    imeLay->addRow("当前：", m_imePunctInfo);

    auto* index = new QGroupBox("定时扫描（文件索引刷新）", this);
    auto* indexLay = new QFormLayout(index);
    m_deep = new QCheckBox("启用全盘深度索引（后台进行，占用更多内存）", index);
    indexLay->addRow(m_deep);
    m_refreshHours = new QSpinBox(index);
    m_refreshHours->setRange(0, 168);
    m_refreshHours->setSuffix(" 小时");
    indexLay->addRow("扫描间隔（0 = 不自动）：", m_refreshHours);
    m_maxEntries = new QSpinBox(index);
    m_maxEntries->setRange(100000, 20000000);
    m_maxEntries->setSingleStep(500000);
    m_maxEntries->setGroupSeparatorShown(true);
    indexLay->addRow("索引条目上限：", m_maxEntries);
    m_roots = new QLineEdit(index);
    m_roots->setPlaceholderText("留空 = 所有固定磁盘（如 C:\\;D:\\）");
    indexLay->addRow("扫描根目录：", m_roots);

    auto* appDirs = new QGroupBox("应用搜索的额外目录", this);
    auto* appLay = new QFormLayout(appDirs);
    // 这些目录是"启动器输名字要能搜到"的应用来源；填文件夹时往里找 exe，
    // 找几层可配（默认 3 层）。扫描是重活，所以只在保存之后才跑，不随输入跑。
    m_appRoots = new QListWidget(appDirs);
    m_appRoots->setMaximumHeight(120);
    m_appRoots->setToolTip(QStringLiteral("每行一个目录。开始菜单那些快捷方式不用往这儿加，本来就扫。"));
    auto* rootRow = new QHBoxLayout;
    auto* addRoot = new QPushButton("添加目录…", appDirs);
    auto* delRoot = new QPushButton("移除选中", appDirs);
    rootRow->addWidget(m_appRoots, 1);
    auto* rootBtns = new QVBoxLayout;
    rootBtns->addWidget(addRoot);
    rootBtns->addWidget(delRoot);
    rootBtns->addStretch(1);
    rootRow->addLayout(rootBtns);
    appLay->addRow("目录列表：", rootRow);
    m_appDepth = new QSpinBox(appDirs);
    m_appDepth->setRange(0, 6);
    m_appDepth->setSuffix(QStringLiteral(" 层"));
    m_appDepth->setToolTip(QStringLiteral("0 = 只看这个目录本身；3 = 往里三层里的 exe 都算。"));
    appLay->addRow("往里找几层：", m_appDepth);
    m_appInfo = new QLabel(appDirs);
    m_appInfo->setObjectName("muted");
    m_appInfo->setWordWrap(true);
    appLay->addRow("当前：", m_appInfo);
    connect(addRoot, &QPushButton::clicked, this, &ConfigPage::addAppRoot);
    connect(delRoot, &QPushButton::clicked, this, &ConfigPage::removeAppRoot);

    auto* data = new QGroupBox("数据", this);
    auto* dataLay = new QFormLayout(data);
    // 截图 / 录像的存放目录：填了就用填的，留空回到 %APPDATA%\ltool 下的默认子目录。
    // 改目录只影响"以后存的文件"，已有文件不搬 —— 这行话必须写在页面上，
    // 不然人会以为文件跟过去了。
    const auto pathRow = [this, data, dataLay](QLineEdit** member, const QString& label,
                                      const QString& defaultDir, const QString& what, bool isCap) {
        auto* row = new QWidget(data);
        auto* rl = new QHBoxLayout(row);
        rl->setContentsMargins(0, 0, 0, 0);
        auto* e = new QLineEdit(row);
        e->setPlaceholderText(QDir::toNativeSeparators(defaultDir));
        e->setToolTip(QString("%1放这里。留空 = %2").arg(what, QDir::toNativeSeparators(defaultDir)));
        auto* browse = new QPushButton("浏览…", row);
        auto* open = new QPushButton("打开", row);
        open->setToolTip(QString("在资源管理器里打开%1现在存放的目录").arg(what));
        QObject::connect(browse, &QPushButton::clicked, row, [e, label, what, defaultDir] {
            // 没填的时候从"以后会存过去的那个地方"开始挑，而不是数据目录
            const QString start = e->text().trimmed().isEmpty()
                                      ? QDir::toNativeSeparators(defaultDir) : e->text().trimmed();
            const QString got = QFileDialog::getExistingDirectory(e, QString("选%1的存放目录").arg(what), start);
            if (!got.isEmpty())
                e->setText(QDir::toNativeSeparators(got));
        });
        QObject::connect(open, &QPushButton::clicked, row, [isCap] {
            const QString dir = isCap ? Utils::capturesDir() : Utils::recordingsDir();
            QDir().mkpath(dir);
            QDesktopServices::openUrl(QUrl::fromLocalFile(dir));
        });
        rl->addWidget(e, 1);
        rl->addWidget(browse);
        rl->addWidget(open);
        *member = e;
        dataLay->addRow(label, row);
    };
    pathRow(&m_capDir, "截图存放：", Utils::mediaRoot() + "/captures", QStringLiteral("截图"), true);
    pathRow(&m_recDir, "录像存放：", Utils::mediaRoot() + "/recordings", QStringLiteral("录像"), false);
    m_dirInfo = new QLabel(data);
    m_dirInfo->setObjectName("muted");
    m_dirInfo->setWordWrap(true);
    dataLay->addRow("当前：", m_dirInfo);
    auto* btnRow = new QHBoxLayout;
    auto* openBtn = new QPushButton("打开数据目录", data);
    auto* clearBtn = new QPushButton("清除索引缓存", data);
    btnRow->addWidget(openBtn);
    btnRow->addWidget(clearBtn);
    btnRow->addStretch(1);
    dataLay->addRow(btnRow);

    auto* clip = new QGroupBox("剪贴板历史", this);
    auto* clipLay = new QFormLayout(clip);
    m_clip = new QCheckBox("记录我复制过的文本和图片（默认关闭）", clip);
    m_clip->setToolTip("开启后 ltool 会在后台读剪贴板，把内容存到\n"
                       "%APPDATA%\\ltool\\clipboard.json 和 clipimg\\ 下（最多 200 条）。\n"
                       "启动器里输 clip 就能翻历史。关掉只是停止收集，已有历史保留。");
    clipLay->addRow(m_clip);
    m_clipInfo = new QLabel(clip);
    m_clipInfo->setObjectName("muted");
    clipLay->addRow("当前：", m_clipInfo);
    auto* clipClear = new QPushButton("清空剪贴板历史", clip);
    clipClear->setToolTip("删除所有记录和已保存的图片文件");
    clipLay->addRow(clipClear);

    auto* save = new QPushButton("保存", this);
    save->setDefault(true);
    save->setObjectName("primary");
    auto* btnLay = new QHBoxLayout;
    btnLay->addStretch(1);
    btnLay->addWidget(save);
    lay->addWidget(general);
    lay->addWidget(shell);
    lay->addWidget(ime);
    lay->addWidget(index);
    lay->addWidget(clip);
    lay->addWidget(data);
    outer->addLayout(btnLay);

    connect(save, &QPushButton::clicked, this, &ConfigPage::onSave);
    connect(openBtn, &QPushButton::clicked, this, &ConfigPage::onOpenDataDir);
    connect(clearBtn, &QPushButton::clicked, this, &ConfigPage::onClearIndexCache);
    connect(clipClear, &QPushButton::clicked, this, [this] {
        ClipboardStore::instance().clear();
        m_clipInfo->setText(QString("已清空 · 现在 %1 条").arg(ClipboardStore::instance().items().size()));
    });
    load();
}

void ConfigPage::load()
{
    QSettings cfg(Utils::dataDir() + "/config.ini", QSettings::IniFormat);
    m_autostart->setChecked(Utils::autostartEnabled());
    m_hotkey->setKeySequence(QKeySequence(cfg.value("general/hotkey", "Alt+Space").toString()));
    m_installPath->setText(cfg.value("install/path", "D:\\software").toString());
    m_proxy->setText(cfg.value("net/proxy").toString());
    const QString engine = cfg.value("web/engine", "baidu").toString();
    const int idx = m_engine->findData(engine);
    m_engine->setCurrentIndex(idx < 0 ? 0 : idx);
    m_favorites->setChecked(cfg.value("ui/favoritesOnEmpty", false).toBool());
    m_deep->setChecked(cfg.value("index/deep", true).toBool());
    m_refreshHours->setValue(cfg.value("index/refreshHours", 6).toInt());
    m_maxEntries->setValue(cfg.value("index/maxEntries", 5000000).toInt());
    m_roots->setText(cfg.value("index/roots").toString());
    m_appDepth->setValue(cfg.value("apps/depth", 3).toInt());
    m_appRoots->clear();
    for (const QString& r : cfg.value("apps/roots").toStringList())
        if (!r.trimmed().isEmpty())
            new QListWidgetItem(QDir::toNativeSeparators(r), m_appRoots);
    m_appInfo->setText(QStringLiteral("%1 个目录 · 往里 %2 层 · 保存之后才会重扫应用列表")
                           .arg(m_appRoots->count())
                           .arg(m_appDepth->value()));
    refreshPathInfo();
    m_clip->setChecked(ClipboardStore::instance().enabled());
    m_clipInfo->setText(QString("%1 条记录 · 图片存在 clipimg\\").arg(ClipboardStore::instance().items().size()));
    m_shellMenu->setChecked(ShellMenu::installed());
    m_archiveProgress->setChecked(cfg.value("ui/archiveProgress", false).toBool());
    m_shellInfo->setText(ShellMenu::installed()
                              ? QStringLiteral("已集成。%1").arg(ShellMenu::locationHint())
                              : QStringLiteral("未集成（默认）。勾上并保存才会写入；取消勾选再保存即撤销。"));
    showImeState(m_imePunct, m_imePunctInfo);
}

void ConfigPage::onSave()
{
    QSettings cfg(Utils::dataDir() + "/config.ini", QSettings::IniFormat);
    Utils::setAutostart(m_autostart->isChecked());
    if (m_shellMenu->isChecked() != ShellMenu::installed()) {
        const QString r = m_shellMenu->isChecked() ? ShellMenu::install() : ShellMenu::remove();
        m_shellInfo->setText(r);
        m_shellMenu->setChecked(ShellMenu::installed());
    }

    // 快捷键
    const QString oldHk = cfg.value("general/hotkey", "Alt+Space").toString();
    const QString newHk = m_hotkey->keySequence().toString();
    cfg.setValue("general/hotkey", newHk);
    cfg.setValue("install/path", m_installPath->text().trimmed());
    cfg.setValue("net/proxy", m_proxy->text().trimmed());
    if (newHk != oldHk)
        emit hotkeyChanged(newHk);

    // 索引
    const bool deepWas = cfg.value("index/deep", true).toBool();
    const int maxWas = cfg.value("index/maxEntries", 5000000).toInt();
    const QString rootsWas = cfg.value("index/roots").toString();
    const int hoursWas = cfg.value("index/refreshHours", 6).toInt();
    cfg.setValue("web/engine", m_engine->currentData().toString());
    cfg.setValue("ui/favoritesOnEmpty", m_favorites->isChecked());
    cfg.setValue("ui/archiveProgress", m_archiveProgress->isChecked());
    cfg.setValue("index/deep", m_deep->isChecked());
    cfg.setValue("index/maxEntries", m_maxEntries->value());
    cfg.setValue("index/refreshHours", m_refreshHours->value());
    cfg.setValue("index/roots", m_roots->text().trimmed());

    // 应用目录：整个列表 + 层数一起比，变了才重扫（扫盘是重活，不能随输入跑）
    const QStringList appRootsWas = cfg.value("apps/roots").toStringList();
    const int appDepthWas = cfg.value("apps/depth", 3).toInt();
    QStringList appRoots;
    for (int i = 0; i < m_appRoots->count(); ++i) {
        const QString r = m_appRoots->item(i)->text().trimmed();
        if (!r.isEmpty())
            appRoots << QDir::fromNativeSeparators(r);
    }
    cfg.setValue("apps/roots", appRoots);
    cfg.setValue("apps/depth", m_appDepth->value());
    bool appChanged = appDepthWas != m_appDepth->value()
                      || appRoots.size() != appRootsWas.size();
    if (!appChanged) {
        for (int i = 0; i < appRoots.size(); ++i)
            if (QString::compare(appRoots[i], QDir::fromNativeSeparators(appRootsWas[i]),
                                 Qt::CaseInsensitive) != 0)
                appChanged = true;
    }
    m_appInfo->setText(QStringLiteral("%1 个目录 · 往里 %2 层 · 保存之后才会重扫应用列表")
                           .arg(appRoots.size())
                           .arg(m_appDepth->value()));

    const bool indexChanged = deepWas != m_deep->isChecked()
                              || maxWas != m_maxEntries->value()
                              || rootsWas != m_roots->text().trimmed()
                              || hoursWas != m_refreshHours->value()
                              || appChanged;
    if (indexChanged)
        emit rescanRequested();

    // 存放目录：先验"能建、能写"，不行就回退到原来的值并说清楚 —— 不接受悄悄改掉
    const QString capNew = m_capDir->text().trimmed();
    const QString recNew = m_recDir->text().trimmed();
    const QString pc = dirProblem(capNew), pr = dirProblem(recNew);
    QString bad;
    if (!pc.isEmpty())
        bad = QStringLiteral("截图目录%1").arg(pc);
    if (!pr.isEmpty())
        bad += (bad.isEmpty() ? QStringLiteral("录像目录%1").arg(pr)
                              : QStringLiteral("；录像目录%1").arg(pr));
    if (!bad.isEmpty()) {
        Utils::log(QStringLiteral("paths change refused: %1").arg(bad));
        refreshPathInfo();
        m_dirInfo->setText(QStringLiteral("没改：%1。还按原来的地方存。").arg(bad));
    } else {
        const QString capOld = cfg.value("paths/captures").toString();
        const QString recOld = cfg.value("paths/recordings").toString();
        if (capNew != capOld || recNew != recOld) {
            cfg.setValue("paths/captures", capNew);
            cfg.setValue("paths/recordings", recNew);
            cfg.sync();               // 让 Utils::capturesDir() 下一次就读到新值
            CaptureHistory::reload(); // 往期列表改成从新目录读（不删任何文件）
            for (QWidget* w : QApplication::topLevelWidgets())
                if (w->inherits("PinWindow"))
                    w->update();      // 开着的置顶窗左边那条缩略图带当场换掉
            Utils::log(QStringLiteral("paths changed: captures=%1 recordings=%2")
                           .arg(Utils::capturesDir(), Utils::recordingsDir()));
        }
        refreshPathInfo();
    }

    ClipboardStore::instance().setEnabled(m_clip->isChecked());
    m_clipInfo->setText(QString("%1 条记录 · 图片存在 clipimg\\").arg(ClipboardStore::instance().items().size()));

    // 只在真的变了时才写注册表，写完回读一遍把系统里的实际值显示出来
    if (m_imePunct->isEnabled() && imePunctState() != (m_imePunct->isChecked() ? 1 : 0)) {
        const bool ok = setImePunct(m_imePunct->isChecked());
        Utils::log(QString("ime: UseEnglishPunctuationsInChineseInputMode -> %1 ok=%2 now=%3")
                       .arg(m_imePunct->isChecked() ? 1 : 0)
                       .arg(ok ? 1 : 0)
                       .arg(imePunctState()));
    }
    showImeState(m_imePunct, m_imePunctInfo);
}

void ConfigPage::refreshPathInfo()
{
    const QString cap = Utils::capturesDir();
    const QString rec = Utils::recordingsDir();
    m_capDir->setText(QDir::toNativeSeparators(cap));
    m_recDir->setText(QDir::toNativeSeparators(rec));
    // 说明行不再重复整条路径（上面两个框里就是），只报数量和大小。
    // 截图要数两种名字：往期是 cap-*.png，手动"保存"那张是 capture-*.png
    m_dirInfo->setText(QStringLiteral("当前：截图 %1 · 录像 %2 · 置顶窗往期 %3 张\n"
                                       "改目录只影响以后存的文件，已有的文件不会搬走。")
                           .arg(dirSummary(cap, { QStringLiteral("cap-*.png"),
                                                  QStringLiteral("capture-*.png") }),
                                dirSummary(rec, { QStringLiteral("rec-*.mp4") }),
                                QString::number(CaptureHistory::count())));
}

void ConfigPage::onOpenDataDir()
{
    QDesktopServices::openUrl(QUrl::fromLocalFile(Utils::dataDir()));
}

void ConfigPage::onClearIndexCache()
{
    QFile::remove(Utils::dataDir() + "/deepindex.dat");
    QFile::remove(Utils::dataDir() + "/usnstate.dat");
    Utils::log("index cache cleared from settings");
    emit rescanRequested();
}

void ConfigPage::addAppRoot()
{
    const QString start = m_appRoots->count()
        ? m_appRoots->item(m_appRoots->count() - 1)->text()
        : QDir::rootPath();
    const QString got = QFileDialog::getExistingDirectory(this, QStringLiteral("选应用目录"), start);
    if (got.isEmpty())
        return;
    const QString nat = QDir::toNativeSeparators(got);
    for (int i = 0; i < m_appRoots->count(); ++i)
        if (m_appRoots->item(i)->text().compare(nat, Qt::CaseInsensitive) == 0)
            return;   // 同一个目录列两遍没有意义
    new QListWidgetItem(nat, m_appRoots);
}

void ConfigPage::removeAppRoot()
{
    delete m_appRoots->currentItem();
}
