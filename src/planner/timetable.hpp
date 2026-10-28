// The week's timetable: one page (tagged `tags:: timetable`, or titled "Timetable") holding the
// lectures and labs that repeat every week. Today's entries join the day's time blocks: the
// Ctrl+D timeline, the NEXT row and the wake-up at their start and end.
//
//   tags:: timetable
//   - Monday
//       - 09:00-10:00 Organic chemistry lecture
//       - 14:00-17:00 Physical chemistry lab
//   - Tue, Thu
//       - 11:00-12:00 Spectroscopy
//   - Fri 10:00-11:00 Seminar          (a day in front of the time works too)
//
// Day names: Monday or Mon (Tue/Tues, Thu/Thur/Thurs...), lists ("Mon, Wed / Fri"), ranges
// ("Mon-Fri"), Weekdays, Weekends, Daily. Pure functions over the GraphIndex, no widgets.
#pragma once

#include <QDate>
#include <QList>
#include <QString>

namespace graphite {

class GraphIndex;

// Leading day names in `text` (after any "## "): a mask of Qt days (bit 1 = Monday ...
// bit 7 = Sunday), 0 when it does not start with one. `consumed`: the length read, spaces and a
// trailing ':' included.
int dayMask(const QString& text, int* consumed = nullptr);

struct TimetableEntry {
    int start = -1, end = -1;  // minutes since midnight; end -1: no range
    QString text;              // the first line without days and time
    int page = -1, block = -1; // where it is written (GraphIndex::pages())
};

// Is pages()[page] a timetable?
bool isTimetablePage(const GraphIndex& index, int page);
// The first timetable page, or -1.
int timetablePage(const GraphIndex& index);
// The entries for `date`'s weekday, in time order.
QList<TimetableEntry> timetableFor(const GraphIndex& index, const QDate& date);

// The text of a new timetable page: the properties and a block per day.
QString timetableTemplate();

}  // namespace graphite
