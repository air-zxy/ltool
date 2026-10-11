const fs = require('fs');
const p = 'D:/project/cpp/ltool/src/PinWindow.cpp';
const raw = fs.readFileSync(p, 'utf8');
const hadCrlf = raw.indexOf('\r\n') >= 0;
let s = hadCrlf ? raw.split('\r\n').join('\n') : raw;
const NL = String.fromCharCode(10);
let count = 0;
function sub(a, b, expect = 1) {
  const cnt = s.split(a).length - 1;
  if (cnt !== expect) throw new Error(`anchor 命中 ${cnt} 次（要 ${expect}）: ${a.slice(0, 70)}`);
  s = s.split(a).join(b);
  count++;
}

// ---- 1) 撤销栈：从"整图快照"改成"底图 + 形状" ----
sub(`    m_undo.append(m_pm);   // QPixmap 隐式共享，改之前拍一张不花钱`,
    `    m_undo.append(Snap{ m_pm, m_shapes });   // QPixmap 隐式共享，拍一张不花钱`);

// ---- 2) commit() 换成 rebuildFlat() + syncResult() ----
sub(`void PinWindow::commit(const QPixmap& pm)
{
    m_pm = pm;
    CaptureHistory::replace(m_key, m_pm);
    update();
}`,
`// 底图叠上所有形状 → m_flat。只在形状或底图变了的时候重算一次：
// 拖动过程中每帧都重新打码会卡（打码要复制区域 + 两次缩放）。
void PinWindow::rebuildFlat()
{
    m_flat = m_pm;
    if (m_shapes.isEmpty())
        return;
    for (const Shape& sh : m_shapes) {
        if (sh.tool == Mosaic)
            Anno::mosaicBlock(m_flat, m_pm, Anno::shapeRect(sh));
    }
    QPainter p(&m_flat);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(strokePen());
    p.setBrush(Qt::NoBrush);
    for (const Shape& sh : m_shapes) {
        if (sh.tool == Mosaic)
            continue;
        Anno::drawShape(p, sh.tool, Anno::shapeRect(sh), sh.a, sh.b);
    }
}

// 对外（历史/缩略图/复制/另存）交合成结果；内存里留住底图+形状，所以切回来还能接着改
void PinWindow::syncResult(bool write)
{
    rebuildFlat();
    m_edits[m_key] = Edit{ m_pm, m_shapes };
    CaptureHistory::replace(m_key, m_flat, write);
    update();
}`);

// ---- 3) 落一笔 = 往 m_shapes 里加对象 ----
sub(`void PinWindow::finishShape(const QPoint& upTo)
{
    const QRect r = QRect(m_pressImg, upTo).normalized();
    if (r.width() < 2 || r.height() < 2)
        return;   // 只点了一下没拖动：什么都不改，也别留个空的撤销步骤
    beginStroke();
    QPixmap pm = m_pm;
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(strokePen());
    p.setBrush(Qt::NoBrush);
    Anno::drawShape(p, m_tool, r, m_pressImg, upTo);
    p.end();
    commit(pm);
}`,
`void PinWindow::finishShape(const QPoint& upTo)
{
    const QRect r = QRect(m_pressImg, upTo).normalized();
    if (r.width() < 2 || r.height() < 2)
        return;   // 只点了一下没拖动：什么都不改，也别留个空的撤销步骤
    beginStroke();
    Shape sh;
    sh.tool = m_tool;      // 矩形/圆/箭头/打码块都走这条，全是可编辑对象
    sh.a = m_pressImg;
    sh.b = upTo;
    m_shapes.append(sh);
    m_selShape = m_shapes.size() - 1;
    syncResult();
}

// ---- 形状的选中 / 拖动 / 拉手柄 ----
int PinWindow::handleAt(const QPoint& widgetPos) const
{
    if (m_selShape < 0 || m_selShape >= m_shapes.size())
        return kNoDrag;
    const QVector<QRect> hs = widgetShapeHandles();
    for (int i = 0; i < hs.size(); ++i)
        if (hs[i].contains(widgetPos))
            return i;
    return kNoDrag;
}

// 手柄画在窗口坐标上（图像坐标按显示比例换算），拖动判定也用这套，两边不会不一致
QVector<QRect> PinWindow::widgetShapeHandles() const
{
    QVector<QRect> out;
    if (m_selShape < 0 || m_selShape >= m_shapes.size())
        return out;
    const double sc = viewScale();
    const QRect ir = imageRect();
    const Shape& sh = m_shapes[m_selShape];
    for (const QRect& h : Anno::shapeHandles(sh)) {
        out << QRect(ir.left() + int(h.x() * sc), ir.top() + int(h.y() * sc),
                     qMax(1, int(h.width() * sc)), qMax(1, int(h.height() * sc)));
    }
    return out;
}

void PinWindow::beginShapeDrag(int what, const QPoint& imgPos)
{
    if (m_selShape < 0 || m_selShape >= m_shapes.size())
        return;
    m_dragWhat = what;
    m_shapePress = imgPos;
    m_shapeSnap = m_shapes[m_selShape];
    m_shapePushed = false;
}

// 拖手柄=按边伸缩，拖内部=整体平移；都从按下时的快照算，避免一点点累积漂移
void PinWindow::applyShapeDrag(const QPoint& imgPos)
{
    if (m_selShape < 0 || m_selShape >= m_shapes.size())
        return;
    Shape& sh = m_shapes[m_selShape];
    const QPoint d = imgPos - m_shapePress;
    if (d.manhattanLength() > 2 && !m_shapePushed) {
        beginStroke();
        m_shapePushed = true;
    }
    if (m_dragWhat == Anno::kMove) {
        sh.a = m_shapeSnap.a + d;
        sh.b = m_shapeSnap.b + d;
    } else if (sh.tool == Arrow) {
        sh.a = m_dragWhat == 0 ? m_shapeSnap.a + d : m_shapeSnap.a;
        sh.b = m_dragWhat == 1 ? m_shapeSnap.b + d : m_shapeSnap.b;
    } else {
        const QRect n = Anno::resizeRect(Anno::shapeRect(m_shapeSnap), m_dragWhat, d, m_pm.rect());
        sh.a = n.topLeft();
        sh.b = n.bottomRight();
    }
    syncResult(false);      // 拖动过程中别每张都 PNG 落盘，松手再写
}

void PinWindow::deleteSelected()
{
    if (m_selShape < 0 || m_selShape >= m_shapes.size())
        return;
    beginStroke();
    m_shapes.removeAt(m_selShape);
    m_selShape = kNoDrag;
    syncResult();
}`);

// ---- 4) 涂抹马赛克：改的是底图，同步走 syncResult ----
sub(`    const QPixmap before = m_undo.isEmpty() ? m_pm : m_undo.last();
    Anno::mosaicSmudge(m_pm, before, imgPos, radius);
    CaptureHistory::replace(m_key, m_pm, false);   // 松手时再落盘
    update();`,
`    const QPixmap before = m_undo.isEmpty() ? m_pm : m_undo.last().pm;
    Anno::mosaicSmudge(m_pm, before, imgPos, radius);
    syncResult(false);      // 松手时再落盘
}`);

// ---- 5) 裁剪：形状跟着平移，跑出图外的丢掉 ----
sub(`    const QPixmap cut = m_pm.copy(cr);
    beginStroke();
    CaptureHistory::replace(m_key, cut);
    setPixmap(cut, false);   // 保留撤销栈：撤销要把尺寸一起还原
    refreshUndoBtn();`,
`    const QPixmap cut = m_pm.copy(cr);
    beginStroke();
    QVector<Shape> kept;
    for (const Shape& sh : m_shapes) {
        Shape t = sh;
        t.a -= cr.topLeft();
        t.b -= cr.topLeft();
        if (QRect(t.a, t.b).normalized().intersects(cut.rect()))
            kept << t;
    }
    m_selShape = kNoDrag;
    m_edits[m_key] = Edit{ cut, kept };
    setPixmap(cut, false);   // 保留撤销栈：撤销要把尺寸一起还原
    syncResult();
    refreshUndoBtn();`);

// ---- 6) 撤销：底图和形状一起回 ----
sub(`    m_pushed = false;
    const QPixmap pm = m_undo.takeLast();
    CaptureHistory::replace(m_key, pm);
    // 撤销矩形/马赛克这种"尺寸没变"的一笔时别顺手把窗口也重置了；只有撤销裁剪才重新适配
    setPixmap(pm, false, pm.size() != m_pm.size());
    refreshUndoBtn();`,
`    m_pushed = false;
    const Snap s = m_undo.takeLast();
    const bool fit = s.pm.size() != m_pm.size();
    m_selShape = kNoDrag;
    m_edits[m_key] = Edit{ s.pm, s.shapes };
    // 撤销矩形/马赛克这种"尺寸没变"的一笔时别顺手把窗口也重置了；只有撤销裁剪才重新适配
    setPixmap(s.pm, false, fit);
    syncResult();
    refreshUndoBtn();`);

// ---- 7) 画：主图换成合成结果，另外画选中框和手柄 ----
sub(`    p.setRenderHint(QPainter::SmoothPixmapTransform, viewScale() < 0.999);
    p.drawPixmap(ir, m_pm);
    p.setRenderHint(QPainter::SmoothPixmapTransform, false);
    p.setPen(QColor(63, 106, 224, 160));
    p.drawRect(ir.adjusted(0, 0, -1, -1));`,
`    p.setRenderHint(QPainter::SmoothPixmapTransform, viewScale() < 0.999);
    p.drawPixmap(ir, m_flat.isNull() ? m_pm : m_flat);
    p.setRenderHint(QPainter::SmoothPixmapTransform, false);
    p.setPen(QColor(63, 106, 224, 160));
    p.drawRect(ir.adjusted(0, 0, -1, -1));

    // 选中的形状：虚线包围盒 + 手柄，跟截图浮层一个样子
    if (m_selShape >= 0 && m_selShape < m_shapes.size()) {
        const double sc = viewScale();
        const QRect sr = Anno::shapeRect(m_shapes[m_selShape]);
        const QRect vsr(ir.left() + int(sr.x() * sc), ir.top() + int(sr.y() * sc),
                        qMax(1, int(sr.width() * sc)), qMax(1, int(sr.height() * sc)));
        p.setPen(QPen(QColor(63, 106, 224), 1, Qt::DashLine));
        p.setBrush(Qt::NoBrush);
        p.drawRect(vsr.adjusted(-2, -2, 2, 2));
        p.setPen(QColor(63, 106, 224));
        p.setBrush(Qt::white);
        for (const QRect& h : widgetShapeHandles())
            p.drawRect(h);
    }`);

// 拖新笔的预览：马赛克现在也是"框"，预览要照常画
sub(`    // 拖中的一笔预览（马赛克边拖边改，不需要预览）
    if (m_editing && m_tool != Mosaic) {`,
`    // 拖中的一笔预览（Alt 涂抹那种边拖边改，不需要预览）
    if (m_editing && !(m_tool == Mosaic && m_mosaicBrush)) {`);

// ---- 8) 按下：不画图时先选中/抓手柄，其次才拖窗口 ----
sub(`    if (m_tool != None) {
        if (!inImage(e->pos()))
            return;   // 开着工具时不许拖窗口，否则会误以为"拖不动就是在画图"
        m_editing = true;
        m_pushed = false;
        m_pressImg = m_lastImg = toImage(e->pos());
        update();
        return;
    }
    m_press = e->globalPosition().toPoint() - geometry().topLeft();
    m_dragging = true;`,
`    if (m_tool != None) {
        if (!inImage(e->pos()))
            return;   // 开着工具时不许拖窗口，否则会误以为"拖不动就是在画图"
        m_editing = true;
        m_pushed = false;
        m_mosaicBrush = m_tool == Mosaic && (e->modifiers() & Qt::AltModifier);
        m_pressImg = m_lastImg = toImage(e->pos());
        update();
        return;
    }
    // 不画图的那把工具下：点已画的框/圆/箭头/打码块 = 选中，选中后拖=移动、拖手柄=改大小
    if (inImage(e->pos())) {
        const QPoint ip = toImage(e->pos());
        const int h = handleAt(e->pos());
        if (h != kNoDrag) {
            beginShapeDrag(h, ip);
            return;
        }
        const int hit = Anno::hitShape(m_shapes, ip);
        if (hit >= 0) {
            m_selShape = hit;
            beginShapeDrag(Anno::kMove, ip);
            update();
            return;
        }
        if (m_selShape != kNoDrag) {
            m_selShape = kNoDrag;
            update();
        }
    }
    m_press = e->globalPosition().toPoint() - geometry().topLeft();
    m_dragging = true;`);

// ---- 9) 移动：形状拖动优先 ----
sub(`    if (m_editing) {
        m_lastImg = toImage(e->pos());
        if (m_tool == Mosaic)
            smudgeMosaic(m_lastImg);
        else
            update();
        return;
    }`,
`    if (m_dragWhat != kNoDrag && m_selShape >= 0 && (e->buttons() & Qt::LeftButton)) {
        applyShapeDrag(toImage(e->pos()));
        return;
    }
    if (m_editing) {
        m_lastImg = toImage(e->pos());
        if (m_tool == Mosaic && m_mosaicBrush)
            smudgeMosaic(m_lastImg);
        else
            update();
        return;
    }`);

// ---- 10) 松开：形状拖完落一次盘 ----
sub(`    if (m_editing) {
        m_editing = false;
        const QPoint upTo = toImage(e->pos());
        if (m_tool == Crop)
            applyCrop(QRect(m_pressImg, upTo));
        else if (m_tool == Mosaic)
            CaptureHistory::replace(m_key, m_pm);   // 拖的过程中只改内存，松手写一次盘
        else
            finishShape(upTo);
        update();
        return;
    }
    m_dragging = false;`,
`    if (m_dragWhat != kNoDrag) {
        const bool moved = m_shapePushed;
        m_dragWhat = kNoDrag;
        if (moved)
            syncResult();          // 拖完才写一次盘，过程中只改内存
        update();
        return;
    }
    if (m_editing) {
        m_editing = false;
        const QPoint upTo = toImage(e->pos());
        if (m_tool == Crop)
            applyCrop(QRect(m_pressImg, upTo));
        else if (m_tool == Mosaic && m_mosaicBrush)
            syncResult();          // 涂抹只改底图，松手写一次盘
        else
            finishShape(upTo);
        m_mosaicBrush = false;
        update();
        return;
    }
    m_dragging = false;`);

// ---- 11) 键盘：Delete 删选中，Esc 先取消选中再关窗 ----
sub(`    if (e->key() == Qt::Key_Escape) {
        close();
        return;
    }`,
`    if (e->key() == Qt::Key_Escape) {
        if (m_selShape != kNoDrag) {      // 先取消选中，别一按 Esc 就把截图窗关了
            m_selShape = kNoDrag;
            update();
            return;
        }
        close();
        return;
    }
    if (e->key() == Qt::Key_Delete || e->key() == Qt::Key_Backspace) {
        deleteSelected();
        return;
    }`);

// ---- 12) 对外交的都是合成结果 ----
sub(`    connect(copyBtn, &QPushButton::clicked, this, [this] {
        QApplication::clipboard()->setPixmap(m_pm);
    });`,
`    connect(copyBtn, &QPushButton::clicked, this, [this] {
        QApplication::clipboard()->setPixmap(m_flat.isNull() ? m_pm : m_flat);
    });`);
sub(`    connect(ocrBtn, &QPushButton::clicked, this, [this] {
        OcrPanel::extractFrom(m_pm.toImage(), this);
    });`,
`    connect(ocrBtn, &QPushButton::clicked, this, [this] {
        OcrPanel::extractFrom((m_flat.isNull() ? m_pm : m_flat).toImage(), this);
    });`);
sub(`        if (m_pm.save(path))`, `        if ((m_flat.isNull() ? m_pm : m_flat).save(path))`);

// ---- 13) 换图/开图：有本次会话的编辑记录就接着用，切回来还能改 ----
sub(`void PinWindow::setPixmap(const QPixmap& pm, bool resetUndo, bool fitWindow)
{
    m_pm = pm;`,
`void PinWindow::setPixmap(const QPixmap& pm, bool resetUndo, bool fitWindow)
{
    m_pm = pm;
    const auto it = m_edits.constFind(m_key);
    if (it != m_edits.constEnd()) {
        m_pm = it->base;
        m_shapes = it->shapes;
    } else {
        m_shapes.clear();
    }
    m_selShape = kNoDrag;
    rebuildFlat();`);

// ---- 14) 工具条那颗"拖动"现在兼着选择 ----
sub(`        {"拖动窗口（不画图）", None},`,
    `        {"不画图：拖窗口；点已画的框/圆/箭头/打码块 = 选中，可拖动、拉手柄改大小、Delete 删除", None},`);
sub(`        b->setFixedWidth(t.tool == None ? 44 : 30);
        if (t.tool == None)
            b->setText(QStringLiteral("拖动"));`,
    `        b->setFixedWidth(t.tool == None ? 62 : 30);
        if (t.tool == None)
            b->setText(QStringLiteral("拖动/选择"));`);
fs.writeFileSync(p, hadCrlf ? s.split(NL).join('\r\n') : s);
console.log('ok, ' + count + ' substitutions');
