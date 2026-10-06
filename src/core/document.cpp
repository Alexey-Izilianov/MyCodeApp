#include "document.h"

#include <QFile>
#include <QFileInfo>
#include <QThreadPool>

namespace core {

namespace {
constexpr qint64 kReadChunk = 1 << 20;   // шаг прогресса фоновой загрузки
constexpr qint64 kMappedSize = 256 << 20; // от этого размера файл не читается целиком, а отображается
constexpr qint64 kTypingMergeMs = 1000; // пауза, после которой ввод — новый шаг
} // namespace

Document::Document(QObject *parent) : QObject(parent) {}

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
    if (m_loading.fetchAndStoreRelaxed(true))
        return false;
    m_filePath = path; // документ ещё не опубликован — DocumentManager ждёт loadFinished

    QThreadPool::globalInstance()->start([this, path] {
        auto reportProgress = [this](int percent) {
            QMetaObject::invokeMethod(this, [this, percent] { emit loadProgress(percent); },
                                      Qt::QueuedConnection);
        };
        if (QFileInfo(path).size() >= kMappedSize && m_buffer.loadMapped(path, reportProgress)) {
            QMetaObject::invokeMethod(this, [this] {
                m_loading.storeRelaxed(false);
                resetHistory();
                emit loadFinished(true);
            }, Qt::QueuedConnection);
            return;
        }

        QFile file(path);
        bool ok = file.open(QIODevice::ReadOnly);
        QByteArray raw;
        if (ok) {
            raw.resize(file.size());
            qint64 done = 0;
            int lastPercent = -1;
            while (done < raw.size()) {
                const qint64 n = file.read(raw.data() + done, qMin(kReadChunk, raw.size() - done));
                if (n <= 0)
                    break;
                done += n;
                const int percent = int(done * 100 / raw.size());
                if (percent != lastPercent) {
                    lastPercent = percent;
                    reportProgress(percent);
                }
            }
            raw.resize(done);
            // Буфер пока никому не виден — декодируем прямо здесь, в фоне
            ok = m_buffer.loadFromData(raw);
        }
        QMetaObject::invokeMethod(this, [this, ok] {
            m_loading.storeRelaxed(false);
            if (ok)
                resetHistory();
            emit loadFinished(ok);
        }, Qt::QueuedConnection);
    });
    return true;
}

bool Document::save(QString *error)
{
    return !m_filePath.isEmpty() && saveAs(m_filePath, error);
}

bool Document::saveAs(const QString &path, QString *error)
{
    if (m_virtual) {
        if (error)
            *error = tr("Этот текст нельзя сохранить");
        return false;
    }
    if (!m_buffer.save(path, error))
        return false;
    m_filePath = path;
    markClean();
    return true;
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
}

void Document::apply(int offset, int count, const QString &text)
{
    m_buffer.replace(offset, count, text);
    emit edited(offset, count, text);
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
    return true;
}

void Document::resetHistory()
{
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
