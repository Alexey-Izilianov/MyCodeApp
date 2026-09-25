#include "documentmanager.h"

#include <QFileInfo>
#include <spdlog/spdlog.h>

using core::Document;

DocumentManager::DocumentManager(QObject *parent)
    : QObject(parent)
{
}

void DocumentManager::setCurrentIndex(int index)
{
    if (index == m_current || index < -1 || index >= m_docs.size())
        return;
    m_current = index;
    emit currentIndexChanged();
}

QObject *DocumentManager::currentDocument() const
{
    const bool valid = m_current >= 0 && m_current < m_docs.size();
    return valid ? m_docs.at(m_current) : nullptr;
}

QVariantList DocumentManager::tabs() const
{
    QVariantList list;
    for (core::Document *doc : m_docs) {
        QVariantMap tab;
        tab.insert(QStringLiteral("name"), doc->displayName());
        tab.insert(QStringLiteral("dirty"), doc->isDirty());
        tab.insert(QStringLiteral("path"), doc->filePath());
        list.append(tab);
    }
    return list;
}

int DocumentManager::open(const QUrl &url)
{
    const QString path = url.isLocalFile() ? url.toLocalFile() : url.toString();
    if (path.isEmpty())
        return -1;

    for (int i = 0; i < m_docs.size(); ++i) {
        if (m_docs.at(i)->filePath() == path) {
            activate(i); // уже открыт — просто переключаемся
            return i;
        }
    }

    auto *doc = new Document(this);
    if (QFileInfo(path).size() > kAsyncLoadThreshold) {
        // Большой файл: грузим в фоне, в табы опубликуем по loadFinished.
        m_loadingDocs.append(doc);
        connect(doc, &Document::loadProgress, this, [this](int percent) {
            m_loadPercent = percent;
            emit loadPercentChanged();
        });
        connect(doc, &Document::loadFinished, this,
                [this, doc](bool ok) { finishAsyncLoad(doc, ok); });
        emit loadingChanged();
        m_loadTimer.start();
        if (!doc->loadAsync(path)) {
            m_loadingDocs.removeOne(doc);
            emit loadingChanged();
            delete doc;
            emit errorOccurred(QStringLiteral("не удалось открыть файл"));
            return -1;
        }
        return -1; // таб появится по окончании загрузки
    }

    if (!doc->load(path)) {
        delete doc;
        emit errorOccurred(QStringLiteral("не удалось открыть файл"));
        return -1;
    }
    connect(doc, &Document::dirtyChanged, this, &DocumentManager::tabsChanged);
    m_docs.append(doc);
    m_current = m_docs.size() - 1;
    emit tabsChanged();
    emit currentIndexChanged();
    return m_current;
}

void DocumentManager::finishAsyncLoad(core::Document *doc, bool ok)
{
    m_loadingDocs.removeOne(doc);
    if (ok) {
        spdlog::info("dm: async load {} мс, {} байт",
                     m_loadTimer.elapsed(), doc->filePath().toStdString());
        connect(doc, &Document::dirtyChanged, this,
                &DocumentManager::tabsChanged);
        m_docs.append(doc);
        m_current = m_docs.size() - 1;
        emit tabsChanged();
        emit currentIndexChanged();
    } else {
        doc->deleteLater();
        emit errorOccurred(QStringLiteral("не удалось открыть файл"));
    }
    if (!isLoading())
        emit loadingChanged();
}

void DocumentManager::close(int index)
{
    if (index < 0 || index >= m_docs.size())
        return;
    core::Document *doc = m_docs.takeAt(index);
    if (m_current >= m_docs.size())
        m_current = m_docs.size() - 1;
    // Сначала уведомляем: редактор отцепит документ (у него свой мьютекс,
    // защищающий буфер от рендер-потока), и только потом удаляем.
    emit tabsChanged();
    emit currentIndexChanged();
    doc->deleteLater();
}

void DocumentManager::activate(int index)
{
    if (index == m_current || index < 0 || index >= m_docs.size())
        return;
    m_current = index;
    emit currentIndexChanged();
}

bool DocumentManager::isDirty(int index) const
{
    const bool valid = index >= 0 && index < m_docs.size();
    return valid && m_docs.at(index)->isDirty();
}

void DocumentManager::save(int index)
{
    if (index < 0 || index >= m_docs.size())
        return;
    QString error;
    if (!m_docs.at(index)->save(&error))
        emit errorOccurred(error);
}