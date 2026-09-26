#include <QGuiApplication>
#include <QFileInfo>
#include <QQuickWindow>
#include <QTimer>
#include <string>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include "src/services/logger.h"

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

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
    initLogging();
    qInstallMessageHandler(qtMessageHandler);
    spdlog::info("main: старт");

    QQmlApplicationEngine engine;
    spdlog::info("main: engine создан");

    // Путь к файлу можно передать аргументом командной строки
    // (appMyCodeApp.exe C:/path/to/file) — файл грузится при старте.
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
    spdlog::info("main: QML загружен, корневых объектов: {}",
                 std::to_string(roots.size()));

    if (!startPath.isEmpty() && !roots.isEmpty()) {
        if (auto *manager = roots.first()->findChild<QObject *>(QStringLiteral("manager"))) {
            const QUrl startUrl = QUrl::fromLocalFile(
                QFileInfo(startPath).absoluteFilePath());
            int tabIndex = -1;
            QMetaObject::invokeMethod(manager, "open", Q_RETURN_ARG(int, tabIndex),
                                      Q_ARG(QUrl, startUrl));
            spdlog::info("main: open() из аргумента, индекс таба: {}",
                         std::to_string(tabIndex));
        } else {
            spdlog::error("main: manager не найден в сцене");
        }
    }

    // Смоук-режим самотеста: MYCODEAPP_SHOT=<путь.png> — открыть окно,
    // снять скриншот и выйти. Используется для автоматической проверки.
    // MYCODEAPP_AUTOCLOSE=1 — закрыть таб за 1 с до скриншота (тест очистки).
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
            QTimer::singleShot(2000, win, [win, shotPath]() {
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