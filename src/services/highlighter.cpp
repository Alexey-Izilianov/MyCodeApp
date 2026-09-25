#include "highlighter.h"

#include <QFile>
#include <algorithm>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

bool Highlighter::loadRules(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return false;
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    const QJsonObject root = doc.object();
    const QJsonObject langs = root.value("languages").toObject();

    m_languages.clear();
    m_language = -1;
    for (auto it = langs.begin(); it != langs.end(); ++it) {
        const QJsonObject lang = it.value().toObject();
        Language l;
        for (const QJsonValue &ext : lang.value("extensions").toArray())
            l.extensions.insert(ext.toString());
        for (const QJsonValue &rv : lang.value("rules").toArray()) {
            const QJsonObject r = rv.toObject();
            Rule rule;
            rule.re = QRegularExpression(r.value("re").toString());
            if (!rule.re.isValid())
                continue; // битое правило не роняет загрузку остальных
            rule.color = QColor(r.value("color").toString());
            l.rules.append(rule);
        }
        if (!l.rules.isEmpty())
            m_languages.append(l);
    }
    return !m_languages.isEmpty();
}

void Highlighter::setLanguageForFile(const QString &path)
{
    const QString suffix = QFileInfo(path).suffix().toLower();
    m_language = -1;
    for (int i = 0; i < m_languages.size(); ++i) {
        if (m_languages.at(i).extensions.contains(suffix)) {
            m_language = i;
            return;
        }
    }
}

void Highlighter::highlightLine(const QString &line, QVector<Span> &out) const
{
    out.clear();
    if (!hasLanguage() || line.isEmpty())
        return;

    struct Candidate {
        int start;
        int rule;
        int length;
    };
    QVector<Candidate> all;
    const QVector<Rule> &rules = m_languages.at(m_language).rules;
    for (int r = 0; r < rules.size(); ++r) {
        QRegularExpressionMatchIterator it = rules.at(r).re.globalMatch(line);
        while (it.hasNext()) {
            const QRegularExpressionMatch m = it.next();
            if (m.capturedLength(0) > 0)
                all.append({m.capturedStart(0), r, m.capturedLength(0)});
        }
    }

    // Правило, начавшееся раньше, забирает фрагмент себе; при равном старте
    // выигрывает идущее раньше в JSON (ключ JSON перед обычной строкой).
    std::sort(all.begin(), all.end(), [](const Candidate &a, const Candidate &b) {
        if (a.start != b.start)
            return a.start < b.start;
        return a.rule < b.rule;
    });

    int lastEnd = 0;
    for (const Candidate &c : all) {
        if (c.start < lastEnd)
            continue;
        out.append({c.start, c.length, c.rule});
        lastEnd = c.start + c.length;
    }
}

QColor Highlighter::ruleColor(int rule) const
{
    if (!hasLanguage())
        return QColor();
    const QVector<Rule> &rules = m_languages.at(m_language).rules;
    return rule >= 0 && rule < rules.size() ? rules.at(rule).color : QColor();
}