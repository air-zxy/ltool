const fs = require('fs');
const p = 'D:/project/cpp/ltool/src/Archive.cpp';
let s = fs.readFileSync(p, 'utf8');

// 1) 补 include
s = s.replace('#include <QSaveFile>', '#include <QDirIterator>\n#include <QSaveFile>');

// 2) 去掉手滑留下的标签
s = s.replace('                archive_read_header_next_skip_all:;   // 标签占位，下面用正规 API 跳过\n', '');

// 3) 总量探针会白读一遍压缩包：改成从当前条目往后累加
const oldProbe = s.slice(s.indexOf('    qint64 totalBytes = 0;'), s.indexOf('    char buf[kBuf];\n    qint64 done = 0;\n    bool stopped = false;\n    struct archive_entry* entry = nullptr;'));
const newProbe = [
'    // 总量：不再单独扫一遍（那等于把压缩包读两遍），直接从当前条目往后累加，',
'    // 第一条处理完之前进度条会先按"未知"显示，够用。',
'    qint64 totalBytes = 0;',
'    {\n        struct archive_entry* e = nullptr;\n        struct archive* probe = a;   // 复用同一个游标',
'        const la_int64_t off = archive_tell(probe);',
'        Q_UNUSED(off);\n        totalBytes = -1;   // 先置为未知，下面边解边累加',
'        e = nullptr;\n    }',
''
].join('\n');
s = s.replace(oldProbe, newProbe);

fs.writeFileSync(p, s);
console.log('phase 1 done');
