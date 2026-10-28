// Drives the real window headlessly (QT_QPA_PLATFORM=offscreen) with key events and
// checks what ends up on disk. Also renders screenshots into $GRAPHITE_SHOTS if set.
//
// Fixtures copy the *structure* of real Logseq journals (loose first heading, tab
// indentation, a LOGBOOK drawer with "\t " indentation, no trailing newline) with
// placeholder text.

#include <QAbstractButton>
#include <QAction>
#include <QClipboard>
#include <QApplication>
#include <cmath>
#include <QDate>
#include <QDir>
#include <QCryptographicHash>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QFontInfo>
#include <QListWidget>
#include <QElapsedTimer>
#include <QTest>
#include <QTreeWidgetItemIterator>
#include <QTimer>
#include <QToolTip>
#include <QLinearGradient>
#include <QMouseEvent>
#include <QPainter>
#include <QPointingDevice>
#include <QScrollBar>
#include <QSettings>
#include <QTemporaryDir>
#include <QTextCursor>
#include <QTextEdit>
#include <cstdio>

#include "desktop/blur.hpp"
#include "planner/tasks.hpp"
#include "ui/explorer.hpp"
#include "render/imageobject.hpp"
#include "ui/mainwindow.hpp"
#include "ui/nervpanel.hpp"
#include "ui/outlinerview.hpp"
#include "ui/overlay.hpp"
#include "desktop/palette.hpp"
#include "ui/settingspage.hpp"
#include "desktop/updates.hpp"
#include "ui/unitart.hpp"
#include "ui/theme.hpp"
#include "ui/thirdimpact.hpp"

using namespace graphite;

static int failures = 0;

#define CHECK(cond)                                                      \
    do {                                                                 \
        if (!(cond)) {                                                   \
            std::printf("FAIL line %d: %s\n", __LINE__, #cond);          \
            ++failures;                                                  \
        }                                                                \
    } while (0)

#define CHECK_EQ(a, b)                                                   \
    do {                                                                 \
        const QString _a = (a), _b = (b);                                \
        if (_a != _b) {                                                  \
            std::printf("FAIL line %d\n  got:  [%s]\n  want: [%s]\n", __LINE__, qPrintable(_a), qPrintable(_b)); \
            ++failures;                                                  \
        }                                                                \
    } while (0)

static QString readFile(const QString& p) {
    QFile f(p);
    if (!f.open(QIODevice::ReadOnly)) return QString("<missing>");
    return QString::fromUtf8(f.readAll());
}

static void writeFile(const QString& p, const QString& s) {
    QDir().mkpath(QFileInfo(p).absolutePath());
    QFile f(p);
    if (f.open(QIODevice::WriteOnly)) f.write(s.toUtf8());
}

static QTextEdit* editorOf(MainWindow& w) { return w.findChild<QTextEdit*>(); }

static void press(QWidget* w, int key, Qt::KeyboardModifiers m = Qt::NoModifier, const QString& text = QString()) {
    QKeyEvent ev(QEvent::KeyPress, key, m, text);
    QApplication::sendEvent(w, &ev);
}

static void typeText(QWidget* w, const QString& s) {
    for (const QChar c : s) press(w, c.toUpper().unicode(), Qt::NoModifier, QString(c));
}

static void click(QWidget* target, const QPoint& p) {
    for (QEvent::Type type : {QEvent::MouseButtonPress, QEvent::MouseButtonRelease}) {
        QMouseEvent ev(type, p, p, p, Qt::LeftButton, type == QEvent::MouseButtonPress ? Qt::LeftButton : Qt::NoButton,
                       Qt::NoModifier, QPointingDevice::primaryPointingDevice());
        QApplication::sendEvent(target, &ev);
    }
}

static void trigger(MainWindow& w, const QString& seq) {
    for (QAction* a : w.findChildren<QAction*>()) {
        if (a->shortcut() == QKeySequence(seq)) {
            a->trigger();
            return;
        }
    }
    std::printf("FAIL: no action bound to %s\n", qPrintable(seq));
    ++failures;
}

static const char* kJ17 =
    "## Symmetry and Group Theory\n"
    "\t- First question here?\n"
    "\t :LOGBOOK:\n"
    "\t CLOCK: [2026-10-17 Sat 20:19:16]--[2026-10-17 Sat 20:19:17] =>  00:00:01\n"
    "\t :END:\n"
    "\t- Second question, with $E=mc^2$ and [[Some Page]] and #tag\n"
    "- ## Main Group";

static const char* kJ18 = "## Main Group\n\t- completed some slides\n- ## POC\n\t- placeholder note";

// The first block is only "id:: ..." (no bullet) with children under it: an EMPTY block.
static const char* kJ21 =
    "id:: 6ab0411c-a955-47a7-b34c-7f4aa5711473\n"
    "\t- Currently, I need to do the kinetic isotope effect first and then move on to the derivation.\n"
    "\t- The Hammett equation part must include the delta delta G part.\n"
    "\t- **I need to look into all the examples I've missed in the class, by missing classes.**\n"
    "- ## Equilibrium\n"
    "\t- I feel decently confident in equilibrium, I shall ask for problems";

static QImage wallpaper(const QColor& a, const QColor& b, const QColor& c) {
    QImage img(640, 360, QImage::Format_RGB32);
    QPainter p(&img);
    QLinearGradient g(0, 0, 640, 360);
    g.setColorAt(0, a);
    g.setColorAt(0.55, b);
    g.setColorAt(1, c);
    p.fillRect(img.rect(), g);
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(255, 255, 255, 40));
    p.drawEllipse(QPoint(470, 120), 70, 70);
    return img;
}

static const char* kShowcase =
    "- ## Heading level two\n"
    "- TODO Finish the **catalysis** problem set\n"
    "- Plain text with [[Some Page]], a #tag, `inline code`, a [link](https://example.org), and math $E = mc^2$ and $\\ce{H2O}$.\n"
    "\t- A nested child block that is long enough that it has to wrap onto a second line when the window is narrow\n"
    "\t\t- A grandchild\n"
    "\t- Another child\n"
    "- A collapsed parent\n"
    "  collapsed:: true\n"
    "\t- hidden child\n"
    "- DONE Something finished\n"
    "- A multi-line block\n"
    "  second line of the same block\n"
    "- Last block";

static void settle() {
    for (int i = 0; i < 5; ++i) {
        QApplication::processEvents();
        QApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    }
}

static void shot(MainWindow& w, const QString& name) {
    const QByteArray dir = qgetenv("GRAPHITE_SHOTS");
    if (dir.isEmpty()) return;
    QDir().mkpath(QString::fromUtf8(dir));
    settle();
    const QImage img = w.grab().toImage().convertToFormat(QImage::Format_ARGB32);
    if (!Theme::instance().transparent()) {
        img.save(QString::fromUtf8(dir) + "/" + name + ".png");
        return;
    }
    // Show transparent mode over a fake wallpaper so it can be judged by eye.
    QImage out(img.size(), QImage::Format_ARGB32);
    QPainter p(&out);
    QLinearGradient g(0, 0, img.width(), img.height());
    g.setColorAt(0.0, QColor(24, 70, 90));
    g.setColorAt(0.5, QColor(120, 60, 140));
    g.setColorAt(1.0, QColor(230, 120, 60));
    p.fillRect(out.rect(), g);
    p.drawImage(0, 0, img);
    p.end();
    out.save(QString::fromUtf8(dir) + "/" + name + ".png");
}

int main(int argc, char** argv) {
    std::setvbuf(stdout, nullptr, _IONBF, 0);  // every FAIL line reaches the log, even if the run dies
    // Settings go to a fresh folder each run: no leftovers between runs, nothing in ~/.config.
    QTemporaryDir config;
    qputenv("XDG_CONFIG_HOME", config.path().toUtf8());
    QTemporaryDir data;  // the art folder (XDG_DATA_HOME/graphite/art): never the real ~/.local/share
    qputenv("XDG_DATA_HOME", data.path().toUtf8());
    qputenv("GRAPHITE_NO_TRASH", "1");  // deleted pages go to the test graph's .recycle, not the real Trash
    // Updates are looked for in a fake source folder, as old as can be (nothing newer than us).
    QTemporaryDir source;
    auto writeOld = [&](const QString& rel, const QByteArray& text) {
        QDir().mkpath(QFileInfo(source.path() + "/" + rel).absolutePath());
        QFile f(source.path() + "/" + rel);
        if (!f.open(QIODevice::WriteOnly)) return;
        f.write(text);
        f.flush();  // written now, or closing would set the time again
        f.setFileTime(QDateTime(QDate(2020, 1, 1), QTime(0, 0)), QFileDevice::FileModificationTime);
    };
    writeOld("CMakeLists.txt", "project(graphite VERSION 0.0.1 LANGUAGES CXX)\n");
    writeOld("src/ui/x.cpp", "x");
    qputenv("GRAPHITE_UPDATE_REPO", source.path().toUtf8());
    QApplication app(argc, argv);
    QTemporaryDir tmp;
    CHECK(tmp.isValid());
    const QString g = tmp.path();
    const QString j17 = g + "/journals/2026_10_17.md";
    const QString j18 = g + "/journals/2026_10_18.md";
    writeFile(j17, kJ17);
    writeFile(j18, kJ18);
    writeFile(g + "/pages/contents.md", "-");
    writeFile(g + "/pages/Showcase.md", kShowcase);
    writeFile(g + "/journals/2026_09_21.md", kJ21);
    writeFile(g + "/logseq/config.edn", "{:meta/version 1\n :preferred-workflow :now}\n");

    MainWindow w(g);
    w.show();
    settle();
    OutlinerView* view = w.view();
    const QString todayPath = g + "/journals/" + QDate::currentDate().toString("yyyy_MM_dd") + ".md";

    // 1. Opening Today without typing must not leave an empty file behind.
    CHECK(w.openPath(j17));
    CHECK(!QFileInfo::exists(todayPath));
    CHECK(view->document() && view->document()->size() == 4);

    // 2. Edit one block: only that line changes on disk, the LOGBOOK keeps its odd indent.
    view->focusBlock(1);
    QTextEdit* ed = editorOf(w);
    CHECK(ed && ed->isVisible());
    CHECK_EQ(ed->toPlainText(), "First question here?");
    press(ed, Qt::Key_End);
    typeText(ed, " Extra");
    CHECK(w.openPath(j18));  // switching pages saves
    QString expected = kJ17;
    expected.replace("First question here?", "First question here? Extra");
    CHECK_EQ(readFile(j17), expected);

    // The pre-edit file was kept in the tree's .graphite/bak/journals.
    const QStringList baks = QDir(g + "/.graphite/bak/journals").entryList({"2026_10_17.graphite-*.bak.md"});
    CHECK(baks.size() == 1);
    if (!baks.isEmpty()) CHECK_EQ(readFile(g + "/.graphite/bak/journals/" + baks[0]), kJ17);
    const QString afterEdit = readFile(j17);

    // 3. Enter splits at the cursor, Tab nests, Shift+Tab un-nests, Backspace re-joins.
    CHECK(w.openPath(j17));
    view->focusBlock(2);
    ed = editorOf(w);
    const QString second = ed->toPlainText();
    QTextCursor c = ed->textCursor();
    c.setPosition(7);
    ed->setTextCursor(c);
    press(ed, Qt::Key_Return);
    Document* d = view->document();
    CHECK(d->size() == 5);
    CHECK_EQ(QString::fromStdString(d->at(2).text), "Second ");
    CHECK_EQ(QString::fromStdString(d->at(3).text), second.mid(7));
    CHECK(view->currentBlock() == 3);
    press(editorOf(w), Qt::Key_Tab);
    CHECK(d->at(3).depth == 2);
    press(editorOf(w), Qt::Key_Backtab, Qt::ShiftModifier);
    CHECK(d->at(3).depth == 1);
    press(editorOf(w), Qt::Key_Backspace);  // cursor is at the start of the new block
    CHECK(d->size() == 4);
    CHECK_EQ(QString::fromStdString(d->at(2).text), second);
    CHECK(w.openPath(j18));
    CHECK_EQ(readFile(j17), afterEdit);  // net change of zero must leave the file untouched

    // 4. Reordering and collapsing.
    CHECK(w.openPath(j17));
    view->focusBlock(1);
    d = view->document();
    press(editorOf(w), Qt::Key_Down, Qt::AltModifier | Qt::ShiftModifier);
    CHECK_EQ(QString::fromStdString(d->at(1).text).left(6), "Second");
    press(editorOf(w), Qt::Key_Up, Qt::AltModifier | Qt::ShiftModifier);
    CHECK_EQ(QString::fromStdString(d->at(1).text).left(5), "First");
    view->focusBlock(0);
    press(editorOf(w), Qt::Key_Up, Qt::ControlModifier);
    CHECK(d->at(0).collapsed());
    CHECK((d->visible() == std::vector<int>{0, 3}));
    trigger(w, "Ctrl+S");
    CHECK(readFile(j17).contains("collapsed:: true"));
    press(editorOf(w), Qt::Key_Down, Qt::ControlModifier);
    CHECK(!d->at(0).collapsed());
    CHECK((d->visible() == std::vector<int>{0, 1, 2, 3}));

    // 5. A brand-new journal: typing creates the file, Enter makes a second block.
    CHECK(w.openPath(todayPath, true));
    view->focusBlock(0);
    typeText(editorOf(w), "hello $x^2$");
    press(editorOf(w), Qt::Key_Return);
    typeText(editorOf(w), "second");
    trigger(w, "Ctrl+S");
    CHECK_EQ(readFile(todayPath), "- hello $x^2$\n- second");

    // 6. Following links: existing page, and a new page that exists only once typed in.
    view->linkActivated("contents");
    CHECK_EQ(QFileInfo(todayPath).exists() ? "ok" : "gone", "ok");
    view->linkActivated("Brand New Page");
    CHECK(!QFileInfo::exists(g + "/pages/Brand New Page.md"));
    view->focusBlock(0);
    typeText(editorOf(w), "x");
    trigger(w, "Ctrl+S");
    CHECK_EQ(readFile(g + "/pages/Brand New Page.md"), "- x");

    // 7. Ctrl+T switches to transparent backgrounds and back; text stays opaque.
    CHECK(!Theme::instance().transparent());
    trigger(w, "Ctrl+T");
    CHECK(Theme::instance().transparent());
    CHECK(Theme::instance().bg().alpha() == 0 && Theme::instance().text().alpha() == 255);
    trigger(w, "Ctrl+T");
    CHECK(!Theme::instance().transparent());

    // Blur goes through Hyprland only on a real session: never from a headless test.
    {
        BlurControl probe;
        CHECK(!probe.available());
        int size = 0, passes = 0;
        BlurControl::levelToBlur(0, &size, &passes);
        CHECK(size == 1 && passes == 1);
        BlurControl::levelToBlur(BlurControl::kMaxLevel, &size, &passes);
        CHECK(size == 11 && passes == 4);
        BlurControl::levelToBlur(99, &size, &passes);  // clamped
        CHECK(size == 11);
    }

    // 9. Mouse: click to edit, chevron to collapse, click on a [[link]] to follow it.
    CHECK(w.openPath(g + "/pages/Showcase.md"));
    d = view->document();
    QWidget* vp = view->viewport();
    CHECK(d->size() == 11);
    click(vp, view->textRect(5).center());
    CHECK(view->currentBlock() == 5);
    press(editorOf(w), Qt::Key_Escape);
    CHECK(view->currentBlock() == -1);

    const QRect r2 = view->textRect(2);
    click(vp, QPoint(r2.left() - 23, r2.top() + 8));  // chevron of the block with children
    CHECK(d->at(2).collapsed());
    CHECK(view->textRect(3).isNull() && view->textRect(5).isNull());
    click(vp, QPoint(view->textRect(2).left() - 23, view->textRect(2).top() + 8));
    CHECK(!d->at(2).collapsed());
    CHECK(!view->textRect(3).isNull());

    // Sweep along the first line of block 2 until a click lands on a link; the window
    // follows it, so remember what was activated.
    QString followed;
    QObject::connect(view, &OutlinerView::linkActivated, [&](const QString& s) { if (followed.isEmpty()) followed = s; });
    view->endEdit();
    for (int x = view->textRect(2).left(); x < view->textRect(2).right() && followed.isEmpty(); x += 3)
        click(vp, QPoint(x, view->textRect(2).top() + 8));
    CHECK_EQ(followed, "Some Page");

    // 10. Arrow keys move between blocks; the cursor column is kept.
    CHECK(w.openPath(g + "/pages/Showcase.md"));
    view->focusBlock(1);
    press(editorOf(w), Qt::Key_Down);
    CHECK(view->currentBlock() == 2);
    press(editorOf(w), Qt::Key_Up);
    CHECK(view->currentBlock() == 1);
    press(editorOf(w), Qt::Key_Up);
    CHECK(view->currentBlock() == 0);
    press(editorOf(w), Qt::Key_Up);  // first block: stays put
    CHECK(view->currentBlock() == 0);

    // 11. Typing "## " turns a block into a heading immediately.
    view->focusBlock(10, 0);
    const qreal before = editorOf(w)->font().pointSizeF();
    typeText(editorOf(w), "## ");
    CHECK(editorOf(w)->font().pointSizeF() > before);
    CHECK(editorOf(w)->font().bold());

    // 12. A long page scrolls to keep the edited block in view, and back to the top.
    QString longPage;
    for (int i = 0; i < 80; ++i) longPage += QString("- line %1\n").arg(i);
    writeFile(g + "/pages/Long.md", longPage);
    w.resize(1000, 500);
    settle();
    view->endEdit();
    CHECK(w.openPath(g + "/pages/Long.md"));
    CHECK(view->verticalScrollBar()->maximum() > 0);
    view->focusBlock(79);
    settle();
    CHECK(view->verticalScrollBar()->value() > 0);
    ed = editorOf(w);
    CHECK(ed->y() >= 0 && ed->y() + ed->height() <= vp->height());
    view->focusBlock(0);
    settle();
    CHECK(view->verticalScrollBar()->value() == 0);

    // 14. Bracket pairing: pairs, step-over, wrap a selection, backspace removes an empty pair.
    CHECK(w.openPath(g + "/pages/Long.md"));
    view->focusBlock(0);
    ed = editorOf(w);
    ed->selectAll();
    press(ed, Qt::Key_Backspace);
    typeText(ed, "\\ce{H2O} (");
    CHECK_EQ(ed->toPlainText(), "\\ce{H2O} ()");
    press(ed, Qt::Key_Backspace);
    CHECK_EQ(ed->toPlainText(), "\\ce{H2O} ");
    typeText(ed, "[[Some Page]] x");
    CHECK_EQ(ed->toPlainText(), "\\ce{H2O} [[Some Page]] x");
    c = ed->textCursor();
    c.setPosition(0);
    c.setPosition(8, QTextCursor::KeepAnchor);  // select \ce{H2O}
    ed->setTextCursor(c);
    typeText(ed, "$");  // not a pair: replaces the selection
    ed->undo();
    CHECK_EQ(ed->toPlainText(), "\\ce{H2O} [[Some Page]] x");
    c = ed->textCursor();
    c.setPosition(11);
    c.setPosition(20, QTextCursor::KeepAnchor);  // "Some Page" without its brackets
    ed->setTextCursor(c);
    typeText(ed, "(");
    CHECK_EQ(ed->toPlainText(), "\\ce{H2O} [[(Some Page)]] x");
    CHECK_EQ(ed->textCursor().selectedText(), "Some Page");
    ed->undo();
    CHECK_EQ(ed->toPlainText(), "\\ce{H2O} [[Some Page]] x");
    press(ed, Qt::Key_End);
    typeText(ed, " ```");
    CHECK_EQ(ed->toPlainText(), "\\ce{H2O} [[Some Page]] x ```");
    c = ed->textCursor();
    c.setPosition(0);
    ed->setTextCursor(c);
    typeText(ed, "(");  // text follows: no pair
    CHECK(ed->toPlainText().startsWith("(\\ce"));

    // 15. Ctrl+N opens the in-window prompt; Enter opens the page. Ctrl+/ lists every key.
    trigger(w, "Ctrl+N");
    QPlainTextEdit* input = w.findChild<PagePrompt*>() ? w.findChild<PagePrompt*>()->findChild<QPlainTextEdit*>() : nullptr;
    CHECK(input && input->isVisible() && input->hasFocus());
    if (input) {
        typeText(input, "Prompt Page");
        shot(w, "25_title_card_prompt");
        press(input, Qt::Key_Return);  // on to the caption
        typeText(QApplication::focusWidget(), "The page that was asked for");
        press(QApplication::focusWidget(), Qt::Key_Return);
    }
    settle();
    // The caption is the page's description:: property; writing starts in the block below it.
    CHECK(view->document() && view->document()->size() == 2);
    CHECK_EQ(QString::fromStdString(view->document()->at(0).text), "description:: The page that was asked for");
    CHECK(view->currentBlock() == 1);
    CHECK(!w.findChild<PagePrompt*>());
    CHECK(w.findChild<TitleFlash*>() != nullptr);  // first opening: the title card plays
    QTest::qWait(500);                              // fully faded in
    shot(w, "26_title_flash");
    QTest::qWait(1600);
    settle();
    CHECK(!w.findChild<TitleFlash*>());             // and it goes away by itself
    shot(w, "27_caption");
    trigger(w, "Ctrl+S");
    CHECK_EQ(readFile(g + "/pages/Prompt Page.md"), "description:: The page that was asked for\n-");
    view->focusBlock(1);
    trigger(w, "Ctrl+N");
    press(QApplication::focusWidget(), Qt::Key_Escape);
    settle();
    CHECK(!w.findChild<PagePrompt*>());
    CHECK(view->currentBlock() == 1);  // focus went back to the block being edited

    trigger(w, "Ctrl+/");
    settle();
    QStringList shown;
    for (QLabel* l : w.findChildren<QLabel*>()) shown << l->text();
    for (QAction* a : w.findChildren<QAction*>())
        if (a->shortcut() != QKeySequence("F1"))
            CHECK(shown.contains(a->shortcut().toString().split('+').last()));
    CHECK(shown.join('|').contains("Typed in pairs"));
    shot(w, "6_keys");
    trigger(w, "Ctrl+/");
    settle();
    CHECK(!shown.isEmpty() && !w.findChild<KeySheet*>());
    trigger(w, "Ctrl+N");
    shot(w, "7_new_page");
    press(QApplication::focusWidget(), Qt::Key_Escape);
    settle();

    // 16. Title font picker: default is v0.1's; arrows preview on the page, Esc reverts, Enter keeps.
    CHECK(Theme::instance().titleFamily().isEmpty());
    CHECK(Theme::titleFontFor(QString()).weight() == QFont::Black);
    trigger(w, "Ctrl+Shift+F");
    settle();
    QListWidget* fonts = w.findChild<FontPicker*>() ? w.findChild<FontPicker*>()->findChild<QListWidget*>() : nullptr;
    CHECK(fonts && fonts->count() > 1 && fonts->currentRow() == 0);
    press(QApplication::focusWidget(), Qt::Key_Down);
    const QString nextFont = fonts ? fonts->currentItem()->text() : QString();
    for (int i = 0; i < 20; ++i) QApplication::processEvents(QEventLoop::AllEvents, 10);
    QTest::qWait(200);
    CHECK_EQ(Theme::instance().titleFamily(), nextFont);  // live preview
    press(QApplication::focusWidget(), Qt::Key_Escape);
    settle();
    CHECK(Theme::instance().titleFamily().isEmpty());  // reverted
    trigger(w, "Ctrl+Shift+F");
    settle();
    fonts = w.findChild<FontPicker*>()->findChild<QListWidget*>();
    // Everything below comes from the picker's own rows (row 0 = Default, then the families it
    // lists: it skips private ones, so QFontDatabase::families() is not the same set), and its rule:
    // a row shows when its text contains the search text, case-insensitively. CI images have few fonts.
    QStringList rowFamilies;
    for (int i = 1; fonts && i < fonts->count(); ++i) rowFamilies << fonts->item(i)->data(Qt::UserRole).toString();
    CHECK(!rowFamilies.isEmpty());
    const QString serifWanted = rowFamilies.contains("DejaVu Serif") ? QString("DejaVu Serif") : rowFamilies.first();
    QString monoWanted;
    for (const QString& f : rowFamilies)
        if (f != serifWanted && !f.contains(serifWanted, Qt::CaseInsensitive) && !serifWanted.contains(f, Qt::CaseInsensitive)
            && QFontDatabase::isFixedPitch(f)) { monoWanted = f; break; }
    if (monoWanted.isEmpty())
        monoWanted = rowFamilies.contains("DejaVu Sans Mono") ? QString("DejaVu Sans Mono") : rowFamilies.last();
    // What the picker should show and select for a search text, read from its rows as they are now.
    auto expectFor = [&](const QString& needle, int& count, QString& firstFamily) {
        count = 0;
        firstFamily.clear();
        bool found = false;
        for (int i = 0; i < fonts->count(); ++i)
            if (fonts->item(i)->text().contains(needle, Qt::CaseInsensitive)) {
                if (!found) firstFamily = fonts->item(i)->data(Qt::UserRole).toString();
                found = true;
                ++count;
            }
    };
    int shownWanted = 0, total = fonts->count();
    QString titlePicked;
    expectFor(serifWanted, shownWanted, titlePicked);
    CHECK(shownWanted >= 1);
    typeText(QApplication::focusWidget(), serifWanted);
    QTest::qWait(200);
    int visible = 0;
    for (int i = 0; i < fonts->count(); ++i) visible += !fonts->item(i)->isHidden();
    CHECK(visible == shownWanted);  // the filter shows exactly the matching rows
    CHECK(visible <= total);
    CHECK(fonts->currentItem() && !fonts->currentItem()->isHidden());
    CHECK_EQ(fonts->currentItem()->data(Qt::UserRole).toString(), titlePicked);
    CHECK_EQ(Theme::instance().titleFamily(), titlePicked);  // applied live
    shot(w, "8_font_picker");
    press(QApplication::focusWidget(), Qt::Key_Return);
    settle();
    CHECK_EQ(Theme::instance().titleFamily(), titlePicked);
    CHECK(!w.findChild<FontPicker*>());
    Theme::instance().setTitleFamily("No Such Font Family");
    CHECK(Theme::instance().titleFont().weight() == QFont::Black);  // falls back to the default
    Theme::instance().setTitleFamily(QString());

    // The same picker sets the body font: Tab switches tabs, Enter keeps both, Esc restores both.
    trigger(w, "Ctrl+Shift+F");
    settle();
    typeText(QApplication::focusWidget(), serifWanted);
    press(QApplication::focusWidget(), Qt::Key_Tab);                        // title chosen, now body
    press(QApplication::focusWidget(), Qt::Key_A, Qt::ControlModifier);     // select the search text
    fonts = w.findChild<FontPicker*>()->findChild<QListWidget*>();
    QString bodyPicked;
    expectFor(monoWanted, shownWanted, bodyPicked);  // row 0's text is per tab, so read it now
    typeText(QApplication::focusWidget(), monoWanted);
    QTest::qWait(200);
    visible = 0;
    for (int i = 0; i < fonts->count(); ++i) visible += !fonts->item(i)->isHidden();
    CHECK(shownWanted >= 1 && visible == shownWanted);
    CHECK(fonts->currentItem() && fonts->currentItem()->data(Qt::UserRole).toString() == bodyPicked);
    CHECK_EQ(Theme::instance().bodyFamily(), bodyPicked);  // live preview
    press(QApplication::focusWidget(), Qt::Key_Return);
    settle();
    CHECK_EQ(Theme::instance().titleFamily(), titlePicked);
    CHECK_EQ(Theme::instance().bodyFamily(), bodyPicked);
    CHECK(!QFontInfo(Theme::instance().bodyFont()).family().isEmpty());
    trigger(w, "Ctrl+Shift+F");
    settle();
    press(QApplication::focusWidget(), Qt::Key_Tab);
    press(QApplication::focusWidget(), Qt::Key_Up);  // preview something else...
    press(QApplication::focusWidget(), Qt::Key_Escape);  // ...and cancel
    settle();
    CHECK_EQ(Theme::instance().bodyFamily(), bodyPicked);
    Theme::instance().setTitleFamily(QString());
    Theme::instance().setBodyFamily(QString());

    // 17. Backlinks: "Some Page" does not exist yet, but blocks on other pages link to it.
    w.resize(1180, 760);
    settle();
    view->linkActivated("Some Page");
    CHECK(view->referenceCount() == 3);  // Showcase, the Oct 17 journal, and Long (typed in step 14)
    CHECK(!view->referenceRect(0, -1).isNull() && !view->referenceRect(0, 0).isNull());
    shot(w, "9_backlinks");
    {   // clicking a reference opens its page with that block being edited
        const int before = w.index().pageOf(j17);
        CHECK(before >= 0);
        const QRect rr = view->referenceRect(0, 0);
        if (!rr.isNull()) {
            view->verticalScrollBar()->setValue(qMax(0, rr.top() - 100));
            settle();
            click(vp, view->referenceRect(0, 0).center() + QPoint(-rr.width() / 2 + 4, 0));
        }
        CHECK(view->currentBlock() >= 0);
        CHECK(view->document() && view->document() != nullptr);
    }
    // A tag is a page too: #tag shows the blocks that carry it.
    view->linkActivated("tag");
    CHECK(view->referenceCount() >= 2);
    CHECK(!QFileInfo::exists(g + "/pages/tag.md"));  // looking at a tag page creates nothing

    // A link typed now shows up on the target page once you go there (the index follows saves).
    CHECK(w.openPath(g + "/pages/contents.md"));
    view->focusBlock(0);
    press(editorOf(w), Qt::Key_End);
    typeText(editorOf(w), " see [[Fresh Target");  // pairing closes the brackets
    view->linkActivated("Fresh Target");
    CHECK(view->referenceCount() == 1);

    // 18. Ctrl+K: finds blocks by text and pages by title; Enter opens; offers to create.
    trigger(w, "Ctrl+K");
    settle();
    CHECK(w.findChild<QuickSwitcher*>() != nullptr);
    typeText(QApplication::focusWidget(), "catalysis");
    shot(w, "10_search");
    press(QApplication::focusWidget(), Qt::Key_Return);
    settle();
    CHECK(view->document() && QString::fromStdString(view->document()->at(view->currentBlock()).text).contains("catalysis"));
    trigger(w, "Ctrl+K");
    typeText(QApplication::focusWidget(), "showcase");
    press(QApplication::focusWidget(), Qt::Key_Return);
    settle();
    CHECK(w.index().pageOf(g + "/pages/Showcase.md") >= 0);
    CHECK(view->currentBlock() == -1 || view->document()->size() == 11);
    trigger(w, "Ctrl+K");
    typeText(QApplication::focusWidget(), "Totally New Topic");
    {
        QListWidget* results = w.findChild<QuickSwitcher*>()->findChild<QListWidget*>();
        CHECK(results && results->count() >= 1 && results->item(0)->text().startsWith("Create page"));
    }
    press(QApplication::focusWidget(), Qt::Key_Return);
    settle();
    CHECK(view->currentBlock() == 0);
    CHECK(!QFileInfo::exists(g + "/pages/Totally New Topic.md"));  // only once something is typed
    press(editorOf(w), Qt::Key_Escape);

    // 19. Tasks. Ctrl+Enter in a block cycles TODO -> DOING -> DONE -> plain.
    CHECK(w.openPath(g + "/pages/Showcase.md"));
    view->focusBlock(9);  // "A multi-line block\nsecond line..."
    const QString plainText = editorOf(w)->toPlainText();
    press(editorOf(w), Qt::Key_Return, Qt::ControlModifier);
    CHECK_EQ(editorOf(w)->toPlainText(), "TODO " + plainText);
    press(editorOf(w), Qt::Key_Return, Qt::ControlModifier);
    press(editorOf(w), Qt::Key_Return, Qt::ControlModifier);
    press(editorOf(w), Qt::Key_Return, Qt::ControlModifier);
    CHECK_EQ(editorOf(w)->toPlainText(), plainText);
    press(editorOf(w), Qt::Key_Escape);

    // A task on a page that is not open, marked done from the panel: only its line changes.
    const QString labPath = g + "/pages/Lab.md";
    const QString lab = "- TODO write lab report\n  DEADLINE: <2026-10-25 Sun>\n- odd  spacing   kept\n\t- child";
    writeFile(labPath, lab);
    trigger(w, "Ctrl+Shift+T");
    settle();
    TasksPanel* panel = w.findChild<TasksPanel*>();
    CHECK(panel != nullptr);
    if (panel) {
        typeText(QApplication::focusWidget(), "lab report");
        shot(w, "11_tasks");
        press(QApplication::focusWidget(), Qt::Key_Return, Qt::ControlModifier);
        settle();
    }
    QString labDone = lab;
    labDone.replace("- TODO write", "- DONE write");
    CHECK_EQ(readFile(labPath), labDone);
    CHECK(QDir(g + "/.graphite/bak/pages").entryList({"Lab.graphite-*.bak.md"}).size() == 1);
    press(QApplication::focusWidget(), Qt::Key_Escape);
    settle();
    // The panel lists the open page's TODO too, and Enter jumps to it.
    trigger(w, "Ctrl+Shift+T");
    settle();
    typeText(QApplication::focusWidget(), "catalysis");
    press(QApplication::focusWidget(), Qt::Key_Return);
    settle();
    CHECK(view->currentBlock() == 1 && w.index().pageOf(g + "/pages/Showcase.md") >= 0);
    press(editorOf(w), Qt::Key_Escape);

    // 20. Live formula preview while editing, with molar masses; hover on a typeset formula.
    CHECK(w.openPath(g + "/pages/Showcase.md"));
    view->focusBlock(10, 0);
    ed = editorOf(w);
    ed->selectAll();
    typeText(ed, "Neutralise: $\\ce{H2SO4 + 2NaOH -> Na2SO4 + 2H2O}$");  // "}" steps over the paired one
    CHECK_EQ(ed->toPlainText(), "Neutralise: $\\ce{H2SO4 + 2NaOH -> Na2SO4 + 2H2O}$");
    c = ed->textCursor();
    c.setPosition(20);  // inside the formula
    ed->setTextCursor(c);
    settle();
    QWidget* preview = nullptr;
    for (QWidget* ch : vp->findChildren<QWidget*>(Qt::FindDirectChildrenOnly))
        if (ch != ed && ch->isVisible() && QString(ch->metaObject()->className()) == "QWidget") preview = ch;
    CHECK(preview != nullptr);
    shot(w, "12_formula_preview");
    c.setPosition(2);  // outside: the card goes away
    ed->setTextCursor(c);
    settle();
    CHECK(!preview || !preview->isVisible());
    press(ed, Qt::Key_Escape);
    {   // sweep along block 2's line until the mouse is over $\ce{H2O}$
        QString found;
        const QRect r = view->textRect(2);
        for (int x = r.left(); x < r.right() && found.isEmpty(); x += 2) {
            QMouseEvent mv(QEvent::MouseMove, QPointF(x, r.top() + 9), QPointF(x, r.top() + 9), Qt::NoButton, Qt::NoButton, Qt::NoModifier);
            QApplication::sendEvent(vp, &mv);
            if (QToolTip::isVisible()) found = QToolTip::text();
        }
        CHECK(found.contains("H2O 18.02"));
    }

    // 21. Snippets while typing.
    CHECK(w.openPath(g + "/pages/Long.md"));
    view->focusBlock(3, 0);
    ed = editorOf(w);
    ed->selectAll();
    typeText(ed, "Enthalpy ;dH");
    shot(w, "13_snippets");
    press(ed, Qt::Key_Tab);
    CHECK_EQ(ed->toPlainText(), "Enthalpy $\\Delta H^\\circ$");
    CHECK(view->document()->at(3).depth == 0);  // Tab inserted the snippet, did not indent
    typeText(ed, " and \\ce");
    press(ed, Qt::Key_Space);  // exact trigger + Space
    typeText(ed, "A ;eq");
    press(ed, Qt::Key_Return);  // Enter accepts too, inside \ce it is the TeX arrow
    typeText(ed, " B");
    CHECK_EQ(ed->toPlainText(), "Enthalpy $\\Delta H^\\circ$ and $\\ce{A <=> B}$");
    press(ed, Qt::Key_End);  // out of the formula
    typeText(ed, " at ;deg");
    press(ed, Qt::Key_Tab);
    CHECK(ed->toPlainText().endsWith("$ at \xC2\xB0"));  // outside maths: the degree sign
    typeText(ed, " ;today");
    press(ed, Qt::Key_Tab);
    CHECK(ed->toPlainText().contains(" [[" + QLocale::c().toString(QDate::currentDate(), "MMM ")) &&
          ed->toPlainText().endsWith(QLocale::c().toString(QDate::currentDate(), ", yyyy") + "]]"));
    // Formatting snippets, and Ctrl+B / Ctrl+I around a selection (again: taken off).
    ed->selectAll();
    typeText(ed, "a ;b");
    press(ed, Qt::Key_Tab);
    typeText(ed, "bold");
    press(ed, Qt::Key_End);
    typeText(ed, " ;s");
    press(ed, Qt::Key_Space);
    typeText(ed, "gone");
    press(ed, Qt::Key_End);
    typeText(ed, " ;hl");
    press(ed, Qt::Key_Tab);
    typeText(ed, "key");
    CHECK_EQ(ed->toPlainText(), "a **bold** ~~gone~~ ^^key^^");
    press(ed, Qt::Key_End);
    typeText(ed, " word");
    c = ed->textCursor();
    c.movePosition(QTextCursor::Left, QTextCursor::KeepAnchor, 4);
    ed->setTextCursor(c);
    press(ed, Qt::Key_I, Qt::ControlModifier);
    CHECK(ed->toPlainText().endsWith(" *word*"));
    c = ed->textCursor();
    c.movePosition(QTextCursor::Left, QTextCursor::KeepAnchor, 6);
    ed->setTextCursor(c);
    press(ed, Qt::Key_I, Qt::ControlModifier);
    CHECK(ed->toPlainText().endsWith(" word"));
    press(ed, Qt::Key_End);
    press(ed, Qt::Key_B, Qt::ControlModifier);
    typeText(ed, "x");
    CHECK(ed->toPlainText().endsWith(" word**x**"));
    {
        const int fb = view->currentBlock();
        press(ed, Qt::Key_Escape);
        settle();
        const QString shown = view->renderedText(fb);
        CHECK(shown.contains("bold") && !shown.contains("**") && !shown.contains("~~") && shown.contains("gone"));
        view->focusBlock(fb, 0);
        ed = editorOf(w);
    }
    ed->selectAll();
    typeText(ed, "$\\ce");  // half-typed formula: the maths form, and the $ gets closed
    press(ed, Qt::Key_Tab);
    CHECK_EQ(ed->toPlainText(), "$\\ce{}$");
    ed->selectAll();
    typeText(ed, "plain ;zzz");
    press(ed, Qt::Key_Escape);  // nothing matched: Esc ends editing as usual
    CHECK(view->currentBlock() == -1);
    view->focusBlock(3);
    press(editorOf(w), Qt::Key_Tab);  // no popup: Tab indents
    CHECK(view->document()->at(3).depth == 1);
    press(editorOf(w), Qt::Key_Backtab, Qt::ShiftModifier);
    press(editorOf(w), Qt::Key_Escape);
    trigger(w, "Ctrl+/");
    settle();
    press(QApplication::focusWidget(), Qt::Key_Tab);
    shot(w, "14_snippet_sheet");
    press(QApplication::focusWidget(), Qt::Key_Escape);
    settle();

    // 22. Images: shown scaled from the header, decoded at display size, pasted into assets/.
    {
        QDir().mkpath(g + "/assets");
        QImage big(2000, 1200, QImage::Format_RGB32);
        big.fill(QColor(90, 60, 160));
        QPainter(&big).fillRect(200, 200, 800, 500, QColor(156, 255, 60));
        CHECK(big.save(g + "/assets/board photo.png"));
        writeFile(g + "/pages/Pics.md", "- Board:\n  ![board](../assets/board%20photo.png)\n- missing ![x](../assets/nope.png)");
        CHECK(w.openPath(g + "/pages/Pics.md"));
        settle();
        const QRect r0 = view->textRect(0);
        CHECK(r0.height() > 300 && r0.height() < 460);  // 2000x1200 shown at 640x384
        shot(w, "15_image");
        w.grab();  // make sure it was painted
        // Decoded once, at display size: 640 x 384 x 4 bytes, not the 9.6 MB of the original.
        CHECK(ImageObject::cachedBytes() == 640 * 384 * 4);
        CHECK(view->textRect(1).height() < 40);     // missing file: the text stays, one line

        QGuiApplication::clipboard()->setImage(big);
        view->focusBlock(1);
        press(editorOf(w), Qt::Key_End);
        typeText(editorOf(w), " ");
        press(editorOf(w), Qt::Key_V, Qt::ControlModifier);
        const QString t = editorOf(w)->toPlainText();
        CHECK(t.contains("![image_") && t.contains("](../assets/image_") && t.endsWith(".png)"));
        const QStringList pasted = QDir(g + "/assets").entryList({"image_*_0.png"});
        CHECK(pasted.size() == 1);
        QGuiApplication::clipboard()->setText("plain words");
        press(editorOf(w), Qt::Key_V, Qt::ControlModifier);  // text still pastes as text
        CHECK(editorOf(w)->toPlainText().endsWith("plain words"));
        press(editorOf(w), Qt::Key_Escape);
        trigger(w, "Ctrl+S");
        CHECK(readFile(g + "/pages/Pics.md").startsWith("- Board:\n  ![board](../assets/board%20photo.png)\n"));
    }

    // 23. Export to PDF: a real PDF, the window keeps its colours, a long page gets more pages.
    {
        const PaletteColors screen = Theme::instance().palette();
        CHECK(w.openPath(g + "/pages/Showcase.md"));
        const QString pdf = g + "/Showcase.pdf";
        CHECK(w.exportPdf(pdf));
        CHECK(readFile(pdf).startsWith("%PDF"));
        CHECK(QFileInfo(pdf).size() > 2000);
        CHECK(Theme::instance().palette() == screen);  // the print palette was only borrowed
        const QByteArray dir = qgetenv("GRAPHITE_SHOTS");
        if (!dir.isEmpty()) QFile::copy(pdf, QString::fromUtf8(dir) + "/16_export.pdf");
        CHECK(w.openPath(g + "/pages/Long.md"));
        const QString longPdf = g + "/Long.pdf";
        CHECK(w.exportPdf(longPdf));
        if (!dir.isEmpty()) QFile::copy(longPdf, QString::fromUtf8(dir) + "/17_long.pdf");
        CHECK(readFile(longPdf).count("/Type /Page\n") + readFile(longPdf).count("/Type /Page\r") +
              readFile(longPdf).count("/Type /Page ") >= 2 || QFileInfo(longPdf).size() > QFileInfo(pdf).size());
    }

    // 24. The "/" command menu.
    CHECK(w.openPath(g + "/pages/Long.md"));
    view->focusBlock(5, 0);
    ed = editorOf(w);
    ed->selectAll();
    typeText(ed, "Kinetics /h2");
    shot(w, "18_commands");
    press(ed, Qt::Key_Return);
    CHECK_EQ(ed->toPlainText(), "## Kinetics ");
    typeText(ed, "/todo");
    press(ed, Qt::Key_Tab);
    CHECK_EQ(ed->toPlainText(), "## TODO Kinetics ");  // heading first, as Logseq writes it
    typeText(ed, "/due");  // matched by its other word
    press(ed, Qt::Key_Return);   // the date list opens: blank first, then today, tomorrow, ...
    shot(w, "39_date_list");
    for (int i = 0; i < 6; ++i) press(ed, Qt::Key_Down);  // "In a week"
    press(ed, Qt::Key_Return);
    shot(w, "42_time_list");
    press(ed, Qt::Key_Return);   // a deadline asks for a time next: the first row is none
    // On the task's own line, where the command was typed.
    CHECK_EQ(ed->toPlainText(), "## TODO Kinetics DEADLINE: <" + QLocale::c().toString(QDate::currentDate().addDays(7), "yyyy-MM-dd ddd") + ">");
    typeText(ed, " /sched");
    press(ed, Qt::Key_Return);
    press(ed, Qt::Key_Return);   // the blank one: type the date yourself
    typeText(ed, "2026-12-01");
    CHECK(ed->toPlainText().endsWith("SCHEDULED: <2026-12-01>"));
    press(ed, Qt::Key_End);
    typeText(ed, " /due");
    press(ed, Qt::Key_Return);
    press(ed, Qt::Key_Down);
    press(ed, Qt::Key_Return);   // today: replaces the earlier deadline, so only one remains
    typeText(ed, "23:59");       // typing at the time list: the time goes into the brackets
    CHECK(ed->toPlainText().count("DEADLINE:") == 1);
    CHECK(ed->toPlainText().contains("DEADLINE: <" + QLocale::c().toString(QDate::currentDate(), "yyyy-MM-dd ddd") + " 23:59>"));
    ed->selectAll();
    typeText(ed, "rate /chem");
    press(ed, Qt::Key_Return);
    typeText(ed, "A -> B");
    CHECK_EQ(ed->toPlainText(), "rate $\\ce{A -> B}$");
    ed->selectAll();
    typeText(ed, "see /tomorrow");
    press(ed, Qt::Key_Return);
    CHECK(ed->toPlainText().startsWith("see [[") && ed->toPlainText().endsWith(QLocale::c().toString(QDate::currentDate().addDays(1), ", yyyy") + "]]"));
    ed->selectAll();
    typeText(ed, "x https://a.org/path");  // "/" inside a URL is not a command
    CHECK(!view->viewport()->findChildren<QWidget*>().isEmpty());
    press(ed, Qt::Key_Return);              // so Enter splits the block as usual
    CHECK(view->currentBlock() == 6);
    view->focusBlock(5);
    ed = editorOf(w);
    view->setImagePicker([&] { return g + "/assets/board photo.png"; });
    ed->selectAll();
    typeText(ed, "/image");
    press(ed, Qt::Key_Return);
    CHECK(ed->toPlainText().startsWith("![board_photo](../assets/board_photo.png)"));
    CHECK(QFileInfo::exists(g + "/pages/../assets/board_photo.png"));
    ed->selectAll();
    typeText(ed, "/lecture");
    press(ed, Qt::Key_Return);
    d = view->document();
    CHECK(QString::fromStdString(d->at(5).text).startsWith("## Lecture "));
    CHECK_EQ(QString::fromStdString(d->at(6).text), "Topics");
    CHECK(d->at(6).depth == d->at(5).depth + 1 && d->at(9).depth == d->at(6).depth);
    CHECK_EQ(QString::fromStdString(d->at(9).text), "Questions to ask");
    CHECK(view->currentBlock() == 6);
    editorOf(w)->undo();  // nothing odd happens on undo inside the new block
    press(editorOf(w), Qt::Key_Escape);

    // 25. NERV panel: power readout from sysfs, the field, and animation that stops by itself.
    {
        QTemporaryDir ps;
        auto put = [&](const QString& rel, const QString& v) { writeFile(ps.path() + "/" + rel, v + "\n"); };
        put("ACAD/type", "Mains"); put("ACAD/online", "1");
        put("BAT1/type", "Battery"); put("BAT1/present", "1"); put("BAT1/capacity", "83"); put("BAT1/status", "Charging");
        PowerState pw = NervPanel::readPower(ps.path());
        CHECK(pw.battery && pw.external && pw.percent == 83 && pw.minutesLeft == -1);
        put("ACAD/online", "0"); put("BAT1/status", "Discharging");
        put("BAT1/energy_now", "30000000"); put("BAT1/power_now", "10000000");  // 30 Wh at 10 W: 3 h
        pw = NervPanel::readPower(ps.path());
        CHECK(!pw.external && pw.minutesLeft == 180);
        QTemporaryDir desk;  // no battery at all: a desktop, always "external"
        CHECK(NervPanel::readPower(desk.path()).external);

        bool inRange = true;
        for (double x = -20; x <= 20; x += 0.7)
            for (double y = -8; y <= 8; y += 0.9) {
                const int lv = NervPanel::fieldLevel(x, y, 1.3, false);
                inRange = inRange && lv >= 0 && lv <= 7;
            }
        CHECK(inRange);
        CHECK(NervPanel::fieldLevel(0, 0, 0, false) > NervPanel::fieldLevel(0, 0, 0, true));  // calm is quieter

        NervPanel* nerv = w.nervPanel();
        CHECK(nerv && nerv->isVisible());
        w.activateWindow();
        settle();
        if (QApplication::activeWindow() == &w) {
            nerv->pulse(300);
            CHECK(nerv->animating());
            QTest::qWait(150);
            shot(w, "19_nerv");
            // Activating the window also started the 3.2 s start-up pulse: wait that out.
            QElapsedTimer t;
            t.start();
            while (nerv->animating() && t.elapsed() < 4000) QTest::qWait(50);
            CHECK(!nerv->animating());  // back to a still picture: no timer running
            CHECK(t.elapsed() < 3600);
        }
        // Ctrl+Shift+A cycles Pulse -> Constant -> Off -> Pulse.
        CHECK(nerv->mode() == NervPanel::Mode::Pulse);
        // Constant animates only on the charger: the fake supply, plugged in, not this machine's.
        put("ACAD/online", "1"); put("BAT1/status", "Charging");
        qputenv("GRAPHITE_POWER_ROOT", ps.path().toUtf8());
        nerv->refreshPower();
        trigger(w, "Ctrl+Shift+A");
        CHECK(nerv->mode() == NervPanel::Mode::Constant && nerv->isVisible());
        if (QApplication::activeWindow() == &w) {
            QTest::qWait(400);
            CHECK(nerv->animating());  // constant: keeps going while the window is active
        }
        // On battery, Constant drops to pulses (the field conserves internal power).
        put("ACAD/online", "0"); put("BAT1/status", "Discharging");  // the fake battery, unplugged
        nerv->refreshPower();
        CHECK(!nerv->power().external);
        if (QApplication::activeWindow() == &w) {
            QElapsedTimer t3;
            t3.start();
            while (nerv->animating() && t3.elapsed() < 4000) QTest::qWait(50);
            CHECK(!nerv->animating());
            put("ACAD/online", "1");  // plugged back in: constant again
            nerv->refreshPower();
            CHECK(nerv->animating());
        }
        qunsetenv("GRAPHITE_POWER_ROOT");
        nerv->refreshPower();
        trigger(w, "Ctrl+Shift+A");
        CHECK(nerv->mode() == NervPanel::Mode::Off && !nerv->isVisible() && !nerv->animating());
        trigger(w, "Ctrl+Shift+A");
        CHECK(nerv->mode() == NervPanel::Mode::Pulse && nerv->isVisible());
        // Saving sends a short pulse.
        QElapsedTimer t2;
        t2.start();
        while (nerv->animating() && t2.elapsed() < 4000) QTest::qWait(50);
        view->focusBlock(0);
        typeText(editorOf(w), "x");
        trigger(w, "Ctrl+S");
        if (QApplication::activeWindow() == &w) CHECK(nerv->animating());
        press(editorOf(w), Qt::Key_Escape);
    }

    // 26. Text size: one number scales every font; the header grows with it. Settings card.
    {
        CHECK(w.openPath(g + "/pages/Showcase.md"));
        settle();
        const qreal base = Theme::instance().textSize();
        CHECK(base == Theme::kDefaultTextSize);
        const int headerTop = view->textRect(0).top();
        const qreal titleBefore = Theme::instance().titleFont().pointSizeF();
        trigger(w, "Ctrl+=");
        trigger(w, "Ctrl+=");
        settle();
        CHECK(Theme::instance().textSize() == base + 1.0);
        CHECK(Theme::instance().titleFont().pointSizeF() > titleBefore);          // titles follow
        CHECK(Theme::instance().monoFont().pointSizeF() > 10.5);                    // so does code
        CHECK(view->textRect(0).top() > headerTop);                                 // taller header, no overlap
        trigger(w, "Ctrl+0");
        CHECK(Theme::instance().textSize() == Theme::kDefaultTextSize);
        for (int i = 0; i < 40; ++i) trigger(w, "Ctrl+-");
        CHECK(Theme::instance().textSize() == Theme::kMinTextSize);                 // clamped
        trigger(w, "Ctrl+0");

        // Settings is a page in place of the notes: sections, ↑/↓ rows (headings skipped), ←/→ values.
        trigger(w, "Ctrl+,");
        settle();
        SettingsPage* settings = w.findChild<SettingsPage*>();
        CHECK(settings && settings->isVisible() && settings->hasFocus());
        CHECK(!view->isVisible());
        shot(w, "20_settings");
        QWidget* panel = settings;
        for (int i = 0; i < 3; ++i) press(panel, Qt::Key_Down);    // Colours, Transparent, Blur, Text size
        for (int i = 0; i < 9; ++i) press(panel, Qt::Key_Right);   // text size: +4.5 pt
        CHECK(Theme::instance().textSize() == Theme::kDefaultTextSize + 4.5);
        for (int i = 0; i < 7; ++i) press(panel, Qt::Key_Down);    // Fonts, (heading) Immersion, Title cards, Sidebar unit, image, Eva art, A.T. Field
        press(panel, Qt::Key_Right);                               // A.T. Field: Constant
        CHECK(w.nervPanel()->mode() == NervPanel::Mode::Constant);
        shot(w, "20b_settings_large");
        press(panel, Qt::Key_Left);                                // back to Pulse
        CHECK(w.nervPanel()->mode() == NervPanel::Mode::Pulse);
        for (int i = 0; i < 4; ++i) press(panel, Qt::Key_Up);
        press(panel, Qt::Key_Right);                               // Title cards: pages and journals
        CHECK(QSettings().value("titleCards").toString() == "journals");
        press(panel, Qt::Key_Escape);
        settle();
        CHECK(!settings->isVisible() && view->isVisible());
        trigger(w, "Ctrl+J");                                      // a journal: its card plays now
        settle();
        CHECK(w.findChild<TitleFlash*>() != nullptr);
        QSettings().setValue("titleCards", "new");
        shot(w, "21_large_text");
        trigger(w, "Ctrl+0");
    }

    // 27. Undo across blocks, block selection, copy / cut / paste of several blocks.
    {
        const QString up = g + "/pages/Undo.md";
        writeFile(up, "- alpha\n- beta\n\t- beta child\n- gamma");
        CHECK(w.openPath(up));
        d = view->document();
        // Split, indent, type: then undo it all back, one step at a time.
        view->focusBlock(0);
        ed = editorOf(w);
        press(ed, Qt::Key_End);
        press(ed, Qt::Key_Return);            // new block "": split
        typeText(ed, "inserted");
        press(ed, Qt::Key_Tab);               // indent under alpha
        CHECK(d->size() == 5 && d->at(1).depth == 1 && QString::fromStdString(d->at(1).text) == "inserted");
        press(editorOf(w), Qt::Key_Z, Qt::ControlModifier);  // text field undo: typing first
        press(editorOf(w), Qt::Key_Z, Qt::ControlModifier);
        press(editorOf(w), Qt::Key_Z, Qt::ControlModifier);
        press(editorOf(w), Qt::Key_Z, Qt::ControlModifier);
        CHECK(d->size() == 4);
        CHECK_EQ(QString::fromStdString(d->serialize()), "- alpha\n- beta\n\t- beta child\n- gamma");
        press(editorOf(w), Qt::Key_Z, Qt::ControlModifier | Qt::ShiftModifier);  // redo the split
        CHECK(d->size() == 5);
        press(editorOf(w), Qt::Key_Z, Qt::ControlModifier);
        trigger(w, "Ctrl+S");
        CHECK_EQ(readFile(up), "- alpha\n- beta\n\t- beta child\n- gamma");  // untouched on disk

        // Esc selects the block; Shift+Down extends; Ctrl+C copies Logseq markdown.
        view->focusBlock(1);
        press(editorOf(w), Qt::Key_Escape);
        CHECK(view->currentBlock() == -1 && view->selectedBlocks() == std::vector<int>{1});
        press(view, Qt::Key_Down, Qt::ShiftModifier);
        press(view, Qt::Key_Down, Qt::ShiftModifier);
        CHECK((view->selectedBlocks() == std::vector<int>{1, 2, 3}));
        shot(w, "22_selection");
        press(view, Qt::Key_C, Qt::ControlModifier);
        CHECK_EQ(QGuiApplication::clipboard()->text(), "- beta\n\t- beta child\n- gamma");
        // Cut, then paste them on another page: same nesting.
        press(view, Qt::Key_X, Qt::ControlModifier);
        CHECK(d->size() == 1 && QString::fromStdString(d->at(0).text) == "alpha");
        trigger(w, "Ctrl+S");
        CHECK_EQ(readFile(up), "- alpha");
        press(view, Qt::Key_Z, Qt::ControlModifier);  // undo the cut, even after a save
        CHECK(d->size() == 4);
        trigger(w, "Ctrl+S");
        CHECK_EQ(readFile(up), "- alpha\n- beta\n\t- beta child\n- gamma");

        const QString other = g + "/pages/Paste Target.md";
        writeFile(other, "- first\n- ");
        CHECK(w.openPath(other));
        view->focusBlock(1);  // the empty block: replaced by the paste
        press(editorOf(w), Qt::Key_V, Qt::ControlModifier);
        trigger(w, "Ctrl+S");
        CHECK_EQ(readFile(other), "- first\n- beta\n\t- beta child\n- gamma");

        // Delete and indent several at once; Ctrl+A in the view selects all.
        view->focusBlock(1);
        press(editorOf(w), Qt::Key_Escape);
        press(view, Qt::Key_Down, Qt::ShiftModifier);
        press(view, Qt::Key_Down, Qt::ShiftModifier);  // beta, beta child, gamma
        press(view, Qt::Key_Tab);                       // beta and gamma go under "first"
        d = view->document();
        CHECK(d->at(1).depth == 1 && d->at(2).depth == 2 && d->at(3).depth == 1);
        press(view, Qt::Key_Backspace);
        CHECK(d->size() == 1);
        press(view, Qt::Key_Z, Qt::ControlModifier);
        CHECK(d->size() == 4);
        // Undo put the cursor back in a block: Ctrl+A selects its text, a second one every block.
        press(QApplication::focusWidget(), Qt::Key_A, Qt::ControlModifier);
        press(QApplication::focusWidget(), Qt::Key_A, Qt::ControlModifier);
        CHECK(view->selectedBlocks().size() == 4);
        press(view, Qt::Key_Escape);
        CHECK(view->selectedBlocks().empty());

        // Dragging from one block to another selects both.
        const QRect r0 = view->textRect(0), r2 = view->textRect(2);
        QMouseEvent down(QEvent::MouseButtonPress, r0.center(), r0.center(), r0.center(), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(vp, &down);
        QMouseEvent move(QEvent::MouseMove, r2.center(), r2.center(), r2.center(), Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(vp, &move);
        QMouseEvent upEv(QEvent::MouseButtonRelease, r2.center(), r2.center(), r2.center(), Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
        QApplication::sendEvent(vp, &upEv);
        CHECK((view->selectedBlocks() == std::vector<int>{0, 1, 2}));
        press(view, Qt::Key_Escape);
    }

    // 28. Find in page, and focus mode.
    {
        CHECK(w.openPath(g + "/pages/Showcase.md"));
        trigger(w, "Ctrl+F");
        settle();
        QWidget* findInput = QApplication::focusWidget();
        CHECK(qobject_cast<QLineEdit*>(findInput) != nullptr);
        typeText(findInput, "block");
        CHECK(view->findCount() >= 3);  // "nested child block", "multi-line block", "same block"
        CHECK(view->findCurrent() == 0);
        press(findInput, Qt::Key_Return);
        CHECK(view->findCurrent() == 1);
        press(findInput, Qt::Key_Return, Qt::ShiftModifier);
        CHECK(view->findCurrent() == 0);
        shot(w, "23_find");
        press(findInput, Qt::Key_A, Qt::ControlModifier);
        typeText(findInput, "H2SO4");  // only inside a typeset formula: not searchable text
        CHECK(view->findCount() == 0);
        press(findInput, Qt::Key_A, Qt::ControlModifier);
        typeText(findInput, "some page");  // a link's visible label, any case
        CHECK(view->findCount() == 1);
        press(findInput, Qt::Key_Escape);
        CHECK(view->findCount() == 0);

        QWidget* side = w.findChild<Explorer*>("nav")->parentWidget();
        CHECK(side->isVisible());
        trigger(w, "Ctrl+\\");
        CHECK(!side->isVisible());
        shot(w, "24_focus");
        trigger(w, "Ctrl+\\");
        CHECK(side->isVisible());
    }

    // 29. MAGI: the page changed on disk while being edited. Overwrite, then reload.
    {
        const QString mp = g + "/pages/Magi.md";
        writeFile(mp, "- original");
        CHECK(w.openPath(mp));
        view->focusBlock(0);
        press(editorOf(w), Qt::Key_End);
        typeText(editorOf(w), " mine");
        writeFile(mp, "- changed elsewhere");  // another program writes the file
        {
            QFile f(mp);
            CHECK(f.open(QIODevice::ReadWrite));
            f.setFileTime(QDateTime::currentDateTime().addSecs(5), QFileDevice::FileModificationTime);
        }
        bool sawPanel = false;
        QTimer::singleShot(150, [&] {
            if (auto* magi = w.findChild<MagiPanel*>()) {
                sawPanel = true;
                shot(w, "28_magi");
                magi->choose(MagiPanel::Overwrite);
                magi->decide();
            }
        });
        trigger(w, "Ctrl+S");  // blocks in the MAGI panel until the timer answers it
        CHECK(sawPanel);
        CHECK_EQ(readFile(mp), "- original mine");

        view->focusBlock(0);
        press(editorOf(w), Qt::Key_End);
        typeText(editorOf(w), " again");
        writeFile(mp, "- theirs");
        {
            QFile f(mp);
            CHECK(f.open(QIODevice::ReadWrite));
            f.setFileTime(QDateTime::currentDateTime().addSecs(10), QFileDevice::FileModificationTime);
        }
        QTimer::singleShot(150, [&] {
            if (auto* magi = w.findChild<MagiPanel*>()) {
                magi->choose(MagiPanel::Reload);
                magi->decide();
            }
        });
        trigger(w, "Ctrl+S");
        CHECK_EQ(readFile(mp), "- theirs");
        CHECK_EQ(QString::fromStdString(view->document()->at(0).text), "theirs");
    }

    // 30. Pattern Blue: tasks due today or overdue -> a strip on today's journal, a count on Tasks.
    {
        const QString today = QDate::currentDate().toString("yyyy-MM-dd ddd");
        const QString past = QDate::currentDate().addDays(-3).toString("yyyy-MM-dd ddd");
        writeFile(g + "/pages/Exam prep.md", "- TODO revise kinetics\n  DEADLINE: <" + past + ">\n- TODO submit lab\n  SCHEDULED: <" + today + ">");
        trigger(w, "Ctrl+J");  // today's journal
        settle();
        QWidget* tasksRow = nullptr;
        for (QAbstractButton* b : w.findChildren<QAbstractButton*>())
            if (b->text() == "Tasks") tasksRow = b;
        CHECK(tasksRow && tasksRow->property("badge").toInt() >= 2);
        const QRect strip = view->alertRect();
        CHECK(!strip.isNull());
        shot(w, "29_pattern_blue");
        click(vp, QPoint(strip.center().x(), strip.top() + 6));  // the heading; its rows open their task
        settle();
        CHECK(w.findChild<TasksPanel*>() != nullptr);  // the strip opens the tasks
        press(QApplication::focusWidget(), Qt::Key_Escape);
        settle();
        CHECK(w.openPath(g + "/pages/Showcase.md"));
        CHECK(view->alertRect().isNull());  // only on today's journal
    }

    // 31. Entry plug boot, the activity-limit alarm, a unit palette.
    {
        NervPanel* nerv = w.nervPanel();
        w.activateWindow();
        settle();
        nerv->boot();
        CHECK(nerv->booting());
        QTest::qWait(1000);
        shot(w, "30_boot");
        QElapsedTimer t;
        t.start();
        while ((nerv->booting() || nerv->animating()) && t.elapsed() < 6000) QTest::qWait(50);
        CHECK(!nerv->booting() && !nerv->animating());  // boots, pulses, then goes still

        QTemporaryDir low;
        auto put = [&](const QString& rel, const QString& v) { writeFile(low.path() + "/" + rel, v + "\n"); };
        put("ACAD/type", "Mains"); put("ACAD/online", "0");
        put("BAT1/type", "Battery"); put("BAT1/present", "1"); put("BAT1/capacity", "10"); put("BAT1/status", "Discharging");
        put("BAT1/energy_now", "4000000"); put("BAT1/power_now", "6000000");  // 4 Wh at 6 W: 40 min
        qputenv("GRAPHITE_POWER_ROOT", low.path().toUtf8());
        nerv->refreshPower();
        CHECK(nerv->alarm() && nerv->power().minutesLeft == 40);
        const PaletteColors before = Theme::instance().palette();
        Theme::instance().setPalette(unitPalette("unit02"));
        settle();
        shot(w, "31_alarm_unit02");
        Theme::instance().setPalette(before);
        put("BAT1/capacity", "60");
        nerv->refreshPower();
        CHECK(!nerv->alarm());
        qunsetenv("GRAPHITE_POWER_ROOT");
        nerv->refreshPower();
        CHECK(unitPalette("unit00").structure != unitPalette("unit02").structure);
        CHECK(unitName("unit02") == "Unit-02" && unitPalette("unit02").source == "Unit-02");
    }

    // 32. Deleting a page: Esc keeps it; Enter moves it out of the graph (recoverable).
    {
        const QString doomed = g + "/pages/Doomed.md";
        writeFile(doomed, "- soon gone");
        CHECK(w.openPath(doomed));
        QTimer::singleShot(150, [&] {
            if (auto* ask = w.findChild<ConfirmPanel*>()) ask->answer(false);
        });
        CHECK(!w.deletePage());
        CHECK(QFileInfo::exists(doomed));
        QTimer::singleShot(150, [&] {
            if (auto* ask = w.findChild<ConfirmPanel*>()) {
                shot(w, "32_delete");
                ask->answer(true);
            }
        });
        CHECK(w.deletePage());
        CHECK(!QFileInfo::exists(doomed));
        const QStringList recycled = QDir(g + "/.graphite/recycle/pages").entryList({"Doomed.graphite-*.md"});
        CHECK(recycled.size() == 1);
        if (!recycled.isEmpty()) CHECK_EQ(readFile(g + "/.graphite/recycle/pages/" + recycled[0]), "- soon gone");
        CHECK(w.index().pageOf(doomed) < 0);
        bool listed = false;
        for (QTreeWidgetItemIterator it(w.findChild<Explorer*>("nav")); *it; ++it)
            listed = listed || (*it)->text(0) == "Doomed";
        CHECK(!listed);
    }

    // 33. Episode-card layout: a long title wraps in the box on the left, the caption sits right;
    // on the page the caption is in the header, not in the outline, and a click edits it.
    {
        trigger(w, "Ctrl+N");
        settle();
        typeText(QApplication::focusWidget(), "Neon Genesis Evangelion");
        press(QApplication::focusWidget(), Qt::Key_Return);
        typeText(QApplication::focusWidget(), "Take care of yourself.");
        shot(w, "33_prompt_layout");
        press(QApplication::focusWidget(), Qt::Key_Return);
        settle();
        CHECK(w.findChild<TitleFlash*>() != nullptr);
        QTest::qWait(500);
        shot(w, "34_flash_layout");
        QTest::qWait(1600);
        settle();
        CHECK_EQ(view->caption(), "Take care of yourself.");
        CHECK(!view->captionRect().isNull());
        CHECK(view->textRect(0).isNull());   // not in the outline: it is in the header
        shot(w, "35_header_caption");
        click(vp, view->captionRect().center());
        CHECK(view->currentBlock() == 0);    // a click edits it as the block it is
        CHECK_EQ(editorOf(w)->toPlainText(), "description:: Take care of yourself.");
        press(editorOf(w), Qt::Key_Escape);
        press(view, Qt::Key_Escape);
        CHECK(view->textRect(0).isNull());   // and back to the header
    }

    // 34. Pattern Orange (due in the next 3 days) under Pattern Blue; the nearest #exam counts down.
    {
        const QString in2 = QDate::currentDate().addDays(2).toString("yyyy-MM-dd ddd");
        const QString in4 = QDate::currentDate().addDays(4).toString("yyyy-MM-dd ddd");
        const QString in9 = QDate::currentDate().addDays(9).toString("yyyy-MM-dd ddd");
        writeFile(g + "/pages/Coursework.md",
                  "- TODO problem set 4\n  DEADLINE: <" + in2 + ">\n"
                  "- Organic chemistry midterm #exam\n  SCHEDULED: <" + in4 + ">\n"
                  "- DONE old quiz #exam\n  SCHEDULED: <" + QDate::currentDate().addDays(1).toString("yyyy-MM-dd ddd") + ">\n"
                  "- Physical chemistry final #exam\n  SCHEDULED: <" + in9 + ">");
        trigger(w, "Ctrl+J");
        settle();
        CHECK(!view->alertRect(0).isNull() && !view->alertRect(1).isNull());  // blue, then orange
        CHECK(view->alertRect(1).top() > view->alertRect(0).top());
        CHECK(w.nervPanel()->examDays() == 4);  // the nearest open one; the DONE quiz is ignored
        // Next Episode: tomorrow's scheduled task, at the bottom of today's journal.
        const QString tmr = QDate::currentDate().addDays(1).toString("yyyy-MM-dd ddd");
        writeFile(g + "/pages/Tomorrow plan.md", "- TODO buy lab coat\n  SCHEDULED: <" + tmr + ">");
        CHECK(w.openPath(g + "/pages/Showcase.md"));
        trigger(w, "Ctrl+J");
        settle();
        CHECK(!view->upcomingRect(0, 0).isNull());
        CHECK(w.openPath(g + "/pages/Showcase.md"));
        CHECK(view->upcomingRect(0, 0).isNull());  // only on today's journal
        trigger(w, "Ctrl+J");
        settle();
        shot(w, "36_pattern_orange_exam");
        click(vp, QPoint(view->alertRect(1).center().x(), view->alertRect(1).top() + 6));
        settle();
        CHECK(w.findChild<TasksPanel*>() != nullptr);
        press(QApplication::focusWidget(), Qt::Key_Escape);
        settle();
    }

    // 35. Operations: "/timer 2s" + Enter runs it (the command leaves the note); at the end the
    // eyecatch shows, today's journal logs it; "/timer stop" ends one early.
    {
        CHECK(w.openPath(g + "/pages/Showcase.md"));
        view->focusBlock(10, 0);
        ed = editorOf(w);
        ed->selectAll();
        typeText(ed, "Revise kinetics /timer 2s");
        press(ed, Qt::Key_Return);
        CHECK(w.operationRunning());
        CHECK_EQ(ed->toPlainText(), "Revise kinetics");           // the command is gone
        CHECK(w.nervPanel()->operationLeft() > 0);
        shot(w, "37_operation");
        QElapsedTimer t;
        t.start();
        while (w.operationRunning() && t.elapsed() < 5000) QTest::qWait(100);
        settle();
        CHECK(!w.operationRunning());
        CHECK(w.findChild<Eyecatch*>() != nullptr);
        QTest::qWait(300);
        shot(w, "38_eyecatch");
        press(QApplication::focusWidget(), Qt::Key_Space);        // any key: back to work
        settle();
        CHECK(!w.findChild<Eyecatch*>());
        const QString todayFile = g + "/journals/" + QDate::currentDate().toString("yyyy_MM_dd") + ".md";
        trigger(w, "Ctrl+S");
        CHECK(readFile(todayFile).contains("Operation 0:02") && readFile(todayFile).contains("#operation"));

        view->focusBlock(10);
        typeText(editorOf(w), " /timer 10m");
        press(editorOf(w), Qt::Key_Return);
        CHECK(w.operationRunning() && w.nervPanel()->operationLeft() > 590);
        typeText(editorOf(w), " /timer stop");
        press(editorOf(w), Qt::Key_Return);
        CHECK(!w.operationRunning() && w.nervPanel()->operationLeft() < 0);
        typeText(editorOf(w), " /timer soonish");
        press(editorOf(w), Qt::Key_Return);                       // not a duration: nothing starts
        CHECK(!w.operationRunning());
        press(editorOf(w), Qt::Key_Escape);
    }

    // 36. The unit in the sidebar: the palette's own, or the one nearest the desktop's colours.
    {
        CHECK(unitForColours(unitPalette("unit00")) == "unit00");
        CHECK(unitForColours(unitPalette("unit02")) == "unit02");
        CHECK(unitForColours(builtinPalette()) == "unit01");
        PaletteColors desk = builtinPalette();
        desk.source = "matugen colours";
        desk.accent = QColor("#adc6ff");      // the user's own matugen colours: blue-lilac
        desk.structure = QColor("#debcdf");
        std::printf("desktop colours -> %s\n", qPrintable(unitForColours(desk)));
        desk.accent = QColor("#9cff3c"); desk.structure = QColor("#9a6cff");  // violet / green
        CHECK(unitForColours(desk) == "unit01");
        desk.accent = QColor("#7fb2ff"); desk.structure = QColor("#3f7cff");  // blues
        CHECK(unitForColours(desk) == "unit00");
        desk.accent = QColor("#ff8a1e"); desk.structure = QColor("#e8333e");  // reds
        CHECK(unitForColours(desk) == "unit02");
        // No art ships: with the folder empty every unit is just absent, and painting copes.
        for (const char* u : {"unit01", "unit00", "unit02", "custom"}) CHECK(unitArt(u).isEmpty());
        const PaletteColors before = Theme::instance().palette();
        CHECK(w.openPath(g + "/pages/Showcase.md"));
        for (const char* u : {"unit01", "unit00", "unit02"}) {
            Theme::instance().setPalette(unitPalette(u));
            settle();
            shot(w, QString("40_sidebar_") + u);
        }
        Theme::instance().setPalette(before);
    }

    // 37. Trees, books, pages: books are folders; pages move, rename (links follow) and go.
    {
        Explorer* ex = w.findChild<Explorer*>("nav");
        QDir().mkpath(g + "/Organic Chem");
        QDir().mkpath(g + "/Quantum");
        writeFile(g + "/Organic Chem/Carbonyls.md", "- aldol, see [[Enolates]]");
        writeFile(g + "/Organic Chem/Enolates.md", "- # Formation\n- LDA at -78 C");
        writeFile(g + "/Quantum/Particle in a box.md", "- compare #Enolates and [[Organic Chem/Enolates]]");
        QTest::qWait(700);  // made outside the app: the folder watch picks them up
        settle();
        CHECK(w.openPath(g + "/Organic Chem/Carbonyls.md"));
        settle();
        bool sawBook = false;
        // The watch is asynchronous (kqueue on macOS is slower than inotify): give it a few seconds.
        for (int tries = 0; tries < 40 && !sawBook; ++tries) {
            for (QTreeWidgetItemIterator it(ex); *it; ++it) sawBook = sawBook || (*it)->text(0).contains("Organic Chem");
            if (!sawBook) QTest::qWait(150);
        }
        CHECK(sawBook);
        bool sawLogseq = false;  // the tree's own folders are not books
        for (QTreeWidgetItemIterator it(ex); *it; ++it) sawLogseq = sawLogseq || (*it)->text(0).contains("logseq");
        CHECK(!sawLogseq);
        shot(w, "41_explorer");
        // A folder of PDFs is not a book; a page put into it makes it one.
        QDir().mkpath(g + "/Slides");
        writeFile(g + "/Slides/lecture1.pdf", "%PDF-1.4");
        QTest::qWait(700);
        settle();
        auto sees = [ex](const QString& name) {
            for (QTreeWidgetItemIterator it(ex); *it; ++it)
                if ((*it)->data(0, Qt::UserRole + 2).toString() == name) return true;
            return false;
        };
        CHECK(!sees("Slides"));
        writeFile(g + "/Slides/Summary.md", "- slide notes");
        QTest::qWait(700);
        settle();
        CHECK(sees("Slides"));
        QDir(g + "/Slides").removeRecursively();
        QTest::qWait(700);
        settle();

        // [[Book/Page]] goes to that book's page.
        view->linkActivated("Organic Chem/Enolates");
        CHECK(view->document() && QFileInfo(g + "/Organic Chem/Enolates.md").exists());

        // Rename: the file, and every link to it in the tree.
        emit ex->renameRequested(g + "/Organic Chem/Enolates.md", "Enolate chemistry");
        settle();
        CHECK(QFileInfo::exists(g + "/Organic Chem/Enolate chemistry.md") && !QFileInfo::exists(g + "/Organic Chem/Enolates.md"));
        CHECK_EQ(readFile(g + "/Organic Chem/Carbonyls.md"), "- aldol, see [[Enolate chemistry]]");
        CHECK_EQ(readFile(g + "/Quantum/Particle in a box.md"),
                 "- compare #[[Enolate chemistry]] and [[Organic Chem/Enolate chemistry]]");

        // Move to another book (as a drag onto it does): the file moves, links still find it.
        emit ex->moveRequested(g + "/Organic Chem/Carbonyls.md", g + "/Quantum");
        settle();
        CHECK(QFileInfo::exists(g + "/Quantum/Carbonyls.md") && !QFileInfo::exists(g + "/Organic Chem/Carbonyls.md"));
        view->linkActivated("Carbonyls");
        CHECK(QFileInfo(g + "/Quantum/Carbonyls.md").exists());

        // Rename a book: qualified links follow.
        emit ex->renameRequested(g + "/Organic Chem", "Organic");
        settle();
        CHECK(QFileInfo(g + "/Organic").isDir() && !QFileInfo(g + "/Organic Chem").exists());
        CHECK(readFile(g + "/Quantum/Particle in a box.md").contains("[[Organic/Enolate chemistry]]"));

        // New page from the prompt, into a chosen book (Alt+→ cycles the books).
        CHECK(w.openPath(g + "/Quantum/Particle in a box.md"));
        trigger(w, "Ctrl+N");
        settle();
        typeText(QApplication::focusWidget(), "Wavefunctions");
        press(QApplication::focusWidget(), Qt::Key_Return);  // to the caption: the book stays Quantum
        press(QApplication::focusWidget(), Qt::Key_Return);
        settle();
        view->focusBlock(view->document()->size() - 1);
        typeText(editorOf(w), "psi");
        trigger(w, "Ctrl+S");
        CHECK(QFileInfo::exists(g + "/Quantum/Wavefunctions.md"));

        // Delete a whole book (confirm), to the recycle place in tests.
        QTimer::singleShot(150, [&] {
            if (auto* ask = w.findChild<ConfirmPanel*>()) ask->answer(true);
        });
        emit ex->deleteRequested(g + "/Organic");
        settle();
        CHECK(!QFileInfo::exists(g + "/Organic"));
        CHECK(!QDir(g + "/.graphite/recycle").entryList({"Organic.graphite-*"}, QDir::Dirs).isEmpty());

        // Start a new tree elsewhere, then come back: the last page of each tree reopens.
        CHECK(w.openPath(g + "/Quantum/Wavefunctions.md"));
        QTemporaryDir other;
        CHECK(w.openTree(other.path()));
        CHECK(QFileInfo::exists(other.path() + "/.graphite/tree.json") && QFileInfo(other.path() + "/journals").isDir());
        CHECK(view->document() != nullptr);  // today's journal of the new tree
        CHECK(w.openTree(g));
        CHECK(QFileInfo(w.treeDir()) == QFileInfo(g));
        CHECK_EQ(QString::fromStdString(view->document()->at(view->document()->size() - 1).text), "psi");  // back where it was
    }

    // 8a. The journal whose first block is only an id: it must read as an empty block.
    CHECK(w.openPath(g + "/journals/2026_09_21.md"));
    d = view->document();
    CHECK(d->size() == 6);
    CHECK(d->at(0).text.empty());
    CHECK(d->at(0).meta.size() == 1);
    CHECK(!view->textRect(0).isNull());
    view->focusBlock(0);
    CHECK_EQ(editorOf(w)->toPlainText(), "");
    view->endEdit();
    shot(w, "4_journal_id_block");

    // 38. Subtasks (a task indented under a task), dates shown under the task, countdowns.
    {
        const QDate td = QDate::currentDate();
        const QString today = QLocale::c().toString(td, "yyyy-MM-dd ddd");
        const QString tmr = QLocale::c().toString(td.addDays(1), "yyyy-MM-dd ddd");
        writeFile(g + "/pages/Subtasks.md",
                  "- TODO Lab report DEADLINE: <" + today + " 23:59> on kinetics\n"
                  "\t- TODO plot the curves\n"
                  "\t- DONE weigh the samples\n"
                  "\t- TODO write the discussion\n"
                  "\t  SCHEDULED: <" + tmr + ">\n"
                  "\t\t- TODO cite sources\n"
                  "- TODO stand alone");
        CHECK(w.openPath(g + "/pages/Subtasks.md"));
        settle();
        const QList<Task> ts = collectTasks(w.index());
        int lab = -1;
        for (int i = 0; i < ts.size(); ++i)
            if (ts[i].text == "Lab report on kinetics") lab = i;
        CHECK(lab >= 0 && lab + 3 < ts.size());
        if (lab >= 0 && lab + 3 < ts.size()) {
            CHECK(ts[lab].level == 0 && ts[lab].subTotal == 4 && ts[lab].subDone == 1);
            CHECK(ts[lab].deadlineAt == QDateTime(td, QTime(23, 59)));
            CHECK(ts[lab + 1].text == "plot the curves" && ts[lab + 1].level == 1 && ts[lab + 1].parent == lab);
            CHECK(ts[lab + 2].text == "write the discussion" && ts[lab + 2].level == 1);
            CHECK(ts[lab + 3].text == "cite sources" && ts[lab + 3].level == 2 && ts[lab + 3].parent == lab + 2);
        }
        // The date moves under the task's text, whatever line it was typed on.
        const QStringList shown = view->renderedText(0).split(QChar(0x2029)).join('\n').split('\n');
        CHECK(shown.size() == 2);
        CHECK(shown.value(0).startsWith("TODO Lab report on kinetics") && shown.value(0).endsWith("1/4"));  // + its subtasks done
        CHECK(shown.value(1).startsWith("DEADLINE") && shown.value(1).contains("23:59") &&
              (shown.value(1).contains(QString::fromUtf8("T\xE2\x88\x92")) || shown.value(1).contains("LIMIT")));
        std::printf("deadline line: %s\n", qPrintable(shown.value(1)));
        CHECK(view->renderedText(3).split(QChar(0x2029)).join('\n').count('\n') == 1);  // own-line date: still one line under
        shot(w, "43_subtasks_page");
        // Compact strips (the default): counts only, subtasks as cores.
        trigger(w, "Ctrl+J");
        settle();
        CHECK(!view->alerts().isEmpty() && view->alerts().front().rows.isEmpty());
        CHECK(view->alerts().size() == 2 && view->alerts()[1].rows.isEmpty() && view->alerts()[1].detail.contains("1 core"));
        for (const OutlinerView::Alert& al : view->alerts()) std::printf("compact strip: %s\n", qPrintable(al.detail));
        shot(w, "46_pattern_compact");
        // Detailed (Settings > Pattern strips): the target with its subtasks under it.
        QSettings().setValue("patternRows", "detailed");
        CHECK(w.openPath(g + "/pages/Subtasks.md"));
        trigger(w, "Ctrl+J");
        settle();
        bool listed = false;
        for (const OutlinerView::Alert& al : view->alerts())
            for (int k = 0; k + 1 < al.rows.size(); ++k)
                if (al.rows[k].text == "Lab report on kinetics" && al.rows[k].level == 0 && al.rows[k + 1].level == 1 &&
                    al.rows[k].at.isValid() && al.rows[k].subTotal == 4)
                    listed = true;
        CHECK(listed);
        shot(w, "44_pattern_subtasks");
        // A row opens its task.
        const QRect strip = view->alertRect(0);
        const int rowY = strip.top() + QFontMetrics(Theme::instance().labelFont()).height() + 16 + 4;
        click(vp, QPoint(strip.left() + 80, rowY));
        settle();
        CHECK(QApplication::focusWidget() == editorOf(w));
        press(editorOf(w), Qt::Key_Escape);
        trigger(w, "Ctrl+Shift+T");
        settle();
        CHECK(w.findChild<TasksPanel*>() != nullptr);
        shot(w, "45_tasks_subtasks");
        press(QApplication::focusWidget(), Qt::Key_Escape);
        settle();
        QSettings().remove("patternRows");
        QFile::remove(g + "/pages/Subtasks.md");
    }

    // 39. Quick maths: a "= ..." line shows its result; the card shows it while typing.
    {
        writeFile(g + "/pages/Stoichiometry.md", "- moles of NaCl\n- = 0.5 mol * 58.44 g/mol\n- = 5 gm");
        CHECK(w.openPath(g + "/pages/Stoichiometry.md"));
        settle();
        CHECK(view->renderedText(1).contains(QString::fromUtf8("\u2192  29.22 g")));
        CHECK(view->renderedText(2).contains("unknown unit 'gm'"));
        view->focusBlock(0);
        ed = editorOf(w);
        ed->selectAll();
        typeText(ed, "= 2.5 g / M(CuSO4.5H2O)");
        settle();
        QWidget* card = nullptr;
        for (QWidget* ch : vp->findChildren<QWidget*>(Qt::FindDirectChildrenOnly))
            if (ch != ed && ch->isVisible() && QString(ch->metaObject()->className()) == "QWidget") card = ch;
        CHECK(card != nullptr);
        shot(w, "47_quick_maths_card");
        press(ed, Qt::Key_Escape);
        settle();
        CHECK(view->renderedText(0).contains("10.01 mmol"));
        shot(w, "48_quick_maths");
        view->focusBlock(2);
        ed = editorOf(w);
        ed->selectAll();
        typeText(ed, "mass /maths");
        press(ed, Qt::Key_Return);
        CHECK_EQ(ed->toPlainText(), "mass \n= ");  // the sum goes on a line of its own
        press(ed, Qt::Key_Escape);
        QFile::remove(g + "/pages/Stoichiometry.md");
    }

    // 40. Balancing: the card says what the reaction needs; /balance writes the coefficients.
    {
        writeFile(g + "/pages/Redox.md", "- $\\ce{Fe + O2 -> Fe2O3}$\n- $\\ce{2H2 + O2 -> 2H2O}$");
        CHECK(w.openPath(g + "/pages/Redox.md"));
        settle();
        view->focusBlock(0);
        ed = editorOf(w);
        QTextCursor c = ed->textCursor();
        c.setPosition(8);  // inside the formula: the card
        ed->setTextCursor(c);
        settle();
        shot(w, "49_balance_card");
        c.movePosition(QTextCursor::End);
        ed->setTextCursor(c);
        typeText(ed, " /balance");
        press(ed, Qt::Key_Return);
        CHECK_EQ(ed->toPlainText(), "$\\ce{4Fe + 3O2 -> 2Fe2O3}$ ");
        press(ed, Qt::Key_Escape);
        view->focusBlock(1);
        ed = editorOf(w);
        c = ed->textCursor();
        c.movePosition(QTextCursor::End);
        ed->setTextCursor(c);
        typeText(ed, " /balance");
        press(ed, Qt::Key_Return);
        CHECK_EQ(ed->toPlainText(), "$\\ce{2H2 + O2 -> 2H2O}$ ");  // already balanced: unchanged
        press(ed, Qt::Key_Escape);
        QFile::remove(g + "/pages/Redox.md");
    }

#ifndef GRAPHITE_PACKAGED  // a packaged build never watches a source folder
    // 41. Updates: the source folder gets a newer version -> "Refit ready" in the sidebar and in
    // Settings, without polling (the file watch notices).
    {
        Updater* up = w.findChild<Updater*>();
        CHECK(up && up->watching() && up->state() == Updater::State::UpToDate);
        {
            QFile f(source.path() + "/CMakeLists.txt");
            CHECK(f.open(QIODevice::WriteOnly));
            f.write("project(graphite VERSION 99.0.0 LANGUAGES CXX)\n");
        }
        QElapsedTimer t;
        t.start();
        while (up->state() != Updater::State::Available && t.elapsed() < 5000) QTest::qWait(100);
        CHECK(up->state() == Updater::State::Available);
        CHECK(up->status().contains("99.0.0") && up->info().changed == QStringList{"CMakeLists.txt"});
        QAbstractButton* ready = nullptr;
        for (QAbstractButton* b : w.findChildren<QAbstractButton*>())
            if (b->text() == "Refit ready" && b->isVisible()) ready = b;
        CHECK(ready);
        shot(w, "50_update_ready");
        if (ready) ready->click();  // Settings, at the Updates section
        settle();
        SettingsPage* sp = w.findChild<SettingsPage*>();
        CHECK(sp && sp->isVisible() && sp->currentRow() > 0);
        CHECK(sp && sp->verticalScrollBar()->value() > 0);  // scrolled down to it
        shot(w, "51_settings_updates");
        trigger(w, "Ctrl+,");
        settle();
        QSettings().setValue("updates", "off");
        up->setWatching(false);
        CHECK(up->state() == Updater::State::Off);
        for (QAbstractButton* b : w.findChildren<QAbstractButton*>())
            CHECK(b->text() != "Refit ready" || !b->isVisible());
    }

#endif

    // 42. Lab tables, plots, numbered formulas, definitions, oxidation states.
    {
        QString arr = "- Arrhenius\n  | T / K | k / s^-1 |\n  |---|---|";
        for (double T : {300.0, 310.0, 320.0, 330.0})
            arr += QString("\n  | %1 | %2 |").arg(T).arg(1e10 * std::exp(-50000.0 / (8.314462618 * T)), 0, 'g', 4);
        arr += "\n  = plot ln(k) vs 1/T";
        writeFile(g + "/pages/Kinetics.md",
                  "- Activation energy:: the least energy colliding particles need to react\n"
                  "- $$k = A e^{-E_a/RT}$$ {#arrhenius}\n"
                  "- $$\\ln k = \\ln A - \\frac{E_a}{RT}$$ {#linear}\n"
                  "- From [[eq:linear]], the activation energy is the slope times -R. [[eq:nothing]]\n"
                  "- Density\n  | m / g | V / mL | \u03C1 = m/V to g/mL |\n  |---|---|---|\n  | 2.50 | 10.0 | |\n  | 7.80 | 10.0 | |\n  | 3.00 | | |\n" +
                      arr.toUtf8() + "\n"
                  "- $\\ce{KMnO4}$ and $\\ce{MnO4- + Fe^2+ + H+ -> Mn^2+ + Fe^3+ + H2O}$");
        CHECK(w.openPath(g + "/pages/Kinetics.md"));
        settle();
        CHECK(view->renderedText(0).contains(QString::fromUtf8("Activation energy  \u2014  the least")));
        CHECK(view->renderedText(1).contains("(1)") && view->renderedText(2).contains("(2)"));
        CHECK(view->renderedText(3).contains("Eq. (2)") && view->renderedText(3).contains("Eq. (nothing?)"));
        const QString density = view->renderedText(4);
        CHECK(density.contains("0.25") && density.contains("0.78") && density.contains(QString::fromUtf8("\u03C1 (g/mL)")));
        const QString plot = view->renderedText(5);
        CHECK(plot.contains(QString::fromUtf8("E\u2090 = \u2212R\u00B7slope = 49.99 kJ/mol")));  // k to 4 figures
        CHECK(view->goToEquation("arrhenius") && !view->goToEquation("missing"));
        shot(w, "52_lab_tables");
        w.resize(1180, 1000);
        settle();
        shot(w, "53_lab_plot");
        // The card on a reaction: masses, balance, what is reduced and oxidised, the halves.
        view->focusBlock(6);
        ed = editorOf(w);
        QTextCursor c = ed->textCursor();
        c.setPosition(static_cast<int>(ed->toPlainText().indexOf("Fe^2+")));
        ed->setTextCursor(c);
        settle();
        shot(w, "54_redox_card");
        c.setPosition(6);
        ed->setTextCursor(c);
        settle();
        shot(w, "55_oxidation_card");
        press(ed, Qt::Key_Escape);
        // "/table" puts a table in, the cursor in its first empty cell.
        view->focusBlock(6);
        ed = editorOf(w);
        ed->selectAll();
        typeText(ed, "/table");
        press(ed, Qt::Key_Return);
        CHECK(ed->toPlainText().startsWith(QString::fromUtf8("| m / g | V / mL | \u03C1 = m/V to g/mL |\n|---|---|---|\n|  |")));
        CHECK(ed->textCursor().position() == static_cast<int>(ed->toPlainText().size()) - 8);
        press(ed, Qt::Key_Escape);
        w.resize(1180, 760);
        QFile::remove(g + "/pages/Kinetics.md");
    }

    // 43. Ramiel: the exam is 4 days off (section 34), so the Angel is on the panel with the
    // exam's name; a click unfolds it; a passed deadline fires its beam (once a day); the day's
    // last task done can bring a fly-by; after the exam, "Target silenced" once.
    // Ctrl+J on today's journal does nothing when it is open: go away and back, which syncs.
    auto reopenToday = [&] {
        CHECK(w.openPath(g + "/pages/Showcase.md"));
        trigger(w, "Ctrl+J");
        settle();
    };
    {
        NervPanel* nerv = w.nervPanel();
        reopenToday();
        CHECK(nerv->examDays() == 4 && nerv->ramiel());
        CHECK(!nerv->examName().contains("#exam") && !nerv->examName().isEmpty());
        CHECK(QSettings().value("ramiel/date").toString() == QDate::currentDate().addDays(4).toString(Qt::ISODate));
        nerv->pulse(600);  // a few frames: Ramiel turns with the field
        QTest::qWait(250);
        shot(w, "56_ramiel");
        nerv->shapeShift();
        CHECK(nerv->effectRunning());
        QTest::qWait(700);
        shot(w, "57_ramiel_unfolded");
        QTest::qWait(1000);
        CHECK(!nerv->effectRunning());
        Theme::instance().setImmersion(false);
        CHECK(!nerv->ramiel());  // plain wording: the one-line exam row
        nerv->shapeShift();
        CHECK(!nerv->effectRunning());
        Theme::instance().setImmersion(true);

        // The beam: a deadline that has gone by, new since the last look.
        QSettings().remove("ramiel/beamDay");
        const QString yesterday = QDate::currentDate().addDays(-1).toString("yyyy-MM-dd ddd");
        writeFile(g + "/pages/Beam.md", "- TODO hand in the beam report\n  DEADLINE: <" + yesterday + ">");
        reopenToday();
        CHECK(nerv->effectRunning());
        CHECK(QSettings().value("ramiel/beamDay").toDate() == QDate::currentDate());
        CHECK(QSettings().value("ramiel/passed").toStringList().join('\n').contains("hand in the beam report"));
        QTest::qWait(500);
        shot(w, "58_ramiel_beam");
        QTest::qWait(1400);
        writeFile(g + "/pages/Beam2.md", "- TODO another one\n  DEADLINE: <" + yesterday + ">");
        reopenToday();
        CHECK(!nerv->effectRunning());  // once a day
        QFile::remove(g + "/pages/Beam.md");
        QFile::remove(g + "/pages/Beam2.md");

        // The fly-by (forced: GRAPHITE_FLYBY=always): every open task due by today done.
        qputenv("GRAPHITE_FLYBY", "always");
        QSettings().remove("ramiel/flyDay");
        const QString today = QDate::currentDate().toString("yyyy-MM-dd ddd");
        QStringList hidden;  // the earlier sections' tasks due by today, set aside for this
        for (const Task& t : collectTasks(w.index()))
            if (t.due().isValid() && t.due() <= QDate::currentDate()) hidden << w.index().pages()[t.page].path;
        hidden.removeDuplicates();
        writeFile(g + "/pages/Fly.md", "- TODO the last one\n  SCHEDULED: <" + today + ">");
        reopenToday();
        for (const QString& p : hidden) QFile::rename(p, p + ".off");
        writeFile(g + "/pages/Fly.md", "- DONE the last one\n  SCHEDULED: <" + today + ">");
        reopenToday();
        CHECK(nerv->effectRunning());
        CHECK(QSettings().value("ramiel/flyDay").toDate() == QDate::currentDate());
        QTest::qWait(900);
        shot(w, "59_ramiel_flyby");
        QTest::qWait(1200);
        for (const QString& p : hidden) QFile::rename(p + ".off", p);
        QFile::remove(g + "/pages/Fly.md");
        qunsetenv("GRAPHITE_FLYBY");

        // Target silenced: Ramiel waited for an exam whose day is past.
        QSettings().setValue("ramiel/exam", "Organic midterm");
        QSettings().setValue("ramiel/date", QDate::currentDate().addDays(-1).toString(Qt::ISODate));
        reopenToday();
        QTest::qWait(900);
        Eyecatch* card = w.findChild<Eyecatch*>();
        CHECK(card != nullptr);
        shot(w, "60_target_silenced");
        if (card) delete card;
        CHECK(QSettings().value("ramiel/date").toString() == QDate::currentDate().addDays(4).toString(Qt::ISODate));  // the next one
        reopenToday();
        QTest::qWait(900);
        CHECK(!w.findChild<Eyecatch*>());  // once
    }

    // 44. Time blocks: "HH:MM …" in today's journal. The time is a label, the margin has a
    // timeline with now marked, and the NERV panel shows the next one (or the one running).
    {
        NervPanel* nerv = w.nervPanel();
        CHECK(w.openPath(g + "/pages/Showcase.md"));
        CHECK(!view->timelineToday());
        QFile tf(todayPath);
        tf.open(QIODevice::ReadOnly);
        const QByteArray before = tf.readAll();
        tf.close();
        const int now = QTime::currentTime().msecsSinceStartOfDay() / 60000;
        if (now > 0 && now < 23 * 60 + 58) {  // not in the first or last minutes of the day
            writeFile(todayPath, QString::fromUtf8(before) + "\n- DONE 00:00 wake up\n- 23:59 TODO **sleep**\n- " + QTime(0, 0).addSecs((now - 1) * 60).toString("HH:mm") + " call home");  // begun a minute ago
            reopenToday();
            CHECK(view->timelineToday());
            CHECK(nerv->nextText() == QString("Next  \u00B7  23:59 sleep"));
            writeFile(todayPath, QString::fromUtf8(before) + "\n- DONE 00:00 wake up\n- DOING 00:00-23:59 the whole day\n- 23:59 TODO **sleep**");
            reopenToday();
            CHECK(nerv->nextText() == QString("Now  \u00B7  \u201323:59 the whole day"));
            shot(w, "61_time_blocks");

            // 45. Ctrl+D: the day's times in order (subtasks under a heading, a timed deadline on
            // another page), now marked; Ctrl+Enter marks one done and it stays, struck through.
            const QString today = QDate::currentDate().toString("yyyy-MM-dd ddd");
            writeFile(todayPath, QString::fromUtf8(before) + "\n- ## TODO Supramolecular\n\t- 23:59 TODO Make notes\n\t- 00:00 DONE wake up"
                                                             "\n- 00:00-23:59 DOING the whole day");
            writeFile(g + "/pages/Hand in.md", "- TODO hand in the report\n  DEADLINE: <" + today + " 23:58>");
            reopenToday();
            bool listed = false;
            for (const Task& t : collectTasks(w.index()))  // and it is a task, due today
                listed = listed || (t.text == "23:59 Make notes" && t.due() == QDate::currentDate() && t.level == 1);
            CHECK(listed);
            trigger(w, "Ctrl+D");
            settle();
            TimelinePanel* tlp = w.findChild<TimelinePanel*>();
            CHECK(tlp != nullptr);
            if (tlp) {
                QListWidget* list = tlp->findChild<QListWidget*>();
                QStringList order;
                int nowRows = 0;
                for (int i = 0; i < list->count(); ++i) {
                    const int kind = list->item(i)->data(Qt::UserRole).toInt();
                    if (kind == 0) order << list->item(i)->text() + "/" + list->item(i)->data(Qt::UserRole + 5).toString();
                    nowRows += kind == 1;
                }
                CHECK_EQ(order.join(" | "), "wake up/Supramolecular | the whole day/ | hand in the report/Hand in | Make notes/Supramolecular");
                CHECK(nowRows == 1);
                CHECK(list->currentItem() && list->currentItem()->text() == "the whole day");  // starts on what is running
                shot(w, "62_timeline_panel");
                press(list, Qt::Key_Return, Qt::ControlModifier);
                settle();
                CHECK(list->currentItem() && list->currentItem()->text() == "the whole day" &&
                      list->currentItem()->data(Qt::UserRole + 3).toBool());
                press(list, Qt::Key_Escape);
                settle();
                CHECK(!w.findChild<TimelinePanel*>());
                trigger(w, "Ctrl+S");  // the done mark is saved before the file is put back
                settle();
                QFile jf(todayPath);
                CHECK(jf.open(QIODevice::ReadOnly) && QString::fromUtf8(jf.readAll()).contains("00:00-23:59 DONE the whole day"));
            }
            QFile::remove(g + "/pages/Hand in.md");
            writeFile(todayPath, QString::fromUtf8(before));
            reopenToday();
            CHECK(nerv->nextText().isEmpty());

            // 46. The week's timetable: today's entries join the timeline (once, when the journal
            // has the same block) and the NEXT row; Ctrl+Enter leaves them; Ctrl+Shift+D opens the
            // page, or makes one with a block per day.
            writeFile(g + "/pages/Week.md", "tags:: timetable\n\n- Daily\n\t- 23:57 Late lecture\n\t- 00:00-23:59 Lecture block\n"
                                            "- Mon-Sun 00:01 Every day too");
            writeFile(todayPath, QString::fromUtf8(before) + "\n- 23:57 Late lecture");
            reopenToday();
            CHECK(nerv->nextText().endsWith("Lecture block"));  // running all day
            trigger(w, "Ctrl+D");
            settle();
            if (TimelinePanel* tt = w.findChild<TimelinePanel*>(); tt) {
                QListWidget* list = tt->findChild<QListWidget*>();
                QStringList order;
                for (int i = 0; i < list->count(); ++i)
                    if (list->item(i)->data(Qt::UserRole).toInt() == 0)
                        order << list->item(i)->text() + "/" + list->item(i)->data(Qt::UserRole + 5).toString();
                CHECK_EQ(order.join(" | "), "Lecture block/Timetable | Every day too/Timetable | Late lecture/");
                CHECK(list->currentItem() && list->currentItem()->text() == "Lecture block");
                shot(w, "63_timeline_timetable");
                press(list, Qt::Key_Return, Qt::ControlModifier);  // a weekly entry is not ticked off
                settle();
                CHECK(list->currentItem() && !list->currentItem()->data(Qt::UserRole + 3).toBool());
                press(list, Qt::Key_Escape);
                settle();
            } else {
                CHECK(false);
            }
            trigger(w, "Ctrl+Shift+D");
            settle();
            CHECK(w.currentPage() == g + "/pages/Week.md");
            QFile::remove(g + "/pages/Week.md");
            writeFile(todayPath, QString::fromUtf8(before));
            reopenToday();
            trigger(w, "Ctrl+Shift+D");  // none yet: a new page
            settle();
            const QString made = w.currentPage();
            CHECK(made.endsWith("/Timetable.md"));
            shot(w, "64_timetable_new");
            trigger(w, "Ctrl+S");
            settle();
            QFile mf(made);
            CHECK(mf.open(QIODevice::ReadOnly) && QString::fromUtf8(mf.readAll()).startsWith("tags:: timetable\n- Monday\n"));
            mf.close();
            QFile::remove(made);
            reopenToday();

            // 47. An exam under a heading is named with it; the heading's date counts.
            const QString in2 = QDate::currentDate().addDays(2).toString("yyyy-MM-dd ddd");
            writeFile(g + "/pages/Course.md", "- ## Supramolecular Chemistry\n  SCHEDULED: <" + in2 + ">\n\t- Quiz #exam");
            reopenToday();
            CHECK(nerv->examDays() == 2);
            CHECK_EQ(nerv->examName(), QString::fromUtf8("Quiz · Supramolecular Chemistry"));
            if (nerv->ramiel()) shot(w, "65_ramiel_context");
            QFile::remove(g + "/pages/Course.md");
            reopenToday();
            CHECK(nerv->examDays() == 4);

            // 48. Graphite Essentials: the tree got the book at start (the lab tables guide; the
            // timetable section 46 deleted). Every example in the guide works out; a deleted
            // page stays deleted until Settings restores it.
            const QString ess = g + "/Graphite Essentials";
            CHECK(QFileInfo::exists(ess + "/Lab tables.md"));
            CHECK(QSettings().value("essentials/" + QString::number(qHash(g))).toStringList() == QStringList({"Lab tables", "Graphs", "Timetable"}));
            CHECK(w.supplyEssentials() == 0);  // nothing new; the deleted timetable stays away
            CHECK(w.openPath(ess + "/Lab tables.md"));
            settle();
            QString all;
            for (int b = 0; b < view->document()->size(); ++b) all += view->renderedText(b) + "\n";
            CHECK(all.contains(QString::fromUtf8("ρ (g/mL)")) && all.contains("0.25") && all.contains("0.78") && all.contains("0.504"));
            CHECK(all.contains("n (mmol)") && all.contains("10.01") && all.contains("c (M)") && all.contains("0.1001"));
            CHECK(all.contains(QString::fromUtf8("R²")) && all.contains(QString::fromUtf8("Eₐ = −R·slope")));
            CHECK(!all.contains("unknown") && !all.contains("rror") && !all.contains("not work"));
            if (all.contains("rror") || all.contains("unknown")) std::printf("%s\n", qPrintable(all));
            shot(w, "66_essentials_lab_tables");
            view->verticalScrollBar()->setValue(view->verticalScrollBar()->maximum() / 2);
            shot(w, "67_essentials_lab_plots");
            QFile::remove(ess + "/Lab tables.md");
            CHECK(w.supplyEssentials() == 0 && !QFileInfo::exists(ess + "/Lab tables.md"));
            CHECK(w.supplyEssentials(true) == 2);  // Settings > Graphite Essentials: both back
            CHECK(w.openPath(ess + "/Graphs.md"));  // every example in the graphs guide draws
            settle();
            for (int b = 0; b < view->document()->size(); ++b)
                if (view->renderedText(b).contains("graph{")) std::printf("graph guide block %d: %s\n", b, qPrintable(view->renderedText(b)));
            {
                QString guide;
                for (int b = 0; b < view->document()->size(); ++b) guide += view->renderedText(b) + "\n";
                CHECK(!guide.contains("graph{x") && !guide.contains("graph{k") && !guide.contains("graph{t") && !guide.contains("graph{r") &&
                      !guide.contains("graph{s"));
                CHECK(guide.contains("Pattern Blue") && guide.contains("Contact") && guide.contains("Target 01"));
            }
            shot(w, "73_essentials_graphs");
            CHECK(QFileInfo::exists(ess + "/Lab tables.md") && QFileInfo::exists(ess + "/Timetable.md"));
            QFile::remove(ess + "/Timetable.md");
            reopenToday();

            // 50. =plot{y vs x}: no quick-maths card while typing it ("unknown unit" before). A
            // supplied page still exactly as 0.20 wrote it ("= plot") is brought up to date; one
            // you have edited is left alone.
            {
                writeFile(g + "/pages/Plotting.md",
                          "- | c / mM | A |\n  |---|---|\n  | 0.2 | 0.23 |\n  | 0.4 | 0.46 |\n  | 0.6 | 0.69 |\n  =plot{A vs");
                CHECK(w.openPath(g + "/pages/Plotting.md"));
                view->focusBlock(0);
                QTextEdit* pe = editorOf(w);
                QTextCursor pc = pe->textCursor();
                pc.movePosition(QTextCursor::End);
                pe->setTextCursor(pc);
                typeText(pe, " c}");
                CHECK(pe->toPlainText().endsWith("\n=plot{A vs c}"));
                settle();
                bool card = false;
                for (QWidget* ch : vp->findChildren<QWidget*>(Qt::FindDirectChildrenOnly))
                    card = card || (ch != pe && ch->isVisible() && QString(ch->metaObject()->className()) == "QWidget");
                CHECK(!card);
                press(pe, Qt::Key_Escape);
                settle();
                CHECK(view->renderedText(0).contains(QString::fromUtf8("R²")));
                trigger(w, "Ctrl+S");
                settle();
                QFile::remove(g + "/pages/Plotting.md");

                QFile res(":/essentials/Lab tables.md");
                CHECK(res.open(QIODevice::ReadOnly));
                const QString now = QString::fromUtf8(res.readAll());
                QString old20 = now;  // the text 0.20 supplied
                const QList<QPair<const char*, const char*>> back = {
                    {"and `=plot{y vs x}` draws a graph", "and `= plot` draws a graph"},
                    {"Put `=plot{y vs x}` on the line under a table, in the same block (no space after the `=`).",
                     "Put `= plot y vs x` on the line under a table, in the same block."},
                    {"\t  =plot{A vs c}\n", "\t  = plot A vs c\n"},
                    {"\t  =plot{ln(k) vs 1/T}\n", "\t  = plot ln(k) vs 1/T\n"},
                    {"Just `=plot{}` takes", "Just `= plot` takes"},
                    {"then add `=plot{pH vs V}` under", "then add `= plot pH vs V` under"},
                    {"`=plot{y vs x}` under the table: a graph with a fit. `=plot{}`: the first",
                     "`= plot y vs x` under the table: a graph with a fit. `= plot`: the first"},
                    {"Anything a quick maths line (`= 2.5 g / M(NaCl)`) can do", "Anything a `= ` line can do"}};
                for (const auto& [a, b] : back) old20.replace(QString::fromUtf8(a), QString::fromUtf8(b));
                CHECK_EQ(QString(QCryptographicHash::hash(old20.toUtf8(), QCryptographicHash::Md5).toHex()),
                         "641a98801c672081cb30cb5298d6aabc");
                const QString lab = ess + "/Lab tables.md";
                writeFile(lab, old20.toUtf8());
                CHECK(w.supplyEssentials() == 1);
                QFile up(lab);
                CHECK(up.open(QIODevice::ReadOnly) && QString::fromUtf8(up.readAll()) == now);
                up.close();
                writeFile(lab, (old20 + "- my own notes\n").toUtf8());  // edited: stays
                CHECK(w.supplyEssentials() == 0);
                QFile kept(lab);
                CHECK(kept.open(QIODevice::ReadOnly) && QString::fromUtf8(kept.readAll()).endsWith("- my own notes\n"));
                reopenToday();
            }

            // 51. =graph{...}: curves with their marks under them, units on the axes, a lab
            // table's points under the curve; a bad one says why.
            {
                writeFile(g + "/pages/Graphs.md",
                          "- =graph{x^2 - 2, x from -3 to 3}\n"
                          "- =graph{tan(x), x from 0 to 2*pi, y from -6 to 6}\n"
                          "- =graph{sin(x); cos(x), x from 0 to 2*pi}\n"
                          "- | T / K | k / s^-1 |\n  |---|---|\n  | 300 | 19.7 |\n  | 320 | 68.94 |\n  | 340 | 205 |\n"
                          "  =graph{k = A*exp(-Ea/(R*T)), T from 280 K to 360 K, A = 1e10 s^-1, Ea = 50 kJ/mol}\n"
                          "- =graph{x + 1 m, x from 0 to 1}");
                CHECK(w.openPath(g + "/pages/Graphs.md"));
                settle();
                CHECK(view->renderedText(0).contains("Target 01  x = ") && view->renderedText(0).contains("Nadir  (0, "));
                CHECK(view->renderedText(1).contains("Pattern Blue  x = 1.571") && view->renderedText(1).contains("Pattern Blue  x = 4.712"));
                CHECK(view->renderedText(2).count("Contact") == 2);
                CHECK(!view->renderedText(3).contains("graph{"));  // drawn, under the table
                CHECK(view->renderedText(4).contains(QString::fromUtf8("graph{x + 1 m, x from 0 to 1}   ·  ")));
                shot(w, "70_graphs_eva");
                view->verticalScrollBar()->setValue(view->verticalScrollBar()->maximum());
                shot(w, "71_graphs_units");
                Theme::instance().setImmersion(false);
                settle();
                CHECK(view->renderedText(0).contains("Roots  x = "));
                view->verticalScrollBar()->setValue(0);
                shot(w, "72_graphs_plain");
                Theme::instance().setImmersion(true);
                settle();
                QFile::remove(g + "/pages/Graphs.md");
                reopenToday();
            }

            // 49. The title card in a small window: centred, and it follows a resize.
            {
                const QSize was = w.size();
                w.resize(640, 480);
                settle();
                auto* flash = new TitleFlash(w.centralWidget(), 21, "Supramolecular Chemistry", "Hosts, guests and the bonds between them");
                QTest::qWait(250);
                shot(w, "68_title_card_small");
                w.resize(900, 600);
                settle();
                CHECK(flash->geometry() == w.centralWidget()->rect());
                shot(w, "69_title_card_resized");
                w.resize(was);
                settle();
            }
        }
    }

    // 52. A small window: Up / Down past the edge of the view scroll the page with the cursor
    // (between blocks and inside a long wrapped one), both ways.
    {
        const QSize was = w.size();
        w.resize(640, 360);
        QString many;
        for (int i = 0; i < 60; ++i)
            many += QString("- line %1%2\n").arg(i).arg(i % 9 == 4 ? QString(" with a long tail").repeated(12) : QString());
        writeFile(g + "/pages/Scrolling.md", many);
        CHECK(w.openPath(g + "/pages/Scrolling.md"));
        settle();
        view->focusBlock(0, 0);
        settle();
        QTextEdit* se = editorOf(w);
        auto inView = [&] {
            const QRect cr = se->cursorRect();
            const QPoint top = se->viewport()->mapTo(vp, cr.topLeft()), bottom = se->viewport()->mapTo(vp, cr.bottomLeft());
            return se->isVisible() && top.y() >= 0 && bottom.y() <= vp->height();
        };
        int outside = 0, presses = 0;
        for (; presses < 400 && view->activeBlock() < 59; ++presses) {
            press(se, Qt::Key_Down);
            QApplication::processEvents();
            se = editorOf(w);
            if (!inView()) ++outside;
        }
        CHECK(view->activeBlock() == 59);
        CHECK(outside == 0);
        if (outside) std::printf("scroll down: cursor out of view after %d of %d presses\n", outside, presses);
        outside = 0;
        for (presses = 0; presses < 400 && (view->activeBlock() > 0 || se->textCursor().position() > 0); ++presses) {
            press(se, Qt::Key_Up);
            QApplication::processEvents();
            se = editorOf(w);
            if (!inView()) ++outside;
        }
        CHECK(view->activeBlock() == 0);
        CHECK(outside == 0);
        if (outside) std::printf("scroll up: cursor out of view after %d of %d presses\n", outside, presses);
        press(se, Qt::Key_Escape);  // block selection: arrows move the highlight, the view follows
        settle();
        CHECK(view->activeBlock() == -1);
        auto selInView = [&] {
            const QRect r = view->selectionRect();
            return r.isValid() && r.top() >= 0 && r.bottom() <= vp->height();
        };
        outside = 0;
        for (int i = 0; i < 59; ++i) {
            press(view, Qt::Key_Down);
            if (!selInView()) ++outside;
        }
        CHECK(outside == 0);
        if (outside) std::printf("selection down: out of view %d times\n", outside);
        outside = 0;
        for (int i = 0; i < 59; ++i) {
            press(view, Qt::Key_Up);
            if (!selInView()) ++outside;
        }
        CHECK(outside == 0);
        if (outside) std::printf("selection up: out of view %d times\n", outside);
        press(view, Qt::Key_Escape);
        QFile::remove(g + "/pages/Scrolling.md");
        w.resize(was);
        settle();
    }

    // 53. Headings that follow their subtasks: auto-start, finish + fold + flash, reopen,
    // cascade (and its undo), a page that is not open, the switches off.
    {
        const QString sp = g + "/pages/Supramolecular.md";
        writeFile(sp, "- ## TODO Supramolecular Chemistry\n\t- 17:00 TODO Make notes\n\t- 18:00 TODO Problem set\n- plain after");
        CHECK(w.openPath(sp));
        settle();
        auto blockText = [&](int b) { return QString::fromStdString(w.view()->document()->at(b).text); };
        CHECK(view->renderedText(0).contains("0/2"));
        view->focusBlock(1, 0);
        settle();
        QTextEdit* te = editorOf(w);
        press(te, Qt::Key_Return, Qt::ControlModifier);  // TODO -> DOING
        settle();
        CHECK_EQ(blockText(1), QString("17:00 DOING Make notes"));
        CHECK_EQ(blockText(0), QString("## DOING Supramolecular Chemistry"));  // auto-start
        press(te, Qt::Key_Return, Qt::ControlModifier);  // DOING -> DONE
        settle();
        CHECK(view->renderedText(0).contains("1/2"));
        view->focusBlock(2, 0);
        settle();
        te = editorOf(w);
        press(te, Qt::Key_Return, Qt::ControlModifier);
        press(te, Qt::Key_Return, Qt::ControlModifier);
        settle();
        CHECK_EQ(blockText(0), QString("## DONE Supramolecular Chemistry"));
        CHECK(!w.view()->document()->at(0).collapsed());  // not while you are inside it
        QLabel* state = w.findChild<QLabel*>("state");
        CHECK(state && state->text().contains("Supramolecular Chemistry"));
        press(te, Qt::Key_Escape);
        settle();
        CHECK(w.view()->document()->at(0).collapsed());
        trigger(w, "Ctrl+S");
        settle();
        CHECK_EQ(readFile(sp), QString("- ## DONE Supramolecular Chemistry\n  collapsed:: true\n\t- 17:00 DONE Make notes\n"
                                       "\t- 18:00 DONE Problem set\n- plain after"));
        // A new open subtask: the heading reopens (work has started: DOING) and unfolds.
        press(view, Qt::Key_Escape);
        view->focusBlock(0, 0);
        settle();
        te = editorOf(w);
        press(te, Qt::Key_Down, Qt::ControlModifier);  // unfold to reach the subtasks
        settle();
        view->focusBlock(2, static_cast<int>(blockText(2).size()));
        settle();
        te = editorOf(w);
        press(te, Qt::Key_Return);
        settle();
        typeText(editorOf(w), "TODO Lab write-up");
        settle();
        CHECK_EQ(blockText(3), QString("TODO Lab write-up"));
        CHECK_EQ(blockText(0), QString("## DOING Supramolecular Chemistry"));
        CHECK(view->renderedText(0).contains("2/3"));
        // Marking the heading DONE yourself closes the open subtask; Ctrl+Z brings it back.
        view->focusBlock(0, 0);
        settle();
        te = editorOf(w);
        press(te, Qt::Key_Return, Qt::ControlModifier);  // DOING -> DONE
        settle();
        CHECK_EQ(blockText(0), QString("## DONE Supramolecular Chemistry"));
        CHECK_EQ(blockText(3), QString("DONE Lab write-up"));
        press(te, Qt::Key_Z, Qt::ControlModifier);
        settle();
        CHECK_EQ(blockText(0), QString("## DOING Supramolecular Chemistry"));
        CHECK_EQ(blockText(3), QString("TODO Lab write-up"));
        CHECK(!w.view()->document()->at(0).collapsed());
        press(editorOf(w), Qt::Key_Escape);
        settle();
        trigger(w, "Ctrl+S");
        settle();
        // From the Tasks panel, on a page that is not open: its heading finishes and folds.
        const QString op = g + "/pages/Heading B.md";
        writeFile(op, "- TODO Heading B\n\t- TODO only sub\n- after");
        trigger(w, "Ctrl+Shift+T");
        settle();
        if (w.findChild<TasksPanel*>()) {
            typeText(QApplication::focusWidget(), "only sub");
            press(QApplication::focusWidget(), Qt::Key_Return, Qt::ControlModifier);
            settle();
            press(QApplication::focusWidget(), Qt::Key_Escape);
            settle();
        }
        CHECK_EQ(readFile(op), QString("- DONE Heading B\n  collapsed:: true\n\t- DONE only sub\n- after"));
        // Every switch off: the heading stays as it is.
        QSettings().setValue("tasks/finish", false);
        QSettings().setValue("tasks/start", false);
        w.applyTaskOptions();
        view->focusBlock(3, 0);
        settle();
        te = editorOf(w);
        press(te, Qt::Key_Return, Qt::ControlModifier);
        press(te, Qt::Key_Return, Qt::ControlModifier);
        settle();
        CHECK_EQ(blockText(3), QString("DONE Lab write-up"));
        CHECK_EQ(blockText(0), QString("## DOING Supramolecular Chemistry"));
        shot(w, "53_heading_progress");
        press(te, Qt::Key_Escape);
        settle();
        QSettings().remove("tasks/finish");
        QSettings().remove("tasks/start");
        w.applyTaskOptions();
        trigger(w, "Ctrl+S");
        settle();
        QFile::remove(sp);
        QFile::remove(op);
    }

    // 54. Day scrap: after a yes the journal is wiped under the Third Impact; Ctrl+Z brings it
    // back byte for byte, and a copy of the day is in .graphite/bak. Not on a page.
    {
        const QString jp = g + "/journals/2026_01_05.md";
        const QString day = "- 09:00 TODO Problem set\n\t- part (a)\n- notes  odd   spacing";
        writeFile(jp, day);
        CHECK(w.openPath(jp));
        settle();
        QTimer::singleShot(150, [&] {
            if (auto* ask = w.findChild<ConfirmPanel*>()) ask->answer(true);
        });
        CHECK(w.scrapDay());
        settle();
        if (auto* impact = w.findChild<ThirdImpact*>()) {
            QElapsedTimer t;
            t.start();
            while (t.elapsed() < ThirdImpact::kRise + 300) QApplication::processEvents(QEventLoop::AllEvents, 20);
            shot(w, "54_third_impact");
            impact->skip();
            settle();
        }
        CHECK(w.findChild<ThirdImpact*>() == nullptr || !w.findChild<ThirdImpact*>()->isVisible());
        CHECK(view->document()->size() == 1 && view->document()->at(0).text.empty());
        trigger(w, "Ctrl+S");
        settle();
        CHECK(readFile(jp).trimmed() == "-" || readFile(jp).trimmed().isEmpty());
        CHECK(!QDir(g + "/.graphite/bak/journals").entryList({"2026_01_05.graphite-*.bak.md"}).isEmpty());
        press(view, Qt::Key_Z, Qt::ControlModifier);
        settle();
        trigger(w, "Ctrl+S");
        settle();
        CHECK_EQ(readFile(jp), day);
        // A page is not a day.
        CHECK(w.openPath(g + "/pages/Showcase.md"));
        settle();
        CHECK(!w.scrapDay());
        QFile::remove(jp);
        // F2: the field manual (offscreen: its address in the status line, no browser started),
        // the build folder's own copy, a complete page.
        trigger(w, "F2");
        settle();
        QLabel* state = w.findChild<QLabel*>("state");
        CHECK(state && state->text().endsWith("/field-manual.html"));
        const QString manual = readFile(QCoreApplication::applicationDirPath() + "/field-manual.html");
        CHECK(manual.startsWith("<!doctype html>") && manual.contains("<meta charset=\"utf-8\">"));
        CHECK(manual.contains("Graphite Field Manual"));
    }

    // 55. /progress: a bar on a task, stepped with Alt+= / Alt+-; the task and its heading follow.
    {
        const QString pp = g + "/pages/Reading.md";
        writeFile(pp, "- ## TODO Reading\n\t- TODO Read chapter 12\n\t- DONE Notes");
        CHECK(w.openPath(pp));
        settle();
        auto blockText = [&](int b) { return QString::fromStdString(w.view()->document()->at(b).text); };
        view->focusBlock(1, static_cast<int>(blockText(1).size()));
        settle();
        QTextEdit* pe = editorOf(w);
        typeText(pe, " /progress");
        press(pe, Qt::Key_Return);
        CHECK_EQ(pe->toPlainText(), QString("TODO Read chapter 12 {progress 0/10 by 1}"));
        CHECK_EQ(pe->textCursor().selectedText(), QString("10"));  // the end, to type over
        typeText(pe, "3");
        press(pe, Qt::Key_Equal, Qt::AltModifier);
        settle();
        CHECK_EQ(blockText(1), QString("DOING Read chapter 12 {progress 1/3 by 1}"));
        CHECK_EQ(blockText(0), QString("## DOING Reading"));  // the heading started with it
        QLabel* state = w.findChild<QLabel*>("state");
        CHECK(state && state->text().contains("33%") && state->text().contains("1 / 3"));
        press(pe, Qt::Key_Equal, Qt::AltModifier);
        press(pe, Qt::Key_Equal, Qt::AltModifier);
        press(pe, Qt::Key_Equal, Qt::AltModifier);  // past the end: stays
        settle();
        CHECK_EQ(blockText(1), QString("DONE Read chapter 12 {progress 3/3 by 1}"));
        CHECK_EQ(blockText(0), QString("## DONE Reading"));
        press(pe, Qt::Key_Minus, Qt::AltModifier);
        settle();
        CHECK_EQ(blockText(1), QString("DOING Read chapter 12 {progress 2/3 by 1}"));
        CHECK_EQ(blockText(0), QString("## DOING Reading"));
        press(pe, Qt::Key_Escape);  // the block stays selected: Alt+= on the selection
        settle();
        const QString shown = view->renderedText(1);
        CHECK(shown.contains("2 / 3") && shown.contains("66%") && !shown.contains("{progress"));
        shot(w, "74_progress");
        press(view, Qt::Key_Equal, Qt::AltModifier);
        settle();
        CHECK_EQ(blockText(1), QString("DONE Read chapter 12 {progress 3/3 by 1}"));
        CHECK(state && state->text().contains("100%"));
        trigger(w, "Ctrl+S");
        settle();
        QFile::remove(pp);
    }

    // 56. Sidebar art (0.25.0): none ships. The window paints with no files; a custom.png in the
    // art folder (a big one is cut to 800 px) shows; the download asks first and "no" does nothing.
    {
        CHECK(!QFileInfo::exists(artDir() + "/custom.png"));
        QSettings().setValue("unitArt", "auto");
        w.update();
        shot(w, "56_art_none");
        QImage big(500, 1500, QImage::Format_ARGB32);
        big.fill(Qt::transparent);
        QPainter bp(&big);
        bp.fillRect(QRect(100, 200, 300, 1000), QColor(160, 90, 255));
        bp.end();
        const QString src = g + "/mine.png";
        CHECK(big.save(src, "PNG"));
        CHECK(!w.setCustomArt(g + "/Showcase-missing.png"));
        writeFile(g + "/notpng.png", "plain text");
        CHECK(!w.setCustomArt(g + "/notpng.png"));
        CHECK(w.setCustomArt(src));
        CHECK(QSettings().value("unitArt").toString() == "custom");
        const QImage saved(artDir() + "/custom.png", "PNG");
        CHECK(!saved.isNull() && saved.height() == 800 && saved.width() == 240);  // margins cut, scaled to 800 tall
        settle();
        shot(w, "56_art_custom");
        QSettings().setValue("unitArt", "unit01");  // wanted file missing: nothing, no crash
        w.update();
        settle();
        QSettings().setValue("unitArt", "auto");

        bool asked = false;
        QTimer::singleShot(150, [&] {
            if (auto* ask = w.findChild<ConfirmPanel*>()) {
                asked = true;
                ask->answer(false);
            }
        });
        CHECK(!w.downloadArt());
        CHECK(asked);
        CHECK(!QFileInfo::exists(artDir() + "/unit01.png"));
        CHECK(!QFileInfo::exists(artDir() + "/unit00.png"));
        CHECK(!QFileInfo::exists(artDir() + "/unit02.png"));
        QFile::remove(artDir() + "/custom.png");
        forgetUnitArt();
    }

    // 8. Screenshots of a page that shows every kind of formatting.
    w.resize(1180, 760);
    CHECK(w.openPath(g + "/pages/Showcase.md"));
    shot(w, "1_normal");
    view->focusBlock(3);
    shot(w, "2_editing");
    Theme::instance().setTransparent(true);
    shot(w, "3_transparent");
    Theme::instance().setTransparent(false);

    // 13. Colours taken from wallpapers: three different pictures, same page.
    CHECK(w.openPath(g + "/journals/2026_09_21.md"));
    const PaletteColors original = Theme::instance().palette();
    struct Wall { const char* name; QColor a, b, c; };
    for (const Wall& wl : {Wall{"sunset", QColor(250, 140, 60), QColor(190, 50, 120), QColor(30, 30, 90)},
                           Wall{"forest", QColor(60, 150, 90), QColor(20, 110, 110), QColor(15, 40, 50)},
                           Wall{"ocean", QColor(70, 140, 230), QColor(40, 70, 170), QColor(20, 20, 60)}}) {
        const PaletteColors pc = paletteFromImage(wallpaper(wl.a, wl.b, wl.c));
        Theme::instance().setPalette(pc);
        CHECK(Theme::instance().palette() == pc);
        shot(w, QString("5_palette_%1").arg(wl.name));
        std::printf("palette %-7s accent %s structure %s bg %s\n", wl.name, qPrintable(pc.accent.name()),
                    qPrintable(pc.structure.name()), qPrintable(pc.bg.name()));
    }
    Theme::instance().setPalette(original);
    CHECK(Theme::instance().palette() == original);

    if (failures == 0) std::printf("all ui tests passed\n");
    return failures == 0 ? 0 : 1;
}
