#pragma once

// API плагинов MyCodeApp. Плагин — Qt-плагин (DLL), собранный тем же
// компилятором и Qt, что и редактор. Описание лежит в JSON внутри DLL
// (Q_PLUGIN_METADATA ... FILE "plugin.json"):
//   {"id": "texttools", "name": "...", "version": "1.0", "description": "...", "author": "..."}
// Редактор читает его, не загружая DLL. Версия API — часть IID: при
// несовместимых изменениях меняется номер, и старые плагины не загружаются.

#include <QString>
#include <QtPlugin>
#include <functional>

namespace mycodeapp {

// Что редактор даёт плагину. Вызывать только из GUI-потока.
class PluginHost {
public:
    virtual ~PluginHost() = default;

    // Команда в меню «Плагины» и в настройках горячих клавиш.
    // id — уникальный в пределах плагина
    virtual void addCommand(const QString &id, const QString &title, const QString &defaultShortcut,
                            std::function<void()> run) = 0;

    virtual QString currentFilePath() const = 0;
    virtual QString currentText() const = 0;
    virtual QString selectedText() const = 0; // выделение основного курсора
    // Заменить выделение (без выделения — вставить у курсора); один шаг отмены
    virtual void replaceSelection(const QString &text) = 0;
    // Заменить весь текст; один шаг отмены
    virtual void setCurrentText(const QString &text) = 0;
    virtual void showMessage(const QString &text) = 0; // в строке состояния
};

class Plugin {
public:
    virtual ~Plugin() = default;
    virtual void activate(PluginHost *host) = 0;
    virtual void deactivate() {} // перед выгрузкой; команды редактор убирает сам
};

} // namespace mycodeapp

#define MyCodeAppPlugin_iid "org.mycodeapp.Plugin/1"
Q_DECLARE_INTERFACE(mycodeapp::Plugin, MyCodeAppPlugin_iid)
