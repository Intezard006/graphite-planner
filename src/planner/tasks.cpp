#include "planner/tasks.hpp"

#include <QHash>
#include <QFileInfo>
#include <QRegularExpression>
#include <algorithm>
#include <cmath>
#include <functional>

#include "core/document.hpp"
#include "notes/graphindex.hpp"
#include "planner/progress.hpp"

namespace graphite {

namespace {
const QRegularExpression& markerRe() {
    static const QRegularExpression re(R"(^(TODO|DOING|NOW|LATER|WAITING|DONE|CANCELED|CANCELLED)(?=\s|$))");
    return re;
}

bool isOpen(const QString& marker) { return marker != "DONE" && !marker.startsWith("CANCEL") && !marker.isEmpty(); }

// "SCHEDULED: <2026-10-30 Fri>" on any line of the block.
QDate dateLine(const QString& text, const char* key) {
    // On a line of its own (as Logseq writes it) or on the task's own line.
    static const QRegularExpression re(R"((SCHEDULED|DEADLINE):\s*<(\d{4}-\d{2}-\d{2})[^>]*>)");
    for (auto it = re.globalMatch(text); it.hasNext();) {
        const QRegularExpressionMatch m = it.next();
        if (m.captured(1) == QLatin1String(key)) return QDate::fromString(m.captured(2), "yyyy-MM-dd");
    }
    return QDate();
}

// A heading keeps its "## " in front of the marker, as Logseq writes it: "## TODO Exam"; a
// time block keeps its time there: "09:00 TODO problem set", "## 14:00-15:30 DOING lab".
int headingPrefix(const QString& text) {
    static const QRegularExpression re(R"(^(#{1,6} )?(([01]?\d|2[0-3])[:.][0-5]\d(\s*[-–]\s*([01]?\d|2[0-3])[:.][0-5]\d)? +)?)");
    return static_cast<int>(re.match(text).capturedLength());
}
int headingOnly(const QString& text) { return text.startsWith('#') ? static_cast<int>(text.indexOf(' ') + 1) : 0; }

QString replaceMarker(const QString& full, const QString& next) {
    const int h = headingPrefix(full);
    const QString head = full.left(h);
    const QString text = full.mid(h);
    const QRegularExpressionMatch m = markerRe().match(text);
    QString rest = m.hasMatch() ? text.mid(m.capturedLength()) : text;
    if (next.isEmpty()) {
        if (rest.startsWith(' ')) rest.remove(0, 1);
        return head + rest;
    }
    if (!m.hasMatch()) return head + next + (rest.isEmpty() || rest.startsWith(' ') ? "" : " ") + rest;
    return head + next + rest;
}
}  // namespace

QDate Task::due() const {
    if (scheduled.isValid() && deadline.isValid()) return qMin(scheduled, deadline);
    return scheduled.isValid() ? scheduled : deadline;
}

QDate blockDue(const QString& text) {
    const QDate s = dateLine(text, "SCHEDULED"), d = dateLine(text, "DEADLINE");
    if (s.isValid() && d.isValid()) return qMin(s, d);
    return s.isValid() ? s : d;
}

QString taskMarker(const QString& blockText) {
    const QRegularExpressionMatch m = markerRe().match(blockText.mid(headingPrefix(blockText)));
    return m.hasMatch() ? m.captured(1) : QString();
}

QString cycleTask(const QString& t) {
    const QString m = taskMarker(t);
    if (m.isEmpty()) return replaceMarker(t, "TODO");
    if (m == "TODO") return replaceMarker(t, "DOING");
    if (m == "LATER") return replaceMarker(t, "NOW");
    if (m == "DOING" || m == "NOW" || m == "WAITING") return replaceMarker(t, "DONE");
    return replaceMarker(t, QString());  // DONE / CANCELED -> plain block again
}

QString completeTask(const QString& t) { return replaceMarker(t, "DONE"); }

QString setTaskMarker(const QString& t, const QString& marker) { return replaceMarker(t, marker); }

QString taskTitle(const QString& b) {
    const QString first = b.section('\n', 0, 0);
    const QString marker = taskMarker(first);
    const int h = headingOnly(first), p = headingPrefix(first);  // the time stays in the text
    QString t = marker.isEmpty() ? first.mid(h) : first.mid(h, p - h) + first.mid(p + marker.size()).trimmed();
    static const QRegularExpression inlineDate(R"(\s*(SCHEDULED|DEADLINE):\s*<[^>]*>)");
    t.remove(inlineDate);
    if (Progress p; findProgress(t, &p))  // a progress bar: its percentage
        t = t.left(p.at) + QString("%1%").arg(static_cast<int>(std::floor(p.ratio() * 100 + 1e-9))) + t.mid(p.at + p.length);
    return t.simplified();
}

namespace {
// For each block, the nearest task above it on its branch (-1: none); markers alongside.
QVector<int> taskParents(const QVector<int>& depths, const QStringList& texts, QStringList* markers) {
    const int n = static_cast<int>(texts.size());
    QVector<int> parent(n, -1);
    markers->clear();
    QList<int> branch;  // block indices on the current branch
    for (int i = 0; i < n; ++i) {
        markers->append(taskMarker(texts[i]));
        const int d = i < depths.size() ? depths[i] : 0;
        while (!branch.isEmpty() && (branch.back() < depths.size() ? depths[branch.back()] : 0) >= d) branch.removeLast();
        for (int k = static_cast<int>(branch.size()) - 1; k >= 0; --k)
            if (!(*markers)[branch[k]].isEmpty()) {
                parent[i] = branch[k];
                break;
            }
        branch.append(i);
    }
    return parent;
}
}  // namespace

QHash<int, QPair<int, int>> subtaskCounts(const QVector<int>& depths, const QStringList& texts) {
    QStringList markers;
    const QVector<int> parent = taskParents(depths, texts, &markers);
    QHash<int, QPair<int, int>> counts;
    for (int i = 0; i < texts.size(); ++i) {
        if (markers[i].isEmpty()) continue;
        for (int up = parent[i]; up >= 0; up = parent[up]) {  // every task above counts it
            QPair<int, int>& c = counts[up];
            if (!isOpen(markers[i])) ++c.first;
            ++c.second;
        }
    }
    return counts;
}

QList<TaskEdit> taskSyncEdits(const QVector<int>& depths, QStringList texts, const QList<int>& userChanged,
                              const TaskSyncOptions& o) {
    QStringList markers;
    const QVector<int> parent = taskParents(depths, texts, &markers);
    const int n = static_cast<int>(texts.size());
    QVector<QList<int>> children(n);
    for (int i = 0; i < n; ++i)
        if (!markers[i].isEmpty() && parent[i] >= 0) children[parent[i]].append(i);
    QList<TaskEdit> edits;
    QHash<int, int> at;  // block -> index in edits
    auto set = [&](int i, const QString& marker, TaskEdit::Kind kind) {
        texts[i] = setTaskMarker(texts[i], marker);
        markers[i] = marker;
        if (at.contains(i)) {
            edits[at[i]].text = texts[i];
            edits[at[i]].kind = kind;
        } else {
            at.insert(i, static_cast<int>(edits.size()));
            edits.append({i, texts[i], kind});
        }
    };
    auto depthOf = [&](int i) { return i < depths.size() ? depths[i] : 0; };
    // A heading you mark DONE takes its open subtasks with it.
    if (o.finish)
        for (int u : userChanged) {
            if (u < 0 || u >= n || markers[u] != "DONE") continue;
            for (int j = u + 1; j < n && depthOf(j) > depthOf(u); ++j)
                if (isOpen(markers[j])) set(j, "DONE", TaskEdit::Cascaded);
        }
    // Bottom-up: children come after their parent, so a grandparent sees its children's new state.
    for (int i = n - 1; i >= 0; --i) {
        const QString m = markers[i];
        if (children[i].isEmpty() || m.isEmpty() || m.startsWith("CANCEL") || userChanged.contains(i)) continue;
        int open = 0, done = 0, active = 0;
        bool nowFlow = false;
        for (int c : children[i]) {
            const QString& cm = markers[c];
            if (cm == "DONE") ++done;
            else if (!cm.startsWith("CANCEL")) {
                ++open;
                if (cm == "DOING" || cm == "NOW") ++active;
            }
            if (cm == "LATER" || cm == "NOW") nowFlow = true;
        }
        if (o.finish && open == 0 && done > 0 && isOpen(m)) {
            set(i, "DONE", TaskEdit::Finished);
        } else if (o.finish && m == "DONE" && open > 0) {
            const bool started = done + active > 0;
            set(i, started ? (nowFlow ? "NOW" : "DOING") : (nowFlow ? "LATER" : "TODO"), TaskEdit::Reopened);
        } else if (o.start && (m == "TODO" || m == "LATER") && open > 0 && done + active > 0) {
            set(i, m == "LATER" || nowFlow ? "NOW" : "DOING", TaskEdit::Started);
        }
    }
    return edits;
}

QList<TaskEdit> syncTasks(Document& doc, const QList<int>& userChanged, const TaskSyncOptions& o, QList<int>* finished) {
    if (!o.finish && !o.start) return {};
    QVector<int> depths;
    QStringList texts;
    for (int i = 0; i < doc.size(); ++i) {
        depths << doc.at(i).depth;
        texts << QString::fromStdString(doc.at(i).text);
    }
    const QList<TaskEdit> edits = taskSyncEdits(depths, texts, userChanged, o);
    bool cascaded = false;
    for (const TaskEdit& e : edits) {
        doc.setText(e.block, e.text.toStdString());
        cascaded = cascaded || e.kind == TaskEdit::Cascaded;
        if (e.kind == TaskEdit::Finished) {
            if (finished) finished->append(e.block);
            if (o.fold && doc.hasChildren(e.block)) doc.setCollapsed(e.block, true);
        } else if (e.kind == TaskEdit::Reopened && o.fold && doc.hasChildren(e.block) && doc.at(e.block).collapsed()) {
            doc.setCollapsed(e.block, false);
        }
    }
    // A heading you marked DONE that took subtasks with it counts as finished too.
    if (cascaded)
        for (int u : userChanged) {
            if (u < 0 || u >= doc.size() || taskMarker(QString::fromStdString(doc.at(u).text)) != "DONE") continue;
            if (finished) finished->append(u);
            if (o.fold && doc.hasChildren(u)) doc.setCollapsed(u, true);
        }
    return edits;
}

bool blockTime(const QString& text, TimeSlot* slot) {
    static const QRegularExpression re(
        R"(^((?:TODO|DOING|NOW|LATER|DONE|WAITING|CANCELED|CANCELLED)\s+)?(?:#{1,6}\s+)?(([01]?\d|2[0-3])[:.]([0-5]\d)(?:\s*[-–]\s*([01]?\d|2[0-3])[:.]([0-5]\d))?)(?=\s|$)(\s+(?:TODO|DOING|NOW|LATER|DONE|WAITING|CANCELED|CANCELLED)(?=\s|$))?)");
    const QRegularExpressionMatch m = re.match(text);
    if (!m.hasMatch()) return false;
    slot->start = m.captured(3).toInt() * 60 + m.captured(4).toInt();
    slot->end = m.capturedLength(5) ? m.captured(5).toInt() * 60 + m.captured(6).toInt() : -1;
    if (slot->end >= 0 && slot->end <= slot->start) slot->end = -1;  // "23:00-01:00": just the start
    slot->at = static_cast<int>(m.capturedStart(2));
    slot->length = static_cast<int>(m.capturedLength(2));
    const QString marker = (m.captured(1) + m.captured(7)).trimmed();
    slot->done = marker == "DONE" || marker.startsWith("CANCEL");
    return true;
}

QString slotText(const TimeSlot& s) {
    auto hm = [](int m) { return QString("%1:%2").arg(m / 60, 2, 10, QChar('0')).arg(m % 60, 2, 10, QChar('0')); };
    return s.end >= 0 ? hm(s.start) + QString::fromUtf8("–") + hm(s.end) : hm(s.start);
}

QDateTime blockDeadlineTime(const QString& text) {
    static const QRegularExpression re(R"(DEADLINE:\s*<(\d{4}-\d{2}-\d{2})[^>\d]*(\d{1,2}):(\d{2})[^>]*>)");
    const QRegularExpressionMatch m = re.match(text);
    if (!m.hasMatch()) return QDateTime();
    const QDate d = QDate::fromString(m.captured(1), "yyyy-MM-dd");
    const QTime t(m.captured(2).toInt(), m.captured(3).toInt());
    return d.isValid() && t.isValid() ? QDateTime(d, t) : QDateTime();
}

QString countdownText(const QDateTime& at, const QDateTime& now) {
    const qint64 secs = now.secsTo(at);
    const qint64 mins = secs >= 0 ? (secs + 59) / 60 : (-secs) / 60;
    const QString hm = QString("%1H %2M").arg(mins / 60, 2, 10, QChar('0')).arg(mins % 60, 2, 10, QChar('0'));
    return secs >= 0 ? "T\xE2\x88\x92" + hm : "LIMIT +" + hm;
}

QList<Task> collectTasks(const GraphIndex& index, const QString& journalFormat) {
    QList<Task> flat;
    const QVector<GraphIndex::Page>& pages = index.pages();
    for (int pi = 0; pi < pages.size(); ++pi) {
        const QStringList& blocks = pages[pi].blocks;
        const QVector<qint8>& depths = pages[pi].depths;
        // The tasks above this block on its branch: {depth, index into flat or -1 when done}.
        QList<QPair<int, int>> branch;
        QList<int> doneTask;  // ...and for done ones, the open ancestor they count towards
        for (int bi = 0; bi < blocks.size(); ++bi) {
            const QString& b = blocks[bi];
            const int depth = bi < depths.size() ? depths[bi] : 0;
            while (!branch.isEmpty() && branch.back().first >= depth) {
                branch.removeLast();
                doneTask.removeLast();
            }
            if (b.size() < 3 || !(b.front().isUpper() || b.front() == '#' || b.front().isDigit())) continue;  // cheap reject before the regex
            const QString marker = taskMarker(b);
            if (marker.isEmpty()) continue;
            // The nearest open task above it: its parent (done ones count, but have no row).
            int parent = -1;
            if (!branch.isEmpty()) parent = branch.back().second >= 0 ? branch.back().second : doneTask.back();
            for (int up = parent; up >= 0; up = flat[up].parent) {
                ++flat[up].subTotal;
                if (!isOpen(marker)) ++flat[up].subDone;
            }
            if (!isOpen(marker)) {
                branch.append({depth, -1});
                doneTask.append(parent);
                continue;
            }
            Task t;
            t.page = pi;
            t.block = bi;
            t.marker = marker;
            t.parent = parent;
            t.level = parent >= 0 ? flat[parent].level + 1 : 0;
            t.text = taskTitle(b);
            t.scheduled = dateLine(b, "SCHEDULED");
            t.deadline = dateLine(b, "DEADLINE");
            t.deadlineAt = blockDeadlineTime(b);
            if (TimeSlot slot; blockTime(b, &slot)) {
                t.time = slot.start;
                if (!t.scheduled.isValid() && !t.deadline.isValid() && pages[pi].journal)
                    t.scheduled = QDate::fromString(QFileInfo(pages[pi].path).completeBaseName(), journalFormat);
            }
            t.groupDue = t.due();
            for (int up = parent; up >= 0; up = flat[up].parent)
                if (t.groupDue.isValid() && (!flat[up].groupDue.isValid() || t.groupDue < flat[up].groupDue))
                    flat[up].groupDue = t.groupDue;
            branch.append({depth, static_cast<int>(flat.size())});
            doneTask.append(parent);
            flat << t;
        }
    }
    // Order the targets (tasks without a parent); each brings its subtasks along.
    QList<int> roots;
    QHash<int, QList<int>> children;
    for (int i = 0; i < flat.size(); ++i) {
        if (flat[i].parent < 0) roots << i;
        else children[flat[i].parent] << i;
    }
    std::stable_sort(roots.begin(), roots.end(), [&](int ia, int ib) {
        const Task& a = flat[ia];
        const Task& b = flat[ib];
        const QDate da = a.groupDue, db = b.groupDue;
        if (da.isValid() != db.isValid()) return da.isValid();  // dated first
        if (da.isValid() && da != db) return da < db;
        if ((a.time >= 0) != (b.time >= 0)) return a.time >= 0;  // the same day: timed ones first, by time
        if (a.time != b.time) return a.time < b.time;
        const auto& pa = pages[a.page];
        const auto& pb = pages[b.page];
        if (pa.journal != pb.journal) return pa.journal;
        if (a.page != b.page) return pa.journal ? pa.path > pb.path : pa.title.compare(pb.title, Qt::CaseInsensitive) < 0;
        return a.block < b.block;
    });
    QList<Task> out;
    out.reserve(flat.size());
    QHash<int, int> where;  // flat index -> out index
    std::function<void(int)> emitTree = [&](int i) {
        Task t = flat[i];
        t.parent = t.parent >= 0 ? where.value(t.parent, -1) : -1;
        where[i] = static_cast<int>(out.size());
        out << t;
        for (int c : children.value(i)) emitTree(c);
    };
    for (int r : roots) emitTree(r);
    return out;
}

QList<CarriedTask> carryoverTasks(const QList<Task>& tasks, const GraphIndex& index, const QDate& today, int days,
                                  const QString& journalFormat) {
    struct Chunk {
        QDate day;
        int block = 0;
        QList<int> tasks;  // a target, then its subtasks (they follow it in collectTasks' list)
    };
    QList<Chunk> chunks;
    QHash<int, QDate> dayOf;  // page -> its journal's day, or invalid
    bool carrying = false;    // the current target was carried: its subtasks come too
    for (int i = 0; i < tasks.size(); ++i) {
        const Task& t = tasks[i];
        if (t.level > 0) {
            if (carrying) chunks.back().tasks << i;
            continue;
        }
        carrying = false;
        auto d = dayOf.find(t.page);
        if (d == dayOf.end()) {
            const GraphIndex::Page& p = index.pages()[t.page];
            d = dayOf.insert(t.page, p.journal ? QDate::fromString(QFileInfo(p.path).completeBaseName(), journalFormat) : QDate());
        }
        if (!d->isValid() || *d >= today || (days > 0 && d->daysTo(today) > days)) continue;
        chunks.append({*d, t.block, {i}});
        carrying = true;
    }
    std::stable_sort(chunks.begin(), chunks.end(), [](const Chunk& a, const Chunk& b) {
        return a.day != b.day ? a.day < b.day : a.block < b.block;
    });
    QList<CarriedTask> out;
    for (const Chunk& c : chunks)
        for (int i : c.tasks) out.append({i, c.day, static_cast<int>(c.day.daysTo(today))});
    return out;
}

}  // namespace graphite
