#include "textspikeiten.h"
#include <QFile>
#include <QTextStream>
#include <QtMath>
#include <QPainter>
#include <QFontMetrics>
#include <QDebug>
#include <QUrl>
#include <fstream>

TextSpikeItem::TextSpikeItem(QQuickItem *parent) : QQuickPaintedItem(parent)
{
    setRenderTarget(QQuickPaintedItem::Image);
    setAntialiasing(true);
}

//Загрузка файлов
void TextSpikeItem::setFilePath(const QString &path)
{
    fprintf(stderr, "DIAG-cpp: setFilePath вызван, path=%s\n", qPrintable(path));
    fflush(stderr);

    if(path.isEmpty())
        return;

    // QML присылает URL ("file:///C:/..."), а QFile понимает только обычный путь.
    // toLocalFile() срезает схему и декодирует %20 и т.п.; если это не file:// —
    // оставляем строку как есть (путь "C:/..." пройдёт через fallback).
    const QUrl url(path);
    const QString localPath = url.isLocalFile() ? url.toLocalFile() : path;

    qDebug() << "DIAG: setFilePath ->" << localPath;

    QFile file(localPath);

    if(!file.open(QIODevice::ReadOnly | QIODevice::Text))
    {
        qWarning() << "Cannot open" << localPath << ":" << file.errorString();
        spikeLog("setFilePath FAIL: " + localPath.toStdString()
                 + " | " + file.errorString().toStdString());
        return;
    }

    m_lines.clear();

    // Лимит на время спайка: 1,5 млн строк — enough для 100+ МБ файла.
    // Настоящий TextBuffer (piece table, mmap) появится в M2.
    QElapsedTimer loadTimer;
    loadTimer.start();
    QTextStream in(&file);
    while (!in.atEnd() && m_lines.size() < 1500000)
    {
        m_lines << in.readLine();
    }

    qDebug() << "DIAG: загружено строк:" << m_lines.size();

    spikeLog("setFilePath OK: " + localPath.toStdString()
             + " | строк: " + std::to_string(m_lines.size())
             + " | загрузка мс: " + std::to_string(loadTimer.elapsed()));

    emit filePathChanged();
    update();
}

//Скрол текста
void TextSpikeItem::setScrollY(qreal y)
{
    if(y == m_scrollY)
        return;
    m_scrollY = y;
    update();
    emit scrollYChanged();
}

//Отрисовка
void TextSpikeItem::paint(QPainter *painter)
{
    if (m_lines.isEmpty())
        return;

    const QFont monoFont(QStringLiteral("Consolas"), 12);
    painter->setFont(monoFont);
    const QFontMetricsF fm(monoFont);
    const qreal lineHeight = fm.lineSpacing();
    const int firstLine = qBound(0, int(m_scrollY / lineHeight),
                                m_lines.size()- 1);
    const int visibleCount = qCeil(height() / lineHeight) + 1;

    for(int i = 0; i < visibleCount; ++i)
    {
        const int idx = firstLine + i;
        if(idx >= m_lines.size())
            break;

        painter->drawText(QPointF(0.0, (idx * lineHeight) - m_scrollY + fm.ascent()), m_lines.at(idx));
    }
    ++m_frameCount;
    if (!m_fpsTimer.isValid()) {

        m_fpsTimer.start();
    } else if(m_fpsTimer.elapsed() >= 1000) {
        qDebug() << "FPS:" << m_frameCount
                 << "| visible lines:" << visibleCount
                 << "| scrollY:" << m_scrollY;
        m_frameCount = 0;
        m_fpsTimer.restart();

        }
    }

