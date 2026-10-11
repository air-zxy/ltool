#include "Pinyin.h"

#include "pinyin_data.h"

#include <QByteArray>
#include <QHash>
#include <QStringList>
#include <QVector>

#include <algorithm>

namespace {

using Pinyin::kPinyinChunkCount;
using Pinyin::kPinyinChunks;
using Pinyin::kPinyinFirst;
using Pinyin::kPinyinRowCount;

// 每个码点的读法在表里占一行（"ren,lin"），行号 = 码点 - kPinyinFirst。
// 表在头文件里被切成几块（MSVC 单个字面量 65534 字节上限），这里一次性拼成一份
// 并只记下每行的起始偏移，之后查一个字就是两次数组。
struct Table
{
    QByteArray blob;
    QVector<uint> start;   // 行数 + 1 项，末项是整个 blob 的长度
    bool ok = false;
};

const Table& table()
{
    static const Table t = [] {
        Table x;
        for (unsigned i = 0; i < kPinyinChunkCount; ++i) {
            if (i)
                x.blob += '\n';   // 块是在整行边界断的，中间那个换行由这里补回来
            x.blob += Pinyin::kPinyinChunks[i];
        }
        const int n = x.blob.size();
        x.start.reserve(n / 6 + 2);
        x.start.append(0);
        for (int i = 0; i < n; ++i)
            if (x.blob[i] == '\n')
                x.start.append(uint(i + 1));
        x.start.append(uint(n));
        // 行数对不上就是表和生成器脱节了（改了表没重新生成、或者换行算错）。
        // 宁可整个拼音功能失效，也不要拿错行去匹配 —— 那会匹配出一堆不相干的名字。
        x.ok = (x.start.size() - 1) == int(kPinyinRowCount);
        return x;
    }();
    return t;
}

QByteArray rowOf(uint cp)
{
    const Table& t = table();
    if (!t.ok || cp < kPinyinFirst || cp - kPinyinFirst >= kPinyinRowCount)
        return {};
    const uint i = cp - kPinyinFirst;
    const uint a = t.start[i];
    const uint len = t.start[i + 1] - a - 1;   // 减掉行尾那个 '\n'
    return t.blob.mid(int(a), int(len));
}

// 一个字符的候选读音：汉字取表里所有读音；不是汉字（英文字母、数字、点）就按原样算一个候选，
// 这样"腾讯会议3"的全拼是 tengxunhuiyi3、首字母是 txhy3。
QVector<QString> variantsOf(QChar ch)
{
    const QByteArray row = rowOf(ch.unicode());
    if (row.isEmpty())
        return { QString(1, ch.toLower()) };
    QVector<QString> out;
    for (const QByteArray& part : row.split(','))
        if (!part.isEmpty())
            out.append(QString::fromUtf8(part));
    if (out.isEmpty())
        out.append(QString(1, ch.toLower()));
    return out;
}

struct Reading
{
    QStringList full;       // 全拼串（多音字会有多条）
    QStringList initials;   // 首字母串
};

constexpr int kMaxCombos = 64;

Reading expand(const QString& name)
{
    Reading r;
    QVector<QPair<QString, QString>> acc;   // (全拼, 首字母) 成对推进，免得两个列表下标对不上
    acc.append({ QString(), QString() });
    bool anyCjk = false;
    for (const QChar& ch : name) {
        if (!rowOf(ch.unicode()).isEmpty())
            anyCjk = true;
        QVector<QString> vs = variantsOf(ch);
        std::sort(vs.begin(), vs.end());
        vs.erase(std::unique(vs.begin(), vs.end()), vs.end());
        QVector<QPair<QString, QString>> next;
        next.reserve(qMin(kMaxCombos, acc.size() * vs.size()));
        for (const auto& cur : acc)
            for (const QString& v : vs)
                next.append({ cur.first + v, cur.second + v.left(1) });
        if (next.size() > kMaxCombos)
            next.resize(kMaxCombos);   // 组合爆炸就截断：留着前面的读音（表里第一个是主读音）
        acc = std::move(next);
    }
    // 名字里一个汉字都没有就不展开：纯英文名走拼音这条路只会和"子串匹配"重复，
    // 白白把 "oder" 这种提一档，反而把英文应用的原有排序搅乱。
    if (!anyCjk)
        return r;
    for (const auto& p : acc) {
        r.full << p.first;
        r.initials << p.second;
    }
    r.full.removeDuplicates();
    r.initials.removeDuplicates();
    return r;
}

const Reading& readings(const QString& name)
{
    static QHash<QString, Reading> cache;   // 名字重复率高（几百个应用 + 工具关键词），够便宜
    auto it = cache.constFind(name);
    if (it == cache.constEnd()) {
        if (cache.size() > 8000)
            cache.clear();
        it = cache.insert(name, expand(name));
    }
    return it.value();
}

} // namespace

namespace Pinyin {

bool usableQuery(const QString& low)
{
    if (low.size() < 2)
        return false;
    for (const QChar& c : low)
        if (c < QLatin1Char('a') || c > QLatin1Char('z'))
            return false;
    return true;
}

QStringList fullReadings(const QString& name)
{
    return readings(name).full;
}

QStringList initialReadings(const QString& name)
{
    return readings(name).initials;
}

// anchored=false：全拼允许出现在串的任何位置（"guanli" 要能在 "renwuguanliqi" 里命中）；
// anchored=true：全拼也必须从开头命中。首字母两档都只认开头，否则两个字母就能把整个列表捞一遍。
static Kind matchImpl(const QString& name, const QString& low, bool anchored)
{
    if (!usableQuery(low))
        return None;
    const Reading& r = readings(name);
    for (const QString& f : r.full)
        if (anchored ? f.startsWith(low) : f.contains(low))
            return Full;
    for (const QString& i : r.initials)
        if (i.startsWith(low))
            return Initial;
    return None;
}

Kind match(const QString& name, const QString& low)
{
    return matchImpl(name, low, false);
}

Kind matchStart(const QString& name, const QString& low)
{
    return matchImpl(name, low, true);
}

} // namespace Pinyin
