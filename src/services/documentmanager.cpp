#include "documentmanager.h"

#include <QFileInfo>
#include <spdlog/spdlog.h>

#include "src/services/recovery.h"

using core::Document;

DocumentManager::DocumentManager(QObject *parent)
    : QObject(parent)
    , m_recovery(new RecoveryManager(RecoveryManager::defaultDirectory(), this))
{
    m_diskCheck.setInterval(2000);
    connect(&m_diskCheck, &QTimer::timeout, this, &DocumentManager::checkDisk);
    m_diskCheck.start();
    connect(&m_watcher, &QFileSystemWatcher::fileChanged, this, [this] {
        QTimer::singleShot(200, this, &DocumentManager::checkDisk); // дать записи закончиться
    });
}

void DocumentManager::checkDisk()
{
    for (int i = 0; i < m_docs.size(); ++i) {
        Document *doc = m_docs.at(i);
        if (!m_watcher.files().contains(doc->filePath()))
            m_watcher.addPath(doc->filePath());
        if (m_conflicts.contains(doc) || !doc->changedOnDisk())
            continue;
        if (doc->isDirty()) {
            m_conflicts.insert(doc);
            emit externalChange(i, doc->displayName());
        } else if (!doc->reload()) {
            doc->acceptDiskVersion(); // файл недоступен на чтение — не спрашиваем каждые 2 с
        }
    }
}

void DocumentManager::reloadFromDisk(int index)
{
    if (index < 0 || index >= m_docs.size())
        return;
    m_conflicts.remove(m_docs.at(index));
    if (!m_docs.at(index)->reload())
        emit errorOccurred(QStringLiteral("не удалось перечитать файл"));
}

void DocumentManager::keepLocal(int index)
{
    if (index < 0 || index >= m_docs.size())
        return;
    m_conflicts.remove(m_docs.at(index));
    m_docs.at(index)->acceptDiskVersion();
}

int DocumentManager::restoreUnsaved()
{
    int restored = 0;
    for (const QString &path : m_recovery->unsavedFiles()) {
        open(QUrl::fromLocalFile(path));
        ++restored;
    }
    if (restored > 0)
        spdlog::info("dm: восстановлено файлов с несохранёнными правками: {}", restored);
    return restored;
}

void DocumentManager::publish(Document *doc)
{
    connect(doc, &Document::dirtyChanged, this, &DocumentManager::tabsChanged);
    m_recovery->watch(doc);
    m_recovery->restore(doc);
    m_docs.append(doc);
    m_current = int(m_docs.size()) - 1;
    emit tabsChanged();
    emit currentIndexChanged();
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
    publish(doc);
    return m_current;
}

void DocumentManager::finishAsyncLoad(core::Document *doc, bool ok)
{
    m_loadingDocs.removeOne(doc);
    if (ok) {
        spdlog::info("dm: фоновая загрузка {} мс: {}",
                     m_loadTimer.elapsed(), doc->filePath().toStdString());
        publish(doc);
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
    m_recovery->discard(doc); // закрыт осознанно — несохранённое не восстанавливаем
    m_conflicts.remove(doc);
    m_watcher.removePath(doc->filePath());
    if (m_current >= m_docs.size())
        m_current = m_docs.size() - 1;
    // Сначала уведомляем (редактор отцепит документ), потом удаляем
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

QObject *DocumentManager::documentAt(int index) const
{
    return index >= 0 && index < m_docs.size() ? m_docs.at(index) : nullptr;
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