const fs = require('fs');
const p = 'D:/project/cpp/ltool/src/CaptureOverlay.cpp';
// 这份源码是 CRLF，先统一成 LF 再匹配，写回去时还原 CRLF
const raw = fs.readFileSync(p, 'utf8');
const hadCrlf = raw.indexOf('\r\n') >= 0;
let s = hadCrlf ? raw.split('\r\n').join('\n') : raw;
const orig = s;
let n = 0;
function sub(a, b, expect = 1) {
  const cnt = s.split(a).length - 1;
  if (cnt !== expect) throw new Error(`anchor hit ${cnt} times (want ${expect}): ${a.slice(0, 60)}`);
  s = s.split(a).join(b);
  n++;
}

// 1) 删掉前向声明
sub('static QRect resizeRect(const QRect& r0, int what, const QPoint& d, const QRect& bounds);\n\n', '');

// 2) 几何实现全部交给 Anno，这里只留转调
sub(`QVector<QRect> CaptureOverlay::handlesFor(const QRect& s)
{
    const int k = 11;   // 命中块边长：比画出来的方块大一点，好点中
    const auto at = [k](int x, int y) { return QRect(x - k / 2, y - k / 2, k, k); };
    const int l = s.left(), t = s.top(), r = s.right(), b = s.bottom();
    const int cx = (l + r) / 2, cy = (t + b) / 2;
    return { at(l, t), at(cx, t), at(r, t), at(r, cy),
             at(r, b), at(cx, b), at(l, b), at(l, cy) };
}`,
`QVector<QRect> CaptureOverlay::handlesFor(const QRect& r)
{
    return Anno::boxHandles(r);
}`);

sub(`QRect CaptureOverlay::shapeRect(const Shape& s)
{
    return QRect(s.a, s.b).normalized();
}`,
`QRect CaptureOverlay::shapeRect(const Shape& s)
{
    return Anno::shapeRect(s);
}`);

sub(`QVector<QRect> CaptureOverlay::shapeHandles(const Shape& s) const
{
    const int k = 11;
    const auto at = [k](const QPoint& p) { return QRect(p.x() - k / 2, p.y() - k / 2, k, k); };
    if (s.tool == Arrow)
        return { at(s.a), at(s.b) };   // 箭头只拖两端，拖"包围盒手柄"没有意义
    return handlesFor(shapeRect(s));
}`,
`QVector<QRect> CaptureOverlay::shapeHandles(const Shape& s) const
{
    return Anno::shapeHandles(s);
}`);

sub(`bool CaptureOverlay::hitsShape(const Shape& s, const QPoint& pos) const
{
    if (s.tool == Arrow) {
        // 点到线段的距离
        const double vx = double(s.b.x() - s.a.x()), vy = double(s.b.y() - s.a.y());
        const double len2 = vx * vx + vy * vy;
        double t = 0.0;
        if (len2 > 1.0)
            t = qBound(0.0, (double(pos.x() - s.a.x()) * vx + double(pos.y() - s.a.y()) * vy) / len2, 1.0);
        const double px = pos.x() - (s.a.x() + vx * t), py = pos.y() - (s.a.y() + vy * t);
        return std::sqrt(px * px + py * py) < 8.0;
    }
    return shapeRect(s).adjusted(-6, -6, 6, 6).contains(pos);
}

int CaptureOverlay::shapeAt(const QPoint& pos) const
{
    for (int i = m_shapes.size() - 1; i >= 0; --i)
        if (hitsShape(m_shapes[i], pos))
            return i;
    return -1;
}`,
`bool CaptureOverlay::hitsShape(const Shape& s, const QPoint& pos) const
{
    return Anno::hitsShape(s, pos);
}

int CaptureOverlay::shapeAt(const QPoint& pos) const
{
    return Anno::hitShape(m_shapes, pos);
}`);

// 3) 两处 resizeRect 调用改走 Anno
sub('const QRect n = resizeRect(shapeRect(m_shapeSnap), m_dragWhat, d, rect());',
    'const QRect n = Anno::resizeRect(shapeRect(m_shapeSnap), m_dragWhat, d, rect());');
sub('const QRect n = resizeRect(m_sel0, m_dragWhat, d, rect());',
    'const QRect n = Anno::resizeRect(m_sel0, m_dragWhat, d, rect());');

// 4) 删掉本地那份 resizeRect 定义
sub(`// 拖手柄=按边伸缩，拖选区内部=整体平移；两种都从 m_sel0 快照算，避免累积误差
static QRect resizeRect(const QRect& r0, int what, const QPoint& d, const QRect& bounds)
{
    int l = r0.left(), t = r0.top(), r = r0.right(), b = r0.bottom();
    switch (what) {
    case 0: l += d.x(); t += d.y(); break;
    case 1: t += d.y(); break;
    case 2: r += d.x(); t += d.y(); break;
    case 3: r += d.x(); break;
    case 4: r += d.x(); b += d.y(); break;
    case 5: b += d.y(); break;
    case 6: l += d.x(); b += d.y(); break;
    case 7: l += d.x(); break;
    default: return r0;
    }
    return QRect(QPoint(l, t), QPoint(r, b)).normalized().intersected(bounds);
}

`, '');

if (s === orig) throw new Error('nothing changed');
fs.writeFileSync(p, hadCrlf ? s.split(String.fromCharCode(10)).join('\r\n') : s);
console.log('ok, ' + n + ' substitutions');
