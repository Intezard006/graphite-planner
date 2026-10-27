// The outliner: paints a Document's blocks and edits one block at a time.
//
// Every block is drawn from its own QTextDocument (cheap, wraps correctly, gives
// link hit-testing for free). The block being edited is covered by a real
// QTextEdit so cursor, selection, IME and clipboard behave like any text field.
#pragma once

#include <QAbstractScrollArea>
#include <QColor>
#include <QDate>
#include <QDateTime>
#include <QHash>
#include <QList>
#include <QPair>
#include <QPoint>
#include <QRect>
#include <QTimer>
#include <functional>
#include <memory>
#include <vector>

#include "core/document.hpp"
#include "ui/glossary.hpp"
#include "ui/snippets.hpp"
#include "ui/tasks.hpp"

class QPainter;
class QTextDocument;
class QTextEdit;

namespace graphite {

// What drawing one block needs from the rest of the page and the tree.
struct RichContext {
    QHash<QString, int> equations;           // "arrhenius" -> 3, for {#arrhenius} and [[eq:arrhenius]]
    std::shared_ptr<const Glossary> glossary;  // terms to underline; null = none
    QHash<int, QPair<int, int>> progress;      // task block -> {done, total} subtasks (tasks.hpp)
    int block = -1;                            // the block being built (-1: not a page block)
    bool eva = false;                          // immersion: "SYNC 2/5" rather than "2/5"
};

class OutlinerView : public QAbstractScrollArea {
    Q_OBJECT
public:
    explicit OutlinerView(QWidget* parent = nullptr);
    ~OutlinerView() override;

    // Not owned. Pass nullptr to clear the view.
    void setDocument(Document* doc);
    // The tree's definitions ("Term:: meaning"), underlined wherever the term is written.
    void setGlossary(const QList<Definition>& definitions);
    // Headings that follow their subtasks (tasks.hpp), and the "2/5" count on them.
    void setTaskOptions(const TaskSyncOptions& options, bool progress);
    // Day scrap: every block of the page gone, in one undo step.
    void clearPage();
    // Scrolls to the display formula labelled {#label} and selects its block.
    bool goToEquation(const QString& label);
    Document* document() const { return doc_; }

    // Start editing a block; cursorPos < 0 puts the cursor at the end.
    void focusBlock(int block, int cursorPos = -1);
    void endEdit();
    // The document was changed from outside the view (e.g. a task marked done): redraw it.
    void reloadBlocks();
    // Undo across blocks. Call before changing the document from outside the view.
    void pushUndoPoint();
    bool undo();
    bool redo();
    bool canUndo() const { return !undo_.empty(); }

    // The page's description:: (a bullet-less first block), shown as the caption under the
    // title like an episode card; empty if the page has none.
    QString caption() const;
    QRect captionRect() const;  // viewport coordinates; click to edit it

    // Alert strips under the page header (Pattern Blue: due now; Pattern Orange: due soon).
    // Under the strip's heading, its targets: a task, then its subtasks indented under it.
    struct AlertRow {
        QString text;
        int level = 0;      // 0: a target; 1, 2...: its subtasks
        QDate due;          // shown on the right...
        QDateTime at;       // ...or, for a deadline with a time, the countdown to it
        int subDone = 0, subTotal = 0;  // a target's subtasks: the sync ratio
        QString path;       // click: open the task
        int block = -1;
        bool operator==(const AlertRow& o) const {
            return text == o.text && level == o.level && due == o.due && at == o.at && subDone == o.subDone &&
                   subTotal == o.subTotal && path == o.path && block == o.block;
        }
    };
    struct Alert {
        QString title, detail;
        QColor color;
        QList<AlertRow> rows;
        int more = 0;  // targets not listed
        bool operator==(const Alert& o) const {
            return title == o.title && detail == o.detail && color == o.color && rows == o.rows && more == o.more;
        }
    };
    void setAlerts(const QList<Alert>& alerts);
    // Today's journal: its time blocks ("09:00 …") get a timeline in the left margin, with now
    // marked. Called again whenever a block starts or ends, which repaints it.
    void setTimelineToday(bool on);
    bool timelineToday() const { return timeline_; }
    QRect alertRect(int i = 0) const;  // viewport coordinates; empty if there is no such strip
    const QList<Alert>& alerts() const { return alerts_; }
    QString renderedText(int block) const;  // the block as shown (not edited), for tests
    int activeBlock() const { return active_; }  // the block being edited, or -1 (tests)
    QRect selectionRect() const;  // the selection's moving end in viewport coordinates, or invalid (tests)

    // Find in page (Ctrl+F): highlights matches in the visible blocks.
    void openFind();
    void closeFind();
    void setFindQuery(const QString& q);
    void findStep(int delta);  // next / previous match, scrolled into view
    int findCount() const { return static_cast<int>(matches_.size()); }
    int findCurrent() const { return findIndex_; }

    // Block selection (Esc, Shift+arrows, Shift+click, dragging across blocks). Blocks in
    // document order; empty when nothing is selected.
    std::vector<int> selectedBlocks() const;
    void selectBlocks(int fromBlock, int toBlock);
    void clearSelection();
    int currentBlock() const { return active_; }

    // Re-read colours/fonts after the theme changed.
    void refreshTheme();

    // Where a block's text is, in viewport coordinates (empty if the block is hidden).
    QRect textRect(int block) const;

    // Title strip drawn above the first block, scrolling with the page. `kind` is the small
    // label ("Journal", "Page"); a valid `date` adds a weekday / day-of-year readout.
    void setHeader(const QString& title, const QString& kind, const QDate& date = QDate());

    // "Linked references" under the page: blocks on other pages that mention this one.
    struct RefGroup {
        QString title;
        QString path;
        QList<QPair<int, QString>> blocks;  // block index in that page, its raw text
        bool operator==(const RefGroup& o) const { return path == o.path && title == o.title && blocks == o.blocks; }
    };
    void setReferences(const QList<RefGroup>& groups, int total);
    // A section above the references (Next Episode: tasks for tomorrow); empty groups hide it.
    void setUpcoming(const QString& label, const QList<RefGroup>& groups);
    QRect upcomingRect(int group, int row) const;  // viewport coordinates, for tests
    int referenceCount() const { return refTotal_; }
    // Where a reference row is, in viewport coordinates (for tests); empty if none.
    QRect referenceRect(int group, int row) const;

    // Where the open page lives (for relative image paths) and where pasted images go.
    void setPageDir(const QString& dir) { pageDir_ = dir; }
    void setAssetsDir(const QString& dir) { assetsDir_ = dir; }

    // ";today" inserts a link to this page title; "/tomorrow" etc. use the date version.
    void setTodayTitle(std::function<QString()> fn) { todayTitle_ = std::move(fn); }
    void setJournalTitle(std::function<QString(const QDate&)> fn) { journalTitleFor_ = std::move(fn); }
    // "/image" asks for a file through this (a file dialog; tests replace it).
    void setImagePicker(std::function<QString()> fn) { pickImage_ = std::move(fn); }
    // The whole page (collapsed blocks included) as an A4 PDF in print colours.
    bool exportPdf(const QString& file, const QString& title, const QString& kind, const QDate& date) const;

    // Every snippet, for the key sheet: (trigger, what it gives).
    static QList<QPair<QString, QString>> snippetHelp();

    // The editing keys handled in eventFilter(), for the key sheet: (keys, description).
    static QList<QPair<QString, QString>> keyHelp();

signals:
    void edited();                          // text or structure changed
    void linkActivated(const QString& target);  // page name or http(s) URL
    void referenceActivated(const QString& path, int block);  // block -1: the page itself
    void alertActivated();
    void taskActivated(const QString& path, int block);  // a row of an alert strip
    void operationCommand(const QString& args);
    void tasksFinished(const QStringList& names);
    void scrapRequested();  // "/scrap"  // headings that just turned DONE with their subtasks
    void message(const QString& text);  // a short status line ("Balanced: ...")  // "/timer 20m 15s" + Enter: "20m 15s", "stop", or "" 

protected:
    void paintEvent(QPaintEvent*) override;
    void resizeEvent(QResizeEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void mouseReleaseEvent(QMouseEvent*) override;
    void leaveEvent(QEvent*) override;
    void keyPressEvent(QKeyEvent*) override;
    void scrollContentsBy(int dx, int dy) override;
    bool eventFilter(QObject*, QEvent*) override;

private:
    struct Item {
        int block = 0;
        int depth = 0;
        int y = 0;       // top of the row, content coordinates
        int h = 0;       // height of the text itself (row adds padding)
        int textX = 0;
        int textW = 0;
        std::unique_ptr<QTextDocument> doc;
    };
    struct RefItem {
        int section = 1;  // 0: the upcoming section, 1: linked references
        int group = 0;    // index into that section's groups
        int row = -1;  // -1: the group's page title
        int y = 0, h = 0, textX = 0, textW = 0;
        std::unique_ptr<QTextDocument> doc;  // block rows only
    };
    enum class Region { None, Chevron, Text };
    struct Hit {
        int item = -1;
        Region region = Region::None;
    };

    void columnGeometry(int& left, int& width) const;
    void rebuild();     // visible list, rich text, layout
    void relayout();    // positions and scroll range only
    void positionEditor();
    void loadEditor(int block);
    void applyEditorStyle();
    void syncFromEditor();
    void ensureCursorVisible();
    void ensureItemVisible(int item);  // scroll so the item shows (its top, when taller than the view)

    int itemOf(int block) const;
    Hit hitTest(const QPoint& viewportPos) const;
    QPoint toContent(const QPoint& viewportPos) const;
    void buildReferences();
    int refAt(const QPoint& contentPos) const;  // index into refItems_, or -1
    void paintReferences(QPainter& p, int sy, int vh);
    QFont fontForText(const QString& raw) const;

    void splitBlock();
    bool joinWithPrevious();
    void navigate(bool up);
    void structural(bool (Document::*op)(int));
    void moveBlock(bool up);
    void setCollapsedActive(bool collapse);
    void wakeCursor();
    void updatePreview();  // the live formula card under the line being edited
    void updateSnippets(); // the snippet list under the cursor, while a trigger is being typed
    bool acceptSnippet();  // insert the highlighted snippet; false if none is showing
    bool pasteImage();     // Ctrl+V of a picture: saved to assets/, linked at the cursor
    QString importImage(const QString& file);  // copy into assets/, return the link
    bool runCommand(int index);                // one entry of the "/" menu
    QString formulaAt(const QPoint& viewportPos) const;  // TeX source of a typeset formula there  // typing or moving: blink again, and stop 10 s after the last activity
    bool pairBrackets(const QString& typed);
    bool deletePair();

    Document* doc_ = nullptr;
    std::vector<Item> items_;
    QTextEdit* editor_ = nullptr;
    int active_ = -1;
    int lastActive_ = 0;
    int hoverBlock_ = -1;
    int hoverRef_ = -1;
    void pushUndo(const Document& before);  // record `before` as an undo step
    void commitTyping();                    // the open block's text edit becomes an undo step
    void restoreSnapshot(const std::string& to, std::vector<std::string>& other);

    bool selectionKey(QKeyEvent* e);        // keys while blocks are selected
    std::vector<int> selectionRoots() const;  // selected blocks not inside another selected one
    void copySelection();
    void deleteSelection();
    bool pasteBlocks(int afterBlock);        // clipboard bullets -> blocks after this one
    void extendFromEditor(bool up);

    void applyFind();  // highlight findQuery_ in the freshly built block documents
    struct Match {
        int item, pos, len;
    };
    bool captionBlock(int block) const;  // block 0 holding only the description:: line
    bool revealCaption_ = false;         // shown as a block while it is being edited
    QList<Alert> alerts_;
    bool timeline_ = false;
    int alertTop_ = -1;  // content y of the first strip, -1 = none
    QString findQuery_;
    std::vector<Match> matches_;
    int findIndex_ = -1;
    QWidget* findBar_ = nullptr;

    int selAnchor_ = -1, selHead_ = -1;     // item indices; -1 = no block selection
    int pressItem_ = -1;                    // mouse press, for drag selection
    int pressCursor_ = -1;
    // Undo steps are the page as it would be written (serialize()), re-read on undo: about the
    // file's size each, instead of ~5x for an in-memory copy. Capped in count and bytes.
    std::vector<std::string> undo_, redo_;
    int editBlock_ = -1;                    // the block being typed in, and its text then
    std::string editStartText_;
    QString pageDir_, assetsDir_;
    QWidget* preview_ = nullptr;
    QWidget* snippetPopup_ = nullptr;
    enum class Suggest { None, Snippets, Commands, Date, Time, Timer } suggestMode_ = Suggest::None;
    void showDatePopup(Suggest mode);  // the rows are set: place and show the list
    void openDatePicker(const QString& kind);  // after /scheduled or /deadline
    bool acceptDate(int row);
    void openTimePicker(const QDate& day);  // after a deadline's date
    bool acceptTime(int row);
    bool runTimerLine();
    QString dateKind_;
    int datePos_ = -1;
    QList<QDate> dateRows_;
    QList<QTime> timeRows_;  // after a deadline's date: row 0 = no time, 1 = type one
    QStringList timerRows_;  // per timer row: the preset it puts in, or "" for the typed one
    QList<Snippet> snippetMatches_;
    bool snippetMath_ = false;
    QList<int> commandMatches_;
    std::function<QString()> pickImage_;
    std::function<QString(const QDate&)> journalTitleFor_;
    std::function<QString()> todayTitle_;
    QString hoverFormula_;
    QString hoverTip_;          // the definition or formula note being shown
    RichContext rich_;          // this page's equation numbers, the tree's glossary
    QList<Definition> definitions_;
    QString definitionAt(const QPoint& viewportPos) const;
    QTimer blinkIdle_;
    QTimer countdown_;  // on the minute, only while a deadline with a time is shown
    void scheduleCountdown();
    void countdownTick();
    int alertRowAt(const QPoint& viewportPos, int* alert) const;
    int flashTime_ = 0;  // the desktop's blink period; 0 = it does not blink at all
    bool blinking_ = true;
    int contentHeight_ = 0;
    bool syncing_ = false;
    // Heading sync: runs after you change a task marker or the outline; true when it changed
    // something (the page is then re-rendered, the editor's text kept in step).
    bool runTaskSync(const QList<int>& userChanged);
    void changed();  // a structural edit: sync, then edited()
    void countProgress();  // rich_.progress for the page
    void announceProgress(const QString& text);  // "Sync 38% ▮▮▮▯▯… 45 / 120" after a step
    int selectAfter_ = 0;  // runCommand: characters to select after the cursor (the /progress end)
    TaskSyncOptions taskOpts_;
    bool taskProgress_ = true;
    struct Cascade {
        int heading = -1;
        QList<std::pair<int, std::pair<std::string, std::string>>> blocks;  // block, {before, after}
    } lastCascade_;              // undone when the heading leaves DONE while still being edited
    QList<int> pendingFold_;     // finished headings around the block being edited: folded on leaving it
    const QList<RefGroup>& groupsOf(int section) const { return section == 0 ? upcoming_ : refGroups_; }
    QList<RefGroup> upcoming_;
    QString upcomingLabel_;
    int upcomingTop_ = -1, bottomTop_ = -1;  // content y of that section, and of everything below the blocks
    QList<RefGroup> refGroups_;
    std::vector<RefItem> refItems_;
    int refTotal_ = 0;
    int refsTop_ = -1;  // content y where the section starts, -1 = none
    QString headerTitle_;
    QString headerKind_;
    QDate headerDate_;
};

}  // namespace graphite
