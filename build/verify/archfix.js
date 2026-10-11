const fs = require('fs');
const p = 'D:/project/cpp/ltool/src/Archive.cpp';
let s = fs.readFileSync(p, 'utf8');

s = s.replace('#include <QSaveFile>', '#include <QDirIterator>\n#include <QSaveFile>');
s = s.replace('                archive_read_header_next_skip_all:;   // 标签占位，下面用正规 API 跳过\n', '');
s = s.replace('.arg(QString::fromLatin8(archive_version_string()));',
              '.arg(QString::fromLatin1(archive_version_string()));');

// 总量：不再单独扫一遍（那等于把压缩包多读一次），改成边解边累加，解到一半才有准确总数
const oldProbeStart = s.indexOf('    qint64 totalBytes = 0;');
const oldProbeEnd = s.indexOf('    char buf[kBuf];');
if (oldProbeStart < 0 || oldProbeEnd < 0 || oldProbeEnd <= oldProbeStart)
  throw new Error('probe block bounds not found');
s = s.slice(0, oldProbeStart) + [
'    qint64 totalBytes = 0;                 // 边解边累加，见下面循环里的修正',
''
].join('\n') + s.slice(oldProbeEnd);

s = s.replace('        ++res.files;\n        res.bytes += done;',
              '        ++res.files;\n        res.bytes += written;');

// 文件写入处：记下单个文件大小，用来修正 total
s = s.replace([
'        la_ssize_t n = 0;',
'        bool ioError = false;'
].join('\n'), [
'        la_ssize_t n = 0;',
'        qint64 written = 0;',
'        bool ioError = false;'
].join('\n'));
s = s.replace('            done += n;', '            done += n;\n            written += n;');
s = s.replace('        out.close();',
              '        out.close();\n        if (totalBytes < done)\n            totalBytes = done + qMax<qint64>(0, archive_entry_size(e2 ? e2 : entry));');

// 上面那行引用了不存在的 e2，改成直接用当前 entry 的 size 修正
s = s.replace('        if (totalBytes < done)\n            totalBytes = done + qMax<qint64>(0, archive_entry_size(e2 ? e2 : entry));',
              '        if (totalBytes < done + qMax<qint64>(0, archive_entry_size(entry)))\n' +
              '            totalBytes = done + qMax<qint64>(0, archive_entry_size(entry));   // 至少不小于已解出的量');

// zip 的 deflate 不是 bzip2
s = s.replace('        archive_write_add_filter_bzip2(a);   // zip 的 deflate 由格式自己管，这里加的是"外层过滤"',
              '        // zip 的压缩算法由格式自己管（默认 deflate），外层不要再加过滤器');

fs.writeFileSync(p, s);
console.log('Archive.cpp patched');
