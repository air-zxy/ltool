#!/usr/bin/env bash
# 平台层那三道缝在 Linux 侧的行为探针（src/platform/Paths_posix.cpp、Launch_posix.cpp、
# Autostart_posix.cpp）。
# 为什么要有：主仓库现在在 Linux 还 configure 不过（停在 find_package(unofficial-bit7z)），
# 所以 `--seamtest` 那几十条在 Linux 上跑不起来，缝的 POSIX 那份就没有可复跑的证据。
# 这里只编那几个 .cpp + 一段 main，用系统的 Qt6Core，绕开整个工程。
# 跨平台 WP2 把工程编通之后，这个文件就该删掉 —— 那些判据会并进 --seamtest 一个入口。
#
# 跑法（Windows 侧）：wsl -e bash tools/seam_probe.sh
# 开机自启那一段单独跑一次，把 HOME 指到一个空临时目录 —— 它要真的写文件、真的删文件，
# 不能拿跑的人真实的 ~/.config/autostart 当试验场。
set -e
D=$(mktemp -d)
trap 'rm -rf "$D"' EXIT

cat > "$D/probe.cpp" <<'EOF'
#include "platform/Paths.h"
#include "platform/Launch.h"
#include "platform/Autostart.h"
#include "Pinyin.h"
#include "platform/ProcInfo.h"
#include "platform/FileMeta.h"
#include "platform/PluginNames.h"
#include "platform/FileOpsShred.h"    // 第七道缝：FileOps::shred / whoHas / killProcess
#include "platform/SysConfig.h"      // 第八道缝：系统代理 + 安装根
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QProcess>
#include <QThread>
#include <QFileInfo>
#include <QStringList>
#include <cstdio>
static int bad = 0;
static void check(const char* n, bool ok) {
    std::printf("%s %s\n", ok ? "PASS" : "FAIL", n);
    if (!ok) ++bad;
}
static int autostartRoundTrip() {
    const QString target = Platform::autostartTarget();
    check("autostart 落在 HOME 下的自启动目录里", target.startsWith(QDir::homePath()));
    check("Linux 那一颗的文件名就是 ltool.desktop（桌面环境按这个位置找）",
          target.endsWith(QStringLiteral("/ltool.desktop")));
    check("一开始没开", !Platform::autostartEnabled());
    Platform::setAutostart(true);
    check("勾上之后 autostartEnabled 说开了", Platform::autostartEnabled());
    QFile f(target);
    const bool opened = f.open(QIODevice::ReadOnly);
    const QString body = opened ? QString::fromUtf8(f.readAll()) : QString();
    check("文件真的写出来了，路径就是 autostartTarget 说的那个", opened);
    check("Exec 那一行是这一个 exe 的路径（挪目录要跟着走）",
          body.contains("Exec=") && body.contains(QCoreApplication::applicationFilePath()));
    const QString spaced = Platform::autostartEntry("/tmp/My Apps/ltool");
    check("路径里的空格被转义（不然那一行会被切成两个参数）",
          spaced.contains("Exec=/tmp/My\\ Apps/ltool"));
    Platform::setAutostart(false);
    check("关掉之后文件没了", !QFileInfo::exists(target));
    check("关掉之后 autostartEnabled 跟着说没开", !Platform::autostartEnabled());
    std::printf("seam_probe(autostart): failed %d\n", bad);
    std::fflush(stdout);
    return bad;
}
int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    if (argc > 1 && QString(argv[1]) == QLatin1String("--autostart"))
        return autostartRoundTrip();
    // ---- 拼音引擎在 Linux 上给出同样的答案（搜索框那一路是可移植的，实测过才敢说）----
    const QString renwuGuanli = QString::fromUtf8("任务管理器");
    const QString renwu = QString::fromUtf8("任务");
    const QString guanggaotanchuang = QString::fromUtf8("广告弹窗");
    const QString qidongdongfenxi = QString::fromUtf8("开机启动分析");
    const QString laji = QString::fromUtf8("垃圾");
    check("usableQuery：两个纯字母才开这条路", Pinyin::usableQuery(QStringLiteral("rw")));
    check("usableQuery：单个字母不开（开了每个中文名都中）", !Pinyin::usableQuery(QStringLiteral("r")));
    check("usableQuery：夹数字/符号不开", !Pinyin::usableQuery(QStringLiteral("r1")));
    check("renwu 全拼命中「任务管理器」", Pinyin::match(renwuGuanli, QStringLiteral("renwu")) == Pinyin::Full);
    check("rw 首字母命中「任务」", Pinyin::match(renwu, QStringLiteral("rw")) == Pinyin::Initial);
    check("中段也命中（应用那一档要召回：guanli）",
          Pinyin::match(renwuGuanli, QStringLiteral("guanli")) == Pinyin::Full);
    check("An 不当「广告弹窗」的直达（广的主读音是 guang）",
          Pinyin::matchStart(guanggaotanchuang, QStringLiteral("an")) == Pinyin::None);
    check("直达只认开头：renwu 不抢「开机启动分析」",
          Pinyin::matchStart(qidongdongfenxi, QStringLiteral("renwu")) == Pinyin::None);
    check("表和码点对齐：la ji 中「垃圾」，renwu 不中",
          Pinyin::match(laji, QStringLiteral("laji")) == Pinyin::Full
              && Pinyin::match(laji, QStringLiteral("renwu")) == Pinyin::None);
    const QStringList cq = Pinyin::fullReadings(QString::fromUtf8("重庆"));
    check("多音字两个读音都留（chongqing / zhongqing）",
          cq.contains(QStringLiteral("chongqing")) && cq.contains(QStringLiteral("zhongqing")));
    const QString desk = Platform::knownDir(Platform::KnownDir::UserDesktop);
    const QString shared = Platform::knownDir(Platform::KnownDir::SharedDesktop);
    const QString ua = Platform::knownDir(Platform::KnownDir::UserApps);
    const QString sa = Platform::knownDir(Platform::KnownDir::SystemApps);
    const QString home = Platform::expandEnv("$HOME");
    std::printf("INFO desk=[%s] shared=[%s] userApps=[%s] sysApps=[%s] home=[%s]\n",
                desk.toUtf8().constData(), shared.toUtf8().constData(),
                ua.toUtf8().constData(), sa.toUtf8().constData(), home.toUtf8().constData());
    check("user desktop non-empty", !desk.isEmpty());
    check("user apps dir non-empty", !ua.isEmpty());
    check("system apps dir non-empty", !sa.isEmpty());
    check("no shared-desktop concept here", shared.isEmpty());
    check("user apps under home", ua.startsWith(QDir::homePath()));
    check("system apps not under home", !sa.startsWith(QDir::homePath()));
    check("$HOME becomes the real value", home == QDir::homePath());
    check("mixed sentence expands", Platform::expandEnv("A$HOME B") == QString("A") + home + " B");
    check("braced form expands", Platform::expandEnv("${HOME}") == home);
    check("unknown name kept literal", Platform::expandEnv("$LTOOL_NO_SUCH_VAR") == QString("$LTOOL_NO_SUCH_VAR"));
    check("windows-style token left alone", Platform::expandEnv("%SystemRoot%") == QString("%SystemRoot%"));
    check("plain path unchanged", Platform::expandEnv("/tmp/plain") == QString("/tmp/plain"));
    check("empty path not launched", !Platform::launch(QString()));
    check("empty path not elevated", !Platform::launchAsAdmin(QString()));
    // ---- 第五道缝（进程 / 文件元信息）在 Linux 上的真行为 ----
    check("findExecutable: PATH 里的找得到", !Platform::findExecutable("sh").isEmpty());
    check("findExecutable: 没有的返回空", Platform::findExecutable("ltool-no-such-exe-xyz").isEmpty());
    check("workingSetMB 读得到（>0）", Platform::workingSetMB() > 0);
    check("trimWorkingSet 这平台做不到就老实返回 false", !Platform::trimWorkingSet());
    check("fileVersion 普通文件没有版本资源 → 空串",
          Platform::fileVersion("/bin/sh").isEmpty());
    QProcess child;
    child.start("/bin/sleep", {QStringLiteral("30")});
    const bool started = child.waitForStarted(2000);
    const qint64 childPid = started ? child.processId() : -1;
    check("起得了一个子进程用来杀（sleep 30）", started);
    if (started) {
        check("杀之前 /proc 里它在", QFileInfo::exists(QStringLiteral("/proc/%1").arg(childPid)));
        const bool killed = Platform::killProcessTree(childPid);
        child.waitForFinished(2000);
        check("killProcessTree 报成功", killed);
        QThread::msleep(150);
        check("杀完 /proc 里真没了", !QFileInfo::exists(QStringLiteral("/proc/%1").arg(childPid)));
    }
    check("extractIconToPng 对着纯文本老实说 false",
          !Platform::extractIconToPng(QStringLiteral("/etc/hostname"), 0, QDir::tempPath() + "/nope.png"));
    // ---- 第六道缝（插件产物命名）在 POSIX 这一侧的答案 ----
    // 这一族判据的意义是"能红"：Windows 上 pluginFileSuffix() 本来就是 dll，改错了也测不出来；
    // 在 Linux 上，只要还有任何一处把 .dll 当协议写死，这几个期望值就立刻对不上。
    check("这台机器的后缀是 so（不是 dll）", Platform::pluginFileSuffix() == QLatin1String("so"));
    check("扫描匹配式跟着走", Platform::pluginFileGlobs()
          == QStringList{ QStringLiteral("*.so") });
    check("文件名带 lib 前缀", Platform::pluginFileName(QStringLiteral("shell"))
          == QStringLiteral("libltool-shell.so"));
    check("文件名反推得回 id", Platform::pluginIdOf(QStringLiteral("libltool-shell.so"))
          == QStringLiteral("shell"));
    check("别的平台那个后缀在这里不认（dll 反推不出 id）",
          Platform::pluginIdOf(QStringLiteral("ltool-shell.dll")).isEmpty());
    check("裸 .so 也认不出 id（只有 .so 那四个字符的文件名不是插件）",
          Platform::pluginIdOf(QStringLiteral(".so")).isEmpty());
        // ---- 第七道缝（粉碎 / 占用查询 / 结束进程）在 Linux 上的真行为 ----
    // Windows 那侧同一组夹具的实测数记在 docs/notes.md（--shredtest / --whohastest）：
    // 目录树那两颗之和 4096+8192=12288，两边、搬家前后都该是同一个数。
    const QString box = QDir::tempPath() + "/ltool-fileops";
    QDir(box).removeRecursively();            // 只清自己刚建的这一棵，不碰别的东西
    QDir().mkpath(box + "/tree/sub");
    const auto make = [](const QString& path, int bytes) {
        QFile f(path);
        if (!f.open(QIODevice::WriteOnly))
            return false;
        f.write(QByteArray(bytes, 'A'));
        f.close();
        return true;
    };
    make(box + "/a.bin", 1 << 20);
    const qint64 shallow = FileOps::shred(box + "/a.bin", false, nullptr);
    check("Linux 粉碎报的字节数 = 文件大小（1MB）", shallow == (1 << 20));
    check("浅粉碎之后文件真没了", !QFileInfo::exists(box + "/a.bin"));
    QString serr;
    make(box + "/b.bin", 4096);
    const qint64 deep = FileOps::shred(box + "/b.bin", true, &serr);
    check("thorough 三遍覆写也按大小报一遍（不是三遍）", deep == 4096 && serr.isEmpty());
    const QStringList leftovers = QDir(box).entryList(QStringList() << QStringLiteral("~*.tmp"), QDir::Files);
    check("改名用的 ~*.tmp 没留在原地（改名那条真走了）", leftovers.isEmpty());
    make(box + "/tree/a.bin", 4096);
    make(box + "/tree/sub/b.bin", 8192);
    serr.clear();
    const qint64 dirErased = FileOps::shred(box + "/tree", false, &serr);
    check("整棵目录树抹掉的字节数是两颗之和（和 Windows 同一个 12288）", dirErased == 12288);
    check("目录从最深往上删干净、没报错", !QFileInfo::exists(box + "/tree") && serr.isEmpty());
    QString missErr;
    const qint64 missing = FileOps::shred(box + "/nope.bin", false, &missErr);
    check("不存在的目标返回 -1 并说出原因", missing == -1 && missErr.contains(QString::fromUtf8("不存在")));
    const QString held = box + "/held.bin";
    make(held, 1024);
    check("没人开着的文件：占用表是空的", FileOps::whoHas(held, &serr).isEmpty());
    QProcess holder;
    holder.start(QStringLiteral("/bin/tail"),
                 {QStringLiteral("-n"), QStringLiteral("0"), QStringLiteral("-F"), held});
    const bool holderUp = holder.waitForStarted(3000);
    check("起了一个 tail -F 当占用者（谁占用着它的试验场）", holderUp);
    if (holderUp) {
        QString werr;
        const auto hs = FileOps::whoHas(held, &werr);
        std::printf("INFO whoHas 扫到 %d 个占用者，err=[%s]\n", int(hs.size()),
                    werr.toUtf8().constData());
        bool seesIt = false;
        for (const FileOps::Holder& h : hs)
            if (h.pid == quint64(holder.processId()))
                seesIt = true;
        check("扫 /proc/<pid>/fd 认出了那颗 tail 的 pid", seesIt);
        check("占用者的进程名读得出来", !hs.isEmpty() && !hs.first().processName.isEmpty());
        QString kerr;
        check("killProcess 结束它报成功", FileOps::killProcess(quint64(holder.processId()), &kerr));
        holder.waitForFinished(3000);
        QString werr2;
        check("结束之后占用表空了（和 Windows 那条一样归零）", FileOps::whoHas(held, &werr2).isEmpty());
        QString kerr2;
        check("再杀一次（进程号已经没了）老实报没有那个进程",
              !FileOps::killProcess(quint64(holder.processId()), &kerr2) && !kerr2.isEmpty());
    }
    QDir(box).removeRecursively();
    // ---- 第八道缝（这台机器的代理在哪、装东西的根在哪）在 POSIX 上的答案 ----
    // Windows 那半读注册表里那份 IE 设置，这里读 http_proxy 那一族环境变量：
    // 同一个问题、两套机制，不是谁降级给谁。这几条都用 qputenv 摆出现场，
    // 所以量的是规则本身，不依赖跑的人机器上有没有代理。
    qputenv("http_proxy", QByteArray());
    qputenv("HTTP_PROXY", QByteArray());
    qputenv("https_proxy", QByteArray());
    qputenv("HTTPS_PROXY", QByteArray());
    check("四个都空 = 这台机器没配系统代理，返回空（不许猜一个）", Platform::systemProxyUrl().isEmpty());
    qputenv("http_proxy", "127.0.0.1:7890");
    check("host:port 那种写法补成 http:// 再交给下游",
          Platform::systemProxyUrl() == QStringLiteral("http://127.0.0.1:7890"));
    qputenv("http_proxy", "socks5://127.0.0.1:7891");
    check("socks 不当 http 代理报（下游只会摆 QNetworkProxy::HttpProxy，报了就是骗人）",
          Platform::systemProxyUrl().isEmpty());
    qputenv("https_proxy", "http://proxy.internal:8443");
    qputenv("http_proxy", QByteArray());
    check("带方案的 URL 原样通过（这条规矩两边共用一份 normalizeProxy）",
          Platform::systemProxyUrl() == QStringLiteral("http://proxy.internal:8443"));
    qputenv("https_proxy", QByteArray());
    const QString posixRoot = QDir::homePath() + QStringLiteral("/software");
    check("安装根：留空 / Windows 那种盘符写法 / 相对路径都退到 ~/software",
          Platform::usableInstallRoot(QString()) == posixRoot
              && Platform::usableInstallRoot(QStringLiteral("D:\\software")) == posixRoot
              && Platform::usableInstallRoot(QStringLiteral("software")) == posixRoot);
    check("绝对路径原样用（那是用户明确指定的）",
          Platform::usableInstallRoot(QStringLiteral("/opt/dev")) == QStringLiteral("/opt/dev"));
std::printf("seam_probe: failed %d\n", bad);
    std::fflush(stdout);
    return bad;
}
EOF

SRC=$(cd "$(dirname "$0")/.." && pwd)
cd "$SRC"
g++ -std=c++20 -fPIC -Wall -Wextra -I src $(pkg-config --cflags Qt6Core Qt6Gui) \
    "$D/probe.cpp" src/platform/Paths_posix.cpp src/platform/Launch_posix.cpp \
    src/platform/Autostart_posix.cpp src/platform/ProcInfo_posix.cpp \
    src/platform/FileMeta_posix.cpp src/platform/FileOps_posix.cpp src/platform/SysConfig_posix.cpp \
    src/Pinyin.cpp \
    $(pkg-config --libs Qt6Core Qt6Gui) -o "$D/probe"
"$D/probe"
HOME=$(mktemp -d) "$D/probe" --autostart
