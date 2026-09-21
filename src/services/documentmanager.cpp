#include "documentmanager.h"

using core::Document;

DocumentManager::DocumentManager(QObject *parent)
    : QObject(parent)
{
}

void DocumentManager::setCurrentIndex(int index)
{
    if (index == m_current || index < -1 || index >= m_docs.size())
        return;
    m_current = index;
    emit currentIndexChanged();
}

QObject *DocumentManager::currentDocument() const
{
    const bool valid = m_current >= 0 && m_current < m_docs.size();
    return valid ? m_docs.at(m_current) : nullptr;
}

QVariantList DocumentManager::tabs() const
{
    QVariantList list;
    for (core::Document *doc : m_docs) {
        QVariantMap tab;
        tab.insert(QStringLiteral("name"), doc->displayName());
        tab.insert(QStringLiteral("dirty"), doc->isDirty());
        tab.insert(QStringLiteral("path"), doc->filePath());
        list.append(tab);
    }
    return list;
}

int DocumentManager::open(const QUrl &url)
{
    const QString path = url.isLocalFile() ? url.toLocalFile() : url.toString();
    if (path.isEmpty())
        return -1;

    for (int i = 0; i < m_docs.size(); ++i) {
        if (m_docs.at(i)->filePath() == path) {
            activate(i); // уже открыт — просто переключаемся
            return i;
        }
    }

    auto *doc = new Document(this);
    if (!doc->load(path)) {
        delete doc;
        emit errorOccurred(QStringLiteral("не удалось открыть файл"));
        return -1;
    }
    connect(doc, &Document::dirtyChanged, this, &DocumentManager::tabsChanged);

    m_docs.append(doc);
    m_current = m_docs.size() - 1;
    emit tabsChanged();
    emit currentIndexChanged();
    return m_current;
}

void DocumentManager::close(int index)
{
    if (index < 0 || index >= m_docs.size())
        return;
    delete m_docs.takeAt(index);
    if (m_current >= m_docs.size())
        m_current = m_docs.size() - 1;
    emit tabsChanged();
    emit currentIndexChanged();
}

void DocumentManager::activate(int index)
{
    if (index == m_current || index < 0 || index >= m_docs.size())
        return;
    m_current = index;
    emit currentIndexChanged();
}

bool DocumentManager::isDirty(int index) const
{
    const bool valid = index >= 0 && index < m_docs.size();
    return valid && m_docs.at(index)->isDirty();
}

void DocumentManager::save(int index)
{
    if (index < 0 || index >= m_docs.size())
        return;
    QString error;
    if (!m_docs.at(index)->save(&error))
        emit errorOccurred(error);
}