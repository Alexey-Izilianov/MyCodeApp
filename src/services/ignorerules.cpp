#include "ignorerules.h"

#include <QDir>
#include <QFile>

namespace {

QString globToRegex(const QString &glob)
{
    QString re;
    for (int i = 0; i < glob.size(); ++i) {
        const QChar c = glob.at(i);
        if (glob.mid(i, 3) == QLatin1String("**/")) {
            re += QStringLiteral("(?:.*/)?");
            i += 2;
        } else if (glob.mid(i, 2) == QLatin1String("**")) {
            re += QStringLiteral(".*");
            ++i;
        } else if (c == u'*') {
            re += QStringLiteral("[^/]*");
        } else if (c == u'?') {
            re += QStringLiteral("[^/]");
        } else if (c == u'[') {
            const int close = int(glob.indexOf(u']', i + 1));
            if (close < 0) {
                re += QStringLiteral("\\[");
            } else {
                QString set = glob.mid(i + 1, close - i - 1);
                if (set.startsWith(u'!'))
                    set[0] = u'^';
                re += u'[' + set + u']';
                i = close;
            }
        } else if (c == u'\\' && i + 1 < glob.size()) {
            re += QRegularExpression::escape(glob.at(++i));
        } else {
            re += QRegularExpression::escape(c);
        }
    }
    return re;
}

} // namespace

IgnoreRules::IgnoreRules(QString root)
    : m_root(QDir::cleanPath(root))
{
}

QVector<IgnoreRules::Rule> IgnoreRules::parse(const QString &gitignore)
{
    QVector<Rule> rules;
    for (QString line : gitignore.split(u'\n')) {
        if (line.endsWith(u'\r'))
            line.chop(1);
        while (line.endsWith(u' ') && !line.endsWith(QLatin1String("\\ ")))
            line.chop(1);
        if (line.isEmpty() || line.startsWith(u'#'))
            continue;

        Rule rule;
        if (line.startsWith(u'!')) {
            rule.negate = true;
            line.remove(0, 1);
        }
        if (line.endsWith(u'/')) {
            rule.dirOnly = true;
            line.chop(1);
        }
        // Паттерн со слэшем в середине или начале привязан к папке .gitignore,
        // без слэша — совпадает с именем на любой глубине
        const bool anchored = line.contains(u'/');
        if (line.startsWith(u'/'))
            line.remove(0, 1);
        if (line.isEmpty())
            continue;
        rule.re.setPattern(u'^' + (anchored ? QString() : QStringLiteral("(?:.*/)?"))
                           + globToRegex(line) + u'$');
        rules.append(rule);
    }
    return rules;
}

std::optional<bool> IgnoreRules::match(const QVector<Rule> &rules, const QString &relativePath,
                                       bool isDir)
{
    for (auto it = rules.crbegin(); it != rules.crend(); ++it)
        if ((!it->dirOnly || isDir) && it->re.match(relativePath).hasMatch())
            return !it->negate;
    return std::nullopt;
}

QVector<IgnoreRules::Rule> IgnoreRules::rulesFor(const QString &relativeDir) const
{
    QMutexLocker lock(&m_mutex);
    auto it = m_cache.find(relativeDir);
    if (it == m_cache.end()) {
        const QString dir = relativeDir.isEmpty() ? m_root : m_root + u'/' + relativeDir;
        QFile file(dir + QStringLiteral("/.gitignore"));
        QByteArray text;
        if (file.open(QIODevice::ReadOnly))
            text = file.readAll();
        if (relativeDir.isEmpty()) { // локальные исключения репозитория — как корневой .gitignore
            QFile exclude(m_root + QStringLiteral("/.git/info/exclude"));
            if (exclude.open(QIODevice::ReadOnly))
                text = exclude.readAll() + '\n' + text;
        }
        const QVector<Rule> rules = parse(QString::fromUtf8(text));
        it = m_cache.insert(relativeDir, rules);
    }
    return *it;
}

bool IgnoreRules::isIgnored(const QString &absolutePath, bool isDir) const
{
    const QString relative = QDir(m_root).relativeFilePath(absolutePath);
    if (relative.startsWith(QLatin1String("..")))
        return false;
    if (relative == QLatin1String(".git") || relative.endsWith(QLatin1String("/.git")))
        return true;

    // От корня вглубь: правила более глубокой папки перекрывают внешние
    std::optional<bool> ignored;
    QString dir;
    const QStringList parts = relative.split(u'/');
    for (int i = 0; i < parts.size(); ++i) {
        if (const auto m = match(rulesFor(dir), QStringList(parts.mid(i)).join(u'/'), isDir))
            ignored = m;
        dir = dir.isEmpty() ? parts.at(i) : dir + u'/' + parts.at(i);
    }
    return ignored.value_or(false);
}
