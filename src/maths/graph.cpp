#include "maths/graph.hpp"

#include "maths/calc.hpp"
#include "ui/theme.hpp"

#include <QPainter>
#include <QPainterPath>
#include <QRegularExpression>
#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>

namespace graphite {

namespace {

constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();

// Commas and semicolons outside brackets.
QStringList clauses(const QString& spec) {
    QStringList out;
    int depth = 0, from = 0;
    for (int i = 0; i <= spec.size(); ++i) {
        const QChar c = i < spec.size() ? spec[i] : QChar(',');
        if (c == '(' || c == '[' || c == '{') ++depth;
        else if (c == ')' || c == ']' || c == '}') --depth;
        else if ((c == ',' || c == ';') && depth <= 0) {
            const QString part = spec.mid(from, i - from).trimmed();
            if (!part.isEmpty()) out << part;
            from = i + 1;
        }
    }
    return out;
}

// The SI size of one `unit` of the kind `dims` ("" = 1).
bool unitScale(const QString& unit, const std::array<int, 6>& dims, double* scale) {
    if (unit.isEmpty()) {
        *scale = 1;
        return true;
    }
    CalcQuantity one;
    one.si = 1;
    one.dims = dims;
    double v = 0;
    if (!quantityIn(one, unit, &v) || v == 0 || !std::isfinite(v)) return false;
    *scale = 1 / v;
    return true;
}

// "s^-1" -> "s⁻¹", "m^2" -> "m²".
QString prettyUnit(QString u) {
    static const QRegularExpression pw(R"(\^([-−]?\d+))");
    static const QString sup = QString::fromUtf8("⁰¹²³⁴⁵⁶⁷⁸⁹");
    for (QRegularExpressionMatch m = pw.match(u); m.hasMatch(); m = pw.match(u)) {
        QString s;
        for (QChar ch : m.captured(1)) s += ch.isDigit() ? sup[ch.digitValue()] : QChar(0x207B);
        u.replace(m.capturedStart(), m.capturedLength(), s);
    }
    return u;
}

}  // namespace

double niceStep(double range, int count) {
    const double raw = range / count;
    const double mag = std::pow(10.0, std::floor(std::log10(raw)));
    const double f = raw / mag;
    return (f < 1.5 ? 1 : f < 3 ? 2 : f < 7 ? 5 : 10) * mag;
}

QString graphSpec(const QString& line) {
    static const QRegularExpression re(R"(^=\s*graph\s*\{(.*?)\}?$)");
    const QRegularExpressionMatch m = re.match(line.trimmed());
    if (!m.hasMatch()) return QString();
    const QString inner = m.captured(1).trimmed();
    return inner.isNull() ? QString("") : inner;
}

Graph makeGraph(const QString& spec, int samples) {
    Graph g;
    static const QRegularExpression range(R"(^([A-Za-z_]\w*)\s+from\s+(.+?)\s+to\s+(.+)$)", QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression named(R"(^([A-Za-z_]\w*)\s*=(?!=)\s*(.+)$)");
    struct Range {
        QString name, lo, hi;
    };
    QList<Range> ranges;
    QStringList rest;
    for (const QString& c : clauses(spec)) {
        if (const QRegularExpressionMatch m = range.match(c); m.hasMatch()) ranges.append({m.captured(1), m.captured(2), m.captured(3)});
        else rest << c;
    }
    if (!ranges.isEmpty()) g.xName = ranges[0].name;

    // Constants: "name = value" that works out without the variable (in any order).
    CalcVars vars;
    QList<bool> isConst(rest.size(), false);
    for (bool progress = true; progress;) {
        progress = false;
        for (int i = 0; i < rest.size(); ++i) {
            const QRegularExpressionMatch m = named.match(rest[i]);
            if (isConst[i] || !m.hasMatch() || m.captured(1) == g.xName) continue;
            CalcQuantity q;
            if (evaluateQuantity(m.captured(2), &q, nullptr, &vars)) {
                // "A = 1e10 s^-1": the unit as written, so results show in it (not GHz).
                static const QRegularExpression numUnit(R"(^[-+]?(\d+\.?\d*|\.\d+)([eE][-+]?\d+)?\s*([^\d\s].*)$)");
                if (const QRegularExpressionMatch u = numUnit.match(m.captured(2).trimmed()); u.hasMatch()) {
                    double v = 0;
                    if (quantityIn(q, u.captured(3).trimmed(), &v)) q.unit = u.captured(3).trimmed();
                }
                vars.insert(m.captured(1), q);
                isConst[i] = true;
                progress = true;
            }
        }
    }
    for (int i = 0; i < rest.size(); ++i) {
        if (isConst[i]) continue;
        GraphCurve c;
        if (const QRegularExpressionMatch m = named.match(rest[i]); m.hasMatch()) {
            c.label = m.captured(1);
            c.expr = m.captured(2).trimmed();
        } else {
            c.label = c.expr = rest[i];
        }
        g.curves << c;
    }
    if (g.curves.isEmpty()) {
        g.error = "write a function, e.g. =graph{x^2 - 2, x from -3 to 3}";
        return g;
    }

    // The variable's range, in the unit its start is written in.
    CalcQuantity lo, hi;
    lo.si = -10;
    hi.si = 10;
    if (!ranges.isEmpty()) {
        QString e;
        if (!evaluateQuantity(ranges[0].lo, &lo, &e, &vars) || !evaluateQuantity(ranges[0].hi, &hi, &e, &vars)) {
            g.error = g.xName + " from ... to ...: " + e;
            return g;
        }
        if (lo.dims != hi.dims) {
            g.error = "the two ends of the range are different kinds of quantity";
            return g;
        }
    }
    g.xDims = lo.dims;
    g.xUnit = lo.unit.isEmpty() ? hi.unit : lo.unit;
    if (!unitScale(g.xUnit, g.xDims, &g.xScale)) g.xUnit.clear(), g.xScale = 1;
    const QString xUnitWritten = g.xUnit;
    g.x0 = lo.si / g.xScale;
    g.x1 = hi.si / g.xScale;
    if (!(g.x1 > g.x0)) {
        g.error = "the range must go up, e.g. " + g.xName + " from 0 to 5";
        return g;
    }
    const int n = qBound(16, samples, 4000);
    g.xs.resize(n);
    for (int i = 0; i < n; ++i) g.xs[i] = g.x0 + (g.x1 - g.x0) * i / (n - 1);

    // Each curve at every sample; one function for the refinements below.
    CalcQuantity xq;
    xq.dims = g.xDims;
    xq.unit = g.xUnit;
    auto at = [&](const QString& expr, double x, CalcQuantity* q, QString* err) {
        xq.si = x * g.xScale;
        vars.insert(g.xName, xq);
        return evaluateQuantity(expr, q, err, &vars);
    };
    bool haveDims = false;
    for (GraphCurve& c : g.curves) {
        c.y.resize(n);
        c.breakAfter.fill(false, n);
        QString firstError;
        bool any = false;
        for (int i = 0; i < n; ++i) {
            CalcQuantity q;
            QString err;
            if (!at(c.expr, g.xs[i], &q, &err)) {
                c.y[i] = kNaN;
                if (firstError.isEmpty()) firstError = err;
                continue;
            }
            if (!haveDims) {
                haveDims = true;
                g.yDims = q.dims;
                g.yUnit = q.unit;
                if (!unitScale(g.yUnit, g.yDims, &g.yScale)) g.yUnit.clear(), g.yScale = 1;
            } else if (q.dims != g.yDims) {
                g.error = g.curves.size() > 1 ? "the curves are different kinds of quantity" : c.label + " changes its unit";
                return g;
            }
            c.y[i] = q.si / g.yScale;
            any = true;
        }
        if (!any) {
            g.error = (g.curves.size() > 1 ? c.label + ": " : QString()) + firstError;
            return g;
        }
    }
    g.xUnit = prettyUnit(xUnitWritten);
    g.yUnit = prettyUnit(g.yUnit);
    const bool allNamed = std::all_of(g.curves.begin(), g.curves.end(), [&](const GraphCurve& c) { return c.label != c.expr; });
    if (g.curves.size() == 1 && allNamed) g.yName = g.curves[0].label;

    auto value = [&](int curve, double x) {
        CalcQuantity q;
        return at(g.curves[curve].expr, x, &q, nullptr) ? q.si / g.yScale : kNaN;
    };
    auto bisect = [](const std::function<double(double)>& f, double a, double b) {
        double fa = f(a);
        for (int k = 0; k < 60; ++k) {
            const double m = (a + b) / 2, fm = f(m);
            if (!std::isfinite(fm)) break;
            if ((fm < 0) == (fa < 0)) a = m, fa = fm;
            else b = m;
        }
        return (a + b) / 2;
    };
    const double span = g.x1 - g.x0;

    // Asymptotes, roots and turning points of each curve.
    for (int ci = 0; ci < g.curves.size(); ++ci) {
        GraphCurve& c = g.curves[ci];
        // The curve's usual size, to tell a value near infinity from a large one.
        QVector<double> mags;
        for (double v : c.y)
            if (std::isfinite(v)) mags << std::fabs(v);
        std::nth_element(mags.begin(), mags.begin() + mags.size() / 2, mags.end());
        const double huge = 1e3 * std::max(mags[mags.size() / 2], 1e-300);
        for (int i = 0; i + 1 < n; ++i) {
            const double a = c.y[i], b = c.y[i + 1];
            if (!std::isfinite(a) || !std::isfinite(b)) continue;
            const double xm = (g.xs[i] + g.xs[i + 1]) / 2, mid = value(ci, xm);
            const double big = std::max(std::fabs(a), std::fabs(b));
            const bool flips = (a < 0) != (b < 0) && a != 0 && b != 0;
            auto pole = [&](double x) {
                c.breakAfter[i] = true;
                if (g.poles.isEmpty() || std::fabs(g.poles.back() - x) > span * 1e-6) g.poles << x;
            };
            // Not defined between two defined samples: it runs off to infinity there.
            if (!std::isfinite(mid)) {
                pole(xm);
                continue;
            }
            if (flips) {
                // A change of sign: narrowed down, a root goes to zero and an asymptote (tan,
                // 1/x) to infinity.
                const double r = bisect([&](double x) { return value(ci, x); }, g.xs[i], g.xs[i + 1]);
                const double fr = value(ci, r);
                if (!std::isfinite(fr) || std::fabs(fr) > big) pole(r);
                else g.marks.append({GraphMark::Root, ci, -1, r, 0});
                continue;
            }
            if (std::fabs(mid) > 8 * big && std::fabs(mid) > huge) {  // 1/x² between two samples
                pole(xm);
                continue;
            }
            if (a == 0 && (i == 0 || c.y[i - 1] != 0)) g.marks.append({GraphMark::Root, ci, -1, g.xs[i], 0});
        }
        if (c.y[n - 1] == 0) g.marks.append({GraphMark::Root, ci, -1, g.xs[n - 1], 0});
        for (int i = 1; i + 1 < n; ++i) {
            const double a = c.y[i - 1], m = c.y[i], b = c.y[i + 1];
            if (!std::isfinite(a) || !std::isfinite(m) || !std::isfinite(b) || c.breakAfter[i - 1] || c.breakAfter[i]) continue;
            const bool max = m > a && m >= b, min = m < a && m <= b;
            if ((!max && !min) || std::fabs(m) > huge) continue;
            // Golden-section search between the neighbours.
            double l = g.xs[i - 1], r = g.xs[i + 1];
            const double gr = (std::sqrt(5.0) - 1) / 2;
            for (int k = 0; k < 50; ++k) {
                const double p = r - gr * (r - l), q = l + gr * (r - l);
                const double fp = value(ci, p), fq = value(ci, q);
                if (!std::isfinite(fp) || !std::isfinite(fq)) break;
                if (max ? fp > fq : fp < fq) r = q;
                else l = p;
            }
            const double x = (l + r) / 2, y = value(ci, x);
            if (std::isfinite(y)) g.marks.append({max ? GraphMark::Max : GraphMark::Min, ci, -1, x, y});
        }
    }
    // Where two curves meet.
    for (int a = 0; a < g.curves.size(); ++a)
        for (int b = a + 1; b < g.curves.size(); ++b) {
            const GraphCurve &ca = g.curves[a], &cb = g.curves[b];
            for (int i = 0; i + 1 < n; ++i) {
                const double d0 = ca.y[i] - cb.y[i], d1 = ca.y[i + 1] - cb.y[i + 1];
                if (!std::isfinite(d0) || !std::isfinite(d1) || ca.breakAfter[i] || cb.breakAfter[i]) continue;
                if (d0 != 0 && (d0 < 0) == (d1 < 0)) continue;
                if (d0 == 0 && i > 0 && ca.y[i - 1] - cb.y[i - 1] == 0) continue;
                const double x = d0 == 0 ? g.xs[i] : bisect([&](double t) { return value(a, t) - value(b, t); }, g.xs[i], g.xs[i + 1]);
                g.marks.append({GraphMark::Cross, a, b, x, value(a, x)});
            }
        }
    for (GraphMark& m : g.marks)  // within rounding of zero: zero
        if (std::fabs(m.x) < span * 1e-7) m.x = 0;
    std::stable_sort(g.marks.begin(), g.marks.end(), [](const GraphMark& p, const GraphMark& q) { return p.x < q.x; });
    std::sort(g.poles.begin(), g.poles.end());

    // The y range: given, or the curves' (the middle 90 % when something runs off to infinity).
    if (ranges.size() > 1) {
        CalcQuantity ylo, yhi;
        if (evaluateQuantity(ranges[1].lo, &ylo, nullptr, &vars) && evaluateQuantity(ranges[1].hi, &yhi, nullptr, &vars) &&
            ylo.dims == yhi.dims && (ylo.dims == g.yDims || (ylo.dims == std::array<int, 6>{} && ylo.unit.isEmpty()))) {
            const bool plainEnds = ylo.dims == std::array<int, 6>{} && g.yDims != std::array<int, 6>{};
            g.y0 = plainEnds ? ylo.si : ylo.si / g.yScale;  // bare numbers count in the curve's unit
            g.y1 = plainEnds ? yhi.si : yhi.si / g.yScale;
            g.yGiven = g.y1 > g.y0;
        }
        if (!g.yGiven) {
            g.error = ranges[1].name + " from ... to ...: not a range of " + g.yName;
            return g;
        }
    }
    if (!g.yGiven) {
        QVector<double> all;
        for (const GraphCurve& c : g.curves)
            for (double v : c.y)
                if (std::isfinite(v)) all << v;
        std::sort(all.begin(), all.end());
        double lo2 = all.front(), hi2 = all.back();
        if (!g.poles.isEmpty() && all.size() > 20) {
            lo2 = all[all.size() / 20];
            hi2 = all[all.size() - 1 - all.size() / 20];
            const double m = (hi2 - lo2) * 0.25;
            lo2 -= m;
            hi2 += m;
        }
        if (hi2 - lo2 < 1e-12 * std::max(1.0, std::fabs(hi2))) {
            const double d = std::fabs(hi2) > 0 ? std::fabs(hi2) * 0.5 : 1;
            lo2 -= d;
            hi2 += d;
        }
        const double pad = (hi2 - lo2) * 0.08;
        g.y0 = lo2 - pad;
        g.y1 = hi2 + pad;
    }
    g.ok = true;
    return g;
}

QString graphSummary(const Graph& g, bool eva) {
    const QString dot = QString::fromUtf8("   ·   ");
    auto num = [](double v) { return calcNumber(std::fabs(v) < 1e-12 ? 0 : v); };
    auto xText = [&](double x) { return g.xName + " = " + num(x) + (g.xUnit.isEmpty() ? QString() : " " + g.xUnit); };
    auto pt = [&](const GraphMark& m) { return "(" + num(m.x) + ", " + num(m.y) + ")"; };
    const bool many = g.curves.size() > 1;
    auto who = [&](const GraphMark& m) { return many ? g.curves[m.curve].label + " " : QString(); };
    QStringList parts;
    if (eva) {
        int target = 0;
        for (const GraphMark& m : g.marks) {
            if (parts.size() >= 8) break;
            switch (m.kind) {
            case GraphMark::Root: parts << QString("Target %1  ").arg(++target, 2, 10, QChar('0')) + who(m) + xText(m.x); break;
            case GraphMark::Max: parts << "Apex  " + who(m) + pt(m); break;
            case GraphMark::Min: parts << "Nadir  " + who(m) + pt(m); break;
            case GraphMark::Cross: parts << "Contact  " + pt(m); break;
            }
        }
        for (double p : g.poles)
            if (parts.size() < 10) parts << "Pattern Blue  " + xText(p);
    } else {
        QStringList roots;
        for (const GraphMark& m : g.marks)
            if (m.kind == GraphMark::Root && roots.size() < 6) roots << who(m) + num(m.x);
        if (!roots.isEmpty()) parts << (roots.size() == 1 ? "Root  " : "Roots  ") + g.xName + " = " + roots.join(", ");
        for (const GraphMark& m : g.marks) {
            if (parts.size() >= 8) break;
            if (m.kind == GraphMark::Max) parts << "max  " + who(m) + pt(m);
            else if (m.kind == GraphMark::Min) parts << "min  " + who(m) + pt(m);
            else if (m.kind == GraphMark::Cross) parts << "meet  " + pt(m);
        }
        for (double p : g.poles)
            if (parts.size() < 10) parts << "asymptote  " + xText(p);
    }
    return parts.join(dot);
}

QImage drawGraph(const Graph& gr, const Theme& th, qreal dpr, int w, int h) {
    const bool eva = th.immersion();
    QImage img(QSize(w, h) * dpr, QImage::Format_ARGB32_Premultiplied);
    img.setDevicePixelRatio(dpr);
    img.fill(Qt::transparent);
    QPainter p(&img);
    p.setRenderHint(QPainter::Antialiasing);

    QFont lf = th.labelFont();
    QFont tf = th.monoFont();
    tf.setPointSizeF(lf.pointSizeF());
    QFont af = lf;
    af.setCapitalization(QFont::MixedCase);  // units are case-sensitive: mL is not ML
    const QFontMetrics lm(lf), tm(tf), am(af);
    const QColor alarm(0xff, 0x4d, 0x4d);
    const QColor tick = eva ? th.warm() : th.dim();

    // The panel: in immersion a tactical display with its heading; plain, just the chart.
    int top = 6;
    if (eva) {
        QColor bg = th.palette().panel;
        bg.setAlpha(150);
        p.setPen(QPen(th.line(), 1));
        p.setBrush(bg);
        p.drawRect(QRectF(0.5, 0.5, w - 1, h - 1));
        p.setBrush(Qt::NoBrush);
        p.setPen(Qt::NoPen);
        p.setBrush(th.accent());
        p.drawRect(QRectF(0, 0, 3, lm.height() + 10));  // the accent tab
        p.setFont(lf);
        p.setPen(th.accent());
        p.drawText(QRectF(10, 5, w - 20, lm.height()), Qt::AlignLeft | Qt::AlignVCenter, QString::fromUtf8("MAGI  ·  Function analysis"));
        p.setPen(th.faint());
        p.drawText(QRectF(10, 5, w - 20, lm.height()), Qt::AlignRight | Qt::AlignVCenter,
                   QString("%1 sample%2  ·  %3 curve%4")
                       .arg(gr.xs.size())
                       .arg(gr.xs.size() == 1 ? "" : "s")
                       .arg(gr.curves.size())
                       .arg(gr.curves.size() == 1 ? "" : "s"));
        top = lm.height() + 16;
    }

    const double xs = niceStep(gr.x1 - gr.x0, 6), ys = niceStep(gr.y1 - gr.y0, 4);
    auto label = [](double v, double step) { return calcNumber(std::fabs(v) < step * 1e-9 ? 0 : v); };
    int tickW = 0;
    for (double v = std::ceil(gr.y0 / ys) * ys; v <= gr.y1 + ys * 1e-9; v += ys) tickW = qMax(tickW, tm.horizontalAdvance(label(v, ys)));
    const double left = am.height() + 8 + tickW + 8;
    const QRectF area(left, top, w - left - 12, h - top - (tm.height() + am.height() + 14));
    auto X = [&](double v) { return area.left() + (v - gr.x0) / (gr.x1 - gr.x0) * area.width(); };
    auto Y = [&](double v) { return area.bottom() - (v - gr.y0) / (gr.y1 - gr.y0) * area.height(); };

    // Grid, ticks, and the axes through zero.
    QColor grid = th.line();
    p.setFont(tf);
    for (double v = std::ceil(gr.x0 / xs) * xs; v <= gr.x1 + xs * 1e-9; v += xs) {
        p.setPen(QPen(grid, 1));
        p.drawLine(QPointF(X(v), area.top()), QPointF(X(v), area.bottom()));
        p.setPen(tick);
        p.drawText(QRectF(X(v) - 60, area.bottom() + 4, 120, tm.height()), Qt::AlignHCenter | Qt::AlignTop, label(v, xs));
    }
    for (double v = std::ceil(gr.y0 / ys) * ys; v <= gr.y1 + ys * 1e-9; v += ys) {
        p.setPen(QPen(grid, 1));
        p.drawLine(QPointF(area.left(), Y(v)), QPointF(area.right(), Y(v)));
        p.setPen(tick);
        p.drawText(QRectF(area.left() - 8 - tickW, Y(v) - tm.height() / 2.0, tickW, tm.height()), Qt::AlignRight | Qt::AlignVCenter,
                   label(v, ys));
    }
    p.setPen(QPen(th.faint(), 1.2));
    if (gr.y0 < 0 && gr.y1 > 0) p.drawLine(QPointF(area.left(), Y(0)), QPointF(area.right(), Y(0)));
    if (gr.x0 < 0 && gr.x1 > 0) p.drawLine(QPointF(X(0), area.top()), QPointF(X(0), area.bottom()));
    p.setPen(QPen(th.faint(), 1));
    p.drawLine(area.bottomLeft(), area.bottomRight());
    p.drawLine(area.bottomLeft(), area.topLeft());
    if (eva) {  // corner brackets on the plotting field
        p.setPen(QPen(tick, 1.4));
        const double k = 7;
        for (const QPointF& c : {area.topLeft(), area.topRight(), area.bottomLeft(), area.bottomRight()}) {
            const double sx = c.x() < area.center().x() ? 1 : -1, sy = c.y() < area.center().y() ? 1 : -1;
            p.drawLine(c, c + QPointF(k * sx, 0));
            p.drawLine(c, c + QPointF(0, k * sy));
        }
    }

    p.save();
    p.setClipRect(area.adjusted(-1, -1, 1, 1));
    // Asymptotes: where a curve runs off to infinity.
    for (double x : gr.poles) {
        QPen pen(eva ? alarm : th.faint(), 1.2, Qt::DashLine);
        p.setPen(pen);
        p.drawLine(QPointF(X(x), area.top()), QPointF(X(x), area.bottom()));
    }
    // The curves, broken where they are not defined or jump.
    const QList<QColor> colours = eva ? QList<QColor>{th.accent(), th.warm(), th.structure(), th.text()}
                                      : QList<QColor>{th.accent(), th.structure(), th.warm(), th.text()};
    const double lim = (gr.y1 - gr.y0) * 50;  // keep far points finite for the painter
    for (int ci = 0; ci < gr.curves.size(); ++ci) {
        const GraphCurve& c = gr.curves[ci];
        QPainterPath path;
        bool open = false;
        for (int i = 0; i < gr.xs.size(); ++i) {
            const double y = c.y[i];
            if (!std::isfinite(y)) {
                open = false;
                continue;
            }
            const QPointF pt(X(gr.xs[i]), Y(qBound(gr.y0 - lim, y, gr.y1 + lim)));
            if (open) path.lineTo(pt);
            else path.moveTo(pt);
            open = !c.breakAfter[i];
        }
        p.setPen(QPen(colours[ci % colours.size()], 1.8, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p.setBrush(Qt::NoBrush);
        p.drawPath(path);
    }
    // A lab table's measurements.
    p.setPen(QPen(th.palette().panel, 1));
    p.setBrush(th.text());
    for (const QPointF& pt : gr.points) p.drawRect(QRectF(X(pt.x()) - 3, Y(pt.y()) - 3, 6, 6));

    // The marks: roots as target locks, turning points as diamonds, meetings as rings.
    p.setFont(tf);
    int target = 0, drawn = 0;
    for (const GraphMark& m : gr.marks) {
        if (m.y < gr.y0 || m.y > gr.y1 || ++drawn > 16) continue;
        const QPointF c(X(m.x), Y(m.y));
        const QColor col = m.kind == GraphMark::Root ? (eva ? th.warm() : th.accent()) : th.text();
        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(col, 1.4));
        QString tag;
        if (m.kind == GraphMark::Root) {
            ++target;
            if (eva) {
                const double r = 6, k = 3;
                for (int sx : {-1, 1})
                    for (int sy : {-1, 1}) {
                        const QPointF corner = c + QPointF(sx * r, sy * r);
                        p.drawLine(corner, corner - QPointF(sx * k, 0));
                        p.drawLine(corner, corner - QPointF(0, sy * k));
                    }
                p.drawPoint(c);
                tag = QString("%1").arg(target, 2, 10, QChar('0'));
            } else {
                p.setBrush(col);
                p.drawEllipse(c, 2.6, 2.6);
            }
        } else if (m.kind == GraphMark::Cross) {
            p.drawEllipse(c, 4.5, 4.5);
        } else {
            p.drawPolygon(QPolygonF{c + QPointF(0, -4.5), c + QPointF(4.5, 0), c + QPointF(0, 4.5), c + QPointF(-4.5, 0)});
        }
        if (!tag.isEmpty()) {
            p.setPen(col);
            p.drawText(QPointF(c.x() + 8, c.y() - 7), tag);
        }
    }
    p.restore();
    // Asymptote labels, above the field's top edge inside it.
    if (eva)
        for (double x : gr.poles) {
            p.save();
            p.setFont(lf);
            p.setPen(alarm);
            p.translate(X(x) + 3, area.top() + 4);
            p.rotate(90);
            p.drawText(QPointF(0, 0), "Pattern Blue");
            p.restore();
        }

    // Legend for several curves, top right.
    if (gr.curves.size() > 1) {
        p.setFont(af);
        double y = area.top() + 6;
        int widest = 0;
        for (const GraphCurve& c : gr.curves) widest = qMax(widest, am.horizontalAdvance(c.label));
        widest = qMin(widest, static_cast<int>(area.width() / 2));
        QColor bg = th.palette().bg;
        bg.setAlpha(190);
        p.setPen(Qt::NoPen);
        p.setBrush(bg);
        p.drawRect(QRectF(area.right() - widest - 34, y - 3, widest + 30, gr.curves.size() * (am.height() + 2) + 6));
        for (int ci = 0; ci < gr.curves.size(); ++ci) {
            p.setPen(QPen(colours[ci % colours.size()], 2));
            p.drawLine(QPointF(area.right() - widest - 28, y + am.height() / 2.0), QPointF(area.right() - widest - 14, y + am.height() / 2.0));
            p.setPen(th.text());
            p.drawText(QRectF(area.right() - widest - 8, y, widest, am.height()), Qt::AlignLeft | Qt::AlignVCenter,
                       am.elidedText(gr.curves[ci].label, Qt::ElideRight, widest));
            y += am.height() + 2;
        }
    }

    // Axis labels: "T / K", "k / s⁻¹".
    auto axis = [](const QString& name, const QString& unit) { return unit.isEmpty() ? name : name + " / " + unit; };
    p.setFont(af);
    p.setPen(th.text());
    p.drawText(QRectF(area.left(), h - am.height() - 4, area.width(), am.height()), Qt::AlignHCenter, axis(gr.xName, gr.xUnit));
    p.save();
    p.translate(4, area.center().y());
    p.rotate(-90);
    p.drawText(QRectF(-area.height() / 2, 0, area.height(), am.height()), Qt::AlignHCenter | Qt::AlignTop, axis(gr.yName, gr.yUnit));
    p.restore();
    return img;
}

}  // namespace graphite
