const fs = require('fs');
const p = 'src/main.cpp';
let s = fs.readFileSync(p, 'utf8');
const crlf = s.includes('\r\n');
if (crlf) s = s.split('\r\n').join('\n');

function sub(a, b) {
  const c = s.split(a).length - 1;
  if (c !== 1) throw new Error('anchor hit ' + c + ': ' + a.slice(0, 70));
  s = s.replace(a, b);
}

// 系统剪贴板是全局的，别的进程会一瞬间占着它（Qt 读不到就返回空串）。
// 自测里凡是"写哨兵 + 读结果"的地方都要带重试，否则测出来的是"别家在用"而不是功能坏了。
sub('        pw->pickProbe(QRect(100000, 100000, 1, 1));\n',
  '        pw->pickProbe(QRect(100000, 100000, 1, 1));\n' +
  '        QClipboard* clip = QGuiApplication::clipboard();\n' +
  '        const auto clipWrite = [clip](const QString& v) {\n' +
  '            for (int i = 0; i < 10; ++i) {\n' +
  '                clip->setText(v);\n' +
  '                QCoreApplication::processEvents();\n' +
  '                if (clip->text() == v)\n' +
  '                    return;\n' +
  '                QThread::msleep(50);\n' +
  '            }\n' +
  '        };\n' +
  '        const auto clipRead = [clip]() {\n' +
  '            for (int i = 0; i < 10; ++i) {\n' +
  '                const QString v = clip->text();\n' +
  '                if (!v.isEmpty())\n' +
  '                    return v;\n' +
  '                QCoreApplication::processEvents();\n' +
  '                QThread::msleep(50);\n' +
  '            }\n' +
  '            return clip->text();\n' +
  '        };\n');

sub('            QGuiApplication::clipboard()->setText(QStringLiteral("SENTINEL"));\n            send(QEvent::MouseButtonPress, from, Qt::LeftButton);',
  '            clipWrite(QStringLiteral("SENTINEL"));\n            send(QEvent::MouseButtonPress, from, Qt::LeftButton);');
sub('            send(QEvent::MouseButtonRelease, to, Qt::LeftButton);\n            return QGuiApplication::clipboard()->text();',
  '            send(QEvent::MouseButtonRelease, to, Qt::LeftButton);\n            return clipRead();');
sub('        QGuiApplication::clipboard()->setText(QStringLiteral("SENTINEL"));\n        send(QEvent::KeyPress, QPoint(), Qt::NoButton, Qt::NoModifier, Qt::Key_Return);',
  '        clipWrite(QStringLiteral("SENTINEL"));\n        send(QEvent::KeyPress, QPoint(), Qt::NoButton, Qt::NoModifier, Qt::Key_Return);');
sub('        expect("Enter 复制全部", QGuiApplication::clipboard()->text(),',
  '        expect("Enter 复制全部", clipRead(),');
sub('        QGuiApplication::clipboard()->setText(QStringLiteral("SENTINEL"));\n        const QString afterEsc = drag(QPoint(60, 35), QPoint(140, 35));',
  '        clipWrite(QStringLiteral("SENTINEL"));\n        const QString afterEsc = drag(QPoint(60, 35), QPoint(140, 35));');

fs.writeFileSync(p, crlf ? s.split('\n').join('\r\n') : s);
console.log('pinpick 的剪贴板读写都加了重试');
