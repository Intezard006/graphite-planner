#include "planner/agenda.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <algorithm>

#include "notes/graphindex.hpp"
#include "planner/tasks.hpp"
#include "planner/timetable.hpp"

namespace graphite {

namespace {
QString hm(int minutes) { return QString("%1:%2").arg(minutes / 60, 2, 10, QChar('0')).arg(minutes % 60, 2, 10, QChar('0')); }

struct Out {
    QString text;
    void table(const char* name) { text += QString("\n[[%1]]\n").arg(name); }
    void kv(const char* key, const QString& value) { text += QString("%1 = \"%2\"\n").arg(key, tomlEscape(value)); }
};
}  // namespace

QString plainText(const QString& line) {
    static const QRegularExpression image(R"(!\[([^\]]*)\]\([^)]*\))");
    static const QRegularExpression link(R"(\[([^\]]+)\]\([^)]*\))");
    static const QRegularExpression tagPage(R"((^|\s)#\[\[[^\]]*\]\])");
    static const QRegularExpression tag(R"((^|\s)#[^\s#\[\](){}.,;:!?]+)");
    static const QRegularExpression page(R"(\[\[([^\]]*)\]\])");
    static const QRegularExpression chem(R"(\\(?:ce|pu)\{([^{}]*)\})");
    static const QRegularExpression maths(R"(\$\$?([^$]+)\$\$?)");
    static const QRegularExpression texCmd(R"(\\[A-Za-z]+|[{}\\])");
    static const QRegularExpression progress(R"(\{progress[^}]*\})");
    static const QRegularExpression priority(R"(\[#[A-C]\]\s*)");
    static const QRegularExpression marks(R"(\*\*|__|~~|\^\^|`)");
    static const QRegularExpression italic(R"(\*([^*\s][^*]*?)\*)");
    QString t = line;
    t.remove(progress).remove(priority);
    t.replace(image, "\\1").replace(link, "\\1");
    t.remove(tagPage).remove(tag);
    t.replace(page, "\\1");
    for (const QRegularExpression* re : {&chem, &maths}) {  // TeX commands go only inside \ce{} / $...$
        QString out;
        int last = 0;
        for (auto it = re->globalMatch(t); it.hasNext();) {
            const QRegularExpressionMatch m = it.next();
            out += t.mid(last, static_cast<int>(m.capturedStart()) - last) + QString(m.captured(1)).remove(texCmd);
            last = static_cast<int>(m.capturedEnd());
        }
        t = out + t.mid(last);
    }
    t.remove(marks);
    t.replace(italic, "\\1");
    return t.simplified();
}

QString tomlEscape(const QString& v) {
    QString out;
    out.reserve(v.size() + 8);
    for (const QChar c : v) {
        const ushort u = c.unicode();
        if (u < 0x20 || u == 0x7f || u == 0x2028 || u == 0x2029) out += ' ';
        else if (c == '\\') out += "\\\\";
        else if (c == '"') out += "\\\"";
        else out += c;
    }
    return out;
}

QString buildAgenda(const GraphIndex& index, const QDateTime& now, const QString& graphDir, const QString& journalFormat) {
    const QDate today = now.date();
    const QVector<GraphIndex::Page>& pages = index.pages();
    Out o;
    o.text = QString("written = \"%1\"\n").arg(now.toString("yyyy-MM-dd'T'HH:mm:ss"));

    // Exams: not done, not over (the "Upcoming exams" set), by date then time, untimed first.
    const QList<Exam> exams = collectExams(index);
    QSet<qint64> examBlocks;
    auto key = [](int page, int block) { return (qint64(page) << 32) | block; };
    auto examName = [&](const Exam& e) {
        const GraphIndex::Page& p = pages[e.page];
        QString name = plainText(examText(p.blocks[e.block]));
        const QString parent = e.parent >= 0 ? plainText(shortTaskText(p.blocks[e.parent])) : QString();
        if (!parent.isEmpty()) name = name.isEmpty() ? parent : name + ": " + parent;
        return name.isEmpty() ? QString("Exam") : name;
    };
    int n = 0;
    for (const Exam& e : exams) {
        examBlocks.insert(key(e.page, e.block));
        if (e.done || examOver(e) <= now || n >= kAgendaMaxExams) continue;
        ++n;
        o.table("exam");
        o.kv("name", examName(e));
        o.kv("date", e.date.toString(Qt::ISODate));
        if (e.slot.start >= 0) {
            o.kv("time", hm(e.slot.start));
            if (e.slot.end >= 0) o.kv("end", hm(e.slot.end));
        }
    }

    // Deadlines: open tasks with a DEADLINE today or later (exam blocks are listed above).
    struct Dl { QDate date; int minute; QString text; };
    QList<Dl> dls;
    for (const Task& t : collectTasks(index, journalFormat)) {
        if (!t.deadline.isValid() || t.deadline < today) continue;
        if (examBlocks.contains(key(t.page, t.block))) continue;
        QString text = plainText(shortTaskText(t.text));
        if (text.isEmpty()) text = "Deadline";
        dls.append({t.deadline, t.deadlineAt.isValid() ? t.deadlineAt.time().hour() * 60 + t.deadlineAt.time().minute() : -1, text});
    }
    std::stable_sort(dls.begin(), dls.end(), [](const Dl& a, const Dl& b) {
        return a.date != b.date ? a.date < b.date : a.minute < b.minute;
    });
    for (const Dl& d : dls.mid(0, kAgendaMaxDeadlines)) {
        o.table("deadline");
        o.kv("text", d.text);
        o.kv("date", d.date.toString(Qt::ISODate));
        if (d.minute >= 0) o.kv("time", hm(d.minute));
    }

    // Today: the timeline's timed rows (not done): today's journal, other pages' blocks
    // scheduled today, the timetable and timed exams. Deadlines are listed above.
    struct Row { int start, end; QString text; };
    QList<Row> rows;
    const QString journal = graphDir + "/journals/" + today.toString(journalFormat) + ".md";
    for (int pi = 0; pi < pages.size(); ++pi) {
        const GraphIndex::Page& p = pages[pi];
        const bool isToday = p.journal && p.path == journal;
        for (int bi = 0; bi < p.blocks.size(); ++bi) {
            const QString& b = p.blocks[bi];
            if (b.size() < 4 || examBlocks.contains(key(pi, bi))) continue;
            TimeSlot slot;
            if (!blockTime(b, &slot) || slot.done || !(isToday || blockDue(b) == today)) continue;
            const QString marker = taskMarker(b);
            if (marker == "DONE" || marker.startsWith("CANCEL")) continue;
            QString line = b.section('\n', 0, 0);
            line.remove(slot.at, slot.length);
            const QString text = plainText(shortTaskText(line));
            if (!text.isEmpty()) rows.append({slot.start, slot.end, text});
        }
    }
    for (const Exam& e : exams)
        if (e.date == today && !e.done && e.slot.start >= 0) rows.append({e.slot.start, e.slot.end, examName(e)});
    for (const TimetableEntry& e : timetableToday(index, today, journal)) {
        const QString text = plainText(shortTaskText(e.text));
        if (!text.isEmpty()) rows.append({e.start, e.end, text});
    }
    std::stable_sort(rows.begin(), rows.end(), [](const Row& a, const Row& b) {
        return a.start != b.start ? a.start < b.start : a.end < b.end;
    });
    // The same row twice (a journal block and the timetable entry for it, tags aside) counts once.
    for (int i = 0; i < rows.size(); ++i)
        for (int j = i + 1; j < rows.size();)
            if (rows[i].start == rows[j].start && rows[i].text.compare(rows[j].text, Qt::CaseInsensitive) == 0) rows.removeAt(j);
            else ++j;
    for (const Row& r : rows.mid(0, kAgendaMaxToday)) {
        o.table("today");
        o.kv("date", today.toString(Qt::ISODate));
        o.kv("start", hm(r.start));
        if (r.end >= 0) o.kv("end", hm(r.end));
        o.kv("text", r.text);
    }
    return o.text;
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

GraphIndex loadGraphIndex(const QString& graphDir, QString* journalFormat) {
    const QString fmt = readJournalFormat(graphDir);
    if (journalFormat) *journalFormat = fmt;
    QList<GraphIndex::File> files;
    QDir journals(graphDir + "/journals");
    for (const QString& f : journals.entryList({"*.md"}, QDir::Files, QDir::Name | QDir::Reversed)) {
        GraphIndex::File gf;
        gf.path = journals.absoluteFilePath(f);
        const QString base = QFileInfo(gf.path).completeBaseName();
        gf.journal = QDate::fromString(base, fmt).isValid();
        gf.title = base;
        files << gf;
    }
    static const QStringList reserved = {"journals", "logseq", "assets", "whiteboards", "draws", "bak"};
    const QDir root(graphDir);
    for (const QString& d : root.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name | QDir::IgnoreCase)) {
        if (d.startsWith('.') || reserved.contains(d)) continue;
        QDir dir(root.absoluteFilePath(d));
        for (const QString& f : dir.entryList({"*.md"}, QDir::Files, QDir::Name | QDir::IgnoreCase)) {
            GraphIndex::File gf;
            gf.path = dir.absoluteFilePath(f);
            gf.title = QFileInfo(gf.path).completeBaseName();
            if (d == "pages") gf.title.replace("___", "/");
            files << gf;
        }
    }
    GraphIndex index;
    index.sync(files);
    return index;
}

}  // namespace graphite
