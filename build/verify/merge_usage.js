const fs = require('fs');
const p = 'D:/project/cpp/ltool/src/ShellAction.cpp';
const lines = fs.readFileSync(p, 'utf8').split(/\r?\n/);

const start = 41;                       // 1-based
if (!lines[start - 1].startsWith('int showInfo(const QString& path)'))
  throw new Error('start line mismatch: ' + lines[start - 1]);
const end = 192;                        // 1-based, showWhoHas 的收尾 }
if (lines[end - 1] !== '}')
  throw new Error('end line mismatch: ' + JSON.stringify(lines[end - 1]));
const nxt = lines.slice(end, end + 3).find(l => l.trim());
if (!nxt || !nxt.startsWith('int showShred'))
  throw new Error('next function mismatch: ' + nxt);

const body = `// 一个窗口两件事：上面"它占了多少"，下面"哪个程序正开着它"。
// 两块的重活都丢后台线程，算完由 watcher 回填 —— 目录递归求和可能要几秒。
int showUsage(const QString& path)
{
    const FileOps::Info in = FileOps::info(path);
    QDialog d;
    d.setWindowTitle(windowTitle(QStringLiteral("占用分析"), path));
    d.setMinimumSize(560, 430);
    auto* lay = new QVBoxLayout(&d);

    auto* head = new QLabel(QDir::toNativeSeparators(path), &d);
    head->setTextInteractionFlags(Qt::TextSelectableByMouse);
    head->setWordWrap(true);
    lay->addWidget(head);

    auto* form = new QFormLayout;
    form->setHorizontalSpacing(16);
    form->setVerticalSpacing(6);
    const auto addRow = [&form](const QString& k, const QString& v) {
        auto* key = new QLabel(k);
        key->setObjectName(QStringLiteral("key"));
        auto* val = new QLabel(v);
        val->setTextInteractionFlags(Qt::TextSelectableByMouse);
        val->setWordWrap(true);
        form->addRow(key, val);
        return val;
    };
    if (!in.ok) {
        addRow(QStringLiteral("结果"), in.error);
    } else {
        addRow(in.isDir ? QStringLiteral("类型") : QStringLiteral("逻辑大小"),
               in.isDir ? QStringLiteral("文件夹") : FileOps::human(in.size));
        if (!in.isDir && in.alloc > 0) {
            addRow(QStringLiteral("实际占簇"), QStringLiteral("%1%2")
                       .arg(FileOps::human(in.alloc),
                            in.size > 0 && in.alloc > in.size * 2
                                ? QStringLiteral("（比文件本身大，小文件也会吃掉一整个簇）")
                                : QString()));
            if (in.compressed && in.alloc < in.size)
                addRow(QStringLiteral("压缩后"), QStringLiteral("省了 %1").arg(FileOps::human(in.size - in.alloc)));
        }
        addRow(QStringLiteral("簇大小 / 文件系统"),
               QStringLiteral("%1 · %2").arg(in.cluster > 0 ? FileOps::human(in.cluster) : QStringLiteral("—"),
                                             in.fsName));
        if (in.hardLinks > 1)
            addRow(QStringLiteral("硬链接"), QStringLiteral("%1 个入口共用同一份数据").arg(in.hardLinks));
        if (!in.attrs.isEmpty())
            addRow(QStringLiteral("属性"), in.attrs);
        addRow(QStringLiteral("修改时间"), in.lastWrite);
    }
    QLabel* usageVal = addRow(in.isDir ? QStringLiteral("占用（含子文件）") : QStringLiteral("占用"),
                              QStringLiteral("统计中…"));
    lay->addLayout(form);

    auto* usageWatcher = new QFutureWatcher<FileOps::Usage>(&d);
    QObject::connect(usageWatcher, &QFutureWatcherBase::finished, &d, [usageVal, usageWatcher, in]() {
        const FileOps::Usage u = usageWatcher->result();
        if (!u.ok) {
            usageVal->setText(u.error);
            return;
        }
        QString s = FileOps::human(u.bytes);
        if (u.alloc > 0 && u.alloc != u.bytes)
            s += QStringLiteral("（实际占簇 %1）").arg(FileOps::human(u.alloc));
        if (in.isDir)
            s += QStringLiteral(" · %1 个文件 / %2 个子目录").arg(u.files).arg(u.dirs);
        usageVal->setText(s);
    });
    usageWatcher->setFuture(QtConcurrent::run([path] { return FileOps::usage(path); }));

    auto* sep = new QLabel(QStringLiteral("什么程序在使用这个文件"), &d);
    sep->setObjectName(QStringLiteral("muted"));
    lay->addWidget(sep);
    auto* status = new QLabel(QStringLiteral("正在问系统哪些进程开着它…（毫秒级，只查不动任何进程）"), &d);
    status->setObjectName(QStringLiteral("muted"));
    status->setWordWrap(true);
    lay->addWidget(status);

    auto* list = new QListWidget(&d);
    list->setWordWrap(true);
    lay->addWidget(list, 1);

    auto* btns = new QHBoxLayout;
    btns->addStretch(1);
    auto* kill = new QPushButton(QStringLiteral("结束选中的进程"), &d);
    kill->setEnabled(false);
    auto* again = new QPushButton(QStringLiteral("重新查一次"), &d);
    again->setEnabled(false);
    auto* close = new QPushButton(QStringLiteral("关闭"), &d);
    btns->addWidget(again);
    btns->addWidget(kill);
    btns->addWidget(close);
    lay->addLayout(btns);

    auto* holderWatcher = new QFutureWatcher<QPair<QVector<FileOps::Holder>, QString>>(&d);
    QObject::connect(holderWatcher, &QFutureWatcherBase::finished, &d,
                     [list, status, kill, again, holderWatcher]() {
        const auto res = holderWatcher->result();
        list->clear();
        if (res.first.isEmpty()) {
            status->setText(res.second.isEmpty()
                                ? QStringLiteral("没有进程正开着这个文件 —— 现在可以安全地删除或改名。")
                                : res.second);
            again->setEnabled(true);
            return;
        }
        status->setText(QStringLiteral("有 %1 个进程正开着它。结束进程会丢掉那个程序里没保存的东西，"
                                       "先确认一下再动手。").arg(res.first.size()));
        for (const FileOps::Holder& h : res.first) {
            auto* it = new QListWidgetItem(QStringLiteral("%1  (PID %2)  ·  开了 %3 个句柄\\n%4")
                                               .arg(h.processName.isEmpty() ? QStringLiteral("未知进程") : h.processName)
                                               .arg(h.pid).arg(h.handles)
                                               .arg(h.exePath.isEmpty() ? h.objects.value(0) : h.exePath), list);
            it->setData(Qt::UserRole, h.pid);
        }
        again->setEnabled(true);
    });
    QObject::connect(list, &QListWidget::itemSelectionChanged, &d, [list, kill]() {
        kill->setEnabled(!list->selectedItems().isEmpty());
    });
    QObject::connect(kill, &QPushButton::clicked, &d, [list, status, path]() {
        auto* it = list->currentItem();
        if (!it)
            return;
        const quint64 pid = it->data(Qt::UserRole).toULongLong();
        if (QMessageBox::question(nullptr, QStringLiteral("结束进程"),
                QStringLiteral("要强制结束这个进程：\\n\\n%1\\n\\n它里面没保存的东西会直接丢掉。确定吗？")
                    .arg(it->text().section(QLatin1Char('\\n'), 0, 0)))
            != QMessageBox::Yes)
            return;
        QString err;
        status->setText(FileOps::killProcess(pid, &err)
                            ? QStringLiteral("已结束 PID %1，现在这个文件应该没人占着了（可以「重新查一次」确认）").arg(pid)
                            : err);
    });
    auto startQuery = [&d, list, status, again, holderWatcher, path]() {
        list->clear();
        again->setEnabled(false);
        status->setText(QStringLiteral("正在问系统哪些进程开着它…"));
        holderWatcher->setFuture(QtConcurrent::run([path] {
            QString err;
            const auto got = FileOps::whoHas(path, &err);
            return qMakePair(got, err);
        }));
    };
    QObject::connect(again, &QPushButton::clicked, &d, [startQuery]() { startQuery(); });
    QObject::connect(close, &QPushButton::clicked, &d, &QDialog::accept);
    startQuery();
    return d.exec();
}
`;

lines.splice(start - 1, end - start + 1, body);
fs.writeFileSync(p, lines.join('\n'));
console.log('replaced lines ' + start + '-' + end);
