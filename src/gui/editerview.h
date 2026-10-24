#pragma once

#include <QFont>
#include <QHash>
#include <QSet>
#include <QPointer>
#include <QQuickItem>
#include <QScopedValueRollback>
#include <QTimer>
#include <QtQmlIntegration/qqmlintegration.h>
#include <functional>

#include "src/core/document.h"
#include "src/core/editing.h"
#include "src/core/folding.h"
#include "src/core/snippet.h"
#include <optional>
#include "src/services/highlighter.h"
#include "src/services/syntaxhighlighter.h"

class QSGClipNode;
class QSGGeometryNode;
class QSGSimpleRectNode;
class QSGTextNode;
class QSGTransformNode;
class SearchEngine;

// Редактор на QSG-нодах с несколькими курсорами. Все правки — через
// Document::replace (одна правка на все курсоры = один шаг истории).
// Буфер читается и в updatePaintNode: это безопасно без блокировок, потому
// что GUI-поток на время синхронизации сцены стоит, а фоновые задачи
// (поиск, tree-sitter) работают только со снимками.
class EditorView : public QQuickItem {
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(QObject *document READ document WRITE setDocument NOTIFY documentChanged)
    Q_PROPERTY(QString filePath READ filePath NOTIFY documentChanged)
    Q_PROPERTY(QString encoding READ encoding NOTIFY documentChanged)
    Q_PROPERTY(QString lineEnding READ lineEnding NOTIFY documentChanged)
    Q_PROPERTY(QString language READ language NOTIFY documentChanged)
    Q_PROPERTY(qreal scrollY READ scrollY WRITE setScrollY NOTIFY scrollChanged)
    Q_PROPERTY(qreal contentHeight READ contentHeight NOTIFY contentSizeChanged)
    Q_PROPERTY(int cursorLine READ cursorLine NOTIFY cursorChanged)
    Q_PROPERTY(int cursorColumn READ cursorColumn NOTIFY cursorChanged)
    Q_PROPERTY(int cursorCount READ cursorCount NOTIFY cursorChanged)
    Q_PROPERTY(bool canUndo READ canUndo NOTIFY historyChanged)
    Q_PROPERTY(bool canRedo READ canRedo NOTIFY historyChanged)
    Q_PROPERTY(qreal lineHeight READ lineHeight CONSTANT)
    Q_PROPERTY(bool readOnly READ readOnly NOTIFY documentChanged)
    Q_PROPERTY(bool hasSelection READ hasSelection NOTIFY cursorChanged) // у основного курсора
    Q_PROPERTY(bool snippetActive READ snippetActive NOTIFY snippetChanged)
    Q_PROPERTY(bool wordWrap READ wordWrap WRITE setWordWrap NOTIFY wordWrapChanged)
    // Справа место под миникарту: текст и перенос его не занимают
    Q_PROPERTY(qreal rightMargin READ rightMargin WRITE setRightMargin NOTIFY rightMarginChanged)

public:
    explicit EditorView(QQuickItem *parent = nullptr);
    ~EditorView() override;

    QObject *document() const { return m_document; }
    core::Document *textDocument() const { return m_document; }
    void setDocument(QObject *doc);
    QString filePath() const;
    QString encoding() const;
    QString lineEnding() const;
    QString language() const;
    static QString languageOf(const QString &path); // «C++», «Python»…; неизвестный — пусто
    qreal scrollY() const { return m_scrollY; }
    void setScrollY(qreal y);
    qreal contentHeight() const;
    int cursorLine() const { return primary().cursor.line; }
    int cursorColumn() const { return primary().cursor.column; }
    int cursorCount() const { return int(m_carets.size()); }
    bool canUndo() const { return m_document && m_document->canUndo(); }
    bool canRedo() const { return m_document && m_document->canRedo(); }
    qreal lineHeight() const { return m_lineHeight; }
    const QFont &textFont() const { return m_font; }
    bool readOnly() const { return m_document && m_document->isReadOnly(); }
    bool hasSelection() const { return primary().hasSelection(); }
    // Строки выделения основного курсора [from, to]; false — выделения нет
    bool selectedLines(int *from, int *to) const;

    // Перенос длинных строк по ширине окна. Не действует на файлы длиннее kMaxWrapLines
    // строк и на строки длиннее kLongLine символов — у них остаётся прокрутка вбок
    bool wordWrap() const { return m_wordWrap; }
    void setWordWrap(bool wrap);
    qreal rightMargin() const { return m_rightMargin; }
    void setRightMargin(qreal margin);

    // Ряд — строка экрана: видимая (не свёрнутая) строка или её часть при переносе
    int rowCount() const;

    struct ColorRun {
        int from = 0; // визуальные колонки (табуляции развёрнуты)
        int to = 0;
        QRgb color = 0;
        int font = 0; // SyntaxHighlighter::FontStyle
    };
    struct StyledLine {
        int line = 0;
        QString display; // табуляции развёрнуты в пробелы
        QVector<ColorRun> runs; // покрывают display целиком
        bool lastRow = true; // последний ряд перенесённой строки
    };
    // Подсвеченные ряды; строки обрезаются до maxColumn символов
    QVector<StyledLine> styledLines(int firstRow, int count, int maxColumn) const;

    Q_INVOKABLE void save();
    Q_INVOKABLE void undo();
    Q_INVOKABLE void redo();
    Q_INVOKABLE void copy();
    Q_INVOKABLE void cut();
    Q_INVOKABLE void paste();
    Q_INVOKABLE void selectAll();
    Q_INVOKABLE void selectNextOccurrence();
    Q_INVOKABLE void addCaretVertical(int direction); // -1 — выше, 1 — ниже
    Q_INVOKABLE void singleCaret();
    // Выделить фрагмент и показать его посередине экрана (результат поиска)
    Q_INVOKABLE void goTo(int line, int column, int length = 0);

    // Правки [{startLine, startColumn, endLine, endColumn, text}] относительно
    // текущего текста одним шагом отмены. caretEdit >= 0 — курсор ставится в
    // текст этой правки на caretOffset; иначе курсоры едут вместе с текстом
    Q_INVOKABLE void applyTextEdits(const QVariantList &edits, int caretEdit = -1, int caretOffset = 0);
    Q_INVOKABLE QString wordBeforeCursor() const;
    // Сниппет вместо [line:fromColumn, line:toColumn) (при выделении — вместо него):
    // Tab/Shift+Tab — по позициям ввода, Esc — выйти
    Q_INVOKABLE void insertSnippet(const QString &body, int line, int fromColumn, int toColumn);
    bool snippetActive() const { return m_snippet.has_value(); }
    // Для плагинов: одна правка = один шаг отмены
    Q_INVOKABLE QString text() const;
    Q_INVOKABLE void setText(const QString &text);
    Q_INVOKABLE QString selectedText() const; // основной курсор
    Q_INVOKABLE void replaceSelection(const QString &text);
    Q_INVOKABLE QString wordAtCursor() const;
    // Цвет под позицией (#rgb, #rrggbb, #rrggbbaa, rgb()/rgba()): {color, hex, rgb, hsl}; пусто — нет
    Q_INVOKABLE QVariantMap colorAt(int line, int column) const;
    Q_INVOKABLE QRectF caretRectangle() const; // основной курсор, в координатах элемента
    // [{startLine, startColumn, endLine, endColumn, severity}] — подчёркивания диагностики
    Q_INVOKABLE void setDiagnostics(const QVariantList &diagnostics);

    // Курсор и прокрутка документа для сессии: {line, column, scrollX, scrollY}
    Q_INVOKABLE QVariantMap viewStateOf(QObject *document) const;
    Q_INVOKABLE void setViewStateOf(QObject *document, const QVariantMap &state);

    // Полоски изменений (git) в gutter; Removed — перед строкой line удалены строки
    struct ChangeMark {
        enum Kind { Added, Modified, Removed };
        int line = 0;
        int count = 1;
        Kind kind = Added;
    };
    void setChangeMarks(QObject *document, QVector<ChangeMark> marks);
    // Просмотр изменений: тип каждой строки и номер для gutter (-1 — без номера)
    enum DiffLine : qint8 { Unchanged, Inserted, Deleted };
    void setDiffLines(QObject *document, QVector<qint8> kinds, QVector<int> numbers);
    // К следующему (direction > 0) или предыдущему изменению по кругу; 0 — к первому
    Q_INVOKABLE bool gotoChange(int direction);

    // Сворачивание: блок под основным курсором (или охватывающий его), все блоки
    Q_INVOKABLE void foldAtCursor();
    Q_INVOKABLE void unfoldAtCursor();
    Q_INVOKABLE void foldAll();
    Q_INVOKABLE void unfoldAll();

    Q_INVOKABLE void find(const QString &needle, bool caseSensitive);
    Q_INVOKABLE void findNext();
    Q_INVOKABLE void findPrev();
    Q_INVOKABLE void clearSearch();

signals:
    void documentChanged();
    void scrollChanged();
    void contentSizeChanged();
    void cursorChanged();
    void historyChanged();
    void errorOccurred(const QString &message);
    void searchUpdated(int current, int total);
    void contentChanged(); // текст, подсветка или свёрнутость
    void textTyped(const QString &text);
    void hoverRest(int line, int column, qreal x, qreal y); // мышь замерла над символом
    void hoverLeft();
    void definitionClicked(int line, int column); // Ctrl+клик
    void snippetChanged();
    void wordWrapChanged();
    void rightMarginChanged();

protected:
    QSGNode *updatePaintNode(QSGNode *oldNode, UpdatePaintNodeData *) override;
    void geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry) override;
    void focusInEvent(QFocusEvent *event) override;
    void focusOutEvent(QFocusEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void hoverMoveEvent(QHoverEvent *event) override;
    void hoverLeaveEvent(QHoverEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;

private:
    using Cursor = core::TextBuffer::Position;
    using EditKind = core::Document::EditKind;
    using Replacement = core::Document::Replacement;

    struct Caret {
        Cursor cursor;
        Cursor anchor;
        int goal = -1; // визуальная колонка для движения вверх/вниз; -1 — текущая
        bool hasSelection() const { return !(cursor == anchor); }
        Cursor start() const { return qMin(cursor, anchor); }
        Cursor end() const { return qMax(cursor, anchor); }
    };
    struct ViewState {
        QVector<Caret> carets{Caret{}};
        int primary = 0;
        qreal scrollX = 0;
        qreal scrollY = 0;
        core::folding::FoldSet folds;
    };
    struct Match {
        int line = 0;
        int column = 0;
        int length = 0;
    };
    enum class MouseMode { None, Select, Box };

    const core::TextBuffer &buffer() const { return m_document->buffer(); }
    const Caret &primary() const { return m_carets.at(m_primary); }
    // Последняя строка выделения: та, на начале которой оно кончается, не входит
    static int lastSelectedLine(const Caret &c)
    { return c.end().line > c.start().line && c.end().column == 0 ? c.end().line - 1 : c.end().line; }
    bool anySelection() const;

    // Курсоры
    void setCarets(QVector<Caret> carets, int primary);
    void normalizeCarets();
    void caretsChanged(bool scrollToCursor = true);
    void moveCarets(const std::function<Cursor(const Caret &)> &target, bool extend,
                    bool keepGoal = false);
    void setBoxSelection(Cursor anchor, Cursor cursor);
    core::Document::Selections selections() const;
    void setSelections(const core::Document::Selections &selections);

    // Правки. edit(caret, i) — замена для курсора i; place(i, caret до правки,
    // конец вставки) — итоговый курсор, если он не сразу за вставкой
    using EditFn = std::function<Replacement(const Caret &, int)>;
    using PlaceFn = std::function<Caret(int, const Caret &, Cursor)>;
    void editCarets(const EditFn &edit, EditKind kind, const PlaceFn &place = {});
    void replaceSelections(const QString &text, EditKind kind);
    void typeText(const QString &text);
    void insertNewline();
    void erase(bool forward, bool word);
    void indentLines(bool outdent);
    void afterTextChange();
    // Журнал правки документа (Document::changed): своей или из другого места — другой
    // половины экрана, LSP, восстановления
    void applyDocumentEdits(const QVector<core::TextBuffer::Edit> &edits, bool overflow);
    template <class F>
    auto selfEdit(F &&edit)
    {
        const QScopedValueRollback<bool> guard(m_selfEdit, true);
        return edit();
    }
    void spliceWrap(const QVector<core::TextBuffer::Edit> &edits, bool overflow);
    QChar charAt(Cursor c, int delta) const;

    Cursor stepLeft(Cursor c, bool word) const;
    Cursor stepRight(Cursor c, bool word) const;
    Cursor cursorAt(const QPointF &pos) const; // колонка может быть за концом строки
    // {строка, визуальная колонка}; nearest — ближайшая граница символов, иначе символ под точкой
    Cursor visualAt(const QPointF &pos, bool nearest = true) const;
    int visualOf(Cursor c) const;
    Cursor fromVisual(int line, int visual) const;
    void updateBrackets();
    QPair<int, int> wordAt(Cursor c) const;
    void ensureCursorVisible();
    void setScroll(qreal x, qreal y);
    void restartCaretBlink();
    qreal textLeft() const { return m_gutterWidth; }
    qreal textWidth() const { return qMax<qreal>(0, width() - m_gutterWidth - m_rightMargin); }
    void updateGutterWidth();

    void markContentChanged();
    void track(core::Document *doc); // разовые подписки на документ
    void documentReloaded();

    // Ряды экрана с учётом переноса: первый ряд строки (скрытой — её заголовка) и строка ряда
    int rowOf(int line) const;
    int lineAt(int row) const;
    // Видимая строка на rows видимых строк ниже (выше); перенос не учитывается
    int lineByRows(int line, int rows) const;
    // Перенос
    bool wrapping() const;
    void invalidateWrap();
    void updateWrapColumns();
    void ensureWrap() const;
    struct RowLayout {
        QVector<int> starts{0}; // визуальные колонки начала рядов строки
        int indent = 0;         // сдвиг рядов-продолжений
    };
    RowLayout rowLayout(int line) const;
    QVector<int> rowStarts(int line) const { return rowLayout(line).starts; }
    QPoint screenOf(Cursor c) const;            // {колонка в ряду, ряд экрана}
    Cursor visualAtScreen(int row, int column) const; // {строка, визуальная колонка}
    // Отрезок [v0, v1) визуальных колонок строки по рядам — прямоугольники в координатах текста
    QVector<QRectF> spanRects(int line, int v0, int v1) const;
    bool isLineVisible(int line, int firstLine, int lastLine) const
    { return line >= firstLine && line <= lastLine && !m_folds.isHidden(line); }
    bool inFoldMarker(qreal x) const
    { return x >= m_gutterWidth - 2 * m_charWidth && x < m_gutterWidth; }
    void toggleFold(int line, bool scrollToCursor = true); // клик мышью — вид не прыгает к курсору
    void foldsChanged();
    void liftHiddenCarets(); // курсоры из свёрнутого — на строку заголовка

    // Сниппет: позиции ввода в смещениях документа, в порядке обхода ($0 — последняя)
    struct SnippetSession {
        QVector<QVector<core::snippet::Range>> stops;
        int current = 0;
    };
    void selectSnippetStop(int index);
    void updateSnippet(const QVector<core::TextBuffer::Edit> &edits);
    void endSnippet();

    // Поиск
    void jumpToMatch(int index);
    int nearestMatchFromCursor() const;

    // Рендер
    void buildTextNodes(int firstRow, int count, int column0, int column1);
    void buildGutterNodes(int firstRow, const QVector<int> &lineNumbers, const QVector<int> &caretLines);
    void buildFoldMarkers(int firstRow, const QVector<int> &lineNumbers);
    QVector<QPair<QRectF, QColor>> changeRects(int firstLine, int lastLine) const;
    QVector<QPair<QRectF, QColor>> diffRects(int firstRow, const QVector<int> &lineNumbers) const;
    qreal foldPlaceholderX(int line) const;
    QSGTextNode *textNodeFor(QRgb color);
    void setRects(QSGNode *layer, QVector<QSGSimpleRectNode *> &pool,
                  const QVector<QPair<QRectF, QColor>> &rects);

    QPointer<core::Document> m_document;
    QHash<core::Document *, ViewState> m_viewStates;
    QSet<core::Document *> m_tracked;

    QVector<Caret> m_carets{Caret{}}; // отсортированы, не пересекаются
    int m_primary = 0;                // основной: статус-бар, прокрутка
    bool m_boxActive = false;         // выделение столбцом (Alt+Shift+стрелки/мышь)
    Cursor m_boxAnchor;               // углы блока: {строка, визуальная колонка}
    Cursor m_boxCursor;
    QVector<Cursor> m_brackets;       // скобка у основного курсора и её пара
    core::editing::IndentStyle m_indent;
    bool m_python = false;            // отступ после ':'
    core::folding::FoldSet m_folds;
    std::optional<SnippetSession> m_snippet;
    bool m_gutterHover = false;       // маркеры развёрнутых блоков — только при наведении
    struct Diagnostic {
        Cursor start;
        Cursor end;
        int severity = 1;
    };
    QVector<Diagnostic> m_diagnostics;
    QHash<const core::Document *, QVector<ChangeMark>> m_changeMarks;
    struct DiffLines {
        QVector<qint8> kinds;
        QVector<int> numbers;
    };
    QHash<const core::Document *, DiffLines> m_diffLines;
    QTimer m_hoverTimer;
    QPointF m_hoverPos;

    MouseMode m_mouseMode = MouseMode::None;

    qreal m_scrollX = 0;
    qreal m_scrollY = 0;

    bool m_selfEdit = false; // идёт правка из этого вида
    qreal m_rightMargin = 0;
    bool m_wordWrap = false;
    int m_wrapColumns = 0;                 // ширина ряда в колонках
    mutable QVector<int> m_wrapExtra;      // по строкам: рядов сверх первого; -1 — пересчитать
    mutable bool m_wrapPending = false;    // есть -1 в m_wrapExtra — в строках [from, to)
    mutable int m_wrapPendingFrom = 0;
    mutable int m_wrapPendingTo = 0;
    mutable QVector<int> m_wrapBefore;     // по строкам + итог: лишних рядов у видимых строк выше
    mutable QHash<int, RowLayout> m_rowStartsCache;

    QFont m_font;
    qreal m_lineHeight = 0;
    qreal m_charWidth = 0;
    qreal m_textOffsetY = 0; // строка текста по центру своей высоты
    qreal m_gutterWidth = 0;

    QTimer m_caretTimer;
    bool m_caretVisible = true;

    Highlighter m_highlighter;  // регэкспы: JSON/MD и фоллбек
    SyntaxHighlighter m_syntax; // tree-sitter: C++/Python/QML

    SearchEngine *m_searchEngine = nullptr;
    QVector<Match> m_matches;
    int m_currentMatch = -1;
    QString m_searchNeedle;
    bool m_searchCase = false;

    // Сцена
    bool m_contentDirty = true; // текст/подсветка поменялись — пересобрать ноды
    bool m_resetScene = false;  // сменилась тема — все ноды заново
    int m_builtFirstRow = -1;
    int m_builtRowCount = 0;
    int m_builtColumn0 = 0; // собранное окно колонок (длинные строки)
    int m_builtColumn1 = 0;
    QVector<int> m_builtCaretLines;
    QVector<int> m_builtFoldable;     // видимые строки, которые можно свернуть
    bool m_builtGutterHover = false;
    QSGNode *m_root = nullptr;
    QSGSimpleRectNode *m_background = nullptr;
    QSGNode *m_diffLayer = nullptr;   // фон добавленных/удалённых строк
    QSGNode *m_currentLineLayer = nullptr;
    QSGNode *m_changeLayer = nullptr; // полоски изменений в gutter
    QSGClipNode *m_textClip = nullptr;
    QSGTransformNode *m_textTransform = nullptr;
    QSGClipNode *m_gutterClip = nullptr;
    QSGTransformNode *m_gutterTransform = nullptr;
    QSGNode *m_selectionLayer = nullptr;
    QSGNode *m_matchLayer = nullptr;
    QSGNode *m_bracketLayer = nullptr;
    QSGNode *m_foldLayer = nullptr;   // плашки «…» на свёрнутых строках
    QSGGeometryNode *m_foldMarkers = nullptr;       // ▾ развёрнутые
    QSGGeometryNode *m_foldedMarkers = nullptr;     // ▸ свёрнутые
    QSGGeometryNode *m_diagnosticNodes[3] = {};     // волнистые линии: ошибки, предупреждения, прочее
    QSGNode *m_textLayer = nullptr;
    QSGNode *m_caretLayer = nullptr;
    QHash<QRgb, QSGTextNode *> m_textNodes;
    QSGTextNode *m_gutterNode = nullptr;
    QSGTextNode *m_gutterCurrentNode = nullptr;
    QVector<QSGSimpleRectNode *> m_currentLinePool;
    QVector<QSGSimpleRectNode *> m_diffPool;
    QVector<QSGSimpleRectNode *> m_changePool;
    QVector<QSGSimpleRectNode *> m_selectionPool;
    QVector<QSGSimpleRectNode *> m_matchPool;
    QVector<QSGSimpleRectNode *> m_bracketPool;
    QVector<QSGSimpleRectNode *> m_foldPool;
    QVector<QSGSimpleRectNode *> m_caretPool;
};
