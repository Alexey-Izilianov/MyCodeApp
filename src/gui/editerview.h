#pragma once

#include <QQuickItem>
#include <QFont>
#include <QtQmlIntegration/qqmlintegration.h>
#include <QMutex>
#include "src/core/textbuffer.h"
#include "src/services/highlighter.h"

class QSGTransformNode;
class QSGClipNode;
class QSGSimpleRectNode;
class QSGTextNode;
class SearchEngine;

namespace core {
class Document;
}

// Редактор на QSG-нодах (вырос из TextSpikeQsgItem): ввод, один курсор,
// выделение, буфер обмена. Правки буфера — только через эти методы
// (мьютекс защищает буфер от параллельного чтения в updatePaintNode).
class EditorView : public QQuickItem {
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(QObject *document READ document WRITE setDocument
                   NOTIFY documentChanged)
    Q_PROPERTY(QString filePath READ filePath WRITE setFilePath
                   NOTIFY documentChanged)
    Q_PROPERTY(qreal scrollY READ scrollY WRITE setScrollY
                   NOTIFY scrollYChanged)
    Q_PROPERTY(int cursorLine READ cursorLine NOTIFY cursorChanged)
    Q_PROPERTY(int cursorColumn READ cursorColumn NOTIFY cursorChanged)
    Q_PROPERTY(QString encoding READ encoding NOTIFY documentChanged)
    Q_PROPERTY(QString lineEnding READ lineEnding NOTIFY documentChanged)

public:
    explicit EditorView(QQuickItem *parent = nullptr);

    QObject *document() const;
    void setDocument(QObject *doc);
    QString filePath() const;
    void setFilePath(const QString &path);
    qreal scrollY() const { return m_scrollY; }
    void setScrollY(qreal y);
    int cursorLine() const { return m_cursor.line; }
    int cursorColumn() const { return m_cursor.column; }
    QString encoding() const;
    QString lineEnding() const;
    bool hasSelection() const;

    Q_INVOKABLE void save();
    Q_INVOKABLE void copy();
    Q_INVOKABLE void cut();
    Q_INVOKABLE void paste();
    Q_INVOKABLE void selectAll();

    // Поиск (M1): асинхронный, результаты приходят сигналом searchUpdated
    Q_INVOKABLE void find(const QString &needle, bool caseSensitive);
    Q_INVOKABLE void findNext();
    Q_INVOKABLE void findPrev();
    Q_INVOKABLE void clearSearch();

signals:
    void documentChanged();
    void scrollYChanged();
    void cursorChanged();
    void errorOccurred(const QString &message);
    void findRequested();
    void searchUpdated(int current, int total);

protected:
    QSGNode *updatePaintNode(QSGNode *oldNode, UpdatePaintNodeData *) override;
    void keyPressEvent(QKeyEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;

private:
    using Cursor = core::TextBuffer::Position;

    void moveCursor(int dline, int dcol, bool extend);
    void ensureCursorVisible();
    void moveCursorAfterInsert(const QString &text);
    void deleteSelection();
    void markEdited();
    void jumpToMatch(int index);
    int nearestMatchFromCursor() const;
    void clearMatchRects();
    qreal contentHeight() const;

    core::Document *m_document = nullptr;
    qreal m_scrollY = 0.0;
    Cursor m_cursor;
    Cursor m_anchor; // второй конец выделения
    int m_goalColumn = 0; // «липкая» колонка для движения вверх/вниз

    QFont m_font;
    qreal m_lineHeight = 0.0;
    qreal m_charWidth = 0.0;

    bool m_mousePressed = false;

    QMutex m_docMutex; // доступ к document->buffer()

    // подсветка синтаксиса
    Highlighter m_highlighter;

    // поиск
    SearchEngine *m_searchEngine = nullptr;
    struct Match {
        int line = 0;
        int column = 0;
        int length = 0;
    };
    QVector<Match> m_matches;
    int m_currentMatch = -1;
    QString m_searchNeedle;
    bool m_searchCase = false;

    // ноды сцены
    QSGSimpleRectNode *m_background = nullptr;
    QSGClipNode *m_clip = nullptr;
    QSGTransformNode *m_scrollTransform = nullptr;
    QSGNode *m_selLayer = nullptr; // выделение рисуется ДО текстовых нод
    QSGNode *m_matchLayer = nullptr; // совпадения поиска — тоже под текстом
    QVector<QSGTextNode *> m_textPool;
    QVector<QSGSimpleRectNode *> m_selPool;
    QVector<QSGSimpleRectNode *> m_matchPool;
    QSGSimpleRectNode *m_caret = nullptr;
    QSizeF m_lastSize;
    int m_lastFirstLine = -1;
};