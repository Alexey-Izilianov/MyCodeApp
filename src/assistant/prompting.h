#pragma once

#include <QByteArray>
#include <QHash>
#include <QString>
#include <QStringList>
#include <QVector>
#include <deque>
#include <functional>

// Чистая логика ассистента, без сети и интерфейса: разбор потокового ответа сервера
// модели, подготовка фрагментов кода и журналов под размер контекста, место для
// документирующего комментария
namespace assistant {

// Сколько символов текста примерно помещается в токен (код и русский текст у Qwen)
inline constexpr int kCharsPerToken = 3;

// Протокол сервера модели
enum class Protocol {
    Ollama, // /api/chat, ответ — JSON по строке на кусок
    OpenAi, // /v1/chat/completions (llama.cpp server, vLLM, LM Studio…), ответ — SSE
};

struct Delta {
    QString content;
    QString thinking; // рассуждение модели (Qwen3 Thinking, DeepSeek-R1) — не показываем
    QString error;
    bool done = false;
    int promptTokens = -1; // известны только в конце (Ollama, llama.cpp)
    int outputTokens = -1;
};

// Ответ приходит кусками произвольной длины — копит неполные строки между вызовами
class StreamParser {
public:
    explicit StreamParser(Protocol protocol) : m_protocol(protocol) {}
    QVector<Delta> feed(const QByteArray &data);
    // Ответ закончился: последняя строка без перевода строки
    QVector<Delta> finish();

private:
    void parseLine(const QByteArray &line, QVector<Delta> *out) const;

    Protocol m_protocol;
    QByteArray m_pending;
};

// Текст ошибки из тела ответа с ошибкой HTTP: {"error": "..."} или {"error": {"message": "..."}}
QString errorFromBody(const QByteArray &body);

struct Excerpt {
    QString text;
    int firstLine = 0; // номер первой строки фрагмента (с 0)
    int lastLine = 0;
    bool truncated = false; // фрагмент — не весь текст
};

// Строки [from, to] целиком, если влезают в budget символов; иначе — окно вокруг
// центральной строки center (расширяется поровну вверх и вниз)
Excerpt excerpt(const std::function<QString(int)> &lineAt, int from, int to, int center, int budget);

// Выжимка большого журнала: строки с ошибками и предупреждениями (повторы с разными
// числами схлопываются со счётчиком) и конец журнала. Строки подаются по порядку
class LogDigest {
public:
    explicit LogDigest(int budget) : m_budget(budget) {}
    void add(const QString &line);
    QString text() const;
    int lineCount() const { return m_lines; }
    int errorCount() const { return m_errors; }
    int warningCount() const { return m_warnings; }

private:
    struct Notable {
        int line = 0;
        int repeats = 1;
        QString text;
    };
    int m_budget;
    int m_lines = 0;
    int m_errors = 0;
    int m_warnings = 0;
    QVector<Notable> m_notable;
    QHash<QString, int> m_byShape; // строка без чисел -> индекс в m_notable
    int m_notableChars = 0;
    bool m_notableFull = false;
    std::deque<QString> m_tail;
    int m_tailChars = 0;
    QString m_head; // начало журнала: кто и когда запустился
};

// Первый блок кода ``` из ответа в Markdown; без блоков — весь ответ без пробелов по краям
QString firstCodeBlock(const QString &markdown);

// Сообщение коммита из ответа модели: без обёртки в блок кода и кавычек
QString cleanCommitMessage(const QString &answer);

// Куда вставить документирующий комментарий к выделенному коду: перед первой непустой
// строкой с её отступом; в Python — docstring сразу после заголовка def/class
struct Placement {
    int line = 0;   // вставка в начало этой строки
    QString text;   // с переводом строки в конце
};
Placement placeComment(const QStringList &lines, int firstLine, const QString &language,
                       const QString &comment);

} // namespace assistant
