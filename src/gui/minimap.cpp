#include "minimap.h"

#include <QCoreApplication>
#include <QPainter>
#include <QQuickWindow>
#include <QSGImageNode>
#include <QSGSimpleRectNode>
#include <QtMath>

namespace {
constexpr qreal kRowPitch = 2;  // высота ряда, логические пиксели
constexpr qreal kCharWidth = 1;
constexpr qreal kPadding = 6;   // отступ текста слева
constexpr int kTextAlpha = 160;
} // namespace

Minimap::Minimap(QQuickItem *parent)
    : QQuickItem(parent)
{
    setFlag(ItemHasContents, true);
    setAcceptedMouseButtons(Qt::LeftButton);
    setAcceptHoverEvents(true);
}

void Minimap::setEditor(EditorView *editor)
{
    if (editor == m_editor)
        return;
    if (m_editor)
        disconnect(m_editor, nullptr, this, nullptr);
    m_editor = editor;
    if (editor) {
        connect(editor, &EditorView::contentChanged, this, &Minimap::markDirty);
        connect(editor, &EditorView::documentChanged, this, &Minimap::markDirty);
        connect(editor, &EditorView::scrollChanged, this, &QQuickItem::update);
        connect(editor, &EditorView::contentSizeChanged, this, &QQuickItem::update);
    }
    markDirty();
    emit editorChanged();
}

void Minimap::markDirty()
{
    m_imageDirty = true;
    update();
}

int Minimap::capacity() const
{
    return qMax(2, int(height() / kRowPitch));
}

qreal Minimap::editorTopRow() const
{
    return m_editor->scrollY() / m_editor->lineHeight();
}

qreal Minimap::rowsPerPixel() const
{
    const int rows = m_editor->rowCount(), fit = capacity();
    return (rows > fit ? (rows - 1.0) / (fit - 1.0) : 1.0) / kRowPitch;
}

int Minimap::firstRow() const
{
    const int rows = m_editor->rowCount(), fit = capacity();
    if (rows <= fit)
        return 0;
    return qBound(0, qRound(editorTopRow() / (rows - 1) * (rows - fit)), rows - fit);
}

QRectF Minimap::sliderRect() const
{
    const qreal rows = m_editor->height() / m_editor->lineHeight();
    return {0, (editorTopRow() - firstRow()) * kRowPitch, width(), qMax<qreal>(4, rows * kRowPitch)};
}

void Minimap::scrollEditorTo(qreal topRow)
{
    m_editor->setScrollY(topRow * m_editor->lineHeight());
}

QImage Minimap::renderImage(int first) const
{
    const qreal dpr = window() ? window()->effectiveDevicePixelRatio() : 1;
    QImage image(qCeil(width() * dpr), qCeil(height() * dpr), QImage::Format_ARGB32_Premultiplied);
    image.fill(m_background);
    if (!m_editor || m_editor->rowCount() == 0)
        return image;

    QPainter painter(&image);
    painter.scale(dpr, dpr);
    const int maxColumn = int((width() - kPadding) / kCharWidth);
    const auto lines = m_editor->styledLines(first, capacity(), maxColumn);
    for (int i = 0; i < lines.size(); ++i) {
        const auto &line = lines.at(i);
        for (const auto &run : line.runs) {
            QColor color(run.color);
            color.setAlpha(kTextAlpha);
            // Сплошные блоки по непробельным отрезкам
            const int end = qMin(run.to, int(line.display.size()));
            for (int c = run.from; c < end;) {
                if (line.display.at(c).isSpace()) {
                    ++c;
                    continue;
                }
                const int start = c;
                while (c < end && !line.display.at(c).isSpace())
                    ++c;
                painter.fillRect(QRectF(kPadding + start * kCharWidth, i * kRowPitch,
                                        (c - start) * kCharWidth, kRowPitch),
                                 color);
            }
        }
    }
    return image;
}

QSGNode *Minimap::updatePaintNode(QSGNode *oldNode, UpdatePaintNodeData *)
{
    QSGImageNode *imageNode = nullptr;
    QSGSimpleRectNode *slider = nullptr;
    if (oldNode) {
        imageNode = static_cast<QSGImageNode *>(oldNode->firstChild());
        slider = static_cast<QSGSimpleRectNode *>(oldNode->lastChild());
    } else {
        oldNode = new QSGNode;
        imageNode = window()->createImageNode();
        imageNode->setOwnsTexture(true);
        slider = new QSGSimpleRectNode;
        oldNode->appendChildNode(imageNode);
        oldNode->appendChildNode(slider);
        m_imageDirty = true;
    }

    const bool hasText = m_editor && m_editor->rowCount() > 0;
    const int first = hasText ? firstRow() : 0;
    if (m_imageDirty || first != m_builtFirstRow) {
        imageNode->setTexture(window()->createTextureFromImage(renderImage(first)));
        m_builtFirstRow = first;
        m_imageDirty = false;
    }
    imageNode->setRect(boundingRect());

    const int alpha = m_dragging ? 36 : m_hover ? 26 : 14;
    slider->setColor(QColor(255, 255, 255, alpha));
    slider->setRect(hasText ? sliderRect() : QRectF());
    return oldNode;
}

void Minimap::geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry)
{
    QQuickItem::geometryChange(newGeometry, oldGeometry);
    markDirty();
}

void Minimap::mousePressEvent(QMouseEvent *event)
{
    if (!m_editor || m_editor->rowCount() == 0)
        return;
    const qreal y = event->position().y();
    QRectF slider = sliderRect();
    if (!slider.contains(event->position())) {
        // Клик мимо рамки — центрируем редактор на этом ряду и продолжаем как перетаскивание
        const qreal editorRows = m_editor->height() / m_editor->lineHeight();
        scrollEditorTo(firstRow() + y / kRowPitch - editorRows / 2);
        slider = sliderRect();
    }
    m_dragOffset = y - slider.top();
    m_dragging = true;
    update();
    event->accept();
}

void Minimap::mouseMoveEvent(QMouseEvent *event)
{
    if (m_dragging)
        scrollEditorTo((event->position().y() - m_dragOffset) * rowsPerPixel());
    event->accept();
}

void Minimap::mouseReleaseEvent(QMouseEvent *event)
{
    m_dragging = false;
    update();
    event->accept();
}

void Minimap::hoverEnterEvent(QHoverEvent *)
{
    m_hover = true;
    update();
}

void Minimap::hoverLeaveEvent(QHoverEvent *)
{
    m_hover = false;
    update();
}

void Minimap::wheelEvent(QWheelEvent *event)
{
    if (m_editor)
        QCoreApplication::sendEvent(m_editor, event);
}
