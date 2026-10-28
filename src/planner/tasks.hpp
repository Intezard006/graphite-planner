// Tasks: TODO / DOING / NOW / LATER / WAITING blocks, as Logseq writes them.
//
// Nothing is stored: collectTasks() reads the block texts the GraphIndex already holds, when
// the Tasks panel opens, and the list is dropped when it closes.
#pragma once

#include <QDate>
#include <QDateTime>
#include <QHash>
#include <QPair>
#include <QStringList>
#include <QVector>
#include <QList>
#include <QString>

namespace graphite {

class GraphIndex;
class Document;

struct Task {
    int page = -1;    // index into GraphIndex::pages()
    int block = -1;
    QString marker;   // TODO, DOING, ...
    QString text;     // first line without the marker, for display
    QDate scheduled;  // SCHEDULED: <2026-10-30 Fri>
    QDate deadline;   // DEADLINE: <...>
    QDateTime deadlineAt;  // DEADLINE: <2026-10-25 Sun 14:00>: the moment; invalid without a time
    QDate due() const;  // the earlier of the two, or invalid
    // Subtasks: a task indented under another task (any marker, open or done).
    int level = 0;      // 0: a target of its own; 1: its subtask; 2: a subtask's subtask...
    int parent = -1;    // index of the parent task in the list, or -1
    int subDone = 0, subTotal = 0;  // its direct and deeper subtasks, done ones included
    QDate groupDue;     // the earliest date of the task and its open subtasks (orders the list)
    int time = -1;      // a time block ("18:00 TODO …"): minutes since midnight, else -1
};

// Open tasks across the graph: dated ones by date, then undated by journal (newest first).
// A task's open subtasks follow it, in page order (under a done task: its nearest open ancestor).
// A time block in a journal without a date of its own is scheduled on that journal's day
// (`journalFormat`: the journal file names, as in Logseq's config).
QList<Task> collectTasks(const GraphIndex& index, const QString& journalFormat = "yyyy_MM_dd");

// Carryover: the open tasks (TODO / DOING / NOW / LATER...) left in journals dated before
// `today`, at most `days` days back (0: every past journal), dated or not. Each target brings
// its open subtasks (level > 0); an open subtask of a finished heading is a target of its own.
// Oldest day first, then in page order. `task`: an index into collectTasks' list.
struct CarriedTask {
    int task = -1;
    QDate day;          // the journal it is in
    int daysOpen = 0;   // today - day
};
QList<CarriedTask> carryoverTasks(const QList<Task>& tasks, const GraphIndex& index, const QDate& today, int days,
                                  const QString& journalFormat = "yyyy_MM_dd");

// Time blocks: a block whose first line starts with a time (after any task marker) is a slot in
// the day: "09:00 TODO problem set", "TODO 14:00-15:30 lab", "16.30 call home". Minutes since
// midnight; end is -1 without a range. `at`/`length`: where the time is in the text.
struct TimeSlot {
    int start = -1, end = -1;
    int at = 0, length = 0;
    bool done = false;  // DONE or CANCELED
};
bool blockTime(const QString& text, TimeSlot* slot);
QString slotText(const TimeSlot& s);  // "09:00", "14:00–15:30"

// "DEADLINE: <2026-10-25 Sun 14:00>": the moment, or invalid when the date has no time.
QDateTime blockDeadlineTime(const QString& blockText);
// The countdown to `at`: "T-03H 12M" (minutes rounded up), "T-00H 00M" at the moment,
// "LIMIT +01H 05M" past it.
QString countdownText(const QDateTime& at, const QDateTime& now = QDateTime::currentDateTime());

// The earlier of a block's SCHEDULED / DEADLINE dates, or invalid.
QDate blockDue(const QString& blockText);

// The block's marker, or empty.
QString taskMarker(const QString& blockText);
// Ctrl+Enter: none -> TODO -> DOING -> DONE -> none (LATER -> NOW -> DONE for the NOW workflow).
QString cycleTask(const QString& blockText);
// The block with `marker` (TODO, DONE, ...) in front, replacing any; empty removes it.
QString setTaskMarker(const QString& blockText, const QString& marker);
// The same block marked DONE.
QString completeTask(const QString& blockText);
// The first line for display: no marker, heading hashes or inline dates ("18:00 Make notes").
QString taskTitle(const QString& blockText);

// Headings that follow their subtasks (a subtask: a task under another task, the nearest task
// above it on its branch, as in collectTasks):
//   finish  all subtasks closed (at least one DONE) -> the heading is DONE; a DONE heading with
//           an open subtask (a new one, or one reopened) -> DOING when work has started, else
//           TODO (NOW / LATER for the LATER/NOW workflow); a heading you mark DONE closes its
//           open subtasks (cascade)
//   start   a TODO heading turns DOING once one of its subtasks is DOING or DONE
//   fold    a heading that finishes folds its subtasks away; one that reopens unfolds
// Blocks in `userChanged` were just changed by you: they are never rolled up themselves.
// CANCELED headings, and headings whose subtasks are all CANCELED, are left alone.
struct TaskSyncOptions {
    bool finish = true, start = true, fold = true;
};
struct TaskEdit {
    enum Kind { Finished, Reopened, Started, Cascaded };
    int block = -1;
    QString text;  // the block's new text
    Kind kind = Finished;
};
QList<TaskEdit> taskSyncEdits(const QVector<int>& depths, QStringList texts, const QList<int>& userChanged,
                              const TaskSyncOptions& options);
// The subtasks of each task block: {done (or canceled), total}; blocks without subtasks are
// not in it.
QHash<int, QPair<int, int>> subtaskCounts(const QVector<int>& depths, const QStringList& texts);
// The same on a page: the edits applied (only changed blocks are touched) and folds set;
// `finished` gets the headings that turned DONE with their subtasks (auto or cascaded).
QList<TaskEdit> syncTasks(Document& doc, const QList<int>& userChanged, const TaskSyncOptions& options,
                          QList<int>* finished = nullptr);

}  // namespace graphite
