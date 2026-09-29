#include "document.h"

#include <QFile>
#include <QFileInfo>
#include <QThreadPool>

namespace core {

namespace {
constexpr qint64 kReadChunk = 1 << 20;   // шаг прогресса фоновой загрузки
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
                    QMetaObject::invokeMethod(this, [this, percent] { emit loadProgress(percent); },
                                              Qt::QueuedConnection);
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
    if (!m_buffer.save(path, error))
        return false;
    m_filePath = path;
    markClean();
    return true;
}

QString Document::displayName() const
{
    return QFileInfo(m_filePath).fileName();
}

Document::Position Document::replace(Position from, Position to, const QString &text,
                                     EditKind kind, const Selection &before)
{
    if (to < from)
        std::swap(from, to);
    const int offset = m_buffer.offsetOf(from);
    Change change{offset, m_buffer.text(from, to), TextBuffer::normalizeNewlines(text)};
    if (change.removed.isEmpty() && change.inserted.isEmpty())
        return m_buffer.positionOf(offset);

    m_buffer.replace(offset, int(change.removed.size()), change.inserted);
    const Position end = m_buffer.positionOf(offset + int(change.inserted.size()));

    const bool merge = kind == EditKind::Typing && m_groupOpen && m_index > 0
                       && m_index == m_history.size()
                       && m_history.at(m_index - 1).kind == EditKind::Typing
                       && m_lastTyping.elapsed() < kTypingMergeMs;
    if (merge) {
        Step &step = m_history[m_index - 1];
        step.changes.append(std::move(change));
        step.after = {end, end};
    } else {
        m_history.resize(m_index); // новая правка отрезает ветку redo
        if (m_cleanIndex > m_index)
            m_cleanIndex = -1;
        m_history.append({{std::move(change)}, before, {end, end}, kind});
        ++m_index;
    }
    m_groupOpen = kind == EditKind::Typing;
    if (m_groupOpen)
        m_lastTyping.start();
    updateDirty();
    return end;
}

bool Document::undo(Selection *selection)
{
    if (!canUndo())
        return false;
    const Step &step = m_history.at(--m_index);
    for (auto it = step.changes.crbegin(); it != step.changes.crend(); ++it)
        m_buffer.replace(it->offset, int(it->inserted.size()), it->removed);
    *selection = step.before;
    m_groupOpen = false;
    updateDirty();
    return true;
}

bool Document::redo(Selection *selection)
{
    if (!canRedo())
        return false;
    const Step &step = m_history.at(m_index++);
    for (const Change &c : step.changes)
        m_buffer.replace(c.offset, int(c.removed.size()), c.inserted);
    *selection = step.after;
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

void Document::markClean()
{
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
