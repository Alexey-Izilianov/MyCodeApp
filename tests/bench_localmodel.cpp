// Замер генерации встроенной модели llama.cpp. Модель ищется как в приложении
// (рядом с программой / в папке данных / MCA_MODEL); если её нет — только
// проверка отчёта об ошибке. Запуск: bench_localmodel.exe
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <cstdio>

#include "src/assistant/localmodel.h"

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);

    LocalModel model;
    const QString file = LocalModel::modelFile();
    std::fprintf(stdout, "модель: %s\n", file.isEmpty() ? "НЕ НАЙДЕНА" : qPrintable(file));

    QString text;
    QString error;
    int tokens = 0;
    qint64 firstMs = -1;
    QElapsedTimer clock;
    clock.start();

    LocalModel::Request request;
    request.contextSize = 4096;
    request.maxTokens = 64;
    request.messages = {{"user", "Ответь одним словом без пояснений: работает ли встроенная модель?"}};
    QObject::connect(&model, &LocalModel::stateChanged, [&](const QString &state, const QString &msg) {
        std::fprintf(stdout, "  состояние: %s %s\n", qPrintable(state), qPrintable(msg));
        std::fflush(stdout);
    });

    QEventLoop loop;
    model.generate(request,
        [&](const QString &piece) {
            if (firstMs < 0) {
                firstMs = clock.elapsed();
                std::fprintf(stdout, "  первый токен: %lld мс\n", firstMs);
            }
            text += piece;
            return true;
        },
        [&](const QString &err, int output, bool stopped) {
            error = err;
            tokens = output;
            loop.quit();
        });
    loop.exec();

    if (!error.isEmpty()) {
        std::fprintf(stdout, "ошибка: %s\n", qPrintable(error));
        return file.isEmpty() ? 0 : 1; // без модели ошибка — норма
    }
    std::fprintf(stdout, "ответ (%d токенов за %lld мс): %s\n", tokens, clock.elapsed(), qPrintable(text.simplified()));
    return 0;
}
