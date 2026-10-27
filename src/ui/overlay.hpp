// In-window panels drawn over the page: the new-page prompt and the keyboard sheet.
//
// They are child widgets, not dialogs, so they follow the theme (and Ctrl+T) and need no
// window manager rules on Hyprland. A dimmed scrim covers the window; Esc or a click on the
// scrim closes the panel and gives focus back to where it was.
#pragma once

#include <QDate>
#include <QDateTime>
#include <QElapsedTimer>
#include <QList>
#include <QPair>
#include <QPointer>
#include <QString>
#include <QTimer>
#include <QWidget>
#include <functional>

class QLabel;
class QPainter;
class QLineEdit;
class QPlainTextEdit;
class QListWidget;
class QVBoxLayout;

namespace graphite {

class Overlay : public QWidget {
    Q_OBJECT
public:
    // Covers `host` and follows its size. Shown and focused right away.
    explicit Overlay(QWidget* host, int cardWidth);
    void dismiss();

signals:
    void dismissed();

protected:
    QWidget* card() const { return card_; }
    QVBoxLayout* body() const { return body_; }
    virtual void restyle() = 0;  // re-read theme colours

    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void keyPressEvent(QKeyEvent*) override;
    bool eventFilter(QObject*, QEvent*) override;
    void finish();  // subclasses call this from their constructor, after building the card
    void place();   // size and centre the card again (after its content changed)
    virtual void paintCard(QPainter& p, const QRect& card);  // the card's background

private:
    QWidget* card_ = nullptr;
    QVBoxLayout* body_ = nullptr;
    QPointer<QWidget> previousFocus_;
    int cardWidth_ = 0;
};

// "New page", as an Evangelion title card: EPISODE:N, the title, and a caption (saved as the
// page's description:: property). Enter or Tab goes from title to caption; Enter on the
// caption creates the page. With immersion off it is a plain card with the same two fields.
class PagePrompt : public Overlay {
    Q_OBJECT
public:
    // `exists` says whether a page of that name is already in the graph.
    // `books`: (name, dir) to choose from, `book` the one it starts in. Alt+←/→ changes it.
    PagePrompt(QWidget* host, std::function<bool(const QString&)> exists, int episode,
               const QList<QPair<QString, QString>>& books = {}, int book = 0);

signals:
    void accepted(const QString& name, const QString& caption, const QString& bookDir);

protected:
    void restyle() override;
    void paintCard(QPainter& p, const QRect& card) override;
    bool eventFilter(QObject*, QEvent*) override;

private:
    void updateHint();
    void accept();
    void fitTitle();         // the title box grows with its lines
    QString titleText() const;
    std::function<bool(const QString&)> exists_;
    QLabel* title_ = nullptr;           // "EPISODE:27" (or "New page")
    QPlainTextEdit* input_ = nullptr;   // the title: a box on the left that wraps, as on the cards
    QLineEdit* caption_ = nullptr;      // set to the right underneath
    QLabel* bookLabel_ = nullptr;       // "BOOK  ‹ Organic Chem ›"
    QLabel* hint_ = nullptr;
    QList<QPair<QString, QString>> books_;
    int book_ = 0;
    void showBook();
    void stepBook(int delta);
};

// One line of input with a title: "New book".
class NamePrompt : public Overlay {
    Q_OBJECT
public:
    NamePrompt(QWidget* host, const QString& title, const QString& placeholder);

signals:
    void accepted(const QString& name);

protected:
    void restyle() override;

private:
    QLabel* title_ = nullptr;
    QLineEdit* input_ = nullptr;
    QLabel* hint_ = nullptr;
};

// The title card shown the first time a new page opens: black, EPISODE:N, the title large,
// the caption under it. About 1.6 s with a fade; a click or any key ends it early (the key
// still goes where it was going). One-off, then the widget deletes itself.
class TitleFlash : public QWidget {
    Q_OBJECT
public:
    TitleFlash(QWidget* host, int episode, const QString& title, const QString& caption);

protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    bool eventFilter(QObject*, QEvent*) override;

private:
    void finish();
    QString episode_, title_, caption_;
    QElapsedTimer clock_;
    QTimer frame_;
    qreal opacity_ = 0;
};

// MAGI: the question asked when the open page changed on disk while you were editing it.
// Three panels (MELCHIOR-1 overwrite, BALTHASAR-2 reload, CASPER-3 wait); ←/→ or 1/2/3
// choose, Enter decides, Esc waits. exec() blocks like a dialog and returns the choice.
class MagiPanel : public Overlay {
    Q_OBJECT
public:
    enum Choice { Overwrite = 0, Reload = 1, Wait = 2 };
    MagiPanel(QWidget* host, const QString& fileName);
    Choice exec();
    void choose(int c);  // for tests: select
    void decide();       // and confirm

protected:
    void restyle() override;
    void keyPressEvent(QKeyEvent*) override;

private:
    class Votes;
    Votes* votes_ = nullptr;
    QLabel* title_ = nullptr;
    QLabel* message_ = nullptr;
    QLabel* hint_ = nullptr;
    Choice result_ = Wait;
    bool decided_ = false;
};

// A yes/no question for something that cannot be taken back lightly (deleting a page).
// Enter does it, Esc keeps things as they are. exec() blocks like a dialog.
class ConfirmPanel : public Overlay {
    Q_OBJECT
public:
    ConfirmPanel(QWidget* host, const QString& title, const QString& message, const QString& action);
    bool exec();
    void answer(bool yes);  // for tests

protected:
    void restyle() override;
    void keyPressEvent(QKeyEvent*) override;

private:
    QLabel* title_ = nullptr;
    QLabel* message_ = nullptr;
    QLabel* hint_ = nullptr;
    bool yes_ = false;
    bool decided_ = false;
};

// The eyecatch: the card the series shows at the mid-episode break, shown when an operation
// ends. NEON GENESIS / EVANGELION, then what was done. Stays until a click or a key.
class Eyecatch : public QWidget {
    Q_OBJECT
public:
    Eyecatch(QWidget* host, const QString& headline, const QString& detail);

protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    bool eventFilter(QObject*, QEvent*) override;

private:
    void finish();
    QString headline_, detail_;
    QElapsedTimer clock_;
    QTimer frame_;
    qreal opacity_ = 0;
};

// Every key the app currently answers to, grouped into sections.
class KeySheet : public Overlay {
    Q_OBJECT
public:
    struct Section {
        QString name;
        QList<QPair<QString, QString>> keys;  // "Ctrl+Shift+P", "what it does"
    };
    // `snippets`: (trigger, what it gives), shown on a second page (Tab).
    KeySheet(QWidget* host, const QList<Section>& sections, const QList<QPair<QString, QString>>& snippets = {});

protected:
    void restyle() override;
    bool focusNextPrevChild(bool) override;  // Tab: the other page

private:
    void showPage(int page);
    QList<QLabel*> headings_, caps_, texts_, triggers_;
    QLabel* tab_[2] = {nullptr, nullptr};
    QWidget* page_[2] = {nullptr, nullptr};
    int current_ = 0;
    QLabel* footer_ = nullptr;
};

// Quick switcher (Ctrl+K): type to find pages by title and blocks by text; Enter opens.
class QuickSwitcher : public Overlay {
    Q_OBJECT
public:
    struct Result {
        enum Kind { Page, Block, Create } kind = Page;
        QString primary;    // page title, or the block's text
        QString secondary;  // for blocks: the page it is on
        QString path;
        int block = -1;
    };
    QuickSwitcher(QWidget* host, std::function<QList<Result>(const QString&)> search);

signals:
    void chosen(const QuickSwitcher::Result& result);

protected:
    void restyle() override;
    bool eventFilter(QObject*, QEvent*) override;

private:
    void refresh(const QString& text);
    std::function<QList<Result>(const QString&)> search_;
    QList<Result> results_;
    QLabel* title_ = nullptr;
    QLineEdit* input_ = nullptr;
    QListWidget* list_ = nullptr;
    QLabel* hint_ = nullptr;
};

// Tasks (Ctrl+Shift+T): open tasks from every page, grouped Overdue / Today / Upcoming / No date.
// Enter jumps to the block; Ctrl+Enter marks it done. The rows are built when the panel opens
// and freed when it closes.
class TasksPanel : public Overlay {
    Q_OBJECT
public:
    struct Row {
        QString section;  // non-empty: a section heading, the rest is unused
        QString marker, text, page, path, blockText;
        int block = -1;
        QDate due;
        QDateTime at;       // a deadline with a time: the countdown instead of the date
        bool late = false;  // overdue or due today
        int level = 0;      // a subtask: 1, 2... under the task above it
        bool lastSibling = true;  // the last subtask under its parent (the tree line stops)
        int subDone = 0, subTotal = 0;
    };
    using Fetch = std::function<QList<Row>()>;
    using Complete = std::function<bool(const Row&)>;
    TasksPanel(QWidget* host, Fetch fetch, Complete complete);
    using Step = std::function<bool(const Row&, int)>;  // Alt+= / Alt+-: the row's progress bar
    void setStep(Step step) { step_ = std::move(step); }

signals:
    void open(const QString& path, int block);

protected:
    void restyle() override;
    bool eventFilter(QObject*, QEvent*) override;

private:
    void reload();
    void filter(const QString& text);
    int selectedTask() const;  // index into rows_, or -1
    Fetch fetch_;
    Complete complete_;
    Step step_;
    QList<Row> rows_;
    QLabel* title_ = nullptr;
    QLineEdit* input_ = nullptr;
    QListWidget* list_ = nullptr;
    QLabel* hint_ = nullptr;
};

// Today's timeline (Ctrl+D): every time block of the day and every deadline with a time today,
// in order, with now marked and the free time between blocks. Enter jumps to the block,
// Ctrl+Enter marks it done. Built when it opens.
class TimelinePanel : public Overlay {
    Q_OBJECT
public:
    struct Row {
        int start = -1, end = -1;  // minutes since midnight; end -1: a moment
        QString marker, text, context, path, blockText;  // context: the block it sits under
        int block = -1;
        bool done = false;
        bool deadline = false;  // a DEADLINE with a time, not a time block
        bool weekly = false;    // from the timetable: it repeats, Ctrl+Enter leaves it
    };
    using Fetch = std::function<QList<Row>()>;  // in order
    using Complete = std::function<bool(const Row&)>;
    TimelinePanel(QWidget* host, const QString& title, Fetch fetch, Complete complete);

signals:
    void open(const QString& path, int block);

protected:
    void restyle() override;
    bool eventFilter(QObject*, QEvent*) override;

private:
    void reload();
    int selected() const;  // index into rows_, or -1
    Fetch fetch_;
    Complete complete_;
    QList<Row> rows_;
    QLabel* title_ = nullptr;
    QListWidget* list_ = nullptr;
    QLabel* hint_ = nullptr;
};

// Fonts (Ctrl+Shift+F): two tabs, title and body, each searching the installed families.
// Moving through the list previews on the page itself; Enter keeps both, Esc restores both.
class FontPicker : public Overlay {
    Q_OBJECT
public:
    FontPicker(QWidget* host, const QString& sample);

signals:
    void chosen(const QString& titleFamily, const QString& bodyFamily);  // empty = the default

protected:
    void restyle() override;
    bool eventFilter(QObject*, QEvent*) override;

private:
    enum Target { Title = 0, Body = 1 };
    void showTarget(int target);
    void filter(const QString& text);
    void preview();
    QString current() const;
    static void apply(const QString& title, const QString& body);

    QString pageTitle_;
    QString original_[2], chosen_[2];
    int target_ = Title;
    bool switching_ = false;
    bool accepted_ = false;
    QLabel* tab_[2] = {nullptr, nullptr};
    QLabel* tabHint_ = nullptr;
    QLineEdit* input_ = nullptr;
    QListWidget* list_ = nullptr;
    QLabel* sample_ = nullptr;
    QLabel* hint_ = nullptr;
    QTimer livePreview_;  // the page re-styles on a short delay, so holding an arrow key stays smooth
};

}  // namespace graphite
