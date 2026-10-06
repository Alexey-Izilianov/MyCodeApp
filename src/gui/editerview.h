#pragma once

#include <QFont>
#include <QHash>
#include <QSet>
#include <QPointer>
#include <QQuickItem>
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
    Q_PROPERTY(bool snippetActive READ snippetActive NOTIFY snippetChanged)

public:
    explicit EditorView(QQuickItem *parent = nullptr);
    ~EditorView() override;

    QObject *document() const { return m_document; }
    void setDocument(QObject *doc);
    QString filePath() const;
    QString encoding() const;
    QString lineEnding() const;
    QString language() const;
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

    // Ряд — строка на экране после вычета свёрнутых
    int rowCount() const { return m_document ? m_folds.rowCount(buffer().lineCount()) : 0; }

    struct ColorRun {
        int from = 0; // визуальные колонки (табуляции развёрнуты)
        int to = 0;
        QRgb color = 0;
    };
    struct StyledLine {
        int line = 0;
        QString display; // табуляции развёрнуты в пробелы
        QVector<ColorRun> runs; // покрывают display целиком
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
    QChar charAt(Cursor c, int delta) const;

    Cursor stepLeft(Cursor c, bool word) const;
    Cursor stepRight(Cursor c, bool word) const;
    Cursor cursorAt(const QPointF &pos) const; // колонка может быть за концом строки
    Cursor visualAt(const QPointF &pos) const; // {строка, визуальная колонка}
    int visualOf(Cursor c) const;
    Cursor fromVisual(int line, int visual) const;
    void updateBrackets();
    QPair<int, int> wordAt(Cursor c) const;
    void ensureCursorVisible();
    void setScroll(qreal x, qreal y);
    void restartCaretBlink();
    qreal textLeft() const { return m_gutterWidth; }
    qreal textWidth() const { return qMax<qreal>(0, width() - m_gutterWidth); }
    void updateGutterWidth();

    void markContentChanged();
    void track(core::Document *doc); // разовые подписки на документ
    void documentReloaded();

    int rowOf(int line) const { return m_folds.rowOf(line); }
    int lineAt(int row) const { return m_folds.lineAt(qBound(0, row, rowCount() - 1)); }
    int lineByRows(int line, int rows) const { return lineAt(rowOf(line) + rows); }
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
