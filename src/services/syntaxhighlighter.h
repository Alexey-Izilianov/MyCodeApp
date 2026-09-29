#pragma once

#include <QColor>
#include <QObject>
#include <QStringList>
#include <QThreadPool>
#include <QVector>
#include <atomic>
#include <memory>
#include <optional>

#include "src/core/piecetable.h"
#include "src/core/textbuffer.h"
#include "src/services/highlighter.h"

struct TSTree;

// Подсветка на tree-sitter. Разбор — в фоне, по снимку piece table
// (tree-sitter читает куски снимка как UTF-16 без копирования), одна
// задача за раз. Правки сразу применяются к текущему дереву (ts_tree_edit
// сдвигает узлы — подсветка едет вместе с текстом до конца пересчёта);
// правки, пришедшие во время разбора, докатываются на его результат.
// API — из GUI-потока; highlightLines можно звать из updatePaintNode.
class SyntaxHighlighter : public QObject {
    Q_OBJECT
public:
    using Span = Highlighter::Span; // rule — индекс цвета для color()

    // Крупнее (в QChar) — не разбираем: секунды и сотни МБ на дерево
    static constexpr int kMaxChars = 8 * 1024 * 1024;

    explicit SyntaxHighlighter(QObject *parent = nullptr);
    ~SyntaxHighlighter() override;

    bool setLanguageForFile(const QString &path); // false — нет грамматики
    bool hasLanguage() const { return m_lang != nullptr; }

    void reset(const core::PieceTable::Snapshot &snapshot);
    void applyEdits(const QVector<core::TextBuffer::Edit> &edits,
                    const core::PieceTable::Snapshot &snapshot);
    void clear();

    bool isReady() const { return m_tree != nullptr; }
    bool isParsing() const { return m_busy; }

    void highlightLines(int firstLine, const QStringList &lines,
                        QVector<QVector<Span>> &out) const;
    static QColor color(int rule);
    static const char *styleName(int rule); // "keyword", "string", ...

signals:
    void updated();

private:
    struct Language;
    static const Language *languageFor(const QString &suffix);
    void schedule(const core::PieceTable::Snapshot &snapshot);
    void onParsed(std::shared_ptr<TSTree> tree, int generation);
    void setTree(TSTree *tree);

    const Language *m_lang = nullptr;
    TSTree *m_tree = nullptr;
    bool m_busy = false;
    QVector<core::TextBuffer::Edit> m_editsDuringParse;
    std::optional<core::PieceTable::Snapshot> m_latest;
    std::atomic<int> m_generation{0};
    QThreadPool m_pool;
};
