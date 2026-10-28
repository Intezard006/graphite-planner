#include "ui/mainwindow.hpp"

#include <QAbstractButton>
#include <QAction>
#include <QApplication>
#include <QCloseEvent>
#include <QDateTime>
#include <QCryptographicHash>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPushButton>
#include <QRegularExpression>
#include <QSaveFile>
#include <QFontInfo>
#include <cmath>
#include <QProcess>
#include <QRandomGenerator>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMenu>
#include <QSettings>
#include <QStackedWidget>
#include <QStandardPaths>
#include <QUrl>
#include <QVBoxLayout>

#include "desktop/blur.hpp"
#include "desktop/updates.hpp"
#include "notes/glossary.hpp"
#include "notes/essentials.hpp"
#include "ui/explorer.hpp"
#include "planner/operation.hpp"
#include "desktop/palette.hpp"
#include "ui/settingspage.hpp"
#include "ui/unitart.hpp"
#include "ui/nervpanel.hpp"
#include "planner/progress.hpp"
#include "planner/tasks.hpp"
#include "planner/timetable.hpp"
#include "ui/outlinerview.hpp"
#include "ui/overlay.hpp"
#include "ui/theme.hpp"
#include "ui/thirdimpact.hpp"

namespace graphite {

namespace {

// The sidebar widens with the text size (up to 1.45x), so labels and hints keep their room.
int sidebarWidth() { return static_cast<int>(236 * qBound<qreal>(1.0, Theme::instance().scale(), 1.45)); }

QString ordinal(int day) {
    if (day % 100 >= 11 && day % 100 <= 13) return QString::number(day) + "th";
    switch (day % 10) {
        case 1: return QString::number(day) + "st";
        case 2: return QString::number(day) + "nd";
        case 3: return QString::number(day) + "rd";
        default: return QString::number(day) + "th";
    }
}

QString readJournalFormat(const QString& graph) {
    QFile f(graph + "/logseq/config.edn");
    if (!f.open(QIODevice::ReadOnly)) return "yyyy_MM_dd";
    const QString text = QString::fromUtf8(f.readAll());
    static const QRegularExpression re(R"re(^\s*:journal/file-name-format\s+"([^"]+)")re",
                                       QRegularExpression::MultilineOption);
    const QRegularExpressionMatch m = re.match(text);
    return m.hasMatch() ? m.captured(1) : QString("yyyy_MM_dd");
}

qint64 mtimeOf(const QString& path) { return QFileInfo(path).lastModified().toMSecsSinceEpoch(); }

// A sidebar row for an action ("Today", "New page"): reads like the page list below it,
// with its shortcut on the right instead of a button frame.
class SideAction : public QAbstractButton {
public:
    SideAction(const QString& text, const QString& hint, QWidget* parent) : QAbstractButton(parent), hint_(hint) {
        setText(text);
        setCursor(Qt::PointingHandCursor);
        setFocusPolicy(Qt::NoFocus);
        setAttribute(Qt::WA_Hover);
    }
    QSize sizeHint() const override { return QSize(sidebarWidth(), QFontMetrics(Theme::instance().bodyFont()).lineSpacing() + 13); }

protected:
    void paintEvent(QPaintEvent*) override {
        const Theme& th = Theme::instance();
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        const QRectF r = QRectF(rect()).adjusted(8, 1, -8, -1);
        const bool hot = underMouse();
        if (hot || isDown()) {
            QColor tint = th.structure();
            tint.setAlpha(isDown() ? 52 : 30);
            p.setPen(Qt::NoPen);
            p.setBrush(tint);
            p.drawRoundedRect(r, 3, 3);
        }
        // A small square, like the one before the page kind in the header; warm on an alert row.
        const bool alert = property("alert").toBool();
        p.fillRect(QRectF(r.left() + 12, r.center().y() - 2, 4, 4), alert ? th.warm() : hot ? th.accent() : th.structure());
        p.setFont(th.bodyFont());
        p.setPen(hot || alert ? th.text() : th.dim());
        p.drawText(r.adjusted(26, 0, 0, 0), Qt::AlignVCenter | Qt::AlignLeft, text());
        // A count (Tasks: due today or overdue), in the warm colour, after the name.
        const int badge = property("badge").toInt();
        if (badge > 0) {
            const QString b = QString::number(badge);
            const int bx = static_cast<int>(r.left()) + 26 + QFontMetrics(th.bodyFont()).horizontalAdvance(text()) + 8;
            const QFontMetrics lm(th.labelFont());
            const QRectF pill(bx, r.center().y() - lm.height() / 2.0, lm.horizontalAdvance(b) + 10, lm.height());
            QColor fill = th.warm();
            fill.setAlpha(40);
            p.setPen(Qt::NoPen);
            p.setBrush(fill);
            p.drawRoundedRect(pill, 3, 3);
            p.setFont(th.labelFont());
            p.setPen(th.warm());
            p.drawText(pill, Qt::AlignCenter, b);
        }
        // The shortcut hint, only when it fits beside the name.
        const int nameRight = static_cast<int>(r.left()) + 26 + QFontMetrics(th.bodyFont()).horizontalAdvance(text());
        const int hintW = QFontMetrics(th.labelFont()).horizontalAdvance(hint_.toUpper());
        if (nameRight + 12 < static_cast<int>(r.right()) - 10 - hintW) {
            p.setFont(th.labelFont());
            p.setPen(hot ? th.dim() : th.faint());
            p.drawText(r.adjusted(0, 0, -10, 0), Qt::AlignVCenter | Qt::AlignRight, hint_);
        }
    }

public:
    void setHint(const QString& h) { hint_ = h; }

private:
    QString hint_;
};

}  // namespace

MainWindow::MainWindow(const QString& graphPath, QWidget* parent) : QMainWindow(parent), graph_(graphPath) {
    setWindowTitle("Graphite");
    resize(1180, 760);
    // Alpha must be requested before the window is first shown. Every pixel is still
    // painted opaque in normal mode; Ctrl+T only changes the background alpha.
    setAttribute(Qt::WA_TranslucentBackground);

    journalFormat_ = readJournalFormat(graph_);
    buildUi();
    applyTheme();
    connect(&Theme::instance(), &Theme::changed, this, [this] {
        // Blur follows transparent mode: on with Ctrl+T, the user's own values back when off.
        blur_->setActive(Theme::instance().transparent());
        blurMeter_->setVisible(blur_->available() && Theme::instance().transparent());
        applyTheme();
        updateStatus();
    });

    deadlineWake_.setSingleShot(true);
    deadlineWake_.setTimerType(Qt::CoarseTimer);
    connect(&deadlineWake_, &QTimer::timeout, this, &MainWindow::updatePatternBlue);
    opTick_.setInterval(1000);
    opTick_.setTimerType(Qt::CoarseTimer);
    connect(&opTick_, &QTimer::timeout, this, &MainWindow::operationTick);
    {   // An operation still running from before a restart carries on (or ends now if it is over).
        QSettings s;
        const QDateTime end = s.value("operation/end").toDateTime(), start = s.value("operation/start").toDateTime();
        if (end.isValid() && end > QDateTime::currentDateTime()) {
            opStart_ = start;
            opEnd_ = end;
            opTick_.start();
        } else {
            s.remove("operation");
        }
    }
    treeSettle_.setSingleShot(true);
    treeSettle_.setInterval(400);
    connect(&treeWatch_, &QFileSystemWatcher::directoryChanged, this, [this] { treeSettle_.start(); });
    connect(&treeSettle_, &QTimer::timeout, this, [this] { scanGraph(); });
    saveTimer_.setSingleShot(true);
    saveTimer_.setInterval(700);
    connect(&saveTimer_, &QTimer::timeout, this, [this] { saveNow(); });

    // Updates from the source folder: watched a few seconds after start, so it never slows it.
    updater_ = new Updater(this);
    connect(updater_, &Updater::changed, this, &MainWindow::onUpdateChanged);
#ifndef GRAPHITE_PACKAGED  // a packaged build (app bundle, distro package) has no source folder to watch
    QTimer::singleShot(3000, this, [this] { updater_->setWatching(QSettings().value("updates", "watch").toString() == "watch"); });
#endif
    if (!QSettings().value("manualHinted").toBool())  // once: where the tutorial is
        QTimer::singleShot(1500, this, [this] {
            QSettings().setValue("manualHinted", true);
            flash("New here? F2 opens the field manual", 6000);
        });

    scanGraph();
    if (supplyEssentials() > 0) scanGraph();
    graphLabel_->setText(treeName() + "  \xE2\x96\xBE");
    // Where you left off in this tree: its last page, else today's journal.
    const QString last = QSettings().value("lastPage/" + QString::number(qHash(graph_))).toString();
    if (!last.isEmpty() && QFileInfo::exists(last) && last.startsWith(graph_ + "/")) openPath(last);
    else showToday();

    QSettings s;
    s.setValue("graph", graph_);
    QStringList trees = s.value("trees").toStringList();
    trees.removeAll(graph_);
    trees.prepend(graph_);
    s.setValue("trees", trees.mid(0, 8));
}

MainWindow::~MainWindow() = default;

bool MainWindow::eventFilter(QObject* obj, QEvent* ev) {
    if (obj == graphLabel_ && ev->type() == QEvent::MouseButtonPress) {
        showTreeMenu();
        return true;
    }
    return QMainWindow::eventFilter(obj, ev);
}

void MainWindow::buildUi() {
    auto* root = new QWidget(this);
    root->setObjectName("root");
    setCentralWidget(root);
    auto* h = new QHBoxLayout(root);
    h->setContentsMargins(0, 0, 0, 0);
    h->setSpacing(0);

    // ---- sidebar
    sidebar_ = new QWidget(root);
    sidebar_->setObjectName("sidebar");
    sidebar_->setFixedWidth(sidebarWidth());
    auto* sv = new QVBoxLayout(sidebar_);
    sv->setContentsMargins(0, 22, 0, 14);
    sv->setSpacing(0);

    auto* brand = new QLabel("Graphite", sidebar_);
    brand->setFont(Theme::instance().labelFont());
    brand->setContentsMargins(20, 0, 0, 0);
    brand->setObjectName("brand");
    sv->addWidget(brand);
    sv->addSpacing(2);
    graphLabel_ = new QLabel(sidebar_);  // the tree's name; a click lists the other trees
    graphLabel_->setFont(Theme::instance().bodyFont());
    graphLabel_->setCursor(Qt::PointingHandCursor);
    graphLabel_->setToolTip("Trees: switch, open or start one");
    graphLabel_->installEventFilter(this);
    graphLabel_->setContentsMargins(20, 0, 0, 0);
    sv->addWidget(graphLabel_);
    sv->addSpacing(18);

    auto* search = new SideAction("Search", "Ctrl K", sidebar_);
    connect(search, &QAbstractButton::clicked, this, [this] { quickSwitch(); });
    sv->addWidget(search);
    auto* tasks = new SideAction("Tasks", "Ctrl Shift T", sidebar_);
    tasksAction_ = tasks;
    connect(tasks, &QAbstractButton::clicked, this, [this] { showTasks(); });
    sv->addWidget(tasks);
    auto* today = new SideAction("Today", "Ctrl J", sidebar_);
    connect(today, &QAbstractButton::clicked, this, [this] { closeOverlay(); showToday(); });
    auto* fresh = new SideAction("New page", "Ctrl N", sidebar_);
    connect(fresh, &QAbstractButton::clicked, this, [this] { newPage(); });
    sv->addWidget(today);
    sv->addWidget(fresh);
    auto* upd = new SideAction(QString(), QString(), sidebar_);
    upd->setProperty("alert", true);
    updateAction_ = upd;
    upd->hide();
    connect(upd, &QAbstractButton::clicked, this, [this] {
        if (stack_->currentWidget() != settings_) showSettings();
        QTimer::singleShot(0, this, [this] { settings_->select(updatesRow_); });  // once it is laid out
    });
    sv->addWidget(upd);
    sv->addSpacing(14);

    nav_ = new Explorer(sidebar_);
    nav_->setFont(Theme::instance().bodyFont());
    {   // books folded last time stay folded
        const QStringList f = QSettings().value("folded/" + QString::number(qHash(graph_))).toStringList();
        nav_->setFolded(QSet<QString>(f.begin(), f.end()));
    }
    connect(nav_, &Explorer::openRequested, this, [this](const QString& path) { openPath(path); });
    connect(nav_, &Explorer::moveRequested, this, &MainWindow::movePage);
    connect(nav_, &Explorer::renameRequested, this, &MainWindow::renameEntry);
    connect(nav_, &Explorer::deleteRequested, this, [this](const QString& path) { deletePath(path); });
    connect(nav_, &Explorer::newBookRequested, this, &MainWindow::newBook);
    connect(nav_, &Explorer::newPageRequested, this, [this](const QString& dir) {
        newPageIn_ = dir;
        newPage();
    });
    connect(nav_, &Explorer::foldedChanged, this, [this] {
        const QSet<QString> f = nav_->folded();
        QSettings().setValue("folded/" + QString::number(qHash(graph_)), QStringList(f.begin(), f.end()));
    });
    sv->addWidget(nav_, 1);

    // NERV panel: A.T. Field, clock, power. Ctrl+Shift+A hides it.
    nerv_ = new NervPanel(sidebar_);
    auto* nervBox = new QVBoxLayout();
    nervBox->setContentsMargins(20, 10, 20, 4);
    nervBox->addWidget(nerv_);
    sv->addLayout(nervBox);
    {   // "nervMode": pulse / constant / off (older versions saved a plain on/off)
        QSettings s;
        QString mode = s.value("nervMode").toString();
        if (mode.isEmpty()) mode = s.value("nervPanel", true).toBool() ? "pulse" : "off";
        nerv_->setMode(mode == "constant" ? NervPanel::Mode::Constant : mode == "off" ? NervPanel::Mode::Off : NervPanel::Mode::Pulse);
    }

    h->addWidget(sidebar_);

    // ---- content
    auto* content = new QWidget(root);
    content->setObjectName("content");
    auto* cv = new QVBoxLayout(content);
    cv->setContentsMargins(0, 0, 0, 0);
    cv->setSpacing(0);

    view_ = new OutlinerView(content);
    connect(view_, &OutlinerView::edited, this, [this] {
        scheduleSave();
        updateStatus();
    });
    connect(view_, &OutlinerView::linkActivated, this, &MainWindow::openLink);
    connect(view_, &OutlinerView::alertActivated, this, [this] { showTasks(); });
    connect(view_, &OutlinerView::taskActivated, this, [this](const QString& path, int block) {
        if (openPath(path) && block >= 0) view_->focusBlock(block);
    });
    connect(view_, &OutlinerView::operationCommand, this, &MainWindow::onOperationCommand);
    connect(view_, &OutlinerView::message, this, [this](const QString& m) { flash(m); });
    connect(view_, &OutlinerView::tasksFinished, this, &MainWindow::announceFinished);
    connect(view_, &OutlinerView::carryoverCycle, this, &MainWindow::cycleCarried);
    connect(view_, &OutlinerView::scrapRequested, this, [this] { scrapDay(); });
    applyTaskOptions();
    view_->setTodayTitle([this] { return journalTitle(QDate::currentDate()); });
    view_->setJournalTitle([this](const QDate& d) { return journalTitle(d); });
    connect(view_, &OutlinerView::referenceActivated, this, [this](const QString& path, int block) {
        if (openPath(path) && block >= 0) view_->focusBlock(block);
    });
    // The page, or Settings in its place (Ctrl+,).
    stack_ = new QStackedWidget(content);
    stack_->addWidget(view_);
    settings_ = new SettingsPage(stack_);
    stack_->addWidget(settings_);
    connect(settings_, &SettingsPage::closeRequested, this, [this] { closeSettings(); });
    cv->addWidget(stack_, 1);

    auto* bar = new QHBoxLayout();
    bar->setContentsMargins(28, 6, 28, 10);
    pathLabel_ = new QLabel(content);
    pathLabel_->setFont(Theme::instance().labelFont());
    stateLabel_ = new QLabel(content);
    stateLabel_->setObjectName("state");
    stateLabel_->setFont(Theme::instance().labelFont());
    bar->addWidget(pathLabel_, 1);
    blur_ = new BlurControl(this);
    blurMeter_ = new BlurMeter(content);
    blurMeter_->setLevel(blur_->level());
    blurMeter_->hide();
    connect(blurMeter_, &BlurMeter::levelPicked, blur_, &BlurControl::setLevel);
    connect(blur_, &BlurControl::levelChanged, blurMeter_, &BlurMeter::setLevel);
    bar->addWidget(blurMeter_);
    bar->addSpacing(18);
    bar->addWidget(stateLabel_);
    cv->addLayout(bar);

    h->addWidget(content, 1);

    // ---- actions (shortcuts live on the window so they work while editing)
    // Each one is also listed in the key sheet (Ctrl+/) with its description.
    auto add = [this](const QString& seq, const QString& what, auto fn) {
        auto* a = new QAction(this);
        a->setShortcut(QKeySequence(seq));
        a->setShortcutContext(Qt::WindowShortcut);
        connect(a, &QAction::triggered, this, fn);
        addAction(a);
        if (!what.isEmpty()) windowKeys_.append({seq, what});
    };
    add("Ctrl+K", "Search pages and notes", [this] { quickSwitch(); });
    add("Ctrl+Shift+T", "Tasks from every page", [this] { showTasks(); });
    add("Ctrl+D", "Today's timeline", [this] { showTimeline(); });
    add("Ctrl+Shift+D", "The week's timetable", [this] { openTimetable(); });
    add("Ctrl+F", "Find in page", [this] { closeOverlay(); view_->openFind(); });
    add("Ctrl+\\", "Focus mode: hide the sidebar", [this] { toggleFocusMode(); });
    add("Ctrl+J", "Today's journal", [this] { closeOverlay(); showToday(); });
    add("Ctrl+N", "New page", [this] { newPage(); });
    add("Ctrl+O", "Open another graph", [this] { closeOverlay(); chooseGraph(); });
    add("Ctrl+S", "Save now", [this] { saveNow(); });
    add("Ctrl+T", "Transparent background", [] { Theme::instance().toggleTransparent(); });
    add("Ctrl+[", "Less blur (transparent mode)", [this] { blur_->setLevel(blur_->level() - 1); });
    add("Ctrl+]", "More blur (transparent mode)", [this] { blur_->setLevel(blur_->level() + 1); });
    add("Ctrl+P", "Colours: wallpaper / Unit-01 / 00 / 02", [this] {
        // Cycle wallpaper -> Unit-01 -> Unit-00 -> Unit-02 -> wallpaper.
        const QStringList order = {QString(), "unit01", "unit00", "unit02"};
        const int k = static_cast<int>(order.indexOf(currentUnit()));
        const QString next = order[(k + 1) % order.size()];
        emit paletteRequested(next);
        flash(next.isEmpty() ? QString("Colours: wallpaper") : "Colours: " + unitName(next));
    });
    add("Ctrl+,", "Settings", [this] { showSettings(); });
    add("Ctrl+=", "Larger text", [this] { setTextSize(Theme::instance().textSize() + 0.5); });
    add("Ctrl++", QString(), [this] { setTextSize(Theme::instance().textSize() + 0.5); });
    add("Ctrl+-", "Smaller text", [this] { setTextSize(Theme::instance().textSize() - 0.5); });
    add("Ctrl+0", "Default text size", [this] { setTextSize(Theme::kDefaultTextSize); });
    add("Ctrl+Shift+F", "Fonts (title and body)", [this] { chooseFonts(); });
    add("Ctrl+Shift+A", "A.T. Field: pulse / constant / off", [this] {
        using M = NervPanel::Mode;
        const M next = nerv_->mode() == M::Pulse ? M::Constant : nerv_->mode() == M::Constant ? M::Off : M::Pulse;
        setNervMode(next);
        flash("A.T. Field: " + NervPanel::modeName(next));
    });
    add("Ctrl+Shift+Del", "Delete this page (to the Trash)", [this] { deletePage(); });
    add("Ctrl+Shift+Backspace", "Scrap the day: wipe this journal (Ctrl+Z restores it)", [this] { scrapDay(); });
    add("Ctrl+Shift+E", "Export page to PDF", [this] {
        closeOverlay();
        if (!page_) return;
        QString base = page_->title;
        base.replace(QRegularExpression(R"([/\\:*?"<>|])"), "-");
        const QString docs = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
        const QString file = QFileDialog::getSaveFileName(this, "Export page to PDF", docs + "/" + base + ".pdf", "PDF (*.pdf)");
        if (file.isEmpty()) return;
        const bool ok = exportPdf(file.endsWith(".pdf", Qt::CaseInsensitive) ? file : file + ".pdf");
        flash(ok ? "Exported " + QFileInfo(file).fileName() : "Export failed");
    });
    add("Ctrl+/", "This list (also F1)", [this] { toggleKeySheet(); });
    add("F1", QString(), [this] { toggleKeySheet(); });
    add("F2", "Field manual: the tutorial, in the browser", [this] { openFieldManual(); });
    add("Ctrl+Q", "Quit", [this] { close(); });
}

bool MainWindow::exportPdf(const QString& file) {
    if (!page_) return false;
    if (needsSave()) saveNow();
    QDate date;
    if (page_->journal) date = QDate::fromString(QFileInfo(page_->path).completeBaseName(), journalFormat_);
    return view_->exportPdf(file, page_->title, page_->journal ? "Journal" : "Page", date);
}

// Definitions ("Term:: meaning") anywhere in the tree, for the dotted terms and their hover.
// Gathered with the tasks (page open, save, sync); the view redraws only when they change.
void MainWindow::updateGlossary() {
    QList<Definition> defs;
    for (const GraphIndex::Page& pg : index_.pages())
        for (const QString& block : pg.blocks) {
            if (!block.contains("::")) continue;
            for (const QString& line : block.split('\n')) {
                Definition d;
                if (definitionLine(line, &d.term, &d.text)) {
                    d.page = pg.title;
                    defs << d;
                }
            }
        }
    view_->setGlossary(defs);
}

// Pattern Blue: tasks due today or overdue, anywhere in the graph. A strip on today's journal
// and a count on the sidebar's Tasks row. Counted on page open, save and sync only.
void MainWindow::updatePatternBlue() {
    updateGlossary();
    const QDate today = QDate::currentDate();
    int due = 0, overdue = 0, soon = 0;
    const QList<Task> tasks = collectTasks(index_, journalFormat_);
    for (const Task& t : tasks) {
        const QDate d = t.due();
        if (!d.isValid()) continue;
        if (d <= today) {
            ++due;
            if (d < today) ++overdue;
        } else if (d <= today.addDays(3)) {
            ++soon;
        }
    }
    if (tasksAction_) {
        tasksAction_->setProperty("badge", due);
        tasksAction_->update();
    }
    updateExam();
    updateRamiel(tasks);
    updateNext();
    scheduleDeadlineWake(tasks);

    const bool todayPage = page_ && page_->journal &&
                           QDate::fromString(QFileInfo(page_->path).completeBaseName(), journalFormat_) == today;
    view_->setTimelineToday(todayPage);
    QList<OutlinerView::Alert> alerts;
    if (todayPage) {
        const bool eva = Theme::instance().immersion();
        auto count = [eva](int n) {
            return eva ? QString("%1 target%2").arg(n).arg(n == 1 ? "" : "s") : QString("%1 task%2").arg(n).arg(n == 1 ? "" : "s");
        };
        // Each strip lists its targets: a task whose own date or a subtask's date is in the
        // strip's range, with its open subtasks indented under it.
        auto rowsFor = [&](OutlinerView::Alert& al, auto inRange) {
            constexpr int kMaxRows = 8;
            for (int i = 0; i < tasks.size(); ++i) {
                if (tasks[i].parent >= 0) continue;
                int end = i + 1;
                while (end < tasks.size() && tasks[end].level > 0) ++end;
                bool hit = false;
                for (int k = i; k < end; ++k) hit = hit || inRange(tasks[k].due());
                if (!hit) continue;
                if (al.rows.size() + (end - i) > kMaxRows && !al.rows.isEmpty()) {
                    ++al.more;
                    continue;
                }
                for (int k = i; k < end; ++k) {
                    const Task& t = tasks[k];
                    OutlinerView::AlertRow r;
                    r.text = QString(t.text).remove("**").remove('`');
                    r.level = t.level;
                    r.due = t.due();
                    r.at = t.deadlineAt;
                    r.subDone = t.subDone;
                    r.subTotal = t.subTotal;
                    r.path = index_.pages()[t.page].path;
                    r.block = t.block;
                    al.rows << r;
                }
            }
        };
        // What a strip lists: its targets, and the subtasks in its range.
        auto counted = [](const OutlinerView::Alert& al, auto inRange) {
            int roots = al.more, subs = 0;
            for (const auto& r : al.rows) {
                if (r.level == 0) ++roots;
                else if (inRange(r.due)) ++subs;
            }
            return QPair<int, int>(roots, subs);
        };
        // Subtasks are an Angel's cores: each one has to be broken before the target falls.
        auto subText = [eva](int n) {
            return n ? QString("  \xC2\xB7  %1 %2%3").arg(n).arg(eva ? "core" : "subtask").arg(n == 1 ? "" : "s") : QString();
        };
        // Compact (the default): the heading only, "2 targets · 3 cores", counted by each task's
        // own date. Detailed: the targets listed, subtasks under them (Settings > Pattern strips).
        const bool detailed = QSettings().value("patternRows", "compact").toString() == "detailed";
        auto compact = [&](auto inRange) {
            int roots = 0, subs = 0;
            for (const Task& t : tasks)
                if (inRange(t.due())) ++(t.level ? subs : roots);
            return roots ? count(roots) + subText(subs) : subs ? subText(subs).mid(5) : count(0);
        };
        // Pattern Blue: confirmed (due today or overdue), in red. Pattern Orange: not yet
        // confirmed (due in the next three days), in amber. Fixed colours: they mean something.
        auto blue = [today](const QDate& d) { return d.isValid() && d <= today; };
        auto orange = [today](const QDate& d) { return d.isValid() && d > today && d <= today.addDays(3); };
        if (due) {
            OutlinerView::Alert al{eva ? "Pattern Blue" : "Due today", QString(), QColor(0xff, 0x4d, 0x4d), {}, 0};
            if (detailed) {
                rowsFor(al, blue);
                const auto [roots, subs] = counted(al, blue);
                al.detail = count(roots) + subText(subs);
            } else {
                al.detail = compact(blue);
            }
            if (overdue) al.detail += QString("  \xC2\xB7  %1 overdue").arg(overdue);
            alerts << al;
        }
        if (soon && !detailed) {
            alerts << OutlinerView::Alert{eva ? "Pattern Orange" : "Due soon", compact(orange) + "  \xC2\xB7  next 3 days",
                                          QColor(0xff, 0xb0, 0x00), {}, 0};
        } else if (soon) {
            OutlinerView::Alert al{eva ? "Pattern Orange" : "Due soon", QString(), QColor(0xff, 0xb0, 0x00), {}, 0};
            // A target already listed in red is not listed again in amber.
            rowsFor(al, [&](const QDate& d) { return orange(d); });
            QSet<int> shown;
            if (!alerts.isEmpty())
                for (const auto& r : alerts.front().rows) shown.insert(qHash(r.path) ^ r.block);
            QList<OutlinerView::AlertRow> kept;
            bool skip = false;
            for (const auto& r : al.rows) {
                if (r.level == 0) skip = shown.contains(qHash(r.path) ^ r.block);
                if (!skip) kept << r;
            }
            al.rows = kept;
            const auto [roots, subs] = counted(al, orange);
            al.detail = (roots || subs ? count(roots) + subText(subs) : count(soon)) + "  \xC2\xB7  next 3 days";
            alerts << al;
        }
    }
    view_->setAlerts(alerts);

    // Next Episode: on today's journal, the open tasks scheduled or due tomorrow, by page.
    QList<OutlinerView::RefGroup> next;
    if (todayPage) {
        const QDate tomorrow = today.addDays(1);
        for (const Task& t : tasks) {
            if (t.scheduled != tomorrow && t.deadline != tomorrow) continue;
            const GraphIndex::Page& p = index_.pages()[t.page];
            if (next.isEmpty() || next.back().path != p.path) next.append({p.title, p.path, {}, {}, -1, {}});
            next.back().blocks.append({t.block, p.blocks[t.block]});
        }
    }
    const QString label = (Theme::instance().immersion() ? QString("Next episode") : QString("Tomorrow")) + "  \xC2\xB7  " +
                          QLocale::c().toString(today.addDays(1), "ddd d MMM");
    view_->setUpcoming(label, next);
    updateCarryover(tasks, todayPage);
}

// Carryover: on today's journal, the open tasks left in earlier journals (Settings › Tasks: on,
// and how many days back), a group per day. Shown, never copied: today's file is not touched.
void MainWindow::updateCarryover(const QList<Task>& tasks, bool todayPage) {
    QSettings s;
    QList<OutlinerView::RefGroup> groups;
    const bool eva = Theme::instance().immersion();
    if (todayPage && s.value("tasks/carryover", true).toBool()) {
        const QDate today = QDate::currentDate();
        for (const CarriedTask& c : carryoverTasks(tasks, index_, today, s.value("tasks/carryoverDays", 7).toInt(), journalFormat_)) {
            const Task& t = tasks[c.task];
            const GraphIndex::Page& p = index_.pages()[t.page];
            if (groups.isEmpty() || groups.back().path != p.path) {
                OutlinerView::RefGroup g{QLocale::c().toString(c.day, "ddd d MMM"), p.path, {}, {}, -1, {}};
                g.age = c.daysOpen;
                g.note = c.daysOpen == 1 ? QString(eva ? "1 DAY OPEN" : "Yesterday")
                                         : QString(eva ? "%1 DAYS OPEN" : "%1 days ago").arg(c.daysOpen);
                groups.append(g);
            }
            // A progress bar reads as its percentage here; the row is otherwise the block as written.
            QString text = p.blocks[t.block];
            if (Progress pr; findProgress(text, &pr))
                text = text.left(pr.at) + QString("%1%").arg(static_cast<int>(std::floor(pr.ratio() * 100 + 1e-9))) +
                       text.mid(pr.at + pr.length);
            groups.back().blocks.append({t.block, text});
            groups.back().levels.append(t.level);
        }
    }
    view_->setCarryover(eva ? QString("Carryover  \xC2\xB7  unresolved targets") : QString("Carried over"), groups);
}

// A carried task's marker was clicked (or Ctrl+Enter over it): cycle it in its own journal.
void MainWindow::cycleCarried(QString path, int block) {  // by value: the row it came from is rebuilt meanwhile
    const GraphIndex::Page* p = nullptr;
    for (const GraphIndex::Page& q : index_.pages())
        if (q.path == path) p = &q;
    if (!p || block >= p->blocks.size()) return;
    const QString before = p->blocks[block];  // the index's copy dies with the update below
    const QString after = cycleTask(before);
    if (!setBlockText(path, block, before, after)) {
        flash("That task changed on disk; try again");
        return;
    }
    if (taskMarker(after) == "DONE") {
        const bool eva = Theme::instance().immersion();
        flash((eva ? "Carried target neutralised  \xC2\xB7  " : "Done  \xC2\xB7  ") + taskTitle(after));
        if (eva) nerv_->pulse(1400);
    }
}

namespace {
// A task as one short line: its first line without marker, dates and markup.
QString shortTaskText(QString line) {
    static const QRegularExpression noise(R"(\b(SCHEDULED|DEADLINE):\s*<[^>]*>|^\s*#+\s+|\*\*|`)");
    static const QRegularExpression marker(R"(^\s*(TODO|DOING|NOW|LATER|WAITING|WAIT|DONE|CANCELED|CANCELLED)\s+)");
    return line.section('\n', 0, 0).remove(noise).remove(marker).simplified();
}
}  // namespace

// The nearest exam: a block tagged #exam (or tags:: exam) with a SCHEDULED / DEADLINE date
// from today on (its own, else the nearest block above it that has one), and not marked DONE.
// Shown as a countdown in the NERV panel, named with the block it sits under:
// "Quiz #exam" under "Supramolecular Chemistry" is "Quiz · Supramolecular Chemistry".
void MainWindow::updateExam() {
    static const QRegularExpression tag(R"((^|\s)#(exam|\[\[exam\]\])(\s|$|[.,;:!?])|^\s*tags::.*\bexam\b)",
                                        QRegularExpression::CaseInsensitiveOption | QRegularExpression::MultilineOption);
    const QDate today = QDate::currentDate();
    QDate best;
    QString what;
    for (const GraphIndex::Page& p : index_.pages())
        for (int bi = 0; bi < p.blocks.size(); ++bi) {
            const QString& b = p.blocks[bi];
            if (!b.contains("exam", Qt::CaseInsensitive) || !tag.match(b).hasMatch()) continue;
            const QString marker = taskMarker(b);
            if (marker == "DONE" || marker.startsWith("CANCEL")) continue;
            // The blocks it sits under, nearest first.
            QList<int> above;
            for (int up = bi - 1, depth = bi < p.depths.size() ? p.depths[bi] : 0; up >= 0 && depth > 0; --up)
                if (up < p.depths.size() && p.depths[up] < depth) {
                    above << up;
                    depth = p.depths[up];
                }
            QDate d = blockDue(b);
            for (int k = 0; k < above.size() && !d.isValid(); ++k) d = blockDue(p.blocks[above[k]]);
            if (!d.isValid() || d < today || (best.isValid() && d >= best)) continue;
            best = d;
            what = b.section('\n', 0, 0).trimmed();
            if (!above.isEmpty())
                if (const QString parent = shortTaskText(p.blocks[above[0]]); !parent.isEmpty())
                    what += "  \xC2\xB7  " + parent;
        }
    const int days = best.isValid() ? static_cast<int>(today.daysTo(best)) : -1;
    nerv_->setExam(what, days);
    nerv_->setToolTip(what);
    // Target silenced: Ramiel was on the panel for an exam whose day has gone by. The first
    // start after it says so, once, and the Angel is gone.
    QSettings s;
    if (const QDate was = QDate::fromString(s.value("ramiel/date").toString(), Qt::ISODate); was.isValid() && was < today) {
        const QString name = s.value("ramiel/exam").toString();
        s.remove("ramiel/exam");
        s.remove("ramiel/date");
        if (Theme::instance().immersion())
            QTimer::singleShot(700, this, [this, name] { new Eyecatch(centralWidget(), "Target silenced", name); });
    }
    if (nerv_->ramiel()) {  // the next exam it is waiting for
        s.setValue("ramiel/exam", nerv_->examName());
        s.setValue("ramiel/date", best.toString(Qt::ISODate));
    }
}

// Ramiel's moments, all novelty and all once at most: a beam when a deadline goes by undone
// (once a day), and now and then a fly-by when the day's last open task is done.
void MainWindow::updateRamiel(const QList<Task>& tasks) {
    const QDateTime now = QDateTime::currentDateTime();
    const QDate today = now.date();
    QSettings s;
    QStringList passed, fresh;
    const QStringList known = s.value("ramiel/passed").toStringList();
    const bool seeded = s.contains("ramiel/passed");
    QString firstText;
    int open = 0;
    for (const Task& t : tasks) {
        const QDate due = t.due();
        if (due.isValid() && due <= today) ++open;
        const bool gone = t.deadlineAt.isValid() ? t.deadlineAt < now : t.deadline.isValid() && t.deadline < today;
        if (!gone) continue;
        const QString key = index_.pages()[t.page].path + "|" + t.deadline.toString(Qt::ISODate) + "|" + t.text;
        passed << key;
        if (!known.contains(key)) {
            fresh << key;
            if (firstText.isEmpty()) firstText = shortTaskText(t.text);
        }
    }
    if (passed != known) s.setValue("ramiel/passed", passed);  // done or removed ones drop out
    // The first run only learns what has already passed.
    if (seeded && !fresh.isEmpty()) {
        flash((Theme::instance().immersion() ? "Deadline passed  \xC2\xB7  " : "Deadline passed: ") + firstText);
        if (Theme::instance().immersion() && s.value("ramiel/beamDay").toDate() != today) {
            s.setValue("ramiel/beamDay", today);
            nerv_->fireBeam();
        }
    }
    const int before = lastOpenToday_;
    lastOpenToday_ = open;
    if (before > 0 && open == 0 && Theme::instance().immersion() && s.value("ramiel/flyDay").toDate() != today) {
        s.setValue("ramiel/flyDay", today);  // one roll a day
        const bool always = qEnvironmentVariable("GRAPHITE_FLYBY") == "always";
        if (always || QRandomGenerator::global()->bounded(3) == 0) nerv_->flyBy();
    }
}

// Today's timetable entries, less the ones already written as a time block in today's journal
// (same start, same text): those count once, as the journal's.
QList<TimetableEntry> MainWindow::todayTimetable() const {
    const QDate today = QDate::currentDate();
    QList<TimetableEntry> list = timetableFor(index_, today);
    if (list.isEmpty()) return list;
    const QString path = graph_ + "/journals/" + today.toString(journalFormat_) + ".md";
    QSet<QString> written;
    for (const GraphIndex::Page& p : index_.pages()) {
        if (!p.journal || p.path != path) continue;
        for (const QString& b : p.blocks) {
            TimeSlot slot;
            if (!blockTime(b, &slot)) continue;
            const QString line = b.section('\n', 0, 0).remove(slot.at, slot.length);
            written.insert(QString::number(slot.start) + "|" + shortTaskText(line).toLower());
        }
        break;
    }
    QList<TimetableEntry> out;
    for (const TimetableEntry& e : list)
        if (!shortTaskText(e.text).isEmpty() && !written.contains(QString::number(e.start) + "|" + shortTaskText(e.text).toLower()))
            out << e;
    return out;
}

int MainWindow::supplyEssentials(bool restore) {
    if (graph_.isEmpty() || !QFileInfo(graph_).isDir()) return 0;
    QSettings s;
    const QString key = "essentials/" + QString::number(qHash(graph_));
    QStringList supplied = s.value(key).toStringList();
    const QString dir = graph_ + "/" + essentialsBook();
    int written = 0;
    for (const EssentialPage& ep : essentialPages()) {
        const QString path = dir + "/" + ep.title + ".md";
        if (QFile old(path); !ep.previous.isEmpty() && old.open(QIODevice::ReadOnly)) {
            // Untouched since an earlier Graphite wrote it: the current text replaces it.
            const QByteArray sum = QCryptographicHash::hash(old.readAll(), QCryptographicHash::Md5).toHex();
            old.close();
            if (ep.previous.contains(QString::fromLatin1(sum))) {
                QSaveFile f(path);
                if (f.open(QIODevice::WriteOnly)) {
                    f.write(ep.text.toUtf8());
                    if (f.commit()) ++written;
                }
            }
        }
        if ((supplied.contains(ep.title) && !restore) || QFileInfo::exists(path) || ep.text.isEmpty()) {
            if (!supplied.contains(ep.title)) supplied << ep.title;
            continue;
        }
        if (!supplied.contains(ep.title)) supplied << ep.title;
        // One timetable per tree: a tree that has its own already keeps it.
        if (ep.title == "Timetable" && timetablePage(index_) >= 0) continue;
        QDir().mkpath(dir);
        QSaveFile f(path);
        if (!f.open(QIODevice::WriteOnly)) continue;
        f.write(ep.text.toUtf8());
        if (f.commit()) ++written;
    }
    s.setValue(key, supplied);
    return written;
}

// Ctrl+Shift+D: the week's timetable page; the first time, a new one with a block per day.
void MainWindow::openTimetable() {
    closeOverlay();
    syncIndex();
    if (const int at = timetablePage(index_); at >= 0) {
        openPath(index_.pages()[at].path);
        return;
    }
    QString path = graph_ + "/" + essentialsBook() + "/Timetable.md";
    if (QFileInfo::exists(path)) {  // a "Timetable" page outside the index (should not happen)
        openPath(path);
        return;
    }
    QDir().mkpath(QFileInfo(path).absolutePath());
    if (!openPath(path, true) || !page_ || page_->path != path) return;
    page_->doc = Document::parse(timetableTemplate().toStdString());
    page_->doc.ensureNonEmpty();
    view_->setDocument(&page_->doc);
    scheduleSave();
    view_->focusBlock(2);  // Monday's first slot, after "09:00-10:00 "
    flash("Timetable: lines like 09:00-10:00 Organic lecture under each day");
}

// The day's time blocks ("09:00 TODO problem set" in today's journal, and the timetable's
// entries for today): the one running, or the next, in the NERV panel.
void MainWindow::updateNext() {
    const QString path = graph_ + "/journals/" + QDate::currentDate().toString(journalFormat_) + ".md";
    const int minute = QTime::currentTime().msecsSinceStartOfDay() / 60000;
    QString head, text;
    int bestStart = 24 * 60;
    auto consider = [&](const TimeSlot& slot, const QString& line) {
        if (bestStart < 0) return;  // something is running: it stays
        if (slot.end > 0 && slot.start <= minute && minute < slot.end) {  // running now
            head = "Now  \xC2\xB7  \u2013" + slotText({slot.end, -1});
            text = shortTaskText(line);
            bestStart = -1;
        } else if (slot.start > minute && slot.start < bestStart) {
            bestStart = slot.start;
            head = "Next  \xC2\xB7  " + slotText({slot.start, -1});
            text = shortTaskText(line);
        }
    };
    for (const GraphIndex::Page& p : index_.pages()) {
        if (!p.journal || p.path != path) continue;
        for (const QString& b : p.blocks) {
            TimeSlot slot;
            if (!blockTime(b, &slot) || slot.done) continue;
            QString line = b.section('\n', 0, 0);
            line.remove(slot.at, slot.length);
            consider(slot, line);
        }
        break;
    }
    for (const TimetableEntry& e : todayTimetable()) consider({e.start, e.end}, e.text);
    nerv_->setNext(head, text);
}

// Ctrl+D: today's time blocks (today's journal, and timed tasks scheduled today anywhere) and
// today's deadlines with a time, in order.
void MainWindow::showTimeline() {
    if (qobject_cast<TimelinePanel*>(overlay_.data())) {
        closeOverlay();
        return;
    }
    closeOverlay();
    if (needsSave()) saveNow();
    syncIndex();
    using Row = TimelinePanel::Row;
    auto fetch = [this] {
        const QDate today = QDate::currentDate();
        const QString journal = graph_ + "/journals/" + today.toString(journalFormat_) + ".md";
        QList<Row> rows;
        for (const GraphIndex::Page& p : index_.pages()) {
            const bool isToday = p.journal && p.path == journal;
            for (int bi = 0; bi < p.blocks.size(); ++bi) {
                const QString& b = p.blocks[bi];
                if (b.size() < 4) continue;
                const QString marker = taskMarker(b);
                const bool done = marker == "DONE" || marker.startsWith("CANCEL");
                TimeSlot slot;
                const bool timed = blockTime(b, &slot);
                Row r;
                QString line = b.section('\n', 0, 0);
                if (timed && (isToday || (!marker.isEmpty() && blockDue(b) == today))) {
                    r.start = slot.start;
                    r.end = slot.end;
                    line.remove(slot.at, slot.length);
                } else if (const QDateTime at = blockDeadlineTime(b); at.date() == today && !marker.isEmpty()) {
                    r.start = at.time().hour() * 60 + at.time().minute();
                    r.deadline = true;
                } else {
                    continue;
                }
                r.marker = marker;
                r.done = done || slot.done;
                r.text = shortTaskText(line);
                r.path = p.path;
                r.block = bi;
                r.blockText = b;
                // The block it sits under, if any (e.g. the course heading).
                const int depth = bi < p.depths.size() ? p.depths[bi] : 0;
                for (int up = bi - 1; up >= 0 && depth > 0; --up)
                    if (up < p.depths.size() && p.depths[up] < depth) {
                        r.context = shortTaskText(p.blocks[up]);
                        break;
                    }
                if (r.context.isEmpty() && !isToday) r.context = p.title;
                rows << r;
            }
        }
        for (const TimetableEntry& e : todayTimetable()) {
            const GraphIndex::Page& p = index_.pages()[e.page];
            Row r;
            r.start = e.start;
            r.end = e.end;
            r.text = shortTaskText(e.text);
            r.path = p.path;
            r.block = e.block;
            r.blockText = p.blocks[e.block];
            r.weekly = true;
            r.context = "Timetable";
            rows << r;
        }
        std::stable_sort(rows.begin(), rows.end(), [](const Row& a, const Row& b) {
            return a.start != b.start ? a.start < b.start : a.end < b.end;
        });
        return rows;
    };
    auto complete = [this](const Row& r) {
        return !r.weekly && setBlockText(r.path, r.block, r.blockText, completeTask(r.blockText));  // the timetable repeats
    };
    const QString title = (Theme::instance().immersion() ? QString("Operation schedule") : QString("Timeline")) + "  \xC2\xB7  " +
                          QLocale::c().toString(QDate::currentDate(), "ddd d MMM");
    auto* panel = new TimelinePanel(centralWidget(), title, fetch, complete);
    connect(panel, &TimelinePanel::open, this, [this](const QString& path, int block) {
        if (openPath(path) && block >= 0) view_->focusBlock(block);
    });
    overlay_ = panel;
}

// No polling: one coarse single shot at the next moment something changes (a timed deadline
// going by, a time block starting or ending, midnight), which runs updatePatternBlue again.
void MainWindow::scheduleDeadlineWake(const QList<Task>& tasks) {
    const QDateTime now = QDateTime::currentDateTime();
    QDateTime next(now.date().addDays(1), QTime(0, 0, 5));
    for (const Task& t : tasks)
        if (t.deadlineAt.isValid() && t.deadlineAt > now && t.deadlineAt < next) next = t.deadlineAt.addSecs(1);
    const QString path = graph_ + "/journals/" + now.date().toString(journalFormat_) + ".md";
    for (const GraphIndex::Page& p : index_.pages()) {
        if (!p.journal || p.path != path) continue;
        for (const QString& b : p.blocks) {
            TimeSlot slot;
            if (!blockTime(b, &slot) || slot.done) continue;
            for (const int m : {slot.start, slot.end}) {
                if (m < 0) continue;
                const QDateTime at(now.date(), QTime(m / 60, m % 60, 1));
                if (at > now && at < next) next = at;
            }
        }
        break;
    }
    for (const TimetableEntry& e : todayTimetable())
        for (const int m : {e.start, e.end}) {
            if (m < 0) continue;
            const QDateTime at(now.date(), QTime(m / 60, m % 60, 1));
            if (at > now && at < next) next = at;
        }
    deadlineWake_.start(static_cast<int>(qMax<qint64>(1000, now.msecsTo(next))));
}

void MainWindow::showTasks() {
    if (qobject_cast<TasksPanel*>(overlay_.data())) {
        closeOverlay();
        return;
    }
    closeOverlay();
    if (needsSave()) saveNow();
    syncIndex();  // pick up tasks written in Logseq meanwhile
    using Row = TasksPanel::Row;
    auto fetch = [this] {
        QList<Row> rows;
        const QDate today = QDate::currentDate();
        QString section;
        const QList<Task> tasks = collectTasks(index_, journalFormat_);
        for (int i = 0; i < tasks.size(); ++i) {
            const Task& t = tasks[i];
            const QDate due = t.due();
            const QDate group = t.groupDue;  // a target's section counts its subtasks' dates
            const QString s = !group.isValid() ? "No date" : group < today ? "Overdue" : group == today ? "Today" : "Upcoming";
            if (t.level == 0 && s != section) {
                section = s;
                Row h;
                h.section = s;
                rows << h;
            }
            const GraphIndex::Page& p = index_.pages()[t.page];
            Row r;
            r.marker = t.marker;
            r.text = t.text;
            r.text.remove("**").remove('`');
            r.page = p.title;
            r.path = p.path;
            r.block = t.block;
            r.blockText = p.blocks[t.block];
            r.due = due;
            r.at = t.deadlineAt;
            r.late = due.isValid() && due <= today;
            r.level = t.level;
            r.lastSibling = true;
            for (int k = i + 1; k < tasks.size() && tasks[k].level >= t.level && t.level > 0; ++k)
                if (tasks[k].level == t.level) {
                    r.lastSibling = false;
                    break;
                }
            r.subDone = t.subDone;
            r.subTotal = t.subTotal;
            rows << r;
        }
        return rows;
    };
    auto complete = [this](const Row& r) { return setBlockText(r.path, r.block, r.blockText, completeTask(r.blockText)); };
    auto* panel = new TasksPanel(centralWidget(), fetch, complete);
    panel->setStep([this](const Row& r, int steps) {
        const TaskSyncOptions o = taskOptions();
        const QString after = stepProgress(r.blockText, steps, o.finish, o.start);
        if (after == r.blockText) {
            flash(findProgress(after, nullptr) ? "That bar is already at its end" : "No progress bar on this task: /progress adds one");
            return false;
        }
        if (!setBlockText(r.path, r.block, r.blockText, after)) return false;
        flash(progressMessage(after, Theme::instance().immersion()));
        return true;
    });
    connect(panel, &TasksPanel::open, this, [this](const QString& path, int block) {
        if (openPath(path) && block >= 0) view_->focusBlock(block);
    });
    overlay_ = panel;
}

namespace {
QStringList titlesOf(const Document& doc, const QList<int>& blocks) {
    QStringList out;
    for (int b : blocks) out << taskTitle(QString::fromStdString(doc.at(b).text));
    return out;
}
}  // namespace

bool MainWindow::setBlockText(const QString& path, int block, const QString& expected, const QString& text) {
    // The open page: change it in memory and let the normal autosave write it.
    if (page_ && page_->path == path) {
        if (block >= page_->doc.size() || QString::fromStdString(page_->doc.at(block).text) != expected) return false;
        page_->doc.setText(block, text.toStdString());
        QList<int> finished;
        syncTasks(page_->doc, {block}, taskOptions(), &finished);
        view_->reloadBlocks();
        announceFinished(titlesOf(page_->doc, finished));
        scheduleSave();
        updateStatus();
        index_.update(page_->path, page_->title, page_->journal, page_->doc, page_->mtime);
        return true;
    }
    // Any other page: read it, change that one block (the rest stays byte for byte), write it.
    QFile in(path);
    if (!in.open(QIODevice::ReadOnly)) return false;
    Document doc = Document::parse(in.readAll().toStdString());
    in.close();
    if (block >= doc.size() || QString::fromStdString(doc.at(block).text) != expected) {
        syncIndex();  // the file changed since the index read it
        return false;
    }
    doc.setText(block, text.toStdString());
    QList<int> finished;
    syncTasks(doc, {block}, taskOptions(), &finished);
    backupFile(path);
    const std::string out = doc.serialize();
    QSaveFile f(path);
    const qint64 len = static_cast<qint64>(out.size());
    if (!f.open(QIODevice::WriteOnly) || f.write(out.data(), len) != len || !f.commit()) return false;
    bool journal = false;
    const QString title = titleFromPath(path, &journal);
    index_.update(path, title, journal, doc, mtimeOf(path));
    showReferences();
    nerv_->pulse(1400);
    updatePatternBlue();
    announceFinished(titlesOf(doc, finished));
    return true;
}

TaskSyncOptions MainWindow::taskOptions() const {
    QSettings s;
    TaskSyncOptions o;
    o.finish = s.value("tasks/finish", true).toBool();
    o.start = s.value("tasks/start", true).toBool();
    o.fold = s.value("tasks/fold", true).toBool();
    return o;
}

void MainWindow::applyTaskOptions() { view_->setTaskOptions(taskOptions(), QSettings().value("tasks/progress", true).toBool()); }

void MainWindow::announceFinished(const QStringList& names) {
    if (names.isEmpty()) return;
    const bool eva = Theme::instance().immersion();
    flash((eva ? "Target neutralised  \xC2\xB7  " : "Done  \xC2\xB7  ") + names.join(", "));
    if (eva) nerv_->pulse(1400);
}

void MainWindow::quickSwitch() {
    if (qobject_cast<QuickSwitcher*>(overlay_.data())) {
        closeOverlay();
        return;
    }
    closeOverlay();
    if (needsSave()) saveNow();  // so what was just typed is searchable
    using Result = QuickSwitcher::Result;
    auto search = [this](const QString& query) {
        QList<Result> out;
        const QVector<GraphIndex::Page>& pages = index_.pages();
        const QString q = query.simplified();
        if (q.isEmpty()) {  // nothing typed yet: the newest journals, then pages
            for (int i = 0; i < pages.size() && out.size() < 12; ++i)
                out << Result{Result::Page, pages[i].title, QString(), pages[i].path, -1};
            return out;
        }
        bool exact = false;
        int pageHits = 0;
        for (const GraphIndex::Hit& h : index_.search(q)) {
            const GraphIndex::Page& p = pages[h.page];
            if (h.block < 0) {
                exact = exact || p.title.compare(q, Qt::CaseInsensitive) == 0;
                out.insert(pageHits++, Result{Result::Page, p.title, QString(), p.path, -1});
                continue;
            }
            // One line of the block, starting a little before the first word that matched.
            // Shown as plain text: emphasis and code markers dropped, links keep their brackets.
            QString text = p.blocks[h.block].simplified();
            text.remove("**").remove("__").remove('`');
            const int at = qMax(0, static_cast<int>(text.indexOf(q.section(' ', 0, 0), 0, Qt::CaseInsensitive)) - 30);
            const QString snippet = (at > 0 ? QString("\xE2\x80\xA6") : QString()) + text.mid(at, 160);
            out << Result{Result::Block, snippet, p.title, p.path, h.block};
        }
        // Last, so Enter opens the best match; first only when nothing matched.
        if (!exact && !pathByTitle_.contains(q.toLower()))
            out.append(Result{Result::Create, QString("Create page \xE2\x80\x9C%1\xE2\x80\x9D").arg(q), q,
                                        QString(), -1});
        return out;
    };
    auto* sw = new QuickSwitcher(centralWidget(), search);
    connect(sw, &QuickSwitcher::chosen, this, [this](const Result& r) {
        if (r.kind == Result::Create) {
            openLink(r.secondary);
            if (view_->document()) view_->focusBlock(view_->document()->size() - 1);
        } else if (openPath(r.path) && r.block >= 0) {
            view_->focusBlock(r.block);
        }
    });
    overlay_ = sw;
}

QList<GraphIndex::File> MainWindow::graphFiles() const {
    QList<GraphIndex::File> files;
    QDir journals(graph_ + "/journals");
    for (const QString& f : journals.entryList({"*.md"}, QDir::Files, QDir::Name | QDir::Reversed)) {
        GraphIndex::File gf;
        gf.path = journals.absoluteFilePath(f);
        gf.title = titleFromPath(gf.path, &gf.journal);
        files << gf;
    }
    for (const BookInfo& b : books()) {
        QDir dir(b.dir);
        for (const QString& f : dir.entryList({"*.md"}, QDir::Files, QDir::Name | QDir::IgnoreCase)) {
            GraphIndex::File gf;
            gf.path = dir.absoluteFilePath(f);
            gf.title = titleFromPath(gf.path, &gf.journal);
            files << gf;
        }
    }
    return files;
}

// ---------------------------------------------------------------- trees and books

// A book is any folder at the top of the tree, except the tree's own machinery.
bool MainWindow::isBookDir(const QString& dir) const {
    const QFileInfo fi(dir);
    if (!fi.isDir() || fi.absolutePath() != QFileInfo(graph_).absoluteFilePath()) return false;
    static const QStringList reserved = {"journals", "logseq", "assets", "whiteboards", "draws", "bak"};
    return !fi.fileName().startsWith('.') && !reserved.contains(fi.fileName());
}

QString MainWindow::bookName(const QString& dir) const {
    const QString n = QFileInfo(dir).fileName();
    return n == "pages" ? QString("Unsorted") : n;
}

QList<MainWindow::BookInfo> MainWindow::books(bool withoutPages) const {
    QList<BookInfo> out;
    QDir root(graph_);
    for (const QString& d : root.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name | QDir::IgnoreCase)) {
        const QString dir = root.absoluteFilePath(d);
        if (!isBookDir(dir)) continue;
        // A folder of other things (PDFs, slides) is not a book until a page goes in;
        // an empty one is (a new book).
        const QDir bd(dir);
        if (!withoutPages && bd.entryList(QStringList{"*.md"}, QDir::Files).isEmpty() &&
            !bd.entryList(QDir::AllEntries | QDir::NoDotAndDotDot).isEmpty())
            continue;
        out.append({bookName(dir), dir});
    }
    return out;
}

QString MainWindow::defaultBookDir() const {
    if (!newPageIn_.isEmpty() && isBookDir(newPageIn_)) return newPageIn_;
    if (page_ && !page_->journal && isBookDir(QFileInfo(page_->path).absolutePath())) return QFileInfo(page_->path).absolutePath();
    const QString sel = nav_ ? nav_->currentBookDir() : QString();
    if (!sel.isEmpty()) return sel;
    if (QFileInfo(graph_ + "/pages").isDir()) return graph_ + "/pages";
    return graph_ + "/Unsorted";  // made on first use
}

QString MainWindow::treeName() const {
    QFile f(graph_ + "/.graphite/tree.json");
    if (f.open(QIODevice::ReadOnly)) {
        const QString n = QJsonDocument::fromJson(f.readAll()).object().value("name").toString();
        if (!n.isEmpty()) return n;
    }
    return QDir(graph_).dirName();
}

void MainWindow::setTree(const QString& dir) {
    if (dir.isEmpty() || dir == graph_ || !saveNow()) return;
    if (page_) QSettings().setValue("lastPage/" + QString::number(qHash(graph_)), page_->path);
    view_->setDocument(nullptr);
    page_.reset();
    graph_ = QDir(dir).absolutePath();
    journalFormat_ = readJournalFormat(graph_);
    backedUp_.clear();
    index_ = GraphIndex();
    const QStringList f = QSettings().value("folded/" + QString::number(qHash(graph_))).toStringList();
    nav_->setFolded(QSet<QString>(f.begin(), f.end()));
    graphLabel_->setText(treeName() + "  \xE2\x96\xBE");
    scanGraph();
    if (supplyEssentials() > 0) scanGraph();
    const QString last = QSettings().value("lastPage/" + QString::number(qHash(graph_))).toString();
    if (!last.isEmpty() && QFileInfo::exists(last)) openPath(last);
    else showToday();
    QSettings s;
    s.setValue("graph", graph_);
    QStringList trees = s.value("trees").toStringList();
    trees.removeAll(graph_);
    trees.prepend(graph_);
    s.setValue("trees", trees.mid(0, 8));
    flash("Tree: " + treeName());
}

void MainWindow::openTreeDialog() {
    const QString dir = QFileDialog::getExistingDirectory(this, "Open a tree, or a new empty folder for one", QFileInfo(graph_).absolutePath());
    if (!dir.isEmpty()) openTree(dir);
}

bool MainWindow::openTree(const QString& dir) {
    if (!QFileInfo(dir).isDir()) return false;
    // A folder that is not a tree yet becomes one: a marker, and a journals folder.
    if (!QFileInfo(dir + "/journals").isDir() && !QFileInfo(dir + "/.graphite/tree.json").exists()) {
        QDir().mkpath(dir + "/.graphite");
        QDir().mkpath(dir + "/journals");
        QSaveFile f(dir + "/.graphite/tree.json");
        if (f.open(QIODevice::WriteOnly)) {
            f.write(QJsonDocument(QJsonObject{{"name", QDir(dir).dirName()}, {"version", 1}}).toJson());
            f.commit();
        }
    }
    setTree(dir);
    return QDir(graph_) == QDir(dir);
}

void MainWindow::showTreeMenu() {
    QMenu menu(this);
    const Theme& th = Theme::instance();
    auto css = [](const QColor& c) { return QString("rgba(%1,%2,%3,%4)").arg(c.red()).arg(c.green()).arg(c.blue()).arg(c.alpha()); };
    QColor panel = th.palette().panel;
    panel.setAlpha(255);
    menu.setStyleSheet(QString("QMenu { background: %1; color: %2; border: 1px solid %3; padding: 4px; }"
                               "QMenu::item { padding: 5px 18px; border-radius: 3px; }"
                               "QMenu::item:selected { background: %4; color: %5; }"
                               "QMenu::separator { height: 1px; background: %3; margin: 4px 8px; }")
                           .arg(css(panel), css(th.dim()), css(th.line()), css(QColor(th.structure().red(), th.structure().green(),
                                                                                        th.structure().blue(), 60)), css(th.text())));
    for (const QString& t : QSettings().value("trees").toStringList()) {
        if (!QFileInfo(t).isDir()) continue;
        QAction* a = menu.addAction(QDir(t).dirName() + "    " + QString(t).replace(QDir::homePath(), "~"), this, [this, t] { setTree(t); });
        if (t == graph_) a->setEnabled(false);
    }
    menu.addSeparator();
    menu.addAction("Open or start a tree\u2026", this, [this] { openTreeDialog(); });
    menu.exec(graphLabel_->mapToGlobal(QPoint(0, graphLabel_->height())));
}

// Rename a page (links to it follow, in every page of the tree) or a book.
void MainWindow::renameEntry(QString path, QString rawName) {
    QString name = rawName.trimmed();
    name.replace('/', '-').replace('\\', '-');
    if (name.isEmpty()) return;
    const QFileInfo fi(path);
    if (fi.isDir()) {
        const QString to = graph_ + "/" + name;
        if (QFileInfo::exists(to)) {
            flash("There is already a book called " + name);
            scanGraph();
            return;
        }
        const bool inside = page_ && page_->path.startsWith(path + "/");
        if (inside && !saveNow()) return;
        const QString openFile = inside ? QFileInfo(page_->path).fileName() : QString();
        if (!QDir().rename(path, to)) {
            flash("Could not rename the book");
            scanGraph();
            return;
        }
        rewriteLinks(QFileInfo(path).fileName() + "/", name + "/", QString());  // [[Book/Page]] links
        scanGraph();
        if (inside) loadPage(to + "/" + openFile, false);
        flash("Book renamed: " + name);
        return;
    }
    bool journal = false;
    const QString oldTitle = titleFromPath(path, &journal);
    if (journal) return;
    const QString to = fi.absolutePath() + "/" + name + ".md";
    if (QFileInfo::exists(to)) {
        flash("There is already a page called " + name + " in this book");
        scanGraph();
        return;
    }
    const bool isOpen = page_ && page_->path == path;
    if (isOpen && !saveNow()) return;
    if (!QFile::rename(path, to)) {
        flash("Could not rename the page");
        scanGraph();
        return;
    }
    backedUp_.remove(path);
    const int changed = rewriteLinks(oldTitle, name, to);
    scanGraph();
    if (isOpen) loadPage(to, false);
    flash(changed ? QString("Renamed  \xC2\xB7  links updated on %1 page%2").arg(changed).arg(changed == 1 ? "" : "s")
                  : QString("Renamed"));
}

// [[Old]], [[Book/Old]], #[[Old]] and #Old become the new name, in every page but `skipPath`.
// `oldTitle` ending in "/" renames the book part of qualified links instead.
int MainWindow::rewriteLinks(const QString& oldTitle, const QString& newTitle, const QString& skipPath) {
    const bool bookPart = oldTitle.endsWith('/');
    const QString old = QRegularExpression::escape(bookPart ? oldTitle.chopped(1) : oldTitle);
    QList<QPair<QRegularExpression, QString>> rules;
    if (bookPart) {
        rules.append({QRegularExpression("\\[\\[" + old + "/", QRegularExpression::CaseInsensitiveOption),
                      "[[" + newTitle});
    } else {
        rules.append({QRegularExpression("\\[\\[((?:[^\\]/\\n]+/)?)" + old + "\\]\\]", QRegularExpression::CaseInsensitiveOption),
                      "[[\\1" + newTitle + "]]"});
        if (!oldTitle.contains(' '))
            rules.append({QRegularExpression("(^|\\s)#" + old + "(?=$|[\\s,.;:!?])", QRegularExpression::CaseInsensitiveOption),
                          "\\1#" + (newTitle.contains(' ') ? "[[" + newTitle + "]]" : newTitle)});
    }
    auto apply = [&](QString t) {
        for (const auto& [re, to] : rules) t.replace(re, to);
        return t;
    };
    int pages = 0;
    for (const GraphIndex::Page& p : index_.pages()) {
        if (p.path == skipPath) continue;
        bool any = false;
        for (const QString& b : p.blocks) any = any || apply(b) != b;
        if (!any) continue;
        if (page_ && page_->path == p.path) {  // the open page: in memory, saved as usual
            for (int i = 0; i < page_->doc.size(); ++i) {
                const QString t = QString::fromStdString(page_->doc.at(i).text);
                const QString n = apply(t);
                if (n != t) page_->doc.setText(i, n.toStdString());
            }
            view_->reloadBlocks();
            scheduleSave();
            ++pages;
            continue;
        }
        QFile in(p.path);
        if (!in.open(QIODevice::ReadOnly)) continue;
        Document doc = Document::parse(in.readAll().toStdString());
        in.close();
        for (int i = 0; i < doc.size(); ++i) {
            const QString t = QString::fromStdString(doc.at(i).text);
            const QString n = apply(t);
            if (n != t) doc.setText(i, n.toStdString());  // only these blocks are rewritten
        }
        backupFile(p.path);
        const std::string out = doc.serialize();
        QSaveFile f(p.path);
        const qint64 len = static_cast<qint64>(out.size());
        if (f.open(QIODevice::WriteOnly) && f.write(out.data(), len) == len && f.commit()) ++pages;
    }
    return pages;
}

void MainWindow::movePage(QString path, QString bookDir) {
    const QFileInfo fi(path);
    if (!isBookDir(bookDir) || fi.absolutePath() == bookDir || !fi.exists()) return;
    const QString to = bookDir + "/" + fi.fileName();
    if (QFileInfo::exists(to)) {
        flash("There is already a page called " + fi.completeBaseName() + " in " + bookName(bookDir));
        return;
    }
    const bool isOpen = page_ && page_->path == path;
    if (isOpen && !saveNow()) return;
    if (!QFile::rename(path, to)) {
        flash("Could not move the page");
        return;
    }
    backedUp_.remove(path);
    scanGraph();
    if (isOpen) loadPage(to, false);
    flash("Moved to " + bookName(bookDir));
}

void MainWindow::newBook() {
    closeOverlay();
    auto* ask = new NamePrompt(centralWidget(), "New book", "Book name, e.g. Organic Chemistry");
    connect(ask, &NamePrompt::accepted, this, [this](const QString& raw) {
        QString name = raw;
        name.replace('/', '-').replace('\\', '-');
        const QString dir = graph_ + "/" + name;
        static const QStringList reserved = {"journals", "logseq", "assets", "whiteboards", "draws", "bak"};
        if (QFileInfo::exists(dir)) {
            flash("There is already a book called " + name);
            return;
        }
        if (name.startsWith('.') || reserved.contains(name.toLower())) {
            flash("\u201C" + name + "\u201D is a name the tree uses itself; pick another");
            return;
        }
        QDir().mkpath(dir);
        scanGraph();
        flash("Book: " + name + "  \xC2\xB7  right-click it for a new page");
    });
    overlay_ = ask;
}

void MainWindow::syncIndex() {
    index_.sync(graphFiles());
    // The open page may have edits not on disk yet: the index should see those, not the file.
    if (page_ && !page_->isNew && needsSave()) index_.update(page_->path, page_->title, page_->journal, page_->doc, page_->mtime);
    showReferences();
    if (page_) updatePatternBlue();
}

void MainWindow::showReferences() {
    if (!page_) return;
    constexpr int kMaxShown = 200;  // a huge tag page stays cheap: the rest is only counted
    const QList<GraphIndex::Reference> refs = index_.backlinks(page_->title);
    QList<OutlinerView::RefGroup> groups;
    int shown = 0;
    for (const GraphIndex::Reference& r : refs) {
        if (shown == kMaxShown) break;
        const GraphIndex::Page& p = index_.pages()[r.page];
        if (groups.isEmpty() || groups.back().path != p.path) groups.append({p.title, p.path, {}, {}, -1, {}});
        groups.back().blocks.append({r.block, p.blocks[r.block]});
        ++shown;
    }
    if (groups == lastRefs_ && refs.size() == view_->referenceCount()) return;  // nothing new: keep the rendered rows
    lastRefs_ = groups;
    view_->setReferences(groups, static_cast<int>(refs.size()));
}

void MainWindow::chooseFonts() {
    closeOverlay();
    auto* picker = new FontPicker(centralWidget(), page_ ? page_->title : QString());
    connect(picker, &FontPicker::chosen, this, [](const QString& title, const QString& body) {
        QSettings s;
        s.setValue("titleFont", title);
        s.setValue("bodyFont", body);
    });
    overlay_ = picker;
}

void MainWindow::toggleFocusMode() {
    sidebar_->setVisible(!sidebar_->isVisible());
    if (sidebar_->isVisible()) {
        flashUntil_ = QDateTime();
        updateStatus();
    } else {
        flash("Focus mode  \xC2\xB7  Ctrl+\\ to leave");
    }
    update();  // the sidebar's panel colour is painted by the window
}

QString MainWindow::currentUnit() const {
    const QString saved = QSettings().value("palette").toString();
    return saved == "builtin" ? QString("unit01") : unitIds().contains(saved) ? saved : QString();
}

// "EPISODE:N" on title cards: the page's place among the graph's pages (journals not counted).
int MainWindow::episodeNumber() const {
    int n = 0;
    for (const GraphIndex::Page& p : index_.pages()) n += p.journal ? 0 : 1;
    return n + 1;
}

void MainWindow::setTextSize(qreal pt) {
    Theme::instance().setTextSize(pt);
    QSettings().setValue("textSize", Theme::instance().textSize());
    flash(QString("Text %1 pt").arg(Theme::instance().textSize(), 0, 'f', 1));
}

void MainWindow::showSettings() {
    if (stack_->currentWidget() == settings_) {  // Ctrl+, again: back to the page
        closeSettings();
        return;
    }
    closeOverlay();
    view_->endEdit();
    using Row = SettingRow;
    QList<Row> rows;
    auto heading = [&](const QString& name) {
        Row h;
        h.kind = Row::Heading;
        h.name = name;
        rows << h;
    };
    const QStringList unitOrder = {QString(), "unit01", "unit00", "unit02"};

    heading("Appearance");
    Row colours;
    colours.name = "Colours";
    colours.choices = {"Wallpaper", "Unit-01", "Unit-00", "Unit-02"};
    colours.get = [this, unitOrder] { return qMax(0, static_cast<int>(unitOrder.indexOf(currentUnit()))); };
    colours.set = [this, unitOrder](int v) { emit paletteRequested(unitOrder.value(v)); };
    colours.note = "Wallpaper follows ML4W / matugen; the units are fixed  \xC2\xB7  Ctrl+P cycles";
    rows << colours;

    Row clear;
    clear.name = "Transparent";
    clear.choices = {"Off", "On"};
    clear.get = [] { return Theme::instance().transparent() ? 1 : 0; };
    clear.set = [](int v) { Theme::instance().setTransparent(v == 1); };
    clear.note = "Hyprland blurs what is behind the window  \xC2\xB7  Ctrl+T";
    rows << clear;

    Row blur;
    blur.name = "Blur";
    blur.kind = Row::Slider;
    blur.steps = BlurControl::kMaxLevel;
    blur.get = [this] { return blur_->level(); };
    blur.set = [this](int v) { blur_->setLevel(v); };
    blur.label = [](int v) { return QString("Level %1").arg(v); };
    blur.note = blur_->available() ? "While transparent; your own blur comes back after  \xC2\xB7  Ctrl+[ / Ctrl+]"
                                   : "Needs Hyprland, in transparent mode";
    rows << blur;

    Row size;
    size.name = "Text size";
    size.kind = Row::Slider;
    size.steps = static_cast<int>((Theme::kMaxTextSize - Theme::kMinTextSize) * 2);  // half points
    size.get = [] { return static_cast<int>(std::lround((Theme::instance().textSize() - Theme::kMinTextSize) * 2)); };
    size.set = [this](int v) { setTextSize(Theme::kMinTextSize + v / 2.0); };
    size.label = [](int v) { return QString("%1 pt").arg(Theme::kMinTextSize + v / 2.0, 0, 'f', 1); };
    size.note = "Every font follows it  \xC2\xB7  Ctrl+= / Ctrl+- / Ctrl+0";
    rows << size;

    Row fonts;
    fonts.name = "Fonts";
    fonts.kind = Row::Action;
    fonts.value = [] {
        return QFontInfo(Theme::instance().titleFont()).family() + "  \xC2\xB7  " + QFontInfo(Theme::instance().bodyFont()).family();
    };
    fonts.run = [this] { QTimer::singleShot(0, this, [this] { chooseFonts(); }); };
    fonts.note = "Title and body, from the fonts installed on this machine  \xC2\xB7  Ctrl+Shift+F";
    rows << fonts;

    heading("Immersion");
    Row immersion;
    immersion.name = "Immersion";
    immersion.choices = {"On", "Off"};
    immersion.get = [] { return Theme::instance().immersion() ? 0 : 1; };
    immersion.set = [](int v) {
        Theme::instance().setImmersion(v == 0);
        QSettings().setValue("immersion", v == 0);
    };
    immersion.note = "Title cards, MAGI, Pattern Blue, the entry plug, unit names; off = plain wording";
    rows << immersion;

    Row cards;
    cards.name = "Title cards";
    cards.choices = {"New pages", "Pages and journals", "Off"};
    cards.get = [] {
        const QString m = QSettings().value("titleCards", "new").toString();
        return m == "journals" ? 1 : m == "off" ? 2 : 0;
    };
    cards.set = [](int v) { QSettings().setValue("titleCards", v == 1 ? "journals" : v == 2 ? "off" : "new"); };
    cards.note = "The episode card when a new page first opens; or each time you open a journal too";
    rows << cards;

    Row art;
    art.name = "Sidebar unit";
    art.choices = {"Auto", "Unit-01", "Unit-00", "Unit-02", "Custom", "Off"};
    art.get = [] {
        const QStringList order = {"auto", "unit01", "unit00", "unit02", "custom", "off"};
        return qMax(0, static_cast<int>(order.indexOf(QSettings().value("unitArt", "auto").toString())));
    };
    art.set = [this](int v) {
        const QStringList order = {"auto", "unit01", "unit00", "unit02", "custom", "off"};
        QSettings().setValue("unitArt", order.value(v));
        update();
    };
    art.note = "Faint, behind the page list; Auto follows the colours (Unit-0x, or the desktop's). Nothing shows until its image exists";
    rows << art;

    Row pick;
    pick.name = "Your own image";
    pick.kind = Row::Action;
    pick.value = [] { return QString("Choose image\xE2\x80\xA6"); };
    pick.run = [this] {
        QTimer::singleShot(0, this, [this] {
            const QString file = QFileDialog::getOpenFileName(this, "Sidebar image", QDir::homePath(), "PNG image (*.png)");
            if (!file.isEmpty()) setCustomArt(file);
        });
    };
    pick.note = "A PNG, ideally with a transparent background; shown faintly like the units (Custom)";
    rows << pick;

    Row fetch;
    fetch.name = "Evangelion art";
    fetch.kind = Row::Action;
    fetch.value = [] { return QString("Download Eva art"); };
    fetch.run = [this] { QTimer::singleShot(0, this, [this] { downloadArt(); }); };
    fetch.note = "Official art of Units 00, 01 and 02, fetched once from the Evangelion wiki after you agree";
    rows << fetch;


    Row field;
    field.name = "A.T. Field";
    field.choices = {"Pulse", "Constant", "Off"};
    field.get = [this] { return static_cast<int>(nerv_->mode()); };
    field.set = [this](int v) { setNervMode(static_cast<NervPanel::Mode>(v)); };
    field.note = "Pulse on events and saves  \xC2\xB7  Constant while active, on the charger  \xC2\xB7  Ctrl+Shift+A";
    rows << field;

    Row strips;
    strips.name = "Pattern strips";
    strips.choices = {"Compact", "Detailed"};
    strips.get = [] { return QSettings().value("patternRows", "compact").toString() == "detailed" ? 1 : 0; };
    strips.set = [this](int v) {
        QSettings().setValue("patternRows", v == 1 ? "detailed" : "compact");
        updatePatternBlue();
    };
    strips.note = "On today's journal: the counts only, or each target with its subtasks (cores) under it";
    rows << strips;

    heading("Tasks");
    auto onOff = [&](const QString& name, const QString& key, const QString& note) {
        Row r;
        r.name = name;
        r.choices = {"On", "Off"};
        r.get = [key] { return QSettings().value(key, true).toBool() ? 0 : 1; };
        r.set = [this, key](int v) {
            QSettings().setValue(key, v == 0);
            applyTaskOptions();
        };
        r.note = note;
        rows << r;
    };
    onOff("Finish headings", "tasks/finish",
          "A heading turns DONE when all its subtasks are, and back when one opens  \xC2\xB7  marking it DONE closes them");
    onOff("Start headings", "tasks/start", "A TODO heading turns DOING once one of its subtasks is DOING or DONE");
    onOff("Fold finished", "tasks/fold", "A finished heading folds its subtasks away; it unfolds when reopened");
    onOff("Progress", "tasks/progress", "The count of done subtasks on each heading (SYNC 2/5)");
    Row carry;
    carry.name = "Carryover";
    carry.choices = {"On", "Off"};
    carry.get = [] { return QSettings().value("tasks/carryover", true).toBool() ? 0 : 1; };
    carry.set = [this](int v) {
        QSettings().setValue("tasks/carryover", v == 0);
        updatePatternBlue();
    };
    carry.note = "Today's journal lists the tasks left open in earlier journals (shown, not copied)";
    rows << carry;
    Row range;
    range.name = "Carryover from";
    range.choices = {"3 days", "7 days", "14 days", "All"};
    static const int kDays[] = {3, 7, 14, 0};
    range.get = [] {
        const int d = QSettings().value("tasks/carryoverDays", 7).toInt();
        for (int i = 0; i < 4; ++i)
            if (kDays[i] == d) return i;
        return 1;
    };
    range.set = [this](int v) {
        QSettings().setValue("tasks/carryoverDays", kDays[v]);
        updatePatternBlue();
    };
    range.note = "How far back Carryover looks: journals of the last 3, 7 or 14 days, or every one";
    rows << range;

    heading("Graph");
    Row folder;
    folder.name = "Folder";
    folder.kind = Row::Action;
    folder.value = [this] { return QDir::toNativeSeparators(graph_).replace(QDir::homePath(), "~"); };
    folder.run = [this] { QTimer::singleShot(0, this, [this] { chooseGraph(); }); };
    folder.note = "Open another graph  \xC2\xB7  Ctrl+O";
    rows << folder;

    Row essentials;
    essentials.name = "Graphite Essentials";
    essentials.kind = Row::Action;
    essentials.value = [] { return QString("Restore pages"); };
    essentials.run = [this] {
        QTimer::singleShot(0, this, [this] {
            syncIndex();
            const int n = supplyEssentials(true);
            if (n > 0) scanGraph();
            flash(n > 0 ? QString("Graphite Essentials: %1 page%2 restored").arg(n).arg(n == 1 ? "" : "s")
                        : QString("Graphite Essentials: nothing missing"));
        });
    };
    essentials.note = "The guides and the timetable Graphite supplies; brings back deleted ones (your edits stay)";
    rows << essentials;

    Row deleted;
    deleted.name = "Deleted pages";
    deleted.kind = Row::Info;
    deleted.value = [] { return QString("The Trash"); };
    deleted.note = "Ctrl+Shift+Del moves the open page there; restore it from your file manager";
    rows << deleted;

    Row backups;
    backups.name = "Backups";
    backups.kind = Row::Info;
    backups.value = [] { return QString(".graphite/bak/"); };
    backups.note = "A copy of each file before Graphite first changes it, where Logseq keeps its own";
    rows << backups;

#ifndef GRAPHITE_PACKAGED
    heading("Updates");
    updatesRow_ = static_cast<int>(rows.size()) + 1;  // "Update", under "Source"
    Row source;
    source.name = "Source";
    source.kind = Row::Info;
    source.value = [this] { return updater_->status(); };
    source.note = updater_->repo().isEmpty() ? QString("Built without a source folder")
                                             : QDir::toNativeSeparators(updater_->repo()).replace(QDir::homePath(), "~") +
                                                   "  \xC2\xB7  checked when its files change, never polled";
    rows << source;

    Row run;
    run.name = "Update";
    run.kind = Row::Action;
    run.value = [this] {
        switch (updater_->state()) {
            case Updater::State::Available: return QString("Build and install");
            case Updater::State::Building: return QString("Working\u2026");
            case Updater::State::Installed: return QString("Restart now");
            case Updater::State::Failed: return QString("Try again");
            default: return QString("Check now");
        }
    };
    run.run = [this] {
        switch (updater_->state()) {
            case Updater::State::Available:
            case Updater::State::Failed: updater_->install(); break;
            case Updater::State::Installed: QTimer::singleShot(0, this, [this] { restartForUpdate(); }); break;
            case Updater::State::Building: break;
            default: updater_->check(); flash(updater_->status()); break;
        }
    };
    run.note = "Runs ./install.sh --yes there: build, tests, install  \xC2\xB7  log in " +
               QDir::toNativeSeparators(updater_->logPath()).replace(QDir::homePath(), "~");
    rows << run;

    Row watchUpd;
    watchUpd.name = "Watch for updates";
    watchUpd.choices = {"On", "Off"};
    watchUpd.get = [this] { return updater_->watching() ? 0 : 1; };
    watchUpd.set = [this](int v) {
        QSettings().setValue("updates", v == 0 ? "watch" : "off");
        updater_->setWatching(v == 0);
    };
    watchUpd.note = "Shows \u201CUpdate ready\u201D in the sidebar when the source folder has a newer build";
    rows << watchUpd;

#endif

    heading("About");
    Row memory;
    memory.name = "Memory";
    memory.kind = Row::Info;
    {
        // The app's share of memory right now (PSS), as measured for the efficiency budget.
        QFile f("/proc/self/smaps_rollup");
        QString mb = "unknown";
        if (f.open(QIODevice::ReadOnly))
            for (const QByteArray& line : f.readAll().split('\n'))
                if (line.startsWith("Pss:")) mb = QString("%1 MB").arg(line.mid(4).trimmed().split(' ').value(0).toDouble() / 1024, 0, 'f', 1);
        memory.value = [mb] { return mb; };
    }
    memory.note = "Measured when this page opened";
    rows << memory;

    Row keys;
    keys.name = "Keys and snippets";
    keys.kind = Row::Action;
    keys.run = [this] { QTimer::singleShot(0, this, [this] { toggleKeySheet(); }); };
    keys.note = "Every shortcut, and the snippet list  \xC2\xB7  Ctrl+/";
    rows << keys;

    Row manual;
    manual.name = "Field manual";
    manual.kind = Row::Action;
    manual.run = [this] { openFieldManual(); };
    manual.note = "Nine hands-on drills for new users, in the browser  \xC2\xB7  F2";
    rows << manual;

    settings_->setRows(rows);
    stack_->setCurrentWidget(settings_);
    settings_->setFocus();
    pathLabel_->setText("Settings");
}

void MainWindow::onUpdateChanged() {
    using S = Updater::State;
    const S last = static_cast<S>(lastUpdateState_);
    const S st = updater_->state();
    const bool eva = Theme::instance().immersion();
    auto* row = static_cast<SideAction*>(updateAction_);
    const QString version = updater_->info().repoVersion;
    switch (st) {
        case S::Available: row->setText(eva ? "Refit ready" : "Update ready"); row->setHint(version); break;
        case S::Building: row->setText(eva ? "Refitting\u2026" : "Updating\u2026"); row->setHint(updater_->progress()); break;
        case S::Installed: row->setText(eva ? "Refit complete" : "Restart to update"); row->setHint(version); break;
        case S::Failed: row->setText("Update failed"); row->setHint(QString()); break;
        default: break;
    }
    row->setVisible(st == S::Available || st == S::Building || st == S::Installed || st == S::Failed);
    row->update();
    if (st != last) {
        if (st == S::Available) flash((eva ? "Refit ready: " : "Update ready: ") + updater_->status().section("  \u00B7  ", 1) + "  \u00B7  Settings");
        else if (st == S::Installed && last == S::Building) flash(eva ? "Refit complete \u00B7 restart to sync" : "Installed \u00B7 restart to use it");
        else if (st == S::Failed) flash("Update failed: " + updater_->progress());
        lastUpdateState_ = static_cast<int>(st);
    }
    if (stack_->currentWidget() == settings_) settings_->widget()->update();
}

// The new binary starts on this tree once the page is saved; this window then closes.
void MainWindow::restartForUpdate() {
    if (needsSave()) saveNow();
    if (!updater_->restart({graph_})) {
        flash("Could not start the new Graphite");
        return;
    }
    close();
}

void MainWindow::closeSettings() {
    if (stack_->currentWidget() != settings_) return;
    stack_->setCurrentWidget(view_);
    view_->setFocus();
    updateStatus();
}

void MainWindow::setNervMode(NervPanel::Mode m) {
    nerv_->setMode(m);
    QSettings().setValue("nervMode", m == NervPanel::Mode::Constant ? "constant" : m == NervPanel::Mode::Off ? "off" : "pulse");
    if (m == NervPanel::Mode::Pulse) nerv_->pulse();
}

void MainWindow::closeOverlay() {
    if (overlay_) overlay_->dismiss();
}

void MainWindow::toggleKeySheet() {
    if (qobject_cast<KeySheet*>(overlay_.data())) {
        closeOverlay();
        return;
    }
    closeOverlay();
    QList<KeySheet::Section> sections;
    sections.append({"Window", windowKeys_});
    sections.append({"Editing a block", OutlinerView::keyHelp()});
    overlay_ = new KeySheet(centralWidget(), sections, OutlinerView::snippetHelp());
}

void MainWindow::applyTheme() {
    const Theme& th = Theme::instance();
    auto rgb = [](const QColor& c) { return QString("color: rgb(%1,%2,%3);").arg(c.red()).arg(c.green()).arg(c.blue()); };
    qApp->setStyleSheet(th.styleSheet());
    // The body font can change at run time (Ctrl+Shift+F): everything set from it follows.
    if (qApp->font() != th.bodyFont()) qApp->setFont(th.bodyFont());
    nav_->setFont(th.bodyFont());
    graphLabel_->setFont(th.bodyFont());
    pathLabel_->setFont(th.labelFont());
    stateLabel_->setFont(th.labelFont());
    if (auto* brand = findChild<QLabel*>("brand")) brand->setFont(th.labelFont());
    blurMeter_->updateGeometry();
    sidebar_->setFixedWidth(sidebarWidth());
    for (QAbstractButton* b : sidebar_->findChildren<QAbstractButton*>()) b->updateGeometry();  // text size
    if (nerv_) nerv_->updateGeometry();
    graphLabel_->setStyleSheet(rgb(th.dim()));
    if (auto* brand = findChild<QLabel*>("brand")) brand->setStyleSheet(rgb(th.accent()));
    pathLabel_->setStyleSheet(rgb(th.faint()));
    view_->refreshTheme();
    scanGraph();  // section headings in the sidebar carry theme colours
    update();
}

void MainWindow::paintEvent(QPaintEvent*) {
    const Theme& th = Theme::instance();
    QPainter p(this);
    p.setCompositionMode(QPainter::CompositionMode_Source);
    p.fillRect(rect(), th.bg());
    const int sw = sidebar_ && sidebar_->isVisible() ? sidebarWidth() : 0;  // focus mode: no sidebar
    p.fillRect(QRect(0, 0, sw, height()), th.panel());
    p.setCompositionMode(QPainter::CompositionMode_SourceOver);
    p.setPen(th.line());
    if (sw > 0) p.drawLine(sw - 1, 0, sw - 1, height());

    // The Evangelion unit, faint, behind the page list: the palette's unit, or the one nearest
    // to the desktop's colours (Settings > Sidebar unit can fix it or turn it off).
    const QString artSetting = QSettings().value("unitArt", "auto").toString();
    if (sw > 0 && Theme::instance().immersion() && artSetting != "off" && nav_ && nerv_) {
        const QString unit = artSetting == "auto" ? unitForColours(Theme::instance().palette()) : artSetting;
        const int top = nav_->mapTo(this, QPoint(0, 0)).y() + 8;
        const int bottom = nerv_->isVisible() ? nerv_->mapTo(this, QPoint(0, 0)).y() - 16 : height() - 16;
        if (bottom - top > 80) paintUnitArt(p, QRect(12, top, sw - 24, bottom - top), unit);
    }
}

// ---------------------------------------------------------------- the graph

QString MainWindow::journalTitle(const QDate& d) const {
    return QLocale::c().toString(d, "MMM ") + ordinal(d.day()) + QLocale::c().toString(d, ", yyyy");
}

QString MainWindow::titleFromPath(const QString& path, bool* journal) const {
    const QFileInfo fi(path);
    const QString base = fi.completeBaseName();
    const bool inJournals = fi.dir().dirName() == "journals";
    if (inJournals) {
        const QDate d = QDate::fromString(base, journalFormat_);
        if (d.isValid()) {
            if (journal) *journal = true;
            return journalTitle(d);
        }
    }
    if (journal) *journal = false;
    QString t = base;
    if (fi.dir().dirName() == "pages") t.replace("___", "/");  // Logseq's namespaces, in its own folder
    return t;
}

// A new page: "Book/Page" puts it in that book; otherwise the current book.
QString MainWindow::pagePath(const QString& title) const {
    const int slash = static_cast<int>(title.indexOf('/'));
    if (slash > 0) {
        for (const BookInfo& b : books())
            if (b.name.compare(title.left(slash), Qt::CaseInsensitive) == 0 ||
                QFileInfo(b.dir).fileName().compare(title.left(slash), Qt::CaseInsensitive) == 0)
                return b.dir + "/" + QString(title.mid(slash + 1)).replace('/', '-') + ".md";
    }
    return defaultBookDir() + "/" + QString(title).replace('/', '-') + ".md";
}

void MainWindow::scanGraph() {
    pathByTitle_.clear();
    QList<Explorer::Page> journals;
    QDir jd(graph_ + "/journals");
    for (const QString& f : jd.entryList({"*.md"}, QDir::Files, QDir::Name | QDir::Reversed)) {
        const QString path = jd.absoluteFilePath(f);
        const QString title = titleFromPath(path, nullptr);
        journals.append({path, title});
        pathByTitle_.insert(title.toLower(), path);
    }
    QList<Explorer::Book> list;
    for (const BookInfo& b : books()) {
        Explorer::Book eb{b.name, b.dir, {}};
        QDir dir(b.dir);
        for (const QString& f : dir.entryList({"*.md"}, QDir::Files, QDir::Name | QDir::IgnoreCase)) {
            const QString path = dir.absoluteFilePath(f);
            const QString title = titleFromPath(path, nullptr);
            eb.pages.append({path, title});
            // [[Page]] finds it anywhere in the tree (the first book wins a clash); [[Book/Page]] exactly.
            if (!pathByTitle_.contains(title.toLower())) pathByTitle_.insert(title.toLower(), path);
            pathByTitle_.insert((b.name + "/" + title).toLower(), path);
            pathByTitle_.insert((QFileInfo(b.dir).fileName() + "/" + title).toLower(), path);
        }
        list.append(eb);
    }
    // Rebuild the explorer only when what it shows changed (our own saves touch the folders too,
    // and a rebuild would lose its scroll position); otherwise just move the highlight.
    QString sig;
    for (const Explorer::Page& j : journals) sig += j.path + '\n';
    for (const Explorer::Book& b : list) {
        sig += '#' + b.dir + '\n';
        for (const Explorer::Page& p : b.pages) sig += p.path + '\n';
    }
    if (sig != treeSignature_ || Theme::instance().palette() != lastExplorerPalette_) {
        treeSignature_ = sig;
        lastExplorerPalette_ = Theme::instance().palette();
        nav_->setContent(journals, list, page_ ? page_->path : QString());
    } else {
        nav_->setCurrent(page_ ? page_->path : QString());
    }
    // Watch the tree, its journals and every book: added / removed files arrive as events.
    QStringList dirs = {graph_, graph_ + "/journals"};
    for (const BookInfo& b : books(true)) dirs << b.dir;  // a page put into a folder makes it a book
    const QStringList watched = treeWatch_.directories();
    if (!watched.isEmpty()) treeWatch_.removePaths(watched);
    for (const QString& d : dirs)
        if (QFileInfo(d).isDir()) treeWatch_.addPath(d);
    syncIndex();
}


void MainWindow::showToday() {
    const QString path = graph_ + "/journals/" + QDate::currentDate().toString(journalFormat_) + ".md";
    if (openPath(path, !QFileInfo::exists(path))) {
        const int last = view_->document()->size() - 1;
        view_->focusBlock(last);
    }
}

void MainWindow::newPage() {
    closeOverlay();
    const int episode = episodeNumber();
    QList<QPair<QString, QString>> bookList;
    int start = 0;
    const QString here = defaultBookDir();
    for (const BookInfo& b : books()) {
        if (b.dir == here) start = static_cast<int>(bookList.size());
        bookList.append({b.name, b.dir});
    }
    if (bookList.isEmpty()) bookList.append({bookName(here), here});
    newPageIn_.clear();
    auto* prompt = new PagePrompt(
        centralWidget(), [this](const QString& name) { return pathByTitle_.contains(name.toLower()); }, episode, bookList, start);
    connect(prompt, &PagePrompt::accepted, this, [this, episode](const QString& name, const QString& caption, const QString& dir) {
        pendingEpisode_ = episode;  // the title card repeats the number the prompt showed
        if (pathByTitle_.contains(name.toLower()) || dir.isEmpty() || name.contains('/')) {
            openLink(name);
        } else {
            QDir().mkpath(dir);
            const QString path = dir + "/" + QString(name).replace('/', '-') + ".md";
            openPath(path, !QFileInfo::exists(path));
        }
        if (!page_) return;
        // The caption becomes the page's description:: property (Logseq reads it too), in a
        // bullet-less first block, with an empty block below to start writing in.
        if (!caption.isEmpty() && page_->isNew && page_->doc.size() == 1 && page_->doc.at(0).text.empty()) {
            QString one = caption;
            one.replace('\n', ' ');
            page_->doc = Document::parse(("description:: " + one + "\n-").toStdString());
            view_->setDocument(&page_->doc);
            pendingCaption_ = one;
            scheduleSave();
        }
        if (view_->document()) view_->focusBlock(view_->document()->size() - 1);
    });
    overlay_ = prompt;
}

void MainWindow::openLink(const QString& target) {
    if (target.startsWith("http://") || target.startsWith("https://")) {
        QDesktopServices::openUrl(QUrl(target));
        return;
    }
    const auto it = pathByTitle_.constFind(target.toLower());
    if (it != pathByTitle_.constEnd()) {
        openPath(it.value());
        return;
    }
    const QString path = pagePath(target);
    openPath(path, !QFileInfo::exists(path));
}

// ------------------------------------------------------- opening and saving

bool MainWindow::openPath(const QString& path, bool isNew) {
    if (page_ && page_->path == path) return true;
    if (!saveNow()) return false;  // user cancelled out of a save conflict
    return loadPage(path, isNew);
}

bool MainWindow::loadPage(QString path, bool isNew) {
    auto next = std::make_unique<Page>();
    next->path = path;
    next->isNew = isNew;
    next->title = titleFromPath(path, &next->journal);
    if (isNew) {
        next->doc = Document::parse("");
    } else {
        QFile f(path);
        if (!f.open(QIODevice::ReadOnly)) {
            flash("Could not open file");
            return false;
        }
        const QByteArray data = f.readAll();
        next->doc = Document::parse(std::string(data.constData(), static_cast<size_t>(data.size())));
        next->mtime = mtimeOf(path);
    }
    next->doc.ensureNonEmpty();

    saveTimer_.stop();
    if (stack_) stack_->setCurrentWidget(view_);  // opening a page leaves Settings
    view_->setPageDir(QFileInfo(path).absolutePath());
    view_->setAssetsDir(graph_ + "/assets");
    view_->setDocument(&next->doc);  // the view stops using the old page before it is destroyed
    page_ = std::move(next);
    externalChange_ = false;

    QDate date;
    if (page_->journal) date = QDate::fromString(QFileInfo(path).completeBaseName(), journalFormat_);
    view_->setHeader(page_->title, page_->journal ? "Journal" : "Page", date);
    nerv_->pulse();
    // The first opening of a new page plays its title card (after the New page prompt has had
    // its say about the caption, hence the zero-delay timer).
    // Settings > Title cards: new pages only (default), every journal too, or never.
    const QString cardsMode = QSettings().value("titleCards", "new").toString();
    const bool card = Theme::instance().immersion() && cardsMode != "off" &&
                      ((isNew && !page_->journal) || (page_->journal && cardsMode == "journals"));
    if (card) {
        pendingCaption_ = view_->caption();  // a journal's own description::, if it has one
        const QString title = page_->title;
        // Journals are numbered by the day of the year (EPISODE:296), pages by their count.
        const int episode = page_->journal && date.isValid() ? date.dayOfYear()
                            : pendingEpisode_ > 0            ? pendingEpisode_
                                                             : episodeNumber();
        pendingEpisode_ = 0;
        QTimer::singleShot(0, this, [this, title, episode] {
            if (page_ && page_->title == title) new TitleFlash(centralWidget(), episode, title, pendingCaption_);
        });
    }
    lastRefs_.clear();
    syncIndex();  // also catches pages written meanwhile by another program (stat only)

    nav_->setCurrent(path);
    newPageIn_.clear();
    if (!isNew) QSettings().setValue("lastPage/" + QString::number(qHash(graph_)), path);  // to reopen here next time
    updateStatus();
    return true;
}

void MainWindow::reloadCurrent() {
    if (page_) loadPage(page_->path, false);
}

bool MainWindow::needsSave() const {
    if (!page_) return false;
    if (page_->isNew) {
        // A page that was only opened, never typed in, must not leave an empty file behind.
        for (int i = 0; i < page_->doc.size(); ++i)
            if (!page_->doc.at(i).text.empty()) return true;
        return false;
    }
    return page_->doc.modified();
}

void MainWindow::scheduleSave() { saveTimer_.start(); }

void MainWindow::backupBeforeOverwrite() {
    if (!page_->isNew) backupFile(page_->path);
}

void MainWindow::backupFile(const QString& path, bool again) {
    // Before the first overwrite of a file in this session, keep a copy of what was
    // on disk, in the same bak/ folder Logseq itself uses.
    if ((backedUp_.contains(path) && !again) || !QFileInfo::exists(path)) return;
    backedUp_.insert(path);
    const QFileInfo fi(path);
    const QString rel = QDir(graph_).relativeFilePath(fi.absolutePath());
    const QString dir = graph_ + "/.graphite/bak/" + rel;
    QDir().mkpath(dir);
    const QString stamp = QDateTime::currentDateTime().toString("yyyyMMdd'T'HHmmss");
    QFile::copy(path, dir + "/" + fi.completeBaseName() + ".graphite-" + stamp + ".bak.md");
}

bool MainWindow::saveNow() {
    saveTimer_.stop();
    if (!page_ || !needsSave()) return true;

    if (!page_->isNew && QFileInfo::exists(page_->path) && mtimeOf(page_->path) != page_->mtime) {
        if (deliberating_) return false;  // already asking: autosave waits for the answer
        deliberating_ = true;
        closeOverlay();
        auto* magi = new MagiPanel(centralWidget(), QFileInfo(page_->path).fileName());
        overlay_ = magi;
        const MagiPanel::Choice choice = magi->exec();
        deliberating_ = false;
        if (choice == MagiPanel::Reload) {
            reloadCurrent();
            return true;
        }
        if (choice != MagiPanel::Overwrite) return false;
    }

    backupBeforeOverwrite();
    const std::string out = page_->doc.serialize();
    QDir().mkpath(QFileInfo(page_->path).absolutePath());
    QSaveFile f(page_->path);  // writes a temp file, then renames: never a half-written note
    const qint64 len = static_cast<qint64>(out.size());
    if (!f.open(QIODevice::WriteOnly) || f.write(out.data(), len) != len || !f.commit()) {
        flash("Save failed");
        stateLabel_->setStyleSheet(QString("color: %1;").arg(Theme::instance().warm().name()));
        return false;
    }

    const bool created = page_->isNew;
    page_->doc.markSaved();
    page_->isNew = false;
    page_->mtime = mtimeOf(page_->path);
    index_.update(page_->path, page_->title, page_->journal, page_->doc, page_->mtime);
    nerv_->pulse(1400);  // each save sends a short ripple through the field
    updatePatternBlue();
    externalChange_ = false;
    if (created) scanGraph();
    updateStatus();
    return true;
}

void MainWindow::checkExternalChange() {
    if (!page_ || page_->isNew || !QFileInfo::exists(page_->path)) return;
    if (mtimeOf(page_->path) == page_->mtime) return;
    if (!needsSave()) reloadCurrent();  // nothing of mine to lose: just show the new version
    else externalChange_ = true;
    updateStatus();
}

// A message in the status line that stays readable: the usual Editing / Saved does not
// replace it for `ms`.
void MainWindow::flash(const QString& text, int ms) {
    flashUntil_ = QDateTime::currentDateTime().addMSecs(ms);
    stateLabel_->setText(text);
    stateLabel_->setStyleSheet(QString("color: rgb(%1,%2,%3);").arg(Theme::instance().accent().red())
                                   .arg(Theme::instance().accent().green()).arg(Theme::instance().accent().blue()));
    QTimer::singleShot(ms + 50, this, [this] { updateStatus(); });
}

void MainWindow::updateStatus() {
    if (flashUntil_.isValid() && QDateTime::currentDateTime() < flashUntil_) return;
    if (!page_) {
        pathLabel_->clear();
        stateLabel_->clear();
        return;
    }
    const Theme& th = Theme::instance();
    pathLabel_->setText(QDir(graph_).relativeFilePath(page_->path));

    QString text;
    QColor color;
    if (externalChange_) {
        text = "Changed on disk";
        color = th.warm();
    } else if (needsSave()) {
        text = "Editing";
        color = th.dim();
    } else {
        text = Theme::instance().immersion() ? "MAGI \xC2\xB7 Approved" : "Saved";
        color = th.accent();
    }
    if (th.transparent()) text += "  \xC2\xB7  Transparent";
    stateLabel_->setText(text);
    stateLabel_->setStyleSheet(QString("color: rgb(%1,%2,%3);").arg(color.red()).arg(color.green()).arg(color.blue()));
}

// The tutorial: docs/field-manual.html, installed beside the program (share/graphite, or the app
// bundle's Resources; a build folder has its own copy). Without one, the page on claude.ai.
void MainWindow::openFieldManual() {
    static const QString kUrl = "https://claude.ai/artifact/GEaepPQjgFZquBB8ABLCy2";
    QSettings().setValue("manualHinted", true);
    const QString exe = QCoreApplication::applicationDirPath();
    QUrl url(kUrl);
    for (const QString& c : {exe + "/../share/graphite/field-manual.html",
                             exe + "/../Resources/field-manual.html", exe + "/field-manual.html"})
        if (QFileInfo::exists(c)) { url = QUrl::fromLocalFile(QFileInfo(c).canonicalFilePath()); break; }
    const QString shown = url.isLocalFile() ? url.toLocalFile() : kUrl;
    if (QGuiApplication::platformName() == "offscreen" || !QDesktopServices::openUrl(url)) {
        flash("Field manual: " + shown, 8000);  // tests, or no browser to hand it to
        return;
    }
    flash("Field manual opened in the browser");
}

bool MainWindow::setCustomArt(const QString& path) {
    QString error;
    if (!saveCustomArt(path, &error)) {
        flash(error);
        return false;
    }
    QSettings().setValue("unitArt", "custom");
    flash("Sidebar image set");
    update();
    return true;
}

bool MainWindow::downloadArt() {
    if (artDownload_) {
        flash("Unit art: already downloading");
        return false;
    }
    closeOverlay();
    auto* ask = new ConfirmPanel(centralWidget(), "Download Eva art",
                                 "Graphite does not ship the unit pictures. These three images are official artwork of Evangelion "
                                 "Units 00, 01 and 02, copyright Khara / Gainax. They are fetched from the Evangelion Fandom wiki "
                                 "(evangelion.fandom.com) for personal use on this computer only, and saved in " +
                                     QDir::toNativeSeparators(artDir()).replace(QDir::homePath(), "~") + ".",
                                 "download");
    overlay_ = ask;
    if (!ask->exec()) return false;
    if (QGuiApplication::platformName() == "offscreen") {  // tests never touch the network
        flash("Unit art: no network in this session");
        return true;
    }
    flash("Unit art: downloading\xE2\x80\xA6", 30000);
    artDownload_ = new ArtDownload(this);
    connect(artDownload_, &ArtDownload::finished, this, [this](int saved, const QString& error) {
        artDownload_->deleteLater();
        artDownload_ = nullptr;
        flash(error.isEmpty() ? QString("Unit art downloaded  \xC2\xB7  %1 images").arg(saved) : error);
        update();
    });
    artDownload_->start();
    return true;
}

bool MainWindow::scrapDay() {
    closeOverlay();
    if (!page_ || !page_->journal) {
        flash("Day scrap works on a journal: open one first (Ctrl+J: today)");
        return false;
    }
    bool empty = true;
    for (int b = 0; b < page_->doc.size() && empty; ++b) empty = page_->doc.at(b).text.empty() && page_->doc.at(b).meta.empty();
    if (empty) {
        flash("Nothing to scrap: the day is already empty");
        return false;
    }
    const bool eva = Theme::instance().immersion();
    auto* ask = new ConfirmPanel(centralWidget(), eva ? "Third Impact" : "Scrap the day",
                                 QString("Everything on %1 is wiped. Ctrl+Z brings it back, and a copy is kept in .graphite/bak.")
                                     .arg(page_->title),
                                 "scrap");
    overlay_ = ask;
    if (!ask->exec() || !page_) return false;
    saveNow();
    backupFile(page_->path, true);  // the day as it was, whatever was backed up earlier
    if (!eva) {
        view_->clearPage();
        flash("Day scrapped  \xC2\xB7  Ctrl+Z restores it");
        return true;
    }
    new ThirdImpact(centralWidget(), page_->title + "  \xC2\xB7  returned to LCL", [this, path = page_->path] {
        if (page_ && page_->path == path) view_->clearPage();
        nerv_->pulse(1400);
    });
    return true;
}

bool MainWindow::deletePage() { return page_ ? deletePath(page_->path) : false; }

// Delete a page, a journal or a whole book: after a yes, to the desktop Trash (restorable from
// the file manager), or to the tree's .graphite/recycle/ when there is no Trash.
bool MainWindow::deletePath(QString path) {
    closeOverlay();
    const QFileInfo fi(path);
    if (page_ && page_->path == path && (page_->isNew || !fi.exists())) {
        flash("Not saved yet: nothing to delete");
        return false;
    }
    if (!fi.exists()) return false;
    const bool book = fi.isDir();
    bool journal = false;
    const QString title = book ? bookName(path) : titleFromPath(path, &journal);
    const int count = book ? static_cast<int>(QDir(path).entryList({"*.md"}, QDir::Files).size()) : 0;
    const QString what = book ? (count ? QString("The book \u201C%1\u201D and its %2 page%3 go").arg(title).arg(count).arg(count == 1 ? "" : "s")
                                       : QString("The empty book \u201C%1\u201D goes").arg(title))
                              : QString("\u201C%1\u201D goes").arg(title);
    auto* ask = new ConfirmPanel(centralWidget(), book ? "Delete book" : journal ? "Delete journal" : "Delete page",
                                 what + " to the Trash; you can restore it from there. Links to it on other pages stay.",
                                 "delete");
    overlay_ = ask;
    if (!ask->exec()) return false;

    const bool affectsOpen = page_ && (page_->path == path || page_->path.startsWith(path + "/"));
    if (affectsOpen) {
        saveTimer_.stop();
        view_->setDocument(nullptr);  // the view lets go of the page before it is destroyed
        page_.reset();
    }
    bool gone = qEnvironmentVariableIsEmpty("GRAPHITE_NO_TRASH") && QFile::moveToTrash(path);
    if (!gone) {
        const QString dir = graph_ + "/.graphite/recycle/" + QDir(graph_).relativeFilePath(fi.absolutePath());
        QDir().mkpath(dir);
        gone = QFile::rename(path, dir + "/" + (book ? fi.fileName() : fi.completeBaseName()) + ".graphite-" +
                                       QDateTime::currentDateTime().toString("yyyyMMdd'T'HHmmss") + (book ? "" : ".md"));
    }
    backedUp_.remove(path);
    scanGraph();
    if (affectsOpen || !page_) showToday();
    flash(gone ? QString("Deleted \u201C%1\u201D").arg(title) : QString("Could not delete \u201C%1\u201D").arg(title));
    return gone;
}

// ---------------------------------------------------------------- operations

void MainWindow::onOperationCommand(const QString& args) {
    const QString a = args.trimmed().toLower();
    if (a == "stop" || a == "cancel" || a == "end" || a == "off") {
        if (operationRunning()) {
            stopOperation();
            flash(Theme::instance().immersion() ? "Operation aborted" : "Timer stopped");
        }
        return;
    }
    const int seconds = parseDuration(a);
    if (seconds <= 0) {
        flash("Timer: try /timer 20m 15s, /timer 25 or /timer stop");
        return;
    }
    startOperation(seconds);
}

bool MainWindow::startOperation(int seconds) {
    if (seconds <= 0) return false;
    opStart_ = QDateTime::currentDateTime();
    opEnd_ = opStart_.addSecs(seconds);
    QSettings s;
    s.setValue("operation/start", opStart_);
    s.setValue("operation/end", opEnd_);
    opTick_.start();
    operationTick();
    nerv_->pulse(1600);
    flash((Theme::instance().immersion() ? "Operation start  \xC2\xB7  " : "Timer  \xC2\xB7  ") + formatDuration(seconds));
    return true;
}

void MainWindow::stopOperation() {
    opTick_.stop();
    opStart_ = opEnd_ = QDateTime();
    QSettings().remove("operation");
    nerv_->setOperation(-1, 0);
}

void MainWindow::operationTick() {
    if (!operationRunning()) return;
    const qint64 left = QDateTime::currentDateTime().secsTo(opEnd_);
    if (left <= 0) {
        finishOperation();
        return;
    }
    nerv_->setOperation(static_cast<int>(left), static_cast<int>(opStart_.secsTo(opEnd_)));
}

void MainWindow::finishOperation() {
    const QDateTime start = opStart_, end = opEnd_;
    stopOperation();
    const int length = static_cast<int>(start.secsTo(end));
    const QString span = QLocale::c().toString(start, "HH:mm") + "\u2013" + QLocale::c().toString(end, "HH:mm");
    // Logged in today's journal, tagged, so the "operation" page collects every session.
    appendToToday(QString("Operation %1 \xC2\xB7 %2 #operation").arg(formatDuration(length), span));
    nerv_->pulse(2600);
    QApplication::alert(this);  // the compositor marks the window (urgent) if it is not in front
    const bool eva = Theme::instance().immersion();
    const QString note = formatDuration(length) + "  \u00B7  " + span + "  \u00B7  take a break";
#ifdef Q_OS_MACOS
    {   // Notification Centre through AppleScript; the text goes in as an argument, never into the script
        QProcess::startDetached("osascript",
                                {"-e", "on run argv", "-e", "display notification (item 2 of argv) with title (item 1 of argv)",
                                 "-e", "end run", eva ? "Operation complete" : "Time's up", note});
    }
#else
    if (QGuiApplication::platformName() == "wayland" || QGuiApplication::platformName() == "xcb")
        QProcess::startDetached("notify-send", {"-a", "Graphite", eva ? "Operation complete" : "Time's up", note});
#endif
    if (eva) new Eyecatch(centralWidget(), "Operation complete", formatDuration(length) + "  \xC2\xB7  " + span);
    flash(eva ? "Operation complete" : "Time's up");
}

void MainWindow::appendToToday(const QString& text) {
    const QString path = graph_ + "/journals/" + QDate::currentDate().toString(journalFormat_) + ".md";
    if (page_ && page_->path == path) {  // open: a new last block, saved as usual
        view_->pushUndoPoint();
        Document& d = page_->doc;
        const bool emptyOnly = d.size() == 1 && d.at(0).text.empty();
        if (emptyOnly) d.setText(0, text.toStdString());
        else d.insertBlocks(d.size() - 1, Document::parse(("- " + text).toStdString()));
        view_->reloadBlocks();
        scheduleSave();
        return;
    }
    // Not open: append to the file (only that block is new; the rest stays byte for byte).
    Document d;
    if (QFile in(path); in.open(QIODevice::ReadOnly)) d = Document::parse(in.readAll().toStdString());
    else d = Document::parse("");
    d.ensureNonEmpty();
    if (d.size() == 1 && d.at(0).text.empty() && d.at(0).meta.empty()) d.setText(0, text.toStdString());
    else d.insertBlocks(d.size() - 1, Document::parse(("- " + text).toStdString()));
    backupFile(path);
    QDir().mkpath(QFileInfo(path).absolutePath());
    const std::string out = d.serialize();
    QSaveFile f(path);
    const qint64 len = static_cast<qint64>(out.size());
    if (f.open(QIODevice::WriteOnly) && f.write(out.data(), len) == len && f.commit()) {
        bool journal = false;
        index_.update(path, titleFromPath(path, &journal), journal, d, mtimeOf(path));
        scanGraph();
    }
}

void MainWindow::chooseGraph() { openTreeDialog(); }

void MainWindow::closeEvent(QCloseEvent* e) {
    if (!saveNow()) {
        e->ignore();
        return;
    }
    blur_->setActive(false);  // give Hyprland the user's own blur back
    e->accept();
}

void MainWindow::changeEvent(QEvent* e) {
    QMainWindow::changeEvent(e);
    if (e->type() == QEvent::ActivationChange && isActiveWindow()) {
        if (!activatedOnce_) {  // start-up: the entry plug boots, then the field powers on
            activatedOnce_ = true;
            nerv_->boot();
        }
        checkExternalChange();
        syncIndex();  // Logseq or another editor may have changed other pages meanwhile
    }
}

}  // namespace graphite
