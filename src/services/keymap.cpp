#include "keymap.h"

#include <QFileInfo>
#include <QJsonObject>
#include <QKeySequence>
#include <algorithm>

#include "appsettings.h"

namespace {

QString normalized(const QString &shortcut)
{
    return QKeySequence(shortcut, QKeySequence::PortableText).toString(QKeySequence::PortableText);
}

// Скан-код клавиши (раскладка ПК) -> клавиша английской раскладки без Shift
int keyFromScanCode(quint32 scanCode)
{
    static const char rows[][14] = {
        "1234567890-=",   // 0x02..
        "qwertyuiop[]",   // 0x10..
        "asdfghjkl;'`",   // 0x1E..
        "\\zxcvbnm,./",   // 0x2B..
    };
    static const quint32 starts[] = {0x02, 0x10, 0x1E, 0x2B};
    for (int r = 0; r < 4; ++r) {
        const quint32 offset = scanCode - starts[r];
        if (scanCode >= starts[r] && offset < qstrlen(rows[r]))
            return QChar::toUpper(char16_t(rows[r][offset])); // Qt::Key_A == 'A', Key_BracketLeft == '['
    }
    return 0;
}

} // namespace

Keymap::Keymap(QObject *parent)
    : QObject(parent)
{
    load();
    connect(&m_watcher, &QFileSystemWatcher::fileChanged, this, [this] {
        load();
        ++m_revision;
        emit changed();
    });
}

QString Keymap::path()
{
    return appDataDir() + QStringLiteral("/keybindings.json");
}

void Keymap::load()
{
    m_overrides.clear();
    const QJsonObject json = readJsonFile(path());
    for (auto it = json.begin(); it != json.end(); ++it)
        m_overrides.insert(it.key(), normalized(it.value().toString()));
    // Сохранение заменой файла снимает слежение — добавляем заново
    if (QFileInfo::exists(path()) && !m_watcher.files().contains(path()))
        m_watcher.addPath(path());
}

void Keymap::save()
{
    QJsonObject json;
    for (auto it = m_overrides.cbegin(); it != m_overrides.cend(); ++it)
        json.insert(it.key(), it.value());
    m_watcher.blockSignals(true); // своя запись — не перечитывать
    writeJsonFile(path(), json);
    m_watcher.blockSignals(false);
    if (!m_watcher.files().contains(path()))
        m_watcher.addPath(path());
    ++m_revision;
    emit changed();
}

QString Keymap::effective(const QString &id) const
{
    const auto it = m_overrides.constFind(id);
    return it != m_overrides.cend() ? *it : normalized(m_commands.value(id).defaultShortcut);
}

QVariantList Keymap::commands() const
{
    QStringList ids = m_commands.keys();
    std::sort(ids.begin(), ids.end(), [this](const QString &a, const QString &b) {
        return m_commands.value(a).title.localeAwareCompare(m_commands.value(b).title) < 0;
    });
    QVariantList list;
    for (const QString &id : std::as_const(ids)) {
        const Command &c = m_commands[id];
        list.append(QVariantMap{{QStringLiteral("id"), id},
                                {QStringLiteral("title"), c.title},
                                {QStringLiteral("shortcut"), effective(id)},
                                {QStringLiteral("defaultShortcut"), normalized(c.defaultShortcut)},
                                {QStringLiteral("custom"), m_overrides.contains(id)}});
    }
    return list;
}

void Keymap::declare(const QString &id, const QString &title, const QString &defaultShortcut)
{
    m_commands.insert(id, {title, defaultShortcut});
}

QString Keymap::sequence(const QString &id, const QString &defaultShortcut) const
{
    const auto it = m_overrides.constFind(id);
    return it != m_overrides.cend() ? *it : defaultShortcut;
}

QString Keymap::assign(const QString &id, const QString &shortcut)
{
    const QString value = normalized(shortcut);
    QString takenFrom;
    if (!value.isEmpty()) {
        for (auto it = m_commands.cbegin(); it != m_commands.cend(); ++it) {
            if (it.key() != id && effective(it.key()) == value) {
                m_overrides.insert(it.key(), QString());
                takenFrom = it->title;
            }
        }
    }
    if (value == normalized(m_commands.value(id).defaultShortcut))
        m_overrides.remove(id);
    else
        m_overrides.insert(id, value);
    save();
    return takenFrom;
}

void Keymap::reset(const QString &id)
{
    if (m_overrides.remove(id))
        save();
}

void Keymap::resetAll()
{
    m_overrides.clear();
    save();
}

QString Keymap::shortcutFromKey(int key, int modifiers, quint32 nativeScanCode) const
{
    switch (key) {
    case Qt::Key_Control: case Qt::Key_Shift: case Qt::Key_Alt: case Qt::Key_Meta:
    case Qt::Key_AltGr: case 0: case Qt::Key_unknown:
        return {}; // одни модификаторы — ждём основную клавишу
    case Qt::Key_Backtab:
        key = Qt::Key_Tab;
        break;
    default:
        if (const int physical = keyFromScanCode(nativeScanCode))
            key = physical;
        break;
    }
    const auto mods = Qt::KeyboardModifiers(modifiers) & ~Qt::KeypadModifier;
    return QKeySequence(QKeyCombination(mods, Qt::Key(key))).toString(QKeySequence::PortableText);
}
