#include "documentmanager.h"

#include <QDir>
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
        emit errorOccurred(tr("Не удалось перечитать файл"));
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

void DocumentManager::publish(Document *doc, bool restoreJournal)
{
    connect(doc, &Document::dirtyChanged, this, &DocumentManager::tabsChanged);
    m_recovery->watch(doc);
    if (restoreJournal)
        m_recovery->restore(doc);
    m_docs.append(doc);
    m_current = int(m_docs.size()) - 1;
    emit tabsChanged();
    emit currentIndexChanged();
}

QObject *DocumentManager::takeDocument(int index)
{
    if (index < 0 || index >= m_docs.size())
        return nullptr;
    Document *doc = m_docs.takeAt(index);
    doc->setParent(nullptr); // владелец теперь другой менеджер (или его окно)
    m_recovery->unwatch(doc); // журнал переедет вместе с новым владельцем
    m_conflicts.remove(doc);
    m_watcher.removePath(doc->filePath());
    disconnect(doc, &Document::dirtyChanged, this, &DocumentManager::tabsChanged);
    if (index < m_current)
        --m_current; // забрали вкладку левее текущей — та же вкладка теперь левее
    if (m_current >= m_docs.size())
        m_current = m_docs.size() - 1;
    emit tabsChanged();
    emit currentIndexChanged();
    return doc;
}

void DocumentManager::adopt(QObject *document)
{
    Document *doc = qobject_cast<Document *>(document);
    if (!doc || m_docs.contains(doc))
        return;
    doc->setParent(this); // умирает вместе с этим менеджером (окном)
    // Правки уже в документе — журнал не проигрываем, только продолжаем
    publish(doc, false);
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

QString DocumentManager::pathKey(const QString &path)
{
    if (path.isEmpty())
        return {};
    const QFileInfo info(path);
    const QString canonical = info.canonicalFilePath();
    const QString clean = QDir::cleanPath(canonical.isEmpty() ? info.absoluteFilePath() : canonical);
#ifdef Q_OS_WIN
    return clean.toLower();
#else
    return clean;
#endif
}

Document *DocumentManager::find(const QString &path) const
{
    const QString wanted = pathKey(path);
    for (Document *doc : m_docs)
        if (pathKey(doc->filePath()) == wanted)
            return doc;
    return nullptr;
}

QString DocumentManager::pendingOpenKey(const QString &path) const
{
    const QString key = pathKey(path);
    for (const Document *pending : m_loadingDocs)
        if (pathKey(pending->filePath()) == key)
            return key;
    return {};
}

int DocumentManager::open(const QUrl &url)
{
    const QString requestedPath = url.isLocalFile() ? url.toLocalFile() : url.toString();
    if (requestedPath.isEmpty())
        return -1;

    const QString path = QFileInfo(requestedPath).absoluteFilePath();
    if (Document *existing = find(path)) {
        const int index = m_docs.indexOf(existing);
        activate(index);
        return index;
    }
    if (!pendingOpenKey(path).isEmpty())
        return -1;

    auto *doc = new Document(this);
    if (QFileInfo(path).size() > kAsyncLoadThreshold) {
        // Большой файл: грузим в фоне, в табы опубликуем по loadFinished.
        m_loadingDocs.append(doc);
        connect(doc, &Document::loadProgress, this, [this](int percent) {
            m_loadPercent = percent;
            emit loadPercentChanged();
        });
        connect(doc, &Document::loadFinished, this,
                [this, doc, key = pathKey(path)](bool ok) { finishAsyncLoad(doc, key, ok); });
        m_loadTimer.start();
        if (!doc->loadAsync(path)) {
            m_loadingDocs.removeOne(doc);
            delete doc;
            emit errorOccurred(tr("Не удалось открыть файл"));
            return -1;
        }
        emit loadingChanged();
        return -1; // таб появится по окончании загрузки
    }

    if (!doc->load(path)) {
        delete doc;
        emit errorOccurred(tr("Не удалось открыть файл"));
        return -1;
    }
    publish(doc);
    return m_current;
}

void DocumentManager::finishAsyncLoad(core::Document *doc, const QString &key, bool ok)
{
    m_loadingDocs.removeOne(doc);
    if (ok) {
        spdlog::info("dm: фоновая загрузка {} мс: {}",
                     m_loadTimer.elapsed(), doc->filePath().toStdString());
        publish(doc);
    } else {
        doc->deleteLater();
        emit errorOccurred(tr("Не удалось открыть файл"));
    }
    if (!isLoading())
        emit loadingChanged();
    emit asyncOpenFinished(key, ok ? doc : nullptr, ok ? m_docs.indexOf(doc) : -1);
}

void DocumentManager::close(int index)
{
    if (index < 0 || index >= m_docs.size())
        return;
    core::Document *doc = m_docs.at(index);
    // Во время потокового сохранения крупного файла вкладку закрывать нельзя:
    // отмена обрежет сохранение незавершённым
    if (doc->isSaving()) {
        emit errorOccurred(tr("Подождите: файл ещё сохраняется"));
        return;
    }
    m_docs.takeAt(index);
    m_recovery->discard(doc); // закрыт осознанно — несохранённое не восстанавливаем
    m_conflicts.remove(doc);
    m_watcher.removePath(doc->filePath());
    if (index < m_current || m_current >= m_docs.size())
        --m_current; // закрыли вкладку левее текущей (или саму последнюю) — текущая та же
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

void DocumentManager::closeDocument(QObject *document)
{
    close(indexOfDocument(document));
}

int DocumentManager::indexOfDocument(QObject *document) const
{
    return m_docs.indexOf(qobject_cast<Document *>(document));
}

void DocumentManager::saveAndClose(QObject *document)
{
    Document *doc = qobject_cast<Document *>(document);
    if (!doc || !m_docs.contains(doc))
        return;
    if (doc->isSaving()) {
        emit errorOccurred(tr("Подождите: файл ещё сохраняется"));
        return;
    }
    // Крупный файл сохраняется в фоне — вкладка закрывается по завершении,
    // пока сохранение идёт, её нельзя закрыть
    if (doc->buffer().isMapped()) {
        const int index = m_docs.indexOf(doc);
        const auto conn = connect(doc, &Document::saveFinished, doc,
                [this, doc](bool ok, const QString &error) {
                    if (!ok)
                        emit errorOccurred(error);
                    else if (m_docs.contains(doc))
                        closeDocument(doc);
                }, Qt::SingleShotConnection);
        if (!save(index))
            disconnect(conn);
        return; // при успехе вкладку закроет saveFinished
    }
    if (save(m_docs.indexOf(doc)))
        closeDocument(doc);
}

bool DocumentManager::save(int index)
{
    if (index < 0 || index >= m_docs.size())
        return false;
    QString error;
    if (m_docs.at(index)->save(&error))
        return true;
    emit errorOccurred(error);
    return false;
}
