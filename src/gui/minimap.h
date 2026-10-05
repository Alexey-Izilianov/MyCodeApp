#pragma once

#include <QColor>
#include <QPointer>
#include <QQuickItem>
#include <QtQmlIntegration/qqmlintegration.h>

#include "editerview.h"

// Уменьшенная копия текста редактора с рамкой видимой области.
// Если ряды не помещаются, карта прокручивается пропорционально редактору,
// поэтому рамка при перетаскивании остаётся под мышью.
class Minimap : public QQuickItem {
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(EditorView *editor READ editor WRITE setEditor NOTIFY editorChanged)
    Q_PROPERTY(QColor background MEMBER m_background NOTIFY backgroundChanged)

public:
    explicit Minimap(QQuickItem *parent = nullptr);

    EditorView *editor() const { return m_editor; }
    void setEditor(EditorView *editor);

signals:
    void editorChanged();
    void backgroundChanged();

protected:
    QSGNode *updatePaintNode(QSGNode *oldNode, UpdatePaintNodeData *) override;
    void geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void hoverEnterEvent(QHoverEvent *event) override;
    void hoverLeaveEvent(QHoverEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;

private:
    int capacity() const;       // сколько рядов помещается
    qreal editorTopRow() const; // дробный ряд у верхнего края редактора
    qreal rowsPerPixel() const; // перевод смещения рамки в ряды редактора
    int firstRow() const;
    QRectF sliderRect() const;
    void scrollEditorTo(qreal topRow);
    QImage renderImage(int firstRow) const;
    void markDirty();

    QPointer<EditorView> m_editor;
    QColor m_background;
    bool m_imageDirty = true;
    int m_builtFirstRow = -1;
    bool m_hover = false;
    bool m_dragging = false;
    qreal m_dragOffset = 0; // где внутри рамки её схватили
};
