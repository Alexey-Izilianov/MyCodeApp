#include "syntaxhighlighter.h"

#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QSet>
#include <tree_sitter/api.h>

extern "C" const TSLanguage *tree_sitter_cpp();
extern "C" const TSLanguage *tree_sitter_python();

using core::PieceTable;
using core::TextBuffer;

namespace {

struct Style {
    const char *capture;
    QRgb color;
};

// Индекс в таблице — Span::rule
constexpr Style kStyles[] = {
    {"keyword", 0xcf8e6d},  {"type", 0x4fc1b5},    {"function", 0x56a8f5},
    {"string", 0x6aab73},   {"escape", 0xd5b778},  {"number", 0xb5a1e6},
    {"constant", 0xcf8e6d}, {"comment", 0x7a7e85}, {"preprocessor", 0xe06c75},
};

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

} // namespace

struct SyntaxHighlighter::Language {
    const TSLanguage *ts = nullptr;
    TSQuery *query = nullptr;
    QVector<int> captureStyle; // id захвата -> индекс kStyles, -1 — без цвета
};

const SyntaxHighlighter::Language *SyntaxHighlighter::languageFor(const QString &suffix)
{
    auto make = [](const TSLanguage *ts, const QString &queryPath) {
        Language lang;
        QFile file(queryPath);
        if (!file.open(QIODevice::ReadOnly))
            return lang;
        const QByteArray source = file.readAll();
        uint32_t errorOffset = 0;
        TSQueryError error = TSQueryErrorNone;
        lang.query = ts_query_new(ts, source.constData(), uint32_t(source.size()),
                                  &errorOffset, &error);
        if (!lang.query) {
            qWarning("tree-sitter query %s: error %d at offset %u",
                     qPrintable(queryPath), int(error), errorOffset);
            return lang;
        }
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
        return lang;
    };

    // Запросы живут до конца процесса — как и сами грамматики
    static const Language cpp = make(tree_sitter_cpp(), QStringLiteral(":/assets/queries/cpp.scm"));
    static const Language python = make(tree_sitter_python(), QStringLiteral(":/assets/queries/python.scm"));
    static const QHash<QString, const Language *> bySuffix = {
        {"c", &cpp},  {"cc", &cpp},  {"cpp", &cpp}, {"cxx", &cpp}, {"h", &cpp},
        {"hh", &cpp}, {"hpp", &cpp}, {"hxx", &cpp}, {"inl", &cpp},
        {"py", &python}, {"pyw", &python}, {"pyi", &python},
    };
    const Language *lang = bySuffix.value(suffix);
    return lang && lang->query ? lang : nullptr;
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
    setTree(nullptr);
}

bool SyntaxHighlighter::setLanguageForFile(const QString &path)
{
    clear();
    m_lang = languageFor(QFileInfo(path).suffix().toLower());
    return m_lang;
}

void SyntaxHighlighter::clear()
{
    ++m_generation;
    m_busy = false;
    m_editsDuringParse.clear();
    m_latest.reset();
    setTree(nullptr);
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
    const TSLanguage *ts = m_lang->ts;

    m_pool.start([this, snapshot, old, generation, ts] {
        struct Cancel {
            const std::atomic<int> *current;
            int generation;
        } cancel{&m_generation, generation};
        const TSParseOptions options{&cancel, [](TSParseState *state) {
            const auto *c = static_cast<const Cancel *>(state->payload);
            return c->current->load(std::memory_order_relaxed) != c->generation;
        }};
        const TSInput input{const_cast<PieceTable::Snapshot *>(&snapshot), readChunk,
                            TSInputEncodingUTF16LE, nullptr};

        TSParser *parser = ts_parser_new();
        ts_parser_set_language(parser, ts);
        std::shared_ptr<TSTree> tree(ts_parser_parse_with_options(parser, old, input, options),
                                     [](TSTree *t) { if (t) ts_tree_delete(t); });
        ts_parser_delete(parser);
        if (old)
            ts_tree_delete(old);

        QMetaObject::invokeMethod(this, [this, tree, generation] { onParsed(tree, generation); },
                                  Qt::QueuedConnection);
    });
}

void SyntaxHighlighter::onParsed(std::shared_ptr<TSTree> tree, int generation)
{
    if (generation != m_generation)
        return;
    m_busy = false;
    if (!tree)
        return;
    TSTree *fresh = ts_tree_copy(tree.get());
    for (const TextBuffer::Edit &e : std::as_const(m_editsDuringParse)) {
        const TSInputEdit tsEdit = toTsEdit(e);
        ts_tree_edit(fresh, &tsEdit);
    }
    setTree(fresh);
    if (m_latest)
        schedule(PieceTable::Snapshot(*m_latest));
    emit updated();
}

void SyntaxHighlighter::setTree(TSTree *tree)
{
    if (m_tree)
        ts_tree_delete(m_tree);
    m_tree = tree;
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

    TSQueryCursor *cursor = ts_query_cursor_new();
    ts_query_cursor_set_point_range(cursor, {uint32_t(firstLine), 0},
                                    {uint32_t(firstLine + lines.size()), 0});
    ts_query_cursor_exec(cursor, m_lang->query, ts_tree_root_node(m_tree));

    // Захваты идут по возрастанию начала: вложенный узел перекрашивает
    // внешний (escape в строке), повторный захват того же узла игнорируем —
    // побеждает шаблон, стоящий в .scm раньше.
    QSet<const void *> seen;
    TSQueryMatch match;
    uint32_t captureIndex = 0;
    while (ts_query_cursor_next_capture(cursor, &match, &captureIndex)) {
        const TSQueryCapture &capture = match.captures[captureIndex];
        const int style = m_lang->captureStyle.value(int(capture.index), -1);
        if (style < 0 || seen.contains(capture.node.id))
            continue;
        seen.insert(capture.node.id);

        const TSPoint start = ts_node_start_point(capture.node);
        const TSPoint end = ts_node_end_point(capture.node);
        const int fromRow = qMax(int(start.row), firstLine);
        const int toRow = qMin(int(end.row), firstLine + int(lines.size()) - 1);
        for (int row = fromRow; row <= toRow; ++row) {
            QVector<qint8> &line = paint[row - firstLine];
            const int from = row == int(start.row) ? int(start.column / 2) : 0;
            const int to = row == int(end.row) ? qMin(int(end.column / 2), int(line.size()))
                                               : int(line.size());
            for (int c = from; c < to; ++c)
                line[c] = qint8(style);
        }
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
    return rule >= 0 && rule < int(std::size(kStyles)) ? QColor(kStyles[rule].color)
                                                       : QColor();
}

const char *SyntaxHighlighter::styleName(int rule)
{
    return rule >= 0 && rule < int(std::size(kStyles)) ? kStyles[rule].capture : "";
}
