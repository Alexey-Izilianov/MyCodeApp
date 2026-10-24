#include "prompting.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>

namespace assistant {

QVector<Delta> StreamParser::feed(const QByteArray &data)
{
    m_pending += data;
    QVector<Delta> out;
    qsizetype start = 0;
    for (qsizetype end; (end = m_pending.indexOf('\n', start)) >= 0; start = end + 1)
        parseLine(m_pending.mid(start, end - start), &out);
    m_pending.remove(0, start);
    return out;
}

QVector<Delta> StreamParser::finish()
{
    QVector<Delta> out;
    parseLine(m_pending, &out);
    m_pending.clear();
    return out;
}

void StreamParser::parseLine(const QByteArray &raw, QVector<Delta> *out) const
{
    QByteArray line = raw.trimmed();
    if (m_protocol == Protocol::OpenAi) {
        if (!line.startsWith("data:"))
            return; // пустые строки, комментарии «:», event:
        line = line.mid(5).trimmed();
        if (line == "[DONE]") {
            Delta d;
            d.done = true;
            out->append(d);
            return;
        }
    }
    if (line.isEmpty())
        return;
    const QJsonObject json = QJsonDocument::fromJson(line).object();
    if (json.isEmpty())
        return;

    Delta d;
    if (json.contains(QLatin1String("error"))) {
        d.error = errorFromBody(line);
        d.done = true;
        out->append(d);
        return;
    }
    if (m_protocol == Protocol::Ollama) {
        const QJsonObject message = json.value(QLatin1String("message")).toObject();
        d.content = message.value(QLatin1String("content")).toString();
        d.thinking = message.value(QLatin1String("thinking")).toString();
        d.done = json.value(QLatin1String("done")).toBool();
        if (d.done) {
            d.promptTokens = json.value(QLatin1String("prompt_eval_count")).toInt(-1);
            d.outputTokens = json.value(QLatin1String("eval_count")).toInt(-1);
        }
    } else {
        const QJsonObject choice = json.value(QLatin1String("choices")).toArray().at(0).toObject();
        const QJsonObject delta = choice.value(QLatin1String("delta")).toObject();
        d.content = delta.value(QLatin1String("content")).toString();
        d.thinking = delta.value(QLatin1String("reasoning_content")).toString();
        const QJsonObject usage = json.value(QLatin1String("usage")).toObject();
        if (!usage.isEmpty()) {
            d.promptTokens = usage.value(QLatin1String("prompt_tokens")).toInt(-1);
            d.outputTokens = usage.value(QLatin1String("completion_tokens")).toInt(-1);
        }
    }
    out->append(d);
}

QString errorFromBody(const QByteArray &body)
{
    const QJsonValue error = QJsonDocument::fromJson(body).object().value(QLatin1String("error"));
    if (error.isString())
        return error.toString();
    if (error.isObject())
        return error.toObject().value(QLatin1String("message")).toString();
    return QString::fromUtf8(body.left(300)).trimmed();
}

Excerpt excerpt(const std::function<QString(int)> &lineAt, int from, int to, int center, int budget)
{
    Excerpt result;
    if (to < from)
        return result;
    center = qBound(from, center, to);

    // Влезает ли всё — набираем, пока не превысим бюджет (файл может быть огромным)
    QStringList lines;
    int total = 0;
    for (int i = from; i <= to && total <= budget; ++i) {
        lines.append(lineAt(i));
        total += int(lines.last().size()) + 1;
    }
    if (total <= budget)
        return {lines.join(u'\n'), from, to, false};

    QString middle = lineAt(center);
    if (middle.size() > budget)
        middle = middle.left(budget);
    QStringList above, below;
    int first = center, last = center;
    int used = int(middle.size()) + 1;
    for (bool grew = true; grew;) {
        grew = false;
        if (last < to) {
            const QString next = lineAt(last + 1);
            if (used + next.size() + 1 <= budget) {
                below.append(next);
                used += int(next.size()) + 1;
                ++last;
                grew = true;
            }
        }
        if (first > from) {
            const QString previous = lineAt(first - 1);
            if (used + previous.size() + 1 <= budget) {
                above.prepend(previous);
                used += int(previous.size()) + 1;
                --first;
                grew = true;
            }
        }
    }
    return {(above + QStringList{middle} + below).join(u'\n'), first, last, true};
}

namespace {

const QRegularExpression &errorPattern()
{
    static const QRegularExpression re(
        QStringLiteral(R"(error|fail|fatal|exception|critical|panic|traceback|segfault|abort|denied|refused|)"
                       R"(timed? ?out|ошибк|сбой|исключени|не удалось|невозможно|отказ)"),
        QRegularExpression::CaseInsensitiveOption); // кириллицу без учёта регистра PCRE2 сравнивает и так
    return re;
}

const QRegularExpression &warningPattern()
{
    static const QRegularExpression re(QStringLiteral(R"(warn|deprecat|предупрежд|устарел)"),
                                       QRegularExpression::CaseInsensitiveOption);
    return re;
}

// Строка без меняющихся частей: время, номера, адреса — для схлопывания повторов
QString shapeOf(const QString &line)
{
    static const QRegularExpression variable(QStringLiteral(R"(0x[0-9a-fA-F]+|[0-9a-fA-F]{8,}|\d+)"));
    QString shape = line.left(240);
    shape.replace(variable, QStringLiteral("#"));
    return shape;
}

constexpr int kMaxLine = 400;

QString clipped(const QString &line)
{
    return line.size() <= kMaxLine ? line : line.left(kMaxLine) + u'…';
}

} // namespace

void LogDigest::add(const QString &raw)
{
    const int number = ++m_lines;
    const QString line = clipped(raw);
    if (number <= 5)
        m_head += line + u'\n';

    m_tail.push_back(line);
    m_tailChars += int(line.size()) + 1;
    while (m_tailChars > m_budget && m_tail.size() > 1) {
        m_tailChars -= int(m_tail.front().size()) + 1;
        m_tail.pop_front();
    }

    const bool error = errorPattern().match(line).hasMatch();
    const bool warning = !error && warningPattern().match(line).hasMatch();
    if (!error && !warning)
        return;
    error ? ++m_errors : ++m_warnings;
    const QString shape = shapeOf(line);
    if (const auto it = m_byShape.constFind(shape); it != m_byShape.cend()) {
        ++m_notable[*it].repeats;
        return;
    }
    // Первые ошибки — начало беды; поздние всё равно попадут в конец журнала
    if (m_notableFull)
        return;
    if (m_notableChars + line.size() > m_budget * 55 / 100) {
        m_notableFull = true;
        return;
    }
    m_byShape.insert(shape, int(m_notable.size()));
    m_notable.append({number, 1, line});
    m_notableChars += int(line.size()) + 16;
}

QString LogDigest::text() const
{
    if (int(m_tail.size()) == m_lines) { // журнал целиком влез
        QStringList all(m_tail.cbegin(), m_tail.cend());
        return all.join(u'\n');
    }

    QString out;
    out += QStringLiteral("### head (lines 1-%1)\n").arg(qMin(m_lines, 5)) + m_head;

    if (!m_notable.isEmpty()) {
        out += QStringLiteral("\n### errors and warnings: %1 + %2 lines, repeats collapsed (xN)%3\n")
                   .arg(m_errors).arg(m_warnings)
                   .arg(m_notableFull ? QStringLiteral(", only the first ones") : QString());
        for (const Notable &n : m_notable) {
            out += QStringLiteral("[%1] ").arg(n.line) + n.text;
            if (n.repeats > 1)
                out += QStringLiteral("  (x%1)").arg(n.repeats);
            out += u'\n';
        }
    }

    // Конец журнала — сколько осталось от бюджета, но не меньше четверти
    const int room = qMax(m_budget / 4, m_budget - int(out.size()));
    QStringList tail;
    int used = 0;
    for (auto it = m_tail.crbegin(); it != m_tail.crend() && used + it->size() + 1 <= room; ++it) {
        tail.prepend(*it);
        used += int(it->size()) + 1;
    }
    out += QStringLiteral("\n### tail (lines %1-%2 of %2)\n").arg(m_lines - tail.size() + 1).arg(m_lines);
    out += tail.join(u'\n');
    return out;
}

QString firstCodeBlock(const QString &markdown)
{
    const qsizetype open = markdown.indexOf(QLatin1String("```"));
    if (open >= 0) {
        const qsizetype bodyStart = markdown.indexOf(u'\n', open);
        if (bodyStart >= 0) {
            qsizetype close = markdown.indexOf(QLatin1String("\n```"), bodyStart);
            if (close < 0)
                close = markdown.size(); // ответ оборван на середине блока
            return markdown.mid(bodyStart + 1, close - bodyStart - 1);
        }
    }
    return markdown.trimmed();
}

QString cleanCommitMessage(const QString &answer)
{
    QString text = answer.trimmed().startsWith(QLatin1String("```")) ? firstCodeBlock(answer) : answer;
    text = text.trimmed();
    static const QList<std::pair<QChar, QChar>> quotes = {{u'"', u'"'}, {u'«', u'»'}, {u'\'', u'\''}, {u'`', u'`'}};
    for (const auto &[open, close] : quotes)
        if (text.size() > 1 && text.startsWith(open) && text.endsWith(close)) {
            text = text.mid(1, text.size() - 2).trimmed();
            break;
        }
    // Как принято в git: заголовок без точки, пустая строка, затем подробности
    QStringList lines = text.split(u'\n');
    for (QString &line : lines) {
        while (!line.isEmpty() && line.back().isSpace())
            line.chop(1); // переносы Markdown «два пробела»
    }
    if (lines.first().endsWith(u'.') && !lines.first().endsWith(QLatin1String("..")))
        lines.first().chop(1);
    if (lines.size() > 1 && !lines.at(1).isEmpty())
        lines.insert(1, QString());
    return lines.join(u'\n');
}

namespace {

QString leadingSpace(const QString &line)
{
    qsizetype n = 0;
    while (n < line.size() && (line.at(n) == u' ' || line.at(n) == u'\t'))
        ++n;
    return line.left(n);
}

// Без общего отступа и пустых строк по краям
QStringList dedented(const QString &text)
{
    QStringList lines = text.split(u'\n');
    for (QString &line : lines)
        if (line.endsWith(u'\r'))
            line.chop(1);
    while (!lines.isEmpty() && lines.first().trimmed().isEmpty())
        lines.removeFirst();
    while (!lines.isEmpty() && lines.last().trimmed().isEmpty())
        lines.removeLast();
    qsizetype common = -1;
    for (const QString &line : lines)
        if (!line.trimmed().isEmpty())
            common = common < 0 ? leadingSpace(line).size() : qMin(common, leadingSpace(line).size());
    for (QString &line : lines)
        line = line.mid(qMin(qMax<qsizetype>(common, 0), leadingSpace(line).size()));
    return lines;
}

QString indented(const QStringList &lines, const QString &indent)
{
    QString out;
    for (const QString &line : lines)
        out += (line.isEmpty() ? QString() : indent + line) + u'\n';
    return out;
}

} // namespace

Placement placeComment(const QStringList &lines, int firstLine, const QString &language,
                       const QString &comment)
{
    QStringList body = dedented(comment);
    int first = 0;
    while (first < lines.size() - 1 && lines.at(first).trimmed().isEmpty())
        ++first;

    if (language.compare(QLatin1String("Python"), Qt::CaseInsensitive) == 0) {
        static const QRegularExpression header(QStringLiteral(R"(^\s*(async\s+def|def|class)\s)"));
        int def = first;
        while (def < lines.size() && !header.match(lines.at(def)).hasMatch())
            ++def;
        if (def < lines.size()) {
            int end = def; // заголовок может занимать несколько строк
            while (end < lines.size() - 1 && end < def + 20
                   && !lines.at(end).section(u'#', 0, 0).trimmed().endsWith(u':'))
                ++end;
            const QString defIndent = leadingSpace(lines.at(def));
            QString bodyIndent = defIndent + (defIndent.contains(u'\t') ? QStringLiteral("\t") : QStringLiteral("    "));
            for (int i = end + 1; i < lines.size(); ++i) {
                if (lines.at(i).trimmed().isEmpty())
                    continue;
                if (leadingSpace(lines.at(i)).size() > defIndent.size())
                    bodyIndent = leadingSpace(lines.at(i));
                break;
            }
            const QString joined = body.join(u'\n');
            if (!joined.startsWith(QLatin1String("\"\"\"")) && !joined.startsWith(QLatin1String("'''"))) {
                if (body.size() == 1)
                    body = {QStringLiteral("\"\"\"%1\"\"\"").arg(body.first())};
                else
                    body = QStringList{QStringLiteral("\"\"\"")} + body + QStringList{QStringLiteral("\"\"\"")};
            }
            return {firstLine + end + 1, indented(body, bodyIndent)};
        }
    }
    const QString indent = first < lines.size() ? leadingSpace(lines.at(first)) : QString();
    return {firstLine + first, indented(body, indent)};
}

} // namespace assistant
