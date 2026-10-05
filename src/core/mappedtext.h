#pragma once

#include <QFile>
#include <QString>
#include <QVector>
#include <functional>
#include <memory>

namespace core {

// Файл, отображённый в память (QFile::map), как неизменяемый текст.
// Логический текст — декодированный, с "\r\n" → '\n', как у обычной загрузки.
// Строки декодируются по запросу; индекс — смещения '\n' в QChar и байтовое
// начало каждой kCheckpoint-й строки (остальные находятся memchr).
// Кодировки — UTF-8 и Latin-1: только в них '\n' — это один байт 0x0A.
class MappedText {
public:
    static constexpr int kCheckpoint = 16;

    // nullptr — не удалось отобразить или текст длиннее INT_MAX символов
    static std::shared_ptr<const MappedText> open(const QString &path, bool utf8,
                                                  const std::function<void(int)> &progress = {});

    int length() const { return m_length; }
    const QVector<int> &newlines() const { return m_newlines; }
    bool crlf() const { return m_crlf; }

    QString text(int offset, int count) const;
    // Последовательный проход без поиска начала каждой строки; false из fn — стоп
    void forEachLine(const std::function<bool(int, const QString &)> &fn) const;

private:
    MappedText() = default;
    qint64 lineByteStart(int line) const;
    qint64 lineByteEnd(qint64 start) const; // позиция '\n' или конец файла
    QString decode(qint64 from, qint64 to) const; // без '\r' перед '\n'

    QFile m_file;
    const uchar *m_data = nullptr;
    qint64 m_size = 0;
    qint64 m_begin = 0; // после BOM
    bool m_utf8 = true;
    bool m_crlf = false;
    int m_length = 0;
    QVector<int> m_newlines;
    QVector<qint64> m_checkpoints; // байтовое начало строк 0, 16, 32...
};

} // namespace core
