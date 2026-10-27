// Operations: the study timer started with "/timer 20m 15s". Pure helpers here; the running
// state lives in the window and the countdown in the NERV panel.
#pragma once

#include <QString>

namespace graphite {

// "20m 15s", "1h 5m", "90s", "25" (minutes), "12:30" (min:sec), "1:05:00" -> seconds.
// 0 for anything else (or more than 12 hours).
int parseDuration(const QString& text);

// 1215 -> "20:15", 3900 -> "1:05:00".
QString formatDuration(int seconds);

// "/timer 20m 15s" at the end of `line`: the arguments ("20m 15s", "stop"), or a null string.
QString timerCommand(const QString& line, int* start = nullptr);

}  // namespace graphite
