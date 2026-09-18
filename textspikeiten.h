#pragma once

#include <QQuickPaintedItem>
#include <QElapsedTimer>
#include <QtQmlIntegration/qqmlintegration.h>
#include <fstream>
#include <string>

// Спайк-лог напрямую в файл: stderr у GUI-приложения Windows ненадёжен,
// а append-режим ofstream работает всегда. Общая для main.cpp и спайка.
inline void spikeLog(const std::string &line)
{
    std::ofstream log("C:/Projects/MyCodeApp/spike-cpp.log", std::ios::app);
    log << line << std::endl;
}

class TextSpikeItem : public QQuickPaintedItem {

    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(QString filePath READ filePath WRITE setFilePath
                   NOTIFY filePathChanged)
    Q_PROPERTY(qreal scrollY READ scrollY WRITE setScrollY
                   NOTIFY scrollYChanged)

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
