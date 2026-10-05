#include "editerview.h"

#include <QClipboard>
#include <QFileInfo>
#include <QFontMetricsF>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QQuickWindow>
#include <QSGClipNode>
#include <QSGFlatColorMaterial>
#include <QSGGeometryNode>
#include <QSGSimpleRectNode>
#include <QSGTransformNode>
#include <QTextLayout>
#include <QWheelEvent>
#include <QtMath>
#include <qsgtextnode.h>

#include "src/services/searchengine.h"

using core::Document;
using core::TextBuffer;
namespace editing = core::editing;

namespace {
// Палитра редактора — синхронно с Theme.qml
constexpr QRgb kBackground = 0x1e1f22;
constexpr QRgb kText = 0xc5c8ce;
constexpr QRgb kCurrentLine = 0x26282e;
constexpr QRgb kSelection = 0x2e436e;
constexpr QRgb kCaret = 0xced0d6;
constexpr QRgb kMatch = 0x3b4a2c;
constexpr QRgb kCurrentMatch = 0x6a5520;
constexpr QRgb kBracket = 0x3f4552;
constexpr QRgb kGutterText = 0x4e525a;
constexpr QRgb kGutterCurrent = 0xa0a4ac;
constexpr QRgb kFoldPlaceholder = 0x33363d;

constexpr int kColumnMargin = 256;  // запас колонок при раскладке длинных строк
constexpr int kLongLine = 10000;    // длиннее — ширину считаем без учёта табуляций
constexpr int kFoldAllMaxLines = 500000;
constexpr int kEnclosingSearch = 2000; // насколько выше курсора искать охватывающий блок

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

constexpr QRgb kDiagnosticColors[3] = {0xe0605a, 0xd4a94f, 0x6c9bd2};

QSGGeometryNode *createGeometry(QRgb color, QSGGeometry::DrawingMode mode)
{
    auto *geometry = new QSGGeometry(QSGGeometry::defaultAttributes_Point2D(), 0);
    geometry->setDrawingMode(mode);
    auto *material = new QSGFlatColorMaterial;
    material->setColor(QColor(color));
    auto *node = new QSGGeometryNode;
    node->setGeometry(geometry);
    node->setMaterial(material);
    node->setFlags(QSGNode::OwnsGeometry | QSGNode::OwnsMaterial);
    return node;
}

void setVertices(QSGGeometryNode *node, const QVector<QPointF> &points)
{
    QSGGeometry *geometry = node->geometry();
    geometry->allocate(int(points.size()));
    QSGGeometry::Point2D *v = geometry->vertexDataAsPoint2D();
    for (int i = 0; i < points.size(); ++i)
        v[i].set(float(points.at(i).x()), float(points.at(i).y()));
    node->markDirty(QSGNode::DirtyGeometry);
}
} // namespace

EditorView::EditorView(QQuickItem *parent)
    : QQuickItem(parent)
{
    setFlag(ItemHasContents, true);
    setClip(true); // частично прокрученная строка не должна вылезать за границы
    setFlag(ItemAcceptsInputMethod, true);
    setAcceptedMouseButtons(Qt::LeftButton);
    setAcceptHoverEvents(true);
    setActiveFocusOnTab(true);

    m_font.setFamilies({QStringLiteral("Cascadia Mono"), QStringLiteral("Consolas")});
    m_font.setStyleHint(QFont::Monospace);
    m_font.setPixelSize(14);
    const QFontMetricsF fm(m_font);
    m_charWidth = fm.horizontalAdvance(u'M');
    m_lineHeight = qCeil(fm.height() * 1.35);
    m_textOffsetY = qRound((m_lineHeight - fm.height()) / 2);
    updateGutterWidth();

    m_hoverTimer.setSingleShot(true);
    m_hoverTimer.setInterval(500);
    connect(&m_hoverTimer, &QTimer::timeout, this, [this] {
        if (!m_document)
            return;
        // Символ под мышью (а не ближайшая граница, как у клика)
        const int line = lineAt(int((m_hoverPos.y() + m_scrollY) / m_lineHeight));
        const int visual = int((m_hoverPos.x() - textLeft() + m_scrollX) / m_charWidth);
        const QString text = buffer().lineAt(line);
        const int column = editing::columnAtVisual(text, visual);
        if (column < text.size() && editing::visualColumn(text, column) <= visual)
            emit hoverRest(line, column, m_hoverPos.x(), m_hoverPos.y());
    });

    m_caretTimer.setInterval(530);
    connect(&m_caretTimer, &QTimer::timeout, this, [this] {
        m_caretVisible = !m_caretVisible;
        update();
    });

    m_highlighter.loadRules(QStringLiteral(":/assets/highlight.json"));
    connect(&m_syntax, &SyntaxHighlighter::updated, this, &EditorView::markContentChanged);

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

    if (m_document)
        m_viewStates.insert(m_document, {m_carets, m_primary, m_scrollX, m_scrollY, m_folds});
    if (doc)
        track(doc);

    m_document = doc;
    const ViewState state = doc ? m_viewStates.value(doc) : ViewState{};
    m_carets = state.carets;
    m_primary = state.primary;
    m_boxActive = false;
    m_scrollX = state.scrollX;
    m_scrollY = state.scrollY;
    m_folds = state.folds;
    if (doc)
        normalizeCarets(); // файл мог стать короче, пока вкладка была в фоне

    const QString path = filePath();
    m_highlighter.setLanguageForFile(path);
    if (m_syntax.setLanguageForFile(path) && doc) {
        doc->takeEdits(); // правки до показа во вью дерево не касаются
        m_syntax.reset(buffer().snapshot());
    }
    m_indent = doc ? editing::detectIndent(buffer()) : editing::IndentStyle{};
    m_python = language() == QStringLiteral("Python");

    updateGutterWidth();
    updateBrackets();
    markContentChanged();
    clearSearch();
    restartCaretBlink();
    emit documentChanged();
    emit contentSizeChanged();
    emit scrollChanged();
    emit cursorChanged();
    emit historyChanged();
    update();
}

void EditorView::track(Document *doc)
{
    if (m_tracked.contains(doc))
        return;
    m_tracked.insert(doc);
    connect(doc, &QObject::destroyed, this, [this, doc] {
        m_viewStates.remove(doc);
        m_tracked.remove(doc);
    });
    connect(doc, &Document::reloaded, this, [this, doc] {
        if (doc == m_document)
            documentReloaded();
        else if (m_viewStates.contains(doc))
            m_viewStates[doc].folds.clear();
    });
}

void EditorView::documentReloaded()
{
    m_folds.clear();
    m_document->takeEdits();
    if (m_syntax.hasLanguage())
        m_syntax.reset(buffer().snapshot());
    clearSearch();
    afterTextChange();
}

void EditorView::applyTextEdits(const QVariantList &edits, int caretEdit, int caretOffset)
{
    if (!m_document || readOnly() || edits.isEmpty())
        return;
    struct Shift {
        int start;
        int end;
        int delta;
    };
    QVector<Replacement> parts;
    QVector<Shift> shifts;
    for (const QVariant &value : edits) {
        const QVariantMap e = value.toMap();
        const Cursor from = TextBuffer::clampPosition(
            {e.value(QStringLiteral("startLine")).toInt(), e.value(QStringLiteral("startColumn")).toInt()}, buffer());
        const Cursor to = TextBuffer::clampPosition(
            {e.value(QStringLiteral("endLine")).toInt(), e.value(QStringLiteral("endColumn")).toInt()}, buffer());
        const QString text = TextBuffer::normalizeNewlines(e.value(QStringLiteral("text")).toString());
        parts.append({from, to, text});
        const int start = buffer().offsetOf(from), end = buffer().offsetOf(to);
        shifts.append({start, end, int(text.size()) - (end - start)});
    }
    std::sort(shifts.begin(), shifts.end(), [](const Shift &a, const Shift &b) { return a.start < b.start; });
    // Курсор после правки сдвигается на её разницу длин, внутри заменённого — к её началу
    auto shifted = [&](int offset) {
        int delta = 0;
        for (const Shift &s : std::as_const(shifts)) {
            if (s.end <= offset)
                delta += s.delta;
            else if (s.start < offset)
                return s.start + delta;
            else
                break;
        }
        return offset + delta;
    };
    QVector<QPair<int, int>> caretOffsets;
    for (const Caret &c : std::as_const(m_carets))
        caretOffsets.append({shifted(buffer().offsetOf(c.cursor)), shifted(buffer().offsetOf(c.anchor))});

    const QVector<Cursor> ends = m_document->replace(parts, EditKind::Other, selections());
    if (caretEdit >= 0 && caretEdit < ends.size()) {
        const int end = buffer().offsetOf(ends.at(caretEdit));
        const Cursor c = buffer().positionOf(end - int(parts.at(caretEdit).text.size()) + caretOffset);
        m_carets = {Caret{c, c}};
        m_primary = 0;
    } else {
        for (int i = 0; i < m_carets.size(); ++i)
            m_carets[i] = {buffer().positionOf(caretOffsets.at(i).first), buffer().positionOf(caretOffsets.at(i).second)};
    }
    m_boxActive = false;
    normalizeCarets();
    m_document->setLastStepSelections(selections());
    afterTextChange();
}

QString EditorView::wordBeforeCursor() const
{
    if (!m_document)
        return {};
    const Cursor c = primary().cursor;
    const QString before = buffer().text({c.line, qMax(0, c.column - 256)}, c);
    qsizetype start = before.size();
    while (start > 0 && (before.at(start - 1).isLetterOrNumber() || before.at(start - 1) == u'_'))
        --start;
    return before.mid(start);
}

QString EditorView::wordAtCursor() const
{
    if (!m_document)
        return {};
    const Cursor c = primary().cursor;
    const auto [from, to] = wordAt(c);
    return buffer().lineAt(c.line).mid(from, to - from).trimmed();
}

QRectF EditorView::caretRectangle() const
{
    if (!m_document)
        return {};
    const Cursor c = primary().cursor;
    return {textLeft() - m_scrollX + visualOf(c) * m_charWidth, rowOf(c.line) * m_lineHeight - m_scrollY,
            2, m_lineHeight};
}

void EditorView::setDiagnostics(const QVariantList &diagnostics)
{
    m_diagnostics.clear();
    for (const QVariant &value : diagnostics) {
        const QVariantMap d = value.toMap();
        m_diagnostics.append({{d.value(QStringLiteral("startLine")).toInt(), d.value(QStringLiteral("startColumn")).toInt()},
                              {d.value(QStringLiteral("endLine")).toInt(), d.value(QStringLiteral("endColumn")).toInt()},
                              d.value(QStringLiteral("severity")).toInt()});
    }
    update();
}

void EditorView::goTo(int line, int column, int length)
{
    if (!m_document)
        return;
    const Cursor from = TextBuffer::clampPosition({line, column}, buffer());
    const Cursor to = TextBuffer::clampPosition({line, column + length}, buffer());
    setCarets({{to, from}}, 0);
    setScroll(m_scrollX, rowOf(from.line) * m_lineHeight - height() / 2);
}

QVariantMap EditorView::viewStateOf(QObject *object) const
{
    auto *doc = qobject_cast<Document *>(object);
    if (!doc)
        return {};
    const bool current = doc == m_document;
    const ViewState state = current ? ViewState{m_carets, m_primary, m_scrollX, m_scrollY, m_folds}
                                    : m_viewStates.value(doc);
    const Cursor c = state.carets.at(qBound(0, state.primary, int(state.carets.size()) - 1)).cursor;
    return {{QStringLiteral("line"), c.line}, {QStringLiteral("column"), c.column},
            {QStringLiteral("scrollX"), state.scrollX}, {QStringLiteral("scrollY"), state.scrollY}};
}

void EditorView::setViewStateOf(QObject *object, const QVariantMap &state)
{
    auto *doc = qobject_cast<Document *>(object);
    if (!doc)
        return;
    const Cursor c{state.value(QStringLiteral("line")).toInt(), state.value(QStringLiteral("column")).toInt()};
    const qreal x = state.value(QStringLiteral("scrollX")).toReal();
    const qreal y = state.value(QStringLiteral("scrollY")).toReal();
    if (doc == m_document) {
        setCarets({{c, c}}, 0);
        setScroll(x, y);
    } else {
        track(doc);
        m_viewStates.insert(doc, {{Caret{c, c}}, 0, x, y, {}});
    }
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
        {"qml", "QML"},
    };
    if (!m_document)
        return {};
    return names.value(QFileInfo(filePath()).suffix().toLower(), QStringLiteral("Текст"));
}

qreal EditorView::contentHeight() const
{
    // Последнюю строку можно прокрутить до верха окна
    return m_document ? (rowCount() - 1) * m_lineHeight + height() : 0;
}

void EditorView::save()
{
    QString error;
    if (m_document && !m_document->save(&error))
        emit errorOccurred(error);
}

// ── Колонки ─────────────────────────────────────────────────────────────

int EditorView::visualOf(Cursor c) const
{
    return editing::visualColumn(buffer().text({c.line, 0}, c), c.column);
}

EditorView::Cursor EditorView::fromVisual(int line, int visual) const
{
    const QString text = buffer().lineAt(line);
    return {line, qMin(editing::columnAtVisual(text, visual), int(text.size()))};
}

QChar EditorView::charAt(Cursor c, int delta) const
{
    const int i = c.column + delta;
    if (i < 0 || i >= buffer().lineLength(c.line))
        return {};
    const QString ch = buffer().textAt(buffer().offsetOf({c.line, 0}) + i, 1);
    return ch.isEmpty() ? QChar() : ch.at(0);
}

// ── Курсоры ─────────────────────────────────────────────────────────────

bool EditorView::anySelection() const
{
    return std::any_of(m_carets.cbegin(), m_carets.cend(),
                       [](const Caret &c) { return c.hasSelection(); });
}

void EditorView::normalizeCarets()
{
    const Caret primaryCaret = m_carets.at(qBound(0, m_primary, int(m_carets.size()) - 1));
    for (Caret &c : m_carets) {
        c.cursor = TextBuffer::clampPosition(c.cursor, buffer());
        c.anchor = TextBuffer::clampPosition(c.anchor, buffer());
    }
    std::sort(m_carets.begin(), m_carets.end(),
              [](const Caret &a, const Caret &b) { return a.start() < b.start(); });

    // Пересекающиеся выделения и курсоры в одной точке сливаются; соседние
    // непустые выделения, лишь касающиеся друг друга, остаются раздельными
    QVector<Caret> merged;
    int primary = -1;
    for (const Caret &c : std::as_const(m_carets)) {
        const bool isPrimary = c.cursor == primaryCaret.cursor && c.anchor == primaryCaret.anchor;
        if (!merged.isEmpty()) {
            Caret &last = merged.last();
            const bool overlap = c.start() < last.end()
                                 || (c.start() == last.end() && (!c.hasSelection() || !last.hasSelection()));
            if (overlap) {
                const bool forward = !(last.cursor < last.anchor);
                const Cursor from = last.start(), to = qMax(last.end(), c.end());
                last.anchor = forward ? from : to;
                last.cursor = forward ? to : from;
                last.goal = -1;
                if (isPrimary)
                    primary = int(merged.size()) - 1;
                continue;
            }
        }
        merged.append(c);
        if (isPrimary)
            primary = int(merged.size()) - 1;
    }
    m_carets = merged;
    m_primary = primary >= 0 ? primary : int(m_carets.size()) - 1;
}

void EditorView::caretsChanged()
{
    // Курсор попал в свёрнутое (поиск, Ctrl+End, undo) — разворачиваем
    bool revealed = false;
    for (const Caret &c : std::as_const(m_carets)) {
        if (m_folds.isHidden(c.cursor.line)) {
            m_folds.reveal(c.cursor.line);
            revealed = true;
        }
    }
    if (revealed)
        foldsChanged();
    updateBrackets();
    ensureCursorVisible();
    restartCaretBlink();
    emit cursorChanged();
    update();
}

void EditorView::setCarets(QVector<Caret> carets, int primary)
{
    if (!m_document || carets.isEmpty())
        return;
    m_carets = std::move(carets);
    m_primary = primary;
    m_boxActive = false;
    normalizeCarets();
    m_document->breakUndoGroup();
    caretsChanged();
}

void EditorView::moveCarets(const std::function<Cursor(const Caret &)> &target, bool extend,
                            bool keepGoal)
{
    if (!m_document)
        return;
    for (Caret &c : m_carets) {
        const int goal = c.goal >= 0 ? c.goal : visualOf(c.cursor);
        c.cursor = TextBuffer::clampPosition(target(c), buffer());
        if (!extend)
            c.anchor = c.cursor;
        c.goal = keepGoal ? goal : -1;
    }
    m_boxActive = false;
    normalizeCarets();
    m_document->breakUndoGroup();
    caretsChanged();
}

void EditorView::setBoxSelection(Cursor anchor, Cursor cursor)
{
    anchor.line = qBound(0, anchor.line, buffer().lineCount() - 1);
    cursor.line = qBound(0, cursor.line, buffer().lineCount() - 1);
    anchor.column = qMax(0, anchor.column);
    cursor.column = qMax(0, cursor.column);

    // Углы блока — в визуальных колонках: строки с табуляцией тоже ровные
    QVector<Caret> carets;
    const int step = cursor.line < anchor.line ? -1 : 1;
    for (int line = anchor.line;; line += step) {
        if (!m_folds.isHidden(line))
            carets.append({fromVisual(line, cursor.column), fromVisual(line, anchor.column), cursor.column});
        if (line == cursor.line)
            break;
    }
    setCarets(carets, int(carets.size()) - 1);
    // setCarets сбрасывает режим — восстанавливаем: следующее Alt+Shift+стрелка продолжит блок
    m_boxActive = true;
    m_boxAnchor = anchor;
    m_boxCursor = cursor;
}

void EditorView::singleCaret()
{
    if (m_document)
        setCarets({primary()}, 0);
}

void EditorView::addCaretVertical(int direction)
{
    if (!m_document)
        return;
    const Caret &edge = direction < 0 ? m_carets.first() : m_carets.last();
    const int row = rowOf(edge.cursor.line) + direction;
    if (row < 0 || row >= rowCount())
        return;
    const int line = lineAt(row);
    const int goal = primary().goal >= 0 ? primary().goal : visualOf(primary().cursor);
    const Cursor c = fromVisual(line, goal);
    QVector<Caret> carets = m_carets;
    carets.append({c, c, goal});
    setCarets(carets, int(carets.size()) - 1);
}

QPair<int, int> EditorView::wordAt(Cursor c) const
{
    const QString line = buffer().lineAt(c.line);
    if (line.isEmpty())
        return {0, 0};
    const int at = qMin(c.column, int(line.size()) - 1);
    // Курсор сразу за словом — берём это слово, а не следующий символ
    const int probe = at > 0 && classOf(line.at(at)) != CharClass::Word
                              && classOf(line.at(at - 1)) == CharClass::Word ? at - 1 : at;
    const CharClass cls = classOf(line.at(probe));
    int from = probe, to = probe;
    while (from > 0 && classOf(line.at(from - 1)) == cls)
        --from;
    while (to < line.size() && classOf(line.at(to)) == cls)
        ++to;
    return {from, to};
}

void EditorView::selectNextOccurrence()
{
    if (!m_document)
        return;
    const Caret &p = primary();
    if (!p.hasSelection()) {
        const auto [from, to] = wordAt(p.cursor);
        QVector<Caret> carets = m_carets;
        carets[m_primary] = {{p.cursor.line, to}, {p.cursor.line, from}};
        setCarets(carets, m_primary);
        return;
    }

    if (readOnly())
        return; // поиск ниже материализует весь текст — для гигантского файла это гигабайты

    // Следующее вхождение текста основного выделения после последнего курсора,
    // с переходом через конец файла; уже выделенные вхождения пропускаем
    const QString needle = buffer().text(p.start(), p.end());
    const QString text = buffer().snapshot().toString();
    auto taken = [this](int offset) {
        return std::any_of(m_carets.cbegin(), m_carets.cend(), [&](const Caret &c) {
            return buffer().offsetOf(c.start()) == offset;
        });
    };
    const int startFrom = buffer().offsetOf(m_carets.last().end());
    int found = -1;
    for (qsizetype i = text.indexOf(needle, startFrom); i >= 0 && found < 0;
         i = text.indexOf(needle, i + 1))
        if (!taken(int(i)))
            found = int(i);
    for (qsizetype i = text.indexOf(needle); i >= 0 && i < startFrom && found < 0;
         i = text.indexOf(needle, i + 1))
        if (!taken(int(i)))
            found = int(i);
    if (found < 0)
        return;

    QVector<Caret> carets = m_carets;
    carets.append({buffer().positionOf(found + int(needle.size())), buffer().positionOf(found)});
    setCarets(carets, int(carets.size()) - 1);
}

Document::Selections EditorView::selections() const
{
    Document::Selections out;
    out.reserve(m_carets.size());
    for (const Caret &c : m_carets)
        out.append({c.cursor, c.anchor});
    return out;
}

void EditorView::setSelections(const Document::Selections &selections)
{
    QVector<Caret> carets;
    for (const auto &s : selections)
        carets.append({s.cursor, s.anchor});
    if (carets.isEmpty())
        carets.append(Caret{});
    m_carets = carets;
    m_primary = int(carets.size()) - 1;
    m_boxActive = false;
}

void EditorView::updateBrackets()
{
    m_brackets.clear();
    if (!m_document)
        return;
    // Скобка слева от курсора приоритетнее, как в большинстве редакторов
    const Cursor c = primary().cursor;
    for (const Cursor at : {Cursor{c.line, c.column - 1}, c}) {
        const QChar ch = charAt(at, 0);
        if (!editing::isOpeningBracket(ch) && !editing::isClosingBracket(ch))
            continue;
        if (const auto pair = editing::matchingBracket(buffer(), at)) {
            m_brackets = {at, *pair};
            return;
        }
    }
}

// ── Правки ──────────────────────────────────────────────────────────────

void EditorView::editCarets(const EditFn &edit, EditKind kind, const PlaceFn &place)
{
    if (!m_document || readOnly())
        return;
    const QVector<Caret> before = m_carets;
    QVector<Replacement> parts;
    parts.reserve(before.size());
    bool changes = false;
    for (int i = 0; i < before.size(); ++i) {
        parts.append(edit(before.at(i), i));
        changes = changes || !(parts.last().from == parts.last().to) || !parts.last().text.isEmpty();
    }
    const QVector<Cursor> ends = m_document->replace(parts, kind, selections());
    for (int i = 0; i < before.size(); ++i)
        m_carets[i] = place ? place(i, before.at(i), ends.at(i)) : Caret{ends.at(i), ends.at(i)};
    m_boxActive = false;
    normalizeCarets();
    if (place && changes)
        m_document->setLastStepSelections(selections());
    afterTextChange();
}

void EditorView::replaceSelections(const QString &text, EditKind kind)
{
    editCarets([&](const Caret &c, int) { return Replacement{c.start(), c.end(), text}; },
               anySelection() ? EditKind::Other : kind);
}

void EditorView::typeText(const QString &text)
{
    const QChar ch = text.size() == 1 ? text.at(0) : QChar();
    const QChar close = editing::closingFor(ch);
    auto shifted = [this](Cursor c, int delta) { return buffer().positionOf(buffer().offsetOf(c) + delta); };

    // Закрывающий символ уже стоит справа — перешагиваем его
    if ((editing::isClosingBracket(ch) || editing::isQuote(ch)) && !anySelection()
        && std::all_of(m_carets.cbegin(), m_carets.cend(),
                       [&](const Caret &c) { return charAt(c.cursor, 0) == ch; })) {
        moveCarets([](const Caret &c) { return Cursor{c.cursor.line, c.cursor.column + 1}; }, false);
        return;
    }

    // Выделение оборачивается в скобки или кавычки и остаётся выделенным
    if (!close.isNull() && anySelection()) {
        QVector<int> lengths;
        for (const Caret &c : std::as_const(m_carets))
            lengths.append(buffer().offsetOf(c.end()) - buffer().offsetOf(c.start()));
        editCarets([&](const Caret &c, int) {
            return Replacement{c.start(), c.end(), ch + buffer().text(c.start(), c.end()) + close};
        }, EditKind::Other, [&](int i, const Caret &, Cursor end) {
            const Cursor inner = shifted(end, -1);
            return Caret{inner, shifted(inner, -lengths.at(i))};
        });
        return;
    }

    // Пара закрывается, если справа пусто, пробел или закрывающая скобка;
    // кавычка — ещё и если слева не буква (don't, it's)
    if (!close.isNull()) {
        QVector<bool> paired;
        for (const Caret &c : std::as_const(m_carets)) {
            const QChar next = charAt(c.cursor, 0), prev = charAt(c.cursor, -1);
            bool ok = next.isNull() || next.isSpace() || editing::isClosingBracket(next);
            if (editing::isQuote(ch))
                ok = ok && classOf(prev) != CharClass::Word && prev != ch;
            paired.append(ok);
        }
        editCarets([&](const Caret &c, int i) {
            return Replacement{c.start(), c.end(), paired.at(i) ? QString(ch) + close : QString(ch)};
        }, EditKind::Typing, [&](int i, const Caret &, Cursor end) {
            const Cursor at = paired.at(i) ? Cursor{end.line, end.column - 1} : end;
            return Caret{at, at};
        });
        return;
    }

    // Закрывающая скобка в строке из одних отступов — на уровень левее
    if (editing::isClosingBracket(ch) && !anySelection()) {
        const QString unit = m_indent.unit();
        editCarets([&](const Caret &c, int) {
            const QString prefix = buffer().text({c.cursor.line, 0}, c.cursor);
            if (prefix.isEmpty() || !prefix.trimmed().isEmpty())
                return Replacement{c.cursor, c.cursor, text};
            QString indent = prefix;
            if (indent.endsWith(unit))
                indent.chop(unit.size());
            else
                indent.chop(indent.endsWith(u'\t') ? 1 : qMin(m_indent.width, int(indent.size())));
            return Replacement{{c.cursor.line, 0}, c.cursor, indent + ch};
        }, EditKind::Typing);
        return;
    }

    replaceSelections(text, EditKind::Typing);
}

void EditorView::insertNewline()
{
    const QString unit = m_indent.unit();
    QVector<int> tail; // сколько символов после курсора вставлено следом (Enter между скобками)
    editCarets([&](const Caret &c, int) {
        const QString line = buffer().lineAt(c.start().line);
        const QString before = line.left(c.start().column);
        const QString after = buffer().lineAt(c.end().line).mid(c.end().column);
        const QString indent = leadingWhitespace(before);
        const QString trimmed = before.trimmed();
        const QChar last = trimmed.isEmpty() ? QChar() : trimmed.back();
        const bool opens = editing::isOpeningBracket(last) || (m_python && last == u':');
        const bool between = editing::isOpeningBracket(last)
                             && after.trimmed().startsWith(editing::closingFor(last));
        QString text = u'\n' + (opens ? indent + unit : indent);
        if (between)
            text += u'\n' + indent;
        tail.append(between ? int(indent.size()) + 1 : 0);
        return Replacement{c.start(), c.end(), text};
    }, EditKind::Other, [&](int i, const Caret &, Cursor end) {
        const Cursor at = buffer().positionOf(buffer().offsetOf(end) - tail.at(i));
        return Caret{at, at};
    });
}

void EditorView::erase(bool forward, bool word)
{
    editCarets([&](const Caret &c, int) {
        if (c.hasSelection())
            return Replacement{c.start(), c.end(), {}};
        if (forward)
            return Replacement{c.cursor, stepRight(c.cursor, word), {}};
        if (!word) {
            // Пустая пара «(|)» удаляется целиком
            const QChar prev = charAt(c.cursor, -1);
            if (!prev.isNull() && editing::closingFor(prev) == charAt(c.cursor, 0))
                return Replacement{{c.cursor.line, c.cursor.column - 1}, {c.cursor.line, c.cursor.column + 1}, {}};
            // В отступе из пробелов — до предыдущей позиции табуляции
            const QString prefix = buffer().text({c.cursor.line, 0}, c.cursor);
            if (!m_indent.tabs && !prefix.isEmpty() && prefix.trimmed().isEmpty() && !prefix.contains(u'\t')) {
                const int n = (int(prefix.size()) - 1) % m_indent.width + 1;
                return Replacement{{c.cursor.line, c.cursor.column - n}, c.cursor, {}};
            }
        }
        return Replacement{stepLeft(c.cursor, word), c.cursor, {}};
    }, anySelection() || word ? EditKind::Other : EditKind::Typing);
}

void EditorView::indentLines(bool outdent)
{
    if (!m_document || readOnly())
        return;
    QVector<int> lines;
    for (const Caret &c : std::as_const(m_carets)) {
        int last = c.end().line;
        if (last > c.start().line && c.end().column == 0)
            --last; // строка, на начале которой кончается выделение, не входит
        for (int l = c.start().line; l <= last; ++l)
            lines.append(l);
    }
    std::sort(lines.begin(), lines.end());
    lines.erase(std::unique(lines.begin(), lines.end()), lines.end());

    const QString unit = m_indent.unit();
    QVector<Replacement> parts;
    QHash<int, int> delta;
    for (int l : std::as_const(lines)) {
        const QString text = buffer().lineAt(l);
        if (outdent) {
            int n = 0;
            if (text.startsWith(u'\t'))
                n = 1;
            else
                while (n < m_indent.width && n < text.size() && text.at(n) == u' ')
                    ++n;
            if (n == 0)
                continue;
            parts.append(Replacement{{l, 0}, {l, n}, {}});
            delta.insert(l, -n);
        } else if (!text.isEmpty() || lines.size() == 1) {
            parts.append(Replacement{{l, 0}, {l, 0}, unit});
            delta.insert(l, int(unit.size()));
        }
    }
    if (parts.isEmpty())
        return;

    m_document->replace(parts, EditKind::Other, selections());
    auto shift = [&](Cursor p) {
        const int d = delta.value(p.line);
        if (d < 0)
            p.column = qMax(0, p.column + d);
        else if (d > 0 && p.column > 0)
            p.column += d;
        return p;
    };
    for (Caret &c : m_carets)
        c = {shift(c.cursor), shift(c.anchor)};
    m_boxActive = false;
    normalizeCarets();
    m_document->setLastStepSelections(selections());
    afterTextChange();
}

void EditorView::afterTextChange()
{
    bool overflow = false;
    const QVector<TextBuffer::Edit> edits = m_document->takeEdits(&overflow);
    if (overflow)
        m_folds.clear();
    for (const TextBuffer::Edit &e : edits)
        m_folds.applyEdit(e);
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
    normalizeCarets();
    updateGutterWidth();
    markContentChanged();
    emit contentSizeChanged();
    emit historyChanged();
    caretsChanged();
}

void EditorView::undo()
{
    Document::Selections s;
    if (m_document && m_document->undo(&s)) {
        setSelections(s);
        afterTextChange();
    }
}

void EditorView::redo()
{
    Document::Selections s;
    if (m_document && m_document->redo(&s)) {
        setSelections(s);
        afterTextChange();
    }
}

void EditorView::copy()
{
    if (!m_document || !anySelection())
        return;
    QStringList parts;
    for (const Caret &c : std::as_const(m_carets))
        if (c.hasSelection())
            parts.append(buffer().text(c.start(), c.end()));
    QGuiApplication::clipboard()->setText(parts.join(u'\n'));
}

void EditorView::cut()
{
    if (!m_document || !anySelection())
        return;
    copy();
    editCarets([](const Caret &c, int) { return Replacement{c.start(), c.end(), {}}; }, EditKind::Other);
}

void EditorView::paste()
{
    const QString text = TextBuffer::normalizeNewlines(QGuiApplication::clipboard()->text());
    if (!m_document || text.isEmpty())
        return;
    // Строк столько же, сколько курсоров, — каждому своя строка
    const QStringList lines = text.split(u'\n');
    if (m_carets.size() > 1 && lines.size() == m_carets.size()) {
        editCarets([&](const Caret &c, int i) { return Replacement{c.start(), c.end(), lines.at(i)}; },
                   EditKind::Other);
    } else {
        replaceSelections(text, EditKind::Other);
    }
}

void EditorView::selectAll()
{
    if (m_document)
        setCarets({{buffer().positionOf(buffer().length()), {0, 0}}}, 0);
}

// ── Навигация и прокрутка ───────────────────────────────────────────────

EditorView::Cursor EditorView::stepLeft(Cursor c, bool word) const
{
    if (c.column == 0)
        return c.line > 0 ? Cursor{c.line - 1, buffer().lineLength(c.line - 1)} : c;
    if (!word) // суррогатная пара (эмодзи и т.п.) — один символ
        return {c.line, c.column - (charAt(c, -1).isLowSurrogate() && charAt(c, -2).isHighSurrogate() ? 2 : 1)};
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
        return {c.line, c.column + (charAt(c, 0).isHighSurrogate() && charAt(c, 1).isLowSurrogate() ? 2 : 1)};
    const QString line = buffer().lineAt(c.line);
    int i = c.column;
    const CharClass cls = classOf(line.at(i));
    while (i < len && classOf(line.at(i)) == cls)
        ++i;
    while (i < len && classOf(line.at(i)) == CharClass::Space)
        ++i;
    return {c.line, i};
}

EditorView::Cursor EditorView::visualAt(const QPointF &pos) const
{
    const int line = lineAt(int((pos.y() + m_scrollY) / m_lineHeight));
    return {line, qMax(0, qRound((pos.x() - textLeft() + m_scrollX) / m_charWidth))};
}

EditorView::Cursor EditorView::cursorAt(const QPointF &pos) const
{
    const Cursor v = visualAt(pos);
    Cursor c{v.line, editing::columnAtVisual(buffer().lineAt(v.line), v.column)};
    if (charAt(c, 0).isLowSurrogate()) // не внутрь суррогатной пары
        --c.column;
    return c;
}

void EditorView::ensureCursorVisible()
{
    const Cursor cursor = primary().cursor;
    qreal x = m_scrollX, y = m_scrollY;
    const qreal top = rowOf(cursor.line) * m_lineHeight;
    if (top < y)
        y = top;
    else if (top + m_lineHeight > y + height())
        y = top + m_lineHeight - height();

    const qreal caretX = visualOf(cursor) * m_charWidth;
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
    const int rows = rowCount();
    y = qBound<qreal>(0, y, (rows - 1) * m_lineHeight);

    // Предел по X — самая широкая из видимых строк (и строка курсора)
    auto widthOf = [this](int line) {
        const int len = buffer().lineLength(line);
        return len > kLongLine ? len : editing::visualColumn(buffer().lineAt(line), len);
    };
    int widest = widthOf(primary().cursor.line);
    const int first = int(y / m_lineHeight);
    const int last = qMin(rows - 1, first + int(height() / m_lineHeight) + 1);
    for (int row = first; row <= last; ++row)
        widest = qMax(widest, widthOf(lineAt(row)));
    x = qBound<qreal>(0, x, qMax<qreal>(0, (widest + 4) * m_charWidth - textWidth()));

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

// ── Сворачивание ────────────────────────────────────────────────────────

void EditorView::markContentChanged()
{
    m_contentDirty = true;
    emit contentChanged();
    update();
}

void EditorView::foldsChanged()
{
    markContentChanged();
    emit contentSizeChanged();
    setScroll(m_scrollX, m_scrollY); // контент мог стать короче прокрутки
}

void EditorView::liftHiddenCarets()
{
    for (Caret &c : m_carets) {
        if (!m_folds.isHidden(c.cursor.line))
            continue;
        const int header = lineByRows(c.cursor.line, 0);
        c.cursor = c.anchor = {header, buffer().lineLength(header)};
        c.goal = -1;
    }
    normalizeCarets();
}

void EditorView::toggleFold(int line)
{
    if (!m_document)
        return;
    if (!m_folds.remove(line)) {
        const auto last = core::folding::foldEnd(buffer(), line);
        if (!last)
            return;
        m_folds.add({line, *last});
        liftHiddenCarets();
    }
    foldsChanged();
    caretsChanged();
}

void EditorView::foldAtCursor()
{
    if (!m_document)
        return;
    // Блок, открытый строкой курсора, иначе ближайший охватывающий
    const int line = primary().cursor.line;
    for (int l = line; l >= qMax(0, line - kEnclosingSearch); --l) {
        if (m_folds.isFolded(l) || m_folds.isHidden(l))
            continue;
        const auto last = core::folding::foldEnd(buffer(), l);
        if (last && *last >= line) {
            toggleFold(l);
            return;
        }
    }
}

void EditorView::unfoldAtCursor()
{
    if (m_document && m_folds.isFolded(primary().cursor.line))
        toggleFold(primary().cursor.line);
}

void EditorView::foldAll()
{
    if (!m_document || buffer().lineCount() > kFoldAllMaxLines)
        return;
    QVector<core::folding::FoldSet::Fold> folds;
    for (int line = 0; line < buffer().lineCount(); ++line)
        if (core::folding::canFold(buffer(), line))
            if (const auto last = core::folding::foldEnd(buffer(), line))
                folds.append({line, *last});
    m_folds.assign(folds);
    liftHiddenCarets();
    foldsChanged();
    caretsChanged();
}

void EditorView::unfoldAll()
{
    if (!m_document || m_folds.isEmpty())
        return;
    m_folds.clear();
    foldsChanged();
    caretsChanged();
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
    const Cursor cursor = primary().cursor;
    for (int i = 0; i < m_matches.size(); ++i) {
        const Match &m = m_matches.at(i);
        if (!(Cursor{m.line, m.column} < cursor))
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
    setCarets({{{m.line, m.column + m.length}, {m.line, m.column}}}, 0);
    emit searchUpdated(index + 1, int(m_matches.size()));
}

// ── Ввод ────────────────────────────────────────────────────────────────

void EditorView::keyPressEvent(QKeyEvent *event)
{
    if (!m_document)
        return QQuickItem::keyPressEvent(event);

    const Qt::KeyboardModifiers mods = event->modifiers();
    const bool shift = mods & Qt::ShiftModifier;
    const bool ctrl = mods & Qt::ControlModifier;
    const bool alt = mods & Qt::AltModifier;
    const int key = event->key();
    const int pageLines = qMax(1, int(height() / m_lineHeight) - 1);

    auto vertical = [&](int delta) {
        moveCarets([&](const Caret &c) {
            return fromVisual(lineByRows(c.cursor.line, delta), c.goal >= 0 ? c.goal : visualOf(c.cursor));
        }, shift, true);
    };
    auto horizontal = [&](bool right) {
        moveCarets([&](const Caret &c) {
            if (c.hasSelection() && !shift && !ctrl)
                return right ? c.end() : c.start(); // схлопнуть выделение к краю
            const Cursor to = right ? stepRight(c.cursor, ctrl) : stepLeft(c.cursor, ctrl);
            if (!m_folds.isHidden(to.line))
                return to;
            // Свёрнутый блок перешагиваем целиком: влево — в конец заголовка, вправо — за блок
            const int line = lineByRows(to.line, right ? 1 : 0);
            return right ? Cursor{line, 0} : Cursor{line, buffer().lineLength(line)};
        }, shift);
    };

    // Выделение столбцом: Alt+Shift+стрелки двигают угол блока
    if (alt && shift && !ctrl
        && (key == Qt::Key_Up || key == Qt::Key_Down || key == Qt::Key_Left || key == Qt::Key_Right)) {
        const Cursor start{primary().cursor.line, visualOf(primary().cursor)};
        const Cursor anchor = m_boxActive ? m_boxAnchor : start;
        Cursor corner = m_boxActive ? m_boxCursor : start;
        if (key == Qt::Key_Up) --corner.line;
        if (key == Qt::Key_Down) ++corner.line;
        if (key == Qt::Key_Left) corner.column = qMax(0, corner.column - 1);
        if (key == Qt::Key_Right) ++corner.column;
        setBoxSelection(anchor, corner);
        return event->accept();
    }
    if (ctrl && alt && (key == Qt::Key_Up || key == Qt::Key_Down)) {
        addCaretVertical(key == Qt::Key_Up ? -1 : 1);
        return event->accept();
    }
    // Ctrl+Shift+[ / ] — по коду клавиши, чтобы работало и в русской раскладке
    const quint32 vk = event->nativeVirtualKey();
    if (ctrl && shift && !alt && (key == Qt::Key_BraceLeft || key == Qt::Key_BracketLeft || vk == 0xDB)) {
        foldAtCursor();
        return event->accept();
    }
    if (ctrl && shift && !alt && (key == Qt::Key_BraceRight || key == Qt::Key_BracketRight || vk == 0xDD)) {
        unfoldAtCursor();
        return event->accept();
    }

    switch (key) {
    case Qt::Key_Left: horizontal(false); break;
    case Qt::Key_Right: horizontal(true); break;
    case Qt::Key_Up: vertical(-1); break;
    case Qt::Key_Down: vertical(+1); break;
    case Qt::Key_PageUp: vertical(-pageLines); break;
    case Qt::Key_PageDown: vertical(+pageLines); break;
    case Qt::Key_Home:
        if (ctrl) {
            moveCarets([](const Caret &) { return Cursor{0, 0}; }, shift);
        } else {
            // «Умный» Home: сначала к первому непробельному символу, затем в колонку 0
            moveCarets([&](const Caret &c) {
                const int indent = int(leadingWhitespace(buffer().lineAt(c.cursor.line)).size());
                return Cursor{c.cursor.line, c.cursor.column == indent ? 0 : indent};
            }, shift);
        }
        break;
    case Qt::Key_End:
        moveCarets([&](const Caret &c) {
            return ctrl ? buffer().positionOf(buffer().length())
                        : Cursor{c.cursor.line, buffer().lineLength(c.cursor.line)};
        }, shift);
        break;
    case Qt::Key_Escape:
        if (m_carets.size() > 1)
            singleCaret();
        else if (primary().hasSelection())
            moveCarets([](const Caret &c) { return c.cursor; }, false);
        else
            return QQuickItem::keyPressEvent(event);
        break;
    case Qt::Key_Return:
    case Qt::Key_Enter:
        insertNewline();
        break;
    case Qt::Key_Tab:
        if (std::any_of(m_carets.cbegin(), m_carets.cend(),
                        [](const Caret &c) { return c.start().line != c.end().line; })) {
            indentLines(false);
        } else {
            editCarets([&](const Caret &c, int) {
                const QString unit = m_indent.tabs
                    ? QStringLiteral("\t")
                    : QString(m_indent.width - visualOf(c.start()) % m_indent.width, u' ');
                return Replacement{c.start(), c.end(), unit};
            }, EditKind::Typing);
        }
        break;
    case Qt::Key_Backtab: // Shift+Tab
        indentLines(true);
        break;
    case Qt::Key_Backspace: erase(false, ctrl); break;
    case Qt::Key_Delete: erase(true, ctrl); break;
    default:
        if (ctrl && !alt) { // Ctrl+Alt без команды — это AltGr: печатаем символ
            switch (key) {
            case Qt::Key_A: selectAll(); break;
            case Qt::Key_C: copy(); break;
            case Qt::Key_X: cut(); break;
            case Qt::Key_V: paste(); break;
            case Qt::Key_D: selectNextOccurrence(); break;
            case Qt::Key_Z: shift ? redo() : undo(); break;
            case Qt::Key_Y: redo(); break;
            default: return QQuickItem::keyPressEvent(event);
            }
            break;
        }
        if (event->text().isEmpty() || !event->text().at(0).isPrint())
            return QQuickItem::keyPressEvent(event);
        typeText(event->text());
        emit textTyped(event->text());
    }
    event->accept();
}

void EditorView::mousePressEvent(QMouseEvent *event)
{
    if (!m_document)
        return;
    forceActiveFocus();
    m_hoverTimer.stop();
    emit hoverLeft();
    const Cursor c = TextBuffer::clampPosition(cursorAt(event->position()), buffer());
    const Qt::KeyboardModifiers mods = event->modifiers();

    const qreal x = event->position().x();
    if ((mods & Qt::ControlModifier) && x >= textLeft()) { // Ctrl+клик — к определению
        setCarets({{c, c}}, 0);
        emit definitionClicked(c.line, c.column);
    } else if (inFoldMarker(x)) {
        toggleFold(c.line);
    } else if (x < textLeft()) { // клик по номеру строки — вся строка (со свёрнутым блоком)
        const int nextRow = rowOf(c.line) + 1;
        const Cursor next = nextRow < rowCount() ? Cursor{lineAt(nextRow), 0}
                                                 : Cursor{c.line, buffer().lineLength(c.line)};
        setCarets({{next, {c.line, 0}}}, 0);
        m_mouseMode = MouseMode::Select;
    } else if (m_folds.isFolded(c.line) && !(mods & Qt::AltModifier)
               && x - textLeft() + m_scrollX >= foldPlaceholderX(c.line)) { // клик по «…»
        toggleFold(c.line);
    } else if (mods & Qt::AltModifier) { // Alt+клик — ещё курсор, Alt+протяжка — блок
        QVector<Caret> carets = m_carets;
        carets.append({c, c});
        setCarets(carets, int(carets.size()) - 1);
        m_boxAnchor = visualAt(event->position());
        m_mouseMode = MouseMode::Box;
    } else if (mods & Qt::ShiftModifier) {
        setCarets({{c, primary().anchor}}, 0);
        m_mouseMode = MouseMode::Select;
    } else {
        setCarets({{c, c}}, 0);
        m_mouseMode = MouseMode::Select;
    }
    event->accept();
}

void EditorView::mouseMoveEvent(QMouseEvent *event)
{
    if (!m_document || m_mouseMode == MouseMode::None)
        return;
    if (m_mouseMode == MouseMode::Box) {
        const Cursor corner = visualAt(event->position());
        if (!(corner == m_boxAnchor))
            setBoxSelection(m_boxAnchor, corner);
    } else {
        const Cursor c = TextBuffer::clampPosition(cursorAt(event->position()), buffer());
        setCarets({{c, primary().anchor}}, 0);
    }
    event->accept();
}

void EditorView::mouseReleaseEvent(QMouseEvent *event)
{
    m_mouseMode = MouseMode::None;
    event->accept();
}

void EditorView::mouseDoubleClickEvent(QMouseEvent *event)
{
    if (!m_document)
        return;
    const Cursor c = TextBuffer::clampPosition(cursorAt(event->position()), buffer());
    const auto [from, to] = wordAt(c);
    setCarets({{{c.line, to}, {c.line, from}}}, 0);
    event->accept();
}

void EditorView::hoverMoveEvent(QHoverEvent *event)
{
    if ((event->position() - m_hoverPos).manhattanLength() > 4) {
        m_hoverPos = event->position();
        emit hoverLeft();
        if (m_document && m_hoverPos.x() >= textLeft())
            m_hoverTimer.start();
        else
            m_hoverTimer.stop();
    }
    const bool hover = event->position().x() < textLeft();
    if (hover != m_gutterHover) {
        m_gutterHover = hover;
        update();
    }
}

void EditorView::hoverLeaveEvent(QHoverEvent *)
{
    m_hoverTimer.stop();
    emit hoverLeft();
    if (m_gutterHover) {
        m_gutterHover = false;
        update();
    }
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

qreal EditorView::foldPlaceholderX(int line) const
{
    const int len = buffer().lineLength(line);
    const int width = len > kLongLine ? len : editing::visualColumn(buffer().lineAt(line), len);
    return (width + 1) * m_charWidth;
}

QVector<EditorView::StyledLine> EditorView::styledLines(int firstRow, int count, int maxColumn) const
{
    QVector<StyledLine> out;
    count = qMin(count, rowCount() - firstRow);
    if (count <= 0)
        return out;

    // Символ i стоит не левее визуальной колонки i, поэтому обрезка по maxColumn
    // ничего видимого не теряет, а строки по 10 МБ не подсвечиваются целиком
    QVector<int> lineNumbers(count);
    QStringList texts;
    for (int i = 0; i < count; ++i) {
        lineNumbers[i] = lineAt(firstRow + i);
        texts.append(buffer().lineAt(lineNumbers[i]).left(maxColumn));
    }

    // tree-sitter, пока нет дерева (первый разбор, большой файл), заменяют регэкспы.
    // highlightLines берёт только непрерывные строки, а свёрнутые блоки их рвут
    const bool treeSitter = m_syntax.isReady();
    QVector<QVector<Highlighter::Span>> spans(count);
    if (treeSitter) {
        for (int from = 0; from < count;) {
            int to = from + 1;
            while (to < count && lineNumbers.at(to) == lineNumbers.at(to - 1) + 1)
                ++to;
            QVector<QVector<Highlighter::Span>> part;
            m_syntax.highlightLines(lineNumbers.at(from), texts.mid(from, to - from), part);
            std::move(part.begin(), part.end(), spans.begin() + from);
            from = to;
        }
    } else if (m_highlighter.hasLanguage()) {
        for (int i = 0; i < count; ++i)
            m_highlighter.highlightLine(texts.at(i), spans[i]);
    }

    out.reserve(count);
    QVector<int> map;
    for (int i = 0; i < count; ++i) {
        StyledLine styled{lineNumbers.at(i), editing::expandTabs(texts.at(i), &map), {}};
        const int length = int(texts.at(i).size());
        int pos = 0;
        auto add = [&](int to, QRgb color) {
            if (to > pos)
                styled.runs.append({pos, to, color});
            pos = qMax(pos, to);
        };
        for (const Highlighter::Span &s : std::as_const(spans.at(i))) {
            if (s.start >= length)
                break;
            add(map.at(s.start), kText);
            const QColor color = treeSitter ? SyntaxHighlighter::color(s.rule)
                                            : m_highlighter.ruleColor(s.rule);
            add(map.at(qMin(s.start + s.length, length)), color.isValid() ? color.rgb() : kText);
        }
        add(int(styled.display.size()), kText);
        out.append(std::move(styled));
    }
    return out;
}

void EditorView::buildTextNodes(int firstRow, int count, int column0, int column1)
{
    for (QSGTextNode *node : std::as_const(m_textNodes))
        node->clear();

    auto addText = [&](const QString &text, qreal x, int row, QRgb color) {
        QTextLayout layout(text, m_font);
        layout.beginLayout();
        layout.createLine().setLineWidth(1e6);
        layout.endLayout();
        textNodeFor(color)->addTextLayout(QPointF(x, row * m_lineHeight + m_textOffsetY), &layout);
    };

    const QVector<StyledLine> lines = styledLines(firstRow, count, column1);
    for (int i = 0; i < lines.size(); ++i) {
        const StyledLine &line = lines.at(i);
        for (const ColorRun &run : line.runs) {
            const int from = qMax(run.from, column0), to = qMin(run.to, column1);
            const QString piece = line.display.mid(from, to - from);
            if (from < to && !piece.trimmed().isEmpty())
                addText(piece, from * m_charWidth, firstRow + i, run.color);
        }
        if (m_folds.isFolded(line.line))
            addText(QStringLiteral("…"), foldPlaceholderX(line.line) + m_charWidth, firstRow + i,
                    kGutterCurrent);
    }
}

void EditorView::buildGutterNodes(int firstRow, const QVector<int> &lineNumbers,
                                  const QVector<int> &caretLines)
{
    m_gutterNode->clear();
    m_gutterCurrentNode->clear();
    const qreal right = m_gutterWidth - 2 * m_charWidth;
    for (int i = 0; i < lineNumbers.size(); ++i) {
        const int line = lineNumbers.at(i);
        const QString number = QString::number(line + 1);
        QTextLayout layout(number, m_font);
        layout.beginLayout();
        layout.createLine();
        layout.endLayout();
        QSGTextNode *node = std::binary_search(caretLines.cbegin(), caretLines.cend(), line)
                                ? m_gutterCurrentNode : m_gutterNode;
        node->addTextLayout(QPointF(right - number.size() * m_charWidth,
                                    (firstRow + i) * m_lineHeight + m_textOffsetY),
                            &layout);
    }
}

void EditorView::buildFoldMarkers(int firstRow, const QVector<int> &lineNumbers)
{
    // Треугольники: ▸ у свёрнутых — всегда, ▾ у остальных — при наведении на gutter
    QVector<QPointF> open, closed;
    const qreal h = qRound(m_charWidth * 0.5);
    const qreal cx = qRound(m_gutterWidth - 1.1 * m_charWidth);
    for (int i = 0; i < lineNumbers.size(); ++i) {
        const int line = lineNumbers.at(i);
        const qreal cy = qRound((firstRow + i) * m_lineHeight + m_lineHeight / 2);
        if (m_folds.isFolded(line))
            closed << QPointF(cx - h / 2, cy - h) << QPointF(cx - h / 2, cy + h) << QPointF(cx + h / 2, cy);
        else if (m_gutterHover && std::binary_search(m_builtFoldable.cbegin(), m_builtFoldable.cend(), line))
            open << QPointF(cx - h, cy - h / 2) << QPointF(cx + h, cy - h / 2) << QPointF(cx, cy + h / 2);
    }
    setVertices(m_foldMarkers, open);
    setVertices(m_foldedMarkers, closed);
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

QSGNode *EditorView::updatePaintNode(QSGNode *oldNode, UpdatePaintNodeData *)
{
    if (!oldNode) {
        // Сцена создаётся заново (в т.ч. после потери графического контекста):
        // старые ноды удалены scene graph'ом — забываем указатели.
        m_textNodes.clear();
        m_currentLinePool.clear();
        m_selectionPool.clear();
        m_matchPool.clear();
        m_bracketPool.clear();
        m_foldPool.clear();
        m_caretPool.clear();
        m_builtFirstRow = -1;

        m_root = new QSGNode;
        m_background = new QSGSimpleRectNode({}, QColor(kBackground));
        m_currentLineLayer = new QSGNode;
        m_textClip = new QSGClipNode;
        m_textClip->setIsRectangular(true);
        m_textTransform = new QSGTransformNode;
        m_selectionLayer = new QSGNode;
        m_matchLayer = new QSGNode;
        m_bracketLayer = new QSGNode;
        m_foldLayer = new QSGNode;
        m_textLayer = new QSGNode;
        m_caretLayer = new QSGNode;
        m_gutterTransform = new QSGTransformNode;
        m_gutterNode = window()->createTextNode();
        m_gutterNode->setRenderType(QSGTextNode::NativeRendering);
        m_gutterNode->setColor(QColor(kGutterText));
        m_gutterCurrentNode = window()->createTextNode();
        m_gutterCurrentNode->setRenderType(QSGTextNode::NativeRendering);
        m_gutterCurrentNode->setColor(QColor(kGutterCurrent));
        m_foldMarkers = createGeometry(kGutterText, QSGGeometry::DrawTriangles);
        m_foldedMarkers = createGeometry(kGutterCurrent, QSGGeometry::DrawTriangles);

        m_root->appendChildNode(m_background);
        m_root->appendChildNode(m_currentLineLayer);
        m_root->appendChildNode(m_textClip);
        m_textClip->appendChildNode(m_textTransform);
        m_textTransform->appendChildNode(m_selectionLayer); // слои под текстом
        m_textTransform->appendChildNode(m_matchLayer);
        m_textTransform->appendChildNode(m_bracketLayer);
        m_textTransform->appendChildNode(m_foldLayer);
        m_textTransform->appendChildNode(m_textLayer);
        for (int i = 0; i < 3; ++i) {
            m_diagnosticNodes[i] = createGeometry(kDiagnosticColors[i], QSGGeometry::DrawLines);
            m_textTransform->appendChildNode(m_diagnosticNodes[i]);
        }
        m_textTransform->appendChildNode(m_caretLayer);
        m_root->appendChildNode(m_gutterTransform);
        m_gutterTransform->appendChildNode(m_gutterNode);
        m_gutterTransform->appendChildNode(m_gutterCurrentNode);
        m_gutterTransform->appendChildNode(m_foldMarkers);
        m_gutterTransform->appendChildNode(m_foldedMarkers);
    }

    m_background->setRect(boundingRect());
    const int rows = m_document ? rowCount() : 0;
    if (rows == 0) {
        for (QSGTextNode *node : std::as_const(m_textNodes))
            node->clear();
        m_gutterNode->clear();
        m_gutterCurrentNode->clear();
        setVertices(m_foldMarkers, {});
        setVertices(m_foldedMarkers, {});
        for (QSGGeometryNode *node : m_diagnosticNodes)
            setVertices(node, {});
        for (auto [layer, pool] : {std::pair{m_currentLineLayer, &m_currentLinePool},
                                   std::pair{m_selectionLayer, &m_selectionPool},
                                   std::pair{m_matchLayer, &m_matchPool},
                                   std::pair{m_bracketLayer, &m_bracketPool},
                                   std::pair{m_foldLayer, &m_foldPool},
                                   std::pair{m_caretLayer, &m_caretPool}})
            setRects(layer, *pool, {});
        m_builtFirstRow = -1;
        return m_root;
    }

    const int firstRow = qBound(0, int(m_scrollY / m_lineHeight), rows - 1);
    const int count = qMin(qCeil(height() / m_lineHeight) + 1, rows - firstRow);
    const int lastRow = firstRow + count - 1;
    QVector<int> lineNumbers(count);
    for (int i = 0; i < count; ++i)
        lineNumbers[i] = lineAt(firstRow + i);
    const int firstLine = lineNumbers.first(), lastLine = lineNumbers.last();
    const int viewColumn0 = int(m_scrollX / m_charWidth);
    const int viewColumn1 = viewColumn0 + qCeil(textWidth() / m_charWidth) + 1;

    // Позиция в пикселях — по тексту до курсора, а не по всей строке (строки бывают по 10 МБ)
    auto xOf = [&](Cursor c) { return visualOf(c) * m_charWidth; };
    auto yOf = [&](int line) { return rowOf(line) * m_lineHeight; };
    auto visibleLine = [&](int line) { return isLineVisible(line, firstLine, lastLine); };

    const bool rebuild = m_contentDirty || firstRow != m_builtFirstRow
                         || count != m_builtRowCount || viewColumn0 < m_builtColumn0
                         || viewColumn1 > m_builtColumn1;
    if (rebuild) {
        m_builtFoldable.clear();
        for (int line : std::as_const(lineNumbers))
            if (buffer().lineLength(line) <= kLongLine && core::folding::canFold(buffer(), line))
                m_builtFoldable.append(line);
        m_builtColumn0 = qMax(0, viewColumn0 - kColumnMargin);
        m_builtColumn1 = viewColumn1 + kColumnMargin;
        buildTextNodes(firstRow, count, m_builtColumn0, m_builtColumn1);
        m_builtFirstRow = firstRow;
        m_builtRowCount = count;
        m_contentDirty = false;
    }

    QVector<int> caretLines; // строки курсоров, по возрастанию, без повторов
    for (const Caret &c : std::as_const(m_carets))
        caretLines.append(c.cursor.line);
    std::sort(caretLines.begin(), caretLines.end());
    caretLines.erase(std::unique(caretLines.begin(), caretLines.end()), caretLines.end());
    if (rebuild || caretLines != m_builtCaretLines) {
        buildGutterNodes(firstRow, lineNumbers, caretLines);
        m_builtCaretLines = caretLines;
    }
    if (rebuild || m_gutterHover != m_builtGutterHover) {
        buildFoldMarkers(firstRow, lineNumbers);
        m_builtGutterHover = m_gutterHover;
    }

    QMatrix4x4 textMatrix;
    textMatrix.translate(float(textLeft() - m_scrollX), float(-m_scrollY));
    m_textTransform->setMatrix(textMatrix);
    QMatrix4x4 gutterMatrix;
    gutterMatrix.translate(0, float(-m_scrollY));
    m_gutterTransform->setMatrix(gutterMatrix);
    // Клип чуть левее текста: каретка в колонке 0 не должна обрезаться
    m_textClip->setClipRect(QRectF(textLeft() - 4, 0, textWidth() + 4, height()));

    QVector<QPair<QRectF, QColor>> currentLines;
    for (int line : std::as_const(caretLines))
        if (visibleLine(line))
            currentLines.append({QRectF(0, yOf(line) - m_scrollY, width(), m_lineHeight),
                                 QColor(kCurrentLine)});
    setRects(m_currentLineLayer, m_currentLinePool, currentLines);

    // Выделения и каретки — в координатах текста, только видимые ряды
    QVector<QPair<QRectF, QColor>> selection, carets;
    const bool caretOn = m_caretVisible && hasActiveFocus();
    for (const Caret &c : std::as_const(m_carets)) {
        if (c.hasSelection()) {
            const Cursor a = c.start(), b = c.end();
            for (int row = qMax(rowOf(a.line), firstRow); row <= qMin(rowOf(b.line), lastRow); ++row) {
                const int line = lineNumbers.at(row - firstRow);
                if (line < a.line)
                    continue; // начало выделения внутри свёрнутого блока
                const qreal x0 = line == a.line ? xOf(a) : 0;
                const qreal x1 = line == b.line ? xOf(b)
                                                : xOf({line, buffer().lineLength(line)}) + m_charWidth; // + перевод строки
                selection.append({QRectF(x0, row * m_lineHeight, qMax(m_charWidth, x1 - x0), m_lineHeight),
                                  QColor(kSelection)});
            }
        }
        if (caretOn && visibleLine(c.cursor.line))
            carets.append({QRectF(xOf(c.cursor) - 1, yOf(c.cursor.line) + 2, 2, m_lineHeight - 4),
                           QColor(kCaret)});
    }
    setRects(m_selectionLayer, m_selectionPool, selection);
    setRects(m_caretLayer, m_caretPool, carets);

    QVector<QPair<QRectF, QColor>> brackets;
    for (const Cursor b : std::as_const(m_brackets))
        if (visibleLine(b.line))
            brackets.append({QRectF(xOf(b), yOf(b.line), m_charWidth, m_lineHeight), QColor(kBracket)});
    setRects(m_bracketLayer, m_bracketPool, brackets);

    QVector<QPair<QRectF, QColor>> placeholders;
    for (int i = 0; i < count; ++i)
        if (m_folds.isFolded(lineNumbers.at(i)))
            placeholders.append({QRectF(foldPlaceholderX(lineNumbers.at(i)), (firstRow + i) * m_lineHeight + 3,
                                        3 * m_charWidth, m_lineHeight - 6),
                                 QColor(kFoldPlaceholder)});
    setRects(m_foldLayer, m_foldPool, placeholders);

    QVector<QPair<QRectF, QColor>> matches;
    for (int i = 0; i < m_matches.size(); ++i) {
        const Match &m = m_matches.at(i);
        if (!visibleLine(m.line))
            continue;
        const qreal x0 = xOf({m.line, m.column}), x1 = xOf({m.line, m.column + m.length});
        matches.append({QRectF(x0, yOf(m.line), qMax(m_charWidth, x1 - x0), m_lineHeight),
                        QColor(i == m_currentMatch ? kCurrentMatch : kMatch)});
    }
    setRects(m_matchLayer, m_matchPool, matches);

    // Волнистая линия под диагностикой: зигзаг с шагом 2 px по низу строки
    QVector<QPointF> squiggles[3];
    for (const Diagnostic &d : std::as_const(m_diagnostics)) {
        QVector<QPointF> &points = squiggles[qBound(1, d.severity, 3) - 1];
        for (int line = qMax(d.start.line, firstLine); line <= qMin(d.end.line, lastLine); ++line) {
            if (!visibleLine(line))
                continue;
            const qreal x0 = line == d.start.line ? xOf(d.start) : 0;
            const qreal x1 = qMax(x0 + m_charWidth, line == d.end.line ? xOf(d.end)
                                                                       : xOf({line, buffer().lineLength(line)}));
            const qreal y = yOf(line) + m_lineHeight - 4;
            for (qreal x = x0; x < x1; x += 2) {
                const bool up = int((x - x0) / 2) % 2;
                points << QPointF(x, y + (up ? 2 : 0)) << QPointF(x + 2, y + (up ? 0 : 2));
            }
        }
    }
    for (int i = 0; i < 3; ++i)
        setVertices(m_diagnosticNodes[i], squiggles[i]);
    return m_root;
}
