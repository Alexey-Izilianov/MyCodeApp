#include "palette.h"

Palette &Palette::instance()
{
    static Palette palette;
    return palette;
}

void Palette::set(const EditorColors &editor, const QHash<QString, QColor> &syntax)
{
    m_editor = editor;
    m_syntax = syntax;
    emit changed();
}
