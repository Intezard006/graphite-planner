// The NERV panel at the bottom of the sidebar: an ASCII A.T. Field, a clock and the power
// readout (EXTERNAL on the charger, the umbilical cable; INTERNAL with time left on battery).
//
// Built for battery life. The field is only animated for a moment after something happens
// (start, page change, charger plugged or pulled) or while the mouse is over it, and only in
// the active window; otherwise it is a still picture. The clock and power readout change once
// a minute, from one timer aligned to the minute. Idle cost: one wakeup per minute.
#pragma once

#include <QElapsedTimer>
#include <QSocketNotifier>
#include <QTime>
#include <QTimer>
#include <QWidget>

namespace graphite {

struct PowerState {
    bool battery = false;    // a battery exists
    bool external = true;    // on the charger (or a desktop)
    int percent = -1;
    int minutesLeft = -1;    // while discharging, if the battery reports a rate
};

class NervPanel : public QWidget {
    Q_OBJECT
public:
    explicit NervPanel(QWidget* parent = nullptr);
    ~NervPanel() override;

    // Pulse: ripple for a moment after events (and on hover). Constant: always, but only while
    // the window is active and on the charger (on battery it pulses). Off: the panel is hidden.
    enum class Mode { Pulse, Constant, Off };
    void setMode(Mode m);
    Mode mode() const { return mode_; }
    static QString modeName(Mode m);

    void pulse(int ms = 2600);   // ripple for a moment
    // The nearest #exam: "ANGEL APPROACHING · 4 DAYS" between the field and the clock.
    // days < 0 hides it.
    void setExam(const QString& what, int days);
    // Ramiel (immersion only; plain wording keeps the one-line exam row). In the exam's last
    // week the Angel sits beside the countdown, the exam's name under it, bluer far off and
    // redder near. On the day before, its drill bores down towards the clock; on the day it is
    // through. All still pictures: they move only in the panel's usual frames.
    bool ramiel() const;
    QString examName() const { return examName_; }  // the exam's line without tag, marker, dates
    void shapeShift();  // clicked: it unfolds and folds back
    void fireBeam();    // a deadline passed undone: one beam into the A.T. Field
    void flyBy();       // the day's last task done: once in a while it crosses the field
    bool effectRunning() const;
    // The day's time blocks: "Next · 14:00" and what it is ("Now · –15:30" while one runs),
    // above the clock. Empty `head` hides the row.
    void setNext(const QString& head, const QString& text);
    QString nextText() const { return nextHead_.isEmpty() ? QString() : nextHead_ + " " + nextText_; }
    // A running operation: seconds left of total (left < 0: none). Replaces the clock.
    void setOperation(int left, int total);
    int operationLeft() const { return opLeft_; }
    int examDays() const { return examDays_; }
    // Entry plug: about 1.5 s of start-up lines in the field area, then a pulse. Click skips.
    void boot();
    bool booting() const { return booting_; }
    // On battery and at 15 % or less: the activity-limit countdown takes over the clock.
    bool alarm() const { return power_.battery && !power_.external && power_.percent >= 0 && power_.percent <= 15; }
    bool animating() const { return frame_.isActive(); }
    QSize sizeHint() const override;

protected:
    void resizeEvent(QResizeEvent* e) override;

public:

    // Read /sys/class/power_supply, or `root`, or $GRAPHITE_POWER_ROOT (tests); IOKit on macOS.
    static PowerState readPower(const QString& root = QString());
    void refreshPower() { tick(); }  // read the clock and power now (normally once a minute)
    const PowerState& power() const { return power_; }
    // The field's character at a point, 0..7 into " .:-=+*#": public for tests.
    static int fieldLevel(double x, double y, double phase, bool calm);

protected:
    void paintEvent(QPaintEvent*) override;
    void enterEvent(QEnterEvent*) override;
    void leaveEvent(QEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void showEvent(QShowEvent*) override;
    bool eventFilter(QObject*, QEvent*) override;  // the window's activation, for Constant mode
    void hideEvent(QHideEvent*) override;

private:
    void tick();        // every minute: time and power
    void scheduleTick();
    void step();        // one animation frame
    bool constantNow() const;
    QRect fieldRect() const;

    void listenForPower();  // kernel uevents (IOKit on macOS): charger plugged / pulled, right away
    void onUevent();
    void rereadPower();

    QTimer frame_, minute_, powerSettle_;
    QSocketNotifier* uevents_ = nullptr;
    int ueventFd_ = -1;
    void* powerSource_ = nullptr;  // macOS: the IOKit run-loop source (CFRunLoopSourceRef)
    QElapsedTimer since_;
    qint64 animUntil_ = 0;
    double phase_ = 0;
    bool hovering_ = false;
    bool booting_ = false;
    int opLeft_ = -1, opTotal_ = 0;
    QString exam_;
    int examDays_ = -1;
    qint64 shiftAt_ = -1, beamAt_ = -1, flyAt_ = -1;  // since_ times of the running effects
    mutable QRectF ramielBox_;  // where it was drawn, for clicks
    QString nextHead_, nextText_, examName_;
    static constexpr int kDrill = 14;  // room under the Angel for its drill, the last day
    bool drilling() const { return ramiel() && examDays_ <= 1; }
    double effect(qint64 at, int ms) const;  // 0..1 through a running effect, < 0 when none
    // heat 0 blue .. 1 red; unfold 0 whole .. 1 apart; spin: extra turn in radians.
    void paintRamiel(QPainter& p, const QRectF& box, double heat, double unfold, double spin) const;
    // The exam's name under Ramiel: one line, or two when "Quiz · Course" does not fit (split at
    // the dot, each elided).
    QStringList examNameLines() const;
    qint64 bootStart_ = 0;
    Mode mode_ = Mode::Pulse;
    QWidget* watched_ = nullptr;
    QTime now_;
    PowerState power_;
};

}  // namespace graphite
