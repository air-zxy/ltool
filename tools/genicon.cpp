// 构建期图标生成器：产出 resources/ltool.ico（exe 与安装包共用这一份）
//
// 图标是画出来的，不是素材文件：圆角方块 + 左上→右下的蓝色渐变 + 白色 L。
// 每个尺寸各自按矢量参数重画一遍（不是把 256 缩下去），小尺寸才不会糊。
// ICO 里 7 条全用 PNG 载荷（Vista 之后 shell 就认这个，256 那条必须 PNG）。
#include <QBuffer>
#include <QByteArray>
#include <QColor>
#include <QFile>
#include <QFont>
#include <QGuiApplication>
#include <QImage>
#include <QLinearGradient>
#include <QPainter>
#include <QRectF>
#include <QString>

#include <cstdio>

namespace {

const int kSizes[] = {16, 24, 32, 48, 64, 128, 256};

QImage draw(int size)
{
    QImage img(size, size, QImage::Format_ARGB32_Premultiplied);
    img.fill(Qt::transparent);
    QPainter p(&img);
    p.setRenderHint(QPainter::Antialiasing);

    const qreal m = size / 32.0;                      // 四周留白
    const QRectF box(m, m, size - 2 * m, size - 2 * m);
    QLinearGradient g(box.topLeft(), box.bottomRight());
    g.setColorAt(0.0, QColor(78, 138, 255));
    g.setColorAt(1.0, QColor(47, 92, 215));
    p.setPen(Qt::NoPen);
    p.setBrush(g);
    p.drawRoundedRect(box, size * 3.0 / 16.0, size * 3.0 / 16.0);

    QFont f(QStringLiteral("Microsoft YaHei UI"));   // 和 Utils::appIcon() 同一个字面
    f.setPixelSize(qMax(1, int(size * 0.56)));
    f.setBold(true);
    p.setFont(f);
    p.setPen(QColor(255, 255, 255));
    p.drawText(box, Qt::AlignCenter, QStringLiteral("L"));
    p.end();
    return img;
}

QByteArray pngOf(int size)
{
    QByteArray out;
    QBuffer buf(&out);
    buf.open(QIODevice::WriteOnly);
    draw(size).save(&buf, "PNG");
    buf.close();
    return out;
}

void put16(QByteArray& a, quint16 v)
{
    a.append(reinterpret_cast<const char*>(&v), 2);
}

void put32(QByteArray& a, quint32 v)
{
    a.append(reinterpret_cast<const char*>(&v), 4);
}

} // namespace

int main(int argc, char** argv)
{
    QGuiApplication app(argc, argv);   // QPainter 画字要 QPA
    if (argc < 2) {
        std::fprintf(stderr, "用法: genicon <输出 .ico 路径>\n");
        return 2;
    }
    const QString target = QString::fromLocal8Bit(argv[1]);
    const int n = int(sizeof(kSizes) / sizeof(kSizes[0]));

    QByteArray head;
    put16(head, 0);                    // reserved
    put16(head, 1);                    // 类型 1 = 图标
    put16(head, quint16(n));

    QByteArray dir, images;
    quint32 offset = quint32(6 + 16 * n);
    for (int i = 0; i < n; ++i) {
        const int size = kSizes[i];
        const QByteArray png = pngOf(size);
        QByteArray e;
        e.append(char(size < 256 ? size : 0));   // 宽高：256 记 0
        e.append(char(size < 256 ? size : 0));
        e.append(char(0));                        // 颜色数：24 位以上留 0
        e.append(char(0));                        // 保留
        put16(e, 1);                              // 平面数
        put16(e, 32);                             // 位深
        put32(e, quint32(png.size()));
        put32(e, offset);
        dir += e;
        images += png;
        offset += quint32(png.size());
    }

    QFile f(target);
    if (!f.open(QIODevice::WriteOnly)) {
        std::fprintf(stderr, "genicon: 写不了 %s\n", qPrintable(target));
        return 1;
    }
    f.write(head + dir + images);
    f.close();
    std::printf("genicon: %s（%d 个尺寸，%lld 字节）\n", qPrintable(target), n,
                qint64(head.size() + dir.size() + images.size()));
    return 0;
}
