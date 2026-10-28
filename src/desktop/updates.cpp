#include "desktop/updates.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QStandardPaths>

#ifndef GRAPHITE_SOURCE_DIR
#define GRAPHITE_SOURCE_DIR ""
#endif
#ifndef GRAPHITE_VERSION_TEXT
#define GRAPHITE_VERSION_TEXT "0"
#endif

namespace graphite {

namespace {

// What a build is made of: CMakeLists.txt and src/ (not tests, docs or third_party).
QStringList sourceFiles(const QString& repo) {
    QStringList out;
    if (QFileInfo::exists(repo + "/CMakeLists.txt")) out << repo + "/CMakeLists.txt";
    QDirIterator it(repo + "/src", QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) out << it.next();
    return out;
}

QString readFile(const QString& path) {
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll()).trimmed() : QString();
}

}  // namespace

int compareVersions(const QString& a, const QString& b) {
    const QStringList x = a.split('.'), y = b.split('.');
    for (int i = 0; i < qMax(x.size(), y.size()); ++i) {
        const int p = x.value(i).toInt(), q = y.value(i).toInt();
        if (p != q) return p < q ? -1 : 1;
    }
    return 0;
}

UpdateInfo checkForUpdate(const QString& repo, const QString& exe, const QDateTime& exeTime) {
    UpdateInfo u;
    if (repo.isEmpty() || !QFileInfo::exists(repo + "/CMakeLists.txt")) return u;
    u.repo = true;
    static const QRegularExpression ver(R"(project\s*\(\s*graphite\s+VERSION\s+([0-9.]+))");
    u.repoVersion = ver.match(readFile(repo + "/CMakeLists.txt")).captured(1);

    const QString head = readFile(repo + "/.git/HEAD");
    if (head.startsWith("ref: ")) {
        const QString ref = head.mid(5);
        u.branch = ref.section('/', 2);
        QString hash = readFile(repo + "/.git/" + ref);
        if (hash.isEmpty())  // packed: "<hash> refs/heads/main"
            for (const QString& line : readFile(repo + "/.git/packed-refs").split('\n'))
                if (line.endsWith(" " + ref)) hash = line.left(40);
        u.commit = hash.left(7);
    } else if (!head.isEmpty()) {
        u.commit = head.left(7);  // detached
    }

    // Install keeps the build's mtime, to the second: allow two seconds.
    const QDateTime onDisk = QFileInfo(exe).lastModified();
    const QDateTime built = onDisk.isValid() ? onDisk : exeTime;
    for (const QString& f : sourceFiles(repo))
        if (QFileInfo(f).lastModified() > built.addSecs(2)) u.changed << QDir(repo).relativeFilePath(f);
    u.rebuilt = onDisk.isValid() && exeTime.isValid() && onDisk > exeTime.addSecs(2);
    return u;
}

Updater::Updater(QObject* parent) : QObject(parent) {
    repo_ = qEnvironmentVariableIsSet("GRAPHITE_UPDATE_REPO") ? qEnvironmentVariable("GRAPHITE_UPDATE_REPO")
                                                               : QString(GRAPHITE_SOURCE_DIR);
    exe_ = QCoreApplication::applicationFilePath();  // before an install replaces the file
    exeTime_ = QFileInfo(exe_).lastModified();
    settle_.setSingleShot(true);
    settle_.setInterval(1500);
    connect(&settle_, &QTimer::timeout, this, &Updater::check);
    auto later = [this] { settle_.start(); };
    connect(&watch_, &QFileSystemWatcher::fileChanged, this, later);
    connect(&watch_, &QFileSystemWatcher::directoryChanged, this, later);
}

QString Updater::logPath() const {
    return QStandardPaths::writableLocation(QStandardPaths::GenericCacheLocation) + "/graphite/update.log";
}

void Updater::setWatching(bool on) {
    watching_ = on;
    if (!on) {
        settle_.stop();
        if (!watch_.files().isEmpty()) watch_.removePaths(watch_.files());
        if (!watch_.directories().isEmpty()) watch_.removePaths(watch_.directories());
        if (state_ != State::Building) state_ = State::Off;
        emit changed();
        return;
    }
    check();
}

// Folders catch files that are replaced or added; files catch edits made in place.
void Updater::rewatch() {
    if (!info_.repo) return;
    QStringList want = sourceFiles(repo_);
    want << repo_ << QFileInfo(exe_).absolutePath() << repo_ + "/.git/HEAD";
    QDirIterator it(repo_ + "/src", QDir::Dirs | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
    want << repo_ + "/src";
    while (it.hasNext()) want << it.next();
    QStringList add;
    const QStringList have = watch_.files() + watch_.directories();
    for (const QString& p : want)
        if (!have.contains(p) && QFileInfo::exists(p)) add << p;
    if (!add.isEmpty()) watch_.addPaths(add);
}

void Updater::check() {
    if (state_ == State::Building) return;  // the build itself touches files
    info_ = checkForUpdate(repo_, exe_, exeTime_);
    if (!watching_) state_ = State::Off;
    else if (!info_.repo) state_ = State::NoRepo;
    else if (!info_.changed.isEmpty() || compareVersions(info_.repoVersion, GRAPHITE_VERSION_TEXT) > 0) state_ = State::Available;
    else if (info_.rebuilt) state_ = State::Installed;
    else if (state_ != State::Failed) state_ = State::UpToDate;
    if (watching_) rewatch();
    emit changed();
}

QString Updater::status() const {
    const QString here = QString(GRAPHITE_VERSION_TEXT);
    const QString at = info_.commit.isEmpty() ? QString() : "  ·  " + info_.branch + " " + info_.commit;
    switch (state_) {
        case State::Off: return "Not watching  ·  " + here;
        case State::NoRepo: return "No source folder  ·  " + here;
        case State::UpToDate: return "Up to date  ·  " + here + at;
        case State::Available: {
            const int n = static_cast<int>(info_.changed.size());
            const QString what = n == 0 ? QString() : QString("  ·  %1 file%2 changed").arg(n).arg(n == 1 ? "" : "s");
            const QString to = compareVersions(info_.repoVersion, here) > 0 ? here + " → " + info_.repoVersion : here;
            return "Ready  ·  " + to + what;
        }
        case State::Building: return progress_.isEmpty() ? QString("Starting…") : progress_;
        case State::Installed: return "Installed " + info_.repoVersion + "  ·  restart to use it";
        case State::Failed: return "Failed  ·  " + progress_;
    }
    return QString();
}

void Updater::install() {
    if (state_ == State::Building || !info_.repo) return;
    QDir().mkpath(QFileInfo(logPath()).absolutePath());
    QFile::remove(logPath());
    proc_ = new QProcess(this);
    proc_->setWorkingDirectory(repo_);
    proc_->setProcessChannelMode(QProcess::MergedChannels);
    connect(proc_, &QProcess::readyRead, this, &Updater::readOutput);
    connect(proc_, &QProcess::finished, this, [this](int code, QProcess::ExitStatus st) {
        readOutput();
        const bool ok = st == QProcess::NormalExit && code == 0;
        proc_->deleteLater();
        proc_ = nullptr;
        if (!ok && progress_.isEmpty()) progress_ = "see " + QDir::toNativeSeparators(logPath()).replace(QDir::homePath(), "~");
        state_ = ok ? State::Installed : State::Failed;
        if (ok) progress_.clear();
        info_ = checkForUpdate(repo_, exe_, exeTime_);
        emit changed();
    });
    state_ = State::Building;
    progress_ = "Starting…";
    emit changed();
    proc_->start(repo_ + "/install.sh", {"--yes"});
    proc_->closeWriteChannel();  // nothing can be typed: a question gets "no"
}

// Keeps the log, and a short line of where the build is: "Building 37/120", "Testing 3/5".
void Updater::readOutput() {
    if (!proc_) return;
    const QByteArray chunk = proc_->readAll();
    QFile log(logPath());
    if (log.open(QIODevice::Append)) log.write(chunk);
    pending_ += chunk;
    static const QRegularExpression ansi(R"(\x1b\[[0-9;]*m)");
    static const QRegularExpression ninja(R"(^\[(\d+)/(\d+)\])");
    static const QRegularExpression ctest(R"(^\s*(\d+)/(\d+) Test)");
    static const QRegularExpression ctestFail(R"(tests failed|Tests failed)");
    int nl;
    QString last = progress_;
    while ((nl = static_cast<int>(pending_.indexOf('\n'))) >= 0) {
        QString line = QString::fromUtf8(pending_.left(nl)).remove(ansi).trimmed();
        pending_.remove(0, nl + 1);
        QRegularExpressionMatch m;
        if ((m = ninja.match(line)).hasMatch()) last = "Building " + m.captured(1) + "/" + m.captured(2);
        else if ((m = ctest.match(line)).hasMatch()) last = "Testing " + m.captured(1) + "/" + m.captured(2);
        else if (line.startsWith("==> ")) {  // install.sh's own steps
            const QString step = line.mid(4).section(' ', 0, 0);
            last = step == "Checking" ? "Checking build tools" : step == "Building" ? "Building"
                 : step == "Running" ? "Testing" : step == "Found" ? "Replacing the old copy"
                 : step == "Installing" ? "Installing" : last;
        }
        else if (line.startsWith("!! ") && ctestFail.match(line).hasMatch()) last = "tests failed, nothing installed";
        else if (line.startsWith("error:") || line.contains(": error: ")) last = "build error";
    }
    if (last != progress_) {
        progress_ = last;
        emit changed();
    }
}

bool Updater::restart(const QStringList& args) {
    return QProcess::startDetached(exe_, args);
}

}  // namespace graphite
