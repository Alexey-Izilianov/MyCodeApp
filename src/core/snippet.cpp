#include "snippet.h"

#include <QHash>
#include <QMap>

namespace core::snippet {

namespace {

struct Node {
    enum Kind { Text, Stop, Variable } kind = Text;
    QString text;           // Text — сам текст, Variable — имя
    int number = 0;         // Stop
    QVector<Node> children; // заглушка Stop, запасной текст Variable
};

bool isNameStart(QChar c) { return c.isLetter() || c == u'_'; }
bool isNameChar(QChar c) { return c.isLetterOrNumber() || c == u'_'; }

class Parser {
public:
    explicit Parser(const QString &source) : s(source) {}

    // До конца строки или до '}' (inBraces), который поглощается
    QVector<Node> parse(bool inBraces)
    {
        QVector<Node> nodes;
        QString literal;
        auto flush = [&] {
            if (!literal.isEmpty())
                nodes.append({Node::Text, literal, 0, {}});
            literal.clear();
        };
        while (i < s.size()) {
            const QChar c = s.at(i);
            if (c == u'\\' && i + 1 < s.size() && QStringView(u"$}\\").contains(s.at(i + 1))) {
                literal += s.at(i + 1);
                i += 2;
            } else if (inBraces && c == u'}') {
                ++i;
                flush();
                return nodes;
            } else if (c == u'$') {
                const int mark = i;
                Node node;
                if (dollar(node)) {
                    flush();
                    nodes.append(node);
                } else {
                    i = mark + 1;
                    literal += u'$';
                }
            } else {
                literal += c;
                ++i;
            }
        }
        flush();
        return nodes;
    }

private:
    int number()
    {
        int n = 0;
        while (i < s.size() && s.at(i).isDigit())
            n = n * 10 + s.at(i++).digitValue();
        return n;
    }
    QString name()
    {
        const int start = i;
        while (i < s.size() && isNameChar(s.at(i)))
            ++i;
        return s.mid(start, i - start);
    }

    // i на '$'; false — не конструкция, а просто знак доллара
    bool dollar(Node &node)
    {
        ++i;
        if (i >= s.size())
            return false;
        if (s.at(i).isDigit()) {
            node = {Node::Stop, {}, number(), {}};
            return true;
        }
        if (isNameStart(s.at(i))) {
            node = {Node::Variable, name(), 0, {}};
            return true;
        }
        if (s.at(i) != u'{' || i + 1 >= s.size())
            return false;
        ++i;
        if (s.at(i).isDigit()) {
            node = {Node::Stop, {}, number(), {}};
        } else if (isNameStart(s.at(i))) {
            node = {Node::Variable, name(), 0, {}};
        } else {
            return false;
        }
        if (i >= s.size())
            return false;
        const QChar next = s.at(i++);
        if (next == u'}')
            return true;
        if (next == u':') {
            node.children = parse(true);
            return true;
        }
        if (next == u'|' && node.kind == Node::Stop) { // выбор: берём первый вариант
            QString first;
            bool done = false;
            for (; i < s.size(); ++i) {
                const QChar ch = s.at(i);
                if (ch == u'\\' && i + 1 < s.size()) {
                    if (!done)
                        first += s.at(i + 1);
                    ++i;
                } else if (ch == u'|' && i + 1 < s.size() && s.at(i + 1) == u'}') {
                    i += 2;
                    node.children = {{Node::Text, first, 0, {}}};
                    return true;
                } else if (ch == u',') {
                    done = true;
                } else if (!done) {
                    first += ch;
                }
            }
        }
        return false;
    }

    const QString &s;
    int i = 0;
};

class Renderer {
public:
    explicit Renderer(const Variables &variables) : m_variables(variables) {}

    void collectPlaceholders(const QVector<Node> &nodes)
    {
        for (const Node &n : nodes) {
            if (n.kind == Node::Stop && !n.children.isEmpty() && !m_placeholders.contains(n.number))
                m_placeholders.insert(n.number, plain(n.children));
            collectPlaceholders(n.children);
        }
    }

    void render(const QVector<Node> &nodes)
    {
        for (const Node &n : nodes) {
            switch (n.kind) {
            case Node::Text:
                m_out.text += n.text;
                break;
            case Node::Variable:
                if (const auto value = m_variables ? m_variables(n.text) : std::nullopt)
                    m_out.text += *value;
                else if (!n.children.isEmpty())
                    render(n.children);
                else
                    m_out.text += n.text;
                break;
            case Node::Stop: {
                const int start = int(m_out.text.size());
                if (!n.children.isEmpty())
                    render(n.children);
                else
                    m_out.text += m_placeholders.value(n.number); // зеркало
                m_ranges[n.number].append({start, int(m_out.text.size()) - start});
                break;
            }
            }
        }
    }

    Expansion finish()
    {
        if (!m_ranges.contains(0))
            m_ranges[0].append({int(m_out.text.size()), 0});
        for (auto it = m_ranges.cbegin(); it != m_ranges.cend(); ++it)
            if (it.key() != 0)
                m_out.stops.append({it.key(), it.value()});
        m_out.stops.append({0, m_ranges.value(0)});
        return m_out;
    }

private:
    QString plain(const QVector<Node> &nodes)
    {
        Renderer inner(m_variables);
        inner.m_placeholders = m_placeholders;
        inner.render(nodes);
        return inner.m_out.text;
    }

    const Variables &m_variables;
    QHash<int, QString> m_placeholders;
    QMap<int, QVector<Range>> m_ranges; // по возрастанию номера
    Expansion m_out;
};

} // namespace

Expansion expand(const QString &body, const Variables &variables)
{
    const QVector<Node> nodes = Parser(body).parse(false);
    Renderer renderer(variables);
    renderer.collectPlaceholders(nodes);
    renderer.render(nodes);
    return renderer.finish();
}

} // namespace core::snippet
