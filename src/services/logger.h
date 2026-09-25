#pragma once

#include <QDir>
#include <QStandardPaths>
#include <spdlog/spdlog.h>
#include <spdlog/sinks/basic_file_sink.h>

// Единый лог приложения: %LOCALAPPDATA%/appMyCodeApp/app.log.
// Вызывается один раз из main() после создания QApplication.
inline void initLogging()
{
    static bool inited = false;
    if (inited)
        return;
    inited = true;
    const QString dir = QStandardPaths::writableLocation(
        QStandardPaths::AppLocalDataLocation);
    QDir().mkpath(dir);
    const std::string file = (dir + QStringLiteral("/app.log")).toStdString();
    auto logger = spdlog::basic_logger_mt("app", file, true);
    spdlog::set_default_logger(logger);
    spdlog::flush_on(spdlog::level::info); // GUI-лог важно видеть сразу после падения
    spdlog::set_level(spdlog::level::info);
}