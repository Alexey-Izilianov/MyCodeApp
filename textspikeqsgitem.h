#pragma once

#include <QQuickItem>
#include <QElapsedTimer>
#include <QFont>
#include <QMutex>
#include <QtQmlIntegration/qqmlintegration.h>
#include <QStringList>

class QSGTextNode;
class QSGTransformNode;
class QSGClipNode;
class QSGSimpleRectNode;

// Спайк №2: тот же тест, что и TextSpikeItem (спайк №1), но на сцене QSG.
// Идея: глифы каждой строки лежат в GPU-атласе, при скролле ноды не
// перерисовываются — двигается только transform. Если firstLine не менялся,
// кадр обновляет лишь матрицу (в лог пишем, сколько кадров было rebuild).
class TextSpikeQsgItem : public QQuickItem {

    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(QString filePath READ filePath WRITE setFilePath
                   NOTIFY filePathChanged)
    Q_PROPERTY(qreal scrollY READ scrollY WRITE setScrollY
                   NOTIFY scrollYChanged)
    Q_PROPERTY(bool autoScroll READ autoScroll WRITE setAutoScroll
                   NOTIFY autoScrollChanged)

public:
    explicit TextSpikeQsgItem(QQuickItem *parent = nullptr);

    QString filePath() const { return m_filePath; }
    void setFilePath(const QString &path);
    qreal scrollY() const { return m_scrollY; }
    void setScrollY(qreal y);
    bool autoScroll() const { return m_autoScroll; }
    void setAutoScroll(bool on);

signals:
    void filePathChanged();
    void scrollYChanged();
    void autoScrollChanged();

protected:
    QSGNode *updatePaintNode(QSGNode *oldNode,
                             UpdatePaintNodeData *data) override;

private:
    QString m_filePath;
    qreal m_scrollY = 0.0;
    bool m_autoScroll = false;

    QStringList m_lines;
    QMutex m_linesMutex; // setFilePath (GUI) пишет, updatePaintNode (render) читает

    QFont m_font;
    qreal m_lineHeight = 0.0;

    QSizeF m_lastSize;
    int m_lastFirstLine = -1;
    QVector<QSGTextNode *> m_pool;
    QSGTransformNode *m_scrollTransform = nullptr;
    QSGSimpleRectNode *m_background = nullptr;
    QSGClipNode *m_clip = nullptr;

    QElapsedTimer m_fpsTimer;
    int m_frameCount = 0;
    int m_transformFrames = 0;
    int m_rebuildFrames = 0;
    QElapsedTimer m_layoutTimer;
    qint64 m_layoutUs = 0;
    std::atomic<int> m_guiUpdates{0}; // вызовы setScrollY на GUI-треде
};