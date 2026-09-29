#pragma once

#include <QFont>
#include <QHash>
#include <QPointer>
#include <QQuickItem>
#include <QTimer>
#include <QtQmlIntegration/qqmlintegration.h>

#include "src/core/document.h"
#include "src/services/highlighter.h"
#include "src/services/syntaxhighlighter.h"

class QSGClipNode;
class QSGSimpleRectNode;
class QSGTextNode;
class QSGTransformNode;
class SearchEngine;

// Редактор на QSG-нодах. Все правки — через Document::replace (история undo).
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
    Q_PROPERTY(bool canUndo READ canUndo NOTIFY historyChanged)
    Q_PROPERTY(bool canRedo READ canRedo NOTIFY historyChanged)

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
    int cursorLine() const { return m_cursor.line; }
    int cursorColumn() const { return m_cursor.column; }
    bool canUndo() const { return m_document && m_document->canUndo(); }
    bool canRedo() const { return m_document && m_document->canRedo(); }

    Q_INVOKABLE void save();
    Q_INVOKABLE void undo();
    Q_INVOKABLE void redo();
    Q_INVOKABLE void copy();
    Q_INVOKABLE void cut();
    Q_INVOKABLE void paste();
    Q_INVOKABLE void selectAll();

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
    void wheelEvent(QWheelEvent *event) override;

private:
    using Cursor = core::TextBuffer::Position;
    using EditKind = core::Document::EditKind;

    struct ViewState {
        Cursor cursor;
        Cursor anchor;
        qreal scrollX = 0;
        qreal scrollY = 0;
    };
    struct Match {
        int line = 0;
        int column = 0;
        int length = 0;
    };

    const core::TextBuffer &buffer() const { return m_document->buffer(); }
    bool hasSelection() const { return !(m_cursor == m_anchor); }

    // Правки
    void replaceSelection(const QString &text, EditKind kind);
    void replaceRange(Cursor from, Cursor to, const QString &text, EditKind kind);
    void afterTextChange();

    // Курсор и прокрутка
    void moveCursor(Cursor to, bool extend);
    Cursor stepLeft(Cursor c, bool word) const;
    Cursor stepRight(Cursor c, bool word) const;
    Cursor cursorAt(const QPointF &pos) const;
    void ensureCursorVisible();
    void setScroll(qreal x, qreal y);
    void restartCaretBlink();
    qreal textLeft() const { return m_gutterWidth; }
    qreal textWidth() const { return qMax<qreal>(0, width() - m_gutterWidth); }
    void updateGutterWidth();

    // Поиск
    void jumpToMatch(int index);
    int nearestMatchFromCursor() const;

    // Рендер
    void buildTextNodes(int firstLine, const QStringList &lines, int column0, int column1);
    void buildGutterNodes(int firstLine, int count);
    QSGTextNode *textNodeFor(QRgb color);
    void setRects(QSGNode *layer, QVector<QSGSimpleRectNode *> &pool,
                  const QVector<QRectF> &rects, const QColor &color);
    void setRects(QSGNode *layer, QVector<QSGSimpleRectNode *> &pool,
                  const QVector<QPair<QRectF, QColor>> &rects);

    QPointer<core::Document> m_document;
    QHash<core::Document *, ViewState> m_viewStates;

    Cursor m_cursor;
    Cursor m_anchor;
    int m_goalColumn = 0; // колонка для движения вверх/вниз
    qreal m_scrollX = 0;
    qreal m_scrollY = 0;
    bool m_mousePressed = false;

    QFont m_font;
    qreal m_lineHeight = 0;
    qreal m_charWidth = 0;
    qreal m_textOffsetY = 0; // строка текста по центру своей высоты
    qreal m_gutterWidth = 0;

    QTimer m_caretTimer;
    bool m_caretVisible = true;

    Highlighter m_highlighter;  // регэкспы: JSON/MD и фоллбек
    SyntaxHighlighter m_syntax; // tree-sitter: C++/Python

    SearchEngine *m_searchEngine = nullptr;
    QVector<Match> m_matches;
    int m_currentMatch = -1;
    QString m_searchNeedle;
    bool m_searchCase = false;

    // Сцена
    bool m_contentDirty = true; // текст/подсветка поменялись — пересобрать ноды
    int m_builtFirstLine = -1;
    int m_builtLineCount = 0;
    int m_builtColumn0 = 0; // собранное окно колонок (длинные строки)
    int m_builtColumn1 = 0;
    int m_builtCursorLine = -1;
    QSGNode *m_root = nullptr;
    QSGSimpleRectNode *m_background = nullptr;
    QSGSimpleRectNode *m_currentLine = nullptr;
    QSGClipNode *m_textClip = nullptr;
    QSGTransformNode *m_textTransform = nullptr;
    QSGTransformNode *m_gutterTransform = nullptr;
    QSGNode *m_selectionLayer = nullptr;
    QSGNode *m_matchLayer = nullptr;
    QSGNode *m_textLayer = nullptr;
    QSGSimpleRectNode *m_caret = nullptr;
    QHash<QRgb, QSGTextNode *> m_textNodes;
    QSGTextNode *m_gutterNode = nullptr;
    QSGTextNode *m_gutterCurrentNode = nullptr;
    QVector<QSGSimpleRectNode *> m_selectionPool;
    QVector<QSGSimpleRectNode *> m_matchPool;
};
