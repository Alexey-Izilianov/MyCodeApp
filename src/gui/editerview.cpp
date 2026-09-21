#include "editerview.h"

#include <QKeyEvent>
#include <QMouseEvent>
#include <QClipboard>
#include <QFontMetricsF>
#include <QGuiApplication>
#include <QQuickWindow>
#include <QSGClipNode>
#include <QSGSimpleRectNode>
#include <QSGTransformNode>
#include <qsgtextnode.h>
#include <QTextLayout>
#include <QtMath>
#include <QUrl>
#include "src/core/document.h"
#include "src/core/textbuffer.h"
#include "textspikeiten.h" // spikeLog()

using core::Document;
using core::TextBuffer;

namespace {
const QColor kBackgroundColor(0x1e, 0x1e, 0x1e);
const QColor kTextColor(0xd4, 0xd4, 0xd4);
const QColor kSelectionColor(0x26, 0x4f, 0x78);
const QColor kCaretColor(0xae, 0xaf, 0xad);
} // namespace

EditorView::EditorView(QQuickItem *parent)
    : QQuickItem(parent)
{
    setFlag(QQuickItem::ItemHasContents, true);
    setAcceptedMouseButtons(Qt::LeftButton);

    m_font = QFont(QStringLiteral("Consolas"), 12);
    const QFontMetricsF fm(m_font);
    m_lineHeight = fm.lineSpacing();
    m_charWidth = fm.horizontalAdvance(QChar('M'));
}

QObject *EditorView::document() const
{
    return m_document;
}

QString EditorView::filePath() const
{
    return m_document ? m_document->filePath() : QString();
}

void EditorView::setDocument(QObject *doc)
{
    if (doc == m_document)
        return;
    QMutexLocker lock(&m_docMutex);
    m_document = qobject_cast<Document *>(doc);
    m_cursor = {};
    m_anchor = {};
    m_goalColumn = 0;
    m_scrollY = 0;
    m_lastFirstLine = -1;
    lock.unlock();
    emit documentChanged();
    emit cursorChanged();
    update();
}

void EditorView::setFilePath(const QString &path)
{
    if (path.isEmpty())
        return;
    // QML FileDialog отдаёт file:///-URL, Document::load ждёт локальный путь
    const QUrl url(path);
    const QString local = url.isLocalFile() ? url.toLocalFile() : path;
    auto *doc = new Document(this);
    if (!doc->load(local)) {
        delete doc;
        emit errorOccurred(QStringLiteral("не удалось открыть файл"));
        return;
    }
    setDocument(doc);
}

void EditorView::setScrollY(qreal y)
{
    y = qMax<qreal>(0, y);
    if (y == m_scrollY)
        return;
    m_scrollY = y;
    update();
    emit scrollYChanged();
}

bool EditorView::hasSelection() const
{
    return m_document && !(m_cursor == m_anchor);
}

qreal EditorView::contentHeight() const
{
    return m_document
               ? qreal(m_document->buffer().lineCount()) * m_lineHeight
               : 0.0;
}

void EditorView::save()
{
    if (!m_document)
        return;
    QMutexLocker lock(&m_docMutex);
    QString error;
    if (!m_document->save(&error))
        emit errorOccurred(error);
}

void EditorView::copy()
{
    if (!hasSelection())
        return;
    QMutexLocker lock(&m_docMutex);
    TextBuffer &buf = m_document->buffer();
    const Cursor a = qMin(m_anchor, m_cursor), b = qMax(m_anchor, m_cursor);
    if (a.line == b.line)
        QGuiApplication::clipboard()->setText(
            buf.lineAt(a.line).mid(a.column, b.column - a.column));
}

void EditorView::cut()
{
    copy();
    QMutexLocker lock(&m_docMutex);
    deleteSelection();
    markEdited();
}

void EditorView::paste()
{
    const QString text = QGuiApplication::clipboard()->text();
    if (text.isEmpty())
        return;
    QMutexLocker lock(&m_docMutex);
    deleteSelection();
    m_cursor = TextBuffer::clampPosition(
        {m_cursor.line, m_cursor.column}, m_document->buffer());
    m_document->buffer().insertText({m_cursor.line, m_cursor.column}, text);
    moveCursorAfterInsert(text);
    markEdited();
}

void EditorView::selectAll()
{
    if (!m_document)
        return;
    QMutexLocker lock(&m_docMutex);
    const int last = m_document->buffer().lineCount() - 1;
    m_anchor = {0, 0};
    m_cursor = {last, m_document->buffer().lineLength(last)};
    m_goalColumn = m_cursor.column;
    lock.unlock();
    update();
    emit cursorChanged();
}

void EditorView::moveCursor(int dline, int dcol, bool extend)
{
    if (!m_document)
        return;
    QMutexLocker lock(&m_docMutex);
    TextBuffer &buf = m_document->buffer();
    const int lineCount = buf.lineCount();

    int line = qBound(0, m_cursor.line + dline, lineCount - 1);
    int column = m_cursor.column + dcol;
    if (column < 0 && line > 0) { // конец предыдущей строки
        --line;
        column = buf.lineLength(line);
    }
    if (dcol > 0 && column > buf.lineLength(line) && line < lineCount - 1) {
        ++line; // начало следующей строки
        column = 0;
    }
    column = qBound(0, column, buf.lineLength(line));
    if (dcol == 0) // вниз/вверх — держим целевую колонку через пустые строки
        column = qBound(0, m_goalColumn, buf.lineLength(line));
    m_cursor = {line, column};
    m_goalColumn = column;
    lock.unlock();

    if (!extend)
        m_anchor = m_cursor;
    ensureCursorVisible();
    update();
    emit cursorChanged();
}

void EditorView::ensureCursorVisible()
{
    const qreal y = qreal(m_cursor.line) * m_lineHeight;
    if (y < m_scrollY)
        m_scrollY = y;
    else if (y + m_lineHeight > m_scrollY + height())
        m_scrollY = y + m_lineHeight - height();
    emit scrollYChanged();
}

void EditorView::moveCursorAfterInsert(const QString &text)
{
    const QChar newline = QLatin1Char('\n');
    int line = m_cursor.line, column = m_cursor.column;
    const int idx = text.lastIndexOf(newline);
    if (idx == -1) {
        column += text.size();
    } else {
        line += text.count(newline);
        column = text.size() - idx - 1;
    }
    m_cursor = TextBuffer::clampPosition({line, column},
                                         m_document->buffer());
}

void EditorView::deleteSelection()
{
    if (!(m_cursor == m_anchor)) {
        const Cursor a = qMin(m_anchor, m_cursor);
        m_document->buffer().removeText(a, qMax(m_anchor, m_cursor));
        m_cursor = a; // курсор в начало удалённого фрагмента
    }
}

void EditorView::markEdited()
{
    if (!m_document)
        return;
    m_document->setDirty(true);
    m_lastFirstLine = -1; // перерисовать видимые строки
    m_cursor = TextBuffer::clampPosition({m_cursor.line, m_cursor.column},
                                         m_document->buffer());
    m_anchor = m_cursor;
    m_goalColumn = m_cursor.column;
    ensureCursorVisible();
    emit cursorChanged();
    update();
}

void EditorView::keyPressEvent(QKeyEvent *event)
{
    const bool shift = event->modifiers() & Qt::ShiftModifier;
    const bool ctrl = event->modifiers() & Qt::ControlModifier;
    const int key = event->key();

    if (ctrl) {
        switch (key) {
        case Qt::Key_A: selectAll(); return event->accept();
        case Qt::Key_C: copy(); return event->accept();
        case Qt::Key_X: cut(); return event->accept();
        case Qt::Key_V: paste(); return event->accept();
        case Qt::Key_S: save(); return event->accept();
        default: return QQuickItem::keyPressEvent(event);
        }
    }

    switch (key) {
    case Qt::Key_Left:  moveCursor(0, -1, shift); break;
    case Qt::Key_Right: moveCursor(0, +1, shift); break;
    case Qt::Key_Up:    moveCursor(-1, 0, shift); break;
    case Qt::Key_Down:  moveCursor(+1, 0, shift); break;
    case Qt::Key_Home:
        moveCursor(0, -m_cursor.column, shift); break;
    case Qt::Key_End: {
        int len = 0;
        if (m_document) {
            QMutexLocker lock(&m_docMutex);
            len = m_document->buffer().lineLength(m_cursor.line);
        }
        moveCursor(0, len - m_cursor.column, shift);
        break;
    }
    case Qt::Key_PageUp: {
        const int lines = qMax(1, int(height() / m_lineHeight) - 1);
        moveCursor(-lines, 0, shift);
        break;
    }
    case Qt::Key_PageDown: {
        const int lines = qMax(1, int(height() / m_lineHeight) - 1);
        moveCursor(+lines, 0, shift);
        break;
    }
    case Qt::Key_Return:
    case Qt::Key_Enter:
        if (m_document) {
            QMutexLocker lock(&m_docMutex);
            deleteSelection();
            m_cursor = TextBuffer::clampPosition(
                {m_cursor.line, m_cursor.column}, m_document->buffer());
            m_document->buffer().insertText(
                {m_cursor.line, m_cursor.column}, QStringLiteral("\n"));
            moveCursorAfterInsert(QStringLiteral("\n"));
            markEdited();
        }
        break;
    case Qt::Key_Backspace:
        if (m_document) {
            QMutexLocker lock(&m_docMutex);
            TextBuffer &buf = m_document->buffer();
            if (m_cursor == m_anchor) {
                if (m_cursor.line == 0 && m_cursor.column == 0)
                    break;
                if (m_cursor.column == 0) { // склейка с предыдущей строкой
                    const int prevLen = buf.lineLength(m_cursor.line - 1);
                    buf.removeText({m_cursor.line - 1, prevLen},
                                   {m_cursor.line, 0});
                    m_cursor = {m_cursor.line - 1, prevLen};
                } else {
                    buf.removeText({m_cursor.line, m_cursor.column - 1},
                                   m_cursor);
                    m_cursor = {m_cursor.line, m_cursor.column - 1};
                }
            } else {
                deleteSelection();
            }
            markEdited();
        }
        break;
    case Qt::Key_Delete:
        if (m_document) {
            QMutexLocker lock(&m_docMutex);
            TextBuffer &buf = m_document->buffer();
            if (m_cursor == m_anchor) {
                if (m_cursor.column < buf.lineLength(m_cursor.line))
                    buf.removeText(m_cursor,
                                   {m_cursor.line, m_cursor.column + 1});
                else if (m_cursor.line < buf.lineCount() - 1)
                    buf.removeText(m_cursor, {m_cursor.line + 1, 0});
            } else {
                deleteSelection();
            }
            markEdited();
        }
        break;
    default:
        if (m_document && !event->text().isEmpty()
            && event->text().at(0).isPrint()) {
            QMutexLocker lock(&m_docMutex);
            deleteSelection();
            m_cursor = TextBuffer::clampPosition(
                {m_cursor.line, m_cursor.column}, m_document->buffer());
            m_document->buffer().insertText(
                {m_cursor.line, m_cursor.column}, event->text());
            moveCursorAfterInsert(event->text());
            markEdited();
        } else {
            return QQuickItem::keyPressEvent(event);
        }
    }
    event->accept();
}

namespace {
// Клик по окну -> позиция (линия, колонка) в моноширинной сетке.
core::TextBuffer::Position fromMouse(const QPointF &pos, const TextBuffer &buf,
                 qreal lineHeight, qreal charWidth)
{
    core::TextBuffer::Position c;
    c.line = qBound(0, int(pos.y() / lineHeight), buf.lineCount() - 1);
    c.column = qBound(0, int(qRound(pos.x() / charWidth)),
                      buf.lineLength(c.line));
    return c;
}
} // namespace


void EditorView::mousePressEvent(QMouseEvent *event)
{
    if (!m_document)
        return;
    forceActiveFocus();
    m_mousePressed = true;
    QMutexLocker lock(&m_docMutex);
    const Cursor c = fromMouse(event->position(), m_document->buffer(),
                               m_lineHeight, m_charWidth);
    m_cursor = c;
    m_anchor = c;
    m_goalColumn = c.column;
    lock.unlock();
    update();
    emit cursorChanged();
    event->accept();
}

void EditorView::mouseMoveEvent(QMouseEvent *event)
{
    if (!m_mousePressed || !m_document)
        return;
    QMutexLocker lock(&m_docMutex);
    m_cursor = fromMouse(event->position(), m_document->buffer(),
                         m_lineHeight, m_charWidth);
    m_goalColumn = m_cursor.column;
    lock.unlock();
    update();
    emit cursorChanged();
    event->accept();
}

void EditorView::mouseReleaseEvent(QMouseEvent *event)
{
    m_mousePressed = false;
    event->accept();
}

QSGNode *EditorView::updatePaintNode(QSGNode *oldNode, UpdatePaintNodeData *)
{
    QSGNode *root = oldNode;
    if (!root) {
        root = new QSGNode;
        m_background = new QSGSimpleRectNode(QRectF(0, 0, width(), height()),
                                             kBackgroundColor);
        root->appendChildNode(m_background);
        m_clip = new QSGClipNode;
        m_clip->setClipRect(QRectF(0, 0, width(), height()));
        m_clip->setIsRectangular(true);
        root->appendChildNode(m_clip);
        m_scrollTransform = new QSGTransformNode;
        m_clip->appendChildNode(m_scrollTransform);
        m_selLayer = new QSGNode; // первый ребёнок — ниже текстовых нод
        m_scrollTransform->appendChildNode(m_selLayer);
        m_caret = new QSGSimpleRectNode(QRectF(), kCaretColor);
        root->appendChildNode(m_caret); // каретка вне клипа — всегда видна
    }

    if (QSizeF(width(), height()) != m_lastSize) {
        m_lastSize = QSizeF(width(), height());
        m_background->setRect(QRectF(0, 0, width(), height()));
        m_clip->setClipRect(QRectF(0, 0, width(), height()));
        m_lastFirstLine = -1;
    }

    if (!m_document) {
        // пустая сцена
        return root;
    }

    QMatrix4x4 m;
    m.translate(0.0, float(-m_scrollY));
    m_scrollTransform->setMatrix(m);
    m_scrollTransform->markDirty(QSGNode::DirtyMatrix);

    int lineCount, firstLine, visibleCount;
    QStringList visibleLines;
    Cursor selA, selB, caret;
    QVector<QRectF> selRects;
    {
        QMutexLocker lock(&m_docMutex);
        TextBuffer &buf = m_document->buffer();
        lineCount = buf.lineCount();
        if (lineCount == 0)
            return root;
        firstLine = qBound(0, int(m_scrollY / m_lineHeight), lineCount - 1);
        visibleCount = qCeil(height() / m_lineHeight) + 1;
        const int take = qMin(visibleCount, lineCount - firstLine);
        visibleLines.reserve(take);
        for (int i = 0; i < take; ++i)
            visibleLines.append(buf.lineAt(firstLine + i));
        caret = m_cursor;
        selA = qMin(m_anchor, m_cursor);
        selB = qMax(m_anchor, m_cursor);

        // Прямоугольники выделения — только для видимых строк.
        if (!(selA == selB)) {
            const int from = qMax(selA.line, firstLine);
            const int to = qMin(selB.line, firstLine + visibleCount - 1);
            for (int line = from; line <= to; ++line) {
                const int x0 = line == selA.line ? selA.column : 0;
                const int x1 = line == selB.line
                                   ? selB.column
                                   : buf.lineLength(line);
                const qreal y = line * m_lineHeight - m_scrollY;
                selRects.append(QRectF(
                    x0 * m_charWidth, y,
                    qMax(qreal(x1 - x0) * m_charWidth, m_charWidth * 0.5),
                    m_lineHeight));
            }
        }
    }

    // Текстовые ноды: пересобираем при смене первой видимой строки или правке.
    if (firstLine != m_lastFirstLine) {
        const int take = visibleLines.size();
        while (m_textPool.size() < take) {
            QSGTextNode *node = window()->createTextNode();
            node->setColor(kTextColor);
            node->setRenderType(QSGTextNode::RenderType::QtRendering);
            m_scrollTransform->appendChildNode(node);
            m_textPool.append(node);
        }
        while (m_textPool.size() > take) {
            QSGTextNode *node = m_textPool.takeLast();
            m_scrollTransform->removeChildNode(node);
            delete node;
        }
        for (int i = 0; i < take; ++i) {
            QSGTextNode *node = m_textPool.at(i);
            QMatrix4x4 nm;
            nm.translate(0.0, float((firstLine + i) * m_lineHeight));
            node->setMatrix(nm);

            QTextLayout layout(visibleLines.at(i), m_font);
            layout.beginLayout();
            QTextLine line = layout.createLine();
            if (line.isValid())
                line.setLineWidth(100000);
            layout.endLayout();
            node->clear();
            node->addTextLayout(QPointF(0.0, 0.0), &layout);
        }
        m_lastFirstLine = firstLine;
    }

    // Выделение: прямоугольник на каждую видимую выбранную строку.
    for (QSGSimpleRectNode *n : m_selPool) {
        m_selLayer->removeChildNode(n);
        delete n;
    }
    m_selPool.clear();

    for (const QRectF &r : selRects) {
        QSGSimpleRectNode *rect = new QSGSimpleRectNode(r, kSelectionColor);
        m_selLayer->appendChildNode(rect);
        m_selPool.append(rect);
    }

    // Каретка.
    m_caret->setRect(QRectF(caret.column * m_charWidth + 1.0,
                            caret.line * m_lineHeight - m_scrollY,
                            2.0, m_lineHeight));
    return root;
}