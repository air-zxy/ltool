// 剪贴板插件自带的判据：过滤那条纯函数、页面"行号 ↔ 存储下标"的映射、状态行说实话、
// 双击复用 / Delete 删这条 / 清空 三条动作真的落到数据上。
//
// 全在离屏跑，数据用沙箱 APPDATA 里自造的 clipboard.json（main.cpp 那串白名单会把
// --cliptest 的数据目录挪到临时目录），一条都不碰他真实的剪贴板历史。
// 夹具里 clipboard/enable 留 false：那样引擎 2 秒轮询会在 capture() 第一行就返回，
// 不会因为他真实剪贴板动了一下就往我的夹具前面插一条、把下标全冲乱。
// clipboard/keepDays 也钉成 0：产品默认是 7 天，不钉住的话夹具里那条 400 天前的会被
// load() 当场裁掉，「过滤 / 行号」那 36 条就全踩在一份被动过的数据上。
#include "ClipboardPage.h"
#include "ClipboardStore.h"
#include "Utils.h"

#include <QApplication>
#include <QClipboard>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeyEvent>
#include <QLabel>
#include <QLayout>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QSettings>
#include <QStringList>
#include <QTimer>

#include <cstdio>
#include <functional>

namespace {

bool reachedFrom(QWidget* root, QWidget* w)
{
    bool found = false;
    std::function<void(QLayout*)> walk = [&](QLayout* l) {
        if (!l || found)
            return;
        for (int i = 0; i < l->count(); ++i) {
            QLayoutItem* it = l->itemAt(i);
            if (!it)
                continue;
            if (QWidget* cw = it->widget()) {
                if (cw == w) { found = true; return; }
                walk(cw->layout());
            } else if (QLayout* sub = it->layout())
                walk(sub);
        }
    };
    walk(root->layout());
    return found;
}

// 每一行显示的必须是**它自己那个 UserRole 下标**对应的那条。
// 这就是"行号当用下标"的照妖镜：过滤之后两者不相等，写成 row 会指着别的一条。
QString rowsAgreeWithTheirIndexes(const QVector<ClipboardStore::Item>& items, const ClipboardPage& page)
{
    for (int row = 0; row < page.rowCount(); ++row) {
        const int idx = page.storeIndexAt(row);
        if (idx < 0 || idx >= items.size())
            return QStringLiteral("第%1行的下标 %2 越界").arg(row).arg(idx);
        const QString shown = page.rowTextAt(row);
        if (!shown.contains(ClipboardStore::summary(items[idx])))
            return QStringLiteral("第%1行显示的不是 items[%2] 那条").arg(row).arg(idx);
    }
    return QStringLiteral("ok");
}

QJsonObject textItem(const QString& when, const QString& hash, const QString& text)
{
    QJsonObject o;
    o.insert("when", when);
    o.insert("hash", hash);
    o.insert("image", false);
    o.insert("text", text);
    o.insert("bytes", double(text.size()));
    return o;
}

} // namespace

extern "C" Q_DECL_EXPORT int ltoolPluginSelftest()
{
    int bad = 0;
    const auto check = [&bad](const char* name, const QString& got, const QString& want) {
        std::printf("%s %s got=[%s] want=[%s]\n", got == want ? "PASS" : "FAIL", name,
                    got.toUtf8().constData(), want.toUtf8().constData());
        if (got != want)
            ++bad;
    };
    const auto list = [](const QVector<int>& v) {
        QStringList s;
        for (const int i : v)
            s << QString::number(i);
        return s.join(QLatin1Char(','));
    };

    // ---- 夹具：写进沙箱数据目录，然后让 core 那侧自己 load() ----
    // 时间戳一律按「现在往前推」算出来，不写死日期：这条功能上线后超龄的会被裁掉，
    // 写死 "2026-05-04" 那种夹具会在某一天自己变成超龄条目，让上面那 36 条莫名其妙红一片。
    auto& cs = ClipboardStore::instance();
    const QString data = Utils::dataDir();
    const QString pngPath = QDir::toNativeSeparators(data + "/clipimg/selftest.png");
    const QDateTime now = QDateTime::currentDateTime();
    const auto stamp = [](const QDateTime& t) { return t.toString(Qt::ISODate); };
    const QString freshStamp = stamp(now.addSecs(-60));
    const QString hourStamp = stamp(now.addSecs(-3600));
    const QString yesterday = stamp(now.addDays(-1));
    const QString ancient = stamp(now.addDays(-400));
    const QString ancientDay = ancient.left(10);      // 给「按日期筛」那一条当关键字用
    bool fixtureOk = false;
    if (QDir().mkpath(data + "/clipimg")) {
        QImage img(1280, 720, QImage::Format_RGB32);
        img.fill(QColor(20, 90, 200));
        if (img.save(pngPath, "PNG")) {
            QJsonObject o;
            o.insert("when", freshStamp);
            o.insert("hash", "h0");
            o.insert("image", true);
            o.insert("path", pngPath);
            o.insert("w", 1280);
            o.insert("h", 720);
            o.insert("bytes", 34560.0);
            QJsonArray arr;
            arr.append(o);                                        // 0：一张图
            arr.append(textItem(hourStamp, "h1", "ZZZclip 甲 alpha"));
            arr.append(textItem(yesterday, "h2", "第一行很普通\n第二行里有 zzzclipbeta"));
            arr.append(textItem(ancient, "h3", "普通一条 ZZZ"));
            QFile f(data + "/clipboard.json");
            if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
                f.write(QJsonDocument(arr).toJson(QJsonDocument::Compact));
                f.close();
                fixtureOk = true;
            }
        }
    }
    check("夹具写进了沙箱数据目录（写不进去后面全是废判据）",
          fixtureOk ? QStringLiteral("ok") : QStringLiteral("fail at %1").arg(data), "ok");

    // 沙箱配置里把两条上限钉死：enable=false（真实剪贴板别往夹具里插东西）、
    // keepDays=0（上面那 36 条测的是过滤和行号，不能被天数规则把夹具裁了）。
    // 天数自己的行为在下面「保留天数」那一段单独测。
    {
        QSettings cfg(data + "/config.ini", QSettings::IniFormat);
        cfg.setValue(QStringLiteral("clipboard/enable"), false);
        cfg.setValue(QStringLiteral("clipboard/keepDays"), 0);
        cfg.sync();
    }

    cs.start();                       // load() 读那份夹具（enable/keepDays 都按上面钉的来）
    cs.setEnabled(false);             // start() 之后强制关掉收集：真实剪贴板别往夹具里插东西
    const QVector<ClipboardStore::Item> items = cs.items();
    check("夹具四条都进来了", QString::number(items.size()), "4");

    // ---- 纯函数：过滤 ----
    check("空关键字 = 全部", list(ClipboardPage::filterIndices(items, QString())), "0,1,2,3");
    check("只有空格也算空关键字（别一条都不显示）",
          list(ClipboardPage::filterIndices(items, QStringLiteral("   "))), "0,1,2,3");
    // 整个插件最容易写错的一格：过滤后只剩一条时行号是 0，而它在存储里是第 1 条。
    check("命中一条时返回的是**存储下标**不是行号（alpha → 1，不是 0）",
          list(ClipboardPage::filterIndices(items, QStringLiteral("alpha"))), "1");
    check("一个词命中两条 → 两条下标都在、且按原顺序",
          list(ClipboardPage::filterIndices(items, QStringLiteral("ZZZclip"))), "1,2");
    check("大小写不管（ALPHA 一样命中 1）",
          list(ClipboardPage::filterIndices(items, QStringLiteral("ALPHA"))), "1");
    check("前后空格先 trim 再筛",
          list(ClipboardPage::filterIndices(items, QStringLiteral("  alpha  "))), "1");
    check("中文关键字照样能筛",
          list(ClipboardPage::filterIndices(items, QStringLiteral("普通"))), "2,3");
    check("命不中就一条都不给",
          list(ClipboardPage::filterIndices(items, QStringLiteral("zzzznope"))), QString());
    check("图片条目能按「图片」筛出来",
          list(ClipboardPage::filterIndices(items, QStringLiteral("图片"))), "0");
    check("图片条目还能按尺寸筛（1280）",
          list(ClipboardPage::filterIndices(items, QStringLiteral("1280"))), "0");
    check("搜的是整段而不是首行（第二行里的词也算）",
          list(ClipboardPage::filterIndices(items, QStringLiteral("zzzclipbeta"))), "2");
    check("能按日期筛（那条 400 天前的，日期是算出来的不是写死的）",
          list(ClipboardPage::filterIndices(items, ancientDay)), "3");
    {
        // zzz 同时出现在 1/2/3 三条里，而第 2 条的**摘要和正文里都有** —— 不许重复列
        const QVector<int> many = ClipboardPage::filterIndices(items, QStringLiteral("zzz"));
        QStringList uniq = QStringList();
        for (const int i : many)
            if (!uniq.contains(QString::number(i)))
                uniq << QString::number(i);
        check("一条命中多处也只出现一次", list(many) == uniq.join(QLatin1Char(',')) ? "yes" : "no", "yes");
    }

    // ---- 页面 ----
    ClipboardPage page;
    auto* status = page.findChild<QLabel*>(QStringLiteral("clipStatus"));
    auto* filter = page.findChild<QLineEdit*>(QStringLiteral("clipFilter"));
    auto* listw = page.findChild<QListWidget*>(QStringLiteral("clipList"));
    auto* clearBtn = page.findChild<QPushButton*>(QStringLiteral("clipClear"));
    auto* hint = page.findChild<QLabel*>(QStringLiteral("clipHint"));
    check("状态行摆在布局里（不是 new 出来就算）", reachedFrom(&page, status) ? "yes" : "no", "yes");
    check("过滤框摆在布局里", reachedFrom(&page, filter) ? "yes" : "no", "yes");
    check("列表摆在布局里", reachedFrom(&page, listw) ? "yes" : "no", "yes");
    check("清空那颗摆在布局里", reachedFrom(&page, clearBtn) ? "yes" : "no", "yes");
    check("说明那行摆在布局里", reachedFrom(&page, hint) ? "yes" : "no", "yes");

    check("没过滤时列表四行", QString::number(page.rowCount()), "4");
    check("每一行都对得上自己的下标", rowsAgreeWithTheirIndexes(cs.items(), page), "ok");
    filter->setText(QStringLiteral("alpha"));
    check("过滤成一条之后列表只剩一行", QString::number(page.rowCount()), "1");
    check("过滤后第 0 行映射回的下标是 1（不是 0）", QString::number(page.storeIndexAt(0)), "1");
    check("过滤状态下每一行仍对得上", rowsAgreeWithTheirIndexes(cs.items(), page), "ok");
    check("状态行跟着报过滤出的条数",
          page.statusText().contains(QStringLiteral("过滤出 1 条")) ? "yes" : "no", "yes");
    check("越界问下标老实给 -1（不崩）",
          QStringLiteral("%1,%2").arg(page.storeIndexAt(-1)).arg(page.storeIndexAt(999)), "-1,-1");

    // 复用走的是这一行自己的下标：过滤状态下复用第 0 行，进剪贴板的必须是那条文本
    cs.restore(page.storeIndexAt(0));
    QApplication::processEvents();
    check("复用过滤后的那一行，进剪贴板的是它本身而不是 items[0]",
          QApplication::clipboard()->text(), QStringLiteral("ZZZclip 甲 alpha"));

    // Delete：选中第 0 行（过滤状态下的那条 = items[1]），送一个真按键事件给列表。
    // **必须在下面那段 setEnabled(true) 之前** —— 一打开收集，capture() 就会把剪贴板里
    // 那条补进夹具、把固定 4 条的下标冲乱（上一版就是这么让这两条假红的）。
    listw->setCurrentRow(0);
    QKeyEvent del(QEvent::KeyPress, Qt::Key_Delete, Qt::NoModifier);
    QApplication::sendEvent(listw, &del);
    check("按 Delete 把选中的那条从列表里拿掉了", QString::number(page.rowCount()), "0");
    filter->setText(QString());
    check("剩下的还是三条（删的正是那一条，不是一整片）", QString::number(page.rowCount()), "3");
    check("删完之后每一行仍对得上（下标跟着重排了）",
          rowsAgreeWithTheirIndexes(cs.items(), page), "ok");

    // ---- 保留天数：先问那个纯谓词，再让引擎当场裁一次 ----
    // 边界要说清楚：7 天 = 整 7 天那天就清掉（"保留 7 天"不该留到第 8 天）。
    const QDateTime then = QDateTime::currentDateTime();
    const auto mk = [](const QDateTime& when, const QString& hash) {
        ClipboardStore::Item it;
        it.when = when;
        it.hash = hash;
        it.text = QStringLiteral("x");
        return it;
    };
    const auto yes = [](bool b) { return b ? QStringLiteral("yes") : QStringLiteral("no"); };
    check("差 1 秒到 7 天还不算超龄",
          yes(!ClipboardStore::expired(mk(then.addSecs(-7 * 86400 + 1), "a"), 7, then)), "yes");
    check("整 7 天算超龄", yes(ClipboardStore::expired(mk(then.addSecs(-7 * 86400), "b"), 7, then)), "yes");
    check("天数=0 时再久都不算超龄",
          yes(!ClipboardStore::expired(mk(then.addDays(-4000), "c"), 0, then)), "yes");
    check("时间戳读不出来按超龄处理（界面本来也画不出它是哪天复制的）",
          yes(ClipboardStore::expired(mk(QDateTime(), "d"), 7, then)), "yes");

    const auto hashes = [](const QVector<ClipboardStore::Item>& v) {
        QStringList s;
        for (const ClipboardStore::Item& it : v)
            s << it.hash;
        return s.join(QLatin1Char(','));
    };
    const auto cfgKeepDays = [&data] {
        return QSettings(data + "/config.ini", QSettings::IniFormat)
            .value(QStringLiteral("clipboard/keepDays")).toString();
    };

    cs.setKeepDays(7);
    check("改成 7 天：超龄那条当场从数据里没了", QString::number(cs.items().size()), "2");
    check("活下来的正是那张图和一天前那条（顺序也还在前）", hashes(cs.items()), "h0,h2");
    check("留下的那张图 png 还在（夹具存的是反斜杠写法，孤儿判定不归一化就会把它当没用的删掉）",
          QFile::exists(pngPath) ? "yes" : "no", "yes");
    check("天数写进了配置文件（重启还认）", cfgKeepDays(), "7");

    cs.setKeepDays(0);
    check("改回「不限」不会把裁掉的找回来（那是真删了）", QString::number(cs.items().size()), "2");
    check("配置文件跟着回到 0", cfgKeepDays(), "0");

    // ---- 孤儿图判定：那道闸门和那步归一化都要能直接问 ----
    // 为什么这几条存在：2026-10-16 那次"把 keep 里的路径归一化了一下"只对了一半，
    // 另一侧还是拼出来的混合写法，两边永远不相等 —— 启动那一趟把 49 张**还在用**的图
    // 全判成孤儿删掉了。这种错只在真数据上看得见，所以把它抽成可以直接问的两个函数。
    check("读到 0 条历史时不许启动批量删图",
          ClipboardStore::sweepAllowed(0) ? "yes" : "no", "no");
    check("只剩一条也照常清（不然孤儿图永远堆在盘上）",
          ClipboardStore::sweepAllowed(1) ? "yes" : "no", "yes");
    check("正斜杠和反斜杠写法在判定里算同一个文件",
          ClipboardStore::keepKey(QStringLiteral("C:/a/b/clipimg/x.png"))
              == ClipboardStore::keepKey(QStringLiteral("C:\\a\\b\\clipimg\\x.png")) ? "yes" : "no", "yes");
    check("连续斜杠折叠、大小写折叠（APPDATA 本身是反斜杠 + 大小写不定）",
          ClipboardStore::keepKey(QStringLiteral("C://A/B/x.png"))
              == ClipboardStore::keepKey(QStringLiteral("c:\\a\\b\\X.png")) ? "yes" : "no", "yes");
    check("两个不同的文件不许被归一成同一个（那等于永不删）",
          ClipboardStore::keepKey(QStringLiteral("C:/a/b/x.png"))
              == ClipboardStore::keepKey(QStringLiteral("C:/a/b/y.png")) ? "yes" : "no", "no");
    const QString strayPath = QDir::toNativeSeparators(data + "/clipimg/stray.png");
    {
        QImage s(8, 8, QImage::Format_RGB32);
        s.fill(QColor(255, 0, 0));
        s.save(strayPath, "PNG");
    }
    check("孤儿图夹具造好了（没人引用它）", QFile::exists(strayPath) ? "yes" : "no", "yes");
    cs.setKeepDays(7);      // 触发一次 prune：两条都还没超龄，但这一趟要把 stray 收掉
    check("没人引用的那张被清掉了", QFile::exists(strayPath) ? "yes" : "no", "no");
    check("还在历史里的那张没被牵连（两条都写混了路径也一样）",
          QFile::exists(pngPath) ? "yes" : "no", "yes");
    check("这一趟没顺带删掉任何历史条目", QString::number(cs.items().size()), "2");
    cs.setKeepDays(0);

    // 清空
    clearBtn->click();
    check("点清空之后列表空了", QString::number(page.rowCount()), "0");
    check("清空是真的清掉了数据（不是只藏起来）", QString::number(cs.items().size()), "0");
    check("清空之后状态行还在报 0 条（不写\"没在记录\"以外的假话）",
          page.statusText().contains(QStringLiteral("0 条")) ? "yes" : "no", "yes");

    // 状态行要说实话：关着不能说"正在记录"，开着不能说"没在记录"。放最后做，
    // 因为 setEnabled(true) 会真的开始收集、可能往数据里补条目（见上面那条注释）。
    check("没在记录时状态行写「没在记录」，并指路开关在哪",
          page.statusText().contains(QStringLiteral("没在记录")) ? "yes" : "no", "yes");
    cs.setEnabled(true);
    check("打开记录之后状态行跟着写「正在记录」",
          page.statusText().contains(QStringLiteral("正在记录")) ? "yes" : "no", "yes");
    check("天数是 0 时状态行写「天数不限」，不许只报条数",
          page.statusText().contains(QStringLiteral("天数不限")) ? "yes" : "no", "yes");
    cs.setEnabled(false);

    // ---- 自写不重入：只有真剪贴板才测得出来 ----
    // 盯的是 2026-10-16 那次闪退的形状：restore() 往剪贴板写的那一下，在真实 Windows 上也会
    // 回发一个 dataChanged（外壳把所有窗口都通知，包括写的那个），旧接法在同一个线程里立刻
    // 回头收自己刚写的内容 —— 既白重编码一遍（图片是几百毫秒的"点一下卡住"），又会在
    // restore 还拿着 m_items 引用的时候把这个 vector 挪走。
    // **offscreen 的 clipboard 是个内存桩**：setText 不发 dataChanged、也不碰真剪贴板，
    // 所以这几条跑在离屏里必然"绿得毫无意义"，这里点名跳过（同 --recordui 要真屏幕那条规矩）。
    if (QApplication::platformName() == QLatin1String("offscreen")) {
        std::printf("SKIP 自写不重入这 4 条：offscreen 的剪贴板是桩，测不到（要 QT_QPA_PLATFORM=windows 跑）\n");
    } else {
        cs.setEnabled(true);
        // 收自己那一路现在推迟了一个事件循环回合（见 ClipboardStore::start 里那段注释），
        // 一次 processEvents() 里刚挂上的 0ms 定时器不一定算过期，所以真的转一圈事件循环。
        const auto spin = [](int msec) {
            QEventLoop loop;
            QTimer::singleShot(msec, &loop, &QEventLoop::quit);
            loop.exec();
        };
        const QString clipBackup = QApplication::clipboard()->text();
        QApplication::clipboard()->setText(QStringLiteral("ZZZreentr 甲"));
        spin(80);
        QApplication::clipboard()->setText(QStringLiteral("ZZZreentr 乙"));
        spin(80);
        check("开着收集时外部复制的文本收得到（推迟一个回合之后这条路仍然通）",
              cs.items().size() >= 2 ? "yes" : "no", "yes");
        check("两条的顺序是新的在前", cs.items().isEmpty() ? QString() : cs.items().at(0).text,
              QStringLiteral("ZZZreentr 乙"));
        if (cs.items().size() >= 2) {
            const QString headBefore = cs.items().first().text;
            const QString target = cs.items().at(1).text;
            const int nBefore = static_cast<int>(cs.items().size());
            cs.restore(1);                   // 复用中间那条：会往剪贴板写，等于自我通知一次
            spin(120);
            check("复用放进剪贴板的就是那一条本身", QApplication::clipboard()->text(), target);
            check("自我那次写不把历史顺序搅动（旧写法会把它重新置顶）",
                  cs.items().first().text == headBefore ? "yes" : "no", "yes");
            check("自我那次写也不往历史里多塞一条重复内容", QString::number(cs.items().size()),
                  QString::number(nBefore));
        }
        cs.setEnabled(false);
        QApplication::clipboard()->setText(clipBackup);   // 这条路上动过真剪贴板，尽量原样还回去
    }

    std::printf("clipboard: 失败 %d 项\n", bad);
    std::fflush(stdout);
    return bad == 0 ? 0 : 1;
}
