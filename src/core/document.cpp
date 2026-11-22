#include "document.h"

#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QPointer>
#include <QPromise>
#include <QThreadPool>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <limits>
#include <memory>
#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace core {

namespace {
constexpr qint64 kReadChunk = 1 << 20;   // шаг прогресса фоновой загрузки
constexpr qint64 kTypingMergeMs = 1000; // пауза, после которой ввод — новый шаг

// Заменить существующий файл temp'ом. POSIX rename заменяет сам (QFile::rename
// и на POSIX не заменяет существующий); на Windows — MoveFileExW.
bool renameReplace(const QString &from, const QString &to, QString *error)
{
#ifdef Q_OS_WIN
    if (MoveFileExW(reinterpret_cast<const wchar_t *>(from.utf16()),
                    reinterpret_cast<const wchar_t *>(to.utf16()),
                    MOVEFILE_REPLACE_EXISTING | MOVEFILE_COPY_ALLOWED))
        return true;
    if (error) {
        wchar_t message[256] = {};
        FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                       nullptr, GetLastError(), 0, message, 256, nullptr);
        *error = QString::fromWCharArray(message).trimmed();
    }
    return false;
#else
    if (::rename(QFile::encodeName(from).constData(), QFile::encodeName(to).constData()) == 0)
        return true;
    if (error)
        *error = QString::fromLocal8Bit(std::strerror(errno));
    return false;
#endif
}
} // namespace

qint64 Document::s_mappedThreshold = 256 << 20;

Document::Document(QObject *parent) : QObject(parent) {}

Document::~Document()
{
    // Поток сохранения завершится по отмене и уберёт temp
    cancelSave();
}

bool Document::load(const QString &path)
{
    if (!m_buffer.load(path))
        return false;
    m_filePath = path;
    resetHistory();
    return true;
}

bool Document::loadAsync(const QString &path)
{
    if (m_loading)
        return false;
    m_loading = true;
    m_filePath = path; // документ ещё не опубликован — DocumentManager ждёт loadFinished

    using LoadResult = std::shared_ptr<TextBuffer>;
    auto promise = std::make_shared<QPromise<LoadResult>>();
    promise->start();
    promise->setProgressRange(0, 100);
    auto *watcher = new QFutureWatcher<LoadResult>(this);
    connect(this, &QObject::destroyed, watcher, &QFutureWatcherBase::cancel);
    connect(watcher, &QFutureWatcherBase::progressValueChanged, this, &Document::loadProgress);
    connect(watcher, &QFutureWatcherBase::finished, this, [this, watcher] {
        const auto future = watcher->future();
        LoadResult result = future.resultCount() > 0 ? future.result() : nullptr;
        watcher->deleteLater();
        m_loading = false;
        if (result) {
            m_buffer = std::move(*result);
            resetHistory();
        }
        emit loadFinished(bool(result));
    });
    watcher->setFuture(promise->future());
    QThreadPool::globalInstance()->start([promise, path] {
        if (promise->isCanceled()) {
            promise->finish();
            return;
        }
        auto buffer = std::make_shared<TextBuffer>();
        if (QFileInfo(path).size() >= s_mappedThreshold
            && buffer->loadMapped(path, [&promise](int percent) { promise->setProgressValue(percent); })) {
            promise->addResult(buffer);
            promise->finish();
            return;
        }

        QFile file(path);
        if (file.open(QIODevice::ReadOnly) && !promise->isCanceled()) {
            const qint64 size = file.size();
            if (size < 0 || size > std::numeric_limits<int>::max()) {
                promise->finish();
                return;
            }
            QByteArray raw(size, Qt::Uninitialized);
            qsizetype done = 0;
            while (done < raw.size() && !promise->isCanceled()) {
                const qint64 n = file.read(raw.data() + done,
                                           qMin<qint64>(kReadChunk, raw.size() - done));
                if (n <= 0)
                    break;
                done += n;
                promise->setProgressValue(int(qint64(done) * 100 / raw.size()));
            }
            if (done == raw.size() && file.size() == size && !promise->isCanceled()
                && buffer->loadFromData(raw))
                promise->addResult(buffer);
        }
        promise->finish();
    });
    return true;
}

bool Document::save(QString *error)
{
    return !m_filePath.isEmpty() && saveAs(m_filePath, error);
}

bool Document::saveAsUtf8(QString *error)
{
    const Encoding previous = m_buffer.encoding();
    m_buffer.setEncoding(Encoding::Utf8);
    if (!save(error)) {
        m_buffer.setEncoding(previous);
        return false;
    }
    if (previous != Encoding::Utf8)
        emit fileFormatChanged();
    return true;
}

bool Document::saveWithLineEnding(LineEnding ending, QString *error)
{
    const LineEnding previous = m_buffer.lineEnding();
    m_buffer.setLineEnding(ending);
    if (!save(error)) {
        m_buffer.setLineEnding(previous);
        return false;
    }
    if (previous != ending)
        emit fileFormatChanged();
    return true;
}

bool Document::saveAs(const QString &path, QString *error)
{
    if (m_virtual) {
        if (error)
            *error = tr("Этот текст нельзя сохранить");
        return false;
    }
    if (m_buffer.isMapped()) {
        // Крупный файл: потоковое сохранение с фоновым записом temp
        const QString previous = m_filePath;
        const bool sameFile = (path == m_filePath);
        m_filePath = path;
        if (!saveAsyncAt(path, sameFile, error)) {
            m_filePath = previous;
            return false;
        }
        return true;
    }
    if (!m_buffer.save(path, error))
        return false;
    m_filePath = path;
    markClean();
    return true;
}

void Document::cancelSave()
{
    if (m_saveCancel)
        m_saveCancel->store(true);
    m_saveCancel.reset();
}

bool Document::saveAsync(QString *error)
{
    return saveAsyncAt(m_filePath, true, error);
}

bool Document::saveAsyncAt(const QString &target, bool sameFile, QString *error)
{
    if (!m_buffer.isMapped()) {
        if (error)
            *error = tr("Потоковое сохранение идёт только для файлов от 256 МБ");
        return false;
    }
    if (m_saving) {
        if (error)
            *error = tr("Сохранение уже идёт");
        return false;
    }
    if (m_virtual || m_filePath.isEmpty()) {
        if (error)
            *error = tr("Этот текст нельзя сохранить");
        return false;
    }

    const QString temp = target + QStringLiteral(".mca-save");
    const Encoding encoding = m_buffer.encoding();
    const LineEnding le = m_buffer.lineEnding();
    const bool bom = m_buffer.mappedBaseHasBom();
    const PieceTable::Snapshot snap = m_buffer.snapshot();
    const QPointer<Document> self(this);
    m_saveCancel = std::make_shared<std::atomic_bool>(false);
    const auto canceled = m_saveCancel;
    m_saving = true;
    emit saveStateChanged(true);
    emit saveProgress(0);

    const auto report = [self, canceled](int percent) {
        if (canceled->load() || self.isNull())
            return false;
        QMetaObject::invokeMethod(self, [self, percent] { emit self->saveProgress(percent); },
                                  Qt::QueuedConnection);
        return true;
    };

    QThreadPool::globalInstance()->start([self, snap, encoding, le, bom, target, temp, sameFile,
                                          canceled, report] {
        QString err;
        bool ok = streamSave(snap, encoding, le, bom, temp, report, &err);
        std::shared_ptr<const MappedText> newBase;
        // Скан записанного файла — для перенацеливания кусков базы после
        // переименования. Отказ в отображении прерывает сохранение: без
        // него у кусков не останется источника текста.
        if (ok)
            newBase = MappedText::open(temp, encoding == Encoding::Utf8,
                                       [canceled](int) { return !canceled->load(); });
        if (ok && !newBase && !canceled->load())
            err = QCoreApplication::translate("Document", "Не удалось отобразить сохранённый файл");
        if (!ok || canceled->load() || self.isNull()) {
            // Отмена, ошибка записи или документ уже уничтожен — temp не нужен
            QFile::remove(temp);
            if (self)
                QMetaObject::invokeMethod(self, [self, err, sameFile] {
                    self->finishSave(false, err, {}, {}, {}, sameFile);
                }, Qt::QueuedConnection);
            return;
        }
        QMetaObject::invokeMethod(self, [self, newBase, temp, target, sameFile] {
            self->finishSave(true, {}, newBase, temp, target, sameFile);
        }, Qt::QueuedConnection);
    });
    return true;
}

void Document::finishSave(bool ok, const QString &error, const MappedSharedPtr &newBase,
                          const QString &temp, const QString &target, bool sameFile)
{
    m_saveCancel.reset();
    m_saving = false;
    if (!ok) {
        emit saveStateChanged(false);
        emit saveFinished(false, error);
        return;
    }
    // Windows не переименует ни файл-получатель с активным отображением,
    // ни сам отображённый файл, поэтому снимаем оба отображения. Индексы
    // MappedText считаются по содержимому — содержимое при переименовании
    // не меняется, они остаются верными. Между retarget и reopenAs куски
    // недолго ссылаются на базу без отображения: весь блок выполняется в
    // GUI-потоке без обработки событий, а фоновые потребители читают снимки
    // старого отображения (см. ТЗ, принятый риск).
    if (sameFile)
        m_buffer.releaseMappedBase();
    if (newBase)
        newBase->releaseMapping();
    QString renameError;
    if (!renameReplace(temp, target, &renameError)) {
        if (sameFile)
            m_buffer.remapMappedBase();
        QFile::remove(temp);
        emit saveStateChanged(false);
        emit saveFinished(false, renameError);
        return;
    }
    m_buffer.retargetMappedBase(newBase);
    if (!m_buffer.reopenMappedBaseAs(target)) {
        // Худший случай: отобразить сохранённый файл не вышло — перечитываем с диска
        m_filePath = target;
        m_buffer.loadMapped(m_filePath);
        resetHistory();
        markClean();
        emit reloaded();
    } else {
        m_filePath = target;
        markClean();
    }
    emit saveStateChanged(false);
    emit saveFinished(true, {});
}

void Document::loadVirtual(TextBuffer buffer, const QString &path)
{
    m_buffer = std::move(buffer);
    m_filePath = path;
    m_virtual = true;
    resetHistory();
}

QString Document::displayName() const
{
    return QFileInfo(m_filePath).fileName();
}

QVector<Document::Position> Document::replace(const QVector<Replacement> &parts, EditKind kind,
                                              const Selections &before)
{
    if (m_saving) // правки заблокированы, пока файл записывается
        return QVector<Position>(parts.size());
    struct Item {
        int start;
        int end;
        QString text;
        int index; // порядок во входе
    };
    QVector<Item> items;
    items.reserve(parts.size());
    for (int i = 0; i < parts.size(); ++i) {
        const Replacement &p = parts.at(i);
        items.append({m_buffer.offsetOf(qMin(p.from, p.to)), m_buffer.offsetOf(qMax(p.from, p.to)),
                      TextBuffer::normalizeNewlines(p.text), i});
    }
    std::sort(items.begin(), items.end(), [](const Item &a, const Item &b) { return a.start < b.start; });
    for (int i = 1; i < items.size(); ++i) { // пересечения — ошибка вызывающего; не даём испортить текст
        items[i].start = qMax(items[i].start, items[i - 1].end);
        items[i].end = qMax(items[i].end, items[i].start);
    }

    // С конца к началу: смещения ещё не применённых правок остаются верными.
    // В том же порядке их повторяет redo, в обратном — откатывает undo.
    QVector<Change> changes;
    for (auto it = items.crbegin(); it != items.crend(); ++it) {
        const int count = it->end - it->start;
        if (count == 0 && it->text.isEmpty())
            continue;
        changes.append({it->start, m_buffer.textAt(it->start, count), it->text});
        apply(it->start, count, it->text);
    }

    QVector<Position> ends(parts.size());
    Selections after(parts.size());
    int delta = 0;
    for (const Item &item : std::as_const(items)) {
        const Position end = m_buffer.positionOf(item.start + delta + int(item.text.size()));
        delta += int(item.text.size()) - (item.end - item.start);
        ends[item.index] = end;
        after[item.index] = {end, end};
    }
    if (!changes.isEmpty())
        pushStep(std::move(changes), before, after, kind);
    flushEdits();
    return ends;
}

void Document::replayEdits(const QVector<RawEdit> &edits)
{
    QVector<Change> changes;
    for (const RawEdit &e : edits) {
        const int offset = qBound(0, e.offset, m_buffer.length());
        const int count = qBound(0, e.count, m_buffer.length() - offset);
        changes.append({offset, m_buffer.textAt(offset, count), e.text});
        apply(offset, count, e.text);
    }
    if (!changes.isEmpty())
        pushStep(std::move(changes), {}, {}, EditKind::Other);
    flushEdits();
}

void Document::apply(int offset, int count, const QString &text)
{
    m_buffer.replace(offset, count, text);
    ++m_revision;
    emit edited(offset, count, text);
}

void Document::flushEdits()
{
    bool overflow = false;
    const QVector<TextBuffer::Edit> edits = m_buffer.takeEdits(&overflow);
    if (!edits.isEmpty() || overflow)
        emit changed(edits, overflow);
}

void Document::pushStep(QVector<Change> changes, const Selections &before, const Selections &after,
                        EditKind kind)
{
    const bool merge = kind == EditKind::Typing && m_groupOpen && m_index > 0
                       && m_index == m_history.size()
                       && m_history.at(m_index - 1).kind == EditKind::Typing
                       && m_lastTyping.elapsed() < kTypingMergeMs;
    if (merge) {
        Step &step = m_history[m_index - 1];
        step.changes += changes;
        step.after = after;
    } else {
        m_history.resize(m_index); // новая правка отрезает ветку redo
        if (m_cleanIndex > m_index)
            m_cleanIndex = -1;
        m_history.append({std::move(changes), before, after, kind});
        ++m_index;
    }
    m_groupOpen = kind == EditKind::Typing;
    if (m_groupOpen)
        m_lastTyping.start();
    updateDirty();
}

void Document::setLastStepSelections(const Selections &after)
{
    if (m_index > 0 && m_index == m_history.size())
        m_history[m_index - 1].after = after;
}

bool Document::undo(Selections *selections)
{
    if (!canUndo())
        return false;
    const Step &step = m_history.at(--m_index);
    for (auto it = step.changes.crbegin(); it != step.changes.crend(); ++it)
        apply(it->offset, int(it->inserted.size()), it->removed);
    *selections = step.before;
    m_groupOpen = false;
    updateDirty();
    flushEdits();
    return true;
}

bool Document::redo(Selections *selections)
{
    if (!canRedo())
        return false;
    const Step &step = m_history.at(m_index++);
    for (const Change &c : step.changes)
        apply(c.offset, int(c.removed.size()), c.inserted);
    *selections = step.after;
    m_groupOpen = false;
    updateDirty();
    flushEdits();
    return true;
}

void Document::resetHistory()
{
    ++m_revision;
    m_history.clear();
    m_index = 0;
    m_groupOpen = false;
    markClean();
}

bool Document::changedOnDisk() const
{
    const QFileInfo info(m_filePath);
    return info.exists() && (info.lastModified() != m_diskTime || info.size() != m_diskSize);
}

bool Document::reload()
{
    if (m_saving)
        return false;
    const bool ok = m_buffer.isMapped() ? m_buffer.loadMapped(m_filePath) : m_buffer.load(m_filePath);
    if (!ok)
        return false;
    resetHistory();
    emit reloaded();
    return true;
}

void Document::acceptDiskVersion()
{
    stampDisk();
}

void Document::stampDisk()
{
    const QFileInfo info(m_filePath);
    m_diskTime = info.lastModified();
    m_diskSize = info.size();
}

void Document::markClean()
{
    stampDisk();
    m_cleanIndex = m_index;
    m_groupOpen = false; // ввод после сохранения — отдельный шаг
    updateDirty();
}

void Document::updateDirty()
{
    const bool dirty = m_index != m_cleanIndex;
    if (dirty == m_dirty)
        return;
    m_dirty = dirty;
    emit dirtyChanged(m_dirty);
}

} // namespace core
