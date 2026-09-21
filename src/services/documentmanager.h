#pragma once

#include <QObject>
#include <QString>
#include <QUrl>
#include <QVariantList>
#include <QVector>
#include <QtQmlIntegration/qqmlintegration.h>
#include "src/core/document.h"

// Список открытых документов: открытие/закрытие, активный таб.
// Владеет всеми Document'ами.
class DocumentManager : public QObject {
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(int currentIndex READ currentIndex WRITE setCurrentIndex
                   NOTIFY currentIndexChanged)
    Q_PROPERTY(QObject *currentDocument READ currentDocument
                   NOTIFY currentIndexChanged)
    Q_PROPERTY(QVariantList tabs READ tabs NOTIFY tabsChanged)

public:
    explicit DocumentManager(QObject *parent = nullptr);

    int currentIndex() const { return m_current; }
    void setCurrentIndex(int index);
    QObject *currentDocument() const;
    QVariantList tabs() const; // [{name, dirty, path}, ...]

    Q_INVOKABLE int open(const QUrl &url);
    Q_INVOKABLE void close(int index);
    Q_INVOKABLE void activate(int index);
    Q_INVOKABLE bool isDirty(int index) const;
    Q_INVOKABLE void save(int index);

signals:
    void currentIndexChanged();
    void tabsChanged();
    void errorOccurred(const QString &message);

private:
    QVector<core::Document *> m_docs;
    int m_current = -1;
};