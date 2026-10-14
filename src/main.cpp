#include "AppScanner.h"
#include "AdBlocker.h"
#include "CaptureOverlay.h"
#include "ConfigPage.h"
#include "PinWindow.h"
#include "OcrBundle.h"
#include "QClipboard"
#include "QMimeData"
#include "QKeyEvent"
#include "QMouseEvent"
#include "ClipboardStore.h"
#include "DevEnv.h"
#include "ToolsPage.h"
#include "PluginHost.h"
#include "AboutPage.h"
#include <QComboBox>
#include <QPixmap>
#include <QRandomGenerator>
#include <algorithm>
#include <QPlainTextEdit>
#include "ltool-plugin.h"
#include "DiskScan.h"
#include "FileIndexer.h"
#include "FileOps.h"
#include "Archives.h"
#include "Pinyin.h"
#include "ShellMenu.h"
#include "Ocr.h"
#include "OcrPp.h"
#include "OcrPanel.h"
#include "RegClean.h"
#include "Firewall.h"
#include "SoftwarePage.h"
#include "StartupAudit.h"
#include "SearchWindow.h"
#include "SettingsDialog.h"
#include "SilentInstallLog.h"
#include "TaskScheduler.h"
#include "UninstallPage.h"

#include "UsnIndex.h"
#include "Utils.h"
#include "WebShortcuts.h"

#include <QApplication>
#include <clocale>
#include <QAbstractNativeEventFilter>
#include <QDir>
#include <QDirIterator>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QLineEdit>
#include <QThread>
#include <QPushButton>
#include <QGroupBox>
#include <QLayout>
#include <QScrollArea>
#include <QSet>
#include <QFile>
#include <QFileInfo>
#include <QElapsedTimer>
#include <QProcess>
#include <QEventLoop>
#include <QImage>
#include <QKeySequence>
#include <QMap>
#include <QPainter>
#include <QTimer>
#include <QLocalServer>
#include <QLocalSocket>
#include <QMenu>
#include <QPalette>
#include <QSettings>
#include <QSystemTrayIcon>
#include <QVector>

#include <functional>

#include <windows.h>
#include <dwmapi.h>
#include <psapi.h>

namespace {

constexpr WPARAM kHotkeyId = 1;
constexpr WPARAM kCaptureId = 2;
// F2 录屏。热键注册留在这层薄壳里（它必须是那个还能起得来的进程），按下之后把动作名
// "record" 交给 PluginHost::invoke —— 2026-10-15 起，录屏本体在 plugins/record。
constexpr WPARAM kRecordId = 3;

// 资源管理器右键能进来的那几个旗标（ShellMenu 往注册表写的就是这份）。
// 主程序只负责把"旗标去掉两个短横"当动作名转给插件，所以这张表**同时是 --plugintest 的判据**：
// 少一个动作没人声明 = 右键点下去什么都不发生，那条最难查，不能靠人肉对齐两处写法。
const char* const kShellFlags[] = { "--shred", "--fileinfo", "--whohas", "--extract",
                                    "--extract-sub", "--compress-zip", "--compress-7z" };

// 托盘图标的"安装进行中"动画帧：原图标右下角盖一小块深色圆，圆里转一段弧。
// 预渲染好轮播，避免每次 tick 都重画一遍 base（托盘 setIcon 本身就够贵了）
QVector<QIcon> spinnerFrames(const QIcon& base)
{
    static constexpr int kFrames = 12;
    const QPixmap src = base.pixmap(64, 64);
    QVector<QIcon> frames;
    frames.reserve(kFrames);
    for (int i = 0; i < kFrames; ++i) {
        QPixmap pm(64, 64);
        pm.fill(Qt::transparent);
        QPainter p(&pm);
        p.setRenderHint(QPainter::Antialiasing);
        p.drawPixmap(0, 0, src);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(23, 24, 28));
        p.drawEllipse(QRectF(33, 33, 30, 30));
        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(QColor(94, 200, 120), 4, Qt::SolidLine, Qt::FlatCap));
        p.drawArc(QRectF(36, 36, 24, 24), (360 - i * 30) * 16, 110 * 16);
        frames.append(QIcon(pm));
    }
    return frames;
}

class HotkeyFilter : public QAbstractNativeEventFilter {
public:
    explicit HotkeyFilter(std::function<void(WPARAM)> fn) : m_fn(std::move(fn)) {}

    bool nativeEventFilter(const QByteArray&, void* message, qint64* result) override
    {
        MSG* msg = static_cast<MSG*>(message);
        if (msg && msg->message == WM_HOTKEY
            && (msg->wParam == kHotkeyId || msg->wParam == kCaptureId
            || msg->wParam == kRecordId)) {
            m_fn(msg->wParam);
            if (result)
                *result = 0;
            return true;
        }
        return false;
    }

private:
    std::function<void(WPARAM)> m_fn;
};

// QKeySequence → RegisterHotKey 的 (修饰键, 虚拟键)
bool keySeqToHotkey(const QKeySequence& ks, UINT* mods, UINT* vk)
{
    if (ks.count() != 1)
        return false;
    const QKeyCombination combo = ks[0];
    const Qt::KeyboardModifiers m = combo.keyboardModifiers();
    const Qt::Key key = combo.key();
    UINT mm = 0;
    if (m & Qt::ControlModifier) mm |= MOD_CONTROL;
    if (m & Qt::AltModifier)     mm |= MOD_ALT;
    if (m & Qt::ShiftModifier)   mm |= MOD_SHIFT;
    if (m & Qt::MetaModifier)    mm |= MOD_WIN;
    UINT k = 0;
    if (key == Qt::Key_Space) k = VK_SPACE;
    else if (key >= Qt::Key_F1 && key <= Qt::Key_F24) k = UINT(VK_F1) + UINT(key - Qt::Key_F1);
    else if (key >= Qt::Key_A && key <= Qt::Key_Z) k = UINT('A') + UINT(key - Qt::Key_A);
    else if (key >= Qt::Key_0 && key <= Qt::Key_9) k = UINT('0') + UINT(key - Qt::Key_0);
    else return false;
    if (!mm && !(key >= Qt::Key_F1 && key <= Qt::Key_F24))
        return false; // 必须带修饰键（F 键除外）
    *mods = mm;
    *vk = k;
    return true;
}

// --smoke：无界面自检，扫描应用 + 检测插件 + 建立文件索引后打印结果退出
int runSmokeTest()
{
    // 诊断：打印各扫描根目录与 .lnk 数量
    struct FolderCheck { const char* label; Utils::KnownFolder kf; };
    static const FolderCheck folders[] = {
        {"desktop", Utils::KnownFolder::Desktop},
        {"publicDesktop", Utils::KnownFolder::PublicDesktop},
        {"userPrograms", Utils::KnownFolder::UserStartMenuPrograms},
        {"commonPrograms", Utils::KnownFolder::CommonStartMenuPrograms},
    };
    for (const FolderCheck& fc : folders) {
        const QString dir = Utils::knownFolderPath(fc.kf);
        const int lnkCount = QDir(dir).entryList({"*.lnk"}, QDir::Files).size();
        std::fprintf(stdout, "folder %s: %s exists=%d lnk=%d\n",
                     fc.label, dir.toUtf8().constData(),
                     QDir(dir).exists() ? 1 : 0, lnkCount);
    }

    const auto apps = AppScanner::scan();
    AppScanner::saveIndex(apps);
    int withIcon = 0;
    for (const AppEntry& a : apps)
        if (!a.iconFile.isEmpty())
            ++withIcon;
    std::fprintf(stdout, "apps=%zu icons=%d\n", size_t(apps.size()), withIcon);

    auto plugins = DevEnv::loadPlugins();
    for (DevPlugin& p : plugins) {
        DevEnv::detect(&p);
        int okCount = 0;
        for (const DevItem& it : p.items)
            if (it.ok)
                ++okCount;
        std::fprintf(stdout, "plugin %s: %d/%zu ok\n",
                     p.id.toUtf8().constData(), okCount, size_t(p.items.size()));
    }

    FileIndexer::instance().reindexAsync(false); // 与生产一致：增量优先
    QObject::connect(&FileIndexer::instance(), &FileIndexer::indexingFinished,
                     [](int total) {
                         std::fprintf(stdout, "files=%d\n", total);
                         std::fflush(stdout);
                         QApplication::quit();
                     });
    QTimer::singleShot(120000, [] { std::fprintf(stdout, "files=TIMEOUT\n"); QApplication::quit(); });
    const int ret = QApplication::exec();
    std::fflush(stdout);
    return ret;
}

// --regscan：把注册表分析的结论原样打出来，用来人工核对有没有误报
int runRegScan()
{
    QElapsedTimer t;
    t.start();
    const QVector<RegIssue> quick = RegClean::quickScan();
    const qint64 quickMs = t.elapsed();
    const QVector<RegIssue> all = RegClean::scan();
    std::printf("quick=%d in %lldms  full=%d in %lldms\n", int(quick.size()), quickMs,
                int(all.size()), t.elapsed());
    QMap<QString, int> byCat;
    for (const RegIssue& it : all)
        byCat[RegClean::categoryLabel(it)]++;
    for (auto i = byCat.constBegin(); i != byCat.constEnd(); ++i)
        std::printf("  cat %s = %d\n", i.key().toUtf8().constData(), i.value());
    for (const RegIssue& it : all) {
        std::printf("[%s|%s] %s || %s || %s\\%s @%s\n",
                    RegClean::categoryLabel(it).toUtf8().constData(),
                    it.deletable() ? "del" : "ro", it.title.toUtf8().constData(),
                    it.detail.toUtf8().constData(), it.hiveName.toUtf8().constData(),
                    it.subKey.toUtf8().constData(), it.valueName.toUtf8().constData());
    }
    std::fflush(stdout);
    return 0;
}

// --startupscan：把开机启动分析的结论原样打出来，用来人工核对
int runStartupScan()
{
    QElapsedTimer t;
    t.start();
    const QVector<StartupItem> items = StartupAudit::scan();
    std::printf("items=%d in %lldms\n", int(items.size()), t.elapsed());
    QMap<QString, int> bySrc;
    for (const StartupItem& it : items)
        bySrc[StartupAudit::sourceLabel(it.source)]++;
    for (auto i = bySrc.constBegin(); i != bySrc.constEnd(); ++i)
        std::printf("  src %s = %d\n", i.key().toUtf8().constData(), i.value());
    for (const StartupItem& it : items) {
        std::printf("[%s] %s || sig=%d/%d %s || missing=%d enabled=%d || %s || %s || %s\n",
                    StartupAudit::sourceLabel(it.source).toUtf8().constData(),
                    it.name.toUtf8().constData(), it.hasSignature ? 1 : 0,
                    it.signatureValid ? 1 : 0, it.publisher.toUtf8().constData(),
                    it.targetMissing ? 1 : 0, it.enabled ? 1 : 0,
                    it.targetPath.toUtf8().constData(), it.location.toUtf8().constData(),
                    it.note.toUtf8().constData());
    }
    std::fflush(stdout);
    return 0;
}

// --fw <exe>：读一遍防火墙规则，核对枚举是否真的读到了（0 条要能区分"没有"和"没读到"）
int runFwProbe(const QStringList& args)
{
    QStringList a = args.mid(args.indexOf("--fw") + 1);
    a.removeAll(QStringLiteral("--fw"));
    const QString exe = a.join(QLatin1Char(' '));
    const Firewall::Match m = Firewall::rulesFor(exe.isEmpty() ? QStringLiteral("__none__") : exe);
    std::printf("rules seen=%d  match.any=%d  match.allowIn=%d  names=%s\n", m.total, m.any ? 1 : 0,
                m.allowIn ? 1 : 0, m.names.join(" | ").toUtf8().constData());
    std::fflush(stdout);
    return 0;
}

// --uptime：无界面地把"软件"菜单那一行的两个页面各构造一次，量出卡顿在哪一段
// --adscan：量一次"广告拦截每 5 秒在 GUI 线程上做的事"到底有多贵（无界面）
// 跑某个插件自带的判据（插件里导出 extern "C" 的那个函数，名字由调用方点名）。
// 页面/动作代码搬进 DLL 之后主程序链接不到它内部的测试函数，只能这样要符号。
// 找不到符号**必须算失败**，不能当通过 —— 否则"插件没加载起来"会伪装成"测试全绿"。
//
// arg / ms / text：判据入口的签名不止一种，按那面旗标原来的写法给 ——
//   None            → int()                    （disk / user 那一类，只返回失败数）
//   Ms              → int(int)                 （--recprobe 的毫秒、--rectest/--recordui 的秒）
//   MsAndText       → int(int, const char*)    （--memloop 的轮数 + 「宽x高」）
// flag：非空 = 判据**自己**已经打了最后那行 "<flag>: 失败 N 项"（record 那几条旗标就是这么写的），
//        这儿不能再打一遍（同一次跑出两个失败数就没法看了）；空 = 按老样子用插件 id 补那一行。
enum class SelftestArg { None, Ms, MsAndText };

int runPluginSelftest(const QString& id, const char* symbol = "ltoolPluginSelftest",
                      SelftestArg arg = SelftestArg::None, int ms = 0,
                      const QString& text = QString(), const char* flag = nullptr)
{
    void* raw = PluginHost::entry(id, symbol);
    if (!raw) {
        // "插件根本没起来"和"起来了但没导出这个符号"是两种坏法，分开说才有人能照着修
        if (!PluginHost::plugin(id))
            std::printf("FAIL %s：插件没能加载（%s 里没有能用的 ltool-%s.dll —— 安装包少文件了？）\n",
                        id.toUtf8().constData(),
                        PluginHost::pluginsDir().toUtf8().constData(),
                        id.toUtf8().constData());
        std::printf("FAIL %s：没有可用的判据入口（插件没加载起来，或没导出 %s）\n",
                    id.toUtf8().constData(), symbol);
        std::printf("%s: 失败 1 项\n", flag ? flag : id.toUtf8().constData());
        std::fflush(stdout);
        return 1;
    }
    int bad = 0;
    switch (arg) {
    case SelftestArg::None:
        bad = reinterpret_cast<int (*)()>(raw)();
        break;
    case SelftestArg::Ms:
        bad = reinterpret_cast<int (*)(int)>(raw)(ms);
        break;
    case SelftestArg::MsAndText:
        bad = reinterpret_cast<int (*)(int, const char*)>(raw)(ms, text.toUtf8().constData());
        break;
    }
    if (!flag) {
        std::printf("%s: 失败 %d 项\n", id.toUtf8().constData(), bad);
        std::fflush(stdout);
    }
    return bad == 0 ? 0 : 1;
}

int runAdScan()
{
    for (int pass = 0; pass < 3; ++pass) {
        QElapsedTimer t;
        t.start();
        const auto hits = AdBlocker::instance().scan();
        std::printf("scan pass %d: %d windows, %lld ms\n", pass, int(hits.size()),
                    t.nsecsElapsed() / 1000000);
        std::fflush(stdout);
    }
    return 0;
}

int runUiTiming(const QStringList& args)
{
    auto timed = [](const char* tag, std::function<void()> f) {
        QElapsedTimer t;
        t.start();
        f();
        std::printf("%-20s %8lld ms\n", tag, t.nsecsElapsed() / 1000000);
        std::fflush(stdout);
    };
    const bool wantSoft = !args.contains("un");
    const bool wantUn = !args.contains("soft");
    QWidget* soft = nullptr;
    QWidget* un = nullptr;
    timed("SoftwarePage ctor", [&] {
        if (!wantSoft)
            return;
        soft = new SoftwarePage;
        soft->setAttribute(Qt::WA_DontShowOnScreen);
    });
    timed("SoftwarePage show", [&] { if (soft) soft->show(); });
    timed("UninstallPage ctor", [&] {
        if (!wantUn)
            return;
        un = new UninstallPage;
        un->setAttribute(Qt::WA_DontShowOnScreen);
    });
    timed("UninstallPage show", [&] { if (un) un->show(); });
    {
        QElapsedTimer t;
        t.start();
        QEventLoop loop;
        QTimer::singleShot(2500, &loop, &QEventLoop::quit);
        loop.exec();
        std::printf("%-20s %8lld ms (slices drained)\n", "event loop", t.nsecsElapsed() / 1000000);
    }
    const Firewall::Match m = Firewall::rulesFor(QStringLiteral("D:\\software\\ltool\\ltool.exe"));
    std::printf("rulesFor           %8d rules\n", m.total);
    std::fflush(stdout);
    delete soft;
    delete un;
    return 0;
}


// --fwtry <exe> <显示名>：跑一遍页面上"加入允许列表"按钮走的同一条代码（含提权与回读）
int runFwTry(const QStringList& args)
{
    QStringList rest = args.mid(args.indexOf(QStringLiteral("--fwtry")) + 1);
    rest.removeAll(QStringLiteral("--fwtry"));
    const QString exe = rest.isEmpty() ? QString() : rest.first();
    const QString name = rest.size() > 1 ? rest.at(1) : QFileInfo(exe).completeBaseName();
    const QString out = Firewall::allowExe(exe, name);
    std::printf("allowExe -> %s\n", out.toUtf8().constData());
    std::fflush(stdout);
    return 0;
}

// --fwallow <exe> <规则名> / --fwremove <规则名>：只做一次 COM 写入。
// 这是给"被 UAC 提权拉起来的自己"用的入口（软件列表里点"加入允许列表"时自家人叫自家人），
// 也方便命令行核对权限行为。
int runFwWrite(const QStringList& args)
{
    const bool remove = args.contains(QStringLiteral("--fwremove"));
    const QString flag = remove ? QStringLiteral("--fwremove") : QStringLiteral("--fwallow");
    QStringList rest = args.mid(args.indexOf(flag) + 1);
    rest.removeAll(flag);
    const QString a0 = rest.isEmpty() ? QString() : rest.first();
    const QString a1 = rest.size() > 1 ? rest.at(1) : QString();
    const bool ok = remove ? Firewall::removeRuleNow(a0)
                           : Firewall::addRuleNow(a0, a1.isEmpty() ? QFileInfo(a0).completeBaseName()
                                                                   : a1);
    std::printf("fw%s -> %s\n", remove ? "remove" : "allow", ok ? "ok" : "failed");
    std::fflush(stdout);
    return ok ? 0 : 1;
}

// --searchtime a b c：加载索引后逐个查询并计时（无界面），用于定位搜索框输入卡顿
int runSearchTiming(const QStringList& queries)
{
    FileIndexer::instance().reindexAsync(false);
    QObject::connect(&FileIndexer::instance(), &FileIndexer::indexingFinished,
                     [queries](int total) {
                         std::fprintf(stdout, "index=%d entries\n", total);
                         for (const QString& q : queries) {
                             QElapsedTimer t;
                             t.start();
                             const auto hits = FileIndexer::instance().search(q, 8);
                             std::fprintf(stdout, "search %-14s %7lld ms hits=%d\n",
                                          q.toUtf8().constData(),
                                          t.nsecsElapsed() / 1000000, int(hits.size()));
                             for (int i = 0; i < hits.size() && i < 3; ++i)
                                 std::fprintf(stdout, "    #%d %s\n", i + 1,
                                              hits[i].path.toUtf8().constData());
                         }
                         std::fflush(stdout);
                         QApplication::quit();
                     });
    QTimer::singleShot(180000, [] { std::fprintf(stdout, "searchtime=TIMEOUT\n");
                                   QApplication::quit(); });
    return QApplication::exec();
}

// --searchdump a b c：把每个词的完整结果列表按顺序打出来。
// 改搜索实现（分桶/并行/跳字节）最怕的是"快了但结果顺序变了"，所以这条就是
// 改前改后各跑一遍、拿 diff 当判据的入口 —— diff 空才算过。
int runSearchDump(const QStringList& queries)
{
    FileIndexer::instance().reindexAsync(false);
    QObject::connect(&FileIndexer::instance(), &FileIndexer::indexingFinished,
                     [queries](int total) {
                         std::fprintf(stdout, "index=%d entries\n", total);
                         for (const QString& q : queries) {
                             const auto hits = FileIndexer::instance().search(q, 20);
                             std::fprintf(stdout, "=== %s hits=%d\n", q.toUtf8().constData(),
                                          int(hits.size()));
                             for (int i = 0; i < hits.size(); ++i)
                                 std::fprintf(stdout, "%2d %s%s | %s\n", i,
                                              hits[i].isDir ? "d" : "f",
                                              hits[i].name.toUtf8().constData(),
                                              hits[i].path.toUtf8().constData());
                         }
                         std::fflush(stdout);
                         QApplication::quit();
                     });
    QTimer::singleShot(180000, [] { std::fprintf(stdout, "searchdump=TIMEOUT\n");
                                    QApplication::quit(); });
    return QApplication::exec();
}

// --disktime：A/B 两种磁盘遍历在 C: 上的耗时（旧 QDirIterator vs 新 DiskScan），
// 用来回答"占用分析为什么卡、改完快了多少"
int runDiskTiming()
{
    QElapsedTimer t;
    std::printf("DiskScan (FindFirstFileEx + 并行 + 一次建树)…\n");
    std::fflush(stdout);
    t.start();
    QStringList cleanPaths;
    for (const DiskScan::CleanTarget& c : DiskScan::cleanTargets())
        cleanPaths << c.path;
    const QVector<qint64> sizes = DiskScan::sizesFor(cleanPaths);
    qint64 cleanTotal = 0;
    for (qint64 v : sizes)
        cleanTotal += v;
    const qint64 quick = t.elapsed();
    // 这一段决定用户点「整理」后多久能看到第一个 tab 有结果，比整盘耗时更影响体感
    std::printf("  可清理位置（%d 项）: %lld ms  合计 %.2f GB\n", int(cleanPaths.size()), quick,
                cleanTotal / 1073741824.0);
    std::fflush(stdout);
    t.start();
    const DiskScan::Tree tree = DiskScan::run("C:/", 1 << 20);
    const qint64 fastCold = t.elapsed();
    // 三段分开报，不然"太慢了"只能靠猜：扫盘 / 合并+建树 / 排序剪枝
    const DiskScan::Timing tm = DiskScan::lastTiming();
    std::printf("  新（第一趟，目录项多半是冷的）: %lld ms  目录节点=%d 合计=%.2f GB\n",
                fastCold, int(tree.name.size()), tree.total / 1073741824.0);
    std::printf("      分段：扫盘 %lld ms、合并建树 %lld ms、排序剪枝 %lld ms"
                "（%d 个任务 / %d 线程，扫过 %lld 个目录 / %lld 个文件）\n",
                tm.walkMs, tm.mergeMs, tm.sortMs, tm.jobs, tm.threads, tm.dirsSeen, tm.files);
    for (int i = tree.firstKid[0]; i < tree.firstKid[0] + tree.kidCount[0] && i < tree.kids.size(); ++i) {
        const int k = tree.kids[i];
        std::printf("      %-28s %8.2f GB\n", tree.name[k].toUtf8().constData(),
                    tree.size[k] / 1073741824.0);
        if (i - tree.firstKid[0] >= 4)
            break;
    }
    std::fflush(stdout);

    // A/B 必须两趟都跑在**同一种缓存状态**上：先前后面旧，旧的那趟吃的是新那趟焐热的缓存，
    // 于是量出"新比旧慢 0.7x"这个假结论（本机实测过：同一棵树 32 秒 / 13.9 秒，差别全在冷缓存）。
    // 所以现在是 冷新 → 冷旧 → 热旧 → 热新，两两同状态才比。
    t.restart();
    const qint64 slowCold = DiskScan::legacyWalk("C:/");
    const qint64 slowColdMs = t.elapsed();
    std::printf("  旧（冷）: %lld ms\n", slowColdMs);
    t.restart();
    const qint64 slowWarm = DiskScan::legacyWalk("C:/");
    const qint64 slowWarmMs = t.elapsed();
    t.restart();
    const DiskScan::Tree warm2 = DiskScan::run("C:/", 1 << 20);
    const qint64 fastWarm = t.elapsed();
    std::printf("  同一棵树两趟同缓存比：冷 %lld(新) / %lld(旧) ms，热 %lld(新) / %lld(旧) ms"
                "  → 热态提速 %.1fx\n",
                fastCold, slowColdMs, fastWarm, slowWarmMs,
                slowWarmMs > 0 ? double(slowWarmMs) / double(fastWarm) : 0.0);
    std::printf("  合计对不对得上：新 %.2f GB、旧 %.2f GB、热的再扫一遍 %.2f GB\n",
                tree.total / 1073741824.0, slowWarm / 1073741824.0,
                warm2.total / 1073741824.0);
    std::fflush(stdout);
    return 0;
}

// 深色外观必须由我们自己保证：设置页的 QSS 只覆盖了它写到的那些部件，
// QScrollArea 视口、菜单、滚动条这些没写到的会去取调色板 —— 浅色模式的系统
// （新虚拟机默认就是浅色）会把它们画成白底，C 盘清理页整片发白就是这么来的。
void applyDarkPalette(QApplication& app)
{
    const QColor window(0x1b, 0x1c, 0x20), base(0x17, 0x18, 0x1c), alt(0x23, 0x24, 0x29);
    const QColor text(0xe8, 0xeb, 0xf2), muted(0x8b, 0x93, 0xa1), accent(0x3f, 0x6a, 0xe0);
    QPalette pal = app.palette();
    pal.setColor(QPalette::Window, window);
    pal.setColor(QPalette::Base, base);
    pal.setColor(QPalette::AlternateBase, alt);
    pal.setColor(QPalette::Text, text);
    pal.setColor(QPalette::WindowText, text);
    pal.setColor(QPalette::Button, QColor(0x2b, 0x2d, 0x34));
    pal.setColor(QPalette::ButtonText, text);
    pal.setColor(QPalette::ToolTipBase, alt);
    pal.setColor(QPalette::ToolTipText, text);
    pal.setColor(QPalette::Highlight, accent);
    pal.setColor(QPalette::HighlightedText, Qt::white);
    pal.setColor(QPalette::PlaceholderText, muted);
    pal.setColor(QPalette::Link, accent);
    for (QPalette::ColorGroup g : {QPalette::Disabled, QPalette::Inactive}) {
        pal.setColor(g, QPalette::Text, muted);
        pal.setColor(g, QPalette::WindowText, muted);
        pal.setColor(g, QPalette::ButtonText, muted);
        pal.setColor(g, QPalette::Highlight, QColor(0x3a, 0x3c, 0x45));
    }
    app.setPalette(pal);
}

} // namespace

int main(int argc, char* argv[])
{
    // libarchive 在 UTF-8 和窄字符之间转文件名时用的是 C 运行时 locale，而 Qt 不会去设它：
    // 默认 "C" 下任何非 ASCII 名字都转不过去，压缩带中文名的东西会直接失败。
    setlocale(LC_CTYPE, "");
    // --pindel 会真删 captures 目录里的往期截图，--ocrchain 要验"引擎还没下载"那条链（会真下 26MB），
    // --pathcfg 会写 config.ini 并建目录，--appdirs 会写 apps/roots 并扫盘：
    // 这几种都把数据目录整个挪到临时目录再跑，碰不到用户真实的 %APPDATA%\ltool。
    // 必须在第一次 dataDir() 之前设好。
    for (int i = 1; i < argc; ++i) {
        const QString a = QString::fromLocal8Bit(argv[i]);
        if (a == QLatin1String("--pindel") || a == QLatin1String("--ocrchain")
            || a == QLatin1String("--pathcfg") || a == QLatin1String("--pintool")
            || a == QLatin1String("--appdirs") || a == QLatin1String("--cfgcheck")) {
            const QString sandbox = QDir::tempPath() + "/" + a.mid(2);
            QDir(sandbox).removeRecursively();
            QDir().mkpath(sandbox);
            qputenv("APPDATA", sandbox.toUtf8());
            // 截图/录像的默认落点现在是 exe 旁边（软件目录），挪 APPDATA 管不到它。
            // 会往往期里写东西的那两个自测改用配置项把两个目录也指进沙箱 ——
            // 不然它们会在 build\Release 里留下 captures\，而那会被安装包收走。
            if (a == QLatin1String("--pindel") || a == QLatin1String("--pintool")) {
                QSettings cfg(sandbox + "/ltool/config.ini", QSettings::IniFormat);
                cfg.setValue(QStringLiteral("paths/captures"),
                             QDir::toNativeSeparators(sandbox + "/captures"));
                cfg.setValue(QStringLiteral("paths/recordings"),
                             QDir::toNativeSeparators(sandbox + "/recordings"));
                cfg.sync();
            }
            break;
        }
    }
    QApplication app(argc, argv);
    QApplication::setOrganizationName("ltool");
    QApplication::setApplicationName("ltool");
    QApplication::setApplicationDisplayName("ltool");
    QApplication::setWindowIcon(Utils::appIcon());
    // 常驻托盘：最后一个窗口关闭（比如关掉设置页）不能退出程序
    app.setQuitOnLastWindowClosed(false);
    applyDarkPalette(app);

    Utils::ensureDirs();
    DevEnv::ensureDefaultPlugins();
    WebShortcuts::ensureDefaultWebs();

    const QStringList args = app.arguments();
    const bool smoke = args.contains("--smoke");
    const bool showOnStart = args.contains("--show");
    const bool openSettingsOnStart = args.contains("--settings") || args.contains("--settingspage");
    const bool mftTest = args.contains("--mfttest");
    const bool searchTime = args.contains("--searchtime");

    // --smoke 必须在单实例探针**之前**派发。它原先排在后面，于是本机已有一个在跑的
    // 实例时，探针连上去把请求转交、当场 return 0 —— 输出 0 字节 + 退出码 0，
    // 看起来像"自检通过"，其实一行都没跑（2026-10-14 撞见：交付闭环里这条是哑的）。
    if (smoke)
        return runSmokeTest();

    if (mftTest) {
        // 需要管理员权限：诊断 FSCTL_ENUM_USN_DATA 各组合行为
        for (const QString& drive : {"C:/", "D:/"}) {
            std::fprintf(stdout, "=== %s ===\n", drive.toUtf8().constData());
            UsnIndex::VolumeHandle vol;
            if (vol.open(drive)) {
                UsnIndex::mftEnumMatrix(vol.vol);
                vol.close();
            } else {
                std::fprintf(stdout, "open failed err=%lu\n", GetLastError());
            }
        }
        std::fflush(stdout);
        return 0;
    }

    if (searchTime) {
        QStringList qs = args.mid(args.indexOf("--searchtime") + 1);
        qs.removeAll("--searchtime");
        if (qs.isEmpty())
            qs << "no" << "notepad" << "chrome" << "de" << "use";
        return runSearchTiming(qs);
    }

    if (args.contains("--searchdump")) {
        QStringList qs = args.mid(args.indexOf("--searchdump") + 1);
        qs.removeAll("--searchdump");
        if (qs.isEmpty())
            qs << "no" << "notepad" << "chrome" << "de" << "use"
               << QString::fromUtf8("软件") << QString::fromUtf8("回收")
               << QString::fromUtf8("截图");
        return runSearchDump(qs);
    }

    if (args.contains("--disktime"))
        return runDiskTiming();


    // --rowmenu：磁盘页两栏列表的右键（只有界面在用，且真按一下就会拉起资源管理器，
    // 所以判据放在插件里 DiskPage.cpp，这里只负责点名跑它）。
    if (args.contains("--rowmenu"))
        return runPluginSelftest(QStringLiteral("disk"));

    // --username：验「工具 → Windows 用户名」这一页的判据。页面搬进 DLL 插件之后，
    // 判据跟着页一起进插件（主程序链接不到 UserAccountPage），这儿只负责点名跑它。
    // **一条都不真的去改账户/建账户** —— 那是不可逆级别的动作，只能在界面上由人按下。
    if (args.contains("--username"))
        return runPluginSelftest(QStringLiteral("user"));

    // --plugintest：DLL 插件那条链从头到尾真跑一遍（这次是**真的 LoadLibrary**，
    // 但一次都不 show()，不碰他的桌面）。
    // 名字为什么不叫 --plugin：Qt 自己把 "--plugin" 当命令行选项吃掉了 ——
    // 实测 argc 直接变成 1（--pluginx 就还在），所以这里换个不带歧义的名字。
    if (args.contains("--plugintest")) {
        int bad = 0;
        const auto check = [&bad](const char* name, const QString& got, const QString& want) {
            const bool ok = got == want;
            if (!ok)
                ++bad;
            std::printf("%s %s got=[%s] want=[%s]\n", ok ? "PASS" : "FAIL", name,
                        got.toUtf8().constData(), want.toUtf8().constData());
        };
        const auto has = [&bad](const char* name, bool ok) {
            std::printf("%s %s\n", ok ? "PASS" : "FAIL", name);
            if (!ok)
                ++bad;
        };
        // 十一个自带工具现在全是 dll 插件。这张名单是"装齐了没有"的判据：
        // 少一个就是安装包少了文件，或者那个 dll 没通过检查 —— 别让它在界面上悄悄少一张卡。
        static const char* const kTools[] = { "repair", "ads", "disk", "network", "registry",
                                              "startup", "user", "json", "base64", "shell",
                                              "record" };
        const QDir dir(PluginHost::pluginsDir());
        std::printf("INFO 插件目录：%s\n", dir.absolutePath().toUtf8().constData());
        const auto& metas = PluginHost::scan();
        std::printf("INFO 扫到 %d 个 dll，其中通过检查 %d 个\n", int(metas.size()),
                    int(std::count_if(metas.begin(), metas.end(),
                                      [](const PluginHost::Meta& m) { return m.ok(); })));
        for (const char* id : kTools) {
            const PluginHost::Meta* m = PluginHost::meta(QLatin1String(id));
            has((QString::fromLatin1(id) + " 在目录里且元数据通过检查").toUtf8().constData(),
                m != nullptr && m->ok());
        }
        // 每个插件都跑一遍同一条链：加载 → 建页 → 没 show → 缓存 → 元数据和自报一致
        for (const char* idc : kTools) {
            const QString id = QLatin1String(idc);
            const PluginHost::Meta* m = PluginHost::meta(id);
            if (!m || !m->ok())
                continue;
            LToolPlugin* p = PluginHost::plugin(id);
            has((id + "：真的 LoadLibrary + instance + 版本闸放行").toUtf8().constData(),
                p != nullptr);
            if (!p)
                continue;
            check((id + "：插件自报的 API 版本").toUtf8().constData(),
                  QString::number(p->apiVersion()), QString::number(LTOOL_PLUGIN_API_VERSION));
            // plugin.json 那份和 C++ 里那五个 override 是同一件事的两处写法，对不上就是漂移
            check((id + "：plugin.json 与插件自报的名字/描述/关键词一致").toUtf8().constData(),
                  QStringLiteral("%1/%2/%3").arg(p->name(), p->desc())
                      .arg(p->keywords().size()),
                  QStringLiteral("%1/%2/%3").arg(m->name, m->desc).arg(m->keywords.size()));
            // 动作也是两处写法（API 3）：门口挂的牌和实例自己认的必须同一份。
            // 路由最终看实例那份（见 PluginHost::invoke），所以这条红了 = 主程序会找不到它。
            check((id + "：plugin.json 的 Actions 与实例自报的一致").toUtf8().constData(),
                  m->actions.join(QLatin1Char(',')), p->actions().join(QLatin1Char(',')));
            QString err;
            QWidget* page = PluginHost::page(id, nullptr, &err);
            has((id + "：createPage 给出一张页（" + err + "）").toUtf8().constData(), page != nullptr);
            if (!page)
                continue;
            check((id + "：没 show 过（不碰他的桌面）").toUtf8().constData(),
                  page->isVisible() ? QStringLiteral("yes") : QStringLiteral("no"),
                  QStringLiteral("no"));
            check((id + "：再要一次是同一张页（懒创建 + 缓存）").toUtf8().constData(),
                  PluginHost::page(id, nullptr, &err) == page ? QStringLiteral("yes")
                                                              : QStringLiteral("no"),
                  QStringLiteral("yes"));
        }
        // 递文本那条路（API 2 新加的）：搜索框"json 这串"/"b64 这串"靠它
        QString err;
        QWidget* jp = PluginHost::page(QStringLiteral("json"), nullptr, &err);
        has("json 页在（递文本测试要用它）", jp != nullptr);
        if (jp) {
            has("把一段 JSON 递给 json 工具，插件接住了",
                PluginHost::feed(QStringLiteral("json"), QStringLiteral("{ \"a\": 1 }")));
            auto* edit = jp->findChild<QPlainTextEdit*>();
            check("那段文本真的进了工具", edit && edit->toPlainText().contains('1')
                  ? QStringLiteral("yes") : QStringLiteral("no"), QStringLiteral("yes"));
        }
        has("没实现 feedText 的工具老实返回 false（不假装收下了）",
            !PluginHost::feed(QStringLiteral("disk"), QStringLiteral("随便一段文本")));
        // 插件自带的判据：resolve 得到符号才算这条链通
        has("disk 插件带着自己的判据入口", PluginHost::selftestEntry(QStringLiteral("disk")) != nullptr);
        has("user 插件带着自己的判据入口", PluginHost::selftestEntry(QStringLiteral("user")) != nullptr);
        // 动作路由（API 3 新加的）：**一条都不真去执行** —— 右键那些动作会弹窗、会解压、会粉碎文件。
        // 这里只验"找得到人"这一半：主程序那张 verb 表里每个旗标都必须有人声明接它。
        for (const char* f : kShellFlags) {
            const QString action = QString::fromLatin1(f).mid(2);
            check((QStringLiteral("右键动作 %1 有人接（谁）").arg(action)).toUtf8().constData(),
                  PluginHost::actionOwner(action), QStringLiteral("shell"));
        }
        check("没人声明的动作老实返回空（不是随便抓一个插件顶上）",
              PluginHost::actionOwner(QStringLiteral("compress-lzx")), QString());
        // F2 那一档（2026-10-15 录屏本体进了 plugins/record）：热键还在这层薄壳里，
        // 动作名 "record" 必须有人在门口挂着牌子。这条红了 = 按 F2 什么都没发生，
        // 而"按了没反应"是这程序里最难查的一个形状，所以它和上面那七条同权重。
        check("热键动作 record 有人接（谁）",
              PluginHost::actionOwner(QStringLiteral("record")), QStringLiteral("record"));
        // 录屏的判据也跟着进了插件：符号点名不到 = 那面旗会红（这里只 resolve，一次都不跑 ——
        // 那五条全都要真屏幕，跑起来就是十几秒外加往屏幕上贴窗口）。
        has("record 插件导出 --recq 的判据入口",
            PluginHost::entry(QStringLiteral("record"), "ltoolRecqSelftest") != nullptr);
        QString routedTo;
        check("invoke 一个没人接的动作 = false + 说不出是谁",
              !PluginHost::invoke(QStringLiteral("compress-lzx"), { QStringLiteral("x.zip") }, &routedTo)
              && routedTo.isEmpty() ? QStringLiteral("yes") : QStringLiteral("no"),
              QStringLiteral("yes"));
        // 搜索框那一档：工具那一节要出得来，而且不能像「计划任务」那次一样把整页独占掉
        SearchWindow w;
        auto* input = w.findChild<QLineEdit*>(QStringLiteral("input"));
        auto* list = w.findChild<QListWidget*>(QStringLiteral("list"));
        const auto settle = [] {
            for (int i = 0; i < 40; ++i) {
                QCoreApplication::processEvents();
                QThread::msleep(10);
            }
        };
        const auto section = [list](const QString& s) {
            if (!list)
                return QStringLiteral("no");
            for (int i = 0; i < list->count(); ++i)
                if (list->item(i)->text() == s)
                    return QStringLiteral("yes");
            return QStringLiteral("no");
        };
        has("拿得到输入框和结果列表", input != nullptr && list != nullptr);
        if (input && list) {
            input->setText(QStringLiteral("b64"));
            settle();
            check("搜 b64 出「工具」那一节", section(QStringLiteral("工具")), QStringLiteral("yes"));
            input->setText(QStringLiteral("cipanqingli"));
            settle();
            check("拼音直达也认插件（cipanqingli → 磁盘清理）",
                  section(QStringLiteral("工具")), QStringLiteral("yes"));
            input->setText(QStringLiteral("renwu"));
            settle();
            check("搜 renwu 不该被工具顶掉", section(QStringLiteral("工具")), QStringLiteral("no"));
            check("搜 renwu 时应用那一节还在（工具绝不独占整页）",
                  section(QStringLiteral("── 应用 ──")), QStringLiteral("yes"));
            input->clear();
            settle();
        }
        // 卡片墙：这十一个工具现在全靠插件目录，卡片数必须等于"通过检查的插件数"，
        // 而且每张卡都要能在布局里走到（数 findChild 不等于界面可见 —— 配置页那栏
        // 就是这么隐身的，2026-10-12 他报的）。这条替掉了原来那两条"内嵌槽位 0..N-1"。
        ToolsPage wall;
        QSet<const QWidget*> laidOut;
        std::function<void(QLayout*)> walk = [&](QLayout* l) {
            if (!l)
                return;
            for (int i = 0; i < l->count(); ++i) {
                QLayoutItem* it = l->itemAt(i);
                if (!it)
                    continue;
                if (QWidget* w = it->widget()) {
                    laidOut.insert(w);
                    walk(w->layout());
                } else if (QLayout* sub = it->layout())
                    walk(sub);
            }
        };
        walk(wall.layout());
        int cards = 0;
        for (QFrame* c : wall.findChildren<QFrame*>())
            if (c->objectName() == QLatin1String("row"))
                ++cards;
        int reachable = 0;
        for (QFrame* c : wall.findChildren<QFrame*>())
            if (c->objectName() == QLatin1String("row") && laidOut.contains(c))
                ++reachable;
        int okCount = 0;
        for (const PluginHost::Meta& m : metas)
            if (m.ok())
                ++okCount;
        check("卡片数 = 通过检查的插件数", QString::number(cards), QString::number(okCount));
        check("每张卡都真的摆在布局里（不是只 new 出来）", QString::number(reachable),
              QString::number(cards));
        check("十一个工具都在卡片墙上", QString::number(cards), QStringLiteral("11"));
        // 卡片墙的几何：离屏 resize 之后布局会算，不用 show（show 就把设置窗拍到他桌面上）。
        // 这条盯的是"这十一张卡摆得开吗"—— 卡片来源换成插件目录之后，这是新Shape。
        // WA_DontShowOnScreen：走完整的 show/布局激活路径，但不往屏幕上贴一个像素、
        // 也不抢焦点。裸 resize() 不够 —— 没 show 过的顶层窗口布局根本不算，
        // 第一版就是这么量出"九张卡全在 (0,0,0,0)"，那是探针坏了不是卡片坏了。
        wall.setAttribute(Qt::WA_DontShowOnScreen, true);
        wall.resize(940, 720);
        wall.show();
        for (int i = 0; i < 20; ++i)
            QCoreApplication::processEvents();
        QList<QRect> rects;
        for (QFrame* c : wall.findChildren<QFrame*>())
            if (c->objectName() == QLatin1String("row"))
                rects << c->geometry();
        bool sized = true;
        bool overlap = false;
        for (int i = 0; i < rects.size(); ++i) {
            if (rects[i].width() < 120 || rects[i].height() < 80)
                sized = false;
            for (int j = i + 1; j < rects.size(); ++j)
                if (rects[i].intersects(rects[j]))
                    overlap = true;
        }
        if (!rects.isEmpty())
            std::printf("INFO 第一张卡的矩形=(%d,%d %dx%d)\n", rects.first().x(), rects.first().y(),
                        rects.first().width(), rects.first().height());
        wall.hide();
        check("每张卡都有画得出来的尺寸（最窄 120x80）", sized ? QStringLiteral("yes") : QStringLiteral("no"),
              QStringLiteral("yes"));
        check("卡片互不重叠（三列排版真的分开了）", overlap ? QStringLiteral("yes") : QStringLiteral("no"),
              QStringLiteral("no"));
        std::printf("plugintest: 失败 %d 项\n", bad);
        std::fflush(stdout);
        return bad == 0 ? 0 : 1;
    }

    // --about：「关于」这一页里那些不用联网就能定的事，逐条量清楚。
    // 版本号是不是真的从构建传进来、比较函数会不会把 1.10 排在 1.9 后面、
    // 服务器给的资产名能不能变成 "../"、那段要动安装目录的 PowerShell 有没有逃逸口、
    // 四颗按钮是不是真的摆在布局里（数 findChild 不等于界面可见，配置页那栏栽过）。
    if (args.contains("--about")) {
        int bad = 0;
        const auto check = [&bad](const char* name, const QString& got, const QString& want) {
            const bool ok = got == want;
            if (!ok)
                ++bad;
            std::printf("%s %s got=[%s] want=[%s]\n", ok ? "PASS" : "FAIL", name,
                        got.toUtf8().constData(), want.toUtf8().constData());
        };
        const auto has = [&bad](const char* name, bool ok) {
            std::printf("%s %s\n", ok ? "PASS" : "FAIL", name);
            if (!ok)
                ++bad;
        };
        const QString ver = AboutPage::currentVersion();
        has(QStringLiteral("版本号长得对（x.y.z，预发可以带 -beta.N）: [%1]").arg(ver).toUtf8().constData(),
            QRegularExpression(QStringLiteral("^\\d+\\.\\d+\\.\\d+(-[0-9A-Za-z.]+)?$")).match(ver).hasMatch());
        check("latest 那条 API 地址", AboutPage::latestApiUrl(),
              QStringLiteral("https://api.github.com/repos/mateo-ki/ltool/releases/latest"));
        check("给人点的发布页", AboutPage::releasesPageUrl(),
              QStringLiteral("https://github.com/mateo-ki/ltool/releases/latest"));

        // 版本比较：这一段最容易写错成字符串比（"10" < "9"）
        check("1.10 必须大于 1.9", QString::number(AboutPage::compareVersions("1.10", "1.9")), "1");
        check("0.5.0 等于自己", QString::number(AboutPage::compareVersions("0.5.0", "0.5.0")), "0");
        check("0.6.0 大于 0.5.9", QString::number(AboutPage::compareVersions("0.6.0", "0.5.9")), "1");
        check("0.6 小于 0.6.1（段数不齐也别判成相等）",
              QString::number(AboutPage::compareVersions("0.6", "0.6.1")), "-1");
        check("垃圾输入不炸也不判高（abc 当 0）",
              QString::number(AboutPage::compareVersions("abc", "0.0.0")), "0");
        check("tag 里的 v 前缀和尾巴都能剥掉", AboutPage::versionFromTag("v0.6.0-beta"),
              QStringLiteral("0.6.0"));
        check("tag 里没数字就返回空（别拿整个 tag 当版本）",
              AboutPage::versionFromTag("nightly"), QString());
        // 预发/稳定这条通道靠的就是这三条。GitHub 的 /releases/latest 实测跳过 pre-release
        // （拿 microsoft/PowerToys 对过：列表最新是 v0.102.2862.0 pre=True，latest 回的是
        // v0.101.2362.0），所以稳定通道不用改代码；而装预发的人要能看见正式版可升 ——
        // 那就是"同数字时带后缀的更旧"这一条说了算。
        check("0.8.1-beta.1 必须小于 0.8.1（预发是奔着它去的，不是它之后）",
              QString::number(AboutPage::compareVersions("0.8.1-beta.1", "0.8.1")), "-1");
        check("0.8.1-beta.2 大于 0.8.1-beta.1（同数字下后缀自己排）",
              QString::number(AboutPage::compareVersions("0.8.1-beta.2", "0.8.1-beta.1")), "1");
        check("0.9 大于 0.8.1-beta.9（数字段先分胜负，后缀不参与）",
              QString::number(AboutPage::compareVersions("0.9", "0.8.1-beta.9")), "1");

        // 资产名是服务器给的：路径分隔符、开头一个点、引号都不能带进落盘路径
        const QString evil = AboutPage::safeFileName(QStringLiteral("../evil.exe"));
        has(("资产名里的 ../ 被去掉：" + evil).toUtf8().constData(),
            !evil.contains('/') && !evil.contains('\\') && !evil.startsWith('.'));
        const QString quotedName = AboutPage::safeFileName(QStringLiteral("a\"b.exe"));
        has(("资产名里的引号被换掉：" + quotedName).toUtf8().constData(),
            !quotedName.contains('"'));

        // 拼给 PowerShell 的那段：值全部走单引号字面量，翻倍之后不许留逃逸口
        check("psQuote 把单引号翻倍", AboutPage::psQuote(QStringLiteral("a'b")),
              QStringLiteral("'a''b'"));
        const QString q = AboutPage::psQuote(QStringLiteral("x'; Stop-Process; #"));
        const QString inner = q.mid(1, q.size() - 2);
        bool paired = true;
        for (int i = 0; i < inner.size(); ++i) {
            if (inner.at(i) != QLatin1Char('\''))
                continue;
            if (i + 1 >= inner.size() || inner.at(i + 1) != QLatin1Char('\'')) {
                paired = false;
                break;
            }
            ++i;
        }
        has("psQuote 对注入串：单引号只能成对出现", paired
            && q.startsWith(QLatin1Char('\'')) && q.endsWith(QLatin1Char('\'')));

        const QString self = QCoreApplication::applicationFilePath();
        const QString appDir = QCoreApplication::applicationDirPath();
        QString err;
        const QString script = AboutPage::updateScript(self, appDir, 4321, &err);
        std::printf("INFO 那段脚本第一行：%s\n",
                    script.section('\n', 0, 0).toUtf8().constData());
        has(("脚本建得出来（拿刚编好的 exe 当安装包试）：" + err).toUtf8().constData(),
            !script.isEmpty());
        has("脚本里静默安装 + /DIR 指回现在这个目录",
            script.contains("/VERYSILENT") && script.contains(AboutPage::psQuote(appDir)));
        has("脚本先等我们自己退出（按 pid 轮询），装完再拉起来",
            script.contains("Get-Process -Id 4321") && script.contains("Start-Process"));
        err.clear();
        const QString withQuote = AboutPage::updateScript(self + QStringLiteral("'x"), appDir, 1, &err);
        has("路径里带单引号就整段拒绝（不给 PowerShell 留逃逸口）",
            withQuote.isEmpty() && !err.isEmpty());
        err.clear();
        has("安装包不存在也拒绝",
            AboutPage::updateScript(QStringLiteral("Z:/nope.exe"), appDir, 1, &err).isEmpty()
                && !err.isEmpty());

        // 页面本身：四颗按钮要在布局里、初始不该已经发过请求、下载/安装初始禁用
        AboutPage page;
        QSet<const QWidget*> laidOut;
        std::function<void(QLayout*)> walk = [&](QLayout* l) {
            if (!l)
                return;
            for (int i = 0; i < l->count(); ++i) {
                QLayoutItem* it = l->itemAt(i);
                if (!it)
                    continue;
                if (QWidget* w = it->widget()) {
                    laidOut.insert(w);
                    walk(w->layout());
                } else if (QLayout* sub = it->layout())
                    walk(sub);
            }
        };
        walk(page.layout());
        int onLayout = 0;
        for (const char* objName : { "checkUpdate", "downloadUpdate", "installUpdate", "openReleases" }) {
            QWidget* w = page.findChild<QWidget*>(QString::fromLatin1(objName));
            if (w && laidOut.contains(w))
                ++onLayout;
        }
        check("检查/下载/装并重启/发布页 四颗按钮都在布局里", QString::number(onLayout),
              QStringLiteral("4"));
        auto* status = page.findChild<QLabel*>(QStringLiteral("updateStatus"));
        check("刚建好还没碰过网络（状态还是初始那句）",
              status ? status->text() : QStringLiteral("(没有状态行)"),
              QStringLiteral("还没检查过"));
        bool downloadOff = false, installOff = false;
        for (const QPushButton* b : page.findChildren<QPushButton*>()) {
            if (b->objectName() == QLatin1String("downloadUpdate"))
                downloadOff = !b->isEnabled();
            if (b->objectName() == QLatin1String("installUpdate"))
                installOff = !b->isEnabled();
        }
        has("没查出结果之前下载/装都不给点", downloadOff && installOff);
        // 界面显示的版本必须和编译期那个同源（不是"我记得是 0.6.0"）
        bool showsVersion = false;
        for (const QLabel* l : page.findChildren<QLabel*>())
            if (l->text().contains(ver))
                showsVersion = true;
        has(("页面上显示的版本 == 编译期传进来的 " + ver).toUtf8().constData(), showsVersion);

        std::printf("about: 失败 %d 项\n", bad);
        std::fflush(stdout);
        return bad == 0 ? 0 : 1;
    }

    // --updatecheck：真去问一次 github（走配置里那条下载代理）。这是唯一能量出
    // "这台机器现在能不能问到新版"的入口 —— 点的是界面同一颗按钮，跑的是同一条异步路径。
    if (args.contains("--updatecheck")) {
        AboutPage page;
        auto* btn = page.findChild<QPushButton*>(QStringLiteral("checkUpdate"));
        auto* status = page.findChild<QLabel*>(QStringLiteral("updateStatus"));
        if (!btn || !status) {
            std::printf("FAIL 找不到「检查更新」那颗按钮或状态行\n");
            return 1;
        }
        btn->click();
        QString text = status->text();
        QElapsedTimer t;
        t.start();
        while (t.elapsed() < 40000 && text.contains(QStringLiteral("正在问"))) {
            QCoreApplication::processEvents();
            QThread::msleep(50);
            text = status->text();
        }
        const bool done = !text.contains(QStringLiteral("正在问"));
        std::printf("INFO 用时 %lld ms\n", t.elapsed());
        std::printf("INFO 界面状态：%s\n", text.toUtf8().constData());
        std::printf("%s 检查更新跑完并给出了一句实话\n", done ? "PASS" : "FAIL");
        std::printf("updatecheck: 失败 %d 项\n", done ? 0 : 1);
        return done ? 0 : 1;
    }

    // --diskbranch：验"边扫边报的一级分支大小"和最后树上的是不是一回事。
    // 这条必须能红：如果拆出去的孩子没算进父分支（或者算了两遍），
    // 完成时报出来的数就和定稿树对不上 —— 那界面上先给用户看的数字是假的。
    if (args.contains("--diskbranch")) {
        int bad = 0;
        QVector<QPair<QString, qint64>> last;
        qint64 firstHitMs = 0;
        QElapsedTimer wall;
        wall.start();
        const DiskScan::Tree tree = DiskScan::run("C:/", 1 << 20, [&](const DiskScan::Progress& p) {
            if (!p.doneBranches.isEmpty() && firstHitMs == 0)
                firstHitMs = wall.elapsed();
            last = p.doneBranches;
        });
        const qint64 totalMs = wall.elapsed();
        int rootKids = 0;
        for (int i = tree.firstKid[0]; i < tree.firstKid[0] + tree.kidCount[0]; ++i)
            ++rootKids;
        const auto say = [&bad](const char* name, bool ok, const QString& got) {
            std::printf("%s %s：%s\n", ok ? "PASS" : "FAIL", name, got.toUtf8().constData());
            if (!ok)
                ++bad;
            std::fflush(stdout);
        };
        say("报出来的分支数 == 树上的一级目录数",
            last.size() == rootKids, QStringLiteral("%1 / %2 个").arg(last.size()).arg(rootKids));
        QString mismatch;
        qint64 sum = 0;
        for (const auto& pr : last) {
            const int node = tree.nodeForPath(tree.rootPath + "/" + pr.first);
            const qint64 treeBytes = node >= 0 ? tree.size[node] : -1;
            sum += pr.second;
            if (treeBytes != pr.second && mismatch.isEmpty())
                mismatch = QStringLiteral("%1 报 %2 字节、树上 %3 字节")
                               .arg(pr.first).arg(pr.second).arg(treeBytes);
        }
        say("每个分支报的数都和定稿树一模一样", mismatch.isEmpty(),
            mismatch.isEmpty() ? QStringLiteral("%1 个分支全对上").arg(last.size()) : mismatch);
        say("一级分支之和 = 全盘减去卷根直属", sum + tree.fileBytes[0] == tree.total,
            QStringLiteral("%1 + %2 = %3").arg(sum).arg(tree.fileBytes[0]).arg(tree.total));
        const QString when = QStringLiteral("第一个分支 %1ms 就出数（整趟 %2ms）")
                                 .arg(firstHitMs).arg(totalMs);
        say("边扫边出数：第一个分支在整趟结束之前就报出来了", firstHitMs > 0 && firstHitMs < totalMs,
            when);
        std::printf("diskbranch: 失败 %d 项\n", bad);
        std::fflush(stdout);
        return bad == 0 ? 0 : 1;
    }

    // --diskui：磁盘页那条链整条走一遍（点「整理」→ 途中有没有边扫边出数 →
    // 定稿 → 换盘再换回来吃不吃缓存）。判据在插件里 DiskPage.cpp，因为它要点真按钮、
    // 要看真行卡片，主程序链接不到那些类型。这条慢（要真扫一整盘），所以不进 --plugintest。
    if (args.contains("--diskui"))
        return runPluginSelftest(QStringLiteral("disk"), "ltoolPluginUiSelftest");

    // --diskscale：量"多线程到底有没有用"。同一棵树按 1/2/4/8/16 线程各扫一遍
    // （第一趟之后目录项都在系统缓存里，比的是扩展性而不是冷缓存）。
    // 加这条是因为"磁盘分析太慢"要先分清：是活儿本来就这么多（那就得换 MFT 那条路），
    // 还是线程没吃满（那就调线程数/拆任务）。
    if (args.contains("--diskscale")) {
        static const int kCounts[] = { 1, 2, 4, 8, 16 };
        qint64 base = 0;
        for (int c : kCounts) {
            qputenv("LTOOL_DISKTHREADS", QByteArray::number(c));
            QElapsedTimer t;
            t.start();
            const DiskScan::Tree tree = DiskScan::run("C:/", 1 << 20);
            const qint64 ms = t.elapsed();
            const DiskScan::Timing tm = DiskScan::lastTiming();
            if (!base)
                base = ms;
            std::printf("  %2d 线程: %lld ms（扫盘 %lld、建树 %lld）%lld 目录 / %lld 文件 / %.2f GB"
                        "  相对第一趟 %.2fx\n",
                        c, ms, tm.walkMs, tm.mergeMs, tm.dirsSeen, tm.files,
                        tree.total / 1073741824.0, double(base) / double(ms));
            std::fflush(stdout);
        }
        qputenv("LTOOL_DISKTHREADS", QByteArray());
        std::printf("diskscale: 失败 0 项\n");
        return 0;
    }

    if (args.contains("--regscan"))
        return runRegScan();

    if (args.contains("--startupscan"))
        return runStartupScan();

    if (args.contains("--fw"))
        return runFwProbe(args);

    if (args.contains("--uptime"))
        return runUiTiming(args);

    if (args.contains("--adscan"))
        return runAdScan();

    if (args.contains("--fwtry"))
        return runFwTry(args);

    if (args.contains("--fwallow") || args.contains("--fwremove"))
        return runFwWrite(args);

    // --whohastest <路径>：无界面跑一遍句柄枚举，把每一步的结果打出来
    if (args.contains("--whohastest")) {
        const QString target = args.mid(args.indexOf("--whohastest") + 1).value(0);
        QString err;
        const auto holders = FileOps::whoHas(target, &err);
        std::printf("whohastest: target=%s holders=%d err=%s\n", target.toUtf8().constData(),
                    int(holders.size()), err.toUtf8().constData());
        for (const FileOps::Holder& h : holders)
            std::printf("  pid=%llu %s handles=%d\n", h.pid, h.processName.toUtf8().constData(), h.handles);
        std::fflush(stdout);
        return 0;
    }

    // --archivetest extract <包> <目标目录> / make <输入> <输出> [7z] [档位]
    //                 / probe <文件> / progress <输入> <输出> [7z] [第几次按取消]
    // 无界面跑一遍压缩包读写，给回归测试用；probe 只回答"右键点它会当成压缩包吗"；
    // progress 专门量进度与取消这两条只有界面在用的线（7z 后端换了 bit7z，它们必须重验）。
    if (args.contains("--archivetest")) {
        const QStringList a = args.mid(args.indexOf("--archivetest") + 1);
        if (a.value(0) == QLatin1String("extract")) {
            const Archive::Result r = Archive::extract(a.value(1), a.value(2), nullptr, nullptr);
            std::printf("extract ok=%d files=%d dirs=%d bytes=%lld target=%s err=%s\n",
                        r.ok ? 1 : 0, r.files, r.dirs, r.bytes,
                        r.target.toUtf8().constData(), r.error.toUtf8().constData());
        } else if (a.value(0) == QLatin1String("probe")) {
            std::printf("probe looksLikeArchive=%d\n", Archive::looksLikeArchive(a.value(1)) ? 1 : 0);
        } else if (a.value(0) == QLatin1String("progress")) {
            const bool seven = a.value(3) == QLatin1String("7z");
            // 第 4 个参数：回调第几次时"用户按了取消"。给个不可能达到的数就是全程不取消。
            bool stopsOk = false;
            const int stopAfter = a.value(4).toInt(&stopsOk) && stopsOk ? a.value(4).toInt() : 3;
            qint64 calls = 0, last = -1, peak = 0, total = 0;
            bool monotonic = true;
            const Archive::Report rep = [&](const Archive::Progress& pr) {
                ++calls;
                total = pr.total;
                if (pr.done < last)
                    monotonic = false;
                last = pr.done;
                peak = qMax(peak, pr.done);
                return calls < qint64(stopAfter);
            };
            const Archive::Result r = Archive::compress({ a.value(1) }, a.value(2), seven, rep, nullptr);
            const QString part = QDir::toNativeSeparators(a.value(2)) + QStringLiteral(".part");
            std::printf("progress ok=%d cancelled=%d calls=%lld total=%lld peak=%lld 单调=%d "
                        "半成品还在=%d 成品在=%d err=%s\n",
                        r.ok ? 1 : 0, r.cancelled ? 1 : 0, calls, total, peak,
                        monotonic ? 1 : 0, QFile::exists(part) ? 1 : 0,
                        QFile::exists(QDir::toNativeSeparators(a.value(2))) ? 1 : 0,
                        r.error.toUtf8().constData());
        } else {
            const bool seven = a.value(3) == QLatin1String("7z");
            // 第 5 个参数：7z 的档位（0-9，就是 7-Zip 自己的 -mx 数法，默认 5 = 它的默认档）。
            // zip 那条不吃这个数（libarchive 的 deflate 恒为 6 档）。
            bool lvlOk = false;
            const int level = a.value(4).toInt(&lvlOk);
            const Archive::Result r = Archive::compress({ a.value(1) }, a.value(2), seven, nullptr,
                                                        nullptr, lvlOk ? level : 5);
            std::printf("make ok=%d files=%d dirs=%d bytes=%lld target=%s err=%s\n",
                        r.ok ? 1 : 0, r.files, r.dirs, r.bytes,
                        r.target.toUtf8().constData(), r.error.toUtf8().constData());
        }
        std::fflush(stdout);
        return 0;
    }

    // --snaprect：F1 悬停吸附出来的选区到底贴不贴边（无界面、不开浮层）。
    // 三条判据：① 吸附框必须落在 GetWindowRect 之内 —— 那条里外多出来的圈就是这次的病；
    // ② 完整落在显示器内的窗口上，吸附框必须和 DWM 的可见边界**一模一样**；
    // ③ 至少要有一个窗口吸附框比 GetWindowRect 紧 —— 这台机器上确实存在那圈看不见的边框，
    //    不然 ① 是空转（两个矩形本来就相等时，什么实现都能"通过"）。
    if (args.contains("--snaprect")) {
        int checked = 0, fail = 0, tighter = 0;
        for (HWND h = GetWindow(GetDesktopWindow(), GW_CHILD); h && checked < 12;
             h = GetWindow(h, GW_HWNDNEXT)) {
            if (!IsWindowVisible(h) || GetWindowTextLengthW(h) <= 0)
                continue;
            RECT raw{};
            if (!GetWindowRect(h, &raw) || raw.right - raw.left < 60 || raw.bottom - raw.top < 40)
                continue;
            RECT vis{};
            const bool haveVis =
                SUCCEEDED(DwmGetWindowAttribute(h, DWMWA_EXTENDED_FRAME_BOUNDS, &vis, sizeof(vis)))
                && vis.right > vis.left && vis.bottom > vis.top;
            MONITORINFO mi{};
            mi.cbSize = sizeof(mi);
            const bool fullyOn =
                GetMonitorInfoW(MonitorFromWindow(h, MONITOR_DEFAULTTONEAREST), &mi)
                && raw.left >= mi.rcMonitor.left && raw.top >= mi.rcMonitor.top
                && raw.right <= mi.rcMonitor.right && raw.bottom <= mi.rcMonitor.bottom;
            const QRect own = CaptureOverlay::snapRectFor(reinterpret_cast<WId>(h));
            const QRect rRaw(raw.left, raw.top, raw.right - raw.left, raw.bottom - raw.top);
            const QRect rVis = haveVis ? QRect(vis.left, vis.top, vis.right - vis.left, vis.bottom - vis.top)
                                       : QRect();
            ++checked;
            wchar_t name[64] = L"";
            GetWindowTextW(h, name, 64);
            const bool inside = !own.isNull()
                                && own.left() >= rRaw.left() && own.top() >= rRaw.top()
                                && own.right() <= rRaw.right() && own.bottom() <= rRaw.bottom();
            const bool sameVisible = !haveVis || !fullyOn || own == rVis;
            if (own != rRaw)
                ++tighter;
            if (!inside || !sameVisible)
                ++fail;
            std::printf("%s %-24s 原始=%dx%d 可见=%s 吸附=%dx%d 四周去掉的余量 左%d 上%d 右%d 下%d\n",
                        (!inside || !sameVisible) ? "FAIL" : "ok  ",
                        QString::fromWCharArray(name).left(20).toUtf8().constData(),
                        rRaw.width(), rRaw.height(),
                        haveVis ? QString("%1x%2").arg(rVis.width()).arg(rVis.height()).toUtf8().constData()
                                : QString("拿不到").toUtf8().constData(),
                        own.width(), own.height(),
                        own.left() - rRaw.left(), own.top() - rRaw.top(),
                        rRaw.right() - own.right(), rRaw.bottom() - own.bottom());
        }
        std::printf("snaprect: 验了 %d 个窗口，失败 %d 项；吸附框比原始矩形紧的 %d 个", checked, fail, tighter);
        if (tighter == 0) {
            std::printf("  ← 一个都没有，这条测试什么都没测到，判失败");
            ++fail;
        }
        std::printf("\n");
        std::fflush(stdout);
        return fail;
    }

    // --appdirs：配置页「应用目录」那条路（往里 N 层找 exe）的无界面回归。
    // 夹具建在沙箱数据目录里（见上面那份白名单），路径全程是绝对路径 ——
    // 清理代码拿相对路径去 removeRecursively，删的就是"当时的工作目录"，
    // 这条曾经把整个工程目录删掉过一次，所以这里连删夹具都只删自己建的那一个子目录。
    if (args.contains("--appdirs")) {
        int bad = 0;
        const QString root = Utils::dataDir() + "/appdirs-fixture";
        const auto check = [&bad](const char* name, const QStringList& got,
                                  const QStringList& want) {
            const bool ok = got == want;
            if (!ok)
                ++bad;
            std::printf("%s %s got=[%s] want=[%s]\n", ok ? "PASS" : "FAIL", name,
                        got.join(",").toUtf8().constData(), want.join(",").toUtf8().constData());
        };
        const QString flt = Utils::dataDir() + "/appdirs-filter";
        if (!root.startsWith(QDir::tempPath()) || !root.contains("appdirs-fixture")
            || !flt.startsWith(QDir::tempPath()) || !flt.contains("appdirs-filter")) {
            std::printf("FAIL 夹具路径不对，什么都不建：%s\n", root.toUtf8().constData());
            std::fflush(stdout);
            return 1;
        }
        QDir().mkpath(root + QString::fromUtf8("/中文目录/三层/四层"));
        const auto mk = [&root](const QString& rel) {
            QFile f(root + "/" + rel);
            if (!f.open(QIODevice::WriteOnly))
                return false;
            f.write("MZ");
            f.close();
            return true;
        };
        // 再建一棵"像不像主程序"的夹具树。上面那几个 exe 只写了 "MZ" 两个字节，PE 头根本读不
        // 出来 —— 那是刻意的：解析失败不能变成"什么都不收"（真应用照样收得到），这条由老四条顺带守住。
        const auto mkPe = [&flt](const QString& rel, int subsystem) {
            QFile f(flt + "/" + rel);
            if (!f.open(QIODevice::WriteOnly))
                return false;
            QByteArray img(512, '\0');
            img[0] = 'M'; img[1] = 'Z';
            img[60] = char(64);                       // e_lfanew = 64
            img[64] = 'P'; img[65] = 'E';
            img[64 + 24 + 68] = char(subsystem);      // Subsystem：可选头 +68（2=GUI，3=控制台）
            f.write(img);
            f.close();
            return true;
        };
        QDir().mkpath(flt + "/7.4.1");
        QDir().mkpath(flt + "/locales");
        // 光有自造字节不够 —— 上一版实现和夹具按同一个错偏移写，测试当场白过。
        // 再拷两个系统里现成的：ping 是控制台、notepad 是 GUI，让真实 PE 也过一遍这条判据。
        const QString sysroot = qEnvironmentVariable("SystemRoot", QStringLiteral("C:/Windows"));
        const bool copiedReal =
            QFile::copy(sysroot + "/System32/ping.exe", flt + "/ping.exe")
            && QFile::copy(sysroot + "/System32/notepad.exe", flt + "/notepad.exe");
        const bool built = mk("one.exe") && mk(QString::fromUtf8("中文目录/中文two.exe"))
                           && mk("中文目录/三层/three.exe")
                           && mk("中文目录/三层/四层/four.exe")
                           && mk("Uninstall Bar.exe")
                           && mkPe("gui.exe", 2) && mkPe("tool.exe", 3)
                           && mkPe("FooUpdater.exe", 2) && mkPe("gui2.exe", 2)
                           && mkPe("7.4.1/Inside.exe", 2) && mkPe("locales/X.exe", 2)
                           && copiedReal;
        if (!built)
            std::printf("FAIL 夹具没建全（中文路径写不进去？）\n");

        QSettings cfg(Utils::dataDir() + "/config.ini", QSettings::IniFormat);
        // 扫出来的"来自目录"的那批名字，就是这条功能的全部产出
        const auto namesWith = [&](const QStringList& dirs, int depth) {
            cfg.setValue("apps/roots", dirs);
            cfg.setValue("apps/depth", depth);
            cfg.sync();
            QStringList out;
            for (const AppEntry& e : AppScanner::scan())
                if (e.category == QLatin1String("folder"))
                    out << e.name;
            out.sort();   // 按码元排：ASCII 名字在中文前面，期望值也照这个写
            return out;
        };
        const QString nat = QDir::toNativeSeparators(root);
        const QString cn = QString::fromUtf8("中文two");
        check("层数 0 只看这个目录本身", namesWith({nat}, 0), {"one"});
        check("层数 1 进一层（中文目录要能进去）", namesWith({nat}, 1), {"one", cn});
        check("层数 2 进两层", namesWith({nat}, 2), {"one", "three", cn});
        check("层数 3 进三层", namesWith({nat}, 3), {"four", "one", "three", cn});
        check("卸载器不算应用", namesWith({nat}, 3).contains("Uninstall Bar") ? QStringList{"混进来了"}
                                                                             : QStringList{},
              QStringList{});
        check("直接指到一个 exe 上也收", namesWith({QDir::toNativeSeparators(
                  root + QString::fromUtf8("/中文目录/中文two.exe"))}, 3), {cn});
        check("目录不存在只是跳过，不报错", namesWith({"Q:/没有这个东西/ltool"}, 3), QStringList{});
        // ---- "像不像主程序"那两道：控制台子系统、名字/路径像助手，都不该进应用列表 ----
        const QString fltNat = QDir::toNativeSeparators(flt);
        check("目录里只留看起来是主程序的（命令行工具/助手名/版本目录/运行时目录都跳过）",
              namesWith({fltNat}, 1), QStringList{ "gui", "gui2", "notepad" });
        check("真实系统 exe 也算得对：ping（控制台）被跳、notepad（GUI）留下",
              namesWith({fltNat}, 1).contains("ping") ? QStringList{ "ping 混进来了" } : QStringList{},
              QStringList{});
        // 同目录已有主程序就不再多收一个：点名 gui.exe 之后，整个 flt 目录只留它
        // （notepad.exe 也在这棵树里，所以它一起被"同目录已有主程序"这条挡掉 —— 要它就再点名一次）
        check("点名了同目录的主程序，同目录其余 exe 不再自动塞进来",
              namesWith({fltNat, QDir::toNativeSeparators(flt + "/gui.exe")}, 1),
              QStringList{ "gui" });
        check("点名指到那个命令行 exe：一定要它，不过滤",
              namesWith({QDir::toNativeSeparators(flt + "/tool.exe")}, 3), QStringList{ "tool" });
        // 搜索框读的是 apps.json（loadIndex），不是 scan() 现扫的那份 ——
        // 所以还要验一遍"存进去再读回来，目录来源的那些条一条没丢"
        cfg.setValue("apps/roots", QStringList{nat});
        cfg.setValue("apps/depth", 3);
        cfg.sync();
        AppScanner::saveIndex(AppScanner::scan());
        QStringList back;
        for (const AppEntry& e : AppScanner::loadIndex())
            if (e.category == QLatin1String("folder"))
                back << e.name;
        back.sort();
        check("存进 apps.json 再读回来还在", back, {"four", "one", "three", cn});
        cfg.remove("apps/roots");
        cfg.remove("apps/depth");
        cfg.sync();
        if (!QDir(root).removeRecursively())
            std::printf("FAIL 夹具没删干净：%s\n", root.toUtf8().constData());
        if (!QDir(flt).removeRecursively())
            std::printf("FAIL 过滤夹具没删干净：%s\n", flt.toUtf8().constData());
        std::printf("appdirs: 失败 %d 项\n", bad);
        std::fflush(stdout);
        return bad;
    }

    // --cfgcheck：离屏建一次配置页，把表单行名数一遍。
    // 为什么要这条：这次「下载代理」在配置菜单里出现了两遍（回放历史时那一行被插了两次），
    // 而配置页是 GUI —— 我不往他桌面上点，就只能把"某个行名出现几次"变成可断言的数字。
    // 顺带盯住那行从来没生效过的「扫描根目录」别再被人加回来。沙箱 APPDATA，不碰真实配置。
    if (args.contains("--cfgcheck")) {
        int bad = 0;
        ConfigPage page;
        const auto labels = page.findChildren<QLabel*>();
        const auto rowCount = [&labels](const QString& name) {
            int n = 0;
            for (const QLabel* l : labels)
                if (l->text().trimmed() == name)
                    ++n;
            return n;
        };
        const auto hasRow = [&rowCount](const QString& name) { return rowCount(name) > 0; };
        const auto say = [&bad](const char* what, bool ok, const QString& got) {
            std::printf("%s %s：%s\n", ok ? "ok  " : "FAIL", what, got.toUtf8().constData());
            if (!ok)
                ++bad;
            std::fflush(stdout);
        };
        const int proxy = rowCount(QStringLiteral("下载代理："));
        say("「下载代理」只有一行", proxy == 1, QStringLiteral("数到 %1 行").arg(proxy));
        const int roots = rowCount(QStringLiteral("扫描根目录："));
        say("没生效过的「扫描根目录」已拿掉", roots == 0, QStringLiteral("数到 %1 行").arg(roots));
        const bool hasList = hasRow(QStringLiteral("项目列表："));
        const bool hasDepth = hasRow(QStringLiteral("往里找几层："));
        say("应用那一栏在（列表 + 层数）", hasList && hasDepth,
            QStringLiteral("项目列表=%1 往里找几层=%2").arg(hasList ? 1 : 0).arg(hasDepth ? 1 : 0));
        int addExeBtn = 0;
        int addDirBtn = 0;
        for (QPushButton* b : page.findChildren<QPushButton*>()) {
            if (b->objectName() == QStringLiteral("addAppExe"))
                ++addExeBtn;
            if (b->objectName() == QStringLiteral("addAppDir"))
                ++addDirBtn;
        }
        say("「添加应用…」和「添加目录…」各只有一个", addExeBtn == 1 && addDirBtn == 1,
            QStringLiteral("应用=%1 目录=%2").arg(addExeBtn).arg(addDirBtn));
        const auto lists = page.findChildren<QListWidget*>();
        say("列表控件真的建出来了", !lists.isEmpty(),
            QStringLiteral("%1 个 QListWidget").arg(lists.size()));
        // 上面那两条只数得到"控件建没建"，数不出"摆没摆进布局"。应用目录那一栏就是这么
        // 在页面上隐身的：分组框 new 出来时认了 this 当父件，却从没 addWidget 到那一列，
        // 于是标签 findChild 找得到、--cfgcheck 一片绿，界面上却一行都没有（2026-10-12 他报的）。
        // 所以这条把"布局里能走到"当成结构不变量，逐个分组框验一遍，谁没挂上就点谁的名。
        QSet<const QWidget*> laidOut;
        std::function<void(QLayout*)> walk = [&](QLayout* l) {
            if (!l)
                return;
            for (int i = 0; i < l->count(); ++i) {
                QLayoutItem* it = l->itemAt(i);
                if (!it)
                    continue;
                if (QWidget* w = it->widget()) {
                    laidOut.insert(w);
                    walk(w->layout());
                    if (auto* sc = qobject_cast<QScrollArea*>(w))   // 滚动区里那层不是布局项
                        if (sc->widget())
                            walk(sc->widget()->layout());
                } else if (QLayout* sub = it->layout())
                    walk(sub);
            }
        };
        walk(page.layout());
        QString orphans;
        int groups = 0;
        for (const QGroupBox* g : page.findChildren<QGroupBox*>()) {
            ++groups;
            if (!laidOut.contains(g))
                orphans += QStringLiteral("「%1」").arg(g->title());
        }
        say("每个分组框都真的摆在布局里（没摆=界面上看不见）", orphans.isEmpty(),
            orphans.isEmpty() ? QStringLiteral("%1 个分组框全在布局里").arg(groups) : orphans);
        std::printf("cfgcheck: 失败 %d 项\n", bad);
        std::fflush(stdout);
        return bad;
    }

    // --pinyin：无界面验拼音匹配。核心是"表和码点对齐"（错一位就会把"垃圾"当成"任务"）
    // + 多音字 + 首字母只认开头 + 纯英文名不掺和 + 每次按键的耗时。
    if (args.contains("--pinyin")) {
        int bad = 0;
        const auto check = [&bad](const char* name, const QString& got, const QString& want) {
            const bool ok = got == want;
            if (!ok)
                ++bad;
            std::printf("%s %s got=[%s] want=[%s]\n", ok ? "PASS" : "FAIL", name,
                        got.toUtf8().constData(), want.toUtf8().constData());
        };
        const auto kind = [](Pinyin::Kind k) {
            return k == Pinyin::Full ? QStringLiteral("full")
                 : k == Pinyin::Initial ? QStringLiteral("initial") : QStringLiteral("none");
        };
        check("表：任 → ren", Pinyin::fullReadings(QStringLiteral("任")).contains("ren") ? "yes" : "no", "yes");
        check("表没错位：垃圾 → laji", Pinyin::fullReadings(QStringLiteral("垃圾")).contains("laji") ? "yes" : "no", "yes");
        check("renwu 命中 任务", kind(Pinyin::match(QStringLiteral("任务"), "renwu")), "full");
        check("renwu 命中 任务管理器（整名）",
              kind(Pinyin::match(QStringLiteral("任务管理器"), "renwu")), "full");
        check("rw 命中 任务（首字母）", kind(Pinyin::match(QStringLiteral("任务"), "rw")), "initial");
        check("chongqing 命中 重庆", kind(Pinyin::match(QStringLiteral("重庆"), "chongqing")), "full");
        check("zhongqing 也命中 重庆（多音字全留）", kind(Pinyin::match(QStringLiteral("重庆"), "zhongqing")), "full");
        check("cq 命中 重庆（首字母）", kind(Pinyin::match(QStringLiteral("重庆"), "cq")), "initial");
        check("中间片段也算：guanli 命中 任务管理器",
              kind(Pinyin::match(QStringLiteral("任务管理器"), "guanli")), "full");
        check("全拼允许中段：wu 也算 任务", kind(Pinyin::match(QStringLiteral("任务"), "wu")), "full");
        check("首字母只认开头：gl 不算 任务管理器",
              kind(Pinyin::match(QStringLiteral("任务管理器"), "gl")), "none");
        check("不误伤：wx 不命中 任务", kind(Pinyin::match(QStringLiteral("任务"), "wx")), "none");
        // 直达那一档必须从开头命中：中段命中只是"名字里正好含这几个字母"，
        // 拿它独占整页会把用户要的东西顶掉（renwu 被"计划任务"抢走那次就是这么来的）。
        check("中段命中：renwu 算 计划任务（match 允许中段，应用那一档靠它）",
              kind(Pinyin::match(QStringLiteral("计划任务"), "renwu")), "full");
        check("直达不认中段：renwu 不算 计划任务（matchStart）",
              kind(Pinyin::matchStart(QStringLiteral("计划任务"), "renwu")), "none");
        check("直达认开头：cipanqingli 算 磁盘清理（matchStart）",
              kind(Pinyin::matchStart(QStringLiteral("磁盘清理"), "cipanqingli")), "full");
        check("直达认开头：cpql 也算 磁盘清理（matchStart）",
              kind(Pinyin::matchStart(QStringLiteral("磁盘清理"), "cpql")), "initial");
        check("纯英文名不走拼音这一档", kind(Pinyin::match(QStringLiteral("Qoder"), "oder")), "none");
        check("单字母不启用", Pinyin::usableQuery(QStringLiteral("r")) ? "yes" : "no", "no");
        check("带数字的名字：腾讯会议3",
              Pinyin::fullReadings(QStringLiteral("腾讯会议3")).contains("tengxunhuiyi3") ? "yes" : "no", "yes");
        check("带数字的首字母", Pinyin::initialReadings(QStringLiteral("腾讯会议3")).contains("txhy3") ? "yes" : "no", "yes");

        const auto apps = AppScanner::loadIndex();
        // 耗时：真实应用表里中文名没几个，光量它测不出最坏情况 —— 再掺 600 个六字中文名，
        // 量的是"每次按键都要扫一遍全部名字"那条路。
        QStringList names;
        for (const AppEntry& a : apps)
            names << a.name;
        const QString pool = QStringLiteral("任务管理器设置网络磁盘清理软件更新注册防火墙剪贴板截图录像拼音搜索");
        for (int i = 0; i < 600; ++i) {
            QString nm;
            for (int j = 0; j < 6; ++j)
                nm += pool.at((i * 7 + j * 13) % pool.size());
            names << nm;
        }
        QElapsedTimer t;
        t.start();
        for (const QString& nm : names)
            Pinyin::fullReadings(nm);
        const qint64 buildUs = t.nsecsElapsed() / 1000;
        t.restart();
        int hits = 0;
        for (int i = 0; i < 200; ++i)
            for (const QString& nm : names)
                if (Pinyin::match(nm, "renwu") != Pinyin::None)
                    ++hits;
        // 注意别先除以 200 再打：整数除法会把 8ms 打成 0ms，看着像"快到没有"（假绿）。
        const qint64 scanUs = t.nsecsElapsed() / 1000 / 200;
        std::printf("INFO 名字 %d 个（含 600 个六字中文名）：首次展开 %lldus，缓存后扫全表 %lldus/次，"
                    "renwu 命中 %d 个\n",
                    int(names.size()), long long(buildUs), long long(scanUs), hits / 200);
        check("首次展开全表 <300ms", buildUs < 300000 ? "yes" : "no", "yes");
        check("缓存后一次查询 <3ms", scanUs < 3000 ? "yes" : "no", "yes");
        // ---- 端到端：真建一个不显示的启动器窗口，往输入框里打拼音，看结果列表出不出来。
        //      行是 delegate 画的、没有文字，所以数的是"节标题 + 可执行行的个数"。----
        SearchWindow w;
        auto* input = w.findChild<QLineEdit*>(QStringLiteral("input"));
        auto* list = w.findChild<QListWidget*>(QStringLiteral("list"));
        const bool haveUi = input && list;
        check("拿得到输入框和结果列表", haveUi ? "yes" : "no", "yes");
        if (haveUi) {
            const auto settle = [] {
                for (int i = 0; i < 40; ++i) {
                    QCoreApplication::processEvents();
                    QThread::msleep(10);
                }
            };
            const auto hasSection = [list](const QString& s) {
                for (int i = 0; i < list->count(); ++i)
                    if (list->item(i)->text() == s)
                        return true;
                return false;
            };
            const auto rows = [list] {
                int n = 0;
                for (int i = 0; i < list->count(); ++i)
                    if (list->item(i)->text().isEmpty() && list->item(i)->data(Qt::UserRole).toInt() >= 0)
                        ++n;
                return n;
            };
            // 把结果里出现了哪些小节原样打出来：查"某个词为什么搜不到 X"的时候，
            // 光看断言红不红不够，得看见是哪一节把页占了（直达 return 那一类）。
            const auto sections = [list] {
                QStringList out;
                for (int i = 0; i < list->count(); ++i) {
                    const QString t = list->item(i)->text();
                    if (!t.isEmpty())
                        out << t;
                }
                return out.join(QStringLiteral(" / "));
            };
            const auto query = [&](const QString& q) {
                input->setText(q);
                settle();
                return qMakePair(hasSection(QStringLiteral("工具")), rows());
            };
            const auto t0 = query(QStringLiteral("zzqqxxww"));   // 乱码：什么都不该出
            const auto t1 = query(QStringLiteral("cipanqingli"));  // 磁盘清理的全拼
            const auto t2 = query(QStringLiteral("cpql"));         // 它的首字母
            std::printf("INFO 端到端：乱码 工具节=%d 行=%d ｜全拼 工具节=%d 行=%d ｜首字母 工具节=%d 行=%d\n",
                        t0.first ? 1 : 0, int(t0.second), t1.first ? 1 : 0, int(t1.second),
                        t2.first ? 1 : 0, int(t2.second));
            check("乱码查询不出工具", t0.first ? "yes" : "no", "no");
            check("全拼 cipanqingli 直达工具", t1.first ? "yes" : "no", "yes");
            check("首字母 cpql 也直达工具", t2.first ? "yes" : "no", "yes");
            // 他实际报的那一个：任务管理器不在卡片墙那六件事里，走的是「应用」那一节
            // （SysTools::systemTools() 并进应用索引），所以这条要断言的是应用节。
            input->setText(QStringLiteral("renwu"));
            settle();
            const bool appSect = hasSection(QStringLiteral("── 应用 ──"));
            const bool toolSect = hasSection(QStringLiteral("工具"));
            std::printf("INFO 端到端 renwu：应用节=%d 工具节=%d 可执行行=%d\n",
                        appSect ? 1 : 0, toolSect ? 1 : 0, int(rows()));
            check("renwu 在搜索框里能搜到应用（任务管理器）", appSect ? "yes" : "no", "yes");
            check("renwu 不再被「计划任务」那条关键词当成工具直达（它会把应用整个顶掉）",
                  toolSect ? "yes" : "no", "no");
            // 取整之后直达不能变哑：qingli 不是「磁盘清理」这个名字的开头，
            // 但它是关键词「清理」的开头 —— 这条守的是"别把直达搞成搜不到"。
            const auto t4 = query(QStringLiteral("qingli"));
            check("取整不伤直达：qingli 还是直达磁盘清理", t4.first ? "yes" : "no", "yes");
            // 他报的第二件事：搜 qq 找不到 QQ。这台机器上 QQ 在应用索引里（apps.json 有
            // name=QQ、target 在盘上），所以这里验的是搜索框那一条链，不是索引。
            input->setText(QStringLiteral("qq"));
            settle();
            std::printf("INFO 端到端 qq：小节=[%s] 可执行行=%d\n",
                        sections().toUtf8().constData(), int(rows()));
            check("qq 能搜到应用（这台机器上 QQ 就在应用索引里）",
                  hasSection(QStringLiteral("── 应用 ──")) ? "yes" : "no", "yes");
            // 第一行必须是 QQ 本人。以前不是：使用次数记在 lnkPath 上，而目录扫来的那些没有 lnk，
            // 全共用同一个空键（这台机器那个键涨到 43），QQEX 就靠"别人家的 43 次"压在 QQ 前面。
            QString firstApp;
            for (int i = 0; i < w.resultCount(); ++i)
                if (w.resultAt(i).kind == SearchResult::App) {
                    firstApp = w.resultAt(i).title;
                    break;
                }
            check("搜 qq 的第一行就是 QQ（使用次数不再共用那个空键）", firstApp, QStringLiteral("QQ"));
            // 中文输入法在"全角"下打出来的 ＱＱ 不是 ASCII 的 qq —— 这条就是他报的那个形状，
            // 现在归一到半角，两种打法都必须出同一个应用节
            input->setText(QString::fromUtf8("\xef\xbc\xb1\xef\xbc\xb1"));
            settle();
            std::printf("INFO 端到端 全角ＱＱ：小节=[%s] 可执行行=%d\n",
                        sections().toUtf8().constData(), int(rows()));
            check("全角ＱＱ 也当半角 qq 用（输入法切错不该搜不到）",
                  hasSection(QStringLiteral("── 应用 ──")) ? "yes" : "no", "yes");
            input->setText(QStringLiteral("qq"));
            settle();
            input->setText(QStringLiteral("rw"));
            settle();
            const bool appSect2 = hasSection(QStringLiteral("── 应用 ──"));
            check("rw（首字母）也能搜到应用", appSect2 ? "yes" : "no", "yes");
            // 应用那一侧：拿真实索引里第一个中文名当靶子，用它的拼音去搜必须回到它
            QString cjkName;
            for (const AppEntry& a : apps)
                if (!Pinyin::fullReadings(a.name).isEmpty() && a.name != a.name.toLower()) {
                    bool hasHan = false;
                    for (const QChar& c : a.name)
                        if (c.unicode() >= 0x3400 && c.unicode() <= 0x9FFF)
                            hasHan = true;
                    if (hasHan) { cjkName = a.name; break; }
                }
            if (cjkName.isEmpty()) {
                std::printf("INFO 本机应用索引里没有中文名应用，应用那一侧的端到端只验节标题逻辑\n");
            } else {
                const QString py = Pinyin::fullReadings(cjkName).first();
                input->setText(py);
                settle();
                std::printf("INFO 用「%s」的拼音 %s 搜：应用节=%d 行数=%d\n",
                            cjkName.toUtf8().constData(), py.toUtf8().constData(),
                            hasSection(QStringLiteral("── 应用 ──")) ? 1 : 0, rows());
                check("中文应用名可以用拼音搜到",
                      hasSection(QStringLiteral("── 应用 ──")) ? "yes" : "no", "yes");
            }
            input->clear();
            settle();
        }
        std::printf("pinyin: 失败 %d 项\n", bad);
        std::fflush(stdout);
        return bad == 0 ? 0 : 1;
    }

    // --archspeed [每文件KB] [文件数]：量压缩包读写的吞吐（MB/s），并拿系统里的 7-Zip 当参照，
    // 用来回答"解压/压缩很慢"到底慢在哪：容器格式、编码器单线程、还是每条目录项的开销。
    // 默认 4096KB × 15 = 60MB 的伪日志文本；海量小文件那一档用 `--archspeed 2 2000`。
    // 夹具全在 %TEMP%\ltool-archspeed，不碰用户目录。
    if (args.contains("--archspeed")) {
        const QStringList rest = args.mid(args.indexOf("--archspeed") + 1);
        const qint64 fileKb = rest.value(0, QStringLiteral("4096")).toLongLong();
        const int fileCount = rest.value(1, QStringLiteral("15")).toInt();
        const QString root = QDir::tempPath() + QStringLiteral("/ltool-archspeed");
        QDir(root).removeRecursively();
        const QString src = root + QStringLiteral("/src");
        QDir().mkpath(src);

        quint64 s = 0x2545F4914F6CDD1DULL;
        qint64 total = 0;
        for (int i = 0; i < fileCount; ++i) {
            QFile o(src + QStringLiteral("/f%1.log").arg(i, 4, 10, QLatin1Char('0')));
            if (!o.open(QIODevice::WriteOnly | QIODevice::Truncate))
                continue;
            QByteArray body;
            body.reserve(int(fileKb) * 1024);
            while (qint64(body.size()) < fileKb * 1024) {
                s ^= s << 13;
                s ^= s >> 7;
                s ^= s << 17;
                body += "2026-10-11 12:00:00 INFO worker-";
                body += QByteArray::number(qint64(s % 97));
                body += " processed item ";
                body += QByteArray::number(qint64(s % 100000));
                body += " in ";
                body += QByteArray::number(qint64(s % 500));
                body += "ms status=OK\n";
            }
            body.truncate(int(fileKb) * 1024);
            o.write(body);
            total += body.size();
        }
        std::printf("INFO 夹具：%d 个文件 × %lld KB = %.1f MB（伪日志文本，压得动的那种）\n",
                    fileCount, fileKb, double(total) / 1048576.0);

        QElapsedTimer t;
        const auto rate = [](qint64 bytes, qint64 ms) {
            return ms > 0 ? double(bytes) / 1048576.0 * 1000.0 / double(ms) : 0.0;
        };
        const auto row = [&](const char* op, const char* who, qint64 bytes, qint64 ms, qint64 outBytes) {
            std::printf("%s %-12s %8lld ms  %7.1f MB/s  产物 %6.1f MB\n", op, who, ms,
                        rate(bytes, ms), double(outBytes) / 1048576.0);
            std::fflush(stdout);
        };
        const auto ours = [&](const QString& pkg, const QString& dest) {
            t.restart();
            const Archive::Result r = Archive::extract(pkg, dest, nullptr, nullptr);
            return qMakePair(t.elapsed(), r.ok ? r.files : -1);
        };

        const QString seven = QStringLiteral("C:/Program Files/7-Zip/7z.exe");
        const bool have7z = QFileInfo::exists(seven);
        int last7zRc = -99;
        const auto run7z = [&](const QStringList& a) {
            QElapsedTimer e;
            e.start();
            QProcess p;
            p.setProgram(seven);
            p.setArguments(a);
            p.setProcessChannelMode(QProcess::SeparateChannels);
            p.start();
            if (!p.waitForFinished(600000))
                return qint64(-1);
            last7zRc = p.exitCode();
            return e.elapsed();
        };
        if (!have7z)
            std::printf("INFO 没装 7-Zip，跳过参照组与互操作检查\n");

        const QString ourZip = root + QStringLiteral("/our.zip");
        const QString our7z = root + QStringLiteral("/our.7z");
        t.start();
        const Archive::Result cz = Archive::compress({ src }, ourZip, false, nullptr, nullptr);
        row("压缩", "我们 zip", total, t.elapsed(), QFileInfo(ourZip).size());
        t.restart();
        const Archive::Result c7 = Archive::compress({ src }, our7z, true, nullptr, nullptr);
        row("压缩", "我们 7z=默认5", total, t.elapsed(), QFileInfo(our7z).size());
        if (!cz.ok || !c7.ok)
            std::printf("INFO 我们这边没压出来：zip=%s 7z=%s\n", cz.error.toUtf8().constData(),
                        c7.error.toUtf8().constData());
        row("解压", "我们 zip", total, ours(ourZip, root + "/d1").first, 0);
        row("解压", "我们 7z", total, ours(our7z, root + "/d2").first, 0);

        // 7z 这条现在是 7-Zip 自己的编码器（多线程），档位照旧逐档量。每档验两件事：
        // 我们自己的读取器解得回来、7-Zip 也认这个包 —— 换档位最怕的就是
        // "快是快了，别人打不开"。7-Zip 的枚举只定义了 0/1/3/5/7/9，就扫这几个。
        for (int lvl : { 1, 3, 5, 7, 9 }) {
            const QString p = root + QStringLiteral("/lv%1.7z").arg(lvl);
            t.restart();
            const Archive::Result r = Archive::compress({ src }, p, true, nullptr, nullptr, lvl);
            const qint64 ms = t.elapsed();
            const int back = ours(p, root + QStringLiteral("/lv%1d").arg(lvl)).second;
            const QString tag = QStringLiteral("我们 档%1").arg(lvl);
            row("压缩", tag.toUtf8().constData(), total, ms, QFileInfo(p).size());
            std::printf("        ok=%d 我们自己解回=%d 个文件", r.ok ? 1 : 0, back);
            if (have7z) {
                run7z({ QStringLiteral("t"), p, QStringLiteral("-y") });
                std::printf("  7z t 退出码=%d", last7zRc);
            }
            std::printf("\n");
            std::fflush(stdout);
        }

        if (have7z) {
            const QString refZip = root + QStringLiteral("/ref.zip");
            const QString ref7z = root + QStringLiteral("/ref.7z");
            QFile::remove(refZip);
            QFile::remove(ref7z);
            // 注意别把 run7z(...) 和 QFileInfo(...).size() 写成同一个调用的两个实参：
            // C++ 不保证实参求值顺序，size 会在 7z 还没跑之前就读到一个不存在的文件（=0）
            const qint64 m1 = run7z({ QStringLiteral("a"), QStringLiteral("-tzip"),
                                      QStringLiteral("-mx=5"), refZip, src });
            row("压缩", "7z -tzip", total, m1, QFileInfo(refZip).size());
            const qint64 m2 = run7z({ QStringLiteral("a"), QStringLiteral("-t7z"),
                                      QStringLiteral("-mx=5"), ref7z, src });
            row("压缩", "7z -t7z", total, m2, QFileInfo(ref7z).size());
            const qint64 m3 = run7z({ QStringLiteral("x"), refZip,
                                      QStringLiteral("-o") + root + QStringLiteral("/r1"),
                                      QStringLiteral("-y") });
            row("解压", "7z x zip", total, m3, 0);
            const qint64 m4 = run7z({ QStringLiteral("x"), ref7z,
                                      QStringLiteral("-o") + root + QStringLiteral("/r2"),
                                      QStringLiteral("-y") });
            row("解压", "7z x 7z", total, m4, 0);
            row("解压", "我们 x 7z", total, ours(ref7z, root + "/d3").first, 0);
        }
        QDir(root).removeRecursively();
        return 0;
    }

    // --ocrtest <图片>：无界面跑一遍"图里取字"，把识别结果原样打出来，用来验准确率
    if (args.contains("--ocrtest")) {
        const QString target = args.mid(args.indexOf("--ocrtest") + 1).value(0);
        QImage img(target);
        if (img.isNull()) {
            std::printf("ocrtest: 读不了这张图 %s\n", target.toUtf8().constData());
            std::fflush(stdout);
            return 2;
        }
        QElapsedTimer t;
        t.start();
        QString err;
        const QString text = Ocr::recognize(img, &err);
        std::printf("ocrtest: ms=%lld chars=%d err=%s\n--- 识别结果 ---\n%s\n",
                    t.elapsed(), int(text.size()), err.toUtf8().constData(), text.toUtf8().constData());
        std::fflush(stdout);
        return err.isEmpty() ? 0 : 1;
    }

    // --ocrlayout <图片>：把"带位置的识别"原样打出来（行框/词框/切出来的字框），
    // 划词全靠这些坐标，所以拿一张字在已知位置的图来对一下偏了多少
    if (args.contains("--ocrlayout")) {
        const QString target = args.mid(args.indexOf("--ocrlayout") + 1).value(0);
        QImage img(target);
        if (img.isNull()) {
            std::printf("ocrlayout: 读不了这张图 %s\n", target.toUtf8().constData());
            std::fflush(stdout);
            return 2;
        }
        QElapsedTimer t;
        t.start();
        QString err;
        const QVector<Ocr::Line> lines = Ocr::recognizeLayout(img, &err);
        std::printf("ocrlayout: img=%dx%d lines=%d ms=%lld err=%s\n",
                    img.width(), img.height(), int(lines.size()), t.elapsed(),
                    err.toUtf8().constData());
        for (int i = 0; i < lines.size(); ++i) {
            const Ocr::Line& l = lines.at(i);
            std::printf("  L%d rect=(%d,%d %dx%d) words=%d text=%s\n", i, l.rect.x(), l.rect.y(),
                        l.rect.width(), l.rect.height(), int(l.words.size()),
                        l.text.toUtf8().constData());
            for (int w = 0; w < l.words.size() && w < 6; ++w) {
                const Ocr::Word& wd = l.words.at(w);
                std::printf("      W%d (%d,%d %dx%d) \"%s\"\n", w, wd.rect.x(), wd.rect.y(),
                            wd.rect.width(), wd.rect.height(), wd.text.toUtf8().constData());
            }
            if (i < 2) {
                const QVector<Ocr::Cell> cs = Ocr::cells(l);
                QString boxes;
                for (const Ocr::Cell& c : cs)
                    boxes += QString("[%1,%2 %3x%4]%5 ").arg(c.rect.x()).arg(c.rect.y())
                                       .arg(c.rect.width()).arg(c.rect.height()).arg(c.ch);
                std::printf("      cells=%d %s\n", int(cs.size()), boxes.toUtf8().constData());
            }
        }
        std::fflush(stdout);
        return err.isEmpty() ? 0 : 1;
    }

    // --pinpick <图片>：不碰桌面，把鼠标事件直接送给置顶窗，验"图上划词"整条链
    // （按下→拖→松手→剪贴板）。合成鼠标会抢用户的键鼠，所以这条链用送事件的方式测。
    if (args.contains("--pinpick")) {
        const QString target = args.mid(args.indexOf("--pinpick") + 1).value(0);
        QImage img(target);
        if (img.isNull()) {
            std::printf("pinpick: 读不了这张图 %s\n", target.toUtf8().constData());
            std::fflush(stdout);
            return 2;
        }
        auto* pw = new PinWindow(QPixmap::fromImage(img), QString(), false);
        // 这条自测要往剪贴板写东西，先把用户原有的内容存下来，跑完归还
        auto* savedClip = new QMimeData();
        if (const QMimeData* md = QGuiApplication::clipboard()->mimeData())
            for (const QString& fmt : md->formats())
                savedClip->setData(fmt, md->data(fmt));
        // 同步认完字并把划词模式开着（真按钮走的是后台线程，结果一样）
        pw->pickProbe(QRect(100000, 100000, 1, 1));
        QClipboard* clip = QGuiApplication::clipboard();
        const auto clipWrite = [clip](const QString& v) {
            for (int i = 0; i < 10; ++i) {
                clip->setText(v);
                QCoreApplication::processEvents();
                if (clip->text() == v)
                    return;
                QThread::msleep(50);
            }
        };
        const auto clipRead = [clip]() {
            for (int i = 0; i < 10; ++i) {
                const QString v = clip->text();
                if (!v.isEmpty())
                    return v;
                QCoreApplication::processEvents();
                QThread::msleep(50);
            }
            return clip->text();
        };
        const auto send = [pw](QEvent::Type type, const QPoint& imgPos, Qt::MouseButton btn,
                               Qt::KeyboardModifiers mod = Qt::NoModifier, int key = 0) {
            const QPoint wp = pw->toWidgetRect(QRect(imgPos, QSize(1, 1))).topLeft();
            const QPoint gp = pw->mapToGlobal(wp);
            if (key) {
                QEvent* ev = type == QEvent::KeyPress
                                 ? static_cast<QEvent*>(new QKeyEvent(QEvent::KeyPress, key, mod))
                                 : static_cast<QEvent*>(new QKeyEvent(QEvent::KeyRelease, key, mod));
                QApplication::sendEvent(pw, ev);
                delete ev;
                return;
            }
            QEvent* ev = type == QEvent::MouseButtonDblClick
                             ? static_cast<QEvent*>(new QMouseEvent(type, QPointF(wp), QPointF(gp), btn,
                                                                    btn, mod))
                             : type == QEvent::MouseMove
                             ? static_cast<QEvent*>(new QMouseEvent(type, QPointF(wp), QPointF(gp), btn,
                                                                    Qt::LeftButton, mod))
                             : static_cast<QEvent*>(new QMouseEvent(type, QPointF(wp), QPointF(gp), btn,
                                                                    type == QEvent::MouseButtonPress ? btn
                                                                                                    : Qt::NoButton,
                                                                    mod));
            QApplication::sendEvent(pw, ev);
            delete ev;
        };
        const auto drag = [&](const QPoint& from, const QPoint& to) {
            clipWrite(QStringLiteral("SENTINEL"));
            send(QEvent::MouseButtonPress, from, Qt::LeftButton);
            for (int i = 1; i <= 5; ++i)
                send(QEvent::MouseMove, QPoint(from.x() + (to.x() - from.x()) * i / 5,
                                               from.y() + (to.y() - from.y()) * i / 5),
                     Qt::LeftButton);
            send(QEvent::MouseButtonRelease, to, Qt::LeftButton);
            return clipRead();
        };
        int bad = 0;
        const auto expect = [&bad](const char* name, const QString& got, const QString& want) {
            const bool ok = got == want;
            if (!ok)
                ++bad;
            std::printf("%s %s got=[%s] want=[%s]\n", ok ? "PASS" : "FAIL", name,
                        got.toUtf8().constData(), want.toUtf8().constData());
        };
        // 英文词那两个用例按"引擎自己报的词框"定坐标：写死像素的话，一换引擎框的左沿就挪，
        // 测出来的"故障"其实是坐标对不上（PP-OCRv4 给的 Search 框从 43 起，WinRT 从 45 起，
        // 而选中的判定是字的中心要落在拖动带内，1 个像素就能差出半个字母）
        QRect searchBox;
        for (const Ocr::Line& l : Ocr::recognizeLayout(img))
            for (const Ocr::Word& w : l.words)
                if (w.text == QLatin1String("Search"))
                    searchBox = w.rect;
        std::printf("INFO Search 词框=%d,%d %dx%d\n", searchBox.left(), searchBox.top(),
                    searchBox.width(), searchBox.height());
        expect("拖半行只选带内的字", drag(QPoint(60, 35), QPoint(140, 35)),
               QString::fromUtf8("件安装"));
        expect("单击选整词", drag(QPoint(143, 35), QPoint(144, 36)), QString::fromUtf8("路"));
        expect("罩住两行=两行全文", drag(QPoint(0, 20), QPoint(200, 100)),
               QString::fromUtf8("软件安装路径\n默认搜索引擎"));
        expect("单击英文词=整词", drag(searchBox.center(), searchBox.center() + QPoint(1, 1)),
               QString::fromUtf8("Search"));
        // 词内拆到字：从词框左沿起，拖到六分之五处停手，就该只要前 5 个字母（这是这次选的那档粒度）
        expect("词内只拖到一半=半个词",
               drag(QPoint(searchBox.left() + 1, searchBox.center().y()),
                    QPoint(searchBox.left() + searchBox.width() * 5 / 6, searchBox.center().y())),
               QString::fromUtf8("Searc"));
        expect("一行里跨词要补空格", drag(QPoint(45, 130), QPoint(240, 140)),
               QString::fromUtf8("Search Engine 123"));
        expect("整张图都能框到", drag(QPoint(0, 0), QPoint(639, 179)),
               QString::fromUtf8("软件安装路径\n默认搜索引擎\nSearch Engine 123"));
        // Enter = 复制全部
        clipWrite(QStringLiteral("SENTINEL"));
        send(QEvent::KeyPress, QPoint(), Qt::NoButton, Qt::NoModifier, Qt::Key_Return);
        send(QEvent::KeyRelease, QPoint(), Qt::NoButton, Qt::NoModifier, Qt::Key_Return);
        expect("Enter 复制全部", clipRead(),
               QString::fromUtf8("软件安装路径\n默认搜索引擎\nSearch Engine 123"));
        // Esc 只退出划词：退出后再拖一下，不该再往剪贴板写东西
        send(QEvent::KeyPress, QPoint(), Qt::NoButton, Qt::NoModifier, Qt::Key_Escape);
        send(QEvent::KeyRelease, QPoint(), Qt::NoButton, Qt::NoModifier, Qt::Key_Escape);
        clipWrite(QStringLiteral("SENTINEL"));
        const QString afterEsc = drag(QPoint(60, 35), QPoint(140, 35));
        expect("Esc 之后不再复制", afterEsc, QStringLiteral("SENTINEL"));
        std::printf("pinpick: 失败 %d 项\n", bad);
        std::fflush(stdout);
        QGuiApplication::clipboard()->setMimeData(savedClip);   // 归还用户的剪贴板
        delete pw;
        return bad == 0 ? 0 : 1;
    }

    // --ocrchain <图片>：从"一个文件都没下"开始，验 下载 → 就绪 → 起引擎 → 认字 这条整链。
    // APPDATA 已经挪到 %TEMP%\ltool-ocrchain（见 main() 开头），所以这条会真下 26MB、真往沙箱里写，
    // 碰不到用户 %APPDATA%\ltool\ocr 那一份。它跑一次半分钟量级，是这套里最全的一条回归。
    if (args.contains("--ocrchain")) {
        const QString target = args.mid(args.indexOf("--ocrchain") + 1).value(0);
        const QImage img(target);
        int bad = 0;
        const auto check = [&bad](const char* name, const QString& got, const QString& want) {
            const bool ok = got == want;
            if (!ok)
                ++bad;
            std::printf("%s %s got=[%s] want=[%s]\n", ok ? "PASS" : "FAIL", name,
                        got.toUtf8().constData(), want.toUtf8().constData());
        };
        check("沙箱是干净的（引擎不在）", OcrBundle::status().present ? "yes" : "no", "no");
        QString why;
        check("available 说的是要下载",
              (!Ocr::available(&why) && why.contains(QStringLiteral("下载"))) ? "yes" : "no", "yes");
        QString lerr;
        check("没引擎时 OcrPp 不硬跑", OcrPp::load(&lerr) ? "yes" : "no", "no");

        QObject ctx;
        QVector<int> marks;
        bool done = false, okFetch = false, secondDone = false, secondOk = true;
        QString ferr, secondErr;
        OcrBundle::fetchLater(&ctx, [&marks](int pct, const QString&) { marks.push_back(pct); },
                              [&](bool ok, const QString& err, const QString&) {
                                  done = true;
                                  okFetch = ok;
                                  ferr = err;
                              });
        OcrBundle::fetchLater(&ctx, nullptr, [&](bool ok, const QString& err, const QString&) {
            secondDone = true;
            secondOk = ok;
            secondErr = err;
        });
        QElapsedTimer wall;
        wall.start();
        while (!done && wall.elapsed() < 300000) {
            QCoreApplication::processEvents();
            QThread::msleep(20);
        }
        check("下载中再要一次不会重开一个",
              (secondDone && !secondOk && secondErr.contains(QStringLiteral("正在下载"))) ? "yes" : "no",
              "yes");
        check("下载跑完了", done ? "yes" : "no", "yes");
        check("下载成功", okFetch ? "yes" : "no", "yes");
        std::printf("INFO 进度报了 %d 次，err=%s，用了 %llds\n", int(marks.size()),
                    ferr.toUtf8().constData(), long long(wall.elapsed() / 1000));
        int regress = 0;
        for (int i = 1; i < marks.size(); ++i) {
            if (marks[i] < marks[i - 1])
                ++regress;
        }
        check("进度只朝前（回退会被当成坏了）", regress == 0 ? "yes" : "no", "yes");
        check("进度最后到 100%",
              marks.isEmpty() ? QStringLiteral("none") : QString::number(marks.last()),
              QStringLiteral("100"));
        check("下完的文件齐了", OcrBundle::status().present ? "yes" : "no", "yes");
        QString rerr;
        const QString text = Ocr::recognize(img, &rerr);
        std::printf("INFO 认出来：[%s] err=%s\n", text.toUtf8().constData(),
                    rerr.toUtf8().constData());
        check("下完直接就能认字", text,
              QString::fromUtf8("软件安装路径\n默认搜索引擎\nSearch Engine 123"));
        std::printf("ocrchain: 失败 %d 项\n", bad);
        std::fflush(stdout);
        return bad == 0 ? 0 : 1;
    }

    // --memloop [轮数] [宽x高]：内存这事只能量，不能猜。每轮做一遍"解压一个压不动的 32MB 包"
    // + "录一段指定尺寸的窗口"，每一步采 私有字节/工作集/句柄数。
    // 私有字节随轮数线性涨 = 真泄漏；涨一两轮就封顶 = 分配器留着页不还操作系统（那不是泄漏）。
    // 夹具和产物都在 %TEMP%\ltool-memloop 里，跑完删掉。
    // 2026-10-15：录屏本体进了 plugins/record，这一条量的是"录一整场泄多少"，判据跟着
    // Recorder 走（导出符号 ltoolMemloopRun），这儿只剩读命令行 + 点名要符号。
    if (args.contains("--memloop")) {
        const QString rArg = args.mid(args.indexOf("--memloop") + 1).value(0);
        const int rounds = rArg.isEmpty() ? 3 : qBound(1, rArg.toInt(), 12);
        const QString sizeSpec = args.mid(args.indexOf("--memloop") + 2).value(0);
        return runPluginSelftest(QStringLiteral("record"), "ltoolMemloopRun",
                                 SelftestArg::MsAndText, rounds, sizeSpec, "memloop");
    }

    // --pathcfg：验配置页那两行"存放目录"真的管用。APPDATA 已挪到 %TEMP%\ltool-pathcfg
    //（见 main() 开头），所以这一步会写 config.ini、会建目录，但碰不到你真实的文件。
    if (args.contains("--pathcfg")) {
        int bad = 0;
        const auto check = [&bad](const char* name, const QString& got, const QString& want) {
            const bool ok = got == want;
            if (!ok)
                ++bad;
            std::printf("%s %s got=[%s] want=[%s]\n", ok ? "PASS" : "FAIL", name,
                        got.toUtf8().constData(), want.toUtf8().constData());
        };
        const QString defCap = QCoreApplication::applicationDirPath() + "/captures";
        check("没配置时截图存到软件目录下（不是 C 盘）", Utils::capturesDir() == defCap ? "yes" : "no", "yes");
        check("没配置时录像存到软件目录下",
              Utils::recordingsDir() == QCoreApplication::applicationDirPath() + "/recordings" ? "yes" : "no",
              "yes");

        const QString newCap = QDir::tempPath() + "/ltool-pathcfg/新截图处";
        const QString newRec = QDir::tempPath() + "/ltool-pathcfg/新录像处";
        {
            QSettings cfg(Utils::dataDir() + "/config.ini", QSettings::IniFormat);
            cfg.setValue("paths/captures", QDir::toNativeSeparators(newCap));
            cfg.setValue("paths/recordings", QDir::toNativeSeparators(newRec));
            cfg.sync();   // 产品里就是靠这一步让后面新开的 QSettings 读到
        }
        check("改了配置：截图目录跟着变", Utils::capturesDir() == newCap ? "yes" : "no", "yes");
        check("改了配置：录像目录跟着变", Utils::recordingsDir() == newRec ? "yes" : "no", "yes");

        // 截一张纯色图入往期：文件必须落在新目录，而且老目录不该被建出来
        QPixmap pm(24, 24);
        pm.fill(QColor(200, 30, 30));
        const QString key = CaptureHistory::add(pm);
        std::printf("INFO 往期的 key=%s\n", key.toUtf8().constData());
        check("新截图真的写进新目录", QFile::exists(newCap + "/" + key) ? "yes" : "no", "yes");
        check("光问一句不该把软件目录下的 captures 建出来（构建目录会被安装包收走）",
              QFileInfo::exists(defCap) ? "yes" : "no", "no");

        CaptureHistory::reload();
        check("重载往期：还是那一张", CaptureHistory::count() == 1 ? "yes" : "no", "yes");
        check("重载往期：key 对得上", CaptureHistory::at(0).key == key ? "yes" : "no", "yes");

        QDir().mkpath(Utils::recordingsDir());
        QFile rf(Utils::recordingsDir() + "/probe.bin");
        const bool canWrite = rf.open(QIODevice::WriteOnly)
            && rf.write(QByteArray(8, '\0')) == 8;
        rf.close();
        QFile::remove(rf.fileName());
        check("录像目录能建能写", canWrite ? "yes" : "no", "yes");

        QFile::remove(newCap + "/" + key);
        QDir(QDir::tempPath() + "/ltool-pathcfg").removeRecursively();
        std::printf("pathcfg: 失败 %d 项\n", bad);
        std::fflush(stdout);
        return bad == 0 ? 0 : 1;
    }

    // --pintool：离屏验置顶窗那两个"收起"开关（不显示窗口、不在你桌面上闪）。
    // 核心断言只有一件事：收起工具栏 / 收起往期带之后，**图区的尺寸必须一模一样**，
    // 变的是窗口尺寸；反过来放回去要回到原值。APPDATA 挪到临时目录（会往 captures 写一张）。
    if (args.contains("--pintool")) {
        int bad = 0;
        const auto check = [&bad](const char* name, const QString& got, const QString& want) {
            const bool ok = got == want;
            if (!ok)
                ++bad;
            std::printf("%s %s got=[%s] want=[%s]\n", ok ? "PASS" : "FAIL", name,
                        got.toUtf8().constData(), want.toUtf8().constData());
        };
        QPixmap pm(320, 200);
        pm.fill(QColor(90, 140, 200));
        const QString key = CaptureHistory::add(pm);
        check("先造一张往期", key.isEmpty() ? "empty" : "ok", "ok");
        const auto rectStr = [](const QRect& r) {
            return QStringLiteral("%1,%2 %3x%4").arg(r.x()).arg(r.y()).arg(r.width()).arg(r.height());
        };
        const auto sizeStr = [](const QSize& s) { return QStringLiteral("%1x%2").arg(s.width()).arg(s.height()); };
        PinWindow pw(pm, key, false);
        // 默认就是"两样都收起"：刚截完看到的应该是一张干净的图。
        check("默认两样都收起", (pw.barHidden() && pw.stripHidden()) ? "yes" : "no", "yes");
        check("默认收起时点左上角不命中缩略图", QString::number(pw.probeThumbAt(QPoint(10, 10))), "-1");
        const QSize hidden0 = pw.size();
        const QString shots = QDir::tempPath() + "/ltool-pintool";
        QDir().mkpath(shots);
        pw.grab().save(shots + "/c_default.png");   // 用户每次截完图看到的就是这张
        // 从默认状态把两样放出来。以前默认就是展开，这条路径不存在；现在它是用户第一次
        // 右键「显示工具栏」走的那条 —— "展开时该多大"必须在建窗时就记好，不然回不去。
        pw.setBarHidden(false);
        pw.setStripHidden(false);
        const QRect r0 = pw.probeImageRect();
        const QSize s0 = pw.size();
        const int strip0 = pw.stripW();      // 用实例问出来的宽度，不去碰私有常量
        const int bar0 = pw.barH();
        std::printf("INFO 初始：窗口 %s，图区 %s，带宽 %d，栏高 %d\n",
                    sizeStr(s0).toUtf8().constData(), rectStr(r0).toUtf8().constData(), strip0, bar0);
        check("从默认收起放出两样：图区就是那张图", sizeStr(r0.size()), QStringLiteral("320x200"));
        check("从默认收起放出两样：高出一个栏高",
              QString::number(s0.height() - hidden0.height()), QString::number(bar0));
        check("现在两样都开着", (pw.barHidden() || pw.stripHidden()) ? "yes" : "no", "no");

        pw.setBarHidden(true);
        check("收起工具栏：窗口矮了一个栏高", QString::number(pw.size().height()),
              QString::number(s0.height() - bar0));
        check("收起工具栏：图区尺寸没变", sizeStr(pw.probeImageRect().size()), sizeStr(r0.size()));
        check("收起工具栏：窗口抱住图，不留左右黑边", QString::number(pw.size().width()),
              QString::number(strip0 + r0.width()));
        pw.setBarHidden(false);
        check("放出工具栏：窗口回到原尺寸", sizeStr(pw.size()), sizeStr(s0));
        check("放出工具栏：图区还是那个图区", rectStr(pw.probeImageRect()), rectStr(r0));

        check("收起之前点左上角会命中第 0 张", QString::number(pw.probeThumbAt(QPoint(10, 10))), "0");
        pw.setStripHidden(true);
        // 注意这里不能要求"正好窄 76"：窗口的最小宽度是工具栏那一排按钮撑出来的
        //（这张 320px 宽的图，窗口最小就要 612），收起往期带时缩不到最小值以下。
        // 正确的不变量是：不宽于原来、不低于最小值、图区尺寸一点不动。
        check("收起往期带：窗口不宽于原来", pw.size().width() <= s0.width() ? "yes" : "no", "yes");
        check("收起往期带：不低于最小宽度",
              pw.size().width() >= pw.minimumWidth() ? "yes" : "no", "yes");
        check("收起往期带：图区尺寸没变", sizeStr(pw.probeImageRect().size()), sizeStr(r0.size()));
        check("收起往期带：点左上角不再命中缩略图", QString::number(pw.probeThumbAt(QPoint(10, 10))),
              "-1");
        pw.setStripHidden(false);
        check("放出往期带：窗口回到原尺寸", sizeStr(pw.size()), sizeStr(s0));
        check("放出往期带：又命中第 0 张", QString::number(pw.probeThumbAt(QPoint(10, 10))), "0");
        // 离屏抓两张图自己看一眼（不显示窗口、不在你桌面上闪）：一张正常、一张两样都收起
        pw.grab().save(shots + "/a_normal.png");
        pw.setBarHidden(true);
        pw.setStripHidden(true);
        check("两样都收起：窗口正好等于图本身", sizeStr(pw.size()), sizeStr(r0.size()));
        pw.grab().save(shots + "/b_hidden.png");
        // 最容易被写坏的一步：两个开关叠着用之后再依次放开。
        // 收起工具栏时"记住的原尺寸"是在往期带还开着的时候记的，中间往期带又收过一次，
        // 放开时必须还是回到最初那个尺寸 —— 不然来回点几下窗口就越走越歪（这一条抓到过真 bug）。
        pw.setStripHidden(false);
        pw.setBarHidden(false);
        check("叠用之后再放开：窗口回到原尺寸", sizeStr(pw.size()), sizeStr(s0));
        check("叠用之后再放开：图区还是那个图区", rectStr(pw.probeImageRect()), rectStr(r0));
        check("叠用之后再放开：又命中第 0 张", QString::number(pw.probeThumbAt(QPoint(10, 10))), "0");
        check("叠用之后再放开：两个开关都回到开", (pw.barHidden() || pw.stripHidden()) ? "yes" : "no", "no");
        std::printf("INFO 离屏抓图：%s/a_normal.png 与 b_hidden.png\n", shots.toUtf8().constData());
        QFile::remove(Utils::capturesDir() + "/" + key);
        std::printf("pintool: 失败 %d 项\n", bad);
        std::fflush(stdout);
        return bad == 0 ? 0 : 1;
    }

    // --pindel <图片>：无界面验"右键缩略图删除"。这张图只用来给置顶窗当初始内容，
    // 往期是现场造的三张纯色图，删的是临时目录里的文件，碰不到用户真实的 captures。
    if (args.contains("--pindel")) {
        const QImage img(args.mid(args.indexOf("--pindel") + 1).value(0));
        if (img.isNull()) {
            std::printf("pindel: 读不了这张图\n");
            std::fflush(stdout);
            return 2;
        }
        int bad = 0;
        const auto check = [&bad](const char* name, const QString& got, const QString& want) {
            const bool ok = got == want;
            if (!ok)
                ++bad;
            std::printf("%s %s got=[%s] want=[%s]\n", ok ? "PASS" : "FAIL", name,
                        got.toUtf8().constData(), want.toUtf8().constData());
        };
        QPixmap old3(100, 100); old3.fill(QColor(220, 30, 30));
        QPixmap old2(100, 100); old2.fill(QColor(30, 200, 30));
        QPixmap old1(100, 100); old1.fill(QColor(30, 60, 220));
        const QString k3 = CaptureHistory::add(old3);   // add 是 prepend：列表顺序 = old1, old2, old3
        const QString k2 = CaptureHistory::add(old2);
        const QString k1 = CaptureHistory::add(old1);
        const QString capDir = Utils::capturesDir();
        auto* pw = new PinWindow(old1, k1, false);
        check("起点：往期 3 张", QString::number(CaptureHistory::count()), "3");
        // 删中间那张（不是当前显示的）：当前不动，文件要没了
        check("删中间那张后当前还是原图", pw->deleteThumb(1), k1);
        check("往期剩 2 张", QString::number(CaptureHistory::count()), "2");
        check("被删的文件落盘也没了", QFile::exists(capDir + "/" + k2) ? "yes" : "no", "no");
        check("剩下的第 2 张是那张旧的", CaptureHistory::at(1).key, k3);
        // 删当前这张：应该自动挪到邻居，图上画的也该换了
        check("删当前那张=挪到邻居", pw->deleteThumb(0), k3);
        {
            const QImage shot = pw->grab().toImage();
            int r = 0, g = 0, b = 0;
            for (int y = 0; y < shot.height(); ++y)
                for (int x = 76; x < shot.width(); ++x) {
                    const QColor c = shot.pixelColor(x, y);
                    if (c.red() > 150 && c.green() < 100) ++r;
                    else if (c.green() > 150 && c.red() < 100) ++g;
                    else if (c.blue() > 150 && c.red() < 100) ++b;
                }
            // 剩下的那张是 old3（红）：窗口里画的得是它，不能还是被删掉的蓝
            check("窗口里画的是剩下那张(红)", QString::number(r > g && r > b ? 1 : 0), "1");
        }
        // 删最后一张：往期空了，图还显示着，但 key 已经没有去处了
        check("删掉最后一张后 key 空", pw->deleteThumb(0), QString());
        check("往期 0 张", QString::number(CaptureHistory::count()), "0");
        check("越界删除不炸", pw->deleteThumb(7), QString());
        std::printf("pindel: 失败 %d 项\n", bad);
        std::fflush(stdout);
        delete pw;
        return bad == 0 ? 0 : 1;
    }

    // --ocrget：下载 OCR 引擎（PaddleOCR PP-OCRv4 + ONNX Runtime，27.9MB）并校验/解包。
    // 有界面入口走的也是这套代码，所以这条跑通就说明"新机器没装中文语言包也能用"这件事成立。
    if (args.contains("--ocrget")) {
        std::printf("目标：%s\n", OcrBundle::describe().toUtf8().constData());
        std::fflush(stdout);
        QString err;
        const bool ok = OcrBundle::fetchSync(&err,
            [](int pct, const QString& s) { std::printf("  %3d%% %s\n", pct, s.toUtf8().constData()); std::fflush(stdout); });
        const OcrBundle::Files f = OcrBundle::status();
        std::printf("ocrget: ok=%d err=%s\n  det=%lld KB rec=%lld KB ort=%lld KB\n", ok ? 1 : 0,
                    err.toUtf8().constData(), QFileInfo(f.det).size() / 1024,
                    QFileInfo(f.rec).size() / 1024, QFileInfo(f.onnxruntime).size() / 1024);
        std::fflush(stdout);
        return ok ? 0 : 1;
    }

    // --recprobe [观察毫秒]：录屏之前先量机器（有没有 H.264/AAC 编码器、MP4 写得出吗、
    // Win11 的按窗口取帧能不能用、一帧拷到 CPU 要多久）。会弹一个自己的小窗动 1 秒多，
    // 不碰用户其它窗口。RecProbe.cpp 现在也在 plugins/record 里编，符号叫 ltoolRecprobeRun。
    if (args.contains("--recprobe")) {
        const QString arg = args.mid(args.indexOf("--recprobe") + 1).value(0);
        const int observeMs = arg.isEmpty() ? 1500 : arg.toInt();
        return runPluginSelftest(QStringLiteral("record"), "ltoolRecprobeRun",
                                 SelftestArg::Ms, observeMs, QString(), "recprobe");
    }


    // --recq：录屏那两个新选择（画面大小 / 画质）到底进没进编码器。
    // 全部拿真编码器量：录自制卡 → 把 mp4 解回来说话。目标窗口是自己造的、
    // 摆在屏幕右下角、不抢焦点，不碰用户桌面上任何东西。判据本体在 plugins/record。
    if (args.contains("--recq"))
        return runPluginSelftest(QStringLiteral("record"), "ltoolRecqSelftest",
                                 SelftestArg::None, 0, QString(), "recq");

    // --rectest [秒]：验一遍"只录某个窗口 → mp4 → 再读回来数帧"，外加第二路"录整块屏幕"。
    // 目标窗口是自己造的一张每 30ms 换一个数字的卡片，不碰用户桌面上任何东西；
    // 判据不是"文件存在"，而是拿 SourceReader 把文件解回来数出 N 帧、且 N 和采集到的帧数一致。
    // 本体在 plugins/record/recordplugin.cpp（Recorder 就在那儿编，测试跟着实现走）。
    // 可选的「宽x高」用来钉"奇数尺寸被 H.264 拒"那一条 —— 导出符号收 (int, const char*)，
    // 解析在插件里那一个 cardSize()（--memloop 的画幅参数走同一条）。
    if (args.contains("--rectest")) {
        const QString a = args.mid(args.indexOf("--rectest") + 1).value(0);
        const int secs = a.isEmpty() ? 4 : qBound(2, a.toInt(), 30);
        const QString sizeSpec = args.mid(args.indexOf("--rectest") + 2).value(0);
        return runPluginSelftest(QStringLiteral("record"), "ltoolRectestRun",
                                 SelftestArg::MsAndText, secs, sizeSpec, "rectest");
    }

    // --recordui [秒]：验 F2 那套界面的链条（选窗口的列表有没有内容、悬浮条会不会计数、
    // 点了停止之后文件能不能解回帧）。按钮是用 click() 程序化按的，不合成鼠标，不抢焦点。
    // 2026-10-15：判据跟着 RecordUi 一起搬进 plugins/record（那边导出的符号就叫
    // ltoolRecorduiRun），主程序只剩"读命令行 + 点名要符号"这一步。
    if (args.contains("--recordui")) {
        const QString a = args.mid(args.indexOf("--recordui") + 1).value(0);
        const int secs = a.isEmpty() ? 3 : qBound(2, a.toInt(), 20);
        return runPluginSelftest(QStringLiteral("record"), "ltoolRecorduiRun",
                                 SelftestArg::Ms, secs, QString(), "recordui");
    }

    // --ocrpanel <图片>：把"提取文字"结果窗真开一次（不经过截图遮罩），用来验识别→回填这条链
    if (args.contains("--ocrpanel")) {
        const QString target = args.mid(args.indexOf("--ocrpanel") + 1).value(0);
        QImage img(target);
        if (img.isNull()) {
            std::printf("ocrpanel: 读不了这张图 %s\n", target.toUtf8().constData());
            std::fflush(stdout);
            return 2;
        }
        OcrPanel::extractFrom(img);
        auto* tick = new QTimer(&app);
        tick->setInterval(300);
        QObject::connect(tick, &QTimer::timeout, &app, [&app] {
            if (app.topLevelWidgets().isEmpty())
                app.quit();
        });
        tick->start();
        const int rc = app.exec();
        std::printf("ocrpanel: 窗口已关闭\n");
        std::fflush(stdout);
        return rc;
    }

    // --pinshot <图片>：用这张图开一个置顶截图窗（验标注对象的选中/拖动/缩放/删除）
    if (args.contains("--pinshot")) {
        const QString target = args.mid(args.indexOf("--pinshot") + 1).value(0);
        const QPixmap pm(target);
        if (pm.isNull()) {
            std::printf("pinshot: 读不了这张图 %s\n", target.toUtf8().constData());
            std::fflush(stdout);
            return 2;
        }
        auto* w = new PinWindow(pm, CaptureHistory::add(pm));
        QObject::connect(w, &QObject::destroyed, &app, [&app] {
            if (app.topLevelWidgets().isEmpty())
                app.quit();
        });
        const int rc = app.exec();
        std::printf("pinshot: 窗口已关闭\n");
        std::fflush(stdout);
        return rc;
    }

    // --shredtest <路径> yes：不弹界面直接跑一遍粉碎，用来验证覆写真的发生。
    // 必须带 "yes"，否则只报告目标信息不动手。
    if (args.contains("--shredtest")) {
        const QStringList rest = args.mid(args.indexOf("--shredtest") + 1);
        const QString target = rest.value(0);
        if (rest.value(1) != QLatin1String("yes")) {
            std::printf("shredtest: 没带 yes，只报告不动手 target=%s exists=%d\n",
                        target.toUtf8().constData(), QFileInfo(target).exists() ? 1 : 0);
            std::fflush(stdout);
            return 0;
        }
        QString err;
        const qint64 erased = FileOps::shred(target, rest.value(2) == QLatin1String("deep"), &err);
        std::printf("shredtest: erased=%lld exists_after=%d err=%s\n", erased,
                    QFileInfo(target).exists() ? 1 : 0, err.toUtf8().constData());
        std::fflush(stdout);
        return erased < 0 ? 1 : 0;
    }

    // 资源管理器右键菜单进来的动作：这是另起的一个进程，得先于单实例守卫处理，
    // 否则它只会把主窗口叫醒然后自己退出。
    //
    // 2026-10-15：窗口本体搬进 plugins/shell 了（用户拍的板：入口留在主程序，按下之后转发）。
    // 这儿只剩"旗标 → 动作名"这一手，主程序不再链接占用分析/粉碎/解压/压缩那些界面。
    // **转不出去必须把话讲明白并给非 0 退出码** —— 右键点一下什么都不发生是最难查的形状。
    for (const char* f : kShellFlags) {
        if (!args.contains(QLatin1String(f)))
            continue;
        const QString path = args.mid(args.indexOf(QLatin1String(f)) + 1).value(0);
        const QString action = QString::fromLatin1(f).mid(2);      // 去掉那两个短横
        QString who;
        if (PluginHost::invoke(action, { path }, &who))
            return 0;
        const QString why = who.isEmpty()
            ? QStringLiteral("没装接这个动作的插件（ltool-plugins 里是不是缺 ltool-shell.dll）")
            : QStringLiteral("%1 接了但没干成").arg(who);
        std::printf("FAIL 右键动作 %s：%s 路径=%s\n", action.toUtf8().constData(),
                    why.toUtf8().constData(), path.toUtf8().constData());
        std::fflush(stdout);
        Utils::log(QStringLiteral("shellmenu: %1 没人接：%2").arg(action, why));
        QMessageBox::warning(nullptr, QStringLiteral("ltool"),
                             QStringLiteral("这个右键动作做不了：%1").arg(why));
        return 1;
    }

    // 单实例：第二个实例唤醒已运行的窗口后退出
    //
    // 但有两条判据必须**在事件循环里**跑（要等真屏幕抓一张图），派发点天生在这道守卫之后。
    // 守卫先把它们转交给已运行的实例、当场 return 0，于是自测变成"0 字节输出 + 退出码 0"
    // —— 看着像通过，其实一行都没跑（2026-10-14 撞见 --smoke 也是这个形状，已提到守卫之前）。
    // 所以带这两面旗时必须自己起循环。
    const bool inLoopSelftest = args.contains(QLatin1String("--capgeo"))
                                || args.contains(QLatin1String("--capflash"));
    if (!inLoopSelftest) {
        QLocalSocket probe;
        probe.connectToServer(QString::fromWCharArray(kInstanceKey));
        if (probe.waitForConnected(300)) {
            probe.write("show\n");
            probe.waitForBytesWritten(300);
            probe.disconnectFromServer();
            return 0;
        }
    }

    SearchWindow win;

    QLocalServer::removeServer(QString::fromWCharArray(kInstanceKey));
    QLocalServer server;
    server.listen(QString::fromWCharArray(kInstanceKey));
    QSystemTrayIcon* trayPtr = nullptr; // 稍后指向托盘，供热键提示和"递过来的气泡"使用
    QObject::connect(&server, &QLocalServer::newConnection, [&] {
        while (server.hasPendingConnections()) {
            QLocalSocket* c = server.nextPendingConnection();
            c->waitForReadyRead(100);
            const QByteArray msg = c->readAll();
            c->disconnectFromServer();
            c->deleteLater();
            // 右键解压/压缩那个一次性进程把气泡递过来（"note\t标题\t正文"），
            // 它自己不该为了举着气泡多活 5 秒。其余任何消息都还是"把主窗口叫出来"。
            const QList<QByteArray> parts = msg.split('\t');
            if (parts.value(0) == "note" && parts.size() >= 3) {
                if (trayPtr)
                    trayPtr->showMessage(QString::fromUtf8(parts.value(1).trimmed()),
                                         QString::fromUtf8(parts.value(2).trimmed()),
                                         QSystemTrayIcon::Information, 5000);
                else
                    Utils::log("note 到了但托盘还没建好，这条提示丢了");
                continue;
            }
            win.showLauncher();
        }
    });

    HotkeyFilter filter([&](WPARAM id) {
        if (id == kCaptureId)
            CaptureOverlay::start();
        else if (id == kRecordId) {
            // F2 录屏：本体在 plugins/record（2026-10-15 搬过去的），这层壳只把动作名递过去。
            // **没人接的时候绝不弹模态框** —— 那会盖在用户正做的事情上面（2026-10-15 右键那一路
            // 就是这么被他撞上的）。说实话分三层：日志（事后能查）、托盘气泡（当下能看见）、
            // stderr（从命令行起着的时候能看见）。谁 空 = 根本没插件声明接这个动作。
            QString who;
            if (!PluginHost::invoke(QStringLiteral("record"), {}, &who)) {
                const QString why = who.isEmpty()
                    ? QStringLiteral("没装接 record 这个动作的插件（ltool-plugins 里是不是缺 ltool-record.dll）")
                    : QStringLiteral("%1 接了但没干成").arg(who);
                Utils::log(QStringLiteral("F2 没人接：%1").arg(why));
                std::fprintf(stderr, "F2 record: %s\n", why.toUtf8().constData());
                std::fflush(stderr);
                if (trayPtr)
                    trayPtr->showMessage(QStringLiteral("F2 录屏"), why,
                                         QSystemTrayIcon::Warning, 5000);
            }
        } else
            win.toggleVisible();
    });
    app.installNativeEventFilter(&filter);

    // 热键：优先读配置（可在设置页修改），被占用时降级到备选组合，
    // 之后每 30 秒自动尝试"夺回"首选热键（占用者退出后无需重启）
    UINT curMods = 0, curVk = 0;
    bool hotkeyOk = false;
    QString hotkeyLabel;

    const auto unregisterCurrent = [&] {
        UnregisterHotKey(nullptr, (int)kHotkeyId);
    };
    const auto tryRegister = [&](UINT mods, UINT vk) {
        return RegisterHotKey(nullptr, (int)kHotkeyId, mods | MOD_NOREPEAT, vk);
    };

    QSettings cfg(Utils::dataDir() + "/config.ini", QSettings::IniFormat);
    const QString preferred = cfg.value("general/hotkey", "Alt+Space").toString();
    UINT prefMods = MOD_ALT, prefVk = VK_SPACE;
    keySeqToHotkey(QKeySequence(preferred), &prefMods, &prefVk);

    // 降级链：不用 Win+Space（与系统输入法切换冲突）
    struct HotkeyCombo { UINT mods; const char* label; };
    static const HotkeyCombo combos[] = {
        {MOD_CONTROL | MOD_ALT, "Ctrl+Alt+Space"},
        {MOD_CONTROL | MOD_SHIFT, "Ctrl+Shift+Space"},
    };

    UINT m2 = 0, v2 = 0;
    if (keySeqToHotkey(QKeySequence(preferred), &m2, &v2)
        && tryRegister(m2, v2)) {
        hotkeyOk = true;
        hotkeyLabel = preferred;
        curMods = m2;
        curVk = v2;
    } else {
        Utils::log(QString("RegisterHotKey failed: %1 (err %2)")
                       .arg(preferred).arg(GetLastError()));
        for (const HotkeyCombo& c : combos) {
            if (tryRegister(c.mods, VK_SPACE)) {
                hotkeyOk = true;
                hotkeyLabel = QString::fromUtf8(c.label);
                curMods = c.mods;
                curVk = VK_SPACE;
                break;
            }
            Utils::log(QString("RegisterHotKey failed: %1 (err %2)")
                           .arg(c.label).arg(GetLastError()));
        }
    }
    Utils::log(QString("startup: hotkey=%1 pid=%2 exe=%3")
                   .arg(hotkeyOk ? hotkeyLabel : "FAILED")
                   .arg(GetCurrentProcessId())
                   .arg(QCoreApplication::applicationFilePath()));
    win.setHotkeyLabel(hotkeyOk ? hotkeyLabel : QString());

    // 右键菜单是静态注册表项：表里新加了项（比如"压缩为 .lzx"），已经集成过的机器
    // 不会自己长出来，启动时补写一次
    ShellMenu::refreshIfStale();

    // 托盘气泡提示当前实际热键
    std::function<void()> updateMenuHotkey = [] {}; // 托盘创建后赋值
    const auto notifyHotkey = [&](const QString& label) {
        if (trayPtr)
            trayPtr->showMessage("ltool",
                                 QString("当前呼出热键：%1\n（可在设置中修改）").arg(label),
                                 QSystemTrayIcon::Information, 4000);
    };

    // 设置页修改快捷键 → 重新注册
    auto applyHotkeyChange = [&](const QString& seq) {
        UINT nm = 0, nv = 0;
        unregisterCurrent();
        if (!keySeqToHotkey(QKeySequence(seq), &nm, &nv)
            || !tryRegister(nm, nv)) {
            tryRegister(curMods, curVk); // 回退当前热键
            Utils::log(QString("hotkey change to %1 FAILED, keep %2").arg(seq, hotkeyLabel));
            return;
        }
        curMods = nm;
        curVk = nv;
        hotkeyLabel = seq;
        win.setHotkeyLabel(seq);
        Utils::log(QString("hotkey changed to %1").arg(seq));
    };

    // 每 30 秒尝试夺回首选热键（热键不可用窗口约 1ms）
    auto reclaimHotkey = [&]() {
        if (!hotkeyOk || hotkeyLabel == preferred)
            return;
        unregisterCurrent();
        UINT pm = 0, pv = 0;
        if (keySeqToHotkey(QKeySequence(preferred), &pm, &pv) && tryRegister(pm, pv)) {
            curMods = pm;
            curVk = pv;
            hotkeyLabel = preferred;
            win.setHotkeyLabel(preferred);
            Utils::log(QString("hotkey reclaimed: %1").arg(preferred));
            notifyHotkey(preferred);
        } else {
            tryRegister(curMods, curVk); // 重新挂上当前降级组合
        }
    };
    auto* reclaimTimer = new QTimer(&app);
    reclaimTimer->setInterval(30 * 1000);
    QObject::connect(reclaimTimer, &QTimer::timeout, reclaimHotkey);
    reclaimTimer->start();

    // 托盘
    QSystemTrayIcon tray(Utils::appIcon());
    trayPtr = &tray;
    QMenu menu;
    QAction* showAction = menu.addAction(QString("显示搜索框 (%1)").arg(hotkeyLabel));
    QObject::connect(showAction, &QAction::triggered, &win, &SearchWindow::showLauncher);
    updateMenuHotkey = [&] {
        showAction->setText(QString("显示搜索框 (%1)").arg(hotkeyLabel));
    };
    QObject::connect(menu.addAction("设置"), &QAction::triggered, &win, &SearchWindow::openSettings);
    // 「关于」就在设置最底下那一行，托盘直接把它翻到面前（省得他在七行菜单里找）
    QObject::connect(menu.addAction("关于"), &QAction::triggered, [&win] {
        win.openSettingsPage(7);
    });
    QObject::connect(menu.addAction("重新扫描"), &QAction::triggered, &win, &SearchWindow::rescan);
    // 这两项以前是直接 new 那两个工具窗（JsonTool/Base64Tool）。它们搬进 DLL 插件之后，
    // 主程序够不着那两个类了，只能走和搜索框同一条路：弹设置页 → 展开那张卡。
    QObject::connect(menu.addAction("JSON 格式化"), &QAction::triggered,
                     [&win] { win.openTool(QStringLiteral("json")); });
    QObject::connect(menu.addAction("Base64 编解码"), &QAction::triggered,
                     [&win] { win.openTool(QStringLiteral("base64")); });
    QAction* logAction = menu.addAction("安装日志");
    QObject::connect(logAction, &QAction::triggered, [] { SilentInstallLog::showLog(); });
    QAction* autoRun = menu.addAction("开机自启");
    autoRun->setCheckable(true);
    autoRun->setChecked(Utils::autostartEnabled());
    QObject::connect(autoRun, &QAction::toggled, &Utils::setAutostart);
    menu.addSeparator();
    QObject::connect(menu.addAction("退出"), &QAction::triggered, &QApplication::quit);
    tray.setContextMenu(&menu);
    tray.show();
    QObject::connect(&tray, &QSystemTrayIcon::activated, [&](QSystemTrayIcon::ActivationReason r) {
        if (r != QSystemTrayIcon::Trigger && r != QSystemTrayIcon::DoubleClick)
            return;
        // 有安装在跑时，点图标的语义变成"打开看台"：进度不该只藏在右键菜单里
        if (SilentInstallLog::busy())
            SilentInstallLog::showLog();
        else
            win.showLauncher();
    });

    // 后台静默安装不再自动弹日志窗（会抢焦点），改成托盘图标右下角转圈：
    // 转圈 = 有安装在跑，点图标看进度和「取消安装」，装完自动停下换回原图标。
    const QIcon idleIcon = Utils::appIcon();
    const QVector<QIcon> spinFrames = spinnerFrames(idleIcon);
    auto* spinTimer = new QTimer(&app);
    spinTimer->setInterval(120);
    bool trayBusy = false;
    int trayFrame = 0;
    QObject::connect(spinTimer, &QTimer::timeout, [&] {
        const int n = SilentInstallLog::busyCount();
        if (n <= 0) {
            if (!trayBusy)
                return;
            trayBusy = false;
            tray.setIcon(idleIcon);
            tray.setToolTip("ltool");
            logAction->setText("安装日志");
            return;
        }
        trayFrame = (trayFrame + 1) % spinFrames.size();
        tray.setIcon(spinFrames[trayFrame]);
        tray.setToolTip(QString("正在静默安装 %1 项，点击查看安装日志").arg(n));
        const QString text = QString("安装日志（%1 项进行中）").arg(n);
        if (logAction->text() != text)
            logAction->setText(text);
        trayBusy = true;
    });
    spinTimer->start();

    if (!hotkeyOk) {
        tray.showMessage("ltool", "所有热键组合注册失败（可能被其他软件占用）\n"
                                  "可通过托盘图标打开搜索框，详见 %APPDATA%\\ltool\\log.txt",
                         QSystemTrayIcon::Warning, 8000);
        Utils::log("hotkey: all combos failed");
    } else if (hotkeyLabel != preferred) {
        // 启动即降级：明确告知当前实际热键（此后每 30 秒自动尝试夺回首选热键）
        QTimer::singleShot(1500, &win, [&] { notifyHotkey(hotkeyLabel); });
    }

    // F1 截图热键（独立注册，失败仅记录）
    if (!RegisterHotKey(nullptr, (int)kCaptureId, MOD_NOREPEAT, VK_F1))
        Utils::log(QString("F1 capture hotkey failed (err %1)").arg(GetLastError()));
    // F2 录屏（只录选中的那一个窗口）。失败只记日志：F2 被别的程序占了也不该拖住启动
    if (!RegisterHotKey(nullptr, (int)kRecordId, MOD_NOREPEAT, VK_F2))
        Utils::log(QString("F2 record hotkey failed (err %1)").arg(GetLastError()));

    win.startInitialScan(); // 启动即后台扫描（增量优先）
    TaskScheduler::instance().start(); // 定时任务调度（每 30 秒检查）
    // 广告弹窗拦截器：常驻，配置里勾了"自动拦截"就一直在跑 ——
    // 以前它是设置页里的一个 QTimer，页面一销毁就停，等于从来没真正生效过
    AdBlocker::instance().start();
    // 剪贴板历史：默认关闭，勾了才在后台收（见设置 → 配置 → 剪贴板）
    ClipboardStore::instance().start();

    // 以前这里每 10 分钟 EmptyWorkingSet 一次，看起来"内存漂亮"，代价是下一次按热键
    // 要把整个 Qt + 索引的页换回来，实测就是"唤出很卡"。只在截图那种一次性大块内存后回收。
    // 启动 1.2 秒后把启动器窗口预热一次（见 SearchWindow::warmUp）
    QTimer::singleShot(1200, &win, &SearchWindow::warmUp);
    if (showOnStart)
        QTimer::singleShot(200, &win, &SearchWindow::showLauncher);
    if (openSettingsOnStart)
        QTimer::singleShot(300, &win, &SearchWindow::openSettings);

    // --settingspage <菜单行> [tab]：截图核对某个设置页用（也能从搜索框直达）
    if (args.contains("--settingspage")) {
        const QStringList rest = args.mid(args.indexOf("--settingspage") + 1);
        const int row = rest.value(0, "0").toInt();
        const int tab = rest.size() > 1 ? rest.value(1).toInt() : -1;
        const QString emb = rest.size() > 2 ? rest.value(2) : QString();
        QTimer::singleShot(700, &app, [row, tab, emb] {
            for (QWidget* w : QApplication::topLevelWidgets()) {
                if (auto* dlg = qobject_cast<SettingsDialog*>(w)) {
                    dlg->show();
                    dlg->raise();
                    dlg->activateWindow();
                    dlg->openAt(row, tab, emb);
                    return;
                }
            }
        });
    }

    // --toggle：验"按热键到底出不出来"。三种真实情形各按一次：冷状态、被最小化过
    // （Win+D 也算）、窗口还"可见但躲在别的程序后面"。不合成鼠标键盘，只开自己的窗口抢前台。
    // 必须冷启动跑：另有实例在跑时命令行参数会被转发掉，那种情况下这个测试不会执行。
    int toggleBad = 0;
    if (args.contains("--toggle")) {
        const auto check = [&toggleBad](const char* name, bool ok, const QString& got) {
            if (!ok)
                ++toggleBad;
            std::printf("%s %s got=[%s]\n", ok ? "PASS" : "FAIL", name, got.toUtf8().constData());
            std::fflush(stdout);
        };
        QTimer::singleShot(1000, &app, [&win, &app, check, &toggleBad] {
            const auto state = [&win] {
                const HWND h = reinterpret_cast<HWND>(win.winId());
                return QStringLiteral("visible=%1 前台=%2 最小化=%3 位置=%4,%5 %6x%7")
                    .arg(win.isVisible() ? 1 : 0)
                    .arg(GetForegroundWindow() == h ? 1 : 0)
                    .arg((win.windowState() & Qt::WindowMinimized) ? 1 : 0)
                    .arg(win.x()).arg(win.y()).arg(win.width()).arg(win.height());
            };
            const auto pump = [](int times) {
                for (int i = 0; i < times; ++i) {
                    QCoreApplication::processEvents();
                    QThread::msleep(20);
                }
            };
            const auto mine = [&win] { return reinterpret_cast<HWND>(win.winId()); };

            win.toggleVisible();
            pump(10);
            bool onScreen = false;
            const QRect g = win.geometry();
            for (const QScreen* s : QGuiApplication::screens())
                if (s->availableGeometry().intersects(g))
                    onScreen = true;
            check("冷状态第一次按：出来了，而且落在某个显示器内", win.isVisible() && onScreen, state());

            ShowWindow(mine(), SW_MINIMIZE);
            pump(10);
            std::printf("INFO 最小化之后：%s\n", state().toUtf8().constData());
            // 注意这里不断言"前台不是它"：实测最小化之后 GetForegroundWindow() 仍然算它自己
            //（正是 toggleVisible 必须额外看最小化位的原因）
            check("确实造出了\"最小化\"这个状态", (win.windowState() & Qt::WindowMinimized)
                  && win.isVisible(), state());
            win.toggleVisible();
            pump(15);
            std::printf("INFO 再按一次：%s\n", state().toUtf8().constData());
            // 只断言"窗口拉回来了"这一件确定的事。"顺手抢到前台"脚本环境保证不了：
            // Windows 的前台锁看的是最后一次真实输入，而这里没有任何键盘动作
            //（实测 10 次里会有 1 次拉回来了但前台没给我们）。真实按热键时 Alt 就是那次输入，
            // 到底会不会抢不到，看 showLauncher 里那条"置前失败"日志，不在这里猜。
            check("被最小化过再按热键能拉回来", win.isVisible()
                  && !(win.windowState() & Qt::WindowMinimized), state());

            QWidget* other = new QWidget(nullptr, Qt::Window);
            other->resize(240, 120);
            other->setWindowTitle(QStringLiteral("ltool-toggle-other"));
            other->show();
            pump(5);
            SetForegroundWindow(reinterpret_cast<HWND>(other->winId()));
            // 失焦收起有 500ms 宽限（防止刚弹出来就被自己的激活竞争收掉），宽限过后
            // 还有一次 600ms 的复查 —— 所以要等过这两档再看它收没收
            pump(70);
            std::printf("INFO 盖上别的窗口之后：%s\n", state().toUtf8().constData());
            // "可见但躲在别的窗口后面"这个状态实际到不了：这个窗口失焦会自己收起
            //（event() 里 WindowDeactivate 那支，带 500ms 宽限）。所以这里核对的是失焦确实收起了。
            check("失焦之后确实自己收起了（设计如此）", !win.isVisible(), state());
            win.toggleVisible();
            pump(15);
            std::printf("INFO 再按一次：%s\n", state().toUtf8().constData());
            check("收起之后再按：能再呼出（他一天要按几百次这一支）", win.isVisible(), state());

            if (GetForegroundWindow() == mine()) {
                win.toggleVisible();
                pump(5);
                check("它就在前台时再按一次=收起", !win.isVisible(), state());
            } else {
                std::printf("SKIP 在前台时再按=收起（这个环境抢不到前台，测不了这一支）\n");
            }
            delete other;
            std::printf("toggle: 失败 %d 项\n", toggleBad);
            std::fflush(stdout);
            app.quit();
        });
        QTimer::singleShot(30000, &app, [&app, &toggleBad] {
            std::printf("toggle: 30 秒没跑完，退出（失败 %d 项）\n", toggleBad);
            ++toggleBad;
            app.quit();
        });
    }

    // --capgeo：把"缩放不是 100% 时截全屏"这条链路的几何一次量清楚，**不开浮层、不动系统设置**。
    // 造环境靠 QT_SCALE_FACTOR=1.5（Qt 自己的缩放因子，效果就是逻辑/物理两套坐标错开，
    // 和他把 Windows 缩放调到 150% 走的是同一段代码）。量四件事：
    //   ① 抓到的整屏图：物理尺寸 == 屏幕逻辑尺寸 × DPR（截的就是全屏，没少一块）
    //   ② rendered() 那一步的裁剪：在带 DPR 的画布上按**逻辑选区**裁全屏，
    //      出来的物理尺寸必须还是整块屏 —— 这条就是"截不全"的判据
    //   ③ 裁完 DPR 还在（丢了就会被当 100% 显示，等于白截）
    //   ④ 把逻辑点换算成物理点那一步（悬停吸附用的）在多屏偏移下对不对
    // 造不出真缩放（不动他的系统设置），所以两条一起跑：
    //   · 真屏：抓的图物理尺寸 == 屏幕逻辑尺寸 × DPR（配合 QT_SCALE_FACTOR=1.5 起进程用）
    //   · 合成画布：自造一张 DPR=1.5 的"1000x600 逻辑 / 1500x900 物理"的图，直接问产品函数
    //     CaptureOverlay::cropCanvas —— 这条**不依赖他当前缩放是多少**，100% 的机器上也能
    //     把"截不全全屏"这个错抓出来（改之前它就是红的）。
    // 另外探一下 QPainter::drawPixmap 的 sourceRect 按哪种坐标解释 —— 浮层"选区亮起"那一步
    // 用的就是它，答错了一截就是预览和成品对不上。
    int capGeoBad = 0;
    if (args.contains("--capgeo")) {
        QTimer::singleShot(80, &app, [&app, &capGeoBad] {
            const auto check = [&capGeoBad](const char* what, bool ok, const QString& got) {
                std::printf("%s %s：%s\n", ok ? "ok  " : "FAIL", what, got.toUtf8().constData());
                if (!ok)
                    ++capGeoBad;
                std::fflush(stdout);
            };
            QScreen* scr = QGuiApplication::primaryScreen();
            const qreal dpr = scr->devicePixelRatio();
            const QSize log = scr->geometry().size();
            const QPixmap shot = scr->grabWindow(0);
            std::printf("INFO 屏幕逻辑=%dx%d DPR=%.2f 抓取物理=%dx%d(图上 DPR=%.2f)\n",
                        log.width(), log.height(), dpr, shot.width(), shot.height(),
                        shot.devicePixelRatio());
            const int wantW = int(log.width() * dpr + 0.5);
            const int wantH = int(log.height() * dpr + 0.5);
            check("抓的图铺满整块屏（物理=逻辑×DPR）",
                  qAbs(shot.width() - wantW) <= 2 && qAbs(shot.height() - wantH) <= 2,
                  QStringLiteral("期望 %1x%2，拿到 %3x%4").arg(wantW).arg(wantH)
                      .arg(shot.width()).arg(shot.height()));
            const QPixmap full = CaptureOverlay::cropCanvas(shot, QRect(QPoint(0, 0), log));
            check("产品那条裁剪路：逻辑选区裁全屏，出来的还是整块屏",
                  qAbs(full.width() - wantW) <= 2 && qAbs(full.height() - wantH) <= 2,
                  QStringLiteral("选区=%1x%2(逻辑) 裁完=%3x%4(物理)，差 %5x%6")
                      .arg(log.width()).arg(log.height())
                      .arg(full.width()).arg(full.height())
                      .arg(wantW - full.width()).arg(wantH - full.height()));

            // ---- 合成 DPR=1.5 的画布（和真屏缩放无关，100% 的机器上照样能抓到这个错） ----
            constexpr qreal kDpr = 1.5;
            const QSize kLog(1000, 600);
            const QSize kPhys(int(kLog.width() * kDpr), int(kLog.height() * kDpr));   // 1500x900
            auto makeCanvas = [&]() {
                QPixmap pm(kPhys.width(), kPhys.height());
                pm.fill(Qt::white);
                QPainter q(&pm);
                q.fillRect(kPhys.width() - 1, kPhys.height() - 1, 1, 1, Qt::black);
                q.fillRect(300, 150, 1, 1, QColor(255, 0, 0));   // 设备 (300,150) = 逻辑 (200,100)
                q.end();
                pm.setDevicePixelRatio(kDpr);                     // 现在它的逻辑尺寸是 1000x600
                return pm;
            };
            const QPixmap whole = CaptureOverlay::cropCanvas(makeCanvas(), QRect(QPoint(0, 0), kLog));
            check("合成 1.5 倍画布上裁全屏：尺寸=物理全幅",
                  whole.size() == kPhys,
                  QStringLiteral("期望 %1x%2，拿到 %3x%4（少了 %5x%6）")
                      .arg(kPhys.width()).arg(kPhys.height()).arg(whole.width()).arg(whole.height())
                      .arg(kPhys.width() - whole.width()).arg(kPhys.height() - whole.height()));
            check("合成 1.5 倍画布上裁全屏：右下角那颗点还在",
                  qAbs(whole.devicePixelRatio() - kDpr) < 0.01
                      && whole.toImage().pixelColor(kPhys.width() - 1, kPhys.height() - 1) == QColor(Qt::black),
                  QStringLiteral("图上 DPR=%1，右下角=%2")
                      .arg(whole.devicePixelRatio())
                      .arg(whole.toImage().pixelColor(kPhys.width() - 1, kPhys.height() - 1).name()));
            // 局部选区：逻辑 (200,100) 起 = 设备 (300,150)，那颗红点必须正好落到裁出来那张图的左上角
            const QRect part(200, 100, 400, 200);
            const QPixmap piece = CaptureOverlay::cropCanvas(makeCanvas(), part);
            check("合成画布上裁局部：取到的就是那块（红点跑到左上角）",
                  piece.toImage().pixelColor(0, 0) == QColor(255, 0, 0),
                  QStringLiteral("选区=(%1,%2,%3x%4) 左上角像素=%5，裁完尺寸=%6x%7")
                      .arg(part.x()).arg(part.y()).arg(part.width()).arg(part.height())
                      .arg(piece.toImage().pixelColor(0, 0).name())
                      .arg(piece.width()).arg(piece.height()));

            // ---- QPainter::drawPixmap 的 sourceRect 按哪种坐标解释（浮层"选区亮起"用的就是它） ----
            const QPixmap canvas = makeCanvas();
            QImage sink(kPhys, QImage::Format_ARGB32);
            sink.fill(Qt::red);
            {
                QPainter q(&sink);
                q.drawPixmap(QRect(QPoint(0, 0), kPhys), canvas, QRect(QPoint(0, 0), kLog));
            }
            const QColor rawCorner = sink.pixelColor(kPhys.width() - 1, kPhys.height() - 1);
            std::printf("INFO 直接把逻辑矩形当 sourceRect：目标右下角=%s，说明 Qt 把它当%s看的\n",
                        rawCorner.name().toUtf8().constData(),
                        rawCorner == QColor(Qt::black) ? "逻辑坐标" : "设备像素");
            QImage fixed(kPhys, QImage::Format_ARGB32);
            fixed.fill(Qt::red);
            {
                QPainter q(&fixed);
                q.drawPixmap(QRect(QPoint(0, 0), kPhys), canvas,
                             CaptureOverlay::deviceRectFor(QRect(QPoint(0, 0), kLog), kDpr));
            }
            check("预览那一步换算之后取到的才是整块画布",
                  fixed.pixelColor(kPhys.width() - 1, kPhys.height() - 1) == QColor(Qt::black),
                  QStringLiteral("换算后右下角=%1").arg(
                      fixed.pixelColor(kPhys.width() - 1, kPhys.height() - 1).name()));
            const QRect keep = CaptureOverlay::deviceRectFor(QRect(7, 9, 120, 60), 1.0);
            check("dpr=1 时换算原样返回（100% 缩放下的行为一点没动）",
                  keep == QRect(7, 9, 120, 60),
                  QStringLiteral("(%1,%2,%3x%4)").arg(keep.x()).arg(keep.y())
                      .arg(keep.width()).arg(keep.height()));
            std::printf("capgeo: 失败 %d 项\n", capGeoBad);
            std::fflush(stdout);
            app.quit();
        });
    }

    // --capflash [次]：把"按 F1 到底有没有反应"变成可量数的东西。直接调 start()（不注入键盘，
    // 注入 F1 等于把全屏浮层压在用户屏幕上还不一定收得掉），量三件事：浮层存在且铺满整屏、
    // 它是当前前台窗口、鼠标底下那个窗口就是它（别的程序自己也 topmost 时会被压住）。
    // 每次开完立刻走和 Esc 一模一样的取消路径，屏幕只会亮不到半秒。要冷启动跑。
    int capFlashOk = 0;
    int capFlashTimes = 0;
    if (args.contains("--capflash")) {
        const QString nArg = args.mid(args.indexOf("--capflash") + 1).value(0);
        const int times = nArg.isEmpty() ? 2 : qBound(1, nArg.toInt(), 5);
        capFlashTimes = times;
        QTimer::singleShot(500, &app, [&app, times, &capFlashOk] {
            for (int i = 1; i <= times; ++i) {
                CaptureOverlay::start();
                for (int k = 0; k < 20; ++k) {
                    QCoreApplication::processEvents();
                    QThread::msleep(20);
                }
                QWidget* ov = nullptr;
                for (QWidget* w : QApplication::topLevelWidgets())
                    if (w->inherits("CaptureOverlay") && w->isVisible())
                        ov = w;
                const QRect scr = QGuiApplication::primaryScreen()->geometry();
                const bool full = ov && ov->geometry() == scr;
                HWND hwnd = ov ? reinterpret_cast<HWND>(ov->winId()) : nullptr;
                const bool fg = hwnd && GetForegroundWindow() == hwnd;
                POINT pt {};
                GetCursorPos(&pt);
                const bool onTop = hwnd && GetAncestor(WindowFromPoint(pt), GA_ROOT) == hwnd;
                // 判据只用确定性的三件事（浮层在、铺满整屏、鼠标底下就是它）。
                // "有没有拿到前台"在这里不当判据：脚本环境里前台归属本来就不稳
                //（同一环境下 --toggle 也会 10 次输 1 次），真实按 F1 有物理输入垫底，
                // 那一支由产品日志里的 前台=/试了N次 说话。
                const bool good = ov && full && onTop;
                const quint64 ex = hwnd ? quint64(GetWindowLongPtrW(hwnd, GWL_EXSTYLE)) : 0;
                std::printf("%s 第%d次：浮层=%s 铺满整屏=%s 在最上=%s 前台=%s（屏 %dx%d）exstyle=0x%llX fg=0x%llX 自己=0x%llX\n",
                            good ? "PASS" : "FAIL", i,
                            ov ? "在" : "没有", full ? "是" : "否",
                            onTop ? "是" : "否", fg ? "是" : "否", scr.width(), scr.height(),
                            (unsigned long long)ex,
                            (unsigned long long)(quintptr)GetForegroundWindow(),
                            (unsigned long long)(quintptr)hwnd);
                if (good)
                    ++capFlashOk;
                CaptureOverlay::cancelActive();
                for (int k = 0; k < 25 && CaptureOverlay::isActive(); ++k) {
                    QCoreApplication::processEvents();
                    QThread::msleep(20);
                }
                if (CaptureOverlay::isActive()) {
                    std::printf("FAIL 第%d次：取消之后浮层还开着\n", i);
                } else {
                    std::printf("INFO 第%d次：取消干净了\n", i);
                }
                std::fflush(stdout);
            }
            std::printf("capflash: %d/%d 次做到了\"浮层在、铺满整屏、压在所有窗口最上\"\n",
                        capFlashOk, times);
            app.quit();
        });
    }

    QObject::connect(&win, &SearchWindow::hotkeyChanged, &win, applyHotkeyChange);

    const int rc = app.exec();
    if (args.contains("--toggle"))
        return toggleBad;
    if (args.contains("--capflash"))
        return capFlashTimes - capFlashOk;
    return rc;
}
