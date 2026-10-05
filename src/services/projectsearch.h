#pragma once

#include <QAbstractListModel>
#include <QThreadPool>
#include <QtQmlIntegration/qqmlintegration.h>
#include <atomic>

// Поиск текста по всем файлам проекта (с учётом .gitignore) в фоновом потоке.
// Результаты — плоский список: строка-заголовок файла, за ней его совпадения;
// приходят пачками по мере обхода. Новый поиск отменяет предыдущий.
class ProjectSearch : public QAbstractListModel {
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(QString rootPath MEMBER m_root NOTIFY rootPathChanged)
    Q_PROPERTY(bool running READ isRunning NOTIFY progressChanged)
    Q_PROPERTY(int filesDone READ filesDone NOTIFY progressChanged)
    Q_PROPERTY(int filesTotal READ filesTotal NOTIFY progressChanged)
    Q_PROPERTY(int matchCount READ matchCount NOTIFY progressChanged)
    Q_PROPERTY(int fileCount READ fileCount NOTIFY progressChanged)
    Q_PROPERTY(bool truncated READ isTruncated NOTIFY progressChanged)

public:
    static constexpr int kMaxMatches = 20000;
    static constexpr qint64 kMaxFileSize = 20 << 20;

    enum Role { IsFileRole = Qt::UserRole + 1, PathRole, RelativeRole, LineRole, ColumnRole,
                LengthRole, PreviewRole, CountRole };

    struct Match {
        int line = 0;
        int column = 0;
        int length = 0;
        QString previewHtml; // строка с выделенным совпадением
    };
    struct FileResult {
        QString path;
        QString relative;
        QVector<Match> matches;
    };

    explicit ProjectSearch(QObject *parent = nullptr);
    ~ProjectSearch() override;

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    bool isRunning() const { return m_running; }
    int filesDone() const { return m_filesDone; }
    int filesTotal() const { return m_filesTotal; }
    int matchCount() const { return m_matchCount; }
    int fileCount() const { return int(m_results.size()); }
    bool isTruncated() const { return m_truncated; }

    Q_INVOKABLE void start(const QString &needle, bool caseSensitive);
    Q_INVOKABLE void cancel();
    Q_INVOKABLE void clear();

    static QVector<Match> searchText(const QString &text, const QString &needle, Qt::CaseSensitivity cs,
                                     int limit);

signals:
    void rootPathChanged();
    void progressChanged();

private:
    void append(const QVector<FileResult> &batch, int filesDone, int filesTotal, bool truncated);
    void finish(int generation);

    QString m_root;
    QVector<FileResult> m_results;
    QVector<QPair<int, int>> m_rows; // {файл, совпадение или -1 для заголовка}
    bool m_running = false;
    bool m_truncated = false;
    int m_filesDone = 0;
    int m_filesTotal = 0;
    int m_matchCount = 0;
    std::atomic<int> m_generation{0};
    QThreadPool m_pool;
};
