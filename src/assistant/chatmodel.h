#pragma once

#include <QAbstractListModel>
#include <QStringList>
#include <QTimer>
#include <QtQmlIntegration/qqmlintegration.h>
#include <optional>

// Разговор с ассистентом. Ответ модели дописывается в последнюю строку по мере прихода;
// интерфейс узнаёт об этом не чаще kRefreshMs — разбор текста на каждый токен дорог
class ChatModel : public QAbstractListModel {
    Q_OBJECT
    QML_ANONYMOUS

public:
    // Код, к которому писался документирующий комментарий, и где он был
    struct CommentTarget {
        QString path;
        QString language;
        int firstLine = 0;
        QStringList code;
    };
    struct Message {
        bool user = false;
        QString text;   // что видно в панели
        QString prompt; // что ушло модели (у задач — с кодом); пусто — как text
        QString error;
        QString stats;  // «38 с · 4,9 ток/с»
        bool done = true;
        std::optional<CommentTarget> comment;
    };

    enum Role { UserRole = Qt::UserRole + 1, TextRole, ErrorRole, StatsRole, DoneRole, CommentRole, HasCodeRole };

    explicit ChatModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    const QVector<Message> &messages() const { return m_messages; }
    const Message *find(int row) const; // nullptr — нет такой строки
    int append(Message message);
    void appendText(int row, const QString &text);
    void finish(int row, const QString &error, const QString &stats);
    void clear();

private:
    static constexpr int kRefreshMs = 80;
    void refresh(int row, const QList<int> &roles);

    QVector<Message> m_messages;
    QTimer m_refresh;
    int m_pendingRow = -1;
};
