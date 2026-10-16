#!/usr/bin/env bash
# 平台层那两道缝在 Linux 侧的行为探针（src/platform/Paths_posix.cpp、Launch_posix.cpp）。
# 为什么要有：主仓库现在在 Linux 还 configure 不过（停在 find_package(unofficial-bit7z)），
# 所以 `--seamtest` 那 14 条在 Linux 上跑不起来，缝的 POSIX 那份就没有可复跑的证据。
# 这里只编那两个 .cpp + 一段 main，用系统的 Qt6Core，绕开整个工程。
# 跨平台 WP2 把工程编通之后，这个文件就该删掉 —— 那些判据会并进 --seamtest 一个入口。
#
# 跑法（Windows 侧）：wsl -e bash tools/seam_probe.sh
set -e
D=$(mktemp -d)
trap 'rm -rf "$D"' EXIT

cat > "$D/probe.cpp" <<'EOF'
#include "platform/Paths.h"
#include "platform/Launch.h"
#include <QCoreApplication>
#include <QDir>
#include <cstdio>
static int bad = 0;
static void check(const char* n, bool ok) {
    std::printf("%s %s\n", ok ? "PASS" : "FAIL", n);
    if (!ok) ++bad;
}
int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
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
    $(pkg-config --libs Qt6Core) -o "$D/probe"
"$D/probe"
