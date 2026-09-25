#pragma once

#include <QColor>
#include <QRegularExpression>
#include <QSet>
#include <QString>
#include <QVector>

// Подсветка синтаксиса по регэксп-правилам из JSON (M1).
// Правила построчные: блочные комментарии /* ... */ учитываются только
// внутри одной строки. Приоритет правила = его порядок в JSON.
class Highlighter {
public:
    struct Span {
        int start = 0;   // индекс QChar в строке
        int length = 0;
        int rule = 0;    // индекс правила внутри языка
    };

    bool loadRules(const QString &path); // JSON: {"languages": {...}}
    void setLanguageForFile(const QString &path); // по суффиксу имени
    bool hasLanguage() const { return m_language >= 0; }

    // out: отсортированные по start неперекрывающиеся фрагменты
    void highlightLine(const QString &line, QVector<Span> &out) const;
    QColor ruleColor(int rule) const;

private:
    struct Rule {
        QRegularExpression re;
        QColor color;
    };
    struct Language {
        QSet<QString> extensions;
        QVector<Rule> rules;
    };

    QVector<Language> m_languages;
    int m_language = -1;
};