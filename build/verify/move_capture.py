# 一次性搬迁脚本（2026-10-15，#75）：把 src/main.cpp 里 F1 截图族那 12 面旗标的判据本体
# 原样搬进 plugins/capture/captureselftest.cpp，并在 main.cpp 里换成"点名要导出符号"。
# 不靠我手打 —— 判据里的中文和格式串一过打字就会掉字（这个项目栽过好几次）。
import io
import re
import sys

MAIN = 'src/main.cpp'
OUT = 'plugins/capture/captureselftest.cpp'
FLAG_PREFIX = '    if (args.contains("--%s"))'


def read_lines(p):
    return io.open(p, encoding='utf-8', newline='').read().split('\n')


def find_if(lines, flag):
    pat = 'if (args.contains("--%s"))' % flag
    hits = [i for i, l in enumerate(lines) if l.strip() == pat + ' {']
    assert len(hits) == 1, (flag, hits)
    return hits[0]


def block_end(lines, start):
    depth = 0
    for i in range(start, len(lines)):
        depth += lines[i].count('{') - lines[i].count('}')
        if depth == 0 and i > start:
            return i
    raise AssertionError('unbalanced from %d' % start)


def comment_start(lines, start):
    i = start - 1
    while i >= 0 and lines[i].strip().startswith('//'):
        i -= 1
    return i + 1


def dedent(lines, n):
    out = []
    for l in lines:
        assert l.startswith(' ' * n) or not l.strip(), repr(l)
        out.append(l[n:])
    return out


# 每面旗标：符号名 / 参数 / 头部怎么接上插件的入参 / 收尾
TEXT = {'snaprect': dict(sym='ltoolSnaprectRun', sig='', arg=None, tail=None),
        'ocrtest': dict(sym='ltoolOcrtestRun', sig='const char* spec',
                        arg='const QString target = QString::fromUtf8(spec ? spec : "");', tail=None),
        'ocrlayout': dict(sym='ltoolOcrlayoutRun', sig='const char* spec',
                          arg='const QString target = QString::fromUtf8(spec ? spec : "");', tail=None),
        'pinpick': dict(sym='ltoolPinpickRun', sig='const char* spec',
                        arg='const QString target = QString::fromUtf8(spec ? spec : "");', tail=None),
        'ocrchain': dict(sym='ltoolOcrchainRun', sig='const char* spec',
                         arg='const QString target = QString::fromUtf8(spec ? spec : "");', tail=None),
        'pintool': dict(sym='ltoolPintoolRun', sig='', arg=None, tail=None),
        'pindel': dict(sym='ltoolPindelRun', sig='const char* spec',
                       arg='const QImage img(QString::fromUtf8(spec ? spec : ""));', tail=None),
        'ocrget': dict(sym='ltoolOcrgetRun', sig='', arg=None, tail=None),
        'ocrpanel': dict(sym='ltoolOcrpanelRun', sig='const char* spec',
                         arg='const QString target = QString::fromUtf8(spec ? spec : "");', tail=None),
        'pinshot': dict(sym='ltoolPinshotRun', sig='const char* spec',
                        arg='const QString target = QString::fromUtf8(spec ? spec : "");', tail=None),
        'capgeo': dict(sym='ltoolCapgeoRun', sig='', arg='int bad = 0;',
                       tail='return bad == 0 ? 0 : 1;', unwrap=True),
        'capflash': dict(sym='ltoolCapflashRun', sig='int times', arg='int ok = 0;',
                         tail='std::printf("capflash: 失败 %d 项' + chr(92) + 'n", times - ok);\n'
                              'std::fflush(stdout);\nreturn times - ok;',
                         unwrap=True, drop=3)}

ORDER = ['snaprect', 'ocrtest', 'ocrlayout', 'pinpick', 'ocrchain', 'pintool', 'pindel',
         'ocrget', 'ocrpanel', 'pinshot', 'capgeo', 'capflash']

lines = read_lines(MAIN)
funcs = []
docs = []
drops = []   # (start, end) 行区间，倒序删
for flag in ORDER:
    cfg = TEXT[flag]
    i = find_if(lines, flag)
    j = block_end(lines, i)
    c = comment_start(lines, i)
    body = lines[i + 1:j]
    assert lines[j].strip() == '}', (flag, lines[j])
    if cfg.get('drop'):
        body = body[cfg['drop']:]
    if cfg.get('unwrap'):
        assert body[0].strip().startswith('QTimer::singleShot('), (flag, body[0])
        assert body[-1].strip() == '});', (flag, body[-1])
        body = body[1:-1]
        body = [l for l in body if l.strip() != 'app.quit();']
    # 去掉"从命令行读参数"那几行 —— 符号入参顶替它
    kept = []
    for l in body:
        if 'args.mid(args.indexOf("--%s")' % flag in l:
            continue
        kept.append(l)
    body = kept
    text = '\n'.join(dedent(body, 4))
    if flag == 'capgeo':
        text = text.replace('capGeoBad', 'bad')
    if flag == 'capflash':
        text = text.replace('capFlashOk', 'ok')
    # ocrpanel / pinshot 在插件里没有 app 这个局部，改用 qApp；lambda 捕获 qApp 是非法的
    if flag in ('ocrpanel', 'pinshot'):
        text = text.replace('[&app]', '[]')
        text = text.replace('&app', 'qApp')
        text = text.replace('app.topLevelWidgets()', 'qApp->topLevelWidgets()')
        text = text.replace('app.quit()', 'qApp->quit()')
        text = text.replace('app.exec()', 'QCoreApplication::exec()')
    head = []
    if cfg['arg']:
        head.append(cfg['arg'])
    inner = '\n'.join(head) + ('\n' if head else '') + text
    if cfg['tail']:
        inner += '\n' + cfg['tail']
    funcs.append('extern "C" Q_DECL_EXPORT int %s(%s)\n{\n%s\n}\n'
                 % (cfg['sym'], cfg['sig'], inner))
    docs.append('\n'.join(dedent(lines[c:i], 4)).rstrip())
    drops.append((c, j, flag))

# ---- 生成插件里那半边 ----
prologue = io.open('build/verify/capture_prologue.txt', encoding='utf-8').read()
io.open(OUT, 'w', encoding='utf-8', newline='').write(
    prologue + '\n\n'.join(d + '\n' + f for d, f in zip(docs, funcs)) + '\n')

# ---- main.cpp：把 12 个区间换成派发 ----
dispatch = {
    'snaprect': '    // --snaprect：吸附框贴不贴边。判据本体在 plugins/capture（导出 ltoolSnaprectRun）。\n'
                '    if (args.contains("--snaprect"))\n'
                '        return runPluginSelftest(QStringLiteral("capture"), "ltoolSnaprectRun",\n'
                '                                 SelftestArg::None, 0, QString(), "snaprect");\n',
    'pintool': '    // --pintool：离屏验置顶窗那两个"收起"开关。判据本体在 plugins/capture。\n'
               '    if (args.contains("--pintool"))\n'
               '        return runPluginSelftest(QStringLiteral("capture"), "ltoolPintoolRun",\n'
               '                                 SelftestArg::None, 0, QString(), "pintool");\n',
    'ocrget': '    // --ocrget：下载并校验 OCR 引擎包。判据本体在 plugins/capture。\n'
              '    if (args.contains("--ocrget"))\n'
              '        return runPluginSelftest(QStringLiteral("capture"), "ltoolOcrgetRun",\n'
              '                                 SelftestArg::None, 0, QString(), "ocrget");\n',
}
for flag in ('ocrtest', 'ocrlayout', 'pinpick', 'ocrchain', 'pindel', 'ocrpanel', 'pinshot'):
    dispatch[flag] = ('    // --%s <图片>：判据本体在 plugins/capture（符号 %s）。\n'
                      '    if (args.contains("--%s")) {\n'
                      '        const QString spec = args.mid(args.indexOf("--%s") + 1).value(0);\n'
                      '        return runPluginSelftest(QStringLiteral("capture"), "%s",\n'
                      '                                 SelftestArg::Text, 0, spec, "%s");\n'
                      '    }\n') % (flag, TEXT[flag]['sym'], flag, flag, TEXT[flag]['sym'], flag)
dispatch['capgeo'] = ''    # 这两条要留在事件循环那一段里，手动接线
dispatch['capflash'] = ''

for c, j, flag in sorted(drops, reverse=True):
    assert lines[j].strip() == '}', (flag, lines[j])
    lines[c:j + 1] = dispatch[flag].split('\n') if dispatch[flag] else []

io.open(MAIN, 'w', encoding='utf-8', newline='').write('\n'.join(lines))
print('moved %d flags; main.cpp now %d lines' % (len(ORDER), len(lines)))
