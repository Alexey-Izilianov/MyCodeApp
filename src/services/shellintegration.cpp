#include "shellintegration.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QSettings>

#ifdef Q_OS_WIN
#include <shlobj.h>
#endif

namespace {

const QString kProgId = QStringLiteral("MyCodeApp.File");
const QString kFolderVerb = QStringLiteral("MyCodeApp");

const QStringList kExtensions = {
    "c", "h", "cc", "cpp", "cxx", "hh", "hpp", "hxx", "inl", "py", "pyw", "pyi", "qml", "js", "ts",
    "rs", "json", "jsonc", "md", "markdown", "txt", "log", "ini", "cfg", "conf", "toml", "yaml", "yml",
    "xml", "cmake", "sh", "bat", "cmd", "ps1", "csv", "gitignore",
};

QString exePath()
{
    return QDir::toNativeSeparators(QCoreApplication::applicationFilePath());
}

QString command(const QString &argument)
{
    return QStringLiteral("\"%1\" \"%2\"").arg(exePath(), argument);
}

QString appKey()
{
    return QStringLiteral("Applications/") + QFileInfo(QCoreApplication::applicationFilePath()).fileName();
}

// HKCU\Software\Classes; «Default» — значение по умолчанию у ключа
QSettings classes()
{
    return QSettings(QStringLiteral("HKEY_CURRENT_USER\\Software\\Classes"), QSettings::NativeFormat);
}

} // namespace

ShellIntegration::ShellIntegration(QObject *parent)
    : QObject(parent)
{
}

bool ShellIntegration::isSupported() const
{
#ifdef Q_OS_WIN
    return true;
#else
    return false;
#endif
}

bool ShellIntegration::isRegistered() const
{
    if (!isSupported())
        return false;
    return classes().value(appKey() + QStringLiteral("/shell/open/command/Default")).toString() == command(QStringLiteral("%1"));
}

bool ShellIntegration::setRegistered(bool registered)
{
    if (!isSupported())
        return false;
    QSettings reg = classes();
    const QString icon = QStringLiteral("\"%1\",0").arg(exePath());
    const QStringList folderKeys = {QStringLiteral("Directory/shell/") + kFolderVerb,
                                    QStringLiteral("Directory/Background/shell/") + kFolderVerb};
    if (registered) {
        // Программа в «Открыть с помощью» для перечисленных типов
        reg.setValue(appKey() + QStringLiteral("/FriendlyAppName"), QStringLiteral("MyCodeApp"));
        reg.setValue(appKey() + QStringLiteral("/DefaultIcon/Default"), icon);
        reg.setValue(appKey() + QStringLiteral("/shell/open/command/Default"), command(QStringLiteral("%1")));
        reg.setValue(kProgId + QStringLiteral("/Default"), tr("Текстовый файл"));
        reg.setValue(kProgId + QStringLiteral("/DefaultIcon/Default"), icon);
        reg.setValue(kProgId + QStringLiteral("/shell/open/command/Default"), command(QStringLiteral("%1")));
        for (const QString &ext : kExtensions) {
            reg.setValue(appKey() + QStringLiteral("/SupportedTypes/.") + ext, QString());
            reg.setValue(QStringLiteral(".%1/OpenWithProgids/%2").arg(ext, kProgId), QString());
        }
        // Папка: пункт в её меню и в меню пустого места внутри
        for (const QString &key : folderKeys) {
            reg.setValue(key + QStringLiteral("/Default"), tr("Открыть в MyCodeApp"));
            reg.setValue(key + QStringLiteral("/Icon"), icon);
            reg.setValue(key + QStringLiteral("/command/Default"), command(QStringLiteral("%V")));
        }
    } else {
        reg.remove(appKey());
        reg.remove(kProgId);
        for (const QString &ext : kExtensions)
            reg.remove(QStringLiteral(".%1/OpenWithProgids/%2").arg(ext, kProgId));
        for (const QString &key : folderKeys)
            reg.remove(key);
    }
    reg.sync();
#ifdef Q_OS_WIN
    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr); // Проводник перечитает сопоставления
#endif
    emit registeredChanged();
    return reg.status() == QSettings::NoError && isRegistered() == registered;
}
