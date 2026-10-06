#pragma once

#include <QElapsedTimer>
#include <QFileSystemWatcher>
#include <QSet>
#include <QTimer>
#include <QObject>
#include <QString>
#include <QUrl>
#include <QVariantList>
#include <QVector>
#include <QtQmlIntegration/qqmlintegration.h>
#include "src/core/document.h"

class RecoveryManager;

class DocumentManager : public QObject {
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(int currentIndex READ currentIndex WRITE setCurrentIndex
                   NOTIFY currentIndexChanged)
    Q_PROPERTY(QObject *currentDocument READ currentDocument
                   NOTIFY currentIndexChanged)
    Q_PROPERTY(QVariantList tabs READ tabs NOTIFY tabsChanged)
    Q_PROPERTY(bool loading READ isLoading NOTIFY loadingChanged)
    Q_PROPERTY(int loadPercent READ loadPercent NOTIFY loadPercentChanged)
    Q_PROPERTY(QString loadingName READ loadingName NOTIFY loadingChanged)

public:
    explicit DocumentManager(QObject *parent = nullptr);

    int currentIndex() const { return m_current; }
    void setCurrentIndex(int index);
    QObject *currentDocument() const;
    const QVector<core::Document *> &documents() const { return m_docs; }
    QVariantList tabs() const; // [{name, dirty, path}, ...]
    bool isLoading() const { return !m_loadingDocs.isEmpty(); }
    int loadPercent() const { return m_loadPercent; }
    QString loadingName() const
    { return m_loadingDocs.isEmpty() ? QString() : m_loadingDocs.last()->displayName(); }

    Q_INVOKABLE int open(const QUrl &url);
    Q_INVOKABLE int openPath(const QString &path) { return open(QUrl::fromLocalFile(path)); }
    // Открыть файлы с несохранёнными правками из журнала (сбой, горячий выход)
    Q_INVOKABLE int restoreUnsaved();
    Q_INVOKABLE void close(int index);
    Q_INVOKABLE void activate(int index);
    Q_INVOKABLE bool isDirty(int index) const;
    Q_INVOKABLE QObject *documentAt(int index) const;
    Q_INVOKABLE void save(int index);
    // Ответ на externalChange: перечитать с диска или оставить свои правки
    Q_INVOKABLE void reloadFromDisk(int index);
    Q_INVOKABLE void keepLocal(int index);

signals:
    void currentIndexChanged();
    void tabsChanged();
    void errorOccurred(const QString &message);
    void loadingChanged();
    void loadPercentChanged();
    // Файл изменили снаружи, а в редакторе есть несохранённые правки
    void externalChange(int index, const QString &name);

private:
    void finishAsyncLoad(core::Document *doc, bool ok);
    void publish(core::Document *doc);
    void checkDisk();

    RecoveryManager *m_recovery = nullptr;
    QVector<core::Document *> m_docs;
    QVector<core::Document *> m_loadingDocs; // ещё не опубликованные
    int m_current = -1;
    int m_loadPercent = 0;
    QElapsedTimer m_loadTimer; // длительность последней фоновой загрузки
    // Watcher теряет файл после атомарной замены (так сохраняют многие
    // редакторы), поэтому путь добавляется заново, а опрос страхует
    QFileSystemWatcher m_watcher;
    QTimer m_diskCheck;
    QSet<core::Document *> m_conflicts; // уже спросили — ждём ответа

    static constexpr qint64 kAsyncLoadThreshold = 20 * 1024 * 1024;
};