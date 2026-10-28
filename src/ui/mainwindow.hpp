#pragma once

#include <QDate>
#include <QDateTime>
#include <QHash>
#include <QList>
#include <QMainWindow>
#include <QPair>
#include <QPointer>
#include <QSet>
#include <QFileSystemWatcher>
#include <QTimer>
#include <memory>

#include "core/document.hpp"
#include "notes/graphindex.hpp"
#include "desktop/palette.hpp"
#include "ui/nervpanel.hpp"
#include "ui/outlinerview.hpp"
#include "planner/tasks.hpp"

class QLabel;
class QStackedWidget;

namespace graphite {

class BlurControl;
class Updater;
class ArtDownload;
class Explorer;
class BlurMeter;
class NervPanel;
struct Task;
struct TimetableEntry;
class SettingsPage;
class OutlinerView;
class Overlay;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(const QString& graphPath, QWidget* parent = nullptr);
    ~MainWindow() override;

    const GraphIndex& index() const { return index_; }
    QString currentPage() const { return page_ ? page_->path : QString(); }  // tests
    // Graphite Essentials: write the supplied pages this tree has not had yet (`restore`: also
    // the ones written before and deleted since). The number of pages written.
    int supplyEssentials(bool restore = false);
    NervPanel* nervPanel() const { return nerv_; }
    // Operations (study timer): "/timer 20m 15s" in a block, or these.
    bool startOperation(int seconds);
    void stopOperation();
    bool operationRunning() const { return opEnd_.isValid(); }

    // Switch to the tree in `dir`; a folder that is not a tree yet becomes one.
    bool openTree(const QString& dir);
    QString treeDir() const { return graph_; }

    // Ctrl+Shift+Del: ask, then move the open page's file to the Trash.
    bool deletePage();
    // The open page as a PDF at `file` (Ctrl+Shift+E asks where).
    bool exportPdf(const QString& file);

    // For tests and scripting: open a file in the graph by absolute path.
    bool openPath(const QString& path, bool isNew = false);
    OutlinerView* view() const { return view_; }

    // Settings › Tasks (QSettings tasks/finish|start|fold|progress, all on by default).
    TaskSyncOptions taskOptions() const;
    void applyTaskOptions();
    // Day scrap (Ctrl+Shift+Backspace, /scrap): the open journal wiped after a yes, under a
    // Third Impact with immersion; Ctrl+Z restores it, and a copy goes to .graphite/bak first.
    bool scrapDay();
    void openFieldManual();  // F2
    // Settings › Sidebar unit: your own picture (custom.png), or the Eva art fetched after a
    // consent card. Both end in the art folder (unitart.hpp); nothing is fetched without a yes.
    bool setCustomArt(const QString& path);
    bool downloadArt();  // false when declined or already running

signals:
    void paletteRequested(const QString& unit);  // "" = wallpaper, "unit01" / "unit00" / "unit02"

protected:
    bool eventFilter(QObject*, QEvent*) override;  // the tree name: a click opens the tree menu
    void paintEvent(QPaintEvent*) override;
    void closeEvent(QCloseEvent*) override;
    void changeEvent(QEvent*) override;

private:
    struct Page {
        QString path;
        QString title;
        bool journal = false;
        bool isNew = false;
        qint64 mtime = 0;
        Document doc;
    };

    void buildUi();
    void applyTheme();
    void scanGraph();
    void showToday();
    void newPage();
    void openLink(const QString& target);
    // Trees, books, pages on disk (a tree is graph_; a book is a folder in it).
    struct BookInfo {
        QString name, dir;
    };
    QList<BookInfo> books(bool withoutPages = false) const;  // true: folders of other files too
    QString bookName(const QString& dir) const;  // "Unsorted" for a Logseq pages/ folder
    QString defaultBookDir() const;              // where a new page goes when nothing says
    bool isBookDir(const QString& dir) const;
    QString treeName() const;
    void setTree(const QString& dir);            // switch to another tree (saves first)
    void openTreeDialog();                       // pick a folder: open it, or start a new tree
    void showTreeMenu();
    // Paths by value: callers may pass page_->path, and these can close the open page.
    void movePage(QString path, QString bookDir);
    void renameEntry(QString path, QString name);
    bool deletePath(QString path);
    void newBook();
    int rewriteLinks(const QString& oldTitle, const QString& newTitle, const QString& skipPath);

    // `path` by value: callers pass page_->path, and the old page is destroyed in here.
    bool loadPage(QString path, bool isNew);
    void reloadCurrent();
    bool needsSave() const;
    bool saveNow();
    void scheduleSave();
    void backupBeforeOverwrite();
    void checkExternalChange();
    void updateStatus();
    void flash(const QString& text, int ms = 4000);
    void chooseGraph();
    void toggleKeySheet();
    void setNervMode(NervPanel::Mode m);
    void quickSwitch();
    void showTasks();
    void showTimeline();
    void openTimetable();
    void updatePatternBlue();
    void updateGlossary();
    void updateExam();
    // Ramiel's moments and the day's time blocks, from the task list updatePatternBlue made.
    void updateRamiel(const QList<Task>& tasks);
    void updateNext();
    QList<TimetableEntry> todayTimetable() const;
    void scheduleDeadlineWake(const QList<Task>& tasks);
    void onOperationCommand(const QString& args);
    void operationTick();
    void finishOperation();
    void onUpdateChanged();
    void restartForUpdate();
    void appendToToday(const QString& text);  // one block at the end of today's journal
    // Replace one block's text in any page of the graph, with the usual backup and atomic write.
    // `expected` guards against a file that changed meanwhile.
    bool setBlockText(const QString& path, int block, const QString& expected, const QString& text);
    void announceFinished(const QStringList& names);  // "Target neutralised · X"
    void backupFile(const QString& path, bool again = false);  // again: even if done this session
    QList<GraphIndex::File> graphFiles() const;
    void syncIndex();       // re-read changed files, then refresh the backlinks shown
    void showReferences();
    void chooseFonts();
    void showSettings();
    void closeSettings();
    QString currentUnit() const;  // "" = wallpaper
    int episodeNumber() const;
    void toggleFocusMode();
    void setTextSize(qreal pt);  // applied and remembered
    void closeOverlay();

    QString journalTitle(const QDate& d) const;
    QString pagePath(const QString& title) const;
    QString titleFromPath(const QString& path, bool* journal) const;

    QString graph_;
    QString journalFormat_ = "yyyy_MM_dd";
    QHash<QString, QString> pathByTitle_;  // lower-case title -> file path
    QSet<QString> backedUp_;
    std::unique_ptr<Page> page_;

    OutlinerView* view_ = nullptr;
    Explorer* nav_ = nullptr;
    QWidget* sidebar_ = nullptr;
    QLabel* pathLabel_ = nullptr;
    QLabel* stateLabel_ = nullptr;
    QLabel* graphLabel_ = nullptr;
    QTimer saveTimer_;
    QList<QPair<QString, QString>> windowKeys_;  // shortcut, description: for the key sheet
    QPointer<Overlay> overlay_;                  // the open in-window panel, if any
    GraphIndex index_;
    BlurControl* blur_ = nullptr;
    BlurMeter* blurMeter_ = nullptr;
    NervPanel* nerv_ = nullptr;
    QStackedWidget* stack_ = nullptr;   // the page, or Settings in its place
    SettingsPage* settings_ = nullptr;
    Updater* updater_ = nullptr;
    ArtDownload* artDownload_ = nullptr;  // while "Download Eva art" runs
    QWidget* updateAction_ = nullptr;  // the sidebar's "Update ready" row, hidden when up to date
    int updatesRow_ = -1;              // its Settings row ("Update")
    int lastUpdateState_ = 0;
    QWidget* tasksAction_ = nullptr;  // the sidebar's Tasks row (shows the due count)
    bool activatedOnce_ = false;
    QString newPageIn_;       // "New page here" from the explorer: that book
    QFileSystemWatcher treeWatch_;  // the tree's folders: books or pages added / removed outside
    QTimer treeSettle_;             // ...picked up 400 ms after the last change
    QString treeSignature_;         // what the explorer shows; rebuilt only when it changes
    PaletteColors lastExplorerPalette_;  // ...or when the colours did (its headings carry them)
    QString pendingCaption_;  // the caption just typed for a new page, for its title card
    int pendingEpisode_ = 0;  // and the episode number its prompt showed
    bool deliberating_ = false;  // the MAGI panel is asking about a conflict
    QDateTime opStart_, opEnd_;  // the running operation; invalid = none (saved in QSettings too)
    QTimer opTick_;              // once a second, only while one runs
    QTimer deadlineWake_;        // single shot: the next deadline, time block or midnight
    int lastOpenToday_ = -1;     // open tasks due today or before, at the last update
    QDateTime flashUntil_;       // a status message holds until then
    QList<OutlinerView::RefGroup> lastRefs_;     // what the view shows, to skip identical rebuilds
    bool externalChange_ = false;
};

}  // namespace graphite
