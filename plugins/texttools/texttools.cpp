// Демо-плагин: команды над выделением (без выделения — над всем текстом)

#include <QDateTime>
#include <QObject>
#include <QStringList>
#include <algorithm>

#include "include/mycodeapp/plugin.h"

class TextTools : public QObject, public mycodeapp::Plugin {
    Q_OBJECT
    Q_PLUGIN_METADATA(IID MyCodeAppPlugin_iid FILE "plugin.json")
    Q_INTERFACES(mycodeapp::Plugin)

public:
    void activate(mycodeapp::PluginHost *host) override
    {
        m_host = host;
        host->addCommand(QStringLiteral("upper"), tr("Текст: в верхний регистр"), QStringLiteral("Ctrl+Shift+U"),
                         [this] { transform([](const QString &t) { return t.toUpper(); }); });
        host->addCommand(QStringLiteral("lower"), tr("Текст: в нижний регистр"), QStringLiteral("Ctrl+Shift+L"),
                         [this] { transform([](const QString &t) { return t.toLower(); }); });
        host->addCommand(QStringLiteral("sort"), tr("Текст: сортировать строки"), QString(),
                         [this] { transformLines([](QStringList &lines) {
                             std::sort(lines.begin(), lines.end(), [](const QString &a, const QString &b) {
                                 return a.localeAwareCompare(b) < 0;
                             });
                         }); });
        host->addCommand(QStringLiteral("unique"), tr("Текст: убрать повторяющиеся строки"), QString(),
                         [this] { transformLines([](QStringList &lines) { lines.removeDuplicates(); }); });
        host->addCommand(QStringLiteral("date"), tr("Текст: вставить дату и время"), QString(), [this] {
            m_host->replaceSelection(QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm")));
        });
    }

private:
    template <class F>
    void transform(F f)
    {
        const QString selected = m_host->selectedText();
        if (!selected.isEmpty())
            m_host->replaceSelection(f(selected));
        else
            m_host->setCurrentText(f(m_host->currentText()));
    }
    template <class F>
    void transformLines(F f)
    {
        transform([&f](const QString &text) {
            QStringList lines = text.split(u'\n');
            const bool trailing = lines.size() > 1 && lines.last().isEmpty(); // перевод строки в конце
            if (trailing)
                lines.removeLast();
            f(lines);
            return lines.join(u'\n') + (trailing ? QStringLiteral("\n") : QString());
        });
        m_host->showMessage(tr("Готово"));
    }

    mycodeapp::PluginHost *m_host = nullptr;
};

#include "texttools.moc"
