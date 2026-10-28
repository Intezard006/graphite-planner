#include "planner/progress.hpp"

#include <QPainter>
#include <QRegularExpression>
#include <cmath>

#include "planner/tasks.hpp"
#include "ui/theme.hpp"

namespace graphite {

int progressStepKey(const QKeyEvent* e) {
    if (!(e->modifiers() & Qt::AltModifier)) return 0;
    switch (e->key()) {
        case Qt::Key_Equal:
        case Qt::Key_Plus: return 1;
        case Qt::Key_Minus: return -1;
        default: break;
    }
#ifdef Q_OS_MACOS
    switch (e->nativeVirtualKey()) {  // kVK_ANSI_Equal, kVK_ANSI_Minus, keypad plus / minus
        case 0x18:
        case 0x45: return 1;
        case 0x1B:
        case 0x4E: return -1;
        default: break;
    }
    const QString t = e->text();  // Option+= "≠", Option+Shift+= "±", Option+- "–", Option+Shift+- "—"
    if (t == QString::fromUtf8("\u2260") || t == QString::fromUtf8("\u00B1")) return 1;
    if (t == QString::fromUtf8("\u2013") || t == QString::fromUtf8("\u2014")) return -1;
#endif
    return 0;
}

namespace {
const QRegularExpression& tokenRe() {
    static const QRegularExpression re(
        R"(\{progress\s+(-?\d+(?:\.\d+)?)\s*/\s*(-?\d+(?:\.\d+)?)(?:\s+(?!from\b|by\b|step\b)([^\s{}\d][^\s{}]*))?)"
        R"((?:\s+from\s+(-?\d+(?:\.\d+)?))?(?:\s+(?:by|step)\s+(\d+(?:\.\d+)?))?\s*\})");
    return re;
}
QString num(double v) { return QString::number(std::round(v * 1e6) / 1e6, 'g', 12); }
}  // namespace

double Progress::ratio() const {
    if (end <= start) return value >= end ? 1.0 : 0.0;
    return qBound(0.0, (value - start) / (end - start), 1.0);
}

bool findProgress(const QString& text, Progress* p) {
    const QRegularExpressionMatch m = tokenRe().match(text);
    if (!m.hasMatch()) return false;
    Progress r;
    r.value = m.captured(1).toDouble();
    r.end = m.captured(2).toDouble();
    r.unit = m.captured(3);
    if (m.hasCaptured(4)) r.start = m.captured(4).toDouble();
    if (m.hasCaptured(5) && m.captured(5).toDouble() > 0) r.step = m.captured(5).toDouble();
    r.valueAt = static_cast<int>(m.capturedStart(1));
    r.valueLength = static_cast<int>(m.capturedLength(1));
    r.at = static_cast<int>(m.capturedStart());
    r.length = static_cast<int>(m.capturedLength());
    if (p) *p = r;
    return true;
}

QString progressToken(const Progress& p) {
    QString t = "{progress " + num(p.value) + "/" + num(p.end);
    if (!p.unit.isEmpty()) t += " " + p.unit;
    if (p.start != 0) t += " from " + num(p.start);
    if (p.step != 1) t += " by " + num(p.step);
    return t + "}";
}

QString progressNumbers(const Progress& p) {
    QString s = num(p.value) + " / " + num(p.end);
    if (!p.unit.isEmpty()) s += " " + p.unit.toUpper();
    return s;
}

QString progressMessage(const QString& text, bool eva) {
    Progress p;
    if (!findProgress(text, &p)) return QString();
    if (p.full())
        return (eva ? "Sync ratio 100%  \xC2\xB7  " : "Complete  \xC2\xB7  ") + taskTitle(text.left(p.at) + text.mid(p.at + p.length));
    QString cells;
    for (int i = 0; i < 10; ++i) cells += i < static_cast<int>(p.ratio() * 10 + 1e-9) ? QChar(0x25AE) : QChar(0x25AF);
    return QString("%1%2%  %3  %4").arg(eva ? "Sync " : "").arg(static_cast<int>(std::floor(p.ratio() * 100 + 1e-9))).arg(cells, progressNumbers(p));
}

QString stepProgress(const QString& text, int steps, bool finish, bool start) {
    Progress p;
    if (!findProgress(text, &p)) return text;
    const bool wasFull = p.full();
    const double lo = qMin(p.start, p.end), hi = qMax(p.start, p.end);
    p.value = qBound(lo, p.value + steps * p.step, hi);
    QString out = text.left(p.valueAt) + num(p.value) + text.mid(p.valueAt + p.valueLength);  // the rest as written
    const QString m = taskMarker(out);
    if (m.isEmpty() || m.startsWith("CANCEL")) return out;
    if (finish && p.full() && m != "DONE") return setTaskMarker(out, "DONE");
    if (finish && wasFull && !p.full() && m == "DONE") return setTaskMarker(out, "DOING");
    if (start && steps > 0 && (m == "TODO" || m == "LATER")) return setTaskMarker(out, m == "LATER" ? "NOW" : "DOING");
    return out;
}

QImage drawProgress(const Progress& p, const Theme& th, qreal dpr, int w, int h) {
    QImage img(QSize(w, h) * dpr, QImage::Format_ARGB32_Premultiplied);
    img.setDevicePixelRatio(dpr);
    img.fill(Qt::transparent);
    QPainter g(&img);
    g.setRenderHint(QPainter::Antialiasing);
    const int cells = 20, gap = 2;
    const qreal cw = (w - 6.0 - (cells - 1) * gap) / cells;  // 3 px brackets each side
    const QColor on = p.full() ? th.accent() : th.warm();
    const qreal filled = p.ratio() * cells;
    for (int i = 0; i < cells; ++i) {
        const QRectF r(3 + i * (cw + gap), 2, cw, h - 4);
        if (i + 1 <= filled + 1e-9) {
            g.fillRect(r, on);
        } else if (i < filled) {  // the cell being filled: part of it, dimmer
            g.fillRect(QRectF(r.x(), r.y(), r.width() * (filled - i), r.height()), on.darker(140));
            g.setPen(QPen(th.faint(), 1));
            g.drawRect(r.adjusted(0.5, 0.5, -0.5, -0.5));
        } else {
            g.setPen(QPen(th.faint(), 1));
            g.drawRect(r.adjusted(0.5, 0.5, -0.5, -0.5));
        }
    }
    // Brackets at both ends, as on a NERV gauge.
    g.setPen(QPen(p.full() ? th.accent() : th.dim(), 1.2));
    g.drawPolyline(QPolygonF({QPointF(3, 0.6), QPointF(0.6, 0.6), QPointF(0.6, h - 0.6), QPointF(3, h - 0.6)}));
    g.drawPolyline(QPolygonF({QPointF(w - 3, 0.6), QPointF(w - 0.6, 0.6), QPointF(w - 0.6, h - 0.6), QPointF(w - 3, h - 0.6)}));
    return img;
}

}  // namespace graphite
