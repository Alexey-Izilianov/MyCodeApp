#include <QClipboard>
#include <QGuiApplication>
#include <QQuickWindow>
#include <QTemporaryDir>
#include <QtTest>

#include "src/core/document.h"
#include "src/gui/editerview.h"

using core::Document;

// Поведение редактора через настоящие события клавиатуры и мыши
class EditorViewTest : public QObject {
    Q_OBJECT

private slots:
    void init();
    void cleanup();
    void typingAndUndoRedo();
    void enterKeepsIndent();
    void clickAfterScrollHitsRightLine();
    void multilineCopyPaste();
    void wordNavigation();
    void viewStatePerDocument();
    void nothingDrawnOutsideView();
    void caretsTypeAndUndo();
    void boxSelection();
    void selectNextOccurrence();
    void pasteDistributesLines();
    void altClickAddsCaret();
    void enterOnEveryCaret();
    void autoClosePairs();
    void quotes();
    void wrapSelection();
    void enterBetweenBraces();
    void pythonColonIndent();
    void closingBraceOutdents();
    void tabColumns();
    void tabIndentStyle();
    void indentAndOutdentLines();
    void backspaceUnindents();
    void autoCloseUndoRedo();
    void surrogatePairs();
    void longLineTypingIsFast();
    void foldNavigation();
    void foldEditsAndReveal();
    void foldAllAndClick();

private:
    Document *openDoc(const QByteArray &text, const QString &suffix = QStringLiteral("txt"));
    QString text(const Document *doc) const;
    int lineHeight() const;
    void typeText(const char *chars)
    {
        for (; *chars; ++chars)
            QTest::keyClick(m_window, *chars);
    }

    QTemporaryDir m_dir;
    QQuickWindow *m_window = nullptr;
    EditorView *m_view = nullptr;
    int m_files = 0;
};

void EditorViewTest::init()
{
    m_window = new QQuickWindow;
    m_window->resize(800, 400);
    m_view = new EditorView(m_window->contentItem());
    m_view->setSize(QSizeF(800, 400));
    m_window->show();
    QVERIFY(QTest::qWaitForWindowExposed(m_window));
    m_view->forceActiveFocus();
}

void EditorViewTest::cleanup()
{
    delete m_window; // вместе с view
    m_window = nullptr;
}

Document *EditorViewTest::openDoc(const QByteArray &content, const QString &suffix)
{
    const QString path = m_dir.path() + QStringLiteral("/f%1.").arg(++m_files) + suffix;
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly))
        return nullptr;
    f.write(content);
    f.close();
    auto *doc = new Document(m_view);
    if (!doc->load(path))
        return nullptr;
    m_view->setDocument(doc);
    return doc;
}

QString EditorViewTest::text(const Document *doc) const
{
    const auto &b = doc->buffer();
    return b.text({0, 0}, b.positionOf(b.length()));
}

int EditorViewTest::lineHeight() const
{
    // contentHeight = (lines - 1) * lineHeight + height
    const auto *doc = qobject_cast<Document *>(m_view->document());
    const int lines = doc->buffer().lineCount();
    return lines > 1 ? int((m_view->contentHeight() - m_view->height()) / (lines - 1)) : 0;
}

void EditorViewTest::typingAndUndoRedo()
{
    Document *doc = openDoc("");
    QVERIFY(doc);
    typeText("hello");
    QTest::keyClick(m_window, Qt::Key_Space);
    typeText("world");
    QCOMPARE(text(doc), QStringLiteral("hello world"));
    QVERIFY(doc->isDirty());

    QTest::keyClick(m_window, Qt::Key_Z, Qt::ControlModifier); // ввод без пауз — один шаг
    QCOMPARE(text(doc), QString());
    QVERIFY(!doc->isDirty());
    QTest::keyClick(m_window, Qt::Key_Y, Qt::ControlModifier);
    QCOMPARE(text(doc), QStringLiteral("hello world"));
    QCOMPARE(m_view->cursorColumn(), 11);

    // Движение курсора разрывает группу ввода
    QTest::keyClick(m_window, Qt::Key_Left);
    QTest::keyClick(m_window, Qt::Key_Right);
    typeText("!");
    QTest::keyClick(m_window, Qt::Key_Z, Qt::ControlModifier);
    QCOMPARE(text(doc), QStringLiteral("hello world"));
}

void EditorViewTest::enterKeepsIndent()
{
    Document *doc = openDoc("    int a;");
    QVERIFY(doc);
    QTest::keyClick(m_window, Qt::Key_End);
    QTest::keyClick(m_window, Qt::Key_Return);
    typeText("b");
    QCOMPARE(text(doc), QStringLiteral("    int a;\n    b"));

    QTest::keyClick(m_window, Qt::Key_Home); // к первому непробельному
    QCOMPARE(m_view->cursorColumn(), 4);
    QTest::keyClick(m_window, Qt::Key_Home); // затем в колонку 0
    QCOMPARE(m_view->cursorColumn(), 0);
}

void EditorViewTest::clickAfterScrollHitsRightLine()
{
    QByteArray content;
    for (int i = 0; i < 200; ++i)
        content += "line " + QByteArray::number(i) + '\n';
    QVERIFY(openDoc(content));
    const int lh = lineHeight();
    QVERIFY(lh > 0);

    m_view->setScrollY(100 * lh);
    QTest::mouseClick(m_window, Qt::LeftButton, {}, QPoint(300, lh * 3 + lh / 2));
    QCOMPARE(m_view->cursorLine(), 103);

    // Прокрутка колесом ограничена снизу последней строкой
    m_view->setScrollY(1e9);
    QCOMPARE(m_view->scrollY(), qreal(200 * lh));
}

void EditorViewTest::multilineCopyPaste()
{
    Document *doc = openDoc("one\ntwo\nthree");
    QVERIFY(doc);
    QTest::keyClick(m_window, Qt::Key_Right);
    QTest::keyClick(m_window, Qt::Key_Down, Qt::ShiftModifier);
    QTest::keyClick(m_window, Qt::Key_Right, Qt::ShiftModifier);
    QTest::keyClick(m_window, Qt::Key_C, Qt::ControlModifier);
    QCOMPARE(QGuiApplication::clipboard()->text(), QStringLiteral("ne\ntw"));

    QTest::keyClick(m_window, Qt::Key_End, Qt::ControlModifier);
    QTest::keyClick(m_window, Qt::Key_V, Qt::ControlModifier);
    QCOMPARE(text(doc), QStringLiteral("one\ntwo\nthreene\ntw"));
    QTest::keyClick(m_window, Qt::Key_Z, Qt::ControlModifier);
    QCOMPARE(text(doc), QStringLiteral("one\ntwo\nthree"));
}

void EditorViewTest::wordNavigation()
{
    Document *doc = openDoc("foo_bar(baz, 42);");
    QVERIFY(doc);
    QTest::keyClick(m_window, Qt::Key_Right, Qt::ControlModifier);
    QCOMPARE(m_view->cursorColumn(), 7); // конец идентификатора
    QTest::keyClick(m_window, Qt::Key_End);
    QTest::keyClick(m_window, Qt::Key_Backspace, Qt::ControlModifier);
    QCOMPARE(text(doc), QStringLiteral("foo_bar(baz, 42"));
}

void EditorViewTest::viewStatePerDocument()
{
    Document *a = openDoc("aaa\nbbb\nccc");
    QVERIFY(a);
    QTest::keyClick(m_window, Qt::Key_Down);
    QTest::keyClick(m_window, Qt::Key_Down);
    QTest::keyClick(m_window, Qt::Key_End);

    Document *b = openDoc("x");
    QVERIFY(b);
    QCOMPARE(m_view->cursorLine(), 0);
    m_view->setDocument(a);
    QCOMPARE(m_view->cursorLine(), 2);
    QCOMPARE(m_view->cursorColumn(), 3);
}

void EditorViewTest::nothingDrawnOutsideView()
{
    // Над редактором в приложении — полоса вкладок: частично прокрученная
    // верхняя строка (номер, подсветка текущей строки) не должна туда залезать
    QByteArray content;
    for (int i = 0; i < 200; ++i)
        content += "line " + QByteArray::number(i) + '\n';
    QVERIFY(openDoc(content));
    const int top = 60;
    m_window->setColor(Qt::white);
    m_view->setY(top);
    m_view->setHeight(m_window->height() - top);
    const int lh = lineHeight();
    m_view->setScrollY(50 * lh + lh / 2);
    QTest::mouseClick(m_window, Qt::LeftButton, {}, QPoint(300, top + 2)); // курсор на строку 50
    QCOMPARE(m_view->cursorLine(), 50);
    m_view->setScrollY(50 * lh + lh / 2); // клик подкрутил к курсору — снова полстроки
    const QImage image = m_window->grabWindow();
    const qreal dpr = image.devicePixelRatio();
    for (int y = 0; y < int(top * dpr) - 1; ++y)
        for (int x = 0; x < image.width(); x += 2)
            QVERIFY2(image.pixelColor(x, y) == QColor(Qt::white),
                     qPrintable(QStringLiteral("пиксель (%1, %2) над редактором закрашен").arg(x).arg(y)));
}

void EditorViewTest::caretsTypeAndUndo()
{
    Document *doc = openDoc("a\nb\nc");
    QVERIFY(doc);
    QTest::keyClick(m_window, Qt::Key_Down, Qt::ControlModifier | Qt::AltModifier);
    QTest::keyClick(m_window, Qt::Key_Down, Qt::ControlModifier | Qt::AltModifier);
    QCOMPARE(m_view->cursorCount(), 3);

    typeText("XY");
    QCOMPARE(text(doc), QStringLiteral("XYa\nXYb\nXYc"));
    QTest::keyClick(m_window, Qt::Key_Backspace);
    QCOMPARE(text(doc), QStringLiteral("Xa\nXb\nXc"));

    QTest::keyClick(m_window, Qt::Key_Z, Qt::ControlModifier); // весь ввод на всех курсорах — один шаг
    QCOMPARE(text(doc), QStringLiteral("a\nb\nc"));
    QCOMPARE(m_view->cursorCount(), 3);

    QTest::keyClick(m_window, Qt::Key_Escape);
    QCOMPARE(m_view->cursorCount(), 1);
}

void EditorViewTest::boxSelection()
{
    Document *doc = openDoc("abcdef\nabcdef\nab\nabcdef");
    QVERIFY(doc);
    QTest::keyClick(m_window, Qt::Key_Right);
    QTest::keyClick(m_window, Qt::Key_Right);
    for (int i = 0; i < 3; ++i)
        QTest::keyClick(m_window, Qt::Key_Down, Qt::AltModifier | Qt::ShiftModifier);
    for (int i = 0; i < 2; ++i)
        QTest::keyClick(m_window, Qt::Key_Right, Qt::AltModifier | Qt::ShiftModifier);
    QCOMPARE(m_view->cursorCount(), 4);

    typeText("Z"); // короткая строка «ab» получает курсор в конце
    QCOMPARE(text(doc), QStringLiteral("abZef\nabZef\nabZ\nabZef"));
}

void EditorViewTest::selectNextOccurrence()
{
    Document *doc = openDoc("foo bar foo baz foo");
    QVERIFY(doc);
    QTest::keyClick(m_window, Qt::Key_D, Qt::ControlModifier); // слово под курсором
    QCOMPARE(m_view->cursorCount(), 1);
    QTest::keyClick(m_window, Qt::Key_D, Qt::ControlModifier);
    QTest::keyClick(m_window, Qt::Key_D, Qt::ControlModifier);
    QCOMPARE(m_view->cursorCount(), 3);
    QTest::keyClick(m_window, Qt::Key_D, Qt::ControlModifier); // больше вхождений нет
    QCOMPARE(m_view->cursorCount(), 3);

    typeText("x");
    QCOMPARE(text(doc), QStringLiteral("x bar x baz x"));
}

void EditorViewTest::pasteDistributesLines()
{
    Document *doc = openDoc("a\nb\nc");
    QVERIFY(doc);
    QTest::keyClick(m_window, Qt::Key_Down, Qt::ControlModifier | Qt::AltModifier);
    QTest::keyClick(m_window, Qt::Key_Down, Qt::ControlModifier | Qt::AltModifier);

    QGuiApplication::clipboard()->setText(QStringLiteral("1\r\n2\r\n3"));
    QTest::keyClick(m_window, Qt::Key_V, Qt::ControlModifier);
    QCOMPARE(text(doc), QStringLiteral("1a\n2b\n3c"));

    // Строк не столько, сколько курсоров, — каждый получает весь текст
    QGuiApplication::clipboard()->setText(QStringLiteral("-"));
    QTest::keyClick(m_window, Qt::Key_V, Qt::ControlModifier);
    QCOMPARE(text(doc), QStringLiteral("1-a\n2-b\n3-c"));
}

void EditorViewTest::altClickAddsCaret()
{
    Document *doc = openDoc("one\ntwo\nthree");
    QVERIFY(doc);
    const int lh = lineHeight();
    QTest::mouseClick(m_window, Qt::LeftButton, Qt::AltModifier, QPoint(700, lh * 2 + lh / 2));
    QCOMPARE(m_view->cursorCount(), 2);
    QCOMPARE(m_view->cursorLine(), 2); // новый курсор — основной
    typeText("!");
    QCOMPARE(text(doc), QStringLiteral("!one\ntwo\nthree!"));

    // Alt+клик по уже существующей позиции не плодит дубль
    QTest::mouseClick(m_window, Qt::LeftButton, Qt::AltModifier, QPoint(700, lh * 2 + lh / 2));
    QCOMPARE(m_view->cursorCount(), 2);
}

void EditorViewTest::enterOnEveryCaret()
{
    Document *doc = openDoc("  a\n  b");
    QVERIFY(doc);
    QTest::keyClick(m_window, Qt::Key_End);
    QTest::keyClick(m_window, Qt::Key_Down, Qt::ControlModifier | Qt::AltModifier);
    QTest::keyClick(m_window, Qt::Key_Return);
    QCOMPARE(text(doc), QStringLiteral("  a\n  \n  b\n  "));
    QCOMPARE(m_view->cursorCount(), 2);
}

void EditorViewTest::autoClosePairs()
{
    Document *doc = openDoc("");
    QVERIFY(doc);
    typeText("f(");
    QCOMPARE(text(doc), QStringLiteral("f()"));
    QCOMPARE(m_view->cursorColumn(), 2);
    typeText("a[");
    QCOMPARE(text(doc), QStringLiteral("f(a[])"));
    typeText("1])"); // закрывающие перешагиваются, а не дублируются
    QCOMPARE(text(doc), QStringLiteral("f(a[1])"));
    QCOMPARE(m_view->cursorColumn(), 7);

    typeText("("); // в конце строки — пара
    QTest::keyClick(m_window, Qt::Key_Home);
    typeText("("); // перед словом — одна скобка
    QCOMPARE(text(doc), QStringLiteral("(f(a[1])()"));
}

void EditorViewTest::quotes()
{
    Document *doc = openDoc("");
    QVERIFY(doc);
    typeText("don't ");
    QCOMPARE(text(doc), QStringLiteral("don't ")); // апостроф после буквы — не пара
    typeText("\"a\"");
    QCOMPARE(text(doc), QStringLiteral("don't \"a\""));
    QCOMPARE(m_view->cursorColumn(), 9);
}

void EditorViewTest::wrapSelection()
{
    Document *doc = openDoc("word");
    QVERIFY(doc);
    QTest::keyClick(m_window, Qt::Key_A, Qt::ControlModifier);
    typeText("(");
    QCOMPARE(text(doc), QStringLiteral("(word)"));
    typeText("["); // выделение осталось на слове — оборачиваем ещё раз
    QCOMPARE(text(doc), QStringLiteral("([word])"));
    QTest::keyClick(m_window, Qt::Key_C, Qt::ControlModifier);
    QCOMPARE(QGuiApplication::clipboard()->text(), QStringLiteral("word"));
}

void EditorViewTest::enterBetweenBraces()
{
    Document *doc = openDoc("");
    QVERIFY(doc);
    typeText("if (x) {");
    QCOMPARE(text(doc), QStringLiteral("if (x) {}"));
    QTest::keyClick(m_window, Qt::Key_Return);
    QCOMPARE(text(doc), QStringLiteral("if (x) {\n    \n}"));
    QCOMPARE(m_view->cursorLine(), 1);
    QCOMPARE(m_view->cursorColumn(), 4);
}

void EditorViewTest::pythonColonIndent()
{
    Document *doc = openDoc("def f():", QStringLiteral("py"));
    QVERIFY(doc);
    QTest::keyClick(m_window, Qt::Key_End);
    QTest::keyClick(m_window, Qt::Key_Return);
    typeText("pass");
    QCOMPARE(text(doc), QStringLiteral("def f():\n    pass"));
}

void EditorViewTest::closingBraceOutdents()
{
    Document *doc = openDoc("{\n        ");
    QVERIFY(doc);
    QTest::keyClick(m_window, Qt::Key_End, Qt::ControlModifier);
    typeText("}");
    QCOMPARE(text(doc), QStringLiteral("{\n    }"));
}

void EditorViewTest::tabColumns()
{
    Document *doc = openDoc("    ab\n\tab");
    QVERIFY(doc);
    for (int i = 0; i < 5; ++i)
        QTest::keyClick(m_window, Qt::Key_Right);
    QTest::keyClick(m_window, Qt::Key_Down); // визуальная колонка 5 = после «a» за табуляцией
    QCOMPARE(m_view->cursorLine(), 1);
    QCOMPARE(m_view->cursorColumn(), 2);
    QTest::keyClick(m_window, Qt::Key_Up);
    QCOMPARE(m_view->cursorColumn(), 5);
}

void EditorViewTest::tabIndentStyle()
{
    Document *doc = openDoc("a {\n\tb;\n\tc;\n}");
    QVERIFY(doc);
    QTest::keyClick(m_window, Qt::Key_Tab);
    QCOMPARE(text(doc), QStringLiteral("\ta {\n\tb;\n\tc;\n}"));
}

void EditorViewTest::indentAndOutdentLines()
{
    Document *doc = openDoc("a\nb\nc");
    QVERIFY(doc);
    QTest::keyClick(m_window, Qt::Key_A, Qt::ControlModifier);
    QTest::keyClick(m_window, Qt::Key_Tab);
    QCOMPARE(text(doc), QStringLiteral("    a\n    b\n    c"));
    QTest::keyClick(m_window, Qt::Key_Backtab, Qt::ShiftModifier);
    QCOMPARE(text(doc), QStringLiteral("a\nb\nc"));
    QTest::keyClick(m_window, Qt::Key_Z, Qt::ControlModifier);
    QCOMPARE(text(doc), QStringLiteral("    a\n    b\n    c"));
}

void EditorViewTest::backspaceUnindents()
{
    Document *doc = openDoc("a\n    b\n        x"); // шаг отступа в файле — 4
    QVERIFY(doc);
    QTest::keyClick(m_window, Qt::Key_End, Qt::ControlModifier);
    QTest::keyClick(m_window, Qt::Key_Home); // к первому непробельному
    QTest::keyClick(m_window, Qt::Key_Backspace);
    QCOMPARE(text(doc), QStringLiteral("a\n    b\n    x"));

    typeText("(");
    QTest::keyClick(m_window, Qt::Key_Backspace); // пустая пара — целиком
    QCOMPARE(text(doc), QStringLiteral("a\n    b\n    x"));
}

void EditorViewTest::autoCloseUndoRedo()
{
    Document *doc = openDoc("");
    QVERIFY(doc);
    typeText("(");
    QTest::keyClick(m_window, Qt::Key_Z, Qt::ControlModifier);
    QCOMPARE(text(doc), QString());
    QTest::keyClick(m_window, Qt::Key_Y, Qt::ControlModifier);
    QCOMPARE(text(doc), QStringLiteral("()"));
    QCOMPARE(m_view->cursorColumn(), 1); // redo ставит курсор внутрь пары
}

void EditorViewTest::surrogatePairs()
{
    Document *doc = openDoc("a\xF0\x9F\x98\x80" "b"); // a😀b: эмодзи — два QChar
    QVERIFY(doc);
    QTest::keyClick(m_window, Qt::Key_Right);
    QTest::keyClick(m_window, Qt::Key_Right);
    QCOMPARE(m_view->cursorColumn(), 3); // перешагнули пару целиком
    QTest::keyClick(m_window, Qt::Key_Left);
    QCOMPARE(m_view->cursorColumn(), 1);
    QTest::keyClick(m_window, Qt::Key_Right);
    QTest::keyClick(m_window, Qt::Key_Backspace); // удаляется весь символ
    QCOMPARE(text(doc), QStringLiteral("ab"));
}

void EditorViewTest::longLineTypingIsFast()
{
    // Одна строка на 2 МБ: ввод в её конце не должен копировать строку целиком
    Document *doc = openDoc(QByteArray(2 * 1024 * 1024, 'x'));
    QVERIFY(doc);
    QTest::keyClick(m_window, Qt::Key_End);
    QElapsedTimer timer;
    timer.start();
    typeText("abc(def)ghij\"k\"lmnop");
    const qint64 ms = timer.elapsed();
    QCOMPARE(m_view->cursorColumn(), 2 * 1024 * 1024 + 20);
    QVERIFY2(ms < 1000, qPrintable(QStringLiteral("20 символов: %1 мс").arg(ms)));
    qInfo("typing 20 chars at the end of a 2 MB line: %lld ms", ms);
}

void EditorViewTest::foldNavigation()
{
    Document *doc = openDoc("a {\n    b\n    c\n}\nd", QStringLiteral("cpp"));
    QVERIFY(doc);
    const int lh = lineHeight();
    const qreal fullHeight = m_view->contentHeight();

    m_view->foldAtCursor();
    QCOMPARE(m_view->contentHeight(), fullHeight - 2 * lh); // скрыты b и c

    QTest::keyClick(m_window, Qt::Key_Down); // через свёрнутое — сразу на }
    QCOMPARE(m_view->cursorLine(), 3);
    QTest::keyClick(m_window, Qt::Key_Up);
    QCOMPARE(m_view->cursorLine(), 0);
    QTest::keyClick(m_window, Qt::Key_End);
    QTest::keyClick(m_window, Qt::Key_Right); // с конца заголовка — за блок
    QCOMPARE(m_view->cursorLine(), 3);
    QCOMPARE(m_view->cursorColumn(), 0);
    QTest::keyClick(m_window, Qt::Key_Left); // обратно — в конец заголовка
    QCOMPARE(m_view->cursorLine(), 0);
    QCOMPARE(m_view->cursorColumn(), 3);

    // Клик по ряду под заголовком попадает в строку }
    QTest::mouseClick(m_window, Qt::LeftButton, {}, QPoint(300, lh + lh / 2));
    QCOMPARE(m_view->cursorLine(), 3);

    m_view->unfoldAtCursor(); // курсор не на заголовке — ничего
    QCOMPARE(m_view->contentHeight(), fullHeight - 2 * lh);
    QTest::keyClick(m_window, Qt::Key_Home, Qt::ControlModifier);
    m_view->unfoldAtCursor();
    QCOMPARE(m_view->contentHeight(), fullHeight);
    QCOMPARE(text(doc), QStringLiteral("a {\n    b\n    c\n}\nd"));
}

void EditorViewTest::foldEditsAndReveal()
{
    Document *doc = openDoc("x\na {\n    b\n}\nd", QStringLiteral("cpp"));
    QVERIFY(doc);
    const int lh = lineHeight();
    const qreal fullHeight = m_view->contentHeight();
    QTest::keyClick(m_window, Qt::Key_Down);
    m_view->foldAtCursor(); // курсор на заголовке «a {»

    // Enter в строке выше сдвигает блок, он остаётся свёрнутым
    QTest::keyClick(m_window, Qt::Key_Home, Qt::ControlModifier);
    QTest::keyClick(m_window, Qt::Key_End);
    QTest::keyClick(m_window, Qt::Key_Return);
    QCOMPARE(m_view->contentHeight(), fullHeight); // +1 строка, -1 скрытая
    QTest::keyClick(m_window, Qt::Key_Down);
    QTest::keyClick(m_window, Qt::Key_Down);
    QCOMPARE(m_view->cursorLine(), 4); // с заголовка (2) — на } (4)

    // Backspace в начале } склеивает со скрытой строкой — блок разворачивается
    QTest::keyClick(m_window, Qt::Key_Home);
    QTest::keyClick(m_window, Qt::Key_Backspace);
    QCOMPARE(text(doc), QStringLiteral("x\n\na {\n    b}\nd"));
    QCOMPARE(m_view->contentHeight(), fullHeight);

    // Курсор, попавший в свёрнутое (Ctrl+End в конце блока по отступам), разворачивает его
    QVERIFY(openDoc("def f():\n    a\n    b", QStringLiteral("py")));
    m_view->foldAtCursor();
    const qreal folded = m_view->contentHeight();
    QTest::keyClick(m_window, Qt::Key_End, Qt::ControlModifier);
    QCOMPARE(m_view->cursorLine(), 2);
    QCOMPARE(m_view->contentHeight(), folded + 2 * lh);
}

void EditorViewTest::foldAllAndClick()
{
    Document *doc = openDoc("void f() {\n    if (x) {\n        y();\n    }\n}\nint z;\n",
                            QStringLiteral("cpp"));
    QVERIFY(doc);
    const int lh = lineHeight();
    const qreal fullHeight = m_view->contentHeight();
    QTest::keyClick(m_window, Qt::Key_Down);
    QTest::keyClick(m_window, Qt::Key_Down); // курсор внутри обоих блоков

    m_view->foldAll();
    QCOMPARE(m_view->contentHeight(), fullHeight - 3 * lh); // видны f, }, int z, пустая
    QCOMPARE(m_view->cursorLine(), 0); // курсор поднят на видимый заголовок

    // Клик по «…» за концом заголовка разворачивает внешний блок, внутренний остаётся
    QTest::mouseClick(m_window, Qt::LeftButton, {}, QPoint(780, lh / 2));
    QCOMPARE(m_view->contentHeight(), fullHeight - lh);

    m_view->unfoldAll();
    QCOMPARE(m_view->contentHeight(), fullHeight);
}

QTEST_MAIN(EditorViewTest)
#include "editorview_test.moc"
