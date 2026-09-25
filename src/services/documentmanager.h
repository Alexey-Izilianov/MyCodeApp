#pragma once

#include <QObject>
#include <QString>
#include <QUrl>
#include <QVariantList>
#include <QVector>
#include <QtQmlIntegration/qqmlintegration.h>
#include "src/core/document.h"

// Список открытых документов: открытие/закрытие, активный таб.
// Владеет всеми Document'ами. Файлы больше порога грузятся асинхронно
// с прогрессом (loading/loadPercent/loadingName — для оверлея в QML).
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
    QVariantList tabs() const; // [{name, dirty, path}, ...]
    bool isLoading() const { return !m_loadingDocs.isEmpty(); }
    int loadPercent() const { return m_loadPercent; }
    QString loadingName() const
    { return m_loadingDocs.isEmpty() ? QString() : m_loadingDocs.last()->displayName(); }

    Q_INVOKABLE int open(const QUrl &url);
    Q_INVOKABLE void close(int index);
    Q_INVOKABLE void activate(int index);
    Q_INVOKABLE bool isDirty(int index) const;
    Q_INVOKABLE void save(int index);

signals:
    void currentIndexChanged();
    void tabsChanged();
    void errorOccurred(const QString &message);
    void loadingChanged();
    void loadPercentChanged();

private:
    void finishAsyncLoad(core::Document *doc, bool ok);

    QVector<core::Document *> m_docs;
    QVector<core::Document *> m_loadingDocs; // ещё не опубликованные
    int m_current = -1;
    int m_loadPercent = 0;

    static constexpr qint64 kAsyncLoadThreshold = 20 * 1024 * 1024; // 20 МБ
};