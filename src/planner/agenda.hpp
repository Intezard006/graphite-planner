// The desktop agenda: exams, deadlines and today's timetable as one small TOML text, for the
// desktop (wallpaper widget, lock screen, bar) to watch. Pure functions over the GraphIndex,
// no widgets; the file itself is written by desktop/agendafile.*.
//
//   written = "2026-11-12T20:31:05"
//   [[exam]]      name, date, time?, end?
//   [[deadline]]  text, date, time?
//   [[today]]     date, start, end?, text
//
// Only `key = "string"` lines and [[array]] tables; keys for unknown values are left out.
// No relative values ("in 2 days") anywhere: readers compute those.
#pragma once

#include <QDateTime>
#include <QString>

namespace graphite {

class GraphIndex;

constexpr int kAgendaMaxExams = 20, kAgendaMaxDeadlines = 20, kAgendaMaxToday = 30;

// The agenda text at `now`. `graphDir` + `journalFormat` find today's journal.
QString buildAgenda(const GraphIndex& index, const QDateTime& now, const QString& graphDir,
                    const QString& journalFormat = "yyyy_MM_dd");

// A line of Logseq text as plain text: no [[links]] / [text](url), #tags, \ce{} / $maths$ marks,
// bold / italic / code marks, {progress} bars, [#A] priorities (after shortTaskText's work).
QString plainText(const QString& line);
// A value as the inside of a TOML basic string: control characters become spaces, \ and "
// are escaped.
QString tomlEscape(const QString& value);

// The journal file name format from logseq/config.edn (default yyyy_MM_dd).
QString readJournalFormat(const QString& graphDir);
// Headless: the pages of a graph folder (journals/ and every book), read into an index without
// any window. `journalFormat` gets the format used.
GraphIndex loadGraphIndex(const QString& graphDir, QString* journalFormat = nullptr);

}  // namespace graphite
