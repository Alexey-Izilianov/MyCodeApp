#include "syntaxhighlighter.h"

#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QRegularExpression>
#include <QSet>
#include <cstdlib>
#include <tree_sitter/api.h>

#include "palette.h"

extern "C" const TSLanguage *tree_sitter_cpp();
extern "C" const TSLanguage *tree_sitter_python();
extern "C" const TSLanguage *tree_sitter_qmljs();
extern "C" const TSLanguage *tree_sitter_markdown();
extern "C" const TSLanguage *tree_sitter_markdown_inline();
extern "C" const TSLanguage *tree_sitter_cmake();

using core::PieceTable;
using core::TextBuffer;

namespace {

constexpr QRgb kTextColor = 0; // стиль меняет только начертание

struct Style {
    const char *capture;
    QRgb color;
    int font = 0; // SyntaxHighlighter::FontStyle
};

// Индекс в таблице — Span::rule. Цвета — запасные, если тема вид не задаёт
constexpr Style kStyles[] = {
    {"keyword", 0xcf8e6d},  {"type", 0x4fc1b5},    {"function", 0x56a8f5},
    {"string", 0x6aab73},   {"escape", 0xd5b778},  {"number", 0xb5a1e6},
    {"constant", 0xcf8e6d}, {"comment", 0x7a7e85}, {"preprocessor", 0xe06c75},
    {"property", 0xc77dbb},
    // Разметка (Markdown)
    {"heading", 0xcf8e6d, SyntaxHighlighter::Bold},
    {"bold", 0x56a8f5, SyntaxHighlighter::Bold},
    {"italic", kTextColor, SyntaxHighlighter::Italic},
    {"strike", 0x7a7e85, SyntaxHighlighter::StrikeOut},
    {"code", 0x6aab73},
    {"link", 0x56a8f5},
    {"url", 0x7a7e85, SyntaxHighlighter::Underline},
    {"punctuation", 0x7a7e85},
    {"quote", 0x9da0a8, SyntaxHighlighter::Italic},
};

constexpr int kMaxInjections = 10000; // дальше вложенные языки не разбираем (каждый — свой разбор)

constexpr int kMaxLineChars = 10000; // дальше строка без подсветки

TSPoint toPoint(const TextBuffer::Position &p)
{
    return {uint32_t(p.line), uint32_t(p.column) * 2};
}

TSInputEdit toTsEdit(const TextBuffer::Edit &e)
{
    return {uint32_t(e.startOffset) * 2, uint32_t(e.oldEndOffset) * 2,
            uint32_t(e.newEndOffset) * 2, toPoint(e.start),
            toPoint(e.oldEnd), toPoint(e.newEnd)};
}

const char *readChunk(void *payload, uint32_t byteIndex, TSPoint, uint32_t *bytesRead)
{
    int length = 0;
    const QChar *data = static_cast<const PieceTable::Snapshot *>(payload)
                            ->chunkAt(int(byteIndex / 2), &length);
    *bytesRead = uint32_t(length) * 2;
    return reinterpret_cast<const char *>(data);
}

// Участки узла без его детей (у абзаца в цитате — без маркеров «>» в начале строк)
QVector<TSRange> rangesOf(TSNode node)
{
    QVector<TSRange> ranges;
    TSPoint point = ts_node_start_point(node);
    uint32_t byte = ts_node_start_byte(node);
    for (uint32_t i = 0; i < ts_node_named_child_count(node); ++i) {
        const TSNode child = ts_node_named_child(node, i);
        if (ts_node_start_byte(child) > byte)
            ranges.append({point, ts_node_start_point(child), byte, ts_node_start_byte(child)});
        point = ts_node_end_point(child);
        byte = ts_node_end_byte(child);
    }
    if (ts_node_end_byte(node) > byte)
        ranges.append({point, ts_node_end_point(node), byte, ts_node_end_byte(node)});
    return ranges;
}

} // namespace

struct SyntaxHighlighter::Language {
    const TSLanguage *ts = nullptr;
    TSQuery *query = nullptr;
    QVector<int> captureStyle; // id захвата -> индекс kStyles, -1 — без цвета

    // Текстовые условия шаблонов: #match?, #eq?, #any-of? (и not-). Прочие не проверяются
    struct Predicate {
        uint32_t capture = 0;
        bool negate = false;
        std::optional<QRegularExpression> re; // иначе — сравнение с values
        QStringList values;
    };
    QVector<QVector<Predicate>> predicates; // по номеру шаблона

    // Вложенные языки: роль каждого захвата запроса injections
    enum Role { None, Inline, LanguageName, Content };
    TSQuery *injections = nullptr;
    QVector<Role> injectionRole;
    const Language *inlineLanguage = nullptr; // для захвата @inline
};

struct SyntaxHighlighter::Parsed {
    TSTree *tree = nullptr;
    QVector<Injection> injections;
    ~Parsed()
    {
        if (tree)
            ts_tree_delete(tree);
        for (const Injection &i : std::as_const(injections))
            ts_tree_delete(i.tree);
    }
};

namespace {

QString stringOf(const TSQuery *query, uint32_t id)
{
    uint32_t len = 0;
    const char *raw = ts_query_string_value_for_id(query, id, &len);
    return QString::fromUtf8(raw, len);
}

} // namespace

const SyntaxHighlighter::Language *SyntaxHighlighter::languageFor(const QString &suffix)
{
    auto predicatesOf = [](const TSQuery *query) {
        QVector<QVector<Language::Predicate>> out(ts_query_pattern_count(query));
        for (uint32_t pattern = 0; pattern < uint32_t(out.size()); ++pattern) {
            uint32_t count = 0;
            const TSQueryPredicateStep *steps = ts_query_predicates_for_pattern(query, pattern, &count);
            // Шаги идут группами: имя, аргументы, Done
            for (uint32_t i = 0; i < count;) {
                const uint32_t start = i;
                while (i < count && steps[i].type != TSQueryPredicateStepTypeDone)
                    ++i;
                const uint32_t end = i++;
                if (end - start < 3 || steps[start].type != TSQueryPredicateStepTypeString
                    || steps[start + 1].type != TSQueryPredicateStepTypeCapture)
                    continue;
                QString name = stringOf(query, steps[start].value_id);
                Language::Predicate predicate;
                predicate.capture = steps[start + 1].value_id;
                predicate.negate = name.startsWith(QLatin1String("not-"));
                if (predicate.negate)
                    name = name.mid(4);
                for (uint32_t a = start + 2; a < end; ++a)
                    if (steps[a].type == TSQueryPredicateStepTypeString)
                        predicate.values.append(stringOf(query, steps[a].value_id));
                if (name == QLatin1String("match?") && !predicate.values.isEmpty())
                    predicate.re = QRegularExpression(predicate.values.constFirst());
                else if (name != QLatin1String("eq?") && name != QLatin1String("any-of?"))
                    continue;
                out[pattern].append(predicate);
            }
        }
        return out;
    };

    auto loadQuery = [](const TSLanguage *ts, const QString &queryPath) -> TSQuery * {
        QFile file(queryPath);
        if (!file.open(QIODevice::ReadOnly))
            return nullptr;
        const QByteArray source = file.readAll();
        uint32_t errorOffset = 0;
        TSQueryError error = TSQueryErrorNone;
        TSQuery *query = ts_query_new(ts, source.constData(), uint32_t(source.size()),
                                      &errorOffset, &error);
        if (!query)
            qWarning("tree-sitter query %s: error %d at offset %u",
                     qPrintable(queryPath), int(error), errorOffset);
        return query;
    };
    auto make = [&loadQuery, &predicatesOf](const TSLanguage *ts, const QString &queryPath) {
        Language lang;
        lang.query = loadQuery(ts, queryPath);
        if (!lang.query)
            return lang;
        lang.ts = ts;
        for (uint32_t i = 0; i < ts_query_capture_count(lang.query); ++i) {
            uint32_t len = 0;
            const char *raw = ts_query_capture_name_for_id(lang.query, i, &len);
            const QByteArray name(raw, len);
            const qsizetype dot = name.indexOf('.'); // keyword.control -> keyword
            const QByteArray base = dot < 0 ? name : name.first(dot);
            int style = -1;
            for (int s = 0; s < int(std::size(kStyles)); ++s)
                if (base == kStyles[s].capture)
                    style = s;
            lang.captureStyle.append(style);
        }
        lang.predicates = predicatesOf(lang.query);
        return lang;
    };

    // Запросы живут до конца процесса — как и сами грамматики
    static const Language cpp = make(tree_sitter_cpp(), QStringLiteral(":/assets/queries/cpp.scm"));
    static const Language python = make(tree_sitter_python(), QStringLiteral(":/assets/queries/python.scm"));
    static const Language qml = make(tree_sitter_qmljs(), QStringLiteral(":/assets/queries/qml.scm"));
    static const Language cmake = make(tree_sitter_cmake(), QStringLiteral(":/assets/queries/cmake.scm"));
    static const Language markdownInline = make(tree_sitter_markdown_inline(),
                                                QStringLiteral(":/assets/queries/markdown-inline.scm"));
    static const Language markdown = [&] {
        Language lang = make(tree_sitter_markdown(), QStringLiteral(":/assets/queries/markdown.scm"));
        if (!lang.ts)
            return lang;
        lang.injections = loadQuery(lang.ts, QStringLiteral(":/assets/queries/markdown-injections.scm"));
        if (!lang.injections)
            return lang;
        lang.inlineLanguage = markdownInline.query ? &markdownInline : nullptr;
        for (uint32_t i = 0; i < ts_query_capture_count(lang.injections); ++i) {
            uint32_t len = 0;
            const char *raw = ts_query_capture_name_for_id(lang.injections, i, &len);
            const QByteArray name(raw, len);
            lang.injectionRole.append(name == "inline"               ? Language::Inline
                                      : name == "injection.language" ? Language::LanguageName
                                      : name == "injection.content"  ? Language::Content
                                                                     : Language::None);
        }
        return lang;
    }();
    static const QHash<QString, const Language *> bySuffix = {
        {"c", &cpp},  {"cc", &cpp},  {"cpp", &cpp}, {"cxx", &cpp}, {"h", &cpp},
        {"hh", &cpp}, {"hpp", &cpp}, {"hxx", &cpp}, {"inl", &cpp},
        {"py", &python}, {"pyw", &python}, {"pyi", &python},
        {"qml", &qml},
        {"md", &markdown}, {"markdown", &markdown},
        {"cmake", &cmake},
    };
    const Language *lang = bySuffix.value(suffix);
    return lang && lang->query ? lang : nullptr;
}

const SyntaxHighlighter::Language *SyntaxHighlighter::languageNamed(const QString &name)
{
    // Имена языков в ``` — как на GitHub; JavaScript и TypeScript разбирает грамматика QML
    static const QHash<QString, QString> aliases = {
        {"c++", "cpp"}, {"python", "py"}, {"python3", "py"},
        {"js", "qml"}, {"javascript", "qml"}, {"ts", "qml"}, {"typescript", "qml"}, {"qmljs", "qml"},
    };
    const QString key = name.trimmed().toLower();
    return languageFor(aliases.value(key, key));
}

SyntaxHighlighter::SyntaxHighlighter(QObject *parent)
    : QObject(parent)
{
    m_pool.setMaxThreadCount(1);
}

SyntaxHighlighter::~SyntaxHighlighter()
{
    ++m_generation; // фоновый разбор увидит отмену в progress_callback
    m_pool.waitForDone();
    setTrees(nullptr, {});
}

bool SyntaxHighlighter::setLanguageForFile(const QString &path)
{
    clear();
    const QFileInfo info(path);
    m_lang = info.fileName().compare(QLatin1String("CMakeLists.txt"), Qt::CaseInsensitive) == 0
                 ? languageFor(QStringLiteral("cmake"))
                 : languageFor(info.suffix().toLower());
    return m_lang;
}

void SyntaxHighlighter::clear()
{
    ++m_generation;
    m_busy = false;
    m_editsDuringParse.clear();
    m_latest.reset();
    setTrees(nullptr, {});
}

void SyntaxHighlighter::reset(const PieceTable::Snapshot &snapshot)
{
    clear();
    if (m_lang && snapshot.length() <= kMaxChars)
        schedule(snapshot);
}

void SyntaxHighlighter::applyEdits(const QVector<TextBuffer::Edit> &edits,
                                   const PieceTable::Snapshot &snapshot)
{
    if (!m_tree && !m_busy)
        return; // нет языка или файл слишком велик
    if (snapshot.length() > kMaxChars) {
        clear();
        emit updated();
        return;
    }
    if (m_tree) {
        for (const TextBuffer::Edit &e : edits) {
            const TSInputEdit tsEdit = toTsEdit(e);
            ts_tree_edit(m_tree, &tsEdit);
            for (const Injection &i : std::as_const(m_injections))
                ts_tree_edit(i.tree, &tsEdit);
        }
    }
    if (m_busy) {
        m_editsDuringParse += edits;
        m_latest = snapshot;
    } else {
        schedule(snapshot);
    }
}

void SyntaxHighlighter::schedule(const PieceTable::Snapshot &snapshot)
{
    m_busy = true;
    m_editsDuringParse.clear();
    m_latest.reset();
    TSTree *old = m_tree ? ts_tree_copy(m_tree) : nullptr;
    const int generation = m_generation;
    const Language *lang = m_lang;

    m_pool.start([this, snapshot, old, generation, lang] {
        struct Cancel {
            const std::atomic<int> *current;
            int generation;
            bool cancelled() const { return current->load(std::memory_order_relaxed) != generation; }
        } cancel{&m_generation, generation};
        const TSParseOptions options{&cancel, [](TSParseState *state) {
            return static_cast<const Cancel *>(state->payload)->cancelled();
        }};
        const TSInput input{const_cast<PieceTable::Snapshot *>(&snapshot), readChunk,
                            TSInputEncodingUTF16LE, nullptr};

        auto parsed = std::make_shared<Parsed>();
        TSParser *parser = ts_parser_new();
        ts_parser_set_language(parser, lang->ts);
        parsed->tree = ts_parser_parse_with_options(parser, old, input, options);
        if (old)
            ts_tree_delete(old);

        // Каждый участок — отдельным деревом: незакрытый ` или * не утекает в соседний абзац
        if (parsed->tree && lang->injections) {
            TSQueryCursor *cursor = ts_query_cursor_new();
            ts_query_cursor_exec(cursor, lang->injections, ts_tree_root_node(parsed->tree));
            TSQueryMatch match;
            while (ts_query_cursor_next_match(cursor, &match)
                   && parsed->injections.size() < kMaxInjections && !cancel.cancelled()) {
                const Language *target = nullptr;
                std::optional<TSNode> content;
                bool replacesBase = false;
                for (uint16_t i = 0; i < match.capture_count; ++i) {
                    const TSNode node = match.captures[i].node;
                    switch (lang->injectionRole.value(int(match.captures[i].index), Language::None)) {
                    case Language::Inline:
                        target = lang->inlineLanguage;
                        content = node;
                        break;
                    case Language::LanguageName: {
                        const int from = int(ts_node_start_byte(node) / 2);
                        target = languageNamed(snapshot.textAt(from, int(ts_node_end_byte(node) / 2) - from));
                        break;
                    }
                    case Language::Content:
                        content = node;
                        replacesBase = true;
                        break;
                    case Language::None:
                        break;
                    }
                }
                if (!target || !content)
                    continue;
                const QVector<TSRange> ranges = rangesOf(*content);
                if (ranges.isEmpty())
                    continue;
                ts_parser_set_language(parser, target->ts);
                if (!ts_parser_set_included_ranges(parser, ranges.constData(), uint32_t(ranges.size())))
                    continue;
                if (TSTree *tree = ts_parser_parse_with_options(parser, nullptr, input, options))
                    parsed->injections.append({target, tree, replacesBase});
            }
            ts_query_cursor_delete(cursor);
        }
        ts_parser_delete(parser);

        QMetaObject::invokeMethod(this, [this, parsed, generation] { onParsed(parsed, generation); },
                                  Qt::QueuedConnection);
    });
}

void SyntaxHighlighter::onParsed(const std::shared_ptr<Parsed> &parsed, int generation)
{
    if (generation != m_generation)
        return;
    m_busy = false;
    if (!parsed->tree)
        return;
    TSTree *fresh = std::exchange(parsed->tree, nullptr);
    QVector<Injection> injections = std::exchange(parsed->injections, {});
    for (const TextBuffer::Edit &e : std::as_const(m_editsDuringParse)) {
        const TSInputEdit tsEdit = toTsEdit(e);
        ts_tree_edit(fresh, &tsEdit);
        for (const Injection &i : std::as_const(injections))
            ts_tree_edit(i.tree, &tsEdit);
    }
    setTrees(fresh, std::move(injections));
    if (m_latest)
        schedule(PieceTable::Snapshot(*m_latest));
    emit updated();
}

void SyntaxHighlighter::setTrees(TSTree *tree, QVector<Injection> injections)
{
    if (m_tree)
        ts_tree_delete(m_tree);
    for (const Injection &i : std::as_const(m_injections))
        ts_tree_delete(i.tree);
    m_tree = tree;
    m_injections = std::move(injections);
}

void SyntaxHighlighter::highlightLines(int firstLine, const QStringList &lines,
                                       QVector<QVector<Span>> &out) const
{
    out = QVector<QVector<Span>>(lines.size());
    if (!m_tree || lines.isEmpty())
        return;

    // Цвет каждого символа видимых строк; -1 — цвет текста по умолчанию
    QVector<QVector<qint8>> paint(lines.size());
    for (int i = 0; i < lines.size(); ++i)
        paint[i].fill(-1, qMin(int(lines.at(i).size()), kMaxLineChars));

    const int lastLine = firstLine + int(lines.size()) - 1;
    auto fill = [&](TSPoint start, TSPoint end, qint8 style) {
        const int fromRow = qMax(int(start.row), firstLine);
        const int toRow = qMin(int(end.row), lastLine);
        for (int row = fromRow; row <= toRow; ++row) {
            QVector<qint8> &line = paint[row - firstLine];
            const int from = row == int(start.row) ? int(start.column / 2) : 0;
            const int to = row == int(end.row) ? qMin(int(end.column / 2), int(line.size()))
                                               : int(line.size());
            for (int c = from; c < to; ++c)
                line[c] = style;
        }
    };

    // Захваты идут по возрастанию начала: вложенный узел перекрашивает
    // внешний (escape в строке), повторный захват того же узла игнорируем —
    // побеждает шаблон, стоящий в .scm раньше.
    QSet<const void *> seen;
    TSQueryCursor *cursor = ts_query_cursor_new();
    // Текст узла для условий шаблона; узел на нескольких строках или вне видимых — не проверяем
    auto passes = [&](const Language *lang, const TSQueryMatch &match) {
        for (const Language::Predicate &predicate : lang->predicates.value(match.pattern_index)) {
            for (uint16_t i = 0; i < match.capture_count; ++i) {
                const TSQueryCapture &capture = match.captures[i];
                if (capture.index != predicate.capture)
                    continue;
                const TSPoint start = ts_node_start_point(capture.node);
                const TSPoint end = ts_node_end_point(capture.node);
                if (start.row != end.row || int(start.row) < firstLine || int(start.row) > lastLine)
                    return false;
                const QString text = lines.at(int(start.row) - firstLine)
                                         .mid(int(start.column / 2), int(end.column - start.column) / 2);
                const bool hit = predicate.re ? predicate.re->match(text).hasMatch()
                                              : predicate.values.contains(text);
                if (hit == predicate.negate)
                    return false;
            }
        }
        return true;
    };
    auto paintTree = [&](TSTree *tree, const Language *lang) {
        ts_query_cursor_set_point_range(cursor, {uint32_t(firstLine), 0}, {uint32_t(lastLine + 1), 0});
        ts_query_cursor_exec(cursor, lang->query, ts_tree_root_node(tree));
        TSQueryMatch match;
        uint32_t captureIndex = 0;
        while (ts_query_cursor_next_capture(cursor, &match, &captureIndex)) {
            const TSQueryCapture &capture = match.captures[captureIndex];
            const int style = lang->captureStyle.value(int(capture.index), -1);
            if (style < 0 || seen.contains(capture.node.id) || !passes(lang, match))
                continue;
            seen.insert(capture.node.id);
            fill(ts_node_start_point(capture.node), ts_node_end_point(capture.node), qint8(style));
        }
    };

    paintTree(m_tree, m_lang);
    // Вложенные языки — поверх основного; код в ``` сначала стирает его цвет
    // Участки идут по тексту: первый, кончающийся не выше видимых строк, — бинарным поиском
    const auto first = std::partition_point(m_injections.cbegin(), m_injections.cend(), [&](const Injection &i) {
        return int(ts_node_end_point(ts_tree_root_node(i.tree)).row) < firstLine;
    });
    for (auto it = first; it != m_injections.cend(); ++it) {
        const Injection &injection = *it;
        const TSNode root = ts_tree_root_node(injection.tree);
        if (int(ts_node_start_point(root).row) > lastLine)
            break;
        if (injection.replacesBase) {
            uint32_t count = 0;
            TSRange *ranges = ts_tree_included_ranges(injection.tree, &count);
            for (uint32_t i = 0; i < count; ++i)
                fill(ranges[i].start_point, ranges[i].end_point, -1);
            std::free(ranges);
        }
        paintTree(injection.tree, injection.lang);
    }
    ts_query_cursor_delete(cursor);

    for (int i = 0; i < paint.size(); ++i) {
        const QVector<qint8> &line = paint.at(i);
        for (int c = 0; c < line.size();) {
            const qint8 style = line.at(c);
            int e = c + 1;
            while (e < line.size() && line.at(e) == style)
                ++e;
            if (style >= 0)
                out[i].append({c, e - c, style});
            c = e;
        }
    }
}

QColor SyntaxHighlighter::color(int rule)
{
    if (rule < 0 || rule >= int(std::size(kStyles)))
        return {};
    const QColor themed = Palette::instance().syntax(QLatin1String(kStyles[rule].capture));
    if (themed.isValid() || kStyles[rule].color == kTextColor)
        return themed;
    return QColor(kStyles[rule].color);
}

int SyntaxHighlighter::fontStyle(int rule)
{
    return rule >= 0 && rule < int(std::size(kStyles)) ? kStyles[rule].font : 0;
}

const char *SyntaxHighlighter::styleName(int rule)
{
    return rule >= 0 && rule < int(std::size(kStyles)) ? kStyles[rule].capture : "";
}
