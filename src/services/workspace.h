#pragma once

#include <QObject>
#include <QStringList>
#include <QThreadPool>
#include <QUrl>
#include <QVariantList>
#include <QVariantMap>
#include <QtQmlIntegration/qqmlintegration.h>
#include <atomic>
#include <functional>

// Открытая папка проекта: список файлов (фоновый обход с учётом .gitignore),
// быстрый поиск файла по имени (Ctrl+P), сессия и последняя открытая папка.
class Workspace : public QObject {
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(QString rootPath READ rootPath NOTIFY rootChanged)
    Q_PROPERTY(QString rootName READ rootName NOTIFY rootChanged)
    Q_PROPERTY(bool scanning READ isScanning NOTIFY scanningChanged)
    Q_PROPERTY(int fileCount READ fileCount NOTIFY filesChanged)

public:
    static constexpr int kMaxFiles = 200000;

    explicit Workspace(QObject *parent = nullptr);
    ~Workspace() override;

    QString rootPath() const { return m_root; }
    QString rootName() const;
    bool isScanning() const { return m_scanning; }
    int fileCount() const { return int(m_files.size()); }
    const QStringList &files() const { return m_files; } // относительные пути через '/'

    Q_INVOKABLE bool open(const QUrl &folder);
    Q_INVOKABLE void close();
    Q_INVOKABLE void rescan();
    // [{path, relative, name}], лучшие сверху
    Q_INVOKABLE QVariantList findFiles(const QString &query, int limit = 50) const;
    Q_INVOKABLE QString relativePath(const QString &path) const;

    Q_INVOKABLE QUrl lastRoot() const;
    Q_INVOKABLE QVariantMap loadSession() const;
    Q_INVOKABLE void saveSession(const QVariantMap &session) const;

    // Обход проекта; cancelled() опрашивается между папками
    static QStringList scanFiles(const QString &root, const std::function<bool()> &cancelled = {},
                                 int limit = kMaxFiles);
    // -1 — символы запроса не встречаются в пути по порядку; больше — лучше
    static int fuzzyScore(QStringView query, QStringView path);

signals:
    void rootChanged();
    void filesChanged();
    void scanningChanged();

private:
    QString sessionPath() const;
    void setScanning(bool scanning);

    QString m_root;
    QStringList m_files;
    bool m_scanning = false;
    std::atomic<int> m_generation{0}; // новый обход отменяет предыдущий
    QThreadPool m_pool;
};
