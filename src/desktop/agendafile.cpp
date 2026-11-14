#include "desktop/agendafile.hpp"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QSettings>
#include <QStandardPaths>
#include <algorithm>
#include <cstdio>

#include "notes/graphindex.hpp"
#include "planner/agenda.hpp"

namespace graphite {

QString nervAgendaPath() {
    QString base = qEnvironmentVariable("XDG_STATE_HOME");
    if (base.isEmpty() || !QDir::isAbsolutePath(base)) base = QDir::homePath() + "/.local/state";
    return base + "/nerv/agenda.toml";
}

bool agendaEnabled() { return QSettings().value("desktop/agenda", true).toBool(); }

namespace {
QString withoutWritten(const QString& text) {
    QStringList lines = text.split('\n');
    lines.erase(std::remove_if(lines.begin(), lines.end(), [](const QString& l) { return l.startsWith("written ="); }),
                lines.end());
    return lines.join('\n');
}
}  // namespace

bool sameAgenda(const QString& a, const QString& b) { return withoutWritten(a) == withoutWritten(b); }

AgendaWrite writeAgendaFile(const QString& text, const QString& path) {
    {
        QFile old(path);
        if (old.open(QIODevice::ReadOnly) && old.size() < (1 << 20) && sameAgenda(QString::fromUtf8(old.readAll()), text))
            return AgendaWrite::Unchanged;
    }
    const QFileInfo fi(path);
    const QDir dir = fi.absoluteDir();
    if (!dir.exists()) {
        if (!QDir().mkpath(dir.absolutePath())) return AgendaWrite::Failed;
        QFile::setPermissions(dir.absolutePath(), QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
    }
    const QString tmp = path + ".tmp";
    QFile f(tmp);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) return AgendaWrite::Failed;
    f.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);  // before any content
    const QByteArray bytes = text.toUtf8();
    const bool ok = f.write(bytes) == bytes.size();
    f.close();
    // rename(2) replaces the target atomically (QFile::rename refuses an existing one).
    if (!ok || std::rename(QFile::encodeName(tmp).constData(), QFile::encodeName(path).constData()) != 0) {
        QFile::remove(tmp);
        return AgendaWrite::Failed;
    }
    return AgendaWrite::Written;
}

int runAgendaCli(const QStringList& args) {
    bool print = false, force = false;
    QString graph;
    for (const QString& a : args) {
        if (a == "--print") print = true;
        else if (a == "--force") force = true;
        else if (!a.startsWith("--")) graph = QDir(a).absolutePath();
    }
    if (graph.isEmpty() || !QFileInfo(graph).isDir()) graph = QSettings().value("graph").toString();
    if (graph.isEmpty() || !QFileInfo(graph).isDir()) {
        std::fprintf(stderr, "graphite --agenda: no graph (give a folder, or open one in Graphite once)\n");
        return 1;
    }
    if (!print && !force && !agendaEnabled()) {
        std::fprintf(stderr, "graphite --agenda: off in settings (desktop/agenda); use --force to write anyway\n");
        return 0;
    }
    QString format;
    const GraphIndex index = loadGraphIndex(graph, &format);
    const QString text = buildAgenda(index, QDateTime::currentDateTime(), graph, format);
    if (print) {
        std::fputs(text.toUtf8().constData(), stdout);
        return 0;
    }
    const QString path = nervAgendaPath();
    switch (writeAgendaFile(text, path)) {
        case AgendaWrite::Written: std::printf("agenda written: %s\n", qPrintable(path)); return 0;
        case AgendaWrite::Unchanged: std::printf("agenda unchanged: %s\n", qPrintable(path)); return 0;
        case AgendaWrite::Failed: break;
    }
    std::fprintf(stderr, "graphite --agenda: could not write %s\n", qPrintable(path));
    return 1;
}

void runNervFocus(const QStringList& args) {
    if (!agendaEnabled()) return;
    QString exe = qEnvironmentVariable("GRAPHITE_NERV_FOCUS");
    if (exe.isEmpty()) exe = QStandardPaths::findExecutable("nerv-focus");
    if (exe.isEmpty() || !QFileInfo(exe).isExecutable()) return;
    QProcess::startDetached(exe, args);
}

void nervFocusStart(int seconds, const QString& name) {
    runNervFocus({"start", "--single", QString::number(qMax(1, seconds / 60)), name});
}

void nervFocusStop() { runNervFocus({"stop"}); }

}  // namespace graphite
