#pragma once

#include <QHash>
#include <QMutex>
#include <QRegularExpression>
#include <QString>
#include <QVector>
#include <optional>

// Правила .gitignore проекта. Файл .gitignore папки действует на всё под ней,
// более глубокие файлы и более поздние строки важнее, '!' возвращает путь.
// Правила папок читаются при первом обращении и кэшируются; потокобезопасно.
// Содержимое игнорируемой папки не проверяется — обходчики её просто пропускают.
class IgnoreRules {
public:
    explicit IgnoreRules(QString root = {});

    QString root() const { return m_root; }
    bool isIgnored(const QString &absolutePath, bool isDir) const;

    struct Rule {
        QRegularExpression re;
        bool negate = false;
        bool dirOnly = false;
    };
    static QVector<Rule> parse(const QString &gitignore);
    // nullopt — ни одно правило не подошло; иначе — игнорировать ли
    static std::optional<bool> match(const QVector<Rule> &rules, const QString &relativePath, bool isDir);

private:
    QVector<Rule> rulesFor(const QString &relativeDir) const;

    QString m_root;
    mutable QMutex m_mutex;
    mutable QHash<QString, QVector<Rule>> m_cache; // относительная папка -> её правила
};
