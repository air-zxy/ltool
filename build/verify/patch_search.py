import io

P = r'D:\project\cpp\ltool\src\SearchWindow.cpp'
s = io.open(P, encoding='utf-8').read()

# 1) bump the search sequence on every run
old1 = '''void SearchWindow::runSearch()
{
    m_results.clear();
    m_regIssues.clear();'''
new1 = '''void SearchWindow::runSearch()
{
    ++m_searchSeq;
    m_results.clear();
    m_regIssues.clear();'''
assert old1 in s
s = s.replace(old1, new1, 1)

# 2) file search no longer runs on the GUI thread
old2 = '''    // 文件搜索（≥2 个字符才搜，避免太泛）
    if (low.size() >= 2) {
        const QVector<FileEntry> files = FileIndexer::instance().search(q, 8);
        if (!files.isEmpty()) {
            addSection("── 文件 ──");
            for (const FileEntry& fe : files) {
                SearchResult r;
                r.kind = SearchResult::File;
                r.title = fe.name;
                r.subtitle = fe.path;
                r.target = fe.path;
                r.icon = Utils::cachedFileIcon(fe.path);
                addItem(r);
            }
        }
    }
'''
new2 = '''    // 文件搜索（≥2 个字符才搜，避免太泛）：丢到线程池，结果稍后插回列表
    if (low.size() >= 2)
        startFileSearch(q);
'''
assert old2 in s
s = s.replace(old2, new2, 1)

# 3) append the async helper right after runSearch()
anchor = '''    rebuildList();
}

// ---------- 激活 ----------'''
helper = '''    rebuildList();
}

// 深索引有数百万条，一次扫描上百毫秒：放到线程池里跑，输入不卡；
// 同一时刻只允许一个在跑，跑完若期间又换了查询就用最新输入重来一次
void SearchWindow::startFileSearch(const QString& q)
{
    if (m_fileSearchBusy) {
        m_fileSearchRerun = true;
        return;
    }
    m_fileSearchBusy = true;
    const quint64 seq = m_searchSeq;

    auto* watcher = new QFutureWatcher<QVector<FileEntry>>(this);
    connect(watcher, &QFutureWatcher<QVector<FileEntry>>::finished, this,
            [this, watcher, seq, q] {
                const auto files = watcher->result();
                watcher->deleteLater();
                m_fileSearchBusy = false;
                if (m_fileSearchRerun) {
                    m_fileSearchRerun = false;
                    runSearch();
                    return;
                }
                if (seq != m_searchSeq || m_input->text().trimmed() != q)
                    return;   // 查询已变，结果作废
                if (files.isEmpty())
                    return;

                for (int i = m_results.size() - 1; i >= 0; --i)
                    if (m_results[i].kind == SearchResult::Info
                        && m_results[i].title == QStringLiteral("无匹配结果"))
                        m_results.removeAt(i);
                int pos = m_results.size();
                for (int i = 0; i < m_results.size(); ++i) {
                    if (m_results[i].kind == SearchResult::Web) {
                        pos = i;
                        break;
                    }
                }
                SearchResult head;
                head.kind = SearchResult::Info;
                head.actionable = false;
                head.isSection = true;
                head.title = QStringLiteral("── 文件 ──");
                m_results.insert(pos++, head);
                for (const FileEntry& fe : files) {
                    SearchResult r;
                    r.kind = SearchResult::File;
                    r.title = fe.name;
                    r.subtitle = fe.path;
                    r.target = fe.path;
                    r.icon = Utils::cachedFileIcon(fe.path);
                    m_results.insert(pos++, r);
                }
                rebuildList();
            });
    watcher->setFuture(QtConcurrent::run(
        [q]() { return FileIndexer::instance().search(q, 8); }));
}

// ---------- 激活 ----------'''
assert anchor in s
s = s.replace(anchor, helper, 1)

io.open(P, 'w', encoding='utf-8', newline='\n').write(s)
print('SearchWindow patched')
