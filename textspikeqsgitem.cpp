#include "textspikeqsgitem.h"

#include <QFontMetricsF>
#include <atomic>
#include <QSGRendererInterface>
#include <QFile>
#include <QTextStream>
#include <QTextLayout>
#include <QUrl>
#include <QtMath>
#include <QQuickWindow>
#include <qsgtextnode.h>
#include <QSGSimpleRectNode>
#include <QSGClipNode>
#include <QSGTransformNode>
#include "textspikeiten.h" // spikeLog()

TextSpikeQsgItem::TextSpikeQsgItem(QQuickItem *parent) : QQuickItem(parent)
{
    // Без этого флага QSG не зовёт updatePaintNode: элемент считается "пустым".
    setFlag(QQuickItem::ItemHasContents, true);

    m_font = QFont(QStringLiteral("Consolas"), 12);
    const QFontMetricsF fm(m_font);
    m_lineHeight = fm.lineSpacing();
}

void TextSpikeQsgItem::setFilePath(const QString &path)
{
    if (path.isEmpty())
        return;

    const QUrl url(path);
    const QString localPath = url.isLocalFile() ? url.toLocalFile() : path;

    QFile file(localPath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        spikeLog("QSG setFilePath FAIL: " + localPath.toStdString());
        return;
    }

    QElapsedTimer loadTimer;
    loadTimer.start();

    QStringList lines;
    QTextStream in(&file);
    while (!in.atEnd() && lines.size() < 1500000)
        lines << in.readLine();

    m_linesMutex.lock();
    m_lines = lines;
    m_linesMutex.unlock();

    m_lastFirstLine = -1; // принудительный rebuild нод

    spikeLog("QSG setFilePath OK: " + localPath.toStdString()
             + " | строк: " + std::to_string(lines.size())
             + " | загрузка мс: " + std::to_string(loadTimer.elapsed()));

    emit filePathChanged();
    update();
}

void TextSpikeQsgItem::setScrollY(qreal y)
{
    if (y == m_scrollY)
        return;
    m_scrollY = qMax<qreal>(0, y);
    m_guiUpdates.fetch_add(1, std::memory_order_relaxed);
    update();
    emit scrollYChanged();
}

void TextSpikeQsgItem::setAutoScroll(bool on)
{
    if (on == m_autoScroll)
        return;
    m_autoScroll = on;
    emit autoScrollChanged();
}

// Вызывается на render-треде при каждом update().
QSGNode *TextSpikeQsgItem::updatePaintNode(QSGNode *oldNode,
                                           UpdatePaintNodeData *)
{
    static bool firstCall = true;
    if (firstCall) {
        firstCall = false;
        spikeLog("QSG updatePaintNode: первый вызов, window=" +
                 std::string(window() ? "ok" : "NULL")
                 + ", graphicsApi=" + std::to_string(
                     int(window()->rendererInterface()->graphicsApi())));
    }

    QSGNode *root = oldNode;
    if (!root) {
        root = new QSGNode;

        m_background = new QSGSimpleRectNode(QRectF(0, 0, width(), height()),
                                             QColor(0x1e, 0x1e, 0x1e));
        root->appendChildNode(m_background);

        m_clip = new QSGClipNode;
        m_clip->setClipRect(QRectF(0, 0, width(), height()));
        m_clip->setIsRectangular(true);
        root->appendChildNode(m_clip);

        m_scrollTransform = new QSGTransformNode;
        m_clip->appendChildNode(m_scrollTransform);
    }

    // Смена размера окна: фон/клип и принудительный пересмотр пула.
    if (QSizeF(width(), height()) != m_lastSize) {
        m_lastSize = QSizeF(width(), height());
        m_background->setRect(QRectF(0, 0, width(), height()));
        m_clip->setClipRect(QRectF(0, 0, width(), height()));
        m_lastFirstLine = -1;
    }

    ++m_frameCount;
    if (!m_fpsTimer.isValid()) {
        m_fpsTimer.start();
    } else if (m_fpsTimer.elapsed() >= 1000) {
        spikeLog("QSG FPS: " + std::to_string(m_frameCount)
                 + " | transform-only: " + std::to_string(m_transformFrames)
                 + " | rebuild: " + std::to_string(m_rebuildFrames)
                 + " | gui updates/s: " + std::to_string(m_guiUpdates.exchange(0))
                 + " | layout us/frame: "
                 + std::to_string(m_rebuildFrames ? m_layoutUs / m_rebuildFrames : 0));
        m_frameCount = 0;
        m_transformFrames = 0;
        m_rebuildFrames = 0;
        m_layoutUs = 0;
    }

    m_linesMutex.lock();
    const int lineCount = m_lines.size();
    if (lineCount == 0) {
        m_linesMutex.unlock();
        return root;
    }
    const int firstLine = qBound(0, int(m_scrollY / m_lineHeight), lineCount - 1);
    const int visibleCount = qCeil(height() / m_lineHeight) + 1;
    const int take = qMin(visibleCount, lineCount - firstLine);
    QStringList visibleLines = m_lines.mid(firstLine, take);
    m_linesMutex.unlock();

    // Скролл внутри страницы: глифы уже в атласе, двигаем только матрицу.
    if (firstLine == m_lastFirstLine && m_pool.size() >= take) {
        QMatrix4x4 m;
        m.translate(0.0, float(-m_scrollY));
        m_scrollTransform->setMatrix(m);
        m_scrollTransform->markDirty(QSGNode::DirtyMatrix);
        ++m_transformFrames;
        return root;
    }
    ++m_rebuildFrames;
    m_layoutTimer.start();

    QMatrix4x4 m;
    m.translate(0.0, float(-m_scrollY));
    m_scrollTransform->setMatrix(m);
    m_scrollTransform->markDirty(QSGNode::DirtyMatrix);

    // Пул нод: переиспользуем, добираем, лишние убираем.
    while (m_pool.size() < take) {
        QSGTextNode *node = window()->createTextNode();
        if (!node) {
            spikeLog("QSG createTextNode вернул nullptr!");
            return root;
        }
        node->setColor(QColor(0xd4, 0xd4, 0xd4));
        node->setRenderType(QSGTextNode::RenderType::QtRendering);
        m_scrollTransform->appendChildNode(node);
        m_pool.append(node);
    }
    spikeLog("QSG: пул создан, нод: " + std::to_string(m_pool.size()));
    while (m_pool.size() > take) {
        QSGTextNode *node = m_pool.takeLast();
        m_scrollTransform->removeChildNode(node);
        delete node;
    }

    for (int i = 0; i < take; ++i) {
        QSGTextNode *node = m_pool.at(i);
        QMatrix4x4 nodeMatrix;
        nodeMatrix.translate(0.0, float((firstLine + i) * m_lineHeight));
        node->setMatrix(nodeMatrix);

        QTextLayout layout(visibleLines.at(i), m_font);
        layout.beginLayout();
        QTextLine line = layout.createLine();
        if (line.isValid())
            line.setLineWidth(100000); // без переноса
        layout.endLayout();

        node->clear();
        node->addTextLayout(QPointF(0.0, 0.0), &layout);
        node->markDirty(QSGNode::DirtyGeometry | QSGNode::DirtyMaterial);
    }

    m_lastFirstLine = firstLine;
    m_layoutUs += m_layoutTimer.nsecsElapsed() / 1000;
    if (m_rebuildFrames == 1) // первый rebuild — контрольный лог
        spikeLog("QSG: rebuild завершён, строк залейауто: " + std::to_string(take));
    return root;
}