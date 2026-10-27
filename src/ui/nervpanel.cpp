#include "ui/nervpanel.hpp"

#include <QMouseEvent>
#include <QPolygonF>
#include <QResizeEvent>
#include <QRegularExpression>
#include <QVector3D>
#include <algorithm>
#include <cmath>

#include <QDir>
#include <QEvent>
#include <QFile>
#include <QLocale>
#include <QPainter>
#include <cmath>
#include <unistd.h>
#ifdef Q_OS_LINUX
#include <linux/netlink.h>
#include <sys/socket.h>
#endif
#ifdef Q_OS_MACOS
#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/ps/IOPowerSources.h>
#include <IOKit/ps/IOPSKeys.h>
#endif

#include "ui/theme.hpp"

namespace graphite {

namespace {
constexpr int kRows = 7;
constexpr int kFrameMs = 80;  // 12.5 frames a second, only while animating
const char kRamp[] = " .:-=+*#";

QString readLine(const QString& path) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return QString();
    return QString::fromLatin1(f.readLine()).trimmed();
}

QFont fieldFont() {
    QFont f = Theme::instance().monoFont();
    f.setPointSizeF(7.5 * Theme::instance().scale());
    return f;
}

QFont clockFont() {
    QFont f = Theme::instance().monoFont();
    f.setPointSizeF(19 * Theme::instance().scale());
    f.setWeight(QFont::Light);
    f.setLetterSpacing(QFont::AbsoluteSpacing, 1.0);
    return f;
}
}  // namespace

NervPanel::NervPanel(QWidget* parent) : QWidget(parent) {
    setAttribute(Qt::WA_Hover);
    frame_.setInterval(kFrameMs);
    connect(&frame_, &QTimer::timeout, this, &NervPanel::step);
    minute_.setSingleShot(true);
    connect(&minute_, &QTimer::timeout, this, &NervPanel::tick);
    since_.start();
    now_ = QTime::currentTime();
    power_ = readPower();
    // A uevent burst (charger, battery, USB-C all report) is read once, 300 ms after it ends.
    powerSettle_.setSingleShot(true);
    powerSettle_.setInterval(300);
    connect(&powerSettle_, &QTimer::timeout, this, &NervPanel::rereadPower);
    listenForPower();
}

void NervPanel::rereadPower() {
    const PowerState before = power_;
    const bool wasAlarm = alarm();
    power_ = readPower();
    if (alarm() && !wasAlarm) pulse();  // crossing into the activity limit
    if (power_.external != before.external) {  // umbilical cable plugged or pulled
        pulse();
        if (constantNow() && window()->isActiveWindow() && !frame_.isActive()) frame_.start();
    }
    update();
}

NervPanel::~NervPanel() {
    if (ueventFd_ >= 0) ::close(ueventFd_);
#ifdef Q_OS_MACOS
    if (powerSource_) {
        auto src = static_cast<CFRunLoopSourceRef>(powerSource_);
        CFRunLoopRemoveSource(CFRunLoopGetMain(), src, kCFRunLoopDefaultMode);
        CFRelease(src);
    }
#endif
}

// The kernel announces every power-supply change on a netlink socket (what `udevadm monitor
// --kernel` shows); listening costs nothing until something actually changes.
void NervPanel::listenForPower() {
    if (!qEnvironmentVariableIsEmpty("GRAPHITE_POWER_ROOT")) return;  // tests use a fake supply
#ifdef Q_OS_MACOS
    // IOKit calls back on the run loop when the power source changes (charger in / out, battery
    // level); the same 300 ms settle as the Linux uevents. Qt's macOS event loop is that run loop.
    CFRunLoopSourceRef src = IOPSNotificationCreateRunLoopSource(
        [](void* self) {
            auto* panel = static_cast<NervPanel*>(self);
            QMetaObject::invokeMethod(panel, [panel] { panel->powerSettle_.start(); }, Qt::QueuedConnection);
        },
        this);
    if (src) {
        CFRunLoopAddSource(CFRunLoopGetMain(), src, kCFRunLoopDefaultMode);
        powerSource_ = src;
    }
    return;
#elif defined(Q_OS_LINUX)
    ueventFd_ = ::socket(AF_NETLINK, SOCK_DGRAM | SOCK_CLOEXEC | SOCK_NONBLOCK, NETLINK_KOBJECT_UEVENT);
    if (ueventFd_ < 0) return;
    sockaddr_nl addr{};
    addr.nl_family = AF_NETLINK;
    addr.nl_groups = 1;  // kernel events
    if (::bind(ueventFd_, reinterpret_cast<sockaddr*>(&addr), sizeof addr) < 0) {
        ::close(ueventFd_);
        ueventFd_ = -1;
        return;
    }
    uevents_ = new QSocketNotifier(ueventFd_, QSocketNotifier::Read, this);
    connect(uevents_, &QSocketNotifier::activated, this, &NervPanel::onUevent);
#endif
}

void NervPanel::onUevent() {
#ifdef Q_OS_LINUX
    char buf[4096];
    bool power = false;
    for (;;) {
        const ssize_t n = ::recv(ueventFd_, buf, sizeof buf - 1, 0);
        if (n <= 0) break;
        // "change@/devices/.../power_supply/ACAD\0ACTION=change\0SUBSYSTEM=power_supply\0..."
        if (QByteArray::fromRawData(buf, static_cast<int>(n)).contains("SUBSYSTEM=power_supply")) power = true;
    }
    if (power) powerSettle_.start();
#endif
}

namespace {
QFont examNameFont() {
    QFont f = Theme::instance().labelFont();
    f.setCapitalization(QFont::MixedCase);
    f.setLetterSpacing(QFont::PercentageSpacing, 100);
    return f;
}
}  // namespace

QStringList NervPanel::examNameLines() const {
    const QFontMetrics lm(Theme::instance().labelFont()), fm(examNameFont());
    const int avail = qMax(40, width() - (2 * lm.height() + 6) - 10);
    const QString dot = QString::fromUtf8(" \xC2\xB7 ");
    const int split = static_cast<int>(examName_.indexOf(dot));
    if (fm.horizontalAdvance(examName_) <= avail || split < 0) return {fm.elidedText(examName_, Qt::ElideRight, avail)};
    return {fm.elidedText(examName_.left(split), Qt::ElideRight, avail),
            fm.elidedText(examName_.mid(split + dot.size()), Qt::ElideRight, avail)};
}

void NervPanel::resizeEvent(QResizeEvent* e) {
    QWidget::resizeEvent(e);
    if (ramiel()) updateGeometry();  // the exam's name may need a second line now
}

QSize NervPanel::sizeHint() const {
    const int field = QFontMetrics(fieldFont()).lineSpacing() * kRows;
    const QFontMetrics lm(Theme::instance().labelFont());
    const int extra = ramiel() && examNameLines().size() > 1 ? QFontMetrics(examNameFont()).height() + 2 : 0;
    const int exam = ramiel() ? 2 * lm.height() + 14 + extra + (drilling() ? kDrill : 0) : examDays_ >= 0 ? lm.height() + 10 : 0;
    const int next = nextHead_.isEmpty() ? 0 : lm.height() + 8;
    return QSize(200, lm.height() + 6 + field + exam + next + 12 + QFontMetrics(clockFont()).height() + lm.height() + 6);
}

void NervPanel::setOperation(int left, int total) {
    if (left == opLeft_ && total == opTotal_) return;
    opLeft_ = left;
    opTotal_ = total;
    update();
}

bool NervPanel::ramiel() const { return examDays_ >= 0 && examDays_ <= 7 && Theme::instance().immersion(); }

void NervPanel::setNext(const QString& head, const QString& text) {
    if (head == nextHead_ && text == nextText_) return;
    const bool resize = head.isEmpty() != nextHead_.isEmpty();
    nextHead_ = head;
    nextText_ = text;
    if (resize) updateGeometry();
    update();
}

void NervPanel::setExam(const QString& what, int days) {
    if (what == exam_ && days == examDays_) return;
    const bool before = ramiel(), drillBefore = drilling();
    const QString nameBefore = examName_;
    exam_ = what;
    examDays_ = days;
    static const QRegularExpression noise(R"(#\[\[exam\]\]|#exam\b|\b(TODO|DOING|NOW|LATER)\s+|\b(SCHEDULED|DEADLINE):\s*<[^>]*>|\*\*)",
                                          QRegularExpression::CaseInsensitiveOption);
    examName_ = QString(what).remove(noise).simplified();
    if (examName_ != nameBefore) updateGeometry();  // one line or two
    const bool resize = (days >= 0) != (examDays_ >= 0) || ramiel() != before || drilling() != drillBefore;
    setToolTip(days >= 0 ? what : QString());  // hover the panel: which exam it is
    if (resize) updateGeometry();
    update();
}

#ifdef Q_OS_MACOS
// IOKit: the providing source ("AC Power" / "Battery Power") and the internal battery's
// capacity and time to empty. A Mac without a battery reports no internal source.
static PowerState readMacPower() {
    PowerState s;
    CFTypeRef info = IOPSCopyPowerSourcesInfo();
    if (!info) return s;
    bool ac = true;
    if (CFStringRef providing = IOPSGetProvidingPowerSourceType(info))
        ac = CFStringCompare(providing, CFSTR(kIOPSACPowerValue), 0) == kCFCompareEqualTo;
    if (CFArrayRef list = IOPSCopyPowerSourcesList(info)) {
        for (CFIndex i = 0; i < CFArrayGetCount(list); ++i) {
            CFDictionaryRef d = IOPSGetPowerSourceDescription(info, CFArrayGetValueAtIndex(list, i));
            if (!d) continue;
            auto str = [d](CFStringRef key) { return static_cast<CFStringRef>(CFDictionaryGetValue(d, key)); };
            auto num = [d](CFStringRef key, int fallback) {
                int v = fallback;
                if (auto n = static_cast<CFNumberRef>(CFDictionaryGetValue(d, key))) CFNumberGetValue(n, kCFNumberIntType, &v);
                return v;
            };
            CFStringRef type = str(CFSTR(kIOPSTypeKey));
            if (!type || CFStringCompare(type, CFSTR(kIOPSInternalBatteryType), 0) != kCFCompareEqualTo) continue;
            s.battery = true;
            const int cur = num(CFSTR(kIOPSCurrentCapacityKey), -1), max = num(CFSTR(kIOPSMaxCapacityKey), 100);
            if (cur >= 0 && max > 0) s.percent = cur * 100 / max;
            const int tte = num(CFSTR(kIOPSTimeToEmptyKey), -1);  // minutes, -1 while it is still estimating
            if (!ac && tte > 0) s.minutesLeft = tte;
        }
        CFRelease(list);
    }
    CFRelease(info);
    s.external = !s.battery || ac;
    return s;
}
#endif

PowerState NervPanel::readPower(const QString& rootArg) {
#ifdef Q_OS_MACOS
    if (rootArg.isEmpty() && qEnvironmentVariableIsEmpty("GRAPHITE_POWER_ROOT")) return readMacPower();
#endif
    QString root = rootArg;
    if (root.isEmpty()) root = qEnvironmentVariable("GRAPHITE_POWER_ROOT", "/sys/class/power_supply");
    PowerState s;
    bool anySupply = false, onlineSupply = false;
    for (const QString& name : QDir(root).entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        const QString d = root + "/" + name;
        const QString type = readLine(d + "/type");
        if (type == "Battery") {
            if (readLine(d + "/present") == "0") continue;
            s.battery = true;
            s.percent = readLine(d + "/capacity").toInt();
            const QString status = readLine(d + "/status");
            if (status == "Discharging") {
                // Time left = energy / power (or charge / current), whichever the battery reports.
                const double energy = readLine(d + "/energy_now").toDouble(), power = readLine(d + "/power_now").toDouble();
                const double charge = readLine(d + "/charge_now").toDouble(), current = readLine(d + "/current_now").toDouble();
                if (energy > 0 && power > 0) s.minutesLeft = static_cast<int>(energy / power * 60);
                else if (charge > 0 && current > 0) s.minutesLeft = static_cast<int>(charge / current * 60);
            }
        } else if (type == "Mains" || type == "USB") {
            anySupply = true;
            if (readLine(d + "/online") == "1") onlineSupply = true;
        }
    }
    s.external = !s.battery || onlineSupply || !anySupply;
    return s;
}

int NervPanel::fieldLevel(double x, double y, double phase, bool calm) {
    // Concentric octagons, the A.T. Field's shape, rippling outwards.
    const double d = std::max({std::fabs(x), std::fabs(y), (std::fabs(x) + std::fabs(y)) * 0.7071});
    const double ring = 0.5 + 0.5 * std::cos(d * 1.3 - phase);
    double v = std::pow(ring, 3.0) * std::exp(-d * 0.045);
    if (calm) v *= 0.55;  // the still picture is quieter
    return std::clamp(static_cast<int>(v * 7.99), 0, 7);
}

QString NervPanel::modeName(Mode m) {
    return m == Mode::Pulse ? "Pulse on events" : m == Mode::Constant ? "Constant" : "Off";
}

// Constant only on the charger: on internal power the field conserves energy and just pulses.
bool NervPanel::constantNow() const { return mode_ == Mode::Constant && power_.external; }

void NervPanel::setMode(Mode m) {
    mode_ = m;
    setVisible(m != Mode::Off);
    if (constantNow() && isVisible() && window()->isActiveWindow() && !frame_.isActive()) frame_.start();
    if (m == Mode::Pulse) animUntil_ = qMin(animUntil_, since_.elapsed() + 600);  // wind down
    update();
}

bool NervPanel::eventFilter(QObject* obj, QEvent* ev) {
    if (obj == watched_ && constantNow()) {
        if (ev->type() == QEvent::WindowActivate && isVisible() && !frame_.isActive()) frame_.start();
        // WindowDeactivate: step() sees the inactive window and stops on the next frame.
    }
    return QWidget::eventFilter(obj, ev);
}

namespace {
constexpr qint64 kBootMs = 1500;
constexpr int kShiftMs = 1500, kBeamMs = 1200, kFlyMs = 1900;
const QColor kAlarm(0xff, 0x3b, 0x3b);
const QColor kRamielBlue(0x3d, 0x8b, 0xff);
}

double NervPanel::effect(qint64 at, int ms) const {
    if (at < 0) return -1;
    const qint64 t = since_.elapsed() - at;
    return t < 0 || t > ms ? -1 : static_cast<double>(t) / ms;
}

bool NervPanel::effectRunning() const {
    return effect(shiftAt_, kShiftMs) >= 0 || effect(beamAt_, kBeamMs) >= 0 || effect(flyAt_, kFlyMs) >= 0;
}

void NervPanel::shapeShift() {
    if (!ramiel()) return;
    shiftAt_ = since_.elapsed();
    pulse(kShiftMs + 200);
}

void NervPanel::fireBeam() {
    if (!Theme::instance().immersion() || !isVisible()) return;
    beamAt_ = since_.elapsed();
    pulse(kBeamMs + 600);  // the field ripples where it was hit
}

void NervPanel::flyBy() {
    if (!Theme::instance().immersion() || !isVisible()) return;
    flyAt_ = since_.elapsed();
    pulse(kFlyMs + 200);
}

void NervPanel::boot() {
    if (!isVisible() || !Theme::instance().immersion()) {
        pulse(3200);
        return;
    }
    booting_ = true;
    bootStart_ = since_.elapsed();
    if (!frame_.isActive()) frame_.start();
}

void NervPanel::mousePressEvent(QMouseEvent* e) {
    if (!booting_) {
        if (ramiel() && ramielBox_.contains(e->position())) shapeShift();
        return;
    }
    booting_ = false;  // skip the start-up lines
    pulse(2200);
}

void NervPanel::pulse(int ms) {
    if (!isVisible() || !window()->isActiveWindow()) return;  // nobody is looking: stay still
    animUntil_ = qMax(animUntil_, since_.elapsed() + ms);
    if (!frame_.isActive()) frame_.start();
}

void NervPanel::step() {
    if (booting_) {
        if (since_.elapsed() - bootStart_ < kBootMs) {
            update(fieldRect());
            return;
        }
        booting_ = false;
        animUntil_ = qMax(animUntil_, since_.elapsed() + 2200);  // then the field comes up
    }
    const bool more = hovering_ || constantNow() || since_.elapsed() < animUntil_;
    if (!more || !window()->isActiveWindow()) {
        frame_.stop();
        update();  // settle into the calm picture
        return;
    }
    phase_ += 0.32;
    update(fieldRect());
}

void NervPanel::scheduleTick() {
    const int ms = 60000 - QTime::currentTime().msecsSinceStartOfDay() % 60000 + 40;  // just after the minute
    minute_.start(ms);
}

void NervPanel::tick() {
    now_ = QTime::currentTime();
    powerSettle_.stop();
    rereadPower();  // the same refresh as a uevent (also catches a missed one)
    scheduleTick();
}

void NervPanel::showEvent(QShowEvent*) {
    if (!watched_) {
        watched_ = window();
        watched_->installEventFilter(this);
    }
    tick();
    if (constantNow() && window()->isActiveWindow() && !frame_.isActive()) frame_.start();
}

void NervPanel::hideEvent(QHideEvent*) {
    minute_.stop();
    frame_.stop();
}

void NervPanel::enterEvent(QEnterEvent*) {
    hovering_ = true;
    if (window()->isActiveWindow() && !frame_.isActive()) frame_.start();
}

void NervPanel::leaveEvent(QEvent*) {
    hovering_ = false;
    animUntil_ = qMax(animUntil_, since_.elapsed() + 900);  // ease out
}

QRect NervPanel::fieldRect() const {
    const QFontMetrics lm(Theme::instance().labelFont());
    return QRect(0, lm.height() + 6, width(), QFontMetrics(fieldFont()).lineSpacing() * kRows);
}

// Ramiel: a blue crystal octahedron, turning with the field's phase (so only while the panel
// animates), redder as the exam nears. Faces back to front, translucent, then the edges.
void NervPanel::paintRamiel(QPainter& p, const QRectF& box, double heat, double unfold, double spin) const {
    const QVector3D v[6] = {{1, 0, 0}, {-1, 0, 0}, {0, 1.25f, 0}, {0, -1.25f, 0}, {0, 0, 1}, {0, 0, -1}};
    const int faces[8][3] = {{0, 2, 4}, {4, 2, 1}, {1, 2, 5}, {5, 2, 0}, {0, 3, 4}, {4, 3, 1}, {1, 3, 5}, {5, 3, 0}};
    const float a = static_cast<float>(0.6 + phase_ * 0.35 + spin), tilt = 0.32f;
    const double r = box.height() / 2.6;
    // Turn about the vertical axis, then lean towards the viewer a little.
    auto project = [&](const QVector3D& q, float* depth) {
        const float x = q.x() * std::cos(a) + q.z() * std::sin(a);
        const float z = -q.x() * std::sin(a) + q.z() * std::cos(a);
        const float y = q.y() * std::cos(tilt) - z * std::sin(tilt);
        *depth = q.y() * std::sin(tilt) + z * std::cos(tilt);
        return QPointF(box.center().x() + x * r, box.center().y() - y * r);
    };
    // Unfolding: each face slides out along its own direction and the whole stretches upright,
    // the way it splits apart before it fires.
    QPointF tris[8][3];
    float depth[8];
    for (int f = 0; f < 8; ++f) {
        const QVector3D centre = (v[faces[f][0]] + v[faces[f][1]] + v[faces[f][2]]) / 3.0f;
        const QVector3D out = centre.normalized() * static_cast<float>(unfold * 0.5);
        depth[f] = 0;
        for (int j = 0; j < 3; ++j) {
            QVector3D q = v[faces[f][j]] + out;
            q.setY(q.y() * static_cast<float>(1 + unfold * 0.25));
            float d = 0;
            tris[f][j] = project(q, &d);
            depth[f] += d;
        }
    }
    auto mix = [](const QColor& x, const QColor& y, double t) {
        return QColor::fromRgbF(x.redF() + (y.redF() - x.redF()) * t, x.greenF() + (y.greenF() - x.greenF()) * t,
                                x.blueF() + (y.blueF() - x.blueF()) * t);
    };
    const QColor tone = mix(kRamielBlue, kAlarm, qBound(0.0, heat, 1.0) * 0.85);
    int order[8] = {0, 1, 2, 3, 4, 5, 6, 7};
    std::sort(order, order + 8, [&](int x, int y) { return depth[x] < depth[y]; });
    p.save();
    p.setRenderHint(QPainter::Antialiasing);
    for (int k = 0; k < 8; ++k) {
        const int f = order[k];
        QPolygonF tri;
        for (int j = 0; j < 3; ++j) tri << tris[f][j];
        QColor fill = tone;
        fill.setAlphaF(0.10 + 0.22 * (k / 7.0));  // nearer faces brighter
        p.setPen(QPen(tone.lighter(130), 1));
        p.setBrush(fill);
        p.drawPolygon(tri);
    }
    p.restore();
}

void NervPanel::paintEvent(QPaintEvent*) {
    const Theme& th = Theme::instance();
    QPainter p(this);
    const QFont label = th.labelFont();
    const QFontMetrics lm(label);
    const bool calm = !frame_.isActive();

    // Label row:  A.T. FIELD ............ STABLE / EXPANDING
    p.setFont(label);
    p.setPen(th.faint());
    // With a unit palette the panel names the unit: "UNIT-02 · A.T. FIELD".
    const QString unit = th.palette().source.startsWith("Unit-") && th.immersion() ? th.palette().source : QString();
    const QString name = unit.isEmpty() ? QString("A.T. Field") : unit + "  \xC2\xB7  A.T. Field";
    p.drawText(QRect(0, 0, width(), lm.height()), Qt::AlignLeft | Qt::AlignVCenter, name);
    const QString status = booting_ ? "Boot"
                           : calm   ? (mode_ == Mode::Constant && !power_.external ? "Conserving" : "Stable")
                                    : constantNow() ? "Sustained" : "Expanding";
    if (lm.horizontalAdvance(name.toUpper()) + 12 + lm.horizontalAdvance(status.toUpper()) <= width())
        p.drawText(QRect(0, 0, width(), lm.height()), Qt::AlignRight | Qt::AlignVCenter, status);

    // The field.
    const QRect fr = fieldRect();
    const QFont ff = fieldFont();
    const QFontMetrics fm(ff);
    const int cw = qMax(1, fm.horizontalAdvance('#')), ch = fm.lineSpacing();
    const int cols = fr.width() / cw;
    const double aspect = static_cast<double>(cw) / ch;  // characters are taller than wide
    p.setFont(ff);
    const QColor warm = th.warm(), cool = th.structure();
    if (booting_) {  // entry plug: the start-up lines, one by one
        static const char* lines[][2] = {{"LCL PRESSURE", "NOMINAL"}, {"A10 NERVE CONNECTION", "OK"}, {"SYNC START", ""}};
        const qint64 t = since_.elapsed() - bootStart_;
        for (int i = 0; i < 3; ++i) {
            if (t < i * 420) break;
            const QString left = lines[i][0], right = lines[i][1];
            const int y = fr.top() + (i * 2 + 1) * ch + fm.ascent();
            p.setPen(i == 2 ? th.accent() : th.dim());
            p.drawText(QPointF(fr.left(), y), left);
            if (!right.isEmpty()) {
                const int rw = fm.horizontalAdvance(right);
                const int dotsFrom = fr.left() + fm.horizontalAdvance(left) + cw;
                QString dots;
                for (int x = dotsFrom; x < fr.right() - rw - cw; x += cw) dots += '.';
                p.setPen(th.faint());
                p.drawText(QPointF(dotsFrom, y), dots);
                p.setPen(th.accent());
                p.drawText(QPointF(fr.right() - rw, y), right);
            }
        }
    }
    for (int r = 0; r < kRows && !booting_; ++r) {
        for (int c = 0; c < cols; ++c) {
            const double x = (c - cols / 2.0 + 0.5) * aspect * 1.5, y = (r - kRows / 2.0 + 0.5) * 1.5;
            const int lv = fieldLevel(x, y, phase_, calm);
            if (lv == 0) continue;
            QColor col = lv >= 5 ? warm : cool;
            col.setAlpha(60 + lv * 26);
            p.setPen(col);
            p.drawText(QPointF(fr.left() + c * cw, fr.top() + r * ch + fm.ascent()), QString(QChar(kRamp[lv])));
        }
    }

    // The nearest exam: ANGEL APPROACHING ................ 4 DAYS (red on the day). In its last
    // week the Angel itself is there, and the rifle charging under the countdown:
    //   [octahedron]  RAMIEL ............. 4 DAYS
    //                 YASHIMA ▮▮▮▮▮▯▯▯▯▯     62%
    int below = fr.bottom();
    if (ramiel()) {
        const int ey = fr.bottom() + 8;
        const int s = 2 * lm.height() + 6;
        const double heat = examDays_ <= 1 ? 1.0 : 1.0 - (examDays_ - 1) / 6.0;  // 0 a week out, 1 the day before
        const double sh = effect(shiftAt_, kShiftMs);
        const double unfold = sh < 0 ? 0 : std::sin(sh * M_PI);
        ramielBox_ = QRectF(0, ey, s, s);
        if (drilling()) {
            // The drill: from its lower point down towards NERV (the clock); through on the day.
            const double through = examDays_ == 0 ? 1.0 : now_.msecsSinceStartOfDay() / 86400000.0;
            const double x = s / 2.0, top = ey + s * 0.86, len = (s * 0.14 + kDrill + 12) * through;
            p.setRenderHint(QPainter::Antialiasing);
            p.setPen(QPen(examDays_ == 0 ? kAlarm : th.warm(), 1.2));
            p.drawLine(QPointF(x, top), QPointF(x, top + len));
            p.setBrush(examDays_ == 0 ? kAlarm : th.warm());
            p.setPen(Qt::NoPen);
            p.drawPolygon(QPolygonF{QPointF(x - 2.5, top + len - 3), QPointF(x + 2.5, top + len - 3), QPointF(x, top + len + 2)});
        }
        paintRamiel(p, ramielBox_, heat, unfold, sh < 0 ? 0 : sh * 2 * M_PI);
        const int tx = s + 10;
        const QColor danger = examDays_ <= 1 ? kAlarm : th.warm();
        const QString when = examDays_ == 0 ? QString("Today") : examDays_ == 1 ? QString("Tomorrow") : QString("%1 days").arg(examDays_);
        p.setFont(label);
        p.setPen(danger);
        p.drawText(QRect(tx, ey, width() - tx, lm.height()), Qt::AlignLeft | Qt::AlignVCenter, "Ramiel");
        p.drawText(QRect(tx, ey, width() - tx, lm.height()), Qt::AlignRight | Qt::AlignVCenter, when);
        // Under it, which exam it is (its first line, without the tag), and the heading it is under.
        const QFont nf = examNameFont();
        p.setFont(nf);
        p.setPen(th.dim());
        const QStringList lines = examNameLines();
        const int nh = QFontMetrics(nf).height() + 2;
        int cy = ey + lm.height() + 6;
        for (const QString& line : lines) {
            p.drawText(QRect(tx, cy, width() - tx, lm.height()), Qt::AlignLeft | Qt::AlignVCenter, line);
            cy += nh;
        }
        below = ey + s + 2 + (lines.size() > 1 ? nh : 0) + (drilling() ? kDrill : 0);
    } else if (examDays_ >= 0) {
        const int ey = fr.bottom() + 8;
        const QString when = examDays_ == 0 ? QString("Today") : examDays_ == 1 ? QString("Tomorrow") : QString("%1 days").arg(examDays_);
        QString what = th.immersion() ? QString("Angel approaching") : QString("Exam");
        if (lm.horizontalAdvance(what.toUpper()) + 14 + lm.horizontalAdvance(when.toUpper()) > width())
            what = th.immersion() ? QString("Angel") : QString("Exam");  // a narrow panel: the short name
        p.setFont(label);
        p.setPen(examDays_ == 0 ? kAlarm : th.warm());
        p.drawText(QRect(0, ey, width(), lm.height()), Qt::AlignLeft | Qt::AlignVCenter, what);
        p.drawText(QRect(0, ey, width(), lm.height()), Qt::AlignRight | Qt::AlignVCenter, when);
        below = ey + lm.height();
    }

    // The day's time blocks: NEXT · 14:00 ......... Lab write-up
    if (!nextHead_.isEmpty()) {
        const int ny = below + 8;
        p.setFont(label);
        p.setPen(nextHead_.startsWith("Now") ? th.accent() : th.structure());
        p.drawText(QRect(0, ny, width(), lm.height()), Qt::AlignLeft | Qt::AlignVCenter, nextHead_);
        const int hw = lm.horizontalAdvance(nextHead_.toUpper()) + 12;
        QFont nf = label;
        nf.setCapitalization(QFont::MixedCase);
        nf.setLetterSpacing(QFont::PercentageSpacing, 100);
        p.setFont(nf);
        p.setPen(th.text());
        p.drawText(QRect(hw, ny, width() - hw, lm.height()), Qt::AlignRight | Qt::AlignVCenter,
                   QFontMetrics(nf).elidedText(nextText_, Qt::ElideRight, width() - hw));
        below = ny + lm.height();
    }

    // Clock and power.
    const int y0 = below + 12;
    const QFont cf = clockFont();
    const QFontMetrics cm(cf);
    const bool limit = alarm();
    const bool operation = opLeft_ >= 0 && !limit;
    if (operation && opTotal_ > 0) {  // a thin line: how much of the operation has run
        const int bw = static_cast<int>(width() * (1.0 - opLeft_ / static_cast<double>(opTotal_)));
        p.fillRect(QRect(0, y0 - 6, width(), 2), th.line());
        p.fillRect(QRect(0, y0 - 6, bw, 2), th.accent());
    }
    p.setFont(cf);
    p.setPen(limit ? kAlarm : operation ? th.accent() : th.text());
    // Activity limit: the time left replaces the clock (the percentage when no rate is known).
    const QString big = operation ? (opLeft_ >= 3600 ? QString("%1:%2:%3").arg(opLeft_ / 3600).arg(opLeft_ % 3600 / 60, 2, 10, QChar('0')).arg(opLeft_ % 60, 2, 10, QChar('0'))
                                                     : QString("%1:%2").arg(opLeft_ / 60, 2, 10, QChar('0')).arg(opLeft_ % 60, 2, 10, QChar('0')))
                        : !limit ? now_.toString("HH:mm")
                        : power_.minutesLeft >= 0
                            ? QString("%1:%2").arg(power_.minutesLeft / 60).arg(power_.minutesLeft % 60, 2, 10, QChar('0'))
                            : QString("%1%").arg(power_.percent);
    p.drawText(QPointF(0, y0 + cm.ascent()), big);
    const int y1 = y0 + cm.height() + 2;
    const QString dateText = limit       ? (th.immersion() ? QString("Activity limit") : QString("Battery low"))
                             : operation ? (th.immersion() ? QString("Operation") : QString("Timer"))
                                         : QLocale::c().toString(QDate::currentDate(), "ddd d MMM");

    // EXTERNAL (green, the cable) or INTERNAL 2:47 (orange), with a 10-segment meter.
    const bool ext = power_.external;
    const QColor tone = limit ? kAlarm : ext ? th.accent() : th.warm();
    QString state = ext ? "External" : "Internal";
    if (!ext && power_.minutesLeft >= 0 && !limit)  // in the alarm the countdown is already big
        state += QString("  %1:%2").arg(power_.minutesLeft / 60).arg(power_.minutesLeft % 60, 2, 10, QChar('0'));
    p.setFont(label);
    p.setPen(tone);
    p.drawText(QRect(0, y1, width(), lm.height()), Qt::AlignRight | Qt::AlignVCenter, state);
    if (lm.horizontalAdvance(dateText.toUpper()) + 12 + lm.horizontalAdvance(state.toUpper()) <= width()) {
        p.setPen(limit ? kAlarm : operation ? th.accent() : th.dim());
        p.drawText(QPointF(0, y1 + lm.ascent()), dateText);  // the date only where it fits
    }
    if (power_.battery && power_.percent >= 0) {
        constexpr int kSegW = 5, kGap = 2, kSegH = 9;
        const int segs = 10, lit = (power_.percent + 5) / 10;
        const int x0 = width() - segs * (kSegW + kGap) + kGap;
        const int sy = y0 + (cm.height() - kSegH) / 2;
        for (int i = 0; i < segs; ++i)
            p.fillRect(QRect(x0 + i * (kSegW + kGap), sy, kSegW, kSegH), i < lit ? tone : th.line());
    }

    // Ramiel's beam: it gathers, fires into the field, and the field takes it (an octagon flash).
    if (const double t = effect(beamAt_, kBeamMs); t >= 0) {
        p.setRenderHint(QPainter::Antialiasing);
        const QPointF from = ramiel() ? ramielBox_.center() : QPointF(width() - 6, fr.top() + 4);
        const QPointF to = fr.center();
        const QColor glow(0xff, 0x7a, 0x2e);
        if (t < 0.3) {  // gathering
            const double g = t / 0.3;
            QColor c = glow;
            c.setAlphaF(0.25 + 0.5 * g);
            p.setPen(Qt::NoPen);
            p.setBrush(c);
            p.drawEllipse(from, 1.5 + 3.5 * g, 1.5 + 3.5 * g);
        } else {
            const double fade = t < 0.55 ? 1.0 : 1.0 - (t - 0.55) / 0.45;
            QColor outer = glow, core(0xff, 0xf4, 0xe6);
            outer.setAlphaF(0.35 * fade);
            core.setAlphaF(fade);
            p.setPen(QPen(outer, 7, Qt::SolidLine, Qt::RoundCap));
            p.drawLine(from, to);
            p.setPen(QPen(core, 1.8, Qt::SolidLine, Qt::RoundCap));
            p.drawLine(from, to);
            QPolygonF oct;  // the field holding: an octagon where it hits
            const double rr = 6 + 10 * (t - 0.3) / 0.7;
            for (int k = 0; k < 8; ++k) oct << to + QPointF(rr * std::cos(k * M_PI / 4 + M_PI / 8), rr * std::sin(k * M_PI / 4 + M_PI / 8));
            QColor ring = th.warm();
            ring.setAlphaF(0.8 * fade);
            p.setPen(QPen(ring, 1.4));
            p.setBrush(Qt::NoBrush);
            p.drawPolygon(oct);
        }
    }
    // A fly-by: the Angel crosses the field once, turning, and is gone.
    if (const double t = effect(flyAt_, kFlyMs); t >= 0) {
        const double s = fr.height() * 0.55;
        const double e = t * t * (3 - 2 * t);  // ease in and out
        const QRectF box(-s + (width() + s) * e, fr.center().y() - s / 2 - std::sin(t * M_PI) * fr.height() * 0.15, s, s);
        p.setOpacity(std::sin(t * M_PI));
        paintRamiel(p, box, 0, 0, t * 4 * M_PI);
        p.setOpacity(1);
    }
}

}  // namespace graphite
