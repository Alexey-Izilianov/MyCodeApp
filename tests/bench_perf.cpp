// Повторные замеры производительности ядра (критерии ТЗ, раздел 4.1).
// Запуск: bench.exe <файл-лог>
#include <QCoreApplication>
#include <QFileInfo>
#include <QElapsedTimer>
#include <QFile>
#include <QRegularExpression>
#include <QThread>
#include <cstdio>

#include "src/core/mappedtext.h"
#include "src/core/piecetable.h"
#include "src/core/textbuffer.h"
#include "src/services/searchengine.h"

using namespace core;

static void report(const char *what, double ms)
{
    std::fprintf(stdout, "%-52s %8.1f ms\n", what, ms);
    std::fflush(stdout);
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    const QString path = argc > 1 ? QString::fromLocal8Bit(argv[1])
                                  : QStringLiteral("C:/Project/MyCodeApp/testdata/big.log");

    // Пробные цели поиска берём из первой строки файла — гарантированно есть
    QFile first(path);
    first.open(QIODevice::ReadOnly);
    const QByteArray line1 = first.readLine();
    const QString probe = QString::fromUtf8(line1).trimmed();
    const QStringList tokens = probe.split(u' ');
    QString needlePlain, needleWord;
    for (const QString &tok : tokens) {
        if (tok.size() >= 4 && needlePlain.isEmpty())
            needlePlain = tok;
        if (tok.size() >= 4 && tok.at(0).isLetter() && needleWord.isEmpty())
            needleWord = tok;
    }
    if (needlePlain.isEmpty())
        needlePlain = probe.left(qMin(6, int(probe.size())));
    if (needleWord.isEmpty())
        needleWord = needlePlain;
    const QByteArray np = needlePlain.toLocal8Bit();
    const QByteArray nw = needleWord.toLocal8Bit();

    const bool giant = QFileInfo(path).size() > 250 * 1024 * 1024;
    std::fprintf(stdout, "%-52s %s\n", "файл", qPrintable(path));
    std::fflush(stdout);

    // ── 1. Обычная загрузка: чтение + декодирование (файлы < 256 МБ) ──
    // Для ≥256 МБ не выполняется: приложение грузит их через mmap (шаг 2)
    if (!giant) {
        QElapsedTimer t; t.start();
        TextBuffer buffer;
        if (!buffer.load(path)) { std::fprintf(stderr, "load failed\n"); return 1; }
        report("обычная загрузка 131 МБ (чтение + decode) [тз <1000 мс]", double(t.elapsed()));
    }

    // ── 2. mmap-загрузка (файлы от 256 МБ идут этим путём) ──
    std::shared_ptr<const MappedText> mapped;
    {
        QElapsedTimer t; t.start();
        mapped = MappedText::open(path, true);
        if (!mapped) { std::fprintf(stderr, "map failed\n"); return 1; }
        report("mmap-загрузка (скан newlines)", double(t.elapsed()));
    }

    // ── 3. Ввод на mmap-тексте: 1000 вставок по одной строке ──
    {
        PieceTable pt;
        pt.reset(mapped);
        QElapsedTimer t; t.start();
        const qint64 step = pt.length() / 1000;
        for (int i = 999; i >= 0; --i)
            pt.insert(int(step * i), QStringLiteral("[правка]"));
        report("ввод: 1000 вставок поверх mmap-текста", double(t.elapsed()));
        std::fprintf(stdout, "%-52s %8.3f ms/вставка [критерий <16 мс]\n",
                     "  средне на правку", double(t.elapsed()) / 1000.0);
        pt.verify();
    }

    // ── 4. Поиск по снимку piece table: plain / whole word / regex ──
    {
        PieceTable pt;
        pt.reset(mapped);
        SearchEngine engine;
        const auto snap = pt.snapshot();
        QVector<SearchEngine::Match> matches;
        QObject::connect(&engine, &SearchEngine::resultsReady,
                         [&matches](const QVector<SearchEngine::Match> &m) { matches = m; });

        QElapsedTimer t; t.start();
        engine.start(snap, needlePlain, Qt::CaseInsensitive, SearchEngine::Mode::Plain);
        while (matches.isEmpty() && t.elapsed() < 5000)
            QCoreApplication::processEvents();
        report("поиск plain по снимку [тз <500 мс на 100 МБ]", double(t.elapsed()));
        std::fprintf(stdout, "%-52s %8d совпадений по «%s»\n", "  найдено", matches.size(), np.constData());

        matches.clear(); t.restart();
        engine.start(snap, needleWord, Qt::CaseSensitive, SearchEngine::Mode::WholeWord);
        while (matches.isEmpty() && t.elapsed() < 5000)
            QCoreApplication::processEvents();
        report("поиск целых слов по снимку", double(t.elapsed()));
        std::fprintf(stdout, "%-52s %8d совпадений по «%s»\n", "  найдено", matches.size(), nw.constData());

        matches.clear(); t.restart();
        const QRegularExpression anyCharDigit(".+");
        engine.start(snap, needlePlain.left(3) + QStringLiteral(".+"), Qt::CaseSensitive,
                     SearchEngine::Mode::Regex);
        while (matches.isEmpty() && t.elapsed() < 10000)
            QCoreApplication::processEvents();
        report("поиск regex по снимку (слово.+)", double(t.elapsed()));
        std::fprintf(stdout, "%-52s %8d совпадений\n", "  найдено", matches.size());
    }

    // ── 5. Потоковое сохранение (путь файлов ≥256 МБ): правка → temp ──
    {
        PieceTable pt;
        pt.reset(mapped);
        for (int offset = 0; offset < 10 * (1 << 20); offset += 1 << 20)
            pt.insert(offset, QStringLiteral("маркер правки\n"));
        const auto snap = pt.snapshot();

        const QString temp = path + QStringLiteral(".bench-save");
        QElapsedTimer t; t.start();
        QString err;
        if (!streamSave(snap, Encoding::Utf8, LineEnding::Lf, false, temp, {}, &err)) {
            std::fprintf(stderr, "save failed: %s\n", qPrintable(err));
            return 1;
        }
        const double write = double(t.elapsed());
        auto newBase = MappedText::open(temp, true);
        report("потоковая запись (10 добавок) в temp", write);
        report("скан записанного файла (после записи)", double(t.elapsed()) - write);
        std::fprintf(stdout, "%-52s %8.1f MB/s\n", "  скорость записи",
                     double(QFileInfo(path).size() >> 20) * 1000.0 / write);
        QFile::remove(temp);
    }
    return 0;
}
