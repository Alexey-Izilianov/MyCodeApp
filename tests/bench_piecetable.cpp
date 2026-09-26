// Бенчмарк piece table (M2-1). Критерий ТЗ: вставка в середину 100 МБ < 50 мс.
// Запуск: bench_piecetable.exe [файл]; по умолчанию testdata/big.log (131 МБ).
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QStringList>
#include <cstdio>

#include "src/core/piecetable.h"

using core::PieceTable;

namespace {

void report(const char *what, qint64 ms)
{
    std::fprintf(stdout, "%-42s %6lld ms\n", what, ms);
    std::fflush(stdout);
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    const QString path = argc > 1
        ? QString::fromLocal8Bit(argv[1])
        : QStringLiteral("C:/Projects/MyCodeApp/testdata/big.log");

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        std::fprintf(stderr, "cannot open: %s\n", qPrintable(path));
        return 1;
    }
    const QByteArray raw = file.readAll();
    const qint64 mb = raw.size() / (1024 * 1024);

    QElapsedTimer timer;
    timer.start();
    const QString text = QString::fromUtf8(raw);
    report("decode fromUtf8", timer.elapsed());

    PieceTable pt;
    timer.restart();
    pt.reset(text);
    report("reset (build line index)", timer.elapsed());

    std::fprintf(stdout, "size: %lld MB, chars: %d, lines: %d, pieces: %d\n",
                 mb, text.size(), pt.lineCount(), pt.pieceCount());
    std::fflush(stdout);

    // Критерий ТЗ: вставка в середину < 50 мс
    const int mid = pt.length() / 2;
    const QString stub = QStringLiteral("hello ");
    constexpr int kEdits = 200;
    timer.restart();
    for (int i = 0; i < kEdits; ++i)
        pt.insert(mid, stub);
    const qint64 insertTotal = timer.elapsed();
    std::fprintf(stdout, "insert mid x%d: avg %.3f ms (total %lld ms)\n",
                 kEdits, double(insertTotal) / kEdits, insertTotal);
    std::fflush(stdout);

    timer.restart();
    for (int i = 0; i < kEdits; ++i)
        pt.remove(mid, stub.size());
    const qint64 removeTotal = timer.elapsed();
    std::fprintf(stdout, "remove mid x%d: avg %.3f ms (total %lld ms)\n",
                 kEdits, double(removeTotal) / kEdits, removeTotal);
    std::fflush(stdout);

    // Чтение строк — как в рендере: 2000 случайных + типовой кадр (40 строк)
    const int lines = pt.lineCount();
    quint32 seed = 42;
    volatile int sink = 0;
    timer.restart();
    for (int k = 0; k < 2000; ++k) {
        seed = seed * 1664525u + 1013904223u;
        const int line = int((seed >> 8) % quint32(lines));
        sink += pt.line(line).size();
    }
    const qint64 lineTotal = timer.elapsed();
    std::fprintf(stdout, "lineAt random x2000: avg %.4f ms, frame(40) ~%.3f ms\n",
                 double(lineTotal) / 2000.0, double(lineTotal) / 2000.0 * 40.0);
    std::fflush(stdout);

    timer.restart();
    const QString all = pt.toString();
    report("toString", timer.elapsed());
    std::fprintf(stdout, "toString size: %d\n", all.size());

    timer.restart();
    const PieceTable::Snapshot snap = pt.snapshot();
    std::fprintf(stdout, "snapshot: %lld us\n", timer.nsecsElapsed() / 1000);

    timer.restart();
    const QStringList materialized = snap.lines();
    report("snapshot lines() (search path)", timer.elapsed());
    std::fprintf(stdout, "materialized lines: %d\n", materialized.size());

    // Длинная серия правок: рост кусков (амортизация)
    timer.restart();
    for (int i = 0; i < 5000; ++i) {
        pt.insert(mid, stub);
        pt.remove(mid, stub.size());
    }
    std::fprintf(stdout, "insert+remove x5000: %lld ms, pieces now: %d\n",
                 timer.elapsed(), pt.pieceCount());
    std::fflush(stdout);

    return 0;
}