import io

P = r'D:\project\cpp\ltool\src\FileIndexer.cpp'
s = io.open(P, encoding='utf-8').read()

# --- 1) faster inner search: needle is already lowercase, fold only ASCII in haystack ---
old_ci = '''int indexOfCI(const char* hay, int hayLen, const char* needle, int needleLen)
{
    if (needleLen == 0)
        return 0;
    if (needleLen > hayLen)
        return -1;
    for (int i = 0; i + needleLen <= hayLen; ++i) {
        int j = 0;
        for (; j < needleLen; ++j) {
            if (tolower((unsigned char)hay[i + j]) != tolower((unsigned char)needle[j]))
                break;
        }
        if (j == needleLen)
            return i;
    }
    return -1;
}'''
new_ci = '''inline char lowAscii(char c)
{
    return (c >= 'A' && c <= 'Z') ? char(c - 'A' + 'a') : c;
}

// needle 必须已是小写（调用方传 query.toLower()）。首字节先按两种大小写排除，
// 能对每个名字省掉几十次逐字节折叠；非 ASCII（中文 UTF-8 字节 >=0x80）不做折叠，
// 与原先 C locale tolower 的行为一致
int indexOfCI(const char* hay, int hayLen, const char* needle, int needleLen)
{
    if (needleLen <= 0)
        return 0;
    if (needleLen > hayLen)
        return -1;
    const char first = needle[0];
    const char firstAlt = (first >= 'a' && first <= 'z') ? char(first - 'a' + 'A') : first;
    const int last = hayLen - needleLen;
    for (int i = 0; i <= last; ++i) {
        const char c = hay[i];
        if (c != first && c != firstAlt)
            continue;
        int j = 1;
        for (; j < needleLen; ++j)
            if (lowAscii(hay[i + j]) != needle[j])
                break;
        if (j == needleLen)
            return i;
    }
    return -1;
}'''
assert old_ci in s
s = s.replace(old_ci, new_ci, 1)

# --- 2) parallel deep scan (std::thread: search() may itself run in the QtConcurrent pool) ---
old_scan = '''        for (const VolRuntime& v : m_vols) {
            const char* base = v.arena.constData();
            for (const FileDeepEntry& de : v.entries) {
                const char* nameStart = base + de.offset + de.nameStart;
                const int nameLen = int(de.len) - int(de.nameStart);
                const int idx = indexOfCI(nameStart, nameLen, q8.constData(), int(q8.size()));
                if (idx < 0)
                    continue;
                offer({1, (idx == 0 ? 0 : 2) + (de.isDir ? 0 : 1), nameLen,
                       nullptr, &de, base});
            }
        }
'''
new_scan = '''        // 深索引按段并行扫。用 std::thread 而不是 QtConcurrent：本函数可能已经跑在
        // 线程池里，再向同一个池派活并等待会有重入风险。
        struct Seg {
            const char* arena;
            const FileDeepEntry* from;
            const FileDeepEntry* to;
        };
        QVector<Seg> segs;
        for (const VolRuntime& v : m_vols) {
            const int n = v.entries.size();
            if (n == 0)
                continue;
            const FileDeepEntry* e = v.entries.constData();
            const int parts = qBound(1, n / 250000, 4);
            for (int pi = 0; pi < parts; ++pi) {
                const int a = n * pi / parts;
                const int b = n * (pi + 1) / parts;
                if (b > a)
                    segs.push_back({v.arena.constData(), e + a, e + b});
            }
        }
        QVector<QVector<Cand>> partial(segs.size());
        auto scanSeg = [&](int si) {
            const Seg& sg = segs.at(si);
            QVector<Cand>& out = partial[si];
            out.reserve(64);
            const char* base = sg.arena;
            for (const FileDeepEntry* it = sg.from; it < sg.to; ++it) {
                const int nameLen = int(it->len) - int(it->nameStart);
                const int idx = indexOfCI(base + it->offset + it->nameStart, nameLen,
                                          q8.constData(), int(q8.size()));
                if (idx < 0)
                    continue;
                const Cand c{1, (idx == 0 ? 0 : 2) + (it->isDir ? 0 : 1), nameLen,
                             nullptr, it, base};
                if (out.size() >= want && !better(c, out.back()))
                    continue;
                int k = out.size();
                while (k > 0 && better(c, out.at(k - 1)))
                    --k;
                if (out.size() >= want)
                    out.removeLast();
                out.insert(k, c);
            }
        };
        const int nThreads = qMin(4, segs.size());
        {
            std::vector<std::thread> workers;
            workers.reserve(nThreads);
            for (int t = 0; t < nThreads; ++t)
                workers.emplace_back([&, t] {
                    for (int si = t; si < segs.size(); si += nThreads)
                        scanSeg(si);
                });
            for (auto& w : workers)
                w.join();
        }
        for (const QVector<Cand>& part : partial)
            for (const Cand& c : part)
                offer(c);
'''
assert old_scan in s
s = s.replace(old_scan, new_scan, 1)

if '#include <vector>' not in s:
    s = s.replace('#include <thread>', '#include <thread>\n#include <vector>', 1)

io.open(P, 'w', encoding='utf-8', newline='\n').write(s)
print('FileIndexer optimized')
