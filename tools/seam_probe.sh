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
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
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
    std::printf("seam_probe: failed %d\n", bad);
    std::fflush(stdout);
    return bad;
}
EOF

SRC=$(cd "$(dirname "$0")/.." && pwd)
cd "$SRC"
g++ -std=c++20 -fPIC -Wall -Wextra -I src $(pkg-config --cflags Qt6Core) \
    "$D/probe.cpp" src/platform/Paths_posix.cpp src/platform/Launch_posix.cpp \
    src/platform/Autostart_posix.cpp src/Pinyin.cpp $(pkg-config --libs Qt6Core) -o "$D/probe"
"$D/probe"
HOME=$(mktemp -d) "$D/probe" --autostart
