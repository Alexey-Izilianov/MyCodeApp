#include <QGuiApplication>
#include <QFileInfo>
#include <QQuickWindow>
#include <QTimer>
#include <string>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include "src/services/logger.h"
#include "src/services/appsettings.h"
#include "src/services/localization.h"
#include "src/services/singleinstance.h"

// Свой обработчик сообщений Qt: дефолтный на Windows для GUI-приложения
// (WIN32_EXECUTABLE, нет консоли) шлёт qDebug/qWarning в OutputDebugString —
// их видно только из отладчика. Дублируем в spdlog.
static void qtMessageHandler(QtMsgType type, const QMessageLogContext &,
                             const QString &msg)
{
    switch (type) {
    case QtWarningMsg:  spdlog::warn("qt: {}", msg.toStdString()); break;
    case QtCriticalMsg: spdlog::error("qt: {}", msg.toStdString()); break;
    case QtFatalMsg:    spdlog::critical("qt: {}", msg.toStdString()); break;
    default:            spdlog::info("qt: {}", msg.toStdString()); break;
    }
}

// Файлы — вкладками, папка — проектом (функция openFolder в Main.qml)
static void openPaths(QObject *root, const QStringList &paths)
{
    auto *manager = root->findChild<QObject *>(QStringLiteral("manager"));
    for (const QString &path : paths) {
        const QFileInfo info(path);
        const QUrl url = QUrl::fromLocalFile(info.absoluteFilePath());
        if (info.isDir()) {
            QVariant opened;
            QMetaObject::invokeMethod(root, "openFolder", Q_RETURN_ARG(QVariant, opened), Q_ARG(QVariant, url));
        } else if (manager) {
            int tab = -1;
            QMetaObject::invokeMethod(manager, "open", Q_RETURN_ARG(int, tab), Q_ARG(QUrl, url));
        }
    }
}

static void bringToFront(QQuickWindow *window)
{
    if (window->windowStates() & Qt::WindowMinimized)
        window->setWindowStates(window->windowStates() & ~Qt::WindowMinimized);
    window->show();
    window->raise();
    window->requestActivate();
}

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
    QGuiApplication::setApplicationVersion(QStringLiteral(APP_VERSION));

    // appMyCodeApp.exe [--profile имя] [файл|папка]... — пути абсолютные: у запущенного
    // экземпляра своя рабочая папка
    QStringList paths = QCoreApplication::arguments().mid(1);
    QString profile;
    if (const qsizetype at = paths.indexOf(QStringLiteral("--profile")); at >= 0 && at + 1 < paths.size()) {
        profile = paths.at(at + 1);
        paths.remove(at, 2);
    }
    for (QString &path : paths)
        path = QFileInfo(path).absoluteFilePath();
    initAppDataDir(profile);

    // Повторный запуск отдаёт пути открытому окну. До initLogging: тот обнуляет общий лог.
    // Смоук-запуски (MYCODEAPP_SHOT) всегда отдельные
    SingleInstance instance;
    if (qEnvironmentVariableIsEmpty("MYCODEAPP_SHOT")) {
        if (instance.forwardToRunning(paths))
            return 0;
        instance.listen();
    }

    initLogging();
    qInstallMessageHandler(qtMessageHandler);
    spdlog::info("main: старт");
    Localization::applySavedLanguage();

    QQmlApplicationEngine engine;
    spdlog::info("main: engine создан");

    QObject::connect(
        &engine,
        &QQmlApplicationEngine::objectCreationFailed,
        &app,
        []() { QCoreApplication::exit(-1); },
        Qt::QueuedConnection);

    engine.loadFromModule("MyCodeApp", "Main");

    const auto roots = engine.rootObjects();
    spdlog::info("main: QML загружен, корневых объектов: {}",
                 std::to_string(roots.size()));

    if (!roots.isEmpty()) {
        QObject *root = roots.first();
        openPaths(root, paths);
        QObject::connect(&instance, &SingleInstance::pathsReceived, root, [root](const QStringList &received) {
            openPaths(root, received);
            if (auto *window = qobject_cast<QQuickWindow *>(root))
                bringToFront(window);
        });
    }

    // Смоук-режим самотеста: MYCODEAPP_SHOT=<путь.png> — открыть окно,
    // снять скриншот и выйти. Используется для автоматической проверки.
    // MYCODEAPP_AUTOCLOSE=1 — закрыть таб за 1 с до скриншота (тест очистки).
    // MYCODEAPP_SHOT_DELAY=<мс> — снимать позже (долгая загрузка), по умолчанию 2000.
    const QByteArray shotPath = qgetenv("MYCODEAPP_SHOT");
    if (qgetenv("MYCODEAPP_AUTOCLOSE") == "1" && !roots.isEmpty()) {
        if (auto *manager = roots.first()->findChild<QObject *>(QStringLiteral("manager"))) {
            QTimer::singleShot(1000, manager, [manager]() {
                int dummy = -1;
                QMetaObject::invokeMethod(manager, "close",
                                          Q_ARG(int, 0));
                Q_UNUSED(dummy);
            });
        }
    }
    if (!shotPath.isEmpty() && !roots.isEmpty()) {
        if (auto *win = qobject_cast<QQuickWindow *>(roots.first())) {
            const int delay = qEnvironmentVariableIntValue("MYCODEAPP_SHOT_DELAY");
            QTimer::singleShot(delay > 0 ? delay : 2000, win, [win, shotPath]() {
                const QImage image = win->grabWindow();
                if (image.save(QString::fromLocal8Bit(shotPath)))
                    spdlog::info("main: скриншот сохранён: {}",
                                 QString::fromLocal8Bit(shotPath).toStdString());
                else
                    spdlog::error("main: скриншот не сохранился");
                QCoreApplication::exit(0);
            });
        }
    }

    return QGuiApplication::exec();
}