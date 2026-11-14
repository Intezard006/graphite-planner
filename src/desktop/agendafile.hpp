// The agenda file other desktop programs watch: $XDG_STATE_HOME/nerv/agenda.toml (fallback
// ~/.local/state/nerv/agenda.toml), written atomically (agenda.toml.tmp, then rename), 0600,
// in a 0700 directory. The text comes from planner/agenda.*. Also `graphite --agenda`, and the
// nerv-focus hand-off for operations (study timers).
#pragma once

#include <QString>
#include <QStringList>

namespace graphite {

QString nervAgendaPath();  // $XDG_STATE_HOME/nerv/agenda.toml

// QSettings desktop/agenda, default on: the export and the nerv-focus calls.
bool agendaEnabled();

enum class AgendaWrite { Written, Unchanged, Failed };
// Writes `text` unless the file already holds the same text apart from its `written` line
// (an idle Graphite causes no file churn and no inotify wakeups).
AgendaWrite writeAgendaFile(const QString& text, const QString& path = nervAgendaPath());
// Whether two agenda texts are equal ignoring the `written = ` line.
bool sameAgenda(const QString& a, const QString& b);

// `graphite --agenda [--print] [--force] [graph]`: no window. Needs a QCoreApplication (named
// graphite, so QSettings finds the configured graph). Returns the exit code.
int runAgendaCli(const QStringList& args);

// `nerv-focus` (GRAPHITE_NERV_FOCUS, else on PATH): started detached with `args`, never
// waited for, failures ignored. Nothing happens without the program or with the setting off.
void runNervFocus(const QStringList& args);
// Operation hooks: `nerv-focus start --single MIN "NAME"` / `nerv-focus stop`.
void nervFocusStart(int seconds, const QString& name);
void nervFocusStop();

}  // namespace graphite
