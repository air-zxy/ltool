const fs = require('fs');
const NL = String.fromCharCode(10);
const BS = String.fromCharCode(92);

// ---- UninstallPage.h：主程序是不是"猜的"要标出来 ----
let h = fs.readFileSync('D:/project/cpp/ltool/src/UninstallPage.h', 'utf8');
const HN = h.includes('\r\n') ? '\r\n' : '\n';
h = h.replace('        QString mainExe;             // 防火墙要针对的那个 exe，reload 时推出来',
              '        QString mainExe;             // 防火墙要针对的那个 exe，reload 时推出来'+HN+
              '        bool mainExeSure = false;    // false = 只从卸载器目录猜的，可能就是卸载器本身');
h = h.replace('    static QString guessMainExe(const Entry& e);',
              '    static QString guessMainExe(const Entry& e, bool* sure);');
fs.writeFileSync('D:/project/cpp/ltool/src/UninstallPage.h', h);

// ---- UninstallPage.cpp ----
let u = fs.readFileSync('D:/project/cpp/ltool/src/UninstallPage.cpp', 'utf8');
const reps = [];

// 1) 日期：认 "20240815" 也认 "Sun Oct 12 19:25:22 2025"
reps.push([
  'QString prettyDate(const QString& raw)' + NL + '{' + NL +
  '    if (raw.size() >= 8) {' + NL +
  '        bool digits = true;' + NL +
  '        for (int i = 0; i < 8; ++i)' + NL +
  '            digits = digits && raw[i].isDigit();' + NL +
  '        if (digits) {' + NL +
  '            const QDate d = QDate::fromString(raw.left(8), "yyyyMMdd");' + NL +
  '            if (d.isValid())' + NL +
  '                return d.toString("yyyy-MM-dd");' + NL +
  '        }' + NL +
  '    }' + NL +
  '    return raw;' + NL +
  '}',
  'QString prettyDate(const QString& raw)' + NL +
  '{' + NL +
  '    const QString s = raw.trimmed();' + NL +
  '    if (s.size() >= 8) {' + NL +
  '        bool digits = true;' + NL +
  '        for (int i = 0; i < 8; ++i)' + NL +
  '            digits = digits && s[i].isDigit();' + NL +
  '        if (digits) {' + NL +
  '            const QDate d = QDate::fromString(s.left(8), "yyyyMMdd");' + NL +
  '            if (d.isValid())' + NL +
  '                return d.toString("yyyy-MM-dd");' + NL +
  '        }' + NL +
  '    }' + NL +
  '    // 有的软件写的是 "Sun Oct 12 19:25:22 2025" 这种 ctime 格式，直接吐出来很难看' + NL +
  '    const QDateTime dt = QDateTime::fromString(s, Qt::TextDate);' + NL +
  '    if (dt.isValid())' + NL +
  '        return dt.toString("yyyy-MM-dd");' + NL +
  '    return s;' + NL +
  '}']);

// 2) guessMainExe 带出"是不是猜的"
reps.push(['QString UninstallPage::guessMainExe(const Entry& e)' + NL + '{',
           'QString UninstallPage::guessMainExe(const Entry& e, bool* sure)' + NL + '{' + NL +
           '    *sure = false;']);
reps.push(['        if (p.endsWith(QLatin1String(".exe"), Qt::CaseInsensitive) && QFileInfo::exists(p))' + NL +
           '            return p;',
           '        if (p.endsWith(QLatin1String(".exe"), Qt::CaseInsensitive) && QFileInfo::exists(p)) {' + NL +
           '            *sure = true;' + NL +
           '            return p;' + NL +
           '        }']);
reps.push(['        if (!found.isEmpty())' + NL +
           '            return found;' + NL +
           '    }' + NL +
           '    if (!e.uninstallString.isEmpty())' + NL +
           '        return scanDir(QFileInfo(cleanIconPath(Utils::exeFromCommand(e.uninstallString))).absolutePath());' + NL +
           '    return {};',
           '        if (!found.isEmpty()) {' + NL +
           '            *sure = true;' + NL +
           '            return found;' + NL +
           '        }' + NL +
           '    }' + NL +
           '    // 最后才退到"卸载器同目录"：这里挑出来的往往就是 uninstall.exe 本身' + NL +
           '    if (!e.uninstallString.isEmpty())' + NL +
           '        return scanDir(QFileInfo(cleanIconPath(Utils::exeFromCommand(e.uninstallString))).absolutePath());' + NL +
           '    return {};']);
reps.push(['            e.mainExe = guessMainExe(e);',
           '            e.mainExe = guessMainExe(e, &e.mainExeSure);']);

// 3) 列表副标题只留发布者 + 版本，其余进卡片
reps.push(['        QString line = e.name;' + NL +
           '        if (!e.version.isEmpty())' + NL +
           '            line += QStringLiteral("    %1").arg(e.version);' + NL +
           '        if (!e.installDate.isEmpty())' + NL +
           '            line += QStringLiteral("    %1").arg(e.installDate);' + NL +
           '        if (!e.runnable)' + NL +
           '            line += QStringLiteral("    卸载程序已不存在");' + NL +
           '        auto* it = new QListWidgetItem();',
           '        auto* it = new QListWidgetItem();']);
reps.push(['        QStringList sub;' + NL +
           '        if (!e.publisher.isEmpty())' + NL +
           '            sub << e.publisher;' + NL +
           '        if (!e.version.isEmpty())' + NL +
           '            sub << e.version;' + NL +
           '        if (!e.installDate.isEmpty())' + NL +
           '            sub << e.installDate;' + NL +
           '        if (e.sizeKB > 0)' + NL +
           '            sub << humanSize(e.sizeKB);' + NL +
           '        sub << e.bitness;',
           '        QStringList sub;' + NL +
           '        if (!e.publisher.isEmpty())' + NL +
           '            sub << e.publisher;' + NL +
           '        if (!e.version.isEmpty())' + NL +
           '            sub << QStringLiteral("版本 %1").arg(e.version);']);

// 4) "位数" 改成 "来源"（值本来就有"当前用户"这种，不叫位数）
reps.push(['    const char* keys[] = { "安装日期", "占用", "位数", "安装位置" };',
           '    const char* keys[] = { "安装日期", "占用", "来源", "安装位置" };']);

// 5) 防火墙那行：猜来的主程序要说清楚
reps.push(['    neutral(QStringLiteral("规则针对：%1%2")' + NL +
           '                .arg(e->mainExe,' + NL +
           '                     m.names.isEmpty() ? QString()' + NL +
           '                                       : QStringLiteral("\\n这个 exe 现有的规则：%1").arg(m.names.join(QStringLiteral("、")))));',
           '    neutral(QStringLiteral("规则针对：%1%2%3")' + NL +
           '                .arg(e->mainExe,' + NL +
           '                     e->mainExeSure' + NL +
           '                         ? QString()' + NL +
           '                         : QStringLiteral("（没找到更合适的，这是从卸载器所在目录挑的，很可能就是卸载器本身）"),' + NL +
           '                     m.names.isEmpty() ? QString()' + NL +
           '                                       : QStringLiteral("\\n这个 exe 现有的规则：%1").arg(m.names.join(QStringLiteral("、")))));']);

for (const [a, b] of reps) {
  if (!u.includes(a)) throw new Error('anchor missing: ' + JSON.stringify(a.slice(0, 60)));
  u = u.replace(a, b);
}
if (!u.includes('#include <QDateTime>')) throw new Error('QDateTime include missing');
fs.writeFileSync('D:/project/cpp/ltool/src/UninstallPage.cpp', u);
console.log('all ' + reps.length + ' edits applied');
