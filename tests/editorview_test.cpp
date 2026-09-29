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

private:
    Document *openDoc(const QByteArray &text);
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

Document *EditorViewTest::openDoc(const QByteArray &content)
{
    const QString path = m_dir.path() + QStringLiteral("/f%1.txt").arg(++m_files);
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

QTEST_MAIN(EditorViewTest)
#include "editorview_test.moc"
