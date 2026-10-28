#include "planner/timetable.hpp"

#include "notes/graphindex.hpp"
#include "planner/tasks.hpp"

#include <QRegularExpression>
#include <algorithm>

namespace graphite {

namespace {

// One day name (or a group of them) at `at`: its mask and length, or 0.
int dayToken(const QString& text, int at, int* length) {
    static const QRegularExpression re(
        R"((?:monday|mon|tuesday|tues|tue|wednesday|wed|thursday|thurs|thur|thu|friday|fri|saturday|sat|sunday|sun|weekdays|weekends|weekend|daily|everyday|every day))"
        R"((?![A-Za-z]))",
        QRegularExpression::CaseInsensitiveOption);
    const QRegularExpressionMatch m = re.match(text, at, QRegularExpression::NormalMatch, QRegularExpression::AnchorAtOffsetMatchOption);
    if (!m.hasMatch()) return 0;
    *length = static_cast<int>(m.capturedLength());
    const QString w = m.captured().toLower();
    if (w.startsWith("weekday")) return 0b0111110;
    if (w.startsWith("weekend")) return 0b11000000;
    if (w == "daily" || w.startsWith("every")) return 0b11111110;
    static const char* const names[] = {"mon", "tue", "wed", "thu", "fri", "sat", "sun"};
    for (int d = 0; d < 7; ++d)
        if (w.startsWith(QLatin1String(names[d]))) return 1 << (d + 1);
    return 0;
}

int firstDay(int mask) {
    for (int d = 1; d <= 7; ++d)
        if (mask & (1 << d)) return d;
    return 0;
}

bool skipSpaces(const QString& t, int& i) {
    const int from = i;
    while (i < t.size() && (t[i] == ' ' || t[i] == '\t')) ++i;
    return i > from;
}

}  // namespace

int dayMask(const QString& text, int* consumed) {
    int i = 0;
    while (i < text.size() && text[i] == '#') ++i;
    if (i > 0 && !skipSpaces(text, i)) return 0;
    int mask = 0, len = 0;
    int end = i;
    while (const int one = dayToken(text, i, &len)) {
        i += len;
        int m = one;
        // A range: "Mon-Fri", "Mon – Fri"
        int j = i;
        skipSpaces(text, j);
        if (j < text.size() && (text[j] == '-' || text[j] == QChar(0x2013))) {
            ++j;
            skipSpaces(text, j);
            int len2 = 0;
            if (const int to = dayToken(text, j, &len2); to && firstDay(one) && firstDay(to)) {
                const int a = firstDay(one), b = firstDay(to);
                m = 0;
                for (int d = a;; d = d % 7 + 1) {  // wraps round: "Sat-Mon"
                    m |= 1 << d;
                    if (d == b) break;
                }
                i = j + len2;
            }
        }
        mask |= m;
        end = i;
        // The next one: "Mon, Wed", "Mon / Wed", "Mon & Wed", "Mon and Wed", "Mon Wed"
        skipSpaces(text, i);
        if (i < text.size() && (text[i] == ',' || text[i] == '/' || text[i] == '&')) {
            ++i;
        } else if (text.mid(i, 4).compare("and ", Qt::CaseInsensitive) == 0) {
            i += 3;
        }
        skipSpaces(text, i);
    }
    if (!mask) return 0;
    i = end;
    skipSpaces(text, i);
    if (i < text.size() && text[i] == ':') {
        ++i;
        skipSpaces(text, i);
    }
    if (consumed) *consumed = i;
    return mask;
}

bool isTimetablePage(const GraphIndex& index, int page) {
    static const QRegularExpression tag(R"(^\s*tags::.*\btimetable\b)",
                                        QRegularExpression::CaseInsensitiveOption | QRegularExpression::MultilineOption);
    const GraphIndex::Page& p = index.pages()[page];
    if (p.journal) return false;
    if (p.title.compare("Timetable", Qt::CaseInsensitive) == 0) return true;
    return !p.blocks.isEmpty() && p.blocks[0].contains("tags::") && tag.match(p.blocks[0]).hasMatch();
}

int timetablePage(const GraphIndex& index) {
    int titled = -1;
    for (int i = 0; i < index.pages().size(); ++i) {
        const GraphIndex::Page& p = index.pages()[i];
        if (p.journal || !isTimetablePage(index, i)) continue;
        if (p.title.compare("Timetable", Qt::CaseInsensitive) != 0) return i;  // the tag wins
        if (titled < 0) titled = i;
    }
    return titled;
}

QList<TimetableEntry> timetableFor(const GraphIndex& index, const QDate& date) {
    QList<TimetableEntry> out;
    if (!date.isValid()) return out;
    const int bit = 1 << date.dayOfWeek();
    for (int pi = 0; pi < index.pages().size(); ++pi) {
        if (!isTimetablePage(index, pi)) continue;
        const GraphIndex::Page& p = index.pages()[pi];
        int dayDepth = -1, days = 0;  // the day heading the blocks below belong to
        for (int bi = 0; bi < p.blocks.size(); ++bi) {
            const int depth = bi < p.depths.size() ? p.depths[bi] : 0;
            if (dayDepth >= 0 && depth <= dayDepth) {
                dayDepth = -1;
                days = 0;
            }
            QString line = p.blocks[bi].section('\n', 0, 0);
            int used = 0;
            int own = dayMask(line, &used);
            if (own) {
                line = line.mid(used);
                if (line.trimmed().isEmpty()) {  // a heading: "Monday", "## Tue, Thu"
                    dayDepth = depth;
                    days = own;
                    continue;
                }
            } else {
                own = days;
            }
            if (!(own & bit)) continue;
            TimeSlot slot;
            if (!blockTime(line, &slot)) continue;
            TimetableEntry e;
            e.start = slot.start;
            e.end = slot.end;
            e.text = QString(line).remove(slot.at, slot.length).simplified();
            e.page = pi;
            e.block = bi;
            out << e;
        }
    }
    std::stable_sort(out.begin(), out.end(), [](const TimetableEntry& a, const TimetableEntry& b) {
        return a.start != b.start ? a.start < b.start : a.end < b.end;
    });
    return out;
}

QString timetableTemplate() {
    return "tags:: timetable\n"
           "- Monday\n\t- 09:00-10:00 \n"
           "- Tuesday\n"
           "- Wednesday\n"
           "- Thursday\n"
           "- Friday\n"
           "- Saturday\n"
           "- Sunday\n";
}

}  // namespace graphite
