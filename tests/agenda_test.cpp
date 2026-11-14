// Tests for the desktop agenda: the pure builder over a fixture graph, escaping, the caps, the
// atomic unchanged-skip writer, the nerv-focus hand-off and `graphite --agenda --print`.
// Never touches the real home, state or config: HOME, XDG_STATE_HOME and XDG_CONFIG_HOME are temp.
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMap>
#include <QProcess>
#include <QSettings>
#include <QTemporaryDir>
#include <QThread>
#include <cstdio>

#include "desktop/agendafile.hpp"
#include "notes/graphindex.hpp"
#include "planner/agenda.hpp"

using namespace graphite;

static int failures = 0;
#define CHECK(cond)                                                      \
    do {                                                                 \
        if (!(cond)) {                                                   \
            std::printf("FAIL line %d: %s\n", __LINE__, #cond);          \
            ++failures;                                                  \
        }                                                                \
    } while (0)
#define CHECK_EQ(a, b)                                                                              \
    do {                                                                                            \
        const QString va = (a), vb = (b);                                                           \
        if (va != vb) {                                                                             \
            std::printf("FAIL line %d: %s\n  got  %s\n  want %s\n", __LINE__, #a, qPrintable(va), qPrintable(vb)); \
            ++failures;                                                                             \
        }                                                                                           \
    } while (0)

static void write(const QString& path, const QString& text) {
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    if (f.open(QIODevice::WriteOnly)) f.write(text.toUtf8());
}
static QString read(const QString& path) {
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll()) : QString();
}

using Table = QMap<QString, QString>;
struct Parsed {
    QString written;
    QList<Table> exam, deadline, today;
    bool wellFormed = true;
};
// The contract's TOML subset: `key = "string"` lines and [[array]] tables, nothing else.
static Parsed parse(const QString& text) {
    Parsed p;
    QList<Table>* cur = nullptr;
    for (const QString& line : text.split('\n')) {
        if (line.isEmpty()) continue;
        if (line == "[[exam]]") { p.exam.append(Table()); cur = &p.exam; continue; }
        if (line == "[[deadline]]") { p.deadline.append(Table()); cur = &p.deadline; continue; }
        if (line == "[[today]]") { p.today.append(Table()); cur = &p.today; continue; }
        const int eq = static_cast<int>(line.indexOf(" = \""));
        if (eq <= 0 || !line.endsWith('"')) { p.wellFormed = false; continue; }
        const QString key = line.left(eq);
        QString value;
        const QString raw = line.mid(eq + 4, line.size() - eq - 5);
        for (int i = 0; i < raw.size(); ++i) {
            if (raw[i] == '\\' && i + 1 < raw.size()) value += raw[++i];
            else if (raw[i] == '"') p.wellFormed = false;  // unescaped quote
            else value += raw[i];
        }
        if (cur) cur->last()[key] = value;
        else if (key == "written") p.written = value;
        else p.wellFormed = false;
    }
    return p;
}

static QString runBin(const QStringList& args, const QStringList& env, int* code, QString* err = nullptr) {
    QProcess p;
    QProcessEnvironment e = QProcessEnvironment::systemEnvironment();
    e.remove("DISPLAY");
    e.remove("WAYLAND_DISPLAY");
    e.remove("QT_QPA_PLATFORM");
    for (const QString& kv : env) e.insert(kv.section('=', 0, 0), kv.section('=', 1));
    p.setProcessEnvironment(e);
    p.start(GRAPHITE_BIN, args);
    if (!p.waitForFinished(20000)) { p.kill(); p.waitForFinished(); *code = -99; return QString(); }
    *code = p.exitCode();
    if (err) *err = QString::fromUtf8(p.readAllStandardError());
    return QString::fromUtf8(p.readAllStandardOutput());
}

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName("graphite");
    QCoreApplication::setOrganizationName("graphite");
    QTemporaryDir fakeHome;
    CHECK(fakeHome.isValid());
    const QString home = fakeHome.path();
    qputenv("HOME", home.toUtf8());
    qputenv("XDG_STATE_HOME", (home + "/state").toUtf8());
    qputenv("XDG_CONFIG_HOME", (home + "/config").toUtf8());
    qunsetenv("GRAPHITE_NERV_FOCUS");

    // 1. Escaping and plain text.
    CHECK_EQ(tomlEscape("say \"hi\" C:\\dir\nnext\tline\r"), "say \\\"hi\\\" C:\\\\dir next line ");
    CHECK_EQ(plainText("**Lab** [[Kinetics]] [site](http://x.org) #exam `code` ~~old~~ [#A] {progress 1/4}"),
             "Lab Kinetics site code old");
    CHECK_EQ(plainText("Revise $\\ce{H2O}$ and \\ce{CO2} *now*"), "Revise H2O and CO2 now");
    CHECK_EQ(plainText("keep C:\\dir and #[[tag page]] out"), "keep C:\\dir and out");

    // 2. The fixture graph. Today is Thursday 2026-11-12, 10:00.
    const QString g = home + "/graph";
    write(g + "/journals/2026_11_12.md",
          "- 09:00-10:00 POC lecture #study\n"
          "- 13:00 Say \"hi\" to C:\\dir with **bold** [[Page]]\n"
          "- TODO 15:00 Lab prep\n"
          "- DONE 08:00 Gym\n"
          "- plain note today\n");
    write(g + "/pages/Exams.md",
          "- Equilibrium course\n"
          "\t- 10:00-11:00 Quiz #exam SCHEDULED: <2026-11-14 Sat>\n"
          "- Organic block SCHEDULED: <2026-11-20 Fri>\n"
          "\t- Viva #exam\n"
          "- Undated #exam without any date\n"
          "- DONE Old exam #exam SCHEDULED: <2026-11-13 Fri>\n"
          "- Past exam #exam SCHEDULED: <2026-11-10 Tue>\n"
          "- 08:00-09:00 Morning test #exam SCHEDULED: <2026-11-12 Thu>\n"
          "- 16:00-17:30 Evening test #exam SCHEDULED: <2026-11-12 Thu>\n");
    write(g + "/pages/Work.md",
          "- TODO Lab report 4 DEADLINE: <2026-11-15 Sun 14:00>\n"
          "- TODO Essay on \"bonding\" \\ draft DEADLINE: <2026-11-16 Mon>\n"
          "- DONE Handed in DEADLINE: <2026-11-16 Mon>\n"
          "- TODO Overdue DEADLINE: <2026-11-01 Sun>\n"
          "- TODO Sched only SCHEDULED: <2026-11-18 Wed>\n");
    write(g + "/pages/Timetable.md",
          "tags:: timetable\n\n"
          "- Thursday\n"
          "\t- 11:00-12:00 Spectroscopy\n"
          "\t- 09:00-10:00 POC lecture\n"  // the journal already has it
          "- Friday\n"
          "\t- 09:00-10:00 Not today\n");
    QString fmt;
    const GraphIndex index = loadGraphIndex(g, &fmt);
    CHECK_EQ(fmt, "yyyy_MM_dd");
    const QDateTime now(QDate(2026, 11, 12), QTime(10, 0));
    const QString text = buildAgenda(index, now, g, fmt);
    const Parsed a = parse(text);
    CHECK(a.wellFormed);
    CHECK_EQ(a.written, "2026-11-12T10:00:00");
    CHECK(text.startsWith("written = \"2026-11-12T10:00:00\"\n"));

    // Exams: not done, not over, date then time; " · " becomes ": ".
    CHECK_EQ(QString::number(a.exam.size()), "3");
    if (a.exam.size() == 3) {
        CHECK_EQ(a.exam[0]["name"], "Evening test");  // today 16:00
        CHECK_EQ(a.exam[0]["date"], "2026-11-12");
        CHECK_EQ(a.exam[0]["time"], "16:00");
        CHECK_EQ(a.exam[0]["end"], "17:30");
        CHECK_EQ(a.exam[1]["name"], "Quiz: Equilibrium course");
        CHECK_EQ(a.exam[1]["date"], "2026-11-14");
        CHECK_EQ(a.exam[1]["time"], "10:00");
        CHECK_EQ(a.exam[1]["end"], "11:00");
        CHECK_EQ(a.exam[2]["name"], "Viva: Organic block");  // date from its ancestor
        CHECK_EQ(a.exam[2]["date"], "2026-11-20");
        CHECK(!a.exam[2].contains("time") && !a.exam[2].contains("end"));
    }
    CHECK(!text.contains("Old exam") && !text.contains("Past exam"));
    CHECK(!text.contains("Undated"));
    CHECK(!text.contains(QString::fromUtf8("\xC2\xB7")));

    // Deadlines: open, today or later, date then time; none relative, none done.
    CHECK_EQ(QString::number(a.deadline.size()), "2");
    if (a.deadline.size() == 2) {
        CHECK_EQ(a.deadline[0]["text"], "Lab report 4");
        CHECK_EQ(a.deadline[0]["date"], "2026-11-15");
        CHECK_EQ(a.deadline[0]["time"], "14:00");
        CHECK_EQ(a.deadline[1]["text"], "Essay on \"bonding\" \\ draft");  // parsed back from the escaping
        CHECK(!a.deadline[1].contains("time"));
    }
    CHECK(text.contains("text = \"Essay on \\\"bonding\\\" \\\\ draft\""));
    CHECK(!text.contains("Handed in") && !text.contains("Overdue") && !text.contains("Sched only"));

    // Today: journal blocks, timed tasks, timetable (no twin of the journal's lecture), timed
    // exams; done ones and untimed notes absent.
    CHECK_EQ(QString::number(a.today.size()), "6");
    if (a.today.size() == 6) {
        CHECK_EQ(a.today[0]["start"] + "|" + a.today[0]["end"] + "|" + a.today[0]["text"], "08:00|09:00|Morning test");  // over, still today's row
        CHECK_EQ(a.today[1]["start"] + "|" + a.today[1]["end"] + "|" + a.today[1]["text"], "09:00|10:00|POC lecture");
        CHECK_EQ(a.today[2]["start"] + "|" + a.today[2]["text"], "11:00|Spectroscopy");
        CHECK_EQ(a.today[3]["start"] + "|" + a.today[3]["text"], "13:00|Say \"hi\" to C:\\dir with bold Page");
        CHECK_EQ(a.today[4]["start"] + "|" + a.today[4]["text"], "15:00|Lab prep");
        CHECK_EQ(a.today[5]["start"] + "|" + a.today[5]["text"], "16:00|Evening test");
        CHECK_EQ(a.today[0]["date"], "2026-11-12");
        CHECK(!a.today[2].contains("end") || a.today[2]["end"] == "12:00");
        CHECK(!a.today[3].contains("end") && !a.today[4].contains("end"));
    }
    CHECK(!text.contains("Gym") && !text.contains("Not today") && !text.contains("plain note"));
    CHECK(!text.contains("#") && !text.contains("TODO") && !text.contains("DEADLINE") && !text.contains("SCHEDULED"));

    // 3. An empty graph still has the file: just `written`.
    {
        QTemporaryDir empty;
        QString f;
        const QString t = buildAgenda(loadGraphIndex(empty.path(), &f), now, empty.path(), f);
        CHECK_EQ(t, "written = \"2026-11-12T10:00:00\"\n");
    }

    // 4. Caps: 20 exams, 20 deadlines, 30 today rows.
    {
        QTemporaryDir big;
        QString exams, dls, day;
        for (int i = 0; i < 25; ++i) {
            exams += QString("- Exam %1 #exam SCHEDULED: <2026-12-%2 Tue>\n").arg(i).arg(1 + i, 2, 10, QChar('0'));
            dls += QString("- TODO Task %1 DEADLINE: <2026-12-%2 Tue>\n").arg(i).arg(1 + i, 2, 10, QChar('0'));
        }
        for (int i = 0; i < 35; ++i) day += QString("- %1:%2 Slot %3\n").arg(8 + i / 4, 2, 10, QChar('0')).arg(i % 4 * 15, 2, 10, QChar('0')).arg(i);
        write(big.path() + "/pages/Exams.md", exams);
        write(big.path() + "/pages/Tasks.md", dls);
        write(big.path() + "/journals/2026_11_12.md", day);
        QString f;
        const Parsed b = parse(buildAgenda(loadGraphIndex(big.path(), &f), now, big.path(), f));
        CHECK(b.wellFormed);
        CHECK_EQ(QString::number(b.exam.size()), "20");
        CHECK_EQ(QString::number(b.deadline.size()), "20");
        CHECK_EQ(QString::number(b.today.size()), "30");
        CHECK_EQ(b.exam.first()["name"], "Exam 0");
        CHECK_EQ(b.today.last()["text"], "Slot 29");
    }

    // 5. The writer: atomic, 0600 file in a 0700 directory, skipped when only `written` differs.
    {
        const QString path = nervAgendaPath();
        CHECK_EQ(path, home + "/state/nerv/agenda.toml");
        CHECK(!QFileInfo::exists(path));
        CHECK(writeAgendaFile(text) == AgendaWrite::Written);
        CHECK_EQ(read(path), text);
        CHECK(!QFileInfo::exists(path + ".tmp"));
        CHECK((QFileInfo(path).permissions() & (QFileDevice::ReadGroup | QFileDevice::WriteGroup | QFileDevice::ReadOther | QFileDevice::WriteOther)) == 0);
        CHECK((QFileInfo(home + "/state/nerv").permissions() & (QFileDevice::ReadGroup | QFileDevice::ExeGroup | QFileDevice::ReadOther | QFileDevice::ExeOther)) == 0);
        QString later = text;
        later.replace("2026-11-12T10:00:00", "2026-11-12T10:05:00");
        QFile::setPermissions(path, QFileDevice::ReadOwner | QFileDevice::WriteOwner);
        const QDateTime before = QFileInfo(path).lastModified();
        QThread::msleep(30);
        CHECK(writeAgendaFile(later) == AgendaWrite::Unchanged);
        CHECK_EQ(read(path), text);  // untouched, old `written` still there
        CHECK(QFileInfo(path).lastModified() == before);
        CHECK(writeAgendaFile(later + "\n[[exam]]\nname = \"New\"\ndate = \"2026-12-30\"\n") == AgendaWrite::Written);
        CHECK(read(path).contains("name = \"New\"") && read(path).contains("10:05:00"));
        CHECK(!sameAgenda(text, text + "[[exam]]\n"));
        CHECK(sameAgenda("written = \"a\"\nx\n", "written = \"b\"\nx\n"));
    }

    // 6. nerv-focus: started detached with the right arguments; off with the setting.
    {
        const QString log = home + "/focus.log", fake = home + "/fake-nerv-focus";
        write(fake, "#!/bin/sh\nprintf '%s|' \"$@\" >> '" + log + "'\nprintf '\\n' >> '" + log + "'\n");
        QFile::setPermissions(fake, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
        qputenv("GRAPHITE_NERV_FOCUS", fake.toUtf8());
        CHECK(agendaEnabled());
        nervFocusStart(1500, "Revise \"kinetics\"");
        nervFocusStop();
        for (int i = 0; i < 60 && read(log).count('\n') < 2; ++i) QThread::msleep(50);
        // Two detached processes: either may finish first.
        CHECK(read(log).contains("start|--single|25|Revise \"kinetics\"|\n") && read(log).contains("stop|\n"));
        QSettings().setValue("desktop/agenda", false);
        CHECK(!agendaEnabled());
        QFile::remove(log);
        nervFocusStart(600, "x");
        QThread::msleep(400);
        CHECK(!QFileInfo::exists(log));
        qputenv("GRAPHITE_NERV_FOCUS", (home + "/missing").toUtf8());
        QSettings().setValue("desktop/agenda", true);
        nervFocusStop();  // absent program: silently nothing
        QThread::msleep(200);
        CHECK(!QFileInfo::exists(log));
        qunsetenv("GRAPHITE_NERV_FOCUS");
        QSettings().remove("desktop/agenda");
    }

    // 7. `graphite --agenda`: no window, no display; --print leaves the disk alone.
    {
        const QStringList env = {"HOME=" + home, "XDG_STATE_HOME=" + home + "/state2", "XDG_CONFIG_HOME=" + home + "/config"};
        int code = -1;
        QString err;
        const QString out = runBin({"--agenda", "--print", g}, env, &code, &err);
        CHECK(code == 0);
        const Parsed c = parse(out);
        CHECK(c.wellFormed);
        CHECK(out.startsWith("written = \""));
        CHECK(!QFileInfo::exists(home + "/state2"));
        // Written: the file appears; a second run leaves it alone.
        const QString out2 = runBin({"--agenda", g}, env, &code);
        CHECK(code == 0 && out2.startsWith("agenda written"));
        const QString file = home + "/state2/nerv/agenda.toml";
        CHECK(QFileInfo::exists(file));
        CHECK(parse(read(file)).wellFormed);
        QThread::msleep(1100);  // `written` has a one-second resolution
        CHECK(runBin({"--agenda", g}, env, &code).startsWith("agenda unchanged") && code == 0);
        // Setting off: nothing written, exit 0; --force writes anyway.
        QFile::remove(file);
        QSettings().setValue("desktop/agenda", false);
        QSettings().sync();
        CHECK(runBin({"--agenda", g}, env, &code).isEmpty() && code == 0);
        CHECK(!QFileInfo::exists(file));
        CHECK(runBin({"--agenda", "--force", g}, env, &code).startsWith("agenda written") && code == 0);
        CHECK(QFileInfo::exists(file));
        QSettings().remove("desktop/agenda");
        // No graph known: exit 1 (and no window).
        CHECK(runBin({"--agenda", "--print"}, {"HOME=" + home, "XDG_CONFIG_HOME=" + home + "/emptycfg"}, &code).isEmpty() && code == 1);
    }

    if (failures == 0) std::printf("all agenda tests passed\n");
    return failures == 0 ? 0 : 1;
}
