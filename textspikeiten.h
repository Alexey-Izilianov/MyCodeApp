#pragma once

#include <QQuickPaintedItem>
#include <QElapsedTimer>

class TextSpikeItem : public QQuickPaintedItem {

    Q_PROPERTY(QString filePath READ filePath WRITE setFile NOTIFY filePathChanger)
    Q_PROPERTY(qreal scrollY READ scrollY WRITE setScrollY NOTIFY filePathChanger)

public:

    TextSpikeItem(QQuickItem *parent = nullptr);
    QString filePath() const {return m_filePath; }
    void setFilePath( const QString &path );
    qreal scrollY() const {return m_scrollY; }
    void setScrollY(qreal y);

protected:
    void  paint(QPainter *painter) override;

signals:
    void filePathChanged();
    void scrollYChanged();

private:
    QString m_filePath;
    qreal m_scrollY = 0.0;
    QStringList m_lines;
    QElapsedTimer m_fpsTimer;
    int m_frameCount = 0;

};
