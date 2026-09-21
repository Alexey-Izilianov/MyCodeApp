#include <QGuiApplication>
#include <QFileInfo>
#include <string>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include "textspikeiten.h" // spikeLog()

// Свой обработчик сообщений Qt: дефолтный на Windows для GUI-приложения
// (WIN32_EXECUTABLE, нет консоли) шлёт qDebug/qWarning в OutputDebugString —
// их видно только из отладчика. Дублируем в stderr на всякий случай.
static void spikeMessageHandler(QtMsgType type, const QMessageLogContext &, const QString &msg)
{
    const char *tag = type == QtWarningMsg ? "WARN"
                    : type == QtCriticalMsg ? "CRIT"
                    : type == QtFatalMsg    ? "FATAL" : "LOG";
    spikeLog(std::string("[") + tag + "] " + msg.toStdString());
}

int main(int argc, char *argv[])
{
    spikeLog("main: старт");

    QGuiApplication app(argc, argv);
    spikeLog("main: QGuiApplication создан");
    qInstallMessageHandler(spikeMessageHandler);

    QQmlApplicationEngine engine;
    spikeLog("main: engine создан");

    // Спайк: путь к файлу можно передать аргументом командной строки
    // (appMyCodeApp.exe C:/path/to/file) — тогда файл грузится при старте.
    // В QML-цепочку (context property + onCompleted) не верим: после загрузки
    // сцены выставляем свойство filePath прямо в C++-объект редактора.
    const QString startPath = argc > 1 ? QString::fromLocal8Bit(argv[1]) : QString();

    QObject::connect(
        &engine,
        &QQmlApplicationEngine::objectCreationFailed,
        &app,
        []() { QCoreApplication::exit(-1); },
        Qt::QueuedConnection);

    engine.loadFromModule("MyCodeApp", "Main");

    const auto roots = engine.rootObjects();
    spikeLog("main: QML загружен, корневых объектов: " + std::to_string(roots.size()));

    if (!startPath.isEmpty() && !roots.isEmpty()) {
        if (auto *manager = roots.first()->findChild<QObject *>(QStringLiteral("manager"))) {
            const QUrl startUrl = QUrl::fromLocalFile(
                QFileInfo(startPath).absoluteFilePath());
            int tabIndex = -1;
            QMetaObject::invokeMethod(manager, "open", Q_RETURN_ARG(int, tabIndex),
                                      Q_ARG(QUrl, startUrl));
            spikeLog("main: open() из аргумента, индекс таба: " + std::to_string(tabIndex));
        } else {
            spikeLog("main: ОШИБКА — manager не найден в сцене");
        }
    }

    return QGuiApplication::exec();
}