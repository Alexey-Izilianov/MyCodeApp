#pragma once

#include <QObject>
#include <QString>
#include <atomic>
#include <functional>
#include <memory>
#include <vector>

// Встроенная локальная модель (llama.cpp): загрузка GGUF и потоковая генерация
// чата без внешних серверов. Модель загружается лениво, при первом запросе, и
// остаётся в памяти. Вся тяжёлая работа — в одном рабочем потоке; генерации не
// пересекаются: новая задача отменяет предыдущую.
class LocalModel : public QObject {
    Q_OBJECT
public:
    explicit LocalModel(QObject *parent = nullptr);
    ~LocalModel() override;

    // Где приложение ищет файл модели (первый существующий): рядом с программой
    // (models/), затем в папке данных (models/); переменная окружения MCA_MODEL
    // перекрывает. Пусто — модели нет.
    static QString modelFile();
    static QString modelDirectory(); // каталог модели рядом с программой

    struct Request {
        QString modelPathOverride; // задаёт generate() перед постановкой в очередь
        std::vector<std::pair<QString, QString>> messages; // {роль, текст}
        int maxTokens = 1024;
        double temperature = 0.2;
        int contextSize = 4096;
        std::shared_ptr<std::atomic_bool> cancel;
    };

    // Потоковая генерация. Обратные вызовы — всегда в GUI-потоке.
    // onDone: error пуст — успех; stopped — прервано отменой.
    void generate(const Request &request,
                  std::function<bool(const QString &piece)> onToken,
                  std::function<void(const QString &error, int outputTokens, bool stopped)> onDone);

signals:
    // Смена состояния модели: "loading" (загрузка файла), "ready", "error"
    void stateChanged(const QString &state, const QString &text);

private:
    struct Worker;
    std::unique_ptr<Worker> m_worker;
};
