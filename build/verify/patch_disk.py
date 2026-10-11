import io

PATH = r'D:\project\cpp\ltool\src\DiskPage.cpp'
MARK = 'void DiskPage::startTopLevelScan()'

NEW = '''void DiskPage::startTopLevelScan()
{
    scanAnalysis("C:/");
}

void DiskPage::analysisUp()
{
    QDir d(m_analysisRoot);
    if (d.isRoot() || !d.cdUp())
        return;
    scanAnalysis(d.path());
}

// 分析 root 下每个子目录的占用：一个子目录一个后台任务，完成一个立刻上屏一次
void DiskPage::scanAnalysis(const QString& root)
{
    const quint64 seq = ++m_analysisSeq;
    m_analysisRoot = root.endsWith('/') ? root : root + '/';
    m_analyzeBtn->setEnabled(false);
    m_upBtn->setEnabled(!QDir(m_analysisRoot).isRoot());
    m_analysisPath->setText(QString("当前目录：%1（点击条目进入该目录）")
                                .arg(QDir::toNativeSeparators(m_analysisRoot)));
    m_analysisStatus->setText("分析中…每个子目录完成后立即显示");
    renderAnalysis({}, m_analysisRoot);

    const auto subs = QDir(m_analysisRoot).entryList(QDir::Dirs | QDir::NoDotAndDotDot
                                                    | QDir::NoSymLinks);
    if (subs.isEmpty()) {
        m_analysisStatus->setText("该目录下没有子目录");
        m_analyzeBtn->setEnabled(true);
        return;
    }

    auto state = std::make_shared<ScanState>();
    state->remaining = subs.size();
    const int total = subs.size();
    const QString base = m_analysisRoot;
    for (const QString& sub : subs) {
        const QString child = base + sub;
        auto future = QtConcurrent::run([child]() -> QPair<QString, qint64> {
            qint64 bytes = 0;
            QDirIterator it(child, QDir::Files | QDir::NoSymLinks,
                            QDirIterator::Subdirectories);
            while (it.hasNext()) {
                it.next();
                bytes += it.fileInfo().size();
            }
            return {QFileInfo(child).fileName(), bytes};
        });
        auto* watcher = new QFutureWatcher<QPair<QString, qint64>>(this);
        connect(watcher, &QFutureWatcher<QPair<QString, qint64>>::finished, this,
                [this, watcher, state, total, seq, base] {
                    const auto r = watcher->result();
                    watcher->deleteLater();
                    if (seq != m_analysisSeq) // 已下钻/返回：上一轮结果不再上屏
                        return;
                    state->rows.append(r);
                    if (--state->remaining == 0) {
                        std::sort(state->rows.begin(), state->rows.end(),
                                  [](const auto& a, const auto& b) {
                                      return a.second > b.second;
                                  });
                        qint64 sum = 0;
                        for (const auto& p : state->rows)
                            sum += p.second;
                        m_analyzeBtn->setEnabled(true);
                        m_analysisStatus->setText(
                            QString("分析完成：%1 个子目录，合计 %2")
                                .arg(total)
                                .arg(human(sum)));
                    } else {
                        m_analysisStatus->setText(
                            QString("分析中…已完成 %1/%2").arg(total - state->remaining)
                                .arg(total));
                    }
                    renderAnalysis(state->rows, base);
                });
        watcher->setFuture(future);
    }
}

// 只销毁自己登记的条形行；进度/路径提示不在本布局内，不会被误删
void DiskPage::renderAnalysis(const QVector<QPair<QString, qint64>>& rows, const QString& root)
{
    for (QWidget* w : m_analysisRows)
        delete w;
    m_analysisRows.clear();

    qint64 maxSz = 1;
    for (const auto& p : rows)
        maxSz = qMax(maxSz, p.second);
    for (int i = 0; i < rows.size(); ++i) {
        const auto& p = rows[i];
        const qreal ratio = p.second > 0 ? qreal(p.second) / maxSz : 0.0;
        const QString child = root + p.first;
        auto* row = makeBarRow(this, p.first, p.second, ratio,
                               QColor(kAnalysisColors[i % std::size(kAnalysisColors)]),
                               [this, child] { scanAnalysis(child); });
        row->setToolTip(QString("%1\\n%2\\n点击进入该目录")
                            .arg(QDir::toNativeSeparators(child), human(p.second)));
        m_analysisLay->insertWidget(m_analysisLay->count() - 1, row);
        m_analysisRows.append(row);
    }
}
'''

src = io.open(PATH, encoding='utf-8').read()
i = src.index(MARK)
out = src[:i] + NEW
io.open(PATH, 'w', encoding='utf-8', newline='\n').write(out)
print('truncated at', i, 'new length', len(out))
