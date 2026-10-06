#pragma once

#include <QColor>
#include <QHash>
#include <QObject>

// Цвета редактора из текущей темы. Значения по умолчанию — тёмная тема:
// они действуют, пока тема не загружена, и в тестах.
struct EditorColors {
    QRgb background = 0x1e1f22;
    QRgb text = 0xc5c8ce;
    QRgb currentLine = 0x26282e;
    QRgb selection = 0x2e436e;
    QRgb caret = 0xced0d6;
    QRgb match = 0x3b4a2c;
    QRgb currentMatch = 0x6a5520;
    QRgb bracket = 0x3f4552;
    QRgb gutterText = 0x4e525a;
    QRgb gutterCurrent = 0xa0a4ac;
    QRgb foldPlaceholder = 0x33363d;
    QRgb changeAdded = 0x4b8b57;
    QRgb changeModified = 0x3e6fa8;
    QRgb changeRemoved = 0xb5524f;
    QRgb diffInserted = 0x233428;
    QRgb diffDeleted = 0x3a2528;
    QRgb diagnostics[3] = {0xe0605a, 0xd4a94f, 0x6c9bd2}; // ошибка, предупреждение, прочее
};

class Palette : public QObject {
    Q_OBJECT

public:
    static Palette &instance();

    const EditorColors &editor() const { return m_editor; }
    // Цвет вида подсветки («keyword», «string», ...); невалидный — тема его не задаёт
    QColor syntax(const QString &type) const { return m_syntax.value(type); }

    void set(const EditorColors &editor, const QHash<QString, QColor> &syntax);

signals:
    void changed();

private:
    EditorColors m_editor;
    QHash<QString, QColor> m_syntax;
};
