#include "minimap.h"

#include <QCoreApplication>
#include <QFontMetricsF>
#include <QPainter>
#include <QQuickWindow>
#include <QSGImageNode>
#include <QSGSimpleRectNode>
#include <QtMath>

namespace {
constexpr qreal kPadding = 6; // отступ текста слева
constexpr int kBlockAlpha = 160;
constexpr int kGlyphAlpha = 210;

struct Level {
    qreal rowPitch;
    int columns; // сколько колонок помещается по ширине
};
constexpr Level kLevels[] = {{2, 90}, {5, 64}, {8, 64}};
} // namespace

Minimap::Minimap(QQuickItem *parent)
    : QQuickItem(parent)
{
    setFlag(ItemHasContents, true);
    setAcceptedMouseButtons(Qt::LeftButton);
    setAcceptHoverEvents(true);
    updateMetrics();
}

void Minimap::setLevel(int level)
{
    level = qBound(kMinLevel, level, kMaxLevel);
    if (level == m_level)
        return;
    m_level = level;
    updateMetrics();
    emit levelChanged();
}

void Minimap::updateMetrics()
{
    const Level &level = kLevels[m_level - 1];
    m_rowPitch = level.rowPitch;
    if (m_level == kMinLevel) {
        m_charWidth = 1;
    } else {
        m_font = m_editor ? m_editor->textFont() : QFont(QStringLiteral("Consolas"));
        m_font.setPixelSize(qRound(m_rowPitch * 0.9));
        // Без хинтинга ширина символа дробная, но одинаковая при любом x — колонки не плывут
        m_font.setHintingPreference(QFont::PreferNoHinting);
        m_charWidth = QFontMetricsF(m_font).horizontalAdvance(QLatin1Char('m'));
    }
    setImplicitWidth(qCeil(kPadding * 2 + level.columns * m_charWidth));
    markDirty();
}

void Minimap::setEditor(EditorView *editor)
{
    if (editor == m_editor)
        return;
    if (m_editor)
        disconnect(m_editor, nullptr, this, nullptr);
    m_editor = editor;
    updateMetrics();
    if (editor) {
        connect(editor, &EditorView::contentChanged, this, &Minimap::markDirty);
        connect(editor, &EditorView::documentChanged, this, &Minimap::markDirty);
        connect(editor, &EditorView::scrollChanged, this, &QQuickItem::update);
        connect(editor, &EditorView::contentSizeChanged, this, &QQuickItem::update);
    }
    emit editorChanged();
}

void Minimap::markDirty()
{
    m_imageDirty = true;
    update();
}

int Minimap::capacity() const
{
    return qMax(2, int(height() / m_rowPitch));
}

qreal Minimap::editorTopRow() const
{
    return m_editor->scrollY() / m_editor->lineHeight();
}

qreal Minimap::rowsPerPixel() const
{
    const int rows = m_editor->rowCount(), fit = capacity();
    return (rows > fit ? (rows - 1.0) / (fit - 1.0) : 1.0) / m_rowPitch;
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
    return {0, (editorTopRow() - firstRow()) * m_rowPitch, width(), qMax<qreal>(4, rows * m_rowPitch)};
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
    const int maxColumn = int((width() - kPadding) / m_charWidth);
    const auto lines = m_editor->styledLines(first, capacity(), maxColumn);
    if (m_level > kMinLevel) {
        painter.setFont(m_font);
        painter.setRenderHint(QPainter::TextAntialiasing);
        const QFontMetricsF fm(m_font);
        const qreal baseline = (m_rowPitch - fm.height()) / 2 + fm.ascent();
        for (int i = 0; i < lines.size(); ++i) {
            const auto &line = lines.at(i);
            for (const auto &run : line.runs) {
                QColor color(run.color);
                color.setAlpha(kGlyphAlpha);
                painter.setPen(color);
                const int end = qMin(run.to, int(line.display.size()));
                painter.drawText(QPointF(kPadding + run.from * m_charWidth, i * m_rowPitch + baseline),
                                 line.display.mid(run.from, end - run.from));
            }
        }
        return image;
    }
    for (int i = 0; i < lines.size(); ++i) {
        const auto &line = lines.at(i);
        for (const auto &run : line.runs) {
            QColor color(run.color);
            color.setAlpha(kBlockAlpha);
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
                painter.fillRect(QRectF(kPadding + start * m_charWidth, i * m_rowPitch,
                                        (c - start) * m_charWidth, m_rowPitch),
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
        scrollEditorTo(firstRow() + y / m_rowPitch - editorRows / 2);
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
    if (event->modifiers() & Qt::ControlModifier) { // Ctrl+колесо — масштаб карты
        if (const int step = event->angleDelta().y(); step != 0)
            setLevel(m_level + (step > 0 ? 1 : -1));
        event->accept();
        return;
    }
    if (m_editor)
        QCoreApplication::sendEvent(m_editor, event);
}
