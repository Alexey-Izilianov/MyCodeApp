#pragma once

#include <QColor>
#include <QFont>
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
    // 1 — сплошные полоски, 2 и 3 — настоящие символы мелким и крупным шрифтом.
    // Ширина карты (implicitWidth) зависит от масштаба
    Q_PROPERTY(int level READ level WRITE setLevel NOTIFY levelChanged)

public:
    explicit Minimap(QQuickItem *parent = nullptr);

    EditorView *editor() const { return m_editor; }
    void setEditor(EditorView *editor);
    int level() const { return m_level; }
    void setLevel(int level);

    static constexpr int kMinLevel = 1;
    static constexpr int kMaxLevel = 3;

signals:
    void editorChanged();
    void backgroundChanged();
    void levelChanged();

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
    void updateMetrics();
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
    int m_level = 3;
    qreal m_rowPitch = 2;  // высота ряда, логические пиксели
    qreal m_charWidth = 1;
    QFont m_font;          // для масштабов с символами
    bool m_imageDirty = true;
    int m_builtFirstRow = -1;
    bool m_hover = false;
    bool m_dragging = false;
    qreal m_dragOffset = 0; // где внутри рамки её схватили
};
