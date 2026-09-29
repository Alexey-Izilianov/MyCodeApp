#include "editerview.h"

#include <QClipboard>
#include <QFileInfo>
#include <QFontMetricsF>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QQuickWindow>
#include <QSGClipNode>
#include <QSGSimpleRectNode>
#include <QSGTransformNode>
#include <QTextLayout>
#include <QWheelEvent>
#include <QtMath>
#include <qsgtextnode.h>

#include "src/services/searchengine.h"

using core::Document;
using core::TextBuffer;

namespace {
// Палитра редактора — синхронно с Theme.qml
constexpr QRgb kBackground = 0x1e1f22;
constexpr QRgb kText = 0xc5c8ce;
constexpr QRgb kCurrentLine = 0x26282e;
constexpr QRgb kSelection = 0x2e436e;
constexpr QRgb kCaret = 0xced0d6;
constexpr QRgb kMatch = 0x3b4a2c;
constexpr QRgb kCurrentMatch = 0x6a5520;
constexpr QRgb kGutterText = 0x4e525a;
constexpr QRgb kGutterCurrent = 0xa0a4ac;

constexpr int kTabSpaces = 4;
constexpr int kColumnMargin = 256; // запас колонок при раскладке длинных строк

enum class CharClass { Space, Word, Punct };

CharClass classOf(QChar c)
{
    if (c.isSpace())
        return CharClass::Space;
    return c.isLetterOrNumber() || c == u'_' ? CharClass::Word : CharClass::Punct;
}

QString leadingWhitespace(const QString &line)
{
    int n = 0;
    while (n < line.size() && (line.at(n) == u' ' || line.at(n) == u'\t'))
        ++n;
    return line.left(n);
}
} // namespace

EditorView::EditorView(QQuickItem *parent)
    : QQuickItem(parent)
{
    setFlag(ItemHasContents, true);
    setClip(true); // частично прокрученная строка не должна вылезать за границы
    setFlag(ItemAcceptsInputMethod, true);
    setAcceptedMouseButtons(Qt::LeftButton);
    setActiveFocusOnTab(true);

    m_font.setFamilies({QStringLiteral("Cascadia Mono"), QStringLiteral("Consolas")});
    m_font.setStyleHint(QFont::Monospace);
    m_font.setPixelSize(14);
    const QFontMetricsF fm(m_font);
    m_charWidth = fm.horizontalAdvance(u'M');
    m_lineHeight = qCeil(fm.height() * 1.35);
    m_textOffsetY = qRound((m_lineHeight - fm.height()) / 2);
    updateGutterWidth();

    m_caretTimer.setInterval(530);
    connect(&m_caretTimer, &QTimer::timeout, this, [this] {
        m_caretVisible = !m_caretVisible;
        update();
    });

    m_highlighter.loadRules(QStringLiteral(":/assets/highlight.json"));
    connect(&m_syntax, &SyntaxHighlighter::updated, this, [this] {
        m_contentDirty = true;
        update();
    });

    m_searchEngine = new SearchEngine(this);
    connect(m_searchEngine, &SearchEngine::resultsReady, this,
            [this](const QVector<SearchEngine::Match> &matches) {
                m_matches.clear();
                m_matches.reserve(matches.size());
                for (const auto &m : matches)
                    m_matches.append({m.line, m.column, m.length});
                m_currentMatch = nearestMatchFromCursor();
                emit searchUpdated(m_currentMatch + 1, int(m_matches.size()));
                update();
            });
}

EditorView::~EditorView() = default;

// ── Документ ────────────────────────────────────────────────────────────

void EditorView::setDocument(QObject *object)
{
    auto *doc = qobject_cast<Document *>(object);
    if (doc == m_document)
        return;

    if (m_document) {
        Document *old = m_document;
        if (!m_viewStates.contains(old))
            connect(old, &QObject::destroyed, this, [this, old] { m_viewStates.remove(old); });
        m_viewStates.insert(old, {m_cursor, m_anchor, m_scrollX, m_scrollY});
    }

    m_document = doc;
    const ViewState state = doc ? m_viewStates.value(doc) : ViewState{};
    m_cursor = state.cursor;
    m_anchor = state.anchor;
    m_goalColumn = m_cursor.column;
    m_scrollX = state.scrollX;
    m_scrollY = state.scrollY;

    const QString path = filePath();
    m_highlighter.setLanguageForFile(path);
    if (m_syntax.setLanguageForFile(path) && doc) {
        doc->takeEdits(); // правки до показа во вью дерево не касаются
        m_syntax.reset(buffer().snapshot());
    }

    updateGutterWidth();
    m_contentDirty = true;
    clearSearch();
    restartCaretBlink();
    emit documentChanged();
    emit contentSizeChanged();
    emit scrollChanged();
    emit cursorChanged();
    emit historyChanged();
    update();
}

QString EditorView::filePath() const
{
    return m_document ? m_document->filePath() : QString();
}

QString EditorView::encoding() const
{
    return m_document ? core::encodingName(buffer().encoding()) : QString();
}

QString EditorView::lineEnding() const
{
    return m_document ? core::lineEndingName(buffer().lineEnding()) : QString();
}

QString EditorView::language() const
{
    static const QHash<QString, QString> names = {
        {"c", "C"},       {"h", "C++"},      {"cc", "C++"},     {"cpp", "C++"},
        {"cxx", "C++"},   {"hh", "C++"},     {"hpp", "C++"},    {"hxx", "C++"},
        {"inl", "C++"},   {"py", "Python"},  {"pyw", "Python"}, {"pyi", "Python"},
        {"json", "JSON"}, {"jsonc", "JSON"}, {"md", "Markdown"}, {"markdown", "Markdown"},
    };
    if (!m_document)
        return {};
    return names.value(QFileInfo(filePath()).suffix().toLower(), QStringLiteral("Текст"));
}

qreal EditorView::contentHeight() const
{
    // Последнюю строку можно прокрутить до верха окна
    return m_document ? (buffer().lineCount() - 1) * m_lineHeight + height() : 0;
}

void EditorView::save()
{
    QString error;
    if (m_document && !m_document->save(&error))
        emit errorOccurred(error);
}

// ── Правки ──────────────────────────────────────────────────────────────

void EditorView::replaceSelection(const QString &text, EditKind kind)
{
    replaceRange(qMin(m_cursor, m_anchor), qMax(m_cursor, m_anchor), text, kind);
}

void EditorView::replaceRange(Cursor from, Cursor to, const QString &text, EditKind kind)
{
    if (!m_document)
        return;
    const Cursor end = m_document->replace(from, to, text, kind, {m_cursor, m_anchor});
    m_cursor = m_anchor = end;
    m_goalColumn = end.column;
    afterTextChange();
}

void EditorView::afterTextChange()
{
    bool overflow = false;
    const QVector<TextBuffer::Edit> edits = m_document->takeEdits(&overflow);
    if (m_syntax.hasLanguage()) {
        if (overflow)
            m_syntax.reset(buffer().snapshot());
        else
            m_syntax.applyEdits(edits, buffer().snapshot());
    }
    if (!m_matches.isEmpty()) { // позиции совпадений после правки неверны
        m_matches.clear();
        m_currentMatch = -1;
        emit searchUpdated(0, 0);
    }
    m_cursor = TextBuffer::clampPosition(m_cursor, buffer());
    m_anchor = TextBuffer::clampPosition(m_anchor, buffer());
    updateGutterWidth();
    m_contentDirty = true;
    ensureCursorVisible();
    restartCaretBlink();
    emit contentSizeChanged();
    emit cursorChanged();
    emit historyChanged();
    update();
}

void EditorView::undo()
{
    Document::Selection s;
    if (m_document && m_document->undo(&s)) {
        m_cursor = s.cursor;
        m_anchor = s.anchor;
        m_goalColumn = s.cursor.column;
        afterTextChange();
    }
}

void EditorView::redo()
{
    Document::Selection s;
    if (m_document && m_document->redo(&s)) {
        m_cursor = s.cursor;
        m_anchor = s.anchor;
        m_goalColumn = s.cursor.column;
        afterTextChange();
    }
}

void EditorView::copy()
{
    if (m_document && hasSelection())
        QGuiApplication::clipboard()->setText(buffer().text(m_anchor, m_cursor));
}

void EditorView::cut()
{
    if (!m_document || !hasSelection())
        return;
    copy();
    replaceSelection({}, EditKind::Other);
}

void EditorView::paste()
{
    const QString text = QGuiApplication::clipboard()->text();
    if (m_document && !text.isEmpty())
        replaceSelection(text, EditKind::Other);
}

void EditorView::selectAll()
{
    if (!m_document)
        return;
    m_anchor = {0, 0};
    moveCursor(buffer().positionOf(buffer().length()), true);
}

// ── Курсор и прокрутка ──────────────────────────────────────────────────

void EditorView::moveCursor(Cursor to, bool extend)
{
    if (!m_document)
        return;
    m_cursor = TextBuffer::clampPosition(to, buffer());
    if (!extend)
        m_anchor = m_cursor;
    m_document->breakUndoGroup();
    ensureCursorVisible();
    restartCaretBlink();
    emit cursorChanged();
    update();
}

EditorView::Cursor EditorView::stepLeft(Cursor c, bool word) const
{
    if (c.column == 0)
        return c.line > 0 ? Cursor{c.line - 1, buffer().lineLength(c.line - 1)} : c;
    if (!word)
        return {c.line, c.column - 1};
    const QString line = buffer().lineAt(c.line);
    int i = c.column;
    while (i > 0 && classOf(line.at(i - 1)) == CharClass::Space)
        --i;
    if (i > 0) {
        const CharClass cls = classOf(line.at(i - 1));
        while (i > 0 && classOf(line.at(i - 1)) == cls)
            --i;
    }
    return {c.line, i};
}

EditorView::Cursor EditorView::stepRight(Cursor c, bool word) const
{
    const int len = buffer().lineLength(c.line);
    if (c.column >= len)
        return c.line + 1 < buffer().lineCount() ? Cursor{c.line + 1, 0} : c;
    if (!word)
        return {c.line, c.column + 1};
    const QString line = buffer().lineAt(c.line);
    int i = c.column;
    const CharClass cls = classOf(line.at(i));
    while (i < len && classOf(line.at(i)) == cls)
        ++i;
    while (i < len && classOf(line.at(i)) == CharClass::Space)
        ++i;
    return {c.line, i};
}

EditorView::Cursor EditorView::cursorAt(const QPointF &pos) const
{
    const int line = qBound(0, int((pos.y() + m_scrollY) / m_lineHeight), buffer().lineCount() - 1);
    const int column = qRound((pos.x() - textLeft() + m_scrollX) / m_charWidth);
    return {line, qBound(0, column, buffer().lineLength(line))};
}

void EditorView::ensureCursorVisible()
{
    qreal x = m_scrollX, y = m_scrollY;
    const qreal top = m_cursor.line * m_lineHeight;
    if (top < y)
        y = top;
    else if (top + m_lineHeight > y + height())
        y = top + m_lineHeight - height();

    const qreal caretX = m_cursor.column * m_charWidth;
    const qreal margin = 4 * m_charWidth;
    if (caretX < x)
        x = qMax<qreal>(0, caretX - margin);
    else if (caretX > x + textWidth() - m_charWidth)
        x = caretX - textWidth() + margin;
    setScroll(x, y);
}

void EditorView::setScroll(qreal x, qreal y)
{
    if (!m_document)
        return;
    const int lines = buffer().lineCount();
    y = qBound<qreal>(0, y, (lines - 1) * m_lineHeight);

    // Предел по X — самая длинная из видимых строк (и строка курсора)
    int longest = buffer().lineLength(m_cursor.line);
    const int first = int(y / m_lineHeight);
    const int last = qMin(lines - 1, first + int(height() / m_lineHeight) + 1);
    for (int i = first; i <= last; ++i)
        longest = qMax(longest, buffer().lineLength(i));
    x = qBound<qreal>(0, x, qMax<qreal>(0, (longest + 4) * m_charWidth - textWidth()));

    if (x == m_scrollX && y == m_scrollY)
        return;
    m_scrollX = x;
    m_scrollY = y;
    emit scrollChanged();
    update();
}

void EditorView::setScrollY(qreal y)
{
    setScroll(m_scrollX, y);
}

void EditorView::restartCaretBlink()
{
    m_caretVisible = true;
    if (hasActiveFocus())
        m_caretTimer.start();
}

void EditorView::updateGutterWidth()
{
    const int lines = m_document ? buffer().lineCount() : 1;
    const int digits = qMax(3, int(QString::number(lines).size()));
    const qreal width = (digits + 4) * m_charWidth;
    if (width != m_gutterWidth) {
        m_gutterWidth = width;
        m_contentDirty = true;
    }
}

// ── Поиск ───────────────────────────────────────────────────────────────

void EditorView::find(const QString &needle, bool caseSensitive)
{
    if (!m_document || needle.isEmpty()) {
        clearSearch();
        return;
    }
    m_searchNeedle = needle;
    m_searchCase = caseSensitive;
    m_searchEngine->start(buffer().snapshot(), needle, caseSensitive);
}

void EditorView::findNext()
{
    if (m_matches.isEmpty()) {
        if (!m_searchNeedle.isEmpty())
            find(m_searchNeedle, m_searchCase);
        return;
    }
    jumpToMatch(m_currentMatch < 0 ? nearestMatchFromCursor()
                                   : (m_currentMatch + 1) % int(m_matches.size()));
}

void EditorView::findPrev()
{
    if (m_matches.isEmpty()) {
        if (!m_searchNeedle.isEmpty())
            find(m_searchNeedle, m_searchCase);
        return;
    }
    const int n = int(m_matches.size());
    jumpToMatch(m_currentMatch < 0 ? nearestMatchFromCursor() : (m_currentMatch - 1 + n) % n);
}

void EditorView::clearSearch()
{
    m_searchEngine->cancel();
    m_matches.clear();
    m_currentMatch = -1;
    m_searchNeedle.clear();
    emit searchUpdated(0, 0);
    update();
}

int EditorView::nearestMatchFromCursor() const
{
    for (int i = 0; i < m_matches.size(); ++i) {
        const Match &m = m_matches.at(i);
        if (!(Cursor{m.line, m.column} < m_cursor))
            return i;
    }
    return int(m_matches.size()) - 1;
}

void EditorView::jumpToMatch(int index)
{
    if (!m_document || index < 0 || index >= m_matches.size())
        return;
    m_currentMatch = index;
    const Match &m = m_matches.at(index);
    m_anchor = {m.line, m.column};
    m_goalColumn = m.column + m.length;
    moveCursor({m.line, m.column + m.length}, true);
    emit searchUpdated(index + 1, int(m_matches.size()));
}

// ── Ввод ────────────────────────────────────────────────────────────────

void EditorView::keyPressEvent(QKeyEvent *event)
{
    if (!m_document)
        return QQuickItem::keyPressEvent(event);

    const bool shift = event->modifiers() & Qt::ShiftModifier;
    const bool ctrl = event->modifiers() & Qt::ControlModifier;
    const int key = event->key();

    if (ctrl) {
        switch (key) {
        case Qt::Key_A: selectAll(); break;
        case Qt::Key_C: copy(); break;
        case Qt::Key_X: cut(); break;
        case Qt::Key_V: paste(); break;
        case Qt::Key_Z: shift ? redo() : undo(); break;
        case Qt::Key_Y: redo(); break;
        case Qt::Key_Left: moveCursor(stepLeft(m_cursor, true), shift); break;
        case Qt::Key_Right: moveCursor(stepRight(m_cursor, true), shift); break;
        case Qt::Key_Home: moveCursor({0, 0}, shift); break;
        case Qt::Key_End: moveCursor(buffer().positionOf(buffer().length()), shift); break;
        case Qt::Key_Backspace:
            hasSelection() ? replaceSelection({}, EditKind::Other)
                           : replaceRange(stepLeft(m_cursor, true), m_cursor, {}, EditKind::Other);
            break;
        case Qt::Key_Delete:
            hasSelection() ? replaceSelection({}, EditKind::Other)
                           : replaceRange(m_cursor, stepRight(m_cursor, true), {}, EditKind::Other);
            break;
        default:
            return QQuickItem::keyPressEvent(event);
        }
        if (key == Qt::Key_Left || key == Qt::Key_Right)
            m_goalColumn = m_cursor.column;
        return event->accept();
    }

    const int pageLines = qMax(1, int(height() / m_lineHeight) - 1);
    auto vertical = [&](int delta) {
        const int line = qBound(0, m_cursor.line + delta, buffer().lineCount() - 1);
        moveCursor({line, qMin(m_goalColumn, buffer().lineLength(line))}, shift);
    };

    switch (key) {
    case Qt::Key_Left:
        moveCursor(hasSelection() && !shift ? qMin(m_cursor, m_anchor) : stepLeft(m_cursor, false), shift);
        m_goalColumn = m_cursor.column;
        break;
    case Qt::Key_Right:
        moveCursor(hasSelection() && !shift ? qMax(m_cursor, m_anchor) : stepRight(m_cursor, false), shift);
        m_goalColumn = m_cursor.column;
        break;
    case Qt::Key_Up: vertical(-1); break;
    case Qt::Key_Down: vertical(+1); break;
    case Qt::Key_PageUp: vertical(-pageLines); break;
    case Qt::Key_PageDown: vertical(+pageLines); break;
    case Qt::Key_Home: {
        // «Умный» Home: сначала к первому непробельному символу, затем в колонку 0
        const int indent = int(leadingWhitespace(buffer().lineAt(m_cursor.line)).size());
        moveCursor({m_cursor.line, m_cursor.column == indent ? 0 : indent}, shift);
        m_goalColumn = m_cursor.column;
        break;
    }
    case Qt::Key_End:
        moveCursor({m_cursor.line, buffer().lineLength(m_cursor.line)}, shift);
        m_goalColumn = m_cursor.column;
        break;
    case Qt::Key_Escape:
        if (!hasSelection())
            return QQuickItem::keyPressEvent(event);
        moveCursor(m_cursor, false);
        break;
    case Qt::Key_Return:
    case Qt::Key_Enter:
        replaceSelection(u'\n' + leadingWhitespace(buffer().lineAt(qMin(m_cursor, m_anchor).line)),
                         EditKind::Other);
        break;
    case Qt::Key_Tab:
        replaceSelection(QString(kTabSpaces - m_cursor.column % kTabSpaces, u' '), EditKind::Typing);
        break;
    case Qt::Key_Backspace:
        hasSelection() ? replaceSelection({}, EditKind::Other)
                       : replaceRange(stepLeft(m_cursor, false), m_cursor, {}, EditKind::Typing);
        break;
    case Qt::Key_Delete:
        hasSelection() ? replaceSelection({}, EditKind::Other)
                       : replaceRange(m_cursor, stepRight(m_cursor, false), {}, EditKind::Typing);
        break;
    default:
        if (event->text().isEmpty() || !event->text().at(0).isPrint())
            return QQuickItem::keyPressEvent(event);
        replaceSelection(event->text(), hasSelection() ? EditKind::Other : EditKind::Typing);
    }
    event->accept();
}

void EditorView::mousePressEvent(QMouseEvent *event)
{
    if (!m_document)
        return;
    forceActiveFocus();
    m_mousePressed = true;
    const Cursor c = cursorAt(event->position());
    if (event->position().x() < textLeft()) { // клик по номеру строки — вся строка
        m_anchor = {c.line, 0};
        moveCursor(c.line + 1 < buffer().lineCount() ? Cursor{c.line + 1, 0}
                                                     : Cursor{c.line, buffer().lineLength(c.line)},
                   true);
    } else {
        moveCursor(c, event->modifiers() & Qt::ShiftModifier);
    }
    m_goalColumn = m_cursor.column;
    event->accept();
}

void EditorView::mouseMoveEvent(QMouseEvent *event)
{
    if (!m_mousePressed || !m_document)
        return;
    moveCursor(cursorAt(event->position()), true);
    m_goalColumn = m_cursor.column;
    event->accept();
}

void EditorView::mouseReleaseEvent(QMouseEvent *event)
{
    m_mousePressed = false;
    event->accept();
}

void EditorView::mouseDoubleClickEvent(QMouseEvent *event)
{
    if (!m_document)
        return;
    const Cursor c = cursorAt(event->position());
    const QString line = buffer().lineAt(c.line);
    if (line.isEmpty())
        return event->accept();
    const int at = qMin(c.column, int(line.size()) - 1);
    const CharClass cls = classOf(line.at(at));
    int from = at, to = at;
    while (from > 0 && classOf(line.at(from - 1)) == cls)
        --from;
    while (to < line.size() && classOf(line.at(to)) == cls)
        ++to;
    m_anchor = {c.line, from};
    moveCursor({c.line, to}, true);
    m_goalColumn = to;
    event->accept();
}

void EditorView::wheelEvent(QWheelEvent *event)
{
    QPointF delta = !event->pixelDelta().isNull()
                        ? QPointF(event->pixelDelta())
                        : QPointF(event->angleDelta()) / 120.0 * (3 * m_lineHeight);
    if ((event->modifiers() & Qt::ShiftModifier) && delta.x() == 0)
        delta = {delta.y(), 0};
    setScroll(m_scrollX - delta.x(), m_scrollY - delta.y());
    event->accept();
}

void EditorView::geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry)
{
    QQuickItem::geometryChange(newGeometry, oldGeometry);
    m_contentDirty = true;
    emit contentSizeChanged();
    update();
}

void EditorView::focusInEvent(QFocusEvent *event)
{
    QQuickItem::focusInEvent(event);
    restartCaretBlink();
    update();
}

void EditorView::focusOutEvent(QFocusEvent *event)
{
    QQuickItem::focusOutEvent(event);
    m_caretTimer.stop();
    update();
}

// ── Рендер ──────────────────────────────────────────────────────────────

QSGTextNode *EditorView::textNodeFor(QRgb color)
{
    if (QSGTextNode *node = m_textNodes.value(color))
        return node;
    QSGTextNode *node = window()->createTextNode();
    node->setRenderType(QSGTextNode::NativeRendering);
    node->setColor(QColor(color));
    m_textLayer->appendChildNode(node);
    m_textNodes.insert(color, node);
    return node;
}

void EditorView::buildTextNodes(int firstLine, const QStringList &lines, int column0, int column1)
{
    for (QSGTextNode *node : std::as_const(m_textNodes))
        node->clear();

    // tree-sitter, пока его дерева нет (первый разбор, большой файл) — регэкспы
    const bool treeSitter = m_syntax.isReady();
    QVector<QVector<Highlighter::Span>> spans(lines.size());
    if (treeSitter) {
        m_syntax.highlightLines(firstLine, lines, spans);
    } else if (m_highlighter.hasLanguage()) {
        for (int i = 0; i < lines.size(); ++i)
            m_highlighter.highlightLine(lines.at(i), spans[i]);
    }

    auto addRun = [&](const QString &text, int line, int from, int to, QRgb color) {
        from = qMax(from, column0);
        to = qMin(to, column1);
        if (from >= to)
            return;
        const QString piece = text.mid(from, to - from);
        if (piece.trimmed().isEmpty())
            return;
        QTextLayout layout(piece, m_font);
        layout.beginLayout();
        layout.createLine().setLineWidth(1e6);
        layout.endLayout();
        textNodeFor(color)->addTextLayout(
            QPointF(from * m_charWidth, line * m_lineHeight + m_textOffsetY), &layout);
    };

    for (int i = 0; i < lines.size(); ++i) {
        const QString &text = lines.at(i);
        const int line = firstLine + i;
        int pos = 0;
        for (const Highlighter::Span &s : std::as_const(spans.at(i))) {
            addRun(text, line, pos, s.start, kText);
            const QColor color = treeSitter ? SyntaxHighlighter::color(s.rule)
                                            : m_highlighter.ruleColor(s.rule);
            addRun(text, line, s.start, s.start + s.length, color.isValid() ? color.rgb() : kText);
            pos = s.start + s.length;
        }
        addRun(text, line, pos, int(text.size()), kText);
    }
}

void EditorView::buildGutterNodes(int firstLine, int count)
{
    m_gutterNode->clear();
    m_gutterCurrentNode->clear();
    const qreal right = m_gutterWidth - 2 * m_charWidth;
    for (int line = firstLine; line < firstLine + count; ++line) {
        const QString number = QString::number(line + 1);
        QTextLayout layout(number, m_font);
        layout.beginLayout();
        layout.createLine();
        layout.endLayout();
        QSGTextNode *node = line == m_cursor.line ? m_gutterCurrentNode : m_gutterNode;
        node->addTextLayout(QPointF(right - number.size() * m_charWidth,
                                    line * m_lineHeight + m_textOffsetY),
                            &layout);
    }
}

void EditorView::setRects(QSGNode *layer, QVector<QSGSimpleRectNode *> &pool,
                          const QVector<QPair<QRectF, QColor>> &rects)
{
    while (pool.size() > rects.size()) {
        QSGSimpleRectNode *node = pool.takeLast();
        layer->removeChildNode(node);
        delete node;
    }
    for (int i = 0; i < rects.size(); ++i) {
        if (i == pool.size()) {
            pool.append(new QSGSimpleRectNode(rects.at(i).first, rects.at(i).second));
            layer->appendChildNode(pool.last());
        } else {
            pool.at(i)->setRect(rects.at(i).first);
            pool.at(i)->setColor(rects.at(i).second);
        }
    }
}

void EditorView::setRects(QSGNode *layer, QVector<QSGSimpleRectNode *> &pool,
                          const QVector<QRectF> &rects, const QColor &color)
{
    QVector<QPair<QRectF, QColor>> colored;
    colored.reserve(rects.size());
    for (const QRectF &r : rects)
        colored.append({r, color});
    setRects(layer, pool, colored);
}

QSGNode *EditorView::updatePaintNode(QSGNode *oldNode, UpdatePaintNodeData *)
{
    if (!oldNode) {
        // Сцена создаётся заново (в т.ч. после потери графического контекста):
        // старые ноды удалены scene graph'ом — забываем указатели.
        m_textNodes.clear();
        m_selectionPool.clear();
        m_matchPool.clear();
        m_builtFirstLine = -1;

        m_root = new QSGNode;
        m_background = new QSGSimpleRectNode({}, QColor(kBackground));
        m_currentLine = new QSGSimpleRectNode({}, QColor(kCurrentLine));
        m_textClip = new QSGClipNode;
        m_textClip->setIsRectangular(true);
        m_textTransform = new QSGTransformNode;
        m_selectionLayer = new QSGNode;
        m_matchLayer = new QSGNode;
        m_textLayer = new QSGNode;
        m_caret = new QSGSimpleRectNode({}, QColor(kCaret));
        m_gutterTransform = new QSGTransformNode;
        m_gutterNode = window()->createTextNode();
        m_gutterNode->setRenderType(QSGTextNode::NativeRendering);
        m_gutterNode->setColor(QColor(kGutterText));
        m_gutterCurrentNode = window()->createTextNode();
        m_gutterCurrentNode->setRenderType(QSGTextNode::NativeRendering);
        m_gutterCurrentNode->setColor(QColor(kGutterCurrent));

        m_root->appendChildNode(m_background);
        m_root->appendChildNode(m_currentLine);
        m_root->appendChildNode(m_textClip);
        m_textClip->appendChildNode(m_textTransform);
        m_textTransform->appendChildNode(m_selectionLayer); // слои под текстом
        m_textTransform->appendChildNode(m_matchLayer);
        m_textTransform->appendChildNode(m_textLayer);
        m_textTransform->appendChildNode(m_caret);
        m_root->appendChildNode(m_gutterTransform);
        m_gutterTransform->appendChildNode(m_gutterNode);
        m_gutterTransform->appendChildNode(m_gutterCurrentNode);
    }

    m_background->setRect(boundingRect());
    const int lineCount = m_document ? buffer().lineCount() : 0;
    if (lineCount == 0) {
        for (QSGTextNode *node : std::as_const(m_textNodes))
            node->clear();
        m_gutterNode->clear();
        m_gutterCurrentNode->clear();
        setRects(m_selectionLayer, m_selectionPool, {});
        setRects(m_matchLayer, m_matchPool, {});
        m_currentLine->setRect({});
        m_caret->setRect({});
        m_builtFirstLine = -1;
        return m_root;
    }

    const int firstLine = qBound(0, int(m_scrollY / m_lineHeight), lineCount - 1);
    const int count = qMin(qCeil(height() / m_lineHeight) + 1, lineCount - firstLine);
    const int viewColumn0 = int(m_scrollX / m_charWidth);
    const int viewColumn1 = viewColumn0 + qCeil(textWidth() / m_charWidth) + 1;

    const bool rebuild = m_contentDirty || firstLine != m_builtFirstLine
                         || count != m_builtLineCount || viewColumn0 < m_builtColumn0
                         || viewColumn1 > m_builtColumn1;
    if (rebuild) {
        QStringList lines;
        lines.reserve(count);
        for (int i = 0; i < count; ++i)
            lines.append(buffer().lineAt(firstLine + i));
        m_builtColumn0 = qMax(0, viewColumn0 - kColumnMargin);
        m_builtColumn1 = viewColumn1 + kColumnMargin;
        buildTextNodes(firstLine, lines, m_builtColumn0, m_builtColumn1);
        m_builtFirstLine = firstLine;
        m_builtLineCount = count;
        m_contentDirty = false;
    }
    if (rebuild || m_cursor.line != m_builtCursorLine) {
        buildGutterNodes(firstLine, count);
        m_builtCursorLine = m_cursor.line;
    }

    QMatrix4x4 textMatrix;
    textMatrix.translate(float(textLeft() - m_scrollX), float(-m_scrollY));
    m_textTransform->setMatrix(textMatrix);
    QMatrix4x4 gutterMatrix;
    gutterMatrix.translate(0, float(-m_scrollY));
    m_gutterTransform->setMatrix(gutterMatrix);
    // Клип чуть левее текста: каретка в колонке 0 не должна обрезаться
    m_textClip->setClipRect(QRectF(textLeft() - 4, 0, textWidth() + 4, height()));

    m_currentLine->setRect(QRectF(0, m_cursor.line * m_lineHeight - m_scrollY, width(), m_lineHeight));

    // Выделение: по прямоугольнику на видимую строку, в координатах текста
    QVector<QRectF> selection;
    if (hasSelection()) {
        const Cursor a = qMin(m_cursor, m_anchor), b = qMax(m_cursor, m_anchor);
        for (int line = qMax(a.line, firstLine); line <= qMin(b.line, firstLine + count - 1); ++line) {
            const int x0 = line == a.line ? a.column : 0;
            const int x1 = line == b.line ? b.column : buffer().lineLength(line) + 1; // + перевод строки
            selection.append(QRectF(x0 * m_charWidth, line * m_lineHeight,
                                    qMax(1, x1 - x0) * m_charWidth, m_lineHeight));
        }
    }
    setRects(m_selectionLayer, m_selectionPool, selection, QColor(kSelection));

    QVector<QPair<QRectF, QColor>> matches;
    for (int i = 0; i < m_matches.size(); ++i) {
        const Match &m = m_matches.at(i);
        if (m.line < firstLine || m.line >= firstLine + count)
            continue;
        matches.append({QRectF(m.column * m_charWidth, m.line * m_lineHeight,
                               qMax(1, m.length) * m_charWidth, m_lineHeight),
                        QColor(i == m_currentMatch ? kCurrentMatch : kMatch)});
    }
    setRects(m_matchLayer, m_matchPool, matches);

    const bool caret = m_caretVisible && hasActiveFocus();
    m_caret->setRect(caret ? QRectF(m_cursor.column * m_charWidth - 1, m_cursor.line * m_lineHeight + 2,
                                    2, m_lineHeight - 4)
                           : QRectF());
    return m_root;
}
