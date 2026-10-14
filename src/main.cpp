#include "AppScanner.h"
#include "AdBlocker.h"
#include "CaptureHistory.h"
#include "ConfigPage.h"
#include "ClipboardStore.h"
#include "DevEnv.h"
#include "ToolsPage.h"
#include "PluginHost.h"
#include "PluginStore.h"
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
#include "RegClean.h"
#include "Firewall.h"
#include "StartupAudit.h"
#include "SearchWindow.h"
#include "SettingsDialog.h"
#include "SilentInstallLog.h"
#include "TaskScheduler.h"

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
#include <QFrame>
#include <QRegularExpression>
#include <QPushButton>
#include <QGroupBox>
#include <QLayout>
#include <QScrollArea>
#include <QStackedWidget>
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
//   Text            → int(const char*)         （截图那族收一个图片路径）
// flag：非空 = 判据**自己**已经打了最后那行 "<flag>: 失败 N 项"（record / capture 那几条旗标就是这么写的），
//        这儿不能再打一遍（同一次跑出两个失败数就没法看了）；空 = 按老样子用插件 id 补那一行。
// badOut：非空 = 把判据返回的那个**原始失败数**带回去。--capgeo / --capflash 的退出码就是这个数
//        （不是"有没有失败"的 0/1），调用方要在事件循环结束后拿它当进程退出码。
enum class SelftestArg { None, Ms, MsAndText, Text };

int runPluginSelftest(const QString& id, const char* symbol = "ltoolPluginSelftest",
                      SelftestArg arg = SelftestArg::None, int ms = 0,
                      const QString& text = QString(), const char* flag = nullptr,
                      int* badOut = nullptr)
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
        if (badOut)
            *badOut = 1;
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
    case SelftestArg::Text:
        bad = reinterpret_cast<int (*)(const char*)>(raw)(text.toUtf8().constData());
        break;
    }
    if (badOut)
        *badOut = bad;
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
    // 这两张页现在是 plugins/software 里的实现，主程序够不着那两个类了 —— 和自测同一条件路：
    // 按名字向那个插件点名要一颗导出符号。拿不到就是**装出来的那份 dll 太旧**，要报出来不是当 0 ms。
    const auto makePage = [](const char* symbol) -> QWidget* {
        using PageFn = QWidget* (*)();
        auto* fn = reinterpret_cast<PageFn>(PluginHost::entry(QStringLiteral("software"), symbol));
        if (!fn) {
            std::printf("%-20s 没有导出 %s（插件是旧的，这个量不到）\n", "缺符号", symbol);
            return nullptr;
        }
        return fn();
    };
    QWidget* soft = nullptr;
    QWidget* un = nullptr;
    timed("SoftwarePage ctor", [&] {
        if (!wantSoft)
            return;
        soft = makePage("ltoolSoftwarePage");
        if (soft)
            soft->setAttribute(Qt::WA_DontShowOnScreen);
    });
    timed("SoftwarePage show", [&] { if (soft) soft->show(); });
    timed("UninstallPage ctor", [&] {
        if (!wantUn)
            return;
        un = makePage("ltoolUninstallPage");
        if (un)
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
    // --pathcfg 会写 config.ini 并建目录，--appdirs 会写 apps/roots 并扫盘，
    // --storetest 会写 store/indexUrl 这一项（它就是验"配置里改了地址就跟着走"）：
    // 这几种都把数据目录整个挪到临时目录再跑，碰不到用户真实的 %APPDATA%\ltool。
    // 必须在第一次 dataDir() 之前设好。
    for (int i = 1; i < argc; ++i) {
        const QString a = QString::fromLocal8Bit(argv[i]);
        if (a == QLatin1String("--pindel") || a == QLatin1String("--ocrchain")
            || a == QLatin1String("--pathcfg") || a == QLatin1String("--pintool")
            || a == QLatin1String("--appdirs") || a == QLatin1String("--cfgcheck")
            || a == QLatin1String("--storetest")) {
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
        // 十五个自带工具现在全是 dll 插件。这张名单是"装齐了没有"的判据：
        // 少一个就是安装包少了文件，或者那个 dll 没通过检查 —— 别让它在界面上悄悄少一张卡。
        static const char* const kTools[] = { "repair", "ads", "disk", "network", "registry",
                                              "startup", "user", "json", "base64", "shell",
                                              "record", "capture", "software", "downloads",
                                              "tasks" };
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
        // F1 那一档（2026-10-15 截图族 + OCR 族进了 plugins/capture）：路数和 F2 一样 ——
        // 热键还注册在这层薄壳里，动作名 "capture" 必须有人在门口挂牌。
        // 这条红了 = 按 F1 什么都没发生，而那正是这程序里最难查的一个形状。
        check("热键动作 capture 有人接（谁）",
              PluginHost::actionOwner(QStringLiteral("capture")), QStringLiteral("capture"));
        // 那 12 面旗标的判据本体也全在插件里：符号点名不到 = 对应那面旗当场红（不会伪装成绿）。
        // 这里只 resolve、一次都不跑 —— 截图/置顶窗/OCR 那些要真屏幕，还要真下 27MB 引擎。
        static const char* const kCaptureSyms[] = {
            "ltoolSnaprectRun", "ltoolOcrtestRun", "ltoolOcrlayoutRun", "ltoolPinpickRun",
            "ltoolOcrchainRun", "ltoolPintoolRun", "ltoolPindelRun", "ltoolOcrgetRun",
            "ltoolOcrpanelRun", "ltoolPinshotRun", "ltoolCapgeoRun", "ltoolCapflashRun" };
        for (const char* s : kCaptureSyms)
            has((QStringLiteral("capture 插件导出判据入口 ") + QLatin1String(s)).toUtf8().constData(),
                PluginHost::entry(QStringLiteral("capture"), s) != nullptr);
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
        // 卡片墙：这十五个现在全靠插件目录，卡片数必须等于"通过检查的插件数"，
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
        check("十五张卡片都在卡片墙上", QString::number(cards), QStringLiteral("15"));
        // 卡片墙的几何：离屏 resize 之后布局会算，不用 show（show 就把设置窗拍到他桌面上）。
        // 这条盯的是"这十五张卡摆得开吗"。
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

    // --snaprect：吸附框贴不贴边。判据本体在 plugins/capture（导出 ltoolSnaprectRun）。
    if (args.contains("--snaprect"))
        return runPluginSelftest(QStringLiteral("capture"), "ltoolSnaprectRun",
                                 SelftestArg::None, 0, QString(), "snaprect");


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
        // 语言并进这一页之后，"配置"里必须真有那一节 —— 不然"菜单只剩四行"就变成"语言没地方了"
        {
            QStringList titles;
            for (const QGroupBox* g : page.findChildren<QGroupBox*>())
                if (g->title().contains(QStringLiteral("语言")))
                    titles << g->title();
            say("配置页里有「语言」那一节（菜单第四行没了，它得在这儿）", titles.size() == 1,
                titles.isEmpty() ? QStringLiteral("一节都没找到") : titles.join(QStringLiteral("；")));
        }

        // 设置窗那一面：菜单只剩四行，而且每一行点开都真建出一张页。
        // 这条盯的是"删掉四行菜单之后有没有留下点不开的空行"——残留入口就是没删干净。
        {
            SettingsDialog dlg;
            dlg.setAttribute(Qt::WA_DontShowOnScreen, true);
            auto* menu = dlg.findChild<QListWidget*>(QStringLiteral("menuList"));
            QStringList names;
            if (menu) {
                for (int i = 0; i < menu->count(); ++i)
                    names << menu->item(i)->text();
            }
            say("菜单正好四行、顺序是 网站/插件商店/配置/关于",
                names.join(QStringLiteral(" / ")) == QStringLiteral("网站 / 插件商店 / 配置 / 关于"),
                names.join(QStringLiteral(" / ")));
            auto* stack = dlg.findChild<QStackedWidget*>();
            int built = 0;
            for (int i = 0; menu && i < menu->count(); ++i) {
                menu->setCurrentRow(i);            // currentRowChanged → ensurePage，和真人点同一条路
                QCoreApplication::processEvents();
                QWidget* cur = stack ? stack->currentWidget() : nullptr;
                if (cur && cur->findChildren<QWidget*>().size() > 1)
                    ++built;
            }
            say("每一行点开都不是一块空面板", built == (menu ? menu->count() : 0),
                QStringLiteral("有内容的 %1 / 共 %2 行").arg(built).arg(menu ? menu->count() : 0));
        }
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

    // --ocrtest <图片>：判据本体在 plugins/capture（符号 ltoolOcrtestRun）。
    if (args.contains("--ocrtest")) {
        const QString spec = args.mid(args.indexOf("--ocrtest") + 1).value(0);
        return runPluginSelftest(QStringLiteral("capture"), "ltoolOcrtestRun",
                                 SelftestArg::Text, 0, spec, "ocrtest");
    }


    // --ocrlayout <图片>：判据本体在 plugins/capture（符号 ltoolOcrlayoutRun）。
    if (args.contains("--ocrlayout")) {
        const QString spec = args.mid(args.indexOf("--ocrlayout") + 1).value(0);
        return runPluginSelftest(QStringLiteral("capture"), "ltoolOcrlayoutRun",
                                 SelftestArg::Text, 0, spec, "ocrlayout");
    }


    // --pinpick <图片>：判据本体在 plugins/capture（符号 ltoolPinpickRun）。
    if (args.contains("--pinpick")) {
        const QString spec = args.mid(args.indexOf("--pinpick") + 1).value(0);
        return runPluginSelftest(QStringLiteral("capture"), "ltoolPinpickRun",
                                 SelftestArg::Text, 0, spec, "pinpick");
    }


    // --ocrchain <图片>：判据本体在 plugins/capture（符号 ltoolOcrchainRun）。
    if (args.contains("--ocrchain")) {
        const QString spec = args.mid(args.indexOf("--ocrchain") + 1).value(0);
        return runPluginSelftest(QStringLiteral("capture"), "ltoolOcrchainRun",
                                 SelftestArg::Text, 0, spec, "ocrchain");
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
        // 先记一份"开跑之前存不存在"：这条要判的是**这一问有没有把目录建出来**，不是"它在不在"。
        // 装在软件目录里那份 captures 本来就有用户真实的截图（本机 D:\software\ltool\captures 就是），
        // 拿"存在=no"去断言，对着安装包跑就会红一项（2026-10-15 实测：build 那份绿、装出来的那份红）。
        const bool defCapBefore = QFileInfo::exists(defCap);
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
              QFileInfo::exists(defCap) == defCapBefore ? "same" : "created", "same");

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
    // --storetest：验插件商店那一条链 —— 解析清单、和本地比、按哈希落盘、改名禁用/恢复。
    // **一次都不联网**：联网那两段（拉 index.json、下 dll）只是把字节交给下面这几个纯函数，
    // 这里直接喂文件进去。数据目录已经被挪到 %TEMP%\ltool-storetest（见 main() 开头那个循环），
    // 假 dll 全写进那里面，最后按算出来的绝对路径删掉。
    if (args.contains("--storetest")) {
        int bad = 0;
        using Entry = PluginStore::Entry;   // 这一段里全是商店的条目，写全名会把判据挤出行宽
        const auto check = [&bad](const char* name, const QString& got, const QString& want) {
            const bool ok = got == want;
            if (!ok)
                ++bad;
            std::printf("%s %s got=[%s] want=[%s]\n", ok ? "PASS" : "FAIL", name,
                        got.toUtf8().constData(), want.toUtf8().constData());
        };
        const QString box = Utils::dataDir() + "/store";
        QDir().mkpath(box);
        const auto word = [](PluginStore::State s) {
            return PluginStore::stateWord(s);
        };
        // 造一个"插件 dll"：内容随便，哈希是真算出来的（商店判的就是哈希，内容不需要真是 PE）
        const auto makeFile = [](const QString& path, const QByteArray& content) {
            QFile f(path);
            if (!f.open(QIODevice::WriteOnly))
                return QString();
            f.write(content);
            f.close();
            return PluginStore::sha256Of(path);
        };

        // ---- 1. 清单解析：好的收下，坏的点名拒收 ----
        const QString goodSha = makeFile(box + "/src-good.bin", QByteArray("GOOD-DLL-CONTENT"));
        const QString otherSha = makeFile(box + "/src-other.bin", QByteArray("OTHER"));
        const QString indexJson = QStringLiteral(
            R"({"schema":1,"core":"0.9.0","plugins":[)"
            R"({"id":"good","name":"好的那个","desc":"d","api":%2,"keywords":[],"actions":[],"color":"1,2,3",)"
            R"("file":"ltool-good.dll","bytes":16,"sha256":"%1",)"
            R"("url":"https://raw.githubusercontent.com/mateo-ki/ltool_plugin/main/dist/ltool-good.dll"},)"
            R"({"id":"trav","name":"路径穿越","desc":"d","api":%2,"keywords":[],"actions":[],"color":"1,2,3",)"
            R"("file":"../ltool-trav.dll","bytes":1,"sha256":"%3",)"
            R"("url":"https://raw.githubusercontent.com/mateo-ki/ltool_plugin/main/dist/ltool-trav.dll"},)"
            R"({"id":"plain","name":"不加密","desc":"d","api":%2,"keywords":[],"actions":[],"color":"1,2,3",)"
            R"("file":"ltool-plain.dll","bytes":1,"sha256":"%3",)"
            R"("url":"http://example.invalid/ltool-plain.dll"},)"
            R"({"id":"badhash","name":"哈希不像哈希","desc":"d","api":%2,"keywords":[],"actions":[],"color":"1,2,3",)"
            R"("file":"ltool-bad.dll","bytes":1,"sha256":"deadbeef",)"
            R"("url":"https://example.invalid/ltool-bad.dll"},)"
            R"({"id":"oldapi","name":"接口太旧","desc":"d","api":1,"keywords":[],"actions":[],"color":"1,2,3",)"
            R"("file":"ltool-old.dll","bytes":1,"sha256":"%3",)"
            R"("url":"https://example.invalid/ltool-old.dll"}]})"
            ).arg(goodSha).arg(LTOOL_PLUGIN_API_VERSION).arg(otherSha);

        QStringList problems;
        const auto entries = PluginStore::parseIndex(indexJson.toUtf8(), &problems);
        // 没收下的每一条都说出来：这一面红了要能在三秒内看出是哪个字段被拒
        for (const QString& q : problems)
            std::printf("INFO 拒收 %s\n", q.toUtf8().constData());
        // 五条里 oldapi 是"条目本身没毛病、只是接口版本不对"，它要走到 compare 才被判装不了，
        // 所以解析这一步该收两条、拒三条。
        check("五条里收下两条（good 和 oldapi）", QString::number(entries.size()), "2");
        check("拒掉的三条各说一句", QString::number(problems.size()), "3");
        check("拒收点名到 id", problems.join(QStringLiteral("|")).contains(QStringLiteral("trav"))
                  && problems.join(QStringLiteral("|")).contains(QStringLiteral("plain"))
                  && problems.join(QStringLiteral("|")).contains(QStringLiteral("badhash"))
              ? "yes" : "no", "yes");
        check("好的那条认得出的确是好的", entries.isEmpty() ? QStringLiteral("-") : entries[0].id, "good");
        {
            QStringList p2;
            const auto none = PluginStore::parseIndex(
                QByteArrayLiteral(R"({"schema":2,"plugins":[]})"), &p2);
            check("schema 不认就一条都不接", QString::number(none.size()), "0");
            check("schema 不认要说清是哪一侧", p2.join(QString()).contains(QStringLiteral("schema"))
                      ? "yes" : "no", "yes");
            QStringList p3;
            check("不是 JSON 也别当没事", PluginStore::parseIndex(QByteArrayLiteral(" plainly-not-json"), &p3).isEmpty()
                      && !p3.isEmpty() ? "yes" : "no", "yes");
        }

        // ---- 2. 和本地比：没装/已装/可更新/装不了 ----
        const Entry* found = nullptr;
        for (const Entry& e : entries)
            if (e.id == QLatin1String("good"))
                found = &e;
        check("收下的那两条里有 good", found ? "yes" : "no", "yes");
        if (!found) {
            std::printf("storetest: 失败 %d 项（good 没被收下，后面没法跑）\n", bad + 1);
            QDir(box).removeRecursively();
            return 1;
        }
        const Entry& good = *found;
        QString note;
        check("目录里什么都没有 = 能装", word(PluginStore::compare(good, &note, box)), "能装");
        QFile::copy(box + "/src-good.bin", box + "/ltool-good.dll");
        check("哈希就是清单里那个 = 已装", word(PluginStore::compare(good, &note, box)), "已装");
        {
            makeFile(box + "/ltool-good.dll", QByteArray("一个更早的版本"));
            const QString st = word(PluginStore::compare(good, &note, box));
            check("同一个文件名但内容不同 = 可更新", st, "可更新");
            check("可更新那句里带两边的哈希头", note.contains(QStringLiteral("本机")) && note.contains(QStringLiteral("清单"))
                      ? "yes" : "no", "yes");
        }
        {
            Entry old = good;
            old.api = 1;
            check("清单里的 Api 和宿主对不上 = 装不了", word(PluginStore::compare(old, &note, box)), "装不了");
            QFile::remove(box + "/ltool-good.dll");
            makeFile(box + "/ltool-old2.dll", QByteArray("x"));
            Entry o2 = good;
            o2.file = QStringLiteral("ltool-old2.dll");
            o2.api = 99;
            check("就算文件在，Api 不对也先拒", word(PluginStore::compare(o2, &note, box)), "装不了");
        }

        // ---- 3. 落盘：先验哈希，再改名；半成品不许冒充 ----
        {
            makeFile(box + "/ltool-new.dll.part", QByteArray("GOOD-DLL-CONTENT"));
            Entry e = good;
            e.file = QStringLiteral("ltool-new.dll");
            const QString err = PluginStore::finishDownload(box + "/ltool-new.dll.part", e, box);
            check("哈希对了就落进目录（回报空串）", err.isEmpty() ? "ok" : err, "ok");
            check("落下来的文件哈希还是那个", PluginStore::sha256Of(box + "/ltool-new.dll"), goodSha);
            check("落下来之后 .part 不该留着", QFile::exists(box + "/ltool-new.dll.part") ? "yes" : "no", "no");
        }
        {
            makeFile(box + "/ltool-badhash.dll.part", QByteArray("被人动过的内容"));
            Entry e = good;
            e.file = QStringLiteral("ltool-badhash.dll");
            const QString err = PluginStore::finishDownload(box + "/ltool-badhash.dll.part", e, box);
            check("哈希不对必须报错", err.isEmpty() ? "ok" : "err", "err");
            check("报错里说清是哈希", err.contains(QStringLiteral("哈希")) ? "yes" : "no", "yes");
            check("哈希不对的那份要删掉，不许留下半成品",
                  QFile::exists(box + "/ltool-badhash.dll.part") ? "yes" : "no", "no");
            check("没落进目录", QFile::exists(box + "/ltool-badhash.dll") ? "yes" : "no", "no");
        }
        {
            makeFile(box + "/ltool-trunc.dll.part", QByteArray("GOOD-DLL-CONTENT"));
            Entry e = good;
            e.file = QStringLiteral("ltool-trunc.dll");
            e.bytes = 1 << 20;    // 清单说要 1MB，下到的是 16B —— 传输被截断的形状
            const QString err = PluginStore::finishDownload(box + "/ltool-trunc.dll.part", e, box);
            check("字节数不符也拒", err.contains(QStringLiteral("字节数")) ? "yes" : "no", "yes");
            check("字节数不符的那份也删掉", QFile::exists(box + "/ltool-trunc.dll.part") ? "yes" : "no", "no");
        }

        // ---- 4. 禁用/恢复 = 在 <file> 和 <file>.off 之间改名 ----
        {
            makeFile(box + "/ltool-off.dll", QByteArray("GOOD-DLL-CONTENT"));
            Entry e = good;
            e.file = QStringLiteral("ltool-off.dll");
            check("禁用回报空串", [&] {
                const QString r = PluginStore::setEnabled(e.file, false, box);
                return r.isEmpty() ? QStringLiteral("ok") : r;
            }(), "ok");
            check("禁用之后 live 没了", QFile::exists(box + "/ltool-off.dll") ? "yes" : "no", "no");
            check("禁用之后 .off 在", QFile::exists(box + "/ltool-off.dll.off") ? "yes" : "no", "yes");
            check("目录里那种状态认得出已禁用", word(PluginStore::compare(e, &note, box)), "已禁用");
            check("再禁一次不算失败", [&] {
                const QString r = PluginStore::setEnabled(e.file, false, box);
                return r.isEmpty() ? QStringLiteral("ok") : r;
            }(), "ok");
            check("恢复：.off 改回 live", [&] {
                const QString r = PluginStore::setEnabled(e.file, true, box);
                return (r.isEmpty() && QFile::exists(box + "/ltool-off.dll")
                        && !QFile::exists(box + "/ltool-off.dll.off"))
                          ? QStringLiteral("ok") : r;
            }(), "ok");
            check("目录里又认得出已装", word(PluginStore::compare(e, &note, box)), "已装");
            check("没装过的东西谈不上禁用",
                  PluginStore::setEnabled(QStringLiteral("ltool-never.dll"), false, box).isEmpty()
                      ? QStringLiteral("ok") : QStringLiteral("err"), "err");
        }

        // ---- 4a. 「目录里有什么」这一面（没拉清单时列表就是它） ----
        {
            // 到这儿为止目录里有三个 *.dll（new / off / old2，都是上面几段落的产物），
            // 再造一个改名成 .off 的，就能同时看到"活着"和"被禁用"两种。
            makeFile(box + "/ltool-extra.dll", QByteArray("要被禁的那个"));
            PluginStore::setEnabled(QStringLiteral("ltool-extra.dll"), false, box);
            const auto locals = PluginStore::localRows(box);
            QStringList who;
            int on = 0, off = 0;
            for (const PluginStore::Row& r : locals) {
                who << QStringLiteral("%1=%2").arg(r.remote.file, PluginStore::stateWord(r.state));
                if (r.state == PluginStore::State::Installed)
                    ++on;
                if (r.state == PluginStore::State::Disabled)
                    ++off;
            }
            // 目录里有 *.dll / *.dll.off / *.bin（bin 不算）、还有那个 .part 留下的别的文件也不算
            check("只把 *.dll 和 *.dll.off 当插件（旁边那些 *.bin 不算）", QString::number(who.size()), "4");
            std::printf("INFO 目录里认出来的：%s\n", who.join(QStringLiteral("，")).toUtf8().constData());
            check("活着的报已装、改名 .off 的报已禁用",
                  QStringLiteral("%1/%2").arg(on).arg(off), QStringLiteral("3/1"));
            check("每一条的 note 都写着自己的文件名（不是空话）",
                  std::all_of(locals.begin(), locals.end(), [](const PluginStore::Row& r) {
                      return r.note.contains(r.remote.file);
                  })
                      ? "yes" : "no", "yes");
            check("活着的那几条还带字节数",
                  std::all_of(locals.begin(), locals.end(), [](const PluginStore::Row& r) {
                      return r.state != PluginStore::State::Installed
                             || r.note.contains(QStringLiteral(" B"));
                  })
                      ? "yes" : "no", "yes");
        }

        // ---- 4b. 本机装着、清单里却没列的那几个 ----
        {
            makeFile(box + "/ltool-stranger.dll", QByteArray("不在清单上的那个"));
            makeFile(box + "/ltool-good.dll", QByteArray("GOOD-DLL-CONTENT"));
            // `listed` 就是第 1 段解析出来的那两条（good 的文件名是 ltool-good.dll）：
            // 目录里那个文件既然被清单覆盖了，就不该再被报成"清单外"。
            QStringList names;
            int localOnly = 0;
            int off = 0;
            const auto collect = [&](const QVector<PluginStore::Row>& rs) {
                names.clear();
                localOnly = off = 0;
                for (const PluginStore::Row& r : rs) {
                    names << r.remote.file;
                    if (r.state == PluginStore::State::LocalOnly)
                        ++localOnly;
                    if (r.state == PluginStore::State::Disabled)
                        ++off;
                }
            };
            collect(PluginStore::localOnly(entries, box));
            check("清单外那一栏报出没被清单覆盖的文件",
                  names.contains(QStringLiteral("ltool-stranger.dll"))
                      && names.contains(QStringLiteral("ltool-new.dll")) ? "yes" : "no", "yes");
            check("清单里已经列过的那个文件不再报成清单外",
                  names.contains(QStringLiteral("ltool-good.dll")) ? "yes" : "no", "no");
            check("没禁用之前：清单外的条数 = 总条数减掉已禁用的那几条", QString::number(localOnly),
                  QString::number(names.size() - off));
            const QString err = PluginStore::setEnabled(QStringLiteral("ltool-stranger.dll"), false, box);
            collect(PluginStore::localOnly(entries, box));
            check("清单外那条也能禁用（禁完它还在栏里，已禁用的数比上一轮多一条）",
                  err.isEmpty() && names.contains(QStringLiteral("ltool-stranger.dll"))
                      && off == 2 && localOnly == names.size() - off ? "yes" : "no", "yes");
        }

        // ---- 5. 索引地址：留空用内置那条，配了就用配的 ----
        {
            check("内置地址是插件仓库那份 raw 清单",
                  PluginStore::defaultIndexUrl().startsWith(
                      QStringLiteral("https://raw.githubusercontent.com/mateo-ki/ltool_plugin/main/dist/"))
                      ? "yes" : "no", "yes");
            QSettings cfg(Utils::dataDir() + "/config.ini", QSettings::IniFormat);
            check("没配过就用内置那条", PluginStore::indexUrl() == PluginStore::defaultIndexUrl()
                      ? "yes" : "no", "yes");
            cfg.setValue(QStringLiteral("store/indexUrl"), QStringLiteral("https://example.invalid/index.json"));
            cfg.sync();
            check("配了就用配的", PluginStore::indexUrl(), "https://example.invalid/index.json");
            cfg.setValue(QStringLiteral("store/indexUrl"), QStringLiteral("   "));
            cfg.sync();
            check("清空等于回到内置", PluginStore::indexUrl() == PluginStore::defaultIndexUrl()
                      ? "yes" : "no", "yes");
        }

        // 全程没碰真目录：商店判的是传进去的那个 dir
        check("没把假 dll 写进真正的插件目录",
              QFile::exists(PluginHost::pluginsDir() + "/ltool-new.dll") ? "yes" : "no", "no");
        QDir(box).removeRecursively();
        std::printf("storetest: 失败 %d 项\n", bad);
        std::fflush(stdout);
        return bad == 0 ? 0 : 1;
    }


    // --pintool：离屏验置顶窗那两个"收起"开关。判据本体在 plugins/capture。
    if (args.contains("--pintool"))
        return runPluginSelftest(QStringLiteral("capture"), "ltoolPintoolRun",
                                 SelftestArg::None, 0, QString(), "pintool");


    // --pindel <图片>：判据本体在 plugins/capture（符号 ltoolPindelRun）。
    if (args.contains("--pindel")) {
        const QString spec = args.mid(args.indexOf("--pindel") + 1).value(0);
        return runPluginSelftest(QStringLiteral("capture"), "ltoolPindelRun",
                                 SelftestArg::Text, 0, spec, "pindel");
    }


    // --ocrget：下载并校验 OCR 引擎包。判据本体在 plugins/capture。
    if (args.contains("--ocrget"))
        return runPluginSelftest(QStringLiteral("capture"), "ltoolOcrgetRun",
                                 SelftestArg::None, 0, QString(), "ocrget");


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

    // --ocrpanel <图片>：判据本体在 plugins/capture（符号 ltoolOcrpanelRun）。
    if (args.contains("--ocrpanel")) {
        const QString spec = args.mid(args.indexOf("--ocrpanel") + 1).value(0);
        return runPluginSelftest(QStringLiteral("capture"), "ltoolOcrpanelRun",
                                 SelftestArg::Text, 0, spec, "ocrpanel");
    }


    // --pinshot <图片>：判据本体在 plugins/capture（符号 ltoolPinshotRun）。
    if (args.contains("--pinshot")) {
        const QString spec = args.mid(args.indexOf("--pinshot") + 1).value(0);
        return runPluginSelftest(QStringLiteral("capture"), "ltoolPinshotRun",
                                 SelftestArg::Text, 0, spec, "pinshot");
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
                                || args.contains(QLatin1String("--capflash"))
                                || args.contains(QLatin1String("--storenet"));
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

    // 热键背后那个插件动作没人接的时候怎么说：日志（事后能查）+ 托盘气泡（当下能看见）+
    // stderr（从命令行起着的时候能看见），**三层都说、绝不弹模态框** ——
    // 那会盖在用户正做的事情上面（2026-10-15 右键那一路就是这么被他撞上的）。
    // who 空 = 根本没插件声明接这个动作；非空 = 它接了但没干成（那一份插件自己会说明为什么）。
    const auto routeHotkey = [&](const QString& keyLabel, const QString& action) {
        QString who;
        if (PluginHost::invoke(action, {}, &who))
            return;
        const QString why = who.isEmpty()
            ? QStringLiteral("没装接 %1 这个动作的插件（ltool-plugins 里是不是缺 ltool-%2.dll）")
                  .arg(action, action)
            : QStringLiteral("%1 接了但没干成").arg(who);
        Utils::log(QStringLiteral("%1 没人接：%2").arg(keyLabel, why));
        std::fprintf(stderr, "%s: %s\n", keyLabel.toUtf8().constData(), why.toUtf8().constData());
        std::fflush(stderr);
        if (trayPtr)
            trayPtr->showMessage(keyLabel, why, QSystemTrayIcon::Warning, 5000);
    };

    // F1 = 截图（本体在 plugins/capture，2026-10-15 搬过去的）、F2 = 录屏（plugins/record）：
    // 这层壳只把热键编号翻译成动作名递过去。注册仍然在这里 —— 主程序必须是那个还能起得来的薄壳。
    HotkeyFilter filter([&](WPARAM id) {
        if (id == kCaptureId)
            routeHotkey(QStringLiteral("F1 截图"), QStringLiteral("capture"));
        else if (id == kRecordId)
            routeHotkey(QStringLiteral("F2 录屏"), QStringLiteral("record"));
        else
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
        win.openSettingsPage(3);   // 关于是第四行（菜单只剩四行）
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

    // --capgeo / --capflash：判据本体在 plugins/capture（ltoolCapgeoRun / ltoolCapflashRun），
    // 但这两条**必须留在单实例守卫之后**：浮层要在事件循环里真开出来才算数。
    // 延一拍再跑是原来的行为（窗口刚建好那一下屏幕还在动，量出来的几何不作数）。
    int capGeoBad = 0;
    int capFlashBad = 0;
    if (args.contains("--capgeo")) {
        QTimer::singleShot(80, &app, [&app, &capGeoBad] {
            runPluginSelftest(QStringLiteral("capture"), "ltoolCapgeoRun",
                              SelftestArg::None, 0, QString(), "capgeo", &capGeoBad);
            app.quit();
        });
    }
    if (args.contains("--capflash")) {
        const QString nArg = args.mid(args.indexOf("--capflash") + 1).value(0);
        const int times = nArg.isEmpty() ? 2 : qBound(1, nArg.toInt(), 5);
        QTimer::singleShot(500, &app, [&app, times, &capFlashBad] {
            runPluginSelftest(QStringLiteral("capture"), "ltoolCapflashRun",
                              SelftestArg::Ms, times, QString(), "capflash", &capFlashBad);
            app.quit();
        });
    }

    // --storenet：商店**联网那一段**的界面判据 —— 建一个离屏的 ToolsPage，点「刷新清单」，
    // 等回复落地，然后数画出来的行、和状态行里那个数对一遍。
    // 它和 --storetest 分家是必须的：那条一次都不联网；这条要真去拉 github raw，
    // 没网的环境跑它就是自找的红，所以也不进 CI（他点头之后再手动跑）。
    int storeNetBad = 0;
    if (args.contains("--storenet")) {
        QTimer::singleShot(120, &app, [&app, &storeNetBad] {
            ToolsPage page;
            page.setAttribute(Qt::WA_DontShowOnScreen, true);
            page.resize(940, 720);
            page.show();
            for (int i = 0; i < 10; ++i)
                QCoreApplication::processEvents();
            const auto check = [&storeNetBad](const char* name, bool ok, const QString& got) {
                std::printf("%s %s got=[%s]\n", ok ? "PASS" : "FAIL", name, got.toUtf8().constData());
                if (!ok)
                    ++storeNetBad;
            };
            QPushButton* refresh = page.findChild<QPushButton*>(QStringLiteral("storeRefresh"));
            check("标题那一行真有一颗「刷新清单」，而且在布局里", refresh && refresh->isVisible(),
                  QStringLiteral("找得到=%1 可见=%2").arg(refresh ? 1 : 0)
                      .arg(refresh && refresh->isVisible() ? 1 : 0));
            if (!refresh) {
                std::printf("storenet: 失败 %d 项（没有刷新按钮，后面没法跑）\n", storeNetBad);
                app.quit();
                return;
            }
            // 数之前先把 deleteLater 排干净：rebuildGrid 换整格时旧卡片是"待删"状态，
            // 这时候数 findChildren 会把新旧两批一起数进去（探针第一版就是这么量出 30 张卡的）。
            const auto drain = [] { for (int i = 0; i < 30; ++i) QCoreApplication::processEvents(); };
            drain();
            const auto countButtons = [&page](const QStringList& texts) {
                int n = 0;
                for (QPushButton* b : page.findChildren<QPushButton*>()) {
                    if (b->objectName() == QLatin1String("storeRefresh") || !b->isVisible())
                        continue;
                    if (texts.contains(b->text()))
                        ++n;
                }
                return n;
            };
            const int cardsBefore = page.findChildren<QFrame*>(QStringLiteral("row")).size();
            // 还没拉清单：这一面只报目录里有什么，所以一颗「装」都没有 —— 装什么得先问仓库
            const int installersBefore = countButtons({QStringLiteral("装"), QStringLiteral("更新")});
            refresh->click();
            // 等到状态行变成"清单 N 条…"为止。raw 平时几百毫秒，慢了通常是代理在排队 ——
            // 20 秒上限，到点就把看到的说出来，不许静默当"通过"，也不许把"网络慢"读成"界面坏"。
            QString st;
            QLabel* status = nullptr;
            for (int i = 0; i < 400; ++i) {
                QCoreApplication::processEvents();
                QThread::msleep(50);
                status = nullptr;
                for (QLabel* l : page.findChildren<QLabel*>()) {
                    if (l->text().startsWith(QStringLiteral("清单"))) {
                        status = l;
                        break;
                    }
                }
                st = status ? status->text() : QString();
                if (st.startsWith(QStringLiteral("清单 ")) || st.startsWith(QStringLiteral("清单没拉到")))
                    break;
            }
            drain();
            std::printf("INFO 状态行：%s\n", st.toUtf8().constData());
            const QRegularExpression re(QStringLiteral(
                "清单 (\\d+) 条（地址 .+?）· 这一面 (\\d+) 张卡 · 能装或可更新 (\\d+) · 拒收 (\\d+) · 清单外 (\\d+)"));
            const auto m = re.match(st);
            check("拉到了就在状态行把数说清（没拉到会走另一句，不当成通过）", m.hasMatch(), st.left(70));
            if (!m.hasMatch()) {
                for (QLabel* l : page.findChildren<QLabel*>())
                    if (!l->text().isEmpty())
                        std::printf("  [%s]\n", l->text().left(90).toUtf8().constData());
            } else {
                const int said = m.captured(1).toInt();
                const int cards = m.captured(2).toInt();
                const int canInstall = m.captured(3).toInt();
                const int rejected = m.captured(4).toInt();
                const int outside = m.captured(5).toInt();
                const int nowCards = page.findChildren<QFrame*>(QStringLiteral("row")).size();
                // 只剩一面列表：卡片数就是状态行报的那个数，而且它是"清单 + 清单外"合起来
                check("一面列表：画出的卡片数 = 状态行说的数", nowCards == cards,
                      QStringLiteral("画出 %1 / 说了 %2").arg(nowCards).arg(cards));
                check("卡片数 = 清单条数 + 清单外那几个", cards == said + outside,
                      QStringLiteral("清单 %1 + 清单外 %2 = %3").arg(said).arg(outside).arg(cards));
                check("没拉清单之前一颗「装」都没有（列的是目录，不是仓库）", installersBefore == 0,
                      QStringLiteral("刷新前 %1 颗装/更新按钮").arg(installersBefore));
                check("刷新前后是同一面列表（卡片数随目录/清单变，不是另开一屏）", nowCards >= cardsBefore,
                      QStringLiteral("刷新前 %1 / 刷新后 %2").arg(cardsBefore).arg(nowCards));
                check("插件仓库那十二个都在清单里", said >= 12, QStringLiteral("清单 %1 条").arg(said));
                // 装了的那张卡**不许再有「装」这颗按钮**，没装的那张必须有 —— 这是这次改的正面判据
                // 六个状态词都从 stateWord 取，别把中文抄第二遍（抄的那份第一次加状态就过期）
                const QVector<QString> words = {
                    PluginStore::stateWord(PluginStore::State::Missing),      // 0 能装
                    PluginStore::stateWord(PluginStore::State::Update),       // 1 可更新
                    PluginStore::stateWord(PluginStore::State::Installed),    // 2 已装
                    PluginStore::stateWord(PluginStore::State::Disabled),     // 3 已禁用
                    PluginStore::stateWord(PluginStore::State::ApiMismatch),  // 4 装不了
                    PluginStore::stateWord(PluginStore::State::LocalOnly),    // 5 清单外
                };
                // 逐张卡看：状态词说什么，按钮就该给什么。这是这次"一张列表"的正面判据 ——
                // 装了的不许出现「装」，没装的不许出现「禁用」，可更新的两颗都得有。
                int states = 0, nMissing = 0, nOnDisk = 0, wrong = 0;
                QStringList wrongs;
                for (QFrame* card : page.findChildren<QFrame*>(QStringLiteral("row"))) {
                    int idx = -1;
                    for (QLabel* l : card->findChildren<QLabel*>()) {
                        if (idx >= 0)
                            continue;
                        for (int i = 0; i < words.size(); ++i)
                            if (l->text().startsWith(words[i])) {
                                idx = i;
                                break;
                            }
                    }
                    bool hasInstall = false, hasToggle = false;
                    for (QPushButton* b : card->findChildren<QPushButton*>()) {
                        if (!b->isVisible())
                            continue;
                        const QString t = b->text();
                        if (t == QStringLiteral("装") || t == QStringLiteral("更新"))
                            hasInstall = true;
                        if (t == QStringLiteral("禁用") || t == QStringLiteral("启用"))
                            hasToggle = true;
                    }
                    if (idx < 0) {
                        ++wrong;
                        wrongs << QStringLiteral("(一张没有状态词的卡)");
                        continue;
                    }
                    ++states;
                    const bool wantInstall = idx == 0 || idx == 1;
                    const bool wantToggle = idx >= 1 && idx != 4;   // 装了的（含可更新、清单外、已禁用）都能动
                    if (idx == 0)
                        ++nMissing;
                    if (wantToggle)
                        ++nOnDisk;
                    if (hasInstall != wantInstall || hasToggle != wantToggle) {
                        ++wrong;
                        wrongs << QStringLiteral("「%1」那张 装=%2 禁用=%3（该给 装=%4 禁用=%5）")
                                      .arg(words[idx],
                                           hasInstall ? QStringLiteral("有") : QStringLiteral("没"),
                                           hasToggle ? QStringLiteral("有") : QStringLiteral("没"),
                                           wantInstall ? QStringLiteral("有") : QStringLiteral("没"),
                                           wantToggle ? QStringLiteral("有") : QStringLiteral("没"));
                    }
                }
                check("每张卡的状态词都认得出", states == nowCards,
                      QStringLiteral("有状态 %1 / 卡片 %2").arg(states).arg(nowCards));
                check("每张卡的按钮和自己那句状态对得上（装了的不给装、没装的不给禁用）", wrong == 0,
                      wrongs.isEmpty() ? QStringLiteral("%1 张全对上").arg(nowCards)
                                       : QStringLiteral("%1 张不对：%2").arg(wrong).arg(wrongs.join(QStringLiteral("；"))));
                int installers = 0, toggles = 0;
                for (QPushButton* b : page.findChildren<QPushButton*>()) {
                    if (b->objectName() == QLatin1String("storeRefresh") || !b->isVisible())
                        continue;
                    if (b->text() == QStringLiteral("装") || b->text() == QStringLiteral("更新"))
                        ++installers;
                    else if (b->text() == QStringLiteral("禁用") || b->text() == QStringLiteral("启用"))
                        ++toggles;
                }
                check("装/更新按钮颗数 = 状态行说的能装或可更新", installers == canInstall,
                      QStringLiteral("按钮 %1 颗 / 说了 %2 条").arg(installers).arg(canInstall));
                check("禁用/启用按钮颗数 = 本机真装着的那几个", toggles == nOnDisk,
                      QStringLiteral("禁用/启用 %1 颗 / 装着 %2 张（能装 %3 张）")
                          .arg(toggles).arg(nOnDisk).arg(nMissing));
                check("没拉清单之前这一面只列本机那几个，所以一颗「装」都没有", installersBefore == 0,
                      QStringLiteral("刷新前 %1 颗").arg(installersBefore));
                std::printf("INFO 拒收 %d 条（本仓库的 dist 现在是干净的）\n", rejected);
            }
            page.hide();
            std::printf("storenet: 失败 %d 项\n", storeNetBad);
            std::fflush(stdout);
            app.quit();
        });
    }

    QObject::connect(&win, &SearchWindow::hotkeyChanged, &win, applyHotkeyChange);

    const int rc = app.exec();
    if (args.contains("--toggle"))
        return toggleBad;
    if (args.contains("--capgeo"))
        return capGeoBad;
    if (args.contains("--capflash"))
        return capFlashBad;
    if (args.contains("--storenet"))
        return storeNetBad;
    return rc;
}
