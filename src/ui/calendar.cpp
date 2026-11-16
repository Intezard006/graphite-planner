#include "ui/calendar.hpp"

#include <QKeyEvent>
#include <QLocale>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QRegularExpression>
#include <QWheelEvent>

#include "ui/theme.hpp"

namespace graphite {

namespace {
QString daysAway(qint64 n) {
    if (n == 0) return "today";
    if (n == 1) return "tomorrow";
    if (n == -1) return "yesterday";
    return n > 0 ? QString("in %1 days").arg(n) : QString("%1 days ago").arg(-n);
}
const char* kChips[] = {"Today", "Tomorrow", "Sat", "Next Mon", "+1 week"};
constexpr int kChipCount = 5;
}  // namespace

CalendarPopup::CalendarPopup(QWidget* parent) : QWidget(parent) {
    setMouseTracking(true);
    setFocusPolicy(Qt::NoFocus);  // the editor keeps the keyboard and hands keys over
    hide();
}

void CalendarPopup::open(const QString& kind, const QDate& start, const QHash<QDate, DayMarks>& marks) {
    kind_ = kind;
    marks_ = marks;
    typed_.clear();
    hover_ = QDate();
    sel_ = start.isValid() ? start : QDate::currentDate();
    relayout();
    update();
}

void CalendarPopup::relayout() {
    const Theme& th = Theme::instance();
    const QFontMetrics bm(th.bodyFont()), lm(th.labelFont());
    cell_ = qMax(30, static_cast<int>(bm.lineSpacing() * 1.75));
    pad_ = 12;
    headH_ = lm.height() + 12;
    chipsH_ = lm.height() + 14;
    weekH_ = lm.height() + 8;
    footH_ = lm.height() * 2 + 18;
    int w = cell_ * 7;
    int chips = 0;
    for (const char* c : kChips) chips += lm.horizontalAdvance(QString::fromUtf8(c).toUpper()) + 14 + 6;
    w = qMax(w, chips - 6);
    resize(w + 2 * pad_, pad_ + headH_ + chipsH_ + weekH_ + cell_ * 6 + footH_ + pad_ / 2);
}

QDate CalendarPopup::gridStart() const {
    const QDate first = month();
    return first.addDays(-(first.dayOfWeek() - 1));
}

QRect CalendarPopup::dayRect(const QDate& d) const {
    const qint64 k = gridStart().daysTo(d);
    if (k < 0 || k >= 42) return QRect();
    const int x0 = (width() - 7 * cell_) / 2;
    const int y0 = pad_ + headH_ + chipsH_ + weekH_;
    return QRect(x0 + static_cast<int>(k % 7) * cell_, y0 + static_cast<int>(k / 7) * cell_, cell_, cell_);
}

QRect CalendarPopup::arrowRect(int dir) const {
    const int s = headH_;
    return dir < 0 ? QRect(width() - pad_ - 2 * s - 4, pad_ - 4, s, s) : QRect(width() - pad_ - s, pad_ - 4, s, s);
}

QRect CalendarPopup::chipRect(int k) const {
    if (k < 0 || k >= kChipCount) return QRect();
    const QFontMetrics lm(Theme::instance().labelFont());
    int x = pad_;
    for (int i = 0; i < k; ++i) x += lm.horizontalAdvance(QString::fromUtf8(kChips[i]).toUpper()) + 14 + 6;
    return QRect(x, pad_ + headH_, lm.horizontalAdvance(QString::fromUtf8(kChips[k]).toUpper()) + 14, chipsH_ - 8);
}

QDate CalendarPopup::chipDate(int k) const {
    const QDate today = QDate::currentDate();
    auto next = [&](int dow) {  // the coming one, never today
        const int add = (dow - today.dayOfWeek() + 7) % 7;
        return today.addDays(add == 0 ? 7 : add);
    };
    switch (k) {
        case 0: return today;
        case 1: return today.addDays(1);
        case 2: return next(6);
        case 3: return next(1);
        case 4: return today.addDays(7);
        default: return QDate();
    }
}

QString CalendarPopup::footer() const {
    if (!typed_.isEmpty()) return "Type a date  \xC2\xB7  " + typed_;
    QString s = QLocale::c().toString(sel_, "ddd d MMM yyyy") + "  \xC2\xB7  " + daysAway(QDate::currentDate().daysTo(sel_));
    const DayMarks m = marks_.value(sel_);
    auto add = [&](int n, const char* one, const char* many) {
        if (n > 0) s += "  \xC2\xB7  " + QString::number(n) + " " + QString::fromUtf8(n == 1 ? one : many);
    };
    add(m.exams, "exam", "exams");
    add(m.deadlines, "deadline", "deadlines");
    add(m.scheduled, "task", "tasks");
    return s;
}

void CalendarPopup::select(const QDate& d) {
    if (!d.isValid()) return;
    sel_ = d;
    update();
}

// Typed digits: "2026-12-01" (or 2026-12-1), "12-01" (this year), or a day of the shown month.
void CalendarPopup::typed() {
    static const QRegularExpression full(R"(^(\d{4})-(\d{1,2})-(\d{1,2})$)"), md(R"(^(\d{1,2})-(\d{1,2})$)"), day(R"(^(\d{1,2})$)");
    QRegularExpressionMatch m;
    QDate d;
    if ((m = full.match(typed_)).hasMatch()) d = QDate(m.captured(1).toInt(), m.captured(2).toInt(), m.captured(3).toInt());
    else if ((m = md.match(typed_)).hasMatch()) d = QDate(sel_.year(), m.captured(1).toInt(), m.captured(2).toInt());
    else if ((m = day.match(typed_)).hasMatch()) d = QDate(sel_.year(), sel_.month(), m.captured(1).toInt());
    if (d.isValid()) sel_ = d;
    update();
}

bool CalendarPopup::handleKey(QKeyEvent* e) {
    if (e->modifiers() & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier)) return false;  // Ctrl+S and friends
    switch (e->key()) {
        case Qt::Key_Left: select(sel_.addDays(-1)); break;
        case Qt::Key_Right: select(sel_.addDays(1)); break;
        case Qt::Key_Up: select(sel_.addDays(-7)); break;
        case Qt::Key_Down: select(sel_.addDays(7)); break;
        case Qt::Key_PageUp: select(sel_.addMonths(-1)); break;
        case Qt::Key_PageDown: select(sel_.addMonths(1)); break;
        case Qt::Key_Home: select(QDate::currentDate()); break;
        case Qt::Key_Return:
        case Qt::Key_Enter:
        case Qt::Key_Tab: emit picked(sel_); break;
        case Qt::Key_Escape:
            if (!typed_.isEmpty()) {  // first Esc clears what was typed
                typed_.clear();
                update();
            } else {
                emit cancelled();
            }
            break;
        case Qt::Key_Backspace:
            typed_.chop(1);
            typed();
            break;
        default: {
            const QString t = e->text();
            if (t.size() != 1) return true;
            const QChar c = t[0];
            if (c.isDigit() || (c == '-' && !typed_.isEmpty())) {
                if (typed_.size() < 10) typed_ += c;
                typed();
            } else if (c.toLower() == 't' && typed_.isEmpty()) {
                select(QDate::currentDate());
            }
            break;  // anything else is swallowed: the block's text stays as it is
        }
    }
    if (e->key() != Qt::Key_Backspace && !(e->text().size() == 1 && (e->text()[0].isDigit() || e->text()[0] == '-'))) typed_.clear();
    return true;
}

void CalendarPopup::mousePressEvent(QMouseEvent* e) {
    const QPoint p = e->position().toPoint();
    for (int dir : {-1, 1})
        if (arrowRect(dir).contains(p)) {
            select(sel_.addMonths(dir));
            return;
        }
    for (int k = 0; k < kChipCount; ++k)
        if (chipRect(k).contains(p)) {
            emit picked(chipDate(k));
            return;
        }
    for (int i = 0; i < 42; ++i) {
        const QDate d = gridStart().addDays(i);
        if (dayRect(d).contains(p)) {
            sel_ = d;
            emit picked(d);
            return;
        }
    }
}

void CalendarPopup::mouseMoveEvent(QMouseEvent* e) {
    QDate h;
    for (int i = 0; i < 42 && !h.isValid(); ++i)
        if (dayRect(gridStart().addDays(i)).contains(e->position().toPoint())) h = gridStart().addDays(i);
    if (h != hover_) {
        hover_ = h;
        update();
    }
}

void CalendarPopup::leaveEvent(QEvent*) {
    hover_ = QDate();
    update();
}

void CalendarPopup::wheelEvent(QWheelEvent* e) {
    const int dy = e->angleDelta().y();
    if (dy != 0) select(sel_.addMonths(dy > 0 ? -1 : 1));
    e->accept();
}

void CalendarPopup::paintEvent(QPaintEvent*) {
    const Theme& th = Theme::instance();
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    QColor fill = th.palette().panel;
    fill.setAlpha(th.transparent() ? 235 : 255);
    p.fillRect(rect(), fill);
    p.setPen(QPen(th.line(), 1));
    p.drawRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5));
    const bool deadline = kind_ == "DEADLINE";
    p.fillRect(QRect(0, 0, width(), 2), deadline ? th.warm() : th.accent());

    const QFont label = th.labelFont();
    const QFontMetrics lm(label);
    // Title: the kind on the left, the month and its arrows on the right.
    p.setFont(label);
    p.setPen(deadline ? th.warm() : th.accent());
    const QRect head(pad_, pad_ - 4, width() - 2 * pad_, headH_);
    p.drawText(head, Qt::AlignVCenter | Qt::AlignLeft, kind_);
    p.setPen(th.text());
    const QString monthText = QLocale::c().toString(month(), "MMMM yyyy").toUpper();
    p.drawText(head.adjusted(0, 0, -2 * headH_ - 10, 0), Qt::AlignVCenter | Qt::AlignRight, monthText);
    for (int dir : {-1, 1}) {
        const QRectF a = arrowRect(dir);
        const QPointF c = a.center();
        QPainterPath tri;
        tri.moveTo(c + QPointF(dir * 3.5, 0));
        tri.lineTo(c + QPointF(-dir * 2.5, -4.5));
        tri.lineTo(c + QPointF(-dir * 2.5, 4.5));
        tri.closeSubpath();
        p.setPen(Qt::NoPen);
        p.setBrush(th.dim());
        p.drawPath(tri);
    }
    // Quick picks
    for (int k = 0; k < kChipCount; ++k) {
        const QRectF r = chipRect(k);
        const bool on = chipDate(k) == sel_;
        QColor c = on ? th.accent() : th.structure();
        c.setAlpha(on ? 60 : 30);
        p.setPen(QPen(on ? th.accent() : th.line(), 1));
        p.setBrush(c);
        p.drawRoundedRect(r.adjusted(0.5, 0.5, -0.5, -0.5), 3, 3);
        p.setPen(on ? th.accent() : th.dim());
        p.drawText(r, Qt::AlignCenter, QString::fromUtf8(kChips[k]).toUpper());
    }
    // Weekday names
    const int x0 = (width() - 7 * cell_) / 2;
    const int wy = pad_ + headH_ + chipsH_;
    static const char* names[] = {"M", "T", "W", "T", "F", "S", "S"};
    p.setPen(th.faint());
    for (int i = 0; i < 7; ++i) p.drawText(QRect(x0 + i * cell_, wy, cell_, weekH_), Qt::AlignCenter, names[i]);
    // Days
    const QDate today = QDate::currentDate();
    const QFont body = th.bodyFont();
    QFont bold = body;
    bold.setBold(true);
    for (int i = 0; i < 42; ++i) {
        const QDate d = gridStart().addDays(i);
        const QRectF r = QRectF(dayRect(d)).adjusted(2, 2, -2, -2);
        const bool inMonth = d.month() == sel_.month();
        if (d == sel_) {
            QColor tint = deadline ? th.warm() : th.accent();
            tint.setAlpha(46);
            p.setPen(QPen(deadline ? th.warm() : th.accent(), 1.2));
            p.setBrush(tint);
            p.drawRoundedRect(r, 4, 4);
        } else if (d == hover_) {
            QColor tint = th.structure();
            tint.setAlpha(40);
            p.setPen(Qt::NoPen);
            p.setBrush(tint);
            p.drawRoundedRect(r, 4, 4);
        }
        if (d == today) {  // today: an outline (inside the selection when both)
            p.setPen(QPen(th.structure(), 1.2));
            p.setBrush(Qt::NoBrush);
            p.drawRoundedRect(r.adjusted(2, 2, -2, -2), 3, 3);
        }
        p.setFont(d == today || d == sel_ ? bold : body);
        p.setPen(!inMonth ? th.faint() : d < today ? th.dim() : d == sel_ ? th.text() : d.dayOfWeek() >= 6 ? th.dim() : th.text());
        p.drawText(r.adjusted(0, -4, 0, -4), Qt::AlignCenter, QString::number(d.day()));
        // Marks: red exam, amber deadline, accent scheduled; a dot each, under the number.
        const DayMarks m = marks_.value(d);
        QList<QColor> dots;
        if (m.exams) dots << th.alarm();
        if (m.deadlines) dots << QColor(0xff, 0xb0, 0x00);
        if (m.scheduled) dots << th.accent();
        const qreal dy = r.bottom() - 5, w = 6;
        qreal dx = r.center().x() - (dots.size() - 1) * w / 2;
        p.setPen(Qt::NoPen);
        for (QColor c : dots) {
            if (!inMonth) c.setAlpha(110);
            p.setBrush(c);
            p.drawEllipse(QPointF(dx, dy), 2.0, 2.0);
            dx += w;
        }
    }
    // Footer: the day chosen and what is on it; then the keys.
    const int fy = pad_ + headH_ + chipsH_ + weekH_ + 6 * cell_ + 4;
    p.setFont(label);
    p.setPen(typed_.isEmpty() ? th.text() : th.accent());
    p.drawText(QRect(pad_, fy, width() - 2 * pad_, lm.height() + 4), Qt::AlignVCenter | Qt::AlignLeft,
               lm.elidedText(footer().toUpper(), Qt::ElideRight, width() - 2 * pad_));
    p.setPen(th.faint());
    p.drawText(QRect(pad_, fy + lm.height() + 6, width() - 2 * pad_, lm.height() + 4), Qt::AlignVCenter | Qt::AlignLeft,
               lm.elidedText(QString::fromUtf8("\xE2\x86\x90\xE2\x86\x92\xE2\x86\x91\xE2\x86\x93 DAY  \xC2\xB7  PGUP/DN MONTH  \xC2\xB7  T TODAY  \xC2\xB7  ENTER  \xC2\xB7  ESC"), Qt::ElideRight,
                             width() - 2 * pad_));
}

}  // namespace graphite
