#include "localmodel.h"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QMutex>
#include <QSemaphore>
#include <QThread>
#include <QFileInfo>

#include <condition_variable>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>

#include "llama.h"
#include "src/services/appsettings.h"

namespace {

// Шаблон чата Qwen3 на случай, когда в GGUF нет меты с шаблоном
constexpr char kFallbackTemplate[] = "{% for message in messages %}"
    "{{- '<|im_start|>' + message['role'] + '\n' + message['content'] + '<|im_end|>' + '\n' }}"
    "{%- endfor %}{%- if add_generation_prompt %}{{- '<|im_start|>assistant\n' }}{% endif %}";

// Потоков под модель — половина ядер: интерфейс не замерзает при генерации
int modelThreads()
{
    const int cores = QThread::idealThreadCount();
    return qMax(1, cores / 2);
}

QString templateOf(const llama_model *model)
{
    char buffer[4096] = {};
    const int32_t size = llama_model_meta_val_str(model, "tokenizer.chat_template", buffer, sizeof(buffer));
    if (size > 0)
        return QString::fromUtf8(buffer, qMin<int32_t>(size, sizeof(buffer) - 1));
    return {};
}

} // namespace

struct LocalModel::Worker {
    struct Job {
        Request request;
        std::function<bool(const QString &)> onToken;
        std::function<void(const QString &, int, bool)> onDone;
    };

    // Вся работа с llama.cpp — только в этом потоке
    llama_model *model = nullptr;
    llama_context *context = nullptr;
    QString modelPath;
    int contextSize = 0;
    int maxTokensCap = 2048;

    std::mutex mutex;
    std::condition_variable wake;
    Job pending;
    bool quit = false;

    std::thread thread;

    void start()
    {
        thread = std::thread([this] { run(); });
    }

    void stop()
    {
        {
            std::lock_guard lock(mutex);
            quit = true;
        }
        wake.notify_all();
        if (thread.joinable())
            thread.join();
    }

    void unload()
    {
        if (context)
            llama_free(context);
        context = nullptr;
        if (model)
            llama_model_free(model);
        model = nullptr;
    }

    // Загрузка модели; nullptr — не удалось (ошибка в loadError)
    llama_model *ensureModel(const QString &path, int ctxSize, QString *error)
    {
        if (model && modelPath == path && contextSize == ctxSize)
            return model;
        unload();
        llama_model_params params = llama_model_default_params();
        params.n_gpu_layers = 0; // только CPU (встроенная модель)
        llama_model *loaded = llama_model_load_from_file(QFile::encodeName(path).constData(), params);
        if (!loaded) {
            if (error)
                *error = QCoreApplication::translate("LocalModel",
                    "Не удалось открыть файл модели: %1").arg(path);
            return nullptr;
        }
        if (error)
            error->clear();
        model = loaded;
        modelPath = path;
        contextSize = ctxSize;
        return model;
    }

    llama_context *ensureContext(int ctxSize, QString *error)
    {
        llama_context_params params = llama_context_default_params();
        params.n_ctx = ctxSize;
        params.n_batch = 512; // промпт до сотен токенов за один вызов decode
        params.n_threads = modelThreads();
        params.n_threads_batch = modelThreads();
        llama_context *created = llama_init_from_model(model, params);
        if (!created) {
            if (error)
                *error = QCoreApplication::translate("LocalModel",
                    "Не хватает памяти для контекста (%1 токенов)").arg(ctxSize);
            return nullptr;
        }
        return created;
    }

    void run()
    {
        for (;;)
        {
            Job job;
            {
                std::unique_lock lock(mutex);
                wake.wait(lock, [&] { return quit || pending.onDone != nullptr; });
                if (quit)
                    break;
                job = std::move(pending);
                pending.onDone = nullptr; // забрали
            }

            QString error;
            llama_model *m = ensureModel(modelPath.isEmpty() ? job.request.modelPathOverride
                                                              : modelPath,
                                         job.request.contextSize, &error);
            // modelPath задаёт загрузчик перед постановкой задачи — см. generate()
            if (!m) {
                job.onDone(error, 0, false);
                continue;
            }
            llama_context *ctx = ensureContext(job.request.contextSize, &error);
            if (!ctx) {
                job.onDone(error, 0, false);
                continue;
            }

            // Шаблон чата из модели (или ChatML-фоллбек) → единый промпт
            const QString tmpl = templateOf(m);
            std::string prompt;
            {
                std::vector<llama_chat_message> msgs;
                std::vector<std::string> storage;
                for (const auto & [role, text] : job.request.messages) {
                    storage.emplace_back(role.toStdString());
                    storage.emplace_back(text.toStdString());
                }
                for (size_t i = 0; i < storage.size(); i += 2)
                    msgs.push_back({storage[i].c_str(), storage[i + 1].c_str()});
                std::vector<char> buf(int(prompt.capacity() + 1), 0);
                int32_t formed = llama_chat_apply_template(tmpl.isEmpty() ? kFallbackTemplate
                                                                         : tmpl.toStdString().c_str(),
                                                          msgs.data(), int(msgs.size()), true, buf.data(), int(buf.size()));
                if (formed > int(buf.size())) { // шаблон сам посчитал размер
                    buf.resize(formed + 1);
                    formed = llama_chat_apply_template(tmpl.isEmpty() ? kFallbackTemplate
                                                                     : tmpl.toStdString().c_str(),
                                                      msgs.data(), int(msgs.size()), true, buf.data(), int(buf.size()));
                }
                if (formed < 0) {
                    job.onDone(QCoreApplication::translate("LocalModel", "Не удалось собрать промпт чата"), 0, false);
                    continue;
                }
                prompt.assign(buf.data(), size_t(formed));
            }

            // Токены промпта (без спец-токенов: шаблон даёт всё явно). Первый
            // вызов с пустым буфером возвращает число потребных токенов
            // со знаком минус — снимаем его.
            const llama_vocab *vocab = llama_model_get_vocab(m);
            const int counted = llama_tokenize(vocab, prompt.c_str(), int(prompt.size()),
                                               nullptr, 0, false, true);
            const int need = counted < 0 ? -counted : counted;
            if (need <= 0) {
                job.onDone(QCoreApplication::translate("LocalModel", "Не удалось токенизировать запрос"), 0, false);
                continue;
            }
            std::vector<llama_token> tokens;
            tokens.resize(size_t(need));
            if (llama_tokenize(vocab, prompt.c_str(), int(prompt.size()), tokens.data(), need, false, true) != need) {
                job.onDone(QCoreApplication::translate("LocalModel", "Не удалось токенизировать запрос"), 0, false);
                continue;
            }
            llama_memory_clear(llama_get_memory(ctx), -1);
            llama_sampler_chain_params sp = llama_sampler_chain_default_params();
            llama_sampler *sampler = llama_sampler_chain_init(sp);
            llama_sampler_chain_add(sampler, llama_sampler_init_min_p(0.05f, 1));
            llama_sampler_chain_add(sampler, llama_sampler_init_temp(float(job.request.temperature)));
            llama_sampler_chain_add(sampler, llama_sampler_init_dist(0));

            const auto report = [&](const QString &s) -> bool {
                if (job.request.cancel && job.request.cancel->load())
                    return false;
                QString piece = s;
                return job.onToken(piece);
            };

            // Промпт батчами по 512 (n_batch); нештатно длинный — подрезаем слева
            const int allowed = int(llama_n_ctx(ctx)) - 2 - job.request.maxTokens;
            const int take = qMax(1, qMin(need, allowed));
            const int skipped = need - take;
            bool decodeError = false;
            for (int i = skipped; i < need && !decodeError; i += 512) {
                const int count = qMin(512, need - i);
                const int rc = llama_decode(ctx, llama_batch_get_one(tokens.data() + i, count));
                if (rc != 0)
                    decodeError = true;
            }
            if (decodeError) {
                llama_sampler_free(sampler);
                job.onDone(QCoreApplication::translate("LocalModel",
                    "Не удалось обработать запрос (мало контекста?)"), 0, false);
                continue;
            }

            bool stopped = false;
            int produced = 0;
            for (;;) {
                if (job.request.cancel && job.request.cancel->load()) {
                    stopped = true;
                    break;
                }
                const llama_token next = llama_sampler_sample(sampler, ctx, -1);
                if (llama_vocab_is_eog(vocab, next))
                    break;
                char piece[64] = {};
                const int n = llama_token_to_piece(vocab, next, piece, sizeof(piece), 0, true);
                if (n > 0 && !report(QString::fromUtf8(piece, n)))
                    stopped = true; // получатель просил остановиться
                ++produced;
                if (produced >= job.request.maxTokens)
                    break;
                llama_token feed = next;
                if (llama_decode(ctx, llama_batch_get_one(&feed, 1)) != 0)
                    break; // контекст переполнен — закончили
            }
            llama_sampler_free(sampler);
            job.onDone({}, produced, stopped);
        }
        unload();
    }
};

LocalModel::LocalModel(QObject *parent)
    : QObject(parent)
    , m_worker(std::make_unique<Worker>())
{
    m_worker->start();
}

LocalModel::~LocalModel()
{
    m_worker->stop();
}

QString LocalModel::modelDirectory()
{
    return QCoreApplication::applicationDirPath() + QStringLiteral("/models");
}

QString LocalModel::modelFile()
{
    const QString name = QStringLiteral("Qwen3-4B-Instruct-2507-Q4_K_M.gguf");
    const QStringList candidates = {
        qEnvironmentVariable("MCA_MODEL"),
        modelDirectory() + u'/' + name,
        appDataDir() + QStringLiteral("/models/") + name,
    };
    for (const QString &candidate : candidates)
        if (!candidate.isEmpty() && QFileInfo::exists(candidate))
            return candidate;
    return {};
}

void LocalModel::generate(const Request &request,
                          std::function<bool(const QString &)> onToken,
                          std::function<void(const QString &, int, bool)> onDone)
{
    Worker::Job job;
    job.request = request;
    job.request.modelPathOverride = modelFile();
    job.onToken = [this, onToken = std::move(onToken)](const QString &piece) {
        bool keep = false;
        QMetaObject::invokeMethod(this, [&keep, onToken, piece] { keep = onToken(piece); }, Qt::BlockingQueuedConnection);
        return keep;
    };
    job.onDone = [this, onDone = std::move(onDone)](const QString &error, int tokens, bool stopped) {
        QMetaObject::invokeMethod(this, [onDone, error, tokens, stopped] { onDone(error, tokens, stopped); },
                                  Qt::QueuedConnection);
    };
    std::lock_guard lock(m_worker->mutex);
    m_worker->pending = std::move(job);
    m_worker->wake.notify_all();
}
