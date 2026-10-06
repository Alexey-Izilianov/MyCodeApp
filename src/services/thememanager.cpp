#include "thememanager.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>

#include "appsettings.h"
#include "palette.h"

namespace {

const QString kUserPrefix = QStringLiteral("user:");
const QStringList kBuiltin = {QStringLiteral("dark"), QStringLiteral("light")};

QJsonObject readTheme(const QString &path, QString *error = nullptr)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error)
            *error = QObject::tr("Тема не найдена: %1").arg(path);
        return {};
    }
    QJsonParseError parse;
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parse);
    if (parse.error != QJsonParseError::NoError || !doc.isObject()) {
        if (error)
            *error = QObject::tr("Тема %1: %2 (символ %3)")
                         .arg(QFileInfo(path).fileName(), parse.errorString())
                         .arg(parse.offset);
        return {};
    }
    return doc.object();
}

// Разделы ui/editor/syntax темы поверх тех же разделов базовой
QJsonObject merged(QJsonObject base, const QJsonObject &theme)
{
    for (const char *section : {"ui", "editor", "syntax"}) {
        QJsonObject target = base.value(QLatin1String(section)).toObject();
        const QJsonObject source = theme.value(QLatin1String(section)).toObject();
        for (auto it = source.begin(); it != source.end(); ++it)
            target.insert(it.key(), it.value());
        base.insert(QLatin1String(section), target);
    }
    for (const char *key : {"name", "base"})
        if (theme.contains(QLatin1String(key)))
            base.insert(QLatin1String(key), theme.value(QLatin1String(key)));
    return base;
}

void read(const QJsonObject &colors, const char *key, QRgb &out)
{
    const QColor color(colors.value(QLatin1String(key)).toString());
    if (color.isValid())
        out = color.rgb();
}

EditorColors editorColors(const QJsonObject &e)
{
    EditorColors c;
    read(e, "background", c.background);
    read(e, "text", c.text);
    read(e, "currentLine", c.currentLine);
    read(e, "selection", c.selection);
    read(e, "caret", c.caret);
    read(e, "match", c.match);
    read(e, "currentMatch", c.currentMatch);
    read(e, "bracket", c.bracket);
    read(e, "gutterText", c.gutterText);
    read(e, "gutterCurrent", c.gutterCurrent);
    read(e, "foldPlaceholder", c.foldPlaceholder);
    read(e, "changeAdded", c.changeAdded);
    read(e, "changeModified", c.changeModified);
    read(e, "changeRemoved", c.changeRemoved);
    read(e, "diffInserted", c.diffInserted);
    read(e, "diffDeleted", c.diffDeleted);
    read(e, "error", c.diagnostics[0]);
    read(e, "warning", c.diagnostics[1]);
    read(e, "info", c.diagnostics[2]);
    return c;
}

} // namespace

ThemeManager::ThemeManager(QObject *parent)
    : QObject(parent)
{
    m_reload.setSingleShot(true);
    m_reload.setInterval(150);
    connect(&m_reload, &QTimer::timeout, this, [this] {
        QString error;
        if (!apply(m_current, &error))
            emit message(error); // старые цвета остаются, пока файл не исправят
    });
    connect(&m_watcher, &QFileSystemWatcher::fileChanged, &m_reload, qOverload<>(&QTimer::start));
    connect(&m_watcher, &QFileSystemWatcher::directoryChanged, this, [this] {
        scan();
        if (m_current.startsWith(kUserPrefix))
            m_reload.start();
    });

    QDir().mkpath(userDir());
    m_watcher.addPath(userDir());
    scan();
    if (!apply(appSetting(QStringLiteral("theme"), QStringLiteral("dark")).toString(), nullptr))
        apply(QStringLiteral("dark"), nullptr);
}

QString ThemeManager::userDir()
{
    return appDataDir() + QStringLiteral("/themes");
}

QString ThemeManager::pathOf(const QString &id) const
{
    if (id.startsWith(kUserPrefix))
        return userDir() + u'/' + id.mid(kUserPrefix.size()) + QStringLiteral(".json");
    return QStringLiteral(":/assets/themes/%1.json").arg(id);
}

void ThemeManager::scan()
{
    QVariantList themes;
    auto add = [&](const QString &id, bool user) {
        const QJsonObject theme = readTheme(pathOf(id));
        const QString name = theme.value(QStringLiteral("name")).toString();
        themes.append(QVariantMap{{QStringLiteral("id"), id},
                                  {QStringLiteral("name"), name.isEmpty() ? id.mid(id.indexOf(u':') + 1) : name},
                                  {QStringLiteral("nameEn"), theme.value(QStringLiteral("nameEn")).toString()},
                                  {QStringLiteral("user"), user}});
    };
    for (const QString &id : kBuiltin)
        add(id, false);
    const QStringList files = QDir(userDir()).entryList({QStringLiteral("*.json")}, QDir::Files, QDir::Name);
    for (const QString &file : files)
        add(kUserPrefix + QFileInfo(file).completeBaseName(), true);
    if (themes != m_themes) {
        m_themes = themes;
        emit themesChanged();
    }
}

bool ThemeManager::apply(const QString &id, QString *error)
{
    const QJsonObject theme = readTheme(pathOf(id), error);
    if (theme.isEmpty())
        return false;
    const QString base = theme.value(QStringLiteral("base")).toString() == QStringLiteral("light")
                             ? QStringLiteral("light") : QStringLiteral("dark");
    const QJsonObject full = kBuiltin.contains(id) ? theme : merged(readTheme(pathOf(base)), theme);

    QHash<QString, QColor> syntax;
    const QJsonObject syntaxColors = full.value(QStringLiteral("syntax")).toObject();
    for (auto it = syntaxColors.begin(); it != syntaxColors.end(); ++it)
        if (const QColor color(it.value().toString()); color.isValid())
            syntax.insert(it.key(), color);

    m_ui = full.value(QStringLiteral("ui")).toObject().toVariantMap();
    m_current = id;
    Palette::instance().set(editorColors(full.value(QStringLiteral("editor")).toObject()), syntax);
    watchCurrent();
    emit changed();
    return true;
}

void ThemeManager::watchCurrent()
{
    if (!m_watcher.files().isEmpty())
        m_watcher.removePaths(m_watcher.files());
    // Сохранение через замену файла снимает слежение — путь добавляется заново
    if (m_current.startsWith(kUserPrefix) && QFileInfo::exists(pathOf(m_current)))
        m_watcher.addPath(pathOf(m_current));
}

void ThemeManager::setTheme(const QString &id)
{
    QString error;
    if (apply(id, &error))
        setAppSetting(QStringLiteral("theme"), id);
    else
        emit message(error);
}

QString ThemeManager::createUserTheme()
{
    const QJsonObject current = readTheme(pathOf(m_current));
    const QString base = current.value(QStringLiteral("base")).toString() == QStringLiteral("light")
                                 || m_current == QStringLiteral("light")
                             ? QStringLiteral("light") : QStringLiteral("dark");
    QJsonObject theme = merged(readTheme(pathOf(base)), current);
    int n = 1;
    while (QFileInfo::exists(userDir() + QStringLiteral("/my-theme-%1.json").arg(n)))
        ++n;
    theme.insert(QStringLiteral("name"), tr("Моя тема %1").arg(n));
    theme.insert(QStringLiteral("base"), base);
    const QString path = userDir() + QStringLiteral("/my-theme-%1.json").arg(n);
    if (!writeJsonFile(path, theme)) {
        emit message(tr("Не удалось создать %1").arg(path));
        return {};
    }
    scan();
    setTheme(kUserPrefix + QStringLiteral("my-theme-%1").arg(n));
    return path;
}
