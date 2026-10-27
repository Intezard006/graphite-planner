#include "ui/blur.hpp"

#include <QGuiApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMouseEvent>
#include <QPainter>
#include <QProcess>
#include <QSettings>
#include <QStandardPaths>
#include <QWheelEvent>

#include "ui/theme.hpp"

namespace graphite {

namespace {
bool onHyprland() {
    return QGuiApplication::platformName() == "wayland" && !qEnvironmentVariableIsEmpty("HYPRLAND_INSTANCE_SIGNATURE") &&
           qEnvironmentVariableIsEmpty("GRAPHITE_NO_HYPRCTL") && !QStandardPaths::findExecutable("hyprctl").isEmpty();
}

void sendBlur(int size, int passes) {
    const QString lua = QString("hl.config({decoration={blur={size=%1, passes=%2}}})").arg(size).arg(passes);
    QProcess p;
    p.setProgram("hyprctl");
    p.setArguments({"eval", lua});
    p.setStandardOutputFile(QProcess::nullDevice());  // hyprctl answers "ok"; keep the terminal clean
    p.setStandardErrorFile(QProcess::nullDevice());
    p.startDetached();
}
}  // namespace

// ---------------------------------------------------------------- control

BlurControl::BlurControl(QObject* parent) : QObject(parent) {
    available_ = onHyprland();
    level_ = qBound(0, QSettings().value("blurLevel", 5).toInt(), kMaxLevel);
    debounce_.setSingleShot(true);
    debounce_.setInterval(60);  // dragging the meter sends at most ~16 updates a second
    connect(&debounce_, &QTimer::timeout, this, &BlurControl::push);
}

BlurControl::~BlurControl() { setActive(false); }

void BlurControl::levelToBlur(int level, int* size, int* passes) {
    level = qBound(0, level, kMaxLevel);
    *size = 1 + level;                                          // 1 .. 11 px
    *passes = level < 3 ? 1 : level < 6 ? 2 : level < 9 ? 3 : 4;  // more passes = smoother, wider
}

bool BlurControl::readOption(const char* name, int* value) {
    QProcess p;
    p.start("hyprctl", {"-j", "getoption", QString::fromLatin1(name)});
    if (!p.waitForFinished(1000)) return false;
    const QJsonObject o = QJsonDocument::fromJson(p.readAllStandardOutput()).object();
    if (!o.contains("int")) return false;
    *value = o.value("int").toInt();
    return true;
}

void BlurControl::setLevel(int level) {
    level = qBound(0, level, kMaxLevel);
    if (level == level_) return;
    level_ = level;
    QSettings().setValue("blurLevel", level_);
    emit levelChanged(level_);
    if (active_) debounce_.start();
}

void BlurControl::setActive(bool on) {
    if (!available_ || on == active_) return;
    if (on) {
        // Remember the user's own blur before touching it, on disk too in case we crash.
        if (!readOption("decoration:blur:size", &origSize_) || !readOption("decoration:blur:passes", &origPasses_)) return;
        QSettings s;
        s.setValue("blur/origSize", origSize_);
        s.setValue("blur/origPasses", origPasses_);
        s.sync();
        active_ = true;
        push();
    } else {
        active_ = false;
        debounce_.stop();
        push();
        QSettings s;
        s.remove("blur/origSize");
        s.remove("blur/origPasses");
    }
}

void BlurControl::push() {
    if (active_) {
        int size, passes;
        levelToBlur(level_, &size, &passes);
        sendBlur(size, passes);
    } else if (origSize_ >= 0 && origPasses_ >= 0) {
        sendBlur(origSize_, origPasses_);
    }
}

void BlurControl::recoverAfterCrash() {
    if (!onHyprland()) return;
    QSettings s;
    if (!s.contains("blur/origSize") || !s.contains("blur/origPasses")) return;
    sendBlur(s.value("blur/origSize").toInt(), s.value("blur/origPasses").toInt());
    s.remove("blur/origSize");
    s.remove("blur/origPasses");
}

// ------------------------------------------------------------------ meter

namespace {
constexpr int kSegW = 6, kSegGap = 3, kSegH = 10;
}

BlurMeter::BlurMeter(QWidget* parent) : QWidget(parent) {
    setCursor(Qt::PointingHandCursor);
    setToolTip("Background blur (Ctrl+[ / Ctrl+])");
}

QSize BlurMeter::sizeHint() const {
    const QFontMetrics lm(Theme::instance().labelFont());
    return QSize(segmentsLeft() + (BlurControl::kMaxLevel + 1) * (kSegW + kSegGap), qMax(lm.height(), kSegH) + 4);
}

int BlurMeter::segmentsLeft() const {
    return QFontMetrics(Theme::instance().labelFont()).horizontalAdvance("BLUR") + 10;
}

void BlurMeter::setLevel(int level) {
    if (level == level_) return;
    level_ = level;
    update();
}

int BlurMeter::levelAt(int x) const {
    return qBound(0, (x - segmentsLeft()) / (kSegW + kSegGap), BlurControl::kMaxLevel);
}

void BlurMeter::paintEvent(QPaintEvent*) {
    const Theme& th = Theme::instance();
    QPainter p(this);
    p.setFont(th.labelFont());
    p.setPen(th.dim());
    p.drawText(QRect(0, 0, segmentsLeft(), height()), Qt::AlignVCenter | Qt::AlignLeft, "Blur");
    const int y = (height() - kSegH) / 2;
    for (int i = 0; i <= BlurControl::kMaxLevel; ++i) {
        const QRect seg(segmentsLeft() + i * (kSegW + kSegGap), y, kSegW, kSegH);
        if (i <= level_) p.fillRect(seg, i == level_ ? th.accent() : th.structure());
        else p.fillRect(seg, th.line());
    }
}

void BlurMeter::mousePressEvent(QMouseEvent* e) {
    if (e->button() == Qt::LeftButton) emit levelPicked(levelAt(e->position().toPoint().x()));
}

void BlurMeter::mouseMoveEvent(QMouseEvent* e) {
    if (e->buttons() & Qt::LeftButton) emit levelPicked(levelAt(e->position().toPoint().x()));
}

void BlurMeter::wheelEvent(QWheelEvent* e) {
    const int step = e->angleDelta().y() > 0 ? 1 : e->angleDelta().y() < 0 ? -1 : 0;
    if (step) emit levelPicked(qBound(0, level_ + step, BlurControl::kMaxLevel));
}

}  // namespace graphite
