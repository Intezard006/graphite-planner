// Updates from the source folder this build came from (GRAPHITE_SOURCE_DIR, a local checkout:
// there is no server). A newer build is ready when a source file there is newer than the
// installed binary; the binary itself being newer than the running one means "restart".
//
// No polling: inotify watches on the source folders, their files and the binary's folder,
// checked 1.5 s after the last change. "Update" runs the checkout's ./install.sh --yes.
#pragma once

#include <QDateTime>
#include <QFileSystemWatcher>
#include <QObject>
#include <QProcess>
#include <QStringList>
#include <QTimer>

namespace graphite {

struct UpdateInfo {
    bool repo = false;          // the source folder is there
    QString repoVersion;        // project(graphite VERSION ...) in its CMakeLists.txt
    QString branch, commit;     // its git HEAD ("main", "e78a755"); empty without git
    QStringList changed;        // source files newer than the installed binary (relative paths)
    bool rebuilt = false;       // the binary on disk is newer than the one running
};

// Compares the checkout at `repo` with the binary at `exe`, built at `exeTime` (its mtime when
// it was started). Stats about a hundred files.
UpdateInfo checkForUpdate(const QString& repo, const QString& exe, const QDateTime& exeTime);

// -1, 0, 1 for "0.9.2" against "0.10.0".
int compareVersions(const QString& a, const QString& b);

class Updater : public QObject {
    Q_OBJECT
public:
    enum class State { Off, NoRepo, UpToDate, Available, Building, Installed, Failed };

    explicit Updater(QObject* parent = nullptr);
    void setWatching(bool on);  // QSettings "updates" = watch | off
    bool watching() const { return watching_; }

    State state() const { return state_; }
    const UpdateInfo& info() const { return info_; }
    QString repo() const { return repo_; }
    QString logPath() const;
    QString status() const;     // one line for the Settings row
    QString progress() const { return progress_; }

    void check();               // now (also on a timer after file changes)
    void install();             // ./install.sh --yes in the checkout
    bool restart(const QStringList& args);  // start the new binary (the caller then quits)

signals:
    void changed();             // state, info or progress

private:
    void rewatch();
    void readOutput();

    QString repo_, exe_;
    QDateTime exeTime_;
    bool watching_ = false;
    State state_ = State::Off;
    UpdateInfo info_;
    QFileSystemWatcher watch_;
    QTimer settle_;
    QProcess* proc_ = nullptr;
    QString progress_;
    QByteArray pending_;  // output not yet ending in a newline
};

}  // namespace graphite
