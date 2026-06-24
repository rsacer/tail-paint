#include "Canvas.h"

#include <QMouseEvent>
#include <QWheelEvent>
#include <QContextMenuEvent>
#include <QMenu>
#include <QPainter>
#include <QPaintEvent>
#include <QQueue>
#include <QApplication>
#include <QClipboard>
#include <QMimeData>

Canvas::Canvas(QWidget *parent)
    : QWidget(parent)
{
    setAttribute(Qt::WA_StaticContents);
    setMouseTracking(true);   // para atualizar o cursor sobre as alças
    newImage();
}

QSize Canvas::sizeHint() const
{
    // O widget é a imagem escalada pelo zoom, mais a margem das alças.
    const QSize scaled(qRound(m_image.width() * m_zoom),
                       qRound(m_image.height() * m_zoom));
    return scaled + QSize(kHandleMargin, kHandleMargin);
}

void Canvas::updateWidgetSize()
{
    setFixedSize(sizeHint());
    updateGeometry();
}

void Canvas::setTool(Tool tool)
{
    // Trocar de ferramenta fixa qualquer imagem flutuante pendente.
    commitFloatingImage();
    clearSelection();
    m_tool = tool;
}

void Canvas::newImage(const QSize &size)
{
    commitFloatingImage();
    clearSelection();
    m_undoStack.clear();
    m_redoStack.clear();

    m_image = QImage(size, QImage::Format_ARGB32_Premultiplied);
    m_image.fill(Qt::white);

    setModified(false);
    emitHistorySignals();
    updateWidgetSize();
    update();
    emit canvasSizeChanged(m_image.size());
}

bool Canvas::openImage(const QString &fileName)
{
    QImage loaded;
    if (!loaded.load(fileName))
        return false;

    commitFloatingImage();
    clearSelection();
    m_undoStack.clear();
    m_redoStack.clear();

    m_image = loaded.convertToFormat(QImage::Format_ARGB32_Premultiplied);

    setModified(false);
    emitHistorySignals();
    updateWidgetSize();
    update();
    emit canvasSizeChanged(m_image.size());
    return true;
}

void Canvas::openImageFromData(const QImage &image)
{
    if (image.isNull())
        return;

    commitFloatingImage();
    clearSelection();
    m_undoStack.clear();
    m_redoStack.clear();

    m_image = image.convertToFormat(QImage::Format_ARGB32_Premultiplied);

    setModified(false);
    emitHistorySignals();
    updateWidgetSize();
    update();
    emit canvasSizeChanged(m_image.size());
}

bool Canvas::saveImage(const QString &fileName, const char *fileFormat)
{
    commitFloatingImage();
    if (m_image.save(fileName, fileFormat)) {
        setModified(false);
        return true;
    }
    return false;
}

void Canvas::clearImage()
{
    commitFloatingImage();
    clearSelection();
    pushUndoState();
    m_image.fill(Qt::white);
    setModified(true);
    update();
}

void Canvas::resizeCanvas(const QSize &newSize)
{
    const QSize clamped(qMax(1, newSize.width()), qMax(1, newSize.height()));
    if (clamped == m_image.size())
        return;

    commitFloatingImage();
    clearSelection();
    pushUndoState();
    resizeImage(&m_image, clamped);
    setModified(true);
    updateWidgetSize();
    update();
    emit canvasSizeChanged(m_image.size());
}

// ---- Imagem flutuante (inserir / colar) ----

void Canvas::insertFloatingImage(const QImage &image)
{
    if (image.isNull())
        return;

    // Fixa qualquer flutuante anterior antes de inserir a nova.
    commitFloatingImage();
    clearSelection();
    pushUndoState();

    m_floatingImage = image.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    m_hasFloating = true;
    m_floatingPos = QPoint(8, 8);

    // "Colidir com a borda": se a imagem é maior que o canvas, expande para caber.
    if (m_boundToEdge) {
        const int needW = qMax(m_image.width(), m_floatingImage.width());
        const int needH = qMax(m_image.height(), m_floatingImage.height());
        if (needW != m_image.width() || needH != m_image.height()) {
            resizeImage(&m_image, QSize(needW, needH));
            updateWidgetSize();
            emit canvasSizeChanged(m_image.size());
        }
        m_floatingPos = QPoint(0, 0);
    }

    setModified(true);
    update();
}

void Canvas::pasteFromClipboard()
{
    const QClipboard *clipboard = QApplication::clipboard();
    const QMimeData *mime = clipboard->mimeData();
    if (mime && mime->hasImage()) {
        const QImage img = qvariant_cast<QImage>(mime->imageData());
        if (!img.isNull())
            insertFloatingImage(img);
    }
}

void Canvas::commitFloatingImage()
{
    if (!m_hasFloating)
        return;

    QPainter painter(&m_image);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
    painter.drawImage(m_floatingPos, m_floatingImage);
    painter.end();

    m_hasFloating = false;
    m_movingFloating = false;
    m_floatingImage = QImage();
    setModified(true);
    update();
}

QPoint Canvas::clampFloatingPos(const QPoint &pos) const
{
    if (!m_boundToEdge || m_floatingImage.isNull())
        return pos;

    // Mantém a imagem flutuante inteiramente dentro da área do canvas.
    const int maxX = qMax(0, m_image.width() - m_floatingImage.width());
    const int maxY = qMax(0, m_image.height() - m_floatingImage.height());
    return QPoint(qBound(0, pos.x(), maxX), qBound(0, pos.y(), maxY));
}

// ---- Seleção ----

void Canvas::setSelectionCornerPercent(int pct)
{
    m_selectionCornerPercent = qBound(0, pct, 100);
    if (m_hasSelection || m_selecting)
        update();
}

qreal Canvas::cornerRadiusFor(const QRect &r) const
{
    // 100% = metade do menor lado (vira elipse/círculo); 0% = canto reto.
    const qreal maxR = qMin(r.width(), r.height()) / 2.0;
    return maxR * (m_selectionCornerPercent / 100.0);
}

QPainterPath Canvas::selectionPath(const QRect &r) const
{
    QPainterPath path;
    const qreal rad = cornerRadiusFor(r);
    if (rad <= 0.0)
        path.addRect(r);
    else
        path.addRoundedRect(r, rad, rad);
    return path;
}

QImage Canvas::maskedCopy(const QRect &r) const
{
    QImage piece = m_image.copy(r);
    if (m_selectionCornerPercent <= 0)
        return piece;  // retângulo simples, sem máscara

    // Aplica a máscara de cantos arredondados: fora do caminho fica transparente.
    QImage out(piece.size(), QImage::Format_ARGB32_Premultiplied);
    out.fill(Qt::transparent);
    QPainter painter(&out);
    painter.setRenderHint(QPainter::Antialiasing, true);
    QPainterPath path = selectionPath(QRect(QPoint(0, 0), r.size()));
    painter.setClipPath(path);
    painter.drawImage(0, 0, piece);
    painter.end();
    return out;
}

void Canvas::clearSelection()
{
    if (m_hasSelection || m_selecting) {
        m_hasSelection = false;
        m_selecting = false;
        m_selectionRect = QRect();
        update();
    }
}

void Canvas::liftSelectionToFloating()
{
    if (!m_hasSelection)
        return;

    const QRect r = m_selectionRect.intersected(m_image.rect());
    if (r.isEmpty()) {
        clearSelection();
        return;
    }

    pushUndoState();

    // Recorta a região (com máscara de cantos) para uma imagem flutuante e
    // apaga o original respeitando o mesmo formato.
    m_floatingImage = maskedCopy(r);
    QPainter painter(&m_image);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setBrush(Qt::white);
    painter.setPen(Qt::NoPen);
    painter.drawPath(selectionPath(r));
    painter.end();

    m_hasFloating = true;
    m_floatingPos = r.topLeft();
    m_hasSelection = false;
    m_selectionRect = QRect();
    setModified(true);
    update();
}

void Canvas::copySelection()
{
    QImage toCopy;
    if (m_hasFloating) {
        toCopy = m_floatingImage;
    } else if (m_hasSelection) {
        const QRect r = m_selectionRect.intersected(m_image.rect());
        if (!r.isEmpty())
            toCopy = maskedCopy(r);
    }
    if (!toCopy.isNull())
        QApplication::clipboard()->setImage(toCopy);
}

void Canvas::deleteSelection()
{
    if (m_hasFloating) {
        // Descarta a flutuante sem fixá-la.
        m_hasFloating = false;
        m_movingFloating = false;
        m_floatingImage = QImage();
        setModified(true);
        update();
        return;
    }
    if (m_hasSelection) {
        const QRect r = m_selectionRect.intersected(m_image.rect());
        if (!r.isEmpty()) {
            pushUndoState();
            QPainter painter(&m_image);
            painter.setRenderHint(QPainter::Antialiasing, true);
            painter.setBrush(Qt::white);
            painter.setPen(Qt::NoPen);
            painter.drawPath(selectionPath(r));
            painter.end();
            setModified(true);
        }
        clearSelection();
    }
}

// ---- Histórico ----

void Canvas::pushUndoState()
{
    m_undoStack.push(m_image);
    if (m_undoStack.size() > kMaxHistory)
        m_undoStack.removeFirst();
    m_redoStack.clear();
    emitHistorySignals();
}

void Canvas::undo()
{
    commitFloatingImage();
    clearSelection();
    if (m_undoStack.isEmpty())
        return;
    m_redoStack.push(m_image);
    m_image = m_undoStack.pop();
    setModified(true);
    emitHistorySignals();
    updateWidgetSize();
    update();
    emit canvasSizeChanged(m_image.size());
}

void Canvas::redo()
{
    commitFloatingImage();
    clearSelection();
    if (m_redoStack.isEmpty())
        return;
    m_undoStack.push(m_image);
    m_image = m_redoStack.pop();
    setModified(true);
    emitHistorySignals();
    updateWidgetSize();
    update();
    emit canvasSizeChanged(m_image.size());
}

// ---- Alças de redimensionamento ----

QRect Canvas::handleRect(Handle h) const
{
    // As alças ficam nas bordas da imagem JÁ escalada pelo zoom (coord. widget).
    const int w = qRound(m_image.width() * m_zoom);
    const int hgt = qRound(m_image.height() * m_zoom);
    const int s = kHandleSize;
    const int half = s / 2;

    switch (h) {
    case Handle::Right:
        return QRect(w + (kHandleMargin - s) / 2, hgt / 2 - half, s, s);
    case Handle::Bottom:
        return QRect(w / 2 - half, hgt + (kHandleMargin - s) / 2, s, s);
    case Handle::Corner:
        return QRect(w + (kHandleMargin - s) / 2, hgt + (kHandleMargin - s) / 2, s, s);
    default:
        return QRect();
    }
}

Canvas::Handle Canvas::handleAt(const QPoint &pos) const
{
    // Área de toque um pouco maior que a alça visível, para facilitar.
    const int pad = 4;
    for (Handle h : { Handle::Corner, Handle::Right, Handle::Bottom }) {
        if (handleRect(h).adjusted(-pad, -pad, pad, pad).contains(pos))
            return h;
    }
    return Handle::None;
}

// ---- Eventos de mouse ----

void Canvas::mousePressEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton)
        return;

    const QPoint pos = event->pos();

    // 1) Começou a arrastar uma alça de redimensionamento? (coord. widget)
    const Handle h = handleAt(pos);
    if (h != Handle::None) {
        m_resizing = true;
        m_activeHandle = h;
        m_resizeStartPos = pos;
        m_resizeStartSize = m_image.size();
        pushUndoState();
        return;
    }

    // A partir daqui trabalhamos em coordenadas da imagem (desfaz o zoom).
    const QPoint imgPos = toImage(pos);

    // 2) Há uma imagem flutuante? Clicar sobre ela = mover; fora = fixar.
    if (m_hasFloating) {
        const QRect floatRect(m_floatingPos, m_floatingImage.size());
        if (floatRect.contains(imgPos)) {
            m_movingFloating = true;
            m_floatingGrab = imgPos - m_floatingPos;
            return;
        }
        // Clicou fora: fixa a imagem e segue para a ferramenta normal.
        commitFloatingImage();
    }

    // 3) Ferramenta de seleção.
    if (m_tool == Tool::Select) {
        // Clicar dentro de uma seleção existente = "levantar" e mover.
        if (m_hasSelection && m_selectionRect.contains(imgPos)) {
            liftSelectionToFloating();
            m_movingFloating = true;
            m_floatingGrab = imgPos - m_floatingPos;
            return;
        }
        // Caso contrário, começa uma nova seleção.
        clearSelection();
        m_selecting = true;
        m_selectStart = imgPos;
        m_selectionRect = QRect(imgPos, imgPos);
        update();
        return;
    }

    // 4) Ferramentas normais (somente dentro da área da imagem).
    if (!m_image.rect().contains(imgPos))
        return;

    if (m_tool == Tool::Fill) {
        pushUndoState();
        floodFill(imgPos);
        setModified(true);
        update();
        return;
    }

    pushUndoState();
    m_startPoint = imgPos;
    m_lastPoint = imgPos;
    m_drawing = true;

    if (m_tool == Tool::Pencil || m_tool == Tool::Eraser) {
        drawLineTo(imgPos);
    } else {
        m_previewBase = m_image;
    }
}

void Canvas::mouseMoveEvent(QMouseEvent *event)
{
    const QPoint pos = event->pos();
    const QPoint imgPos = toImage(pos);

    // Redimensionando via alça. O delta em pixels-de-tela é dividido pelo zoom
    // para virar delta em pixels-da-imagem.
    if (m_resizing) {
        const QPoint delta = pos - m_resizeStartPos;
        QSize newSize = m_resizeStartSize;
        if (m_activeHandle == Handle::Right || m_activeHandle == Handle::Corner)
            newSize.setWidth(qMax(1, m_resizeStartSize.width()
                                       + qRound(delta.x() / m_zoom)));
        if (m_activeHandle == Handle::Bottom || m_activeHandle == Handle::Corner)
            newSize.setHeight(qMax(1, m_resizeStartSize.height()
                                        + qRound(delta.y() / m_zoom)));

        resizeImage(&m_image, newSize);
        updateWidgetSize();
        update();
        emit canvasSizeChanged(m_image.size());
        return;
    }

    // Movendo a imagem flutuante (com clamp opcional na borda).
    if (m_movingFloating && m_hasFloating) {
        m_floatingPos = clampFloatingPos(imgPos - m_floatingGrab);
        update();
        return;
    }

    // Arrastando para criar a seleção.
    if (m_selecting) {
        m_selectionRect = QRect(m_selectStart, imgPos).normalized()
                              .intersected(m_image.rect());
        update();
        return;
    }

    // Atualiza o cursor quando passa por cima de uma alça.
    if (!m_drawing && !(event->buttons() & Qt::LeftButton)) {
        switch (handleAt(pos)) {
        case Handle::Right:  setCursor(Qt::SizeHorCursor); break;
        case Handle::Bottom: setCursor(Qt::SizeVerCursor); break;
        case Handle::Corner: setCursor(Qt::SizeFDiagCursor); break;
        default:
            if (m_hasFloating &&
                QRect(m_floatingPos, m_floatingImage.size()).contains(imgPos))
                setCursor(Qt::SizeAllCursor);
            else if (m_tool == Tool::Select && m_hasSelection &&
                     m_selectionRect.contains(imgPos))
                setCursor(Qt::SizeAllCursor);
            else if (m_tool == Tool::Select)
                setCursor(Qt::CrossCursor);
            else
                setCursor(Qt::CrossCursor);
            break;
        }
    }

    if (!(event->buttons() & Qt::LeftButton) || !m_drawing)
        return;

    if (m_tool == Tool::Pencil || m_tool == Tool::Eraser) {
        drawLineTo(imgPos);
    } else {
        m_image = m_previewBase;
        drawShapeTo(imgPos, m_image);
        update();
    }
}

void Canvas::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton)
        return;

    if (m_resizing) {
        m_resizing = false;
        m_activeHandle = Handle::None;
        setModified(true);
        return;
    }

    if (m_movingFloating) {
        m_movingFloating = false;
        return;
    }

    // Finaliza a criação da seleção.
    if (m_selecting) {
        m_selecting = false;
        m_hasSelection = !m_selectionRect.isEmpty()
                         && m_selectionRect.width() > 2
                         && m_selectionRect.height() > 2;
        if (!m_hasSelection)
            m_selectionRect = QRect();
        update();
        return;
    }

    if (!m_drawing)
        return;

    const QPoint imgPos = toImage(event->pos());
    if (m_tool == Tool::Pencil || m_tool == Tool::Eraser) {
        drawLineTo(imgPos);
    } else {
        m_image = m_previewBase;
        drawShapeTo(imgPos, m_image);
        update();
    }

    m_drawing = false;
    setModified(true);
}

void Canvas::leaveEvent(QEvent *event)
{
    unsetCursor();
    QWidget::leaveEvent(event);
}

// ---- Zoom ----

void Canvas::setZoom(qreal factor)
{
    applyZoom(factor, nullptr);
}

void Canvas::zoomIn()
{
    applyZoom(m_zoom * 1.25, nullptr);
}

void Canvas::zoomOut()
{
    applyZoom(m_zoom / 1.25, nullptr);
}

void Canvas::resetZoom()
{
    applyZoom(1.0, nullptr);
}

void Canvas::applyZoom(qreal factor, const QPoint *anchorWidgetPos)
{
    Q_UNUSED(anchorWidgetPos);
    const qreal newZoom = qBound(kMinZoom, factor, kMaxZoom);
    if (qFuzzyCompare(newZoom, m_zoom))
        return;

    m_zoom = newZoom;
    updateWidgetSize();
    update();
    emit zoomChanged(m_zoom);
}

void Canvas::wheelEvent(QWheelEvent *event)
{
    // Ctrl + roda = zoom centrado no cursor. Sem Ctrl, comportamento padrão
    // (rolagem do QScrollArea), então repassamos o evento.
    if (event->modifiers() & Qt::ControlModifier) {
        const QPoint anchor = event->position().toPoint();
        const qreal step = (event->angleDelta().y() > 0) ? 1.25 : 1.0 / 1.25;
        applyZoom(m_zoom * step, &anchor);
        event->accept();
    } else {
        event->ignore();
    }
}

void Canvas::contextMenuEvent(QContextMenuEvent *event)
{
    QMenu menu(this);
    QAction *insertAct = menu.addAction(QStringLiteral("Inserir imagem"));
    QAction *chosen = menu.exec(event->globalPos());
    if (chosen == insertAct)
        emit insertImageRequested();
}

// ---- Desenho ----

void Canvas::drawLineTo(const QPoint &endPoint)
{
    QPainter painter(&m_image);
    painter.setRenderHint(QPainter::Antialiasing, true);

    const QColor color = (m_tool == Tool::Eraser) ? QColor(Qt::white) : m_penColor;

    if (endPoint == m_lastPoint) {
        // Clique único (sem arrastar): pinta um ponto redondo do tamanho do pincel.
        painter.setPen(Qt::NoPen);
        painter.setBrush(color);
        const qreal r = m_penWidth / 2.0;
        painter.drawEllipse(QPointF(endPoint), r, r);
    } else {
        painter.setPen(QPen(color, m_penWidth, Qt::SolidLine,
                            Qt::RoundCap, Qt::RoundJoin));
        painter.drawLine(m_lastPoint, endPoint);
    }

    update();
    m_lastPoint = endPoint;
}

void Canvas::drawShapeTo(const QPoint &endPoint, QImage &target)
{
    QPainter painter(&target);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(QPen(m_penColor, m_penWidth, Qt::SolidLine,
                        Qt::RoundCap, Qt::RoundJoin));

    switch (m_tool) {
    case Tool::Line:
        painter.drawLine(m_startPoint, endPoint);
        break;
    case Tool::Rectangle:
        painter.drawRect(QRect(m_startPoint, endPoint).normalized());
        break;
    case Tool::Ellipse:
        painter.drawEllipse(QRect(m_startPoint, endPoint).normalized());
        break;
    default:
        break;
    }
}

void Canvas::floodFill(const QPoint &startPoint)
{
    if (!m_image.rect().contains(startPoint))
        return;

    const QRgb targetColor = m_image.pixel(startPoint);
    const QRgb fillColor = m_penColor.rgba();
    if (targetColor == fillColor)
        return;

    const int width = m_image.width();
    const int height = m_image.height();

    QQueue<QPoint> queue;
    queue.enqueue(startPoint);

    while (!queue.isEmpty()) {
        const QPoint p = queue.dequeue();
        const int x = p.x();
        const int y = p.y();

        if (x < 0 || x >= width || y < 0 || y >= height)
            continue;
        if (m_image.pixel(x, y) != targetColor)
            continue;

        m_image.setPixel(x, y, fillColor);

        queue.enqueue(QPoint(x + 1, y));
        queue.enqueue(QPoint(x - 1, y));
        queue.enqueue(QPoint(x, y + 1));
        queue.enqueue(QPoint(x, y - 1));
    }
}

void Canvas::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);
    QPainter painter(this);

    // Conteúdo escalado pelo zoom (imagem + flutuante). O painter é salvo/restaurado
    // para que as alças sejam desenhadas em coordenadas de tela (não escaladas).
    painter.save();
    painter.scale(m_zoom, m_zoom);
    if (m_zoom < 1.0)
        painter.setRenderHint(QPainter::SmoothPixmapTransform, true);

    // 1) A imagem confirmada.
    painter.drawImage(0, 0, m_image);

    // 2) A imagem flutuante por cima (se houver), com uma moldura tracejada.
    if (m_hasFloating) {
        painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
        painter.drawImage(m_floatingPos, m_floatingImage);

        QPen pen(QColor(0x35, 0x84, 0xe4), 1.0 / m_zoom, Qt::DashLine);
        painter.setPen(pen);
        painter.setBrush(Qt::NoBrush);
        painter.drawRect(QRect(m_floatingPos, m_floatingImage.size())
                             .adjusted(0, 0, -1, -1));
    }

    // 2b) Seleção (ferramenta Select) — respeita o arredondamento dos cantos.
    if ((m_hasSelection || m_selecting) && !m_selectionRect.isEmpty()) {
        const QPainterPath path =
            selectionPath(m_selectionRect.adjusted(0, 0, -1, -1));
        // Leve realce do interior + moldura tracejada.
        painter.fillPath(path, QColor(0x35, 0x84, 0xe4, 40));
        QPen pen(QColor(0x35, 0x84, 0xe4), 1.0 / m_zoom, Qt::DashLine);
        painter.setPen(pen);
        painter.setBrush(Qt::NoBrush);
        painter.drawPath(path);
    }
    painter.restore();

    // 2c) Indicador de tamanho da seleção (em pixels), em coordenadas de tela.
    if ((m_hasSelection || m_selecting) && !m_selectionRect.isEmpty()) {
        const QString text = QStringLiteral("%1 × %2")
                                 .arg(m_selectionRect.width())
                                 .arg(m_selectionRect.height());

        painter.setRenderHint(QPainter::Antialiasing, true);
        QFont f = painter.font();
        f.setPointSizeF(10);
        painter.setFont(f);

        const QRectF textRect = painter.fontMetrics().boundingRect(text);
        const qreal padX = 6, padY = 3;
        const QSizeF badge(textRect.width() + padX * 2, textRect.height() + padY * 2);

        // Posiciona logo acima do canto superior esquerdo da seleção (coord. tela).
        const QPointF selTop(m_selectionRect.left() * m_zoom,
                             m_selectionRect.top() * m_zoom);
        qreal bx = selTop.x();
        qreal by = selTop.y() - badge.height() - 4;
        if (by < 0)  // se não couber acima, mostra dentro do topo
            by = selTop.y() + 4;

        const QRectF badgeRect(bx, by, badge.width(), badge.height());
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(0x1b, 0x1b, 0x1b, 220));
        painter.drawRoundedRect(badgeRect, 5, 5);
        painter.setPen(QColor(0xff, 0xff, 0xff));
        painter.drawText(badgeRect, Qt::AlignCenter, text);
    }

    // 3) As alças de redimensionamento (cantos/bordas) — coord. de tela.
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(QPen(QColor(0x1c, 0x71, 0xd8), 1));
    painter.setBrush(QColor(0x35, 0x84, 0xe4));
    for (Handle h : { Handle::Right, Handle::Bottom, Handle::Corner }) {
        const QRect r = handleRect(h);
        painter.drawRoundedRect(r, 2, 2);
    }
}

// ---- Util ----

void Canvas::resizeImage(QImage *image, const QSize &newSize)
{
    if (image->size() == newSize)
        return;

    QImage newImage(newSize, QImage::Format_ARGB32_Premultiplied);
    newImage.fill(Qt::white);
    QPainter painter(&newImage);
    painter.drawImage(QPoint(0, 0), *image);
    *image = newImage;
}

void Canvas::setModified(bool modified)
{
    if (m_modified != modified) {
        m_modified = modified;
        emit modifiedChanged(m_modified);
    }
}

void Canvas::emitHistorySignals()
{
    emit undoAvailable(!m_undoStack.isEmpty());
    emit redoAvailable(!m_redoStack.isEmpty());
}
