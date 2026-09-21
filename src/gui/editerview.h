#pragma once

#include <QQuickItem>
#include <QFont>
#include <QtQmlIntegration/qqmlintegration.h>
#include <QMutex>
#include "src/core/textbuffer.h"

class QSGTransformNode;
class QSGClipNode;
class QSGSimpleRectNode;
class QSGTextNode;

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
    bool hasSelection() const;

    Q_INVOKABLE void save();
    Q_INVOKABLE void copy();
    Q_INVOKABLE void cut();
    Q_INVOKABLE void paste();
    Q_INVOKABLE void selectAll();

signals:
    void documentChanged();
    void scrollYChanged();
    void cursorChanged();
    void errorOccurred(const QString &message);

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
    void deleteSelection();
    void markEdited();
    qreal contentHeight() const;

    core::Document *m_document = nullptr;
    qreal m_scrollY = 0.0;
    Cursor m_cursor;
    Cursor m_anchor; // второй конец выделения

    QFont m_font;
    qreal m_lineHeight = 0.0;
    qreal m_charWidth = 0.0;

    bool m_mousePressed = false;

    QMutex m_docMutex; // доступ к document->buffer()

    // ноды сцены
    QSGSimpleRectNode *m_background = nullptr;
    QSGClipNode *m_clip = nullptr;
    QSGTransformNode *m_scrollTransform = nullptr;
    QVector<QSGTextNode *> m_textPool;
    QVector<QSGSimpleRectNode *> m_selPool;
    QSGSimpleRectNode *m_caret = nullptr;
    QSizeF m_lastSize;
    int m_lastFirstLine = -1;
};