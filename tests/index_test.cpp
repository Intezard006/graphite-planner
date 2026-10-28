// The link index and search: what counts as a reference, incremental re-reading, ordering.
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <cmath>
#include <cstdio>

#include "core/document.hpp"
#include "notes/graphindex.hpp"
#include "planner/operation.hpp"
#include "planner/tasks.hpp"
#include "planner/timetable.hpp"
#include "maths/graph.hpp"
#include "maths/calc.hpp"
#include "chem/balance.hpp"
#include "maths/labtable.hpp"
#include "chem/oxidation.hpp"
#include "planner/progress.hpp"
#include "chem/molar.hpp"
#include "desktop/updates.hpp"

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

static void write(const QString& path, const QString& text, int ageSeconds = 0) {
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    if (f.open(QIODevice::WriteOnly)) f.write(text.toUtf8());
    f.close();
    // Distinct, explicit times so "changed on disk" never depends on how fast the test runs.
    QFile g(path);
    if (g.open(QIODevice::ReadWrite))
        g.setFileTime(QDateTime::currentDateTime().addSecs(-ageSeconds), QFileDevice::FileModificationTime);
}

static QString refs(const QString& text) { return GraphIndex::referencesIn(text).join("|"); }

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);

    // 1. What counts as a reference.
    CHECK_EQ(refs("see [[Group Theory]] and [[group theory]]"), "Group Theory");  // once, first spelling
    CHECK_EQ(refs("from [[eq:arrhenius]] and [[Kinetics]]"), "Kinetics");  // equation links are not pages
    CHECK_EQ(refs("#kinetics and #[[Main Group]] here"), "Main Group|kinetics");  // [[...]] forms first
    CHECK_EQ(refs("ends with a tag #exam."), "exam");
    CHECK_EQ(refs("C# and page#anchor and https://x.org/#frag"), "");  // only at the start of a word
    CHECK_EQ(refs("`[[not a link]]` and $#x$ but [[yes]]"), "yes");      // code and maths are skipped
    CHECK_EQ(refs("```\n[[in a fence]]\n```\n[[after]]"), "after");
    CHECK_EQ(refs("tags:: exam, [[Physical Chem]], #lab"), "exam|Physical Chem|lab");
    CHECK_EQ(refs("## Heading with no tag"), "");
    CHECK_EQ(refs("(#paren) first"), "paren");

    // 2. Reading a graph.
    QTemporaryDir dir;
    const QString g = dir.path();
    const QString j1 = g + "/journals/2026_10_17.md", j2 = g + "/journals/2026_10_18.md";
    const QString pa = g + "/pages/Group Theory.md", pb = g + "/pages/Bonding.md";
    write(j1, "- read [[Group Theory]] ch 2\n- nothing here", 300);
    write(j2, "- ## Plan\n\t- #[[Group Theory]] problems\n\t- also [[Bonding]]", 300);
    write(pa, "- Point groups\n- self link [[Group Theory]]", 300);
    write(pb, "tags:: exam\n\n- Ionic vs covalent, see [[group theory]]", 300);
    const QList<GraphIndex::File> files = {{j2, "Oct 18th, 2026", true}, {j1, "Oct 17th, 2026", true},
                                           {pb, "Bonding", false}, {pa, "Group Theory", false}};
    GraphIndex idx;
    idx.sync(files);
    CHECK(idx.pages().size() == 4);

    // Backlinks: other pages only, journals newest first, then pages A-Z; case-insensitive.
    QList<GraphIndex::Reference> bl = idx.backlinks("Group Theory");
    QStringList where;
    for (const auto& r : bl) where << idx.pages()[r.page].title + ":" + QString::number(r.block);
    CHECK_EQ(where.join(", "), "Oct 18th, 2026:1, Oct 17th, 2026:0, Bonding:1");
    CHECK(idx.backlinkCount("bonding") == 1);
    CHECK(idx.backlinkCount("exam") == 1);  // from tags::
    CHECK(idx.backlinkCount("nobody") == 0);

    // 3. Only changed files are re-read: an unchanged file keeps what was read before, even
    // if its content differs (same mtime), and a changed one is picked up.
    const QString before = idx.pages()[idx.pageOf(pa)].blocks.join("|");
    QFile::remove(j1);
    write(j1, "- now about [[Bonding]] only", 100);
    idx.sync(files);
    CHECK(idx.backlinkCount("Bonding") == 2);
    CHECK(idx.backlinks("Group Theory").size() == 2);
    CHECK_EQ(idx.pages()[idx.pageOf(pa)].blocks.join("|"), before);

    // A deleted file drops out.
    QList<GraphIndex::File> fewer = files;
    fewer.removeAt(1);
    QFile::remove(j1);
    idx.sync(fewer);
    CHECK(idx.pages().size() == 3 && idx.pageOf(j1) == -1);
    CHECK(idx.backlinkCount("Bonding") == 1);

    // 4. update() from an unsaved document wins over the file.
    Document doc = Document::parse("- typed but not saved: [[Bonding]] #new");
    idx.update(pa, "Group Theory", false, doc, idx.pages()[idx.pageOf(pa)].mtime);
    CHECK(idx.backlinkCount("Bonding") == 2);
    CHECK(idx.backlinkCount("new") == 1);
    idx.sync(fewer);  // file unchanged on disk: the in-memory version stays
    CHECK(idx.backlinkCount("new") == 1);

    // 5. Search: titles first (exact, then prefix, then words), then blocks.
    QList<GraphIndex::Hit> hits = idx.search("bonding");
    CHECK(!hits.isEmpty() && hits[0].block == -1 && idx.pages()[hits[0].page].title == "Bonding");
    CHECK(hits.size() >= 2 && hits[1].block >= 0);
    hits = idx.search("covalent ionic");  // words in any order
    CHECK(hits.size() == 1 && idx.pages()[hits[0].page].title == "Bonding");
    CHECK(idx.search("   ").isEmpty());
    CHECK(idx.search("zzz-nothing").isEmpty());
    CHECK(idx.search("o", 3).size() == 3);  // limit

    // 6. Tasks: markers, cycling, completing, dates and order.
    CHECK_EQ(cycleTask("buy ligands"), "TODO buy ligands");
    CHECK_EQ(cycleTask("TODO buy ligands"), "DOING buy ligands");
    CHECK_EQ(cycleTask("DOING buy ligands"), "DONE buy ligands");
    CHECK_EQ(cycleTask("DONE buy ligands"), "buy ligands");
    CHECK_EQ(cycleTask("LATER read ch 3"), "NOW read ch 3");
    CHECK_EQ(cycleTask(""), "TODO");
    CHECK_EQ(cycleTask("TODO"), "DOING");
    CHECK_EQ(completeTask("TODO a\nSCHEDULED: <2026-10-30 Fri>"), "DONE a\nSCHEDULED: <2026-10-30 Fri>");
    CHECK_EQ(taskMarker("TODOLIST is not a task"), "");
    CHECK_EQ(taskMarker("## TODO heading"), "TODO");  // Logseq: heading first, then the marker
    CHECK_EQ(cycleTask("## Exam"), "## TODO Exam");
    CHECK_EQ(cycleTask("## DONE Exam"), "## Exam");
    CHECK_EQ(completeTask("### LATER read"), "### DONE read");

    QTemporaryDir tdir;
    const QString tj = tdir.path() + "/journals/2026_10_20.md", tp = tdir.path() + "/pages/Lab.md";
    write(tj, "- TODO undated journal task\n- DONE finished\n- LATER someday", 50);
    write(tp, "- TODO report\n  DEADLINE: <2026-10-25 Sun>\n- DOING titration\n  SCHEDULED: <2026-10-23 Fri>\n- CANCELED no", 50);
    GraphIndex ti;
    ti.sync({{tj, "Oct 20th, 2026", true}, {tp, "Lab", false}});
    const QList<Task> tasks = collectTasks(ti);
    QStringList got;
    for (const Task& t : tasks) got << t.marker + " " + t.text + (t.due().isValid() ? " @" + t.due().toString("MM-dd") : QString());
    CHECK_EQ(got.join(" | "), "DOING titration @10-23 | TODO report @10-25 | TODO undated journal task | LATER someday");

    // Time blocks are tasks too ("18:00 TODO …", even as subtasks under a heading task); in a
    // journal without a date of their own they are due that day, earlier times first.
    {
        QTemporaryDir bdir;
        const QString bj = bdir.path() + "/journals/2026_10_25.md";
        write(bj, "- ## TODO Main Group\n\t- 18:00 TODO Slides Ge_Sn\n\t- TODO New slides\n"
                  "- ## TODO Supramolecular\n\t- 17:00 TODO Make notes\n\t- 16:00 TODO Look at slides\n\t- 15:00 DONE lunch", 50);
        GraphIndex bi;
        bi.sync({{bj, "Oct 25th, 2026", true}});
        QStringList seen;
        for (const Task& t : collectTasks(bi))
            seen << QString(t.level, '>') + t.text + (t.groupDue.isValid() ? " @" + t.groupDue.toString("MM-dd") : QString()) +
                        (t.time >= 0 ? QString(" t%1").arg(t.time) : QString());
        CHECK_EQ(seen.join(" | "), "Main Group @10-25 | >18:00 Slides Ge_Sn @10-25 t1080 | >New slides | "
                                   "Supramolecular @10-25 | >17:00 Make notes @10-25 t1020 | >16:00 Look at slides @10-25 t960");
    }

    // The week's timetable: day headings with time blocks under them, or a day in front.
    {
        int used = 0;
        CHECK(dayMask("Monday", &used) == 1 << 1 && used == 6);
        CHECK(dayMask("## Tue, Thu:") == ((1 << 2) | (1 << 4)));
        CHECK(dayMask("Mon-Fri") == 0b111110);
        CHECK(dayMask("Sat – Mon") == ((1 << 6) | (1 << 7) | (1 << 1)));
        CHECK(dayMask("mon / wed and fri 10:00 x", &used) == ((1 << 1) | (1 << 3) | (1 << 5)) && used == 18);
        CHECK(dayMask("Weekdays") == 0b111110 && dayMask("weekend") == 0b11000000 && dayMask("Daily") == 0b11111110);
        CHECK(dayMask("Monsoon notes") == 0);
        CHECK(dayMask("Saturated fats") == 0);
        CHECK(dayMask("09:00 x") == 0);

        QTemporaryDir tdir;
        const QString tp = tdir.path() + "/pages/Week.md", other = tdir.path() + "/pages/Notes.md";
        write(tp, "tags:: timetable\n\n- Monday\n\t- 14:00-17:00 Physical lab\n\t\t- room 204\n\t- 09:00-10:00 Organic\n"
                  "- Tue, Thu\n\t- 11:00 Spectroscopy\n- Sun 10:00-11:00 Seminar\n- 08:00 not under a day\n- Weekdays\n\t- 07:30 Run",
              50);
        write(other, "- Monday\n\t- 09:00 not a timetable", 50);
        GraphIndex ti;
        ti.sync({{tp, "Week", false}, {other, "Notes", false}});
        CHECK(isTimetablePage(ti, 0) && !isTimetablePage(ti, 1));
        CHECK(timetablePage(ti) == 0);
        auto day = [&](const QDate& d) {
            QStringList s;
            for (const TimetableEntry& e : timetableFor(ti, d))
                s << slotText({e.start, e.end}) + " " + e.text;
            return s.join(" | ");
        };
        CHECK_EQ(day(QDate(2026, 10, 26)), "07:30 Run | 09:00\u201310:00 Organic | 14:00\u201317:00 Physical lab");  // Monday
        CHECK_EQ(day(QDate(2026, 10, 29)), "07:30 Run | 11:00 Spectroscopy");                               // Thursday
        CHECK_EQ(day(QDate(2026, 10, 25)), "10:00\u201311:00 Seminar");                                     // Sunday
        CHECK_EQ(day(QDate(2026, 10, 31)), "");                                                              // Saturday
    }

    // Headings that follow their subtasks.
    {
        auto run = [](const char* page, QList<int> user = {}, TaskSyncOptions o = {}) {
            Document d = Document::parse(page);
            QList<int> fin;
            syncTasks(d, user, o, &fin);
            QString out = QString::fromStdString(d.serialize());
            if (!fin.isEmpty()) {
                QStringList f;
                for (int b : fin) f << QString::number(b);
                out += "  [finished " + f.join(",") + "]";
            }
            return out;
        };
        // All subtasks done: the heading finishes and folds; the times stay first.
        CHECK_EQ(run("- ## TODO Supramolecular\n\t- 17:00 DONE Make notes\n\t- 16:00 DONE Look at slides"),
                 "- ## DONE Supramolecular\n  collapsed:: true\n\t- 17:00 DONE Make notes\n\t- 16:00 DONE Look at slides  [finished 0]");
        // Without folding.
        TaskSyncOptions nofold;
        nofold.fold = false;
        CHECK_EQ(run("- ## TODO A\n\t- DONE x", {}, nofold), "- ## DONE A\n\t- DONE x  [finished 0]");
        // A new open subtask under a DONE heading reopens it: DOING when work has started, TODO
        // when not; a reopened heading unfolds.
        CHECK_EQ(run("- DONE A\n  collapsed:: true\n\t- DONE x\n\t- TODO new"), "- DOING A\n\t- DONE x\n\t- TODO new");
        CHECK_EQ(run("- DONE A\n\t- TODO new"), "- TODO A\n\t- TODO new");
        CHECK_EQ(run("- DONE A\n\t- LATER new"), "- LATER A\n\t- LATER new");  // the LATER/NOW workflow
        // Auto-start, and not with it switched off.
        CHECK_EQ(run("- TODO A\n\t- DOING x\n\t- TODO y"), "- DOING A\n\t- DOING x\n\t- TODO y");
        TaskSyncOptions nostart;
        nostart.start = false;
        CHECK_EQ(run("- TODO A\n\t- DOING x\n\t- TODO y", {}, nostart), "- TODO A\n\t- DOING x\n\t- TODO y");
        CHECK_EQ(run("- LATER A\n\t- NOW x\n\t- LATER y"), "- NOW A\n\t- NOW x\n\t- LATER y");
        // Marking the heading DONE yourself closes its open subtasks (and it is not rolled back).
        CHECK_EQ(run("- DONE A\n\t- TODO x\n\t\t- TODO deep\n\t- note", {0}),
                 "- DONE A\n  collapsed:: true\n\t- DONE x\n\t\t- DONE deep\n\t- note  [finished 0]");
        // Your own change to a heading is not overridden: TODO stays TODO over done subtasks.
        CHECK_EQ(run("- TODO A\n\t- DONE x", {0}), "- TODO A\n\t- DONE x");
        // Three levels: the grandparent finishes once its child heading does; plain blocks in
        // between do not break the link.
        CHECK_EQ(run("- TODO G\n\t- note\n\t\t- TODO P\n\t\t\t- DONE x", {}, nofold),
                 "- DONE G\n\t- note\n\t\t- DONE P\n\t\t\t- DONE x  [finished 2,0]");
        // CANCELED: all-canceled subtasks leave the heading alone; a canceled heading is never touched.
        CHECK_EQ(run("- TODO A\n\t- CANCELED x"), "- TODO A\n\t- CANCELED x");
        CHECK_EQ(run("- CANCELED A\n\t- DONE x"), "- CANCELED A\n\t- DONE x");
        CHECK_EQ(run("- TODO A\n\t- CANCELED x\n\t- DONE y", {}, nofold), "- DONE A\n\t- CANCELED x\n\t- DONE y  [finished 0]");
        // Untouched blocks keep their bytes (odd spacing, properties).
        CHECK_EQ(run("- TODO A\n\t- DONE x\n- other  block\n  id:: 1234", {}, nofold), "- DONE A\n\t- DONE x\n- other  block\n  id:: 1234  [finished 0]");
        // Everything off: nothing changes.
        TaskSyncOptions off;
        off.finish = off.start = false;
        CHECK_EQ(run("- TODO A\n\t- DONE x", {}, off), "- TODO A\n\t- DONE x");
        // Counts for the progress label: every task below counts.
        const QHash<int, QPair<int, int>> counts = subtaskCounts({0, 1, 2, 1}, {"TODO A", "DONE x", "TODO y", "CANCELED z"});
        CHECK(counts.value(0) == qMakePair(2, 3) && counts.value(1) == qMakePair(0, 1) && !counts.contains(2));
    }

    // Graphs of functions: roots, turning points, asymptotes, meetings, units.
    {
        CHECK_EQ(graphSpec("=graph{x^2, x from 0 to 1}"), "x^2, x from 0 to 1");
        CHECK(graphSpec("=graph{}").isEmpty() && !graphSpec("=graph{}").isNull() && graphSpec("= graph x").isNull());
        auto marks = [](const Graph& g) {
            QStringList s;
            for (const GraphMark& m : g.marks)
                s << QString("%1 %2,%3").arg("RXNC"[m.kind]).arg(calcNumber(std::fabs(m.x) < 1e-9 ? 0 : m.x),
                                                                 calcNumber(std::fabs(m.y) < 1e-9 ? 0 : m.y));
            for (double p : g.poles) s << "P " + calcNumber(std::fabs(p) < 1e-9 ? 0 : p);
            return s.join(" | ");
        };
        const Graph q = makeGraph("x^2 - 2, x from -3 to 3");
        CHECK(q.ok);
        CHECK_EQ(marks(q), QString::fromUtf8("R \u22121.414,0 | N 0,\u22122 | R 1.414,0"));
        CHECK_EQ(graphSummary(q, false), QString::fromUtf8("Roots  x = −1.414, 1.414   ·   min  (0, −2)"));
        CHECK(graphSummary(q, true).startsWith("Target 01  x = "));
        CHECK_EQ(marks(makeGraph("1/x, x from -2 to 2")), "P 0");
        CHECK_EQ(marks(makeGraph("tan(x), x from 0.1 to 6")), "R 3.142,0 | P 1.571 | P 4.712");
        CHECK(marks(makeGraph("sin(x); cos(x), x from 0 to 2*pi")).count("C ") == 2);
        const Graph both = makeGraph("sin(x); cos(x), x from 0 to 2*pi");
        bool quarter = false;
        for (const GraphMark& m : both.marks) quarter = quarter || (m.kind == GraphMark::Cross && std::fabs(m.x - M_PI / 4) < 1e-6);
        CHECK(quarter);
        CHECK_EQ(marks(makeGraph("sqrt(x), x from -1 to 4")), "");  // undefined below 0, no asymptote
        CHECK_EQ(marks(makeGraph("2^x - 8, x from 0 to 5")), "R 3,0");
        const Graph k = makeGraph("k = A*exp(-Ea/(R*T)), T from 250 K to 400 K, A = 1e10 s^-1, Ea = 50 kJ/mol");
        CHECK(k.ok);
        CHECK_EQ(k.xName + " / " + k.xUnit + " | " + k.yName + " / " + k.yUnit, QString::fromUtf8("T / K | k / s⁻¹"));
        CHECK(std::fabs(k.x0 - 250) < 1e-9 && std::fabs(k.x1 - 400) < 1e-9);
        CHECK(std::fabs(k.curves[0].y.back() * k.yScale / (1e10 * std::exp(-50000 / (8.314462618 * 400))) - 1) < 1e-9);
        const Graph c = makeGraph("T to degC, T from 0 degC to 100 degC");
        CHECK(!c.ok || c.xUnit.contains("C"));
        const Graph given = makeGraph("1/x, x from -2 to 2, y from -10 to 10");
        CHECK(given.ok && given.yGiven && given.y0 == -10 && given.y1 == 10);
        CHECK(!makeGraph("").ok && makeGraph("").error.contains("=graph{x^2"));
        CHECK(makeGraph("x^2, x from 5 to 1").error.contains("must go up"));
        CHECK(makeGraph("x + 1 m, x from 0 to 1").error.length() > 0);
        CHECK(makeGraph("x m; x s, x from 0 to 1").error.contains("different kinds"));
        std::printf("graph summary: %s\n", qPrintable(graphSummary(makeGraph("tan(x), x from 0.1 to 6"), true)));
    }

    // Dates on the task's own line count too, and stay out of its text.
    {
        QTemporaryDir idir;
        const QString ip = idir.path() + "/pages/Inline.md";
        write(ip, "- TODO lab report DEADLINE: <2026-11-02 Mon>\n- TODO read SCHEDULED: <2026-11-01 Sun> ch 4", 50);
        GraphIndex ii;
        ii.sync({{ip, "Inline", false}});
        const QList<Task> it = collectTasks(ii);
        CHECK(it.size() == 2);
        if (it.size() == 2) {
            CHECK_EQ(it[0].text, "read ch 4");
            CHECK(it[0].due() == QDate(2026, 11, 1));
            CHECK_EQ(it[1].text, "lab report");
            CHECK(it[1].deadline == QDate(2026, 11, 2));
        }
        CHECK(blockDue("x SCHEDULED: <2026-11-01 Sun> DEADLINE: <2026-10-30 Fri>") == QDate(2026, 10, 30));
    }

    // Deadlines with a time, and their countdown.
    CHECK(blockDeadlineTime("TODO x DEADLINE: <2026-10-25 Sun 14:05>") == QDateTime(QDate(2026, 10, 25), QTime(14, 5)));
    CHECK(!blockDeadlineTime("TODO x DEADLINE: <2026-10-25 Sun>").isValid());
    {
        const QDateTime now(QDate(2026, 10, 23), QTime(10, 47, 30));
        CHECK_EQ(countdownText(QDateTime(QDate(2026, 10, 23), QTime(14, 0)), now), QString::fromUtf8("T\xE2\x88\x92") + "03H 13M");
        CHECK_EQ(countdownText(QDateTime(QDate(2026, 10, 25), QTime(10, 47, 30)), now), QString::fromUtf8("T\xE2\x88\x92") + "48H 00M");
        CHECK_EQ(countdownText(QDateTime(QDate(2026, 10, 23), QTime(9, 40)), now), "LIMIT +01H 07M");
    }
    // Subtasks: indented under a task; done ones count towards the parent's ratio.
    {
        QTemporaryDir sdir;
        const QString sp = sdir.path() + "/pages/Sub.md";
        write(sp, "- TODO parent\n\t- note\n\t\t- TODO deep child\n\t- DONE child done\n- DONE closed\n\t- TODO orphan\n- TODO later root", 50);
        GraphIndex si;
        si.sync({{sp, "Sub", false}});
        const QList<Task> st = collectTasks(si);
        QStringList got2;
        for (const Task& t : st) got2 << QString("%1%2(%3/%4)").arg(QString(t.level, '>'), t.text).arg(t.subDone).arg(t.subTotal);
        CHECK_EQ(got2.join(" | "), "parent(1/2) | >deep child(0/0) | orphan(0/0) | later root(0/0)");
    }

    // Quick maths with units ("= ..." lines).
    {
        auto val = [](const char* e) {
            const CalcResult r = evaluate(QString::fromUtf8(e));
            return r.ok ? r.value : "ERR " + r.error;
        };
        CHECK_EQ(val("0.5 mol * 58.44 g/mol"), "29.22 g");
        CHECK_EQ(val("2.5 g / M(CuSO4.5H2O)"), "10.01 mmol");
        CHECK_EQ(val("8.314 J/(mol K) * 298 K"), "2.478 kJ/mol");
        CHECK_EQ(val("R * 298 K to kJ/mol"), "2.478 kJ/mol");
        CHECK_EQ(val("2 atm * 3 L"), "608.0 J");  // 607.95: four figures, the zero counts
        CHECK_EQ(val("25 \u00B0C to K"), "298.1 K");
        CHECK_EQ(val("300 K to \u00B0C"), QString::fromUtf8("26.85 \u00B0C"));
        CHECK_EQ(val("1 mol * NA"), QString::fromUtf8("6.022\u00D710\u00B2\u00B3"));
        CHECK_EQ(val("-log(1e-3 M)"), "3");
        CHECK_EQ(val("0.1 M * 250 mL"), "25 mmol");
        CHECK_EQ(val("500 mL + 1 L"), "1500 mL");
        CHECK_EQ(val("10 g / 2 L / M(NaCl)"), "85.56 mM");
        CHECK_EQ(val("1 kcal to kJ"), "4.184 kJ");
        CHECK_EQ(val("(2 + 3) * 4"), "20");
        CHECK_EQ(val("2^10"), "1024");
        CHECK_EQ(val("2^0.5"), "1.414");          // any power of a plain number
        CHECK_EQ(val("8^(1/3)"), "2");
        CHECK_EQ(val("10^-2.5"), "0.003162");
        CHECK_EQ(val("(2 m)^2"), QString::fromUtf8("4 m\u00B2"));
        CHECK(val("(2 m)^0.5").startsWith("ERR"));  // a unit takes whole powers only
        CHECK(val("2^(1 m)").startsWith("ERR"));
        CHECK_EQ(val("1 cm3 to mL"), "1 mL");
        CHECK_EQ(val("6.02\u00D710\u00B2\u00B3 * 2"), QString::fromUtf8("1.204\u00D710\u00B2\u2074"));
        CHECK(val("2 g + 3 mol").startsWith("ERR"));
        CHECK(val("5 gm").contains("unknown unit 'gm'"));
        CHECK(val("1 g to L").startsWith("ERR"));
        CHECK(val("M(xyz)").startsWith("ERR"));
        CHECK_EQ(calcExpression("= 1 + 1"), "1 + 1");
        CHECK(calcExpression("x = 5").isNull());
        CHECK_EQ(calcTex(QString::fromUtf8("6.022\u00D710\u00B2\u00B3 mol\u207B\u00B9")), "=\\ 6.022\\times 10^{23}\\ \\mathrm{mol^{-1}}");
    }

    // Balancing reactions (\ce insides): atoms and charge, smallest whole coefficients.
    {
        auto bal = [](const char* e) {
            const BalanceResult b = balanceEquation(QString::fromUtf8(e));
            return b.ok ? (b.already ? "SAME " : "") + b.ce : "ERR " + b.error;
        };
        CHECK_EQ(bal("Fe + O2 -> Fe2O3"), "4Fe + 3O2 -> 2Fe2O3");
        CHECK_EQ(bal("H2 + O2 -> H2O"), "2H2 + O2 -> 2H2O");
        CHECK_EQ(bal("2H2 + O2 -> 2H2O"), "SAME 2H2 + O2 -> 2H2O");
        CHECK_EQ(bal("C3H8 + O2 -> CO2 + H2O"), "C3H8 + 5O2 -> 3CO2 + 4H2O");
        CHECK_EQ(bal("Cu + HNO3 -> Cu(NO3)2 + NO + H2O"), "3Cu + 8HNO3 -> 3Cu(NO3)2 + 2NO + 4H2O");
        CHECK_EQ(bal("KMnO4 + HCl -> KCl + MnCl2 + H2O + Cl2"), "2KMnO4 + 16HCl -> 2KCl + 2MnCl2 + 8H2O + 5Cl2");
        CHECK_EQ(bal("MnO4- + Fe^2+ + H+ -> Mn^2+ + Fe^3+ + H2O"), "MnO4- + 5Fe^2+ + 8H+ -> Mn^2+ + 5Fe^3+ + 4H2O");
        CHECK_EQ(bal("Cr2O7^2- + H+ + e- -> Cr^3+ + H2O"), "Cr2O7^2- + 14H+ + 6e- -> 2Cr^3+ + 7H2O");
        CHECK_EQ(bal("CuSO4.5H2O -> CuSO4 + H2O"), "CuSO4.5H2O -> CuSO4 + 5H2O");
        CHECK_EQ(bal("CaCO3(s) -> CaO(s) + CO2 ^"), "SAME CaCO3(s) -> CaO(s) + CO2 ^");
        CHECK_EQ(bal("N2 + H2 <=>[Fe] NH3"), "N2 + 3H2 <=>[Fe] 2NH3");
        CHECK_EQ(bal("[Fe(CN)6]^3- + e- -> [Fe(CN)6]^4-"), "SAME [Fe(CN)6]^3- + e- -> [Fe(CN)6]^4-");
        CHECK_EQ(bal("1/2 H2 + 1/2 Cl2 -> HCl"), "H2 + Cl2 -> 2HCl");
        CHECK(bal("H2O").startsWith("ERR no reaction"));
        CHECK(bal("H2 -> O2").startsWith("ERR"));
        CHECK(bal("Xq -> H2").startsWith("ERR can't read"));
        CHECK(bal("H2 + O2 -> H2O + H2O2").startsWith("ERR more than one"));
        int s = 0, n = 0;
        const QString text = "see $\\ce{H2O}$ and $\\ce{H2 + O2 -> H2O}$";
        CHECK(reactionAt(text, 0, &s, &n) && text.mid(s, n) == "H2 + O2 -> H2O");
        CHECK(!reactionAt("$\\ce{NaCl}$", 0, &s, &n));
        QMap<QString, int> atoms;
        int z = 0;
        CHECK(formulaAtoms("SO4^2-", &atoms, &z) && atoms.value("O") == 4 && atoms.value("S") == 1 && z == -2);
        CHECK(formulaAtoms("[Fe(CN)6]^{3-}", &atoms, &z) && atoms.value("C") == 6 && atoms.value("Fe") == 1 && z == -3);
        CHECK(formulaAtoms("Ca(OH)2", &atoms, &z) && atoms.value("H") == 2 && atoms.value("O") == 2 && z == 0);
        CHECK(formulaAtoms("NH4+", &atoms, &z) && atoms.value("H") == 4 && z == 1);
        double m = 0;
        CHECK(formulaMass("CuSO4.5H2O", &m) && std::abs(m - 249.68) < 0.05);
    }

    // Lab tables: computed columns, plots and fits.
    {
        LabTable t;
        CHECK(parseLabTable({"| m / g | V / mL | ρ = m/V to g/mL |", "|---|---|---|", "| 2.50 | 10.0 | |", "| 5.00 | 10.0 | |", "| 1.00 | | |"}, &t));
        CHECK(t.columns.size() == 3 && t.columns[0].name == "m" && t.columns[0].unit == "g" && t.columns[2].computed && t.columns[2].name == QString::fromUtf8("ρ"));
        CHECK(t.cells.size() == 3);
        CHECK_EQ(t.cells[0][2], "0.25");
        CHECK_EQ(t.cells[1][2], "0.5");
        CHECK(t.cells[2][2].isEmpty() && t.columns[2].error.isEmpty());  // an empty input: blank, no error
        CHECK_EQ(t.label(2), QString::fromUtf8("ρ (g/mL)"));
        // No "to": the first row's unit; cells can carry their own unit; later columns use earlier ones.
        CHECK(parseLabTable({"| n / mmol | M / g/mol | = n*M | x = n*2 |", "| 2 | 58.44 | | |", "| 0.5 mol | 58.44 | | |"}, &t));
        CHECK_EQ(t.columns[2].unit, "mg");  // the first row's: 116.9 mg
        CHECK_EQ(t.cells[0][2], "116.9");
        CHECK_EQ(t.cells[1][2], "29220");
        CHECK_EQ(t.cells[0][3], "4");
        CHECK(parseLabTable({"| a | = a * q |", "| 2 | |"}, &t) && t.columns[1].error.contains("q"));
        CHECK(!parseLabTable({"| a |", "not a row"}, &t));

        CHECK(plotSpec("= plot A vs c") == "A vs c" && plotSpec("= plot").isEmpty() && !plotSpec("= plot").isNull());
        CHECK(plotSpec("=plot{A vs c}") == "A vs c" && plotSpec("  =plot{ ln(k) vs 1/T }") == "ln(k) vs 1/T");
        CHECK(plotSpec("=plot{}").isEmpty() && !plotSpec("=plot{}").isNull() && plotSpec("=plot{A vs c") == "A vs c");
        CHECK(plotSpec("=plotting").isNull() && plotSpec("= 2 mol").isNull());
        CHECK(plotSpec("= 1 + 1").isNull() && plotSpec("plot").isNull());
        CHECK(parseLabTable({"| c / M | A |", "|--|--|", "| 0.1 | 0.12 |", "| 0.2 | 0.23 |", "| 0.3 | 0.35 |", "| 0.4 | 0.46 |"}, &t));
        LabPlot p = labPlot(t, "A vs c");
        CHECK(p.ok && p.fitted && p.points.size() == 4);
        CHECK(std::abs(p.slope - 1.14) < 1e-9 && std::abs(p.intercept - 0.005) < 1e-9 && p.r2 > 0.999);
        CHECK(p.summary.startsWith(QString::fromUtf8("slope 1.14 M⁻¹")));
        CHECK(p.xLabel == "c" && p.xUnit == "M");
        p = labPlot(t, "");  // the first two numeric columns, x first
        CHECK(p.ok && std::abs(p.slope - 1.14) < 1e-9);
        CHECK(!labPlot(t, "A c").ok);
        // Arrhenius: ln(k) against 1/T, Ea = 50 kJ/mol built in.
        QStringList rows = {"| T / K | k / s^-1 |"};
        for (double T : {300.0, 310.0, 320.0, 330.0})
            rows << QString("| %1 | %2 |").arg(T).arg(1e10 * std::exp(-50000.0 / (8.314462618 * T)), 0, 'g', 12);
        CHECK(parseLabTable(rows, &t));
        p = labPlot(t, "ln(k) vs 1/T");
        CHECK(p.ok && p.fitted && p.points.size() == 4 && p.xUnit == QString::fromUtf8("K⁻¹"));
        CHECK(p.summary.contains(QString::fromUtf8("Eₐ = −R·slope = 50 kJ/mol")) || p.summary.contains(QString::fromUtf8("Eₐ = −R·slope = 50.00 kJ/mol")));
    }

    // Time blocks.
    {
        TimeSlot t;
        CHECK(blockTime("09:00 TODO problem set", &t) && t.start == 540 && t.end == -1 && !t.done && t.at == 0 && t.length == 5);
        CHECK(blockTime("TODO 14:00-15:30 lab", &t) && t.start == 840 && t.end == 930 && t.at == 5);
        CHECK_EQ(slotText(t), QString::fromUtf8("14:00\u201315:30"));
        CHECK(blockTime("DONE 7.30 run", &t) && t.start == 450 && t.done);
        CHECK(blockTime(QString::fromUtf8("16:00\u201317:00 call"), &t) && t.end == 1020);
        CHECK(blockTime("## 11:15 seminar", &t) && t.start == 675);
        CHECK(!blockTime("at 09:00 the lab", &t) && !blockTime("24:00 x", &t) && !blockTime("9:5 x", &t) && !blockTime("10:30am", &t));
        CHECK(blockTime("23:00-01:00 x", &t) && t.end == -1);
        // The time may come first: still a task, and cycling keeps the time in front.
        CHECK(blockTime("09:00 DONE problem set", &t) && t.done);
        CHECK_EQ(taskMarker("09:00 TODO problem set"), "TODO");
        CHECK_EQ(taskMarker("14:00-15:30 DOING lab"), "DOING");
        CHECK_EQ(taskMarker("09:00 problem set"), "");
        CHECK_EQ(cycleTask("09:00 TODO problem set"), "09:00 DOING problem set");
        CHECK_EQ(cycleTask("09:00 problem set"), "09:00 TODO problem set");
        CHECK_EQ(cycleTask("09:00 DONE problem set"), "09:00 problem set");
        CHECK_EQ(completeTask("## 11:15 TODO seminar"), "## 11:15 DONE seminar");
    }

    // Oxidation states and redox.
    {
        auto ox = [](const char* s) { return oxidationLine(QString::fromUtf8(s)); };
        const QString dot = QString::fromUtf8("  \u00B7  "), minus = QString(QChar(0x2212));
        CHECK_EQ(ox("KMnO4"), "K +1" + dot + "Mn +7" + dot + "O " + minus + "2");
        CHECK_EQ(ox("SO4^2-"), "S +6" + dot + "O " + minus + "2");
        CHECK_EQ(ox("Cr2O7^2-"), "Cr +6" + dot + "O " + minus + "2");
        CHECK_EQ(ox("H2O2"), "H +1" + dot + "O " + minus + "1");
        CHECK_EQ(ox("NaH"), "Na +1" + dot + "H " + minus + "1");
        CHECK_EQ(ox("OF2"), "O +2" + dot + "F " + minus + "1");
        CHECK_EQ(ox("Fe3O4"), "Fe +8/3" + dot + "O " + minus + "2");
        CHECK_EQ(ox("NH4+"), "N " + minus + "3" + dot + "H +1");
        CHECK_EQ(ox("ClO3-"), "Cl +5" + dot + "O " + minus + "2");
        CHECK_EQ(ox("O2"), "O 0");
        CHECK_EQ(ox("Fe^3+"), "Fe +3");
        CHECK_EQ(ox("C6H12O6"), "C 0" + dot + "H +1" + dot + "O " + minus + "2");
        CHECK_EQ(ox("K4[Fe(CN)6]"), "K +1" + dot + "N " + minus + "3");  // C and Fe: no rule separates them
        CHECK(ox("e-").isEmpty() && ox("hello").isEmpty());

        RedoxReport r = redoxReport("MnO4- + Fe^2+ + H+ -> Mn^2+ + Fe^3+ + H2O");
        CHECK(r.changes.size() == 2);
        CHECK(r.changes.contains("Mn +7 " + QString::fromUtf8("→") + " +2 (reduced)"));
        CHECK(r.changes.contains("Fe +2 " + QString::fromUtf8("→") + " +3 (oxidised)"));
        CHECK_EQ(r.reduction, "MnO4- + 8H+ + 5e- -> Mn^2+ + 4H2O");
        CHECK_EQ(r.oxidation, "Fe^2+ -> Fe^3+ + e-");
        r = redoxReport("Zn + Cu^2+ -> Zn^2+ + Cu");
        CHECK_EQ(r.oxidation, "Zn -> Zn^2+ + 2e-");
        CHECK_EQ(r.reduction, "Cu^2+ + 2e- -> Cu");
        r = redoxReport("Cr2O7^2- + I- + H+ -> Cr^3+ + I2 + H2O");
        CHECK_EQ(r.reduction, "Cr2O7^2- + 14H+ + 6e- -> 2Cr^3+ + 7H2O");
        CHECK_EQ(r.oxidation, "2I- -> I2 + 2e-");
        CHECK(redoxReport("NaOH + HCl -> NaCl + H2O").changes.isEmpty());
        r = redoxReport("H2 + O2 -> H2O");
        CHECK(r.changes.size() == 2);  // only H and O: then they count
    }

    // Updates: a source file newer than the binary, a newer version, a newer binary.
    {
        CHECK(compareVersions("0.9.2", "0.10.0") < 0 && compareVersions("1.0", "1.0.0") == 0 && compareVersions("0.15.0", "0.14.9") > 0);
        QTemporaryDir repo;
        QDir(repo.path()).mkpath("src/ui");
        QDir(repo.path()).mkpath(".git/refs/heads");
        auto write = [](const QString& p, const QByteArray& b) { QFile f(p); if (f.open(QIODevice::WriteOnly)) f.write(b); };
        write(repo.path() + "/CMakeLists.txt", "project(graphite VERSION 0.15.0 LANGUAGES CXX)\n");
        write(repo.path() + "/src/ui/a.cpp", "x");
        write(repo.path() + "/.git/HEAD", "ref: refs/heads/main\n");
        write(repo.path() + "/.git/refs/heads/main", "0123456789abcdef0123456789abcdef01234567\n");
        write(repo.path() + "/bin", "exe");
        const QDateTime past = QDateTime::currentDateTime().addSecs(-3600);
        auto touch = [](const QString& p, const QDateTime& t) {
            QFile f(p);
            if (f.open(QIODevice::ReadWrite)) f.setFileTime(t, QFileDevice::FileModificationTime);
        };
        for (const char* f : {"/CMakeLists.txt", "/src/ui/a.cpp"}) touch(repo.path() + f, past.addSecs(-60));
        touch(repo.path() + "/bin", past);
        UpdateInfo u = checkForUpdate(repo.path(), repo.path() + "/bin", past);
        CHECK(u.repo && u.repoVersion == "0.15.0" && u.branch == "main" && u.commit == "0123456");
        CHECK(u.changed.isEmpty() && !u.rebuilt);
        touch(repo.path() + "/src/ui/a.cpp", past.addSecs(60));
        u = checkForUpdate(repo.path(), repo.path() + "/bin", past);
        CHECK(u.changed == QStringList{"src/ui/a.cpp"});
        touch(repo.path() + "/bin", past.addSecs(120));  // installed from it since
        u = checkForUpdate(repo.path(), repo.path() + "/bin", past);
        CHECK(u.changed.isEmpty() && u.rebuilt);
        CHECK(!checkForUpdate(repo.path() + "/nowhere", repo.path() + "/bin", past).repo);
    }

    // 7. Operation timer durations.
    CHECK(parseDuration("20m 15s") == 20 * 60 + 15);
    CHECK(parseDuration("25") == 25 * 60);
    CHECK(parseDuration("1h 5m") == 3900);
    CHECK(parseDuration("90s") == 90);
    CHECK(parseDuration("12:30") == 12 * 60 + 30);
    CHECK(parseDuration("1:05:00") == 3900);
    CHECK(parseDuration("45 min") == 45 * 60);
    CHECK(parseDuration("soon") == 0 && parseDuration("") == 0 && parseDuration("0") == 0);
    CHECK(parseDuration("13h") == 0);  // over twelve hours
    CHECK_EQ(formatDuration(1215), "20:15");
    CHECK_EQ(formatDuration(3900), "1:05:00");
    int at = -1;
    CHECK_EQ(timerCommand("notes /timer 20m 15s", &at), "20m 15s");
    CHECK(at == 6);
    CHECK_EQ(timerCommand("/timer stop"), "stop");
    CHECK(timerCommand("/timer").isEmpty() && !timerCommand("/timer").isNull());
    CHECK(timerCommand("see http://x/timer 5").isNull());  // not at the start of a word
    CHECK(timerCommand("no command").isNull());

    // Progress bars: parse, step (clamped, decimals kept tidy), the marker follows.
    {
        Progress p;
        CHECK(findProgress("TODO Read ch. 12 {progress 45/120 pages from 30 by 5} tonight", &p));
        CHECK(p.value == 45 && p.end == 120 && p.start == 30 && p.step == 5 && p.unit == "pages");
        CHECK(qAbs(p.ratio() - 0.1666667) < 1e-6);
        CHECK_EQ(progressNumbers(p), QString("45 / 120 PAGES"));
        CHECK(findProgress("{progress 3/10}", &p) && p.start == 0 && p.step == 1 && p.unit.isEmpty());
        CHECK(findProgress("{progress 2.5/10 % by 0.5}", &p) && p.unit == "%" && p.step == 0.5);
        CHECK(!findProgress("{progress three/ten}", &p));
        CHECK_EQ(stepProgress("x {progress 3/10}", 1), QString("x {progress 4/10}"));
        CHECK_EQ(stepProgress("x {progress 0/10}", -1), QString("x {progress 0/10}"));  // not below the start
        CHECK_EQ(stepProgress("x {progress 0.1/1 by 0.1}", 2), QString("x {progress 0.3/1 by 0.1}"));
        CHECK_EQ(stepProgress("x {progress 45/120 pages from 30 by 5}", 30), QString("x {progress 120/120 pages from 30 by 5}"));
        // the marker: TODO -> DOING on the first step, DONE at the end, back to DOING below it
        CHECK_EQ(stepProgress("TODO a {progress 0/2}", 1), QString("DOING a {progress 1/2}"));
        CHECK_EQ(stepProgress("09:00 DOING a {progress 1/2}", 1), QString("09:00 DONE a {progress 2/2}"));
        CHECK_EQ(stepProgress("## DONE a {progress 2/2}", -1), QString("## DOING a {progress 1/2}"));
        CHECK_EQ(stepProgress("TODO a {progress 0/2}", 1, true, false), QString("TODO a {progress 1/2}"));
        CHECK_EQ(stepProgress("TODO a {progress 1/2}", 1, false, true), QString("DOING a {progress 2/2}"));
        CHECK_EQ(stepProgress("plain {progress 1/2}", 1), QString("plain {progress 2/2}"));  // not a task: no marker
        CHECK_EQ(stepProgress("no bar here", 1), QString("no bar here"));
        CHECK_EQ(taskTitle("TODO Read {progress 3/12 by 1} SCHEDULED: <2026-10-30 Fri>"), QString("Read 25%"));
        CHECK(progressMessage("TODO Read {progress 12/12}", true).contains("Sync ratio 100%"));
        CHECK(progressMessage("TODO Read {progress 3/12}", false).startsWith("25%"));
    }

    // Carryover: open tasks of earlier journals within the range, dated or not, oldest day
    // first, each target with its open subtasks; today's journal and pages never count.
    {
        QTemporaryDir cdir;
        const QString j = cdir.path() + "/journals/";
        write(j + "2026_10_20.md", "- TODO nine days old", 50);
        write(j + "2026_10_22.md", "- TODO a week old", 50);
        write(j + "2026_10_27.md",
              "- ## TODO Heading\n\t- DONE closed sub\n\t- 18:00 TODO open sub\n- DOING started\n- LATER someday\n"
              "- NOW now\n- DONE finished\n- CANCELED dropped\n- WAITING waiting\n- plain block\n"
              "- TODO dated DEADLINE: <2026-11-05 Thu>\n- DONE done heading\n\t- TODO orphan sub",
              50);
        write(j + "2026_10_28.md", "- TODO yesterday {progress 3/12}", 50);
        write(j + "2026_10_29.md", "- TODO today itself", 50);
        write(j + "2026_10_30.md", "- TODO tomorrow", 50);
        write(cdir.path() + "/pages/Page.md", "- TODO on a page", 50);
        GraphIndex ci;
        QList<GraphIndex::File> src;
        for (const char* d : {"2026_10_20", "2026_10_22", "2026_10_27", "2026_10_28", "2026_10_29", "2026_10_30"})
            src.append({j + d + ".md", d, true});
        src.append({cdir.path() + "/pages/Page.md", "Page", false});
        ci.sync(src);
        const QList<Task> tasks = collectTasks(ci);
        const QDate today(2026, 10, 29);
        auto carried = [&](int days) {
            QStringList out;
            for (const CarriedTask& c : carryoverTasks(tasks, ci, today, days)) {
                const Task& t = tasks[c.task];
                out << QString("%1%2@%3/%4").arg(QString(t.level, '>'), t.text, c.day.toString("dd")).arg(c.daysOpen);
            }
            return out.join(" | ");
        };
        CHECK_EQ(carried(7),
                 "a week old@22/7 | Heading@27/2 | >18:00 open sub@27/2 | started@27/2 | someday@27/2 | now@27/2 | "
                 "waiting@27/2 | dated@27/2 | orphan sub@27/2 | yesterday 25%@28/1");
        CHECK(carried(3).startsWith("Heading@27/2"));     // 7 days back is out of a 3-day range
        CHECK(carried(1) == "yesterday 25%@28/1");
        CHECK(carried(0).startsWith("nine days old@20/9 | a week old@22/7"));  // 0: every past journal
        CHECK(!carried(0).contains("today itself") && !carried(0).contains("tomorrow") && !carried(0).contains("on a page"));
        // the journal's date comes from its file name, in the graph's format
        CHECK(carryoverTasks(tasks, ci, today, 7, "yyyy-MM-dd").isEmpty());
    }

    if (failures == 0) std::printf("all index tests passed\n");
    return failures == 0 ? 0 : 1;
}
