// 图片排版插件自带的判据：格数/列行、收图与拒收、换顺序、合成尺寸与像素、落盘。
//
// 全部离屏跑（不 show 窗口、不合成鼠标键盘事件）：拖放那条链用 addImages()/moveOrSwap()/
// removeAt() 直接驱动 —— 它们就是 dropEvent/mousePress 真正调的那三个函数，不是抄的第二遍。
// 合成图用肉眼看不出的方式验：拿纯色图，验某一格中心那个像素就是那张图的颜色。
#include "GridBoard.h"
#include "GridPage.h"
#include "Utils.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QPainter>
#include <QPushButton>
#include <QStringList>

#include <cstdio>

extern "C" Q_DECL_EXPORT int ltoolPluginSelftest()
{
    int bad = 0;
    const auto check = [&bad](const char* name, const QString& got, const QString& want) {
        std::printf("%s %s got=[%s] want=[%s]\n", got == want ? "PASS" : "FAIL", name,
                    got.toUtf8().constData(), want.toUtf8().constData());
        if (got != want)
            ++bad;
        std::fflush(stdout);
    };
    const auto baseNames = [](const QVector<QString>& paths) {
        QStringList out;
        for (const QString& p : paths)
            out << (p.isEmpty() ? QStringLiteral("-") : QFileInfo(p).completeBaseName());
        return out.join(QLatin1Char(','));
    };

    // 造九张纯色图 + 一个假图片文件（后缀对、内容不是图）到临时目录
    const QString box = QDir::tempPath() + QStringLiteral("/ltool-grid-selftest");
    QDir(box).removeRecursively();
    QDir().mkpath(box);
    const QVector<QColor> colors = { QColor(220, 30, 30), QColor(30, 200, 30), QColor(30, 30, 210),
                                     QColor(230, 200, 20), QColor(200, 40, 200), QColor(20, 200, 200),
                                     QColor(120, 60, 30), QColor(60, 120, 240), QColor(240, 120, 60) };
    QStringList files;
    for (int i = 0; i < colors.size(); ++i) {
        QImage im(64, 40, QImage::Format_RGB32);
        im.fill(colors[i]);
        const QString f = box + QStringLiteral("/c%1.png").arg(i);
        im.save(f, "PNG");
        files << f;
    }
    const QString fake = box + QStringLiteral("/notimage.txt");
    {
        QFile f(fake);
        f.open(QIODevice::WriteOnly);
        f.write("这不是图片");
    }

    GridPage page;
    page.setAttribute(Qt::WA_DontShowOnScreen, true);
    page.resize(720, 720);

    check("默认四宫", QString::number(page.slotCount()), "4");
    check("四宫 = 2 列 2 行", QStringLiteral("%1x%2").arg(page.board()->cols()).arg(page.board()->rows()), "2x2");
    page.setSlots(6);
    check("六宫 = 3 列 2 行", QStringLiteral("%1x%2").arg(page.board()->cols()).arg(page.board()->rows()), "3x2");
    page.setSlots(9);
    check("九宫 = 3 列 3 行", QStringLiteral("%1x%2").arg(page.board()->cols()).arg(page.board()->rows()), "3x3");
    // 换格数只截断，不重排：九格里摆满再退回四宫，前面四张必须还在原位
    page.board()->clearImages();
    page.board()->addImages(files);
    page.setSlots(4);
    check("换格数不丢前面摆好的图（9→4 还是那四张）", baseNames(page.board()->paths()), "c0,c1,c2,c3");


    page.setSlots(4);
    page.board()->clearImages();
    page.board()->addImages(files);            // 四格收九张
    check("四格只收四张，其余算拒收", QStringLiteral("%1/%2").arg(page.board()->filled()).arg(page.board()->rejectedCount()), "4/5");
    check("收进来的顺序就是给定顺序", baseNames(page.board()->paths()), "c0,c1,c2,c3");
    page.board()->clearImages();
    page.board()->addImages({ fake });
    check("非图片后缀一个都不收", QString::number(page.board()->filled()), "0");
    page.board()->clearImages();
    page.board()->addImages(files.mid(0, 3));
    page.board()->moveOrSwap(0, 3);            // 目标是空格 = 挪过去
    check("拖到空格 = 挪过去（原来那格空出来）", baseNames(page.board()->paths()), "-,c1,c2,c0");
    page.board()->moveOrSwap(1, 2);            // 两边都有图 = 交换
    check("拖到有图的格 = 交换", baseNames(page.board()->paths()), "-,c2,c1,c0");
    page.board()->moveOrSwap(0, 1);            // 空格当起点 = 什么都不动
    check("空格拖不动（不会把空格盖到别的格上）", baseNames(page.board()->paths()), "-,c2,c1,c0");
    page.board()->removeAt(2);
    check("拿掉一格只空那一格", baseNames(page.board()->paths()), "-,c2,-,c0");

    // 合成：四宫、每格 60、缝 10、背景红
    page.setSlots(4);
    page.board()->clearImages();
    page.board()->addImages(files.mid(0, 4));
    const QImage out = page.compose(60, 10, QColor(255, 0, 0));
    check("四宫合成尺寸 = 60*2 + 10*3", QStringLiteral("%1x%2").arg(out.width()).arg(out.height()), "150x150");
    check("四边和缝都是背景色", out.pixelColor(3, 3).name(QColor::HexRgb), QStringLiteral("#ff0000"));
    check("第一格中心 = 第一张图的颜色", out.pixelColor(40, 40).name(QColor::HexRgb), colors[0].name(QColor::HexRgb));
    check("第四格中心 = 第四张图的颜色", out.pixelColor(110, 110).name(QColor::HexRgb), colors[3].name(QColor::HexRgb));
    page.board()->removeAt(1);
    const QImage hole = page.compose(60, 10, QColor(255, 0, 0));
    check("空格子就是背景色（不是黑的）", hole.pixelColor(110, 40).name(QColor::HexRgb), QStringLiteral("#ff0000"));

    // 裁的是中间那块正方形，不是把整张压扁：拿一张 120x40 的横图（红｜绿｜蓝 三条竖带），
    // 中间那条正好是 40x40 —— 压扁的话左右两边会漏出红和蓝。
    const QString band = box + QStringLiteral("/band.png");
    {
        QImage im(120, 40, QImage::Format_RGB32);
        im.fill(QColor(30, 200, 30));
        QPainter bp(&im);
        bp.fillRect(0, 0, 40, 40, QColor(220, 30, 30));
        bp.fillRect(80, 0, 40, 40, QColor(30, 30, 210));
        bp.end();
        im.save(band, "PNG");
    }
    page.board()->clearImages();
    page.board()->addImages({ band });
    const QImage wide = page.compose(60, 10, QColor(255, 0, 0));
    check("横图合成只留中间那块（左边不是红）", wide.pixelColor(12, 40).name(QColor::HexRgb),
          QStringLiteral("#1ec81e"));
    check("横图合成只留中间那块（右边不是蓝）", wide.pixelColor(68, 40).name(QColor::HexRgb),
          QStringLiteral("#1ec81e"));

    // 板上预览必须是同一块构图，否则人摆好的和生成出来的是两张图
    page.board()->setFixedSize(300, 300);        // 四宫：留白 8、缝 8 → 每格 138 见方
    page.board()->ensurePolished();
    const QImage shot = page.board()->grab().toImage();
    check("板上预览也是中间那块（左边不是红）", shot.pixelColor(13, 77).name(QColor::HexRgb),
          QStringLiteral("#1ec81e"));
    check("板上预览也是中间那块（右边不是蓝）", shot.pixelColor(141, 77).name(QColor::HexRgb),
          QStringLiteral("#1ec81e"));
    // 这条是探针自己的体检：离屏 grab 万一画出来是张空图，上面两条会"一起绿"骗人。
    // ✕ 那颗角标是深色半透明底，压在绿图上一定比图本身暗。
    check("板上的角标真的画出来了（grab 不是空图）",
          shot.pixelColor(130, 23).green() < shot.pixelColor(77, 77).green() ? "yes" : "no", "yes");
    page.board()->setMinimumSize(320, 320);
    page.board()->setMaximumSize(QSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX));

    // 坏文件：不存在的 .png 不收；后缀对、内容不是图的收进来但要说实话
    page.board()->clearImages();
    page.board()->addImages({ box + QStringLiteral("/nope.png") });
    check("盘上没有这个文件就不收（不会摆一个空位骗人）", QString::number(page.board()->filled()), "0");
    const QString broken = box + QStringLiteral("/broken.png");
    {
        QFile f(broken);
        f.open(QIODevice::WriteOnly);
        f.write("这不是 PNG 的字节");
    }
    page.board()->addImages({ broken });
    check("后缀对的坏文件先进格子（要让人看到自己拖错了）", QString::number(page.board()->filled()), "1");
    const QImage badCell = page.compose(60, 10, QColor(255, 0, 0));
    check("坏文件那一格合成时留背景色（不画成黑的）", badCell.pixelColor(40, 40).name(QColor::HexRgb),
          QStringLiteral("#ff0000"));
    const QImage badShot = page.board()->grab().toImage();
    const QRect s0 = page.board()->cellRect(0), s1 = page.board()->cellRect(1);
    check("坏图格与空格这两处采的不是同一个像素（不然这条是白过的）",
          s0.right() < s1.left() ? "yes" : "no", "yes");
    check("坏文件那一格和空格子画得一样素（不是一张黑块）",
          badShot.pixelColor(s0.left() + 5, s0.top() + s0.height() / 4).rgb()
              == badShot.pixelColor(s1.left() + 5, s1.top() + s1.height() / 4).rgb()
              ? "yes" : "no", "yes");

    // 一张都没有的时候不许生成（要说一句人话，别写出全背景的空图）
    page.board()->clearImages();
    check("空板生成回空串（不写文件）", page.generate().isEmpty() ? "yes" : "no", "yes");

    // 真落盘一次：文件名、尺寸、然后删掉自己造的这个文件
    page.board()->addImages(files.mid(0, 4));
    const QString file = page.generate();
    check("生成写出了真文件", QFileInfo::exists(file) && QFileInfo(file).size() > 0 ? "yes" : "no", "yes");
    check("文件名是 grid-<时间>-<n>宫.png", QFileInfo(file).fileName().startsWith(QStringLiteral("grid-"))
                  && QFileInfo(file).fileName().endsWith(QStringLiteral("4宫.png")) ? "yes" : "no", "yes");
    const QString dir = Utils::capturesDir();
    if (file.startsWith(dir) && QFileInfo(file).fileName().startsWith(QStringLiteral("grid-")))
        QFile::remove(file);                    // 只删自己刚算出来那一个，不碰目录里别的
    check("生成之后「打开这张」那颗该能点", page.findChild<QPushButton*>(QStringLiteral("gridOpen"))
                  && page.findChild<QPushButton*>(QStringLiteral("gridOpen"))->isEnabled() ? "yes" : "no", "yes");


    // 板子被拉成扁的/长的，格子也必须是正方形并且整块居中 —— 出图就是一格一正方形，
    // 板子上画成长条等于"看到的和拿到的不是同一张图"（预览会横向拉扁）。
    page.board()->setFixedSize(500, 300);
    page.setSlots(9);
    page.board()->clearImages();
    page.board()->addImages({ band });          // 那张 120x40 的红|绿|蓝横条，中间是绿的
    page.board()->ensurePolished();
    const QRect g0 = page.board()->cellRect(0);
    const QRect g2 = page.board()->cellRect(2);
    check("扁板子里的格子还是正方形", g0.width() == g0.height() ? "yes" : "no", "yes");
    // 居中这条要能分清"整块缩成正方形后摆在中间"和"格子拉长填满板子"两种写法：
    // 只断言左右对称的话两种都绿（那等于没测），所以这里量的是左边那块留白有多大。
    check("扁板子里整块格子居中（左右留白一样，而且真的留了白）",
          qAbs(g0.left() - (500 - 1 - g2.right())) <= 1 && g0.left() > 40 ? "yes" : "no", "yes");
    check("扁板子里那一格照样画满了图（不是中间一块黑）",
          page.board()->grab().toImage().pixelColor(g0.center()).name(QColor::HexRgb),
          QStringLiteral("#1ec81e"));
    page.board()->setMinimumSize(320, 320);
    page.board()->setMaximumSize(QSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX));

    QDir(box).removeRecursively();              // 这次自测造的临时图，绝对路径算出来的
    std::printf("grid: 失败 %d 项\n", bad);
    std::fflush(stdout);
    return bad;
}
