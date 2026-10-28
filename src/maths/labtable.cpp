#include "maths/labtable.hpp"

#include <QRegularExpression>
#include <algorithm>
#include <cmath>

namespace graphite {

namespace {

QStringList splitRow(const QString& line) {
    QString t = line.trimmed();
    if (t.startsWith('|')) t.remove(0, 1);
    if (t.endsWith('|') && !t.endsWith("\\|")) t.chop(1);
    QStringList cells;
    QString cur;
    for (int i = 0; i < t.size(); ++i) {
        if (t[i] == '\\' && i + 1 < t.size() && t[i + 1] == '|') {
            cur += '|';
            ++i;
        } else if (t[i] == '|') {
            cells << cur.trimmed();
            cur.clear();
        } else {
            cur += t[i];
        }
    }
    cells << cur.trimmed();
    return cells;
}

bool isVariable(const QString& s) {
    if (s.isEmpty() || !(s[0].isLetter() || s[0] == '_')) return false;
    for (QChar c : s)
        if (!(c.isLetterOrNumber() || c == '_')) return false;
    return true;
}

// "m/V to g/mL" -> "m/V", "g/mL"
void splitTarget(const QString& formula, QString* expr, QString* target) {
    static const QRegularExpression to(R"(\s+(?:to|in)\s+(.+)$|\s*(?:->|→)\s*(.+)$)");
    const QRegularExpressionMatch m = to.match(formula);
    *expr = m.hasMatch() ? formula.left(m.capturedStart()).trimmed() : formula.trimmed();
    *target = m.hasMatch() ? (m.capturedLength(1) ? m.captured(1) : m.captured(2)).trimmed() : QString();
}

// A cell as a quantity: "2.50" in the column's unit, or "2.50 g" with its own.
std::optional<CalcQuantity> cellValue(const QString& cell, const QString& unit) {
    static const QRegularExpression numeric(R"(^[-−+]?(\d|\.\d))");
    if (!numeric.match(cell).hasMatch()) return std::nullopt;
    CalcQuantity q;
    if (!evaluateQuantity(cell, &q)) return std::nullopt;
    const bool plainNumber = std::all_of(q.dims.begin(), q.dims.end(), [](int d) { return d == 0; });
    if (plainNumber && !unit.isEmpty()) {
        CalcQuantity u;
        if (!evaluateQuantity("(" + cell + ") " + unit, &u)) return std::nullopt;
        u.unit = unit;
        return u;
    }
    return q;
}

// Whether `expr` mentions the variable `name` as a whole word.
bool usesName(const QString& expr, const QString& name) {
    for (qsizetype i = expr.indexOf(name); i >= 0; i = expr.indexOf(name, i + 1)) {
        const qsizetype end = i + name.size();
        const bool before = i > 0 && (expr[i - 1].isLetterOrNumber() || expr[i - 1] == '_');
        const bool after = end < expr.size() && (expr[end].isLetterOrNumber() || expr[end] == '_');
        if (!before && !after) return true;
    }
    return false;
}

CalcVars rowVars(const LabTable& t, int row) {
    CalcVars v;
    for (int c = 0; c < t.columns.size(); ++c)
        if (!t.columns[c].name.isEmpty() && t.values[row][c]) v.insert(t.columns[c].name, *t.values[row][c]);
    return v;
}

}  // namespace

QString LabTable::label(int column) const {
    const LabColumn& c = columns[column];
    if (!c.computed) return c.header;
    QString title = c.name;
    if (title.isEmpty()) {
        QString expr, target;
        splitTarget(c.formula, &expr, &target);
        title = expr;
    }
    return c.unit.isEmpty() ? title : title + " (" + c.unit + ")";
}

bool parseLabTable(const QStringList& lines, LabTable* t) {
    *t = LabTable();
    static const QRegularExpression rule(R"(^:?-{2,}:?$)");
    QList<QStringList> rows;
    for (const QString& line : lines) {
        if (!line.trimmed().startsWith('|')) return false;
        const QStringList cells = splitRow(line);
        if (std::all_of(cells.begin(), cells.end(), [](const QString& c) { return rule.match(c).hasMatch(); })) continue;
        rows << cells;
    }
    if (rows.isEmpty()) return false;

    static const QRegularExpression computed(R"(^(?:([^=]*?)\s*)?=\s*(.+)$)");
    static const QRegularExpression slash(R"(^(.+?)\s+/\s+(.+)$)");
    static const QRegularExpression bracket(R"(^(.+?)\s*[\(\[](.+)[\)\]]$)");
    for (const QString& h : rows[0]) {
        LabColumn c;
        c.header = h;
        QRegularExpressionMatch m;
        if ((m = computed.match(h)).hasMatch()) {
            c.computed = true;
            c.formula = m.captured(2).trimmed();
            if (isVariable(m.captured(1).trimmed())) c.name = m.captured(1).trimmed();
        } else if ((m = slash.match(h)).hasMatch() || (m = bracket.match(h)).hasMatch()) {
            if (isVariable(m.captured(1).trimmed())) c.name = m.captured(1).trimmed();
            c.unit = m.captured(2).trimmed();
        } else if (isVariable(h)) {
            c.name = h;
        }
        t->columns << c;
    }
    const int n = static_cast<int>(t->columns.size());
    for (int r = 1; r < rows.size(); ++r) {
        QStringList cells = rows[r].mid(0, n);
        while (cells.size() < n) cells << QString();
        t->cells << cells;
        QList<std::optional<CalcQuantity>> vals;
        for (int c = 0; c < n; ++c) vals << (t->columns[c].computed ? std::nullopt : cellValue(cells[c], t->columns[c].unit));
        t->values << vals;
    }
    // Left to right, so a computed column can use the ones before it.
    for (int c = 0; c < n; ++c) {
        LabColumn& col = t->columns[c];
        if (!col.computed) {
            bool any = false, all = true;
            for (int r = 0; r < t->cells.size(); ++r) {
                if (t->cells[r][c].isEmpty()) continue;
                any = true;
                all = all && t->values[r][c].has_value();
            }
            col.numeric = any && all;
            continue;
        }
        QString expr, target;
        splitTarget(col.formula, &expr, &target);
        col.unit = target;
        bool any = false;
        for (int r = 0; r < t->cells.size(); ++r) {
            // A row missing a value the formula uses stays blank (and "V" must not turn into volts).
            bool gap = false;
            for (int k = 0; k < n && !gap; ++k)
                gap = k != c && !t->columns[k].name.isEmpty() && !t->values[r][k] && usesName(expr, t->columns[k].name);
            if (gap) continue;
            const CalcVars vars = rowVars(*t, r);
            CalcQuantity q;
            QString err;
            if (!evaluateQuantity(expr, &q, &err, &vars)) {
                if (col.error.isEmpty()) col.error = err;
                continue;
            }
            if (col.unit.isEmpty()) {  // the first row decides how the column is shown
                double scale = 1;
                col.unit = quantityUnit(q, &scale);
            }
            double shown = 0;
            if (!quantityIn(q, col.unit, &shown)) {
                if (col.error.isEmpty()) col.error = "that is not in " + col.unit;
                continue;
            }
            q.unit = col.unit;
            t->values[r][c] = q;
            t->cells[r][c] = calcNumber(shown);
            any = true;
        }
        col.numeric = any;
    }
    return true;
}

QString plotSpec(const QString& line) {
    // "=plot{A vs c}" (the closing brace may still be missing while typing); "=plot{}" alone.
    static const QRegularExpression braces(R"(^=\s*plot\s*\{([^{}]*)\}?$)");
    if (const QRegularExpressionMatch b = braces.match(line.trimmed()); b.hasMatch()) {
        const QString inner = b.captured(1).trimmed();
        return inner.isNull() ? QString("") : inner;
    }
    // The older form, "= plot A vs c", still reads.
    const QString e = calcExpression(line);
    if (e.isNull()) return QString();
    static const QRegularExpression plot(R"(^plot(?:\s+(.*))?$)");
    const QRegularExpressionMatch m = plot.match(e);
    if (!m.hasMatch()) return QString();
    const QString rest = m.captured(1).trimmed();
    return rest.isNull() ? QString("") : rest;  // "= plot" alone: empty, not null
}

LabPlot labPlot(const LabTable& t, const QString& spec) {
    LabPlot p;
    const int n = static_cast<int>(t.columns.size());
    QString ySpec, xSpec;
    if (spec.isEmpty()) {
        QList<int> numeric;
        for (int c = 0; c < n; ++c)
            if (t.columns[c].numeric) numeric << c;
        if (numeric.size() < 2) {
            p.error = "needs two columns of numbers";
            return p;
        }
        xSpec = QString("#%1").arg(numeric[0]);
        ySpec = QString("#%1").arg(numeric[1]);
    } else {
        static const QRegularExpression vs(R"(\s+(?:vs\.?|against)\s+)", QRegularExpression::CaseInsensitiveOption);
        const QStringList parts = spec.split(vs);
        if (parts.size() != 2) {
            p.error = "write it as: =plot{y vs x}";
            return p;
        }
        ySpec = parts[0].trimmed();
        xSpec = parts[1].trimmed();
    }

    // One axis: a column (by name or header) in its unit, or an expression worked out per row.
    struct Axis {
        QList<std::optional<double>> v;
        QString label, unit, error;
        std::array<int, 6> dims{};
    };
    auto axis = [&](const QString& s) {
        Axis a;
        int col = -1;
        if (s.startsWith('#')) col = s.mid(1).toInt();
        for (int c = 0; c < n && col < 0; ++c)
            if (t.columns[c].name == s || t.columns[c].header == s) col = c;
        if (col >= 0) {
            a.label = t.label(col);
            a.unit = t.columns[col].unit;
            for (int r = 0; r < t.cells.size(); ++r) {
                double x = 0;
                const auto& q = t.values[r][col];
                if (q && quantityIn(*q, a.unit, &x)) {
                    a.v << x;
                    a.dims = q->dims;
                } else {
                    a.v << std::nullopt;
                }
            }
            if (!t.columns[col].computed) a.label = t.columns[col].name.isEmpty() ? t.columns[col].header : t.columns[col].name;
            return a;
        }
        a.label = s;
        bool first = true;
        for (int r = 0; r < t.cells.size(); ++r) {
            const CalcVars vars = rowVars(t, r);
            CalcQuantity q;
            QString err;
            double x = 0;
            if (!evaluateQuantity(s, &q, &err, &vars)) {
                if (a.error.isEmpty()) a.error = err;
                a.v << std::nullopt;
                continue;
            }
            if (first) {
                double scale = 1;
                a.unit = quantityUnit(q, &scale);
                a.dims = q.dims;
                first = false;
            }
            a.v << (quantityIn(q, a.unit, &x) ? std::optional<double>(x) : std::nullopt);
        }
        return a;
    };
    const Axis x = axis(xSpec), y = axis(ySpec);
    p.xLabel = x.label;
    p.yLabel = y.label;
    p.xUnit = x.unit;
    p.yUnit = y.unit;
    for (int r = 0; r < t.cells.size(); ++r)
        if (r < x.v.size() && r < y.v.size() && x.v[r] && y.v[r]) p.points << QPointF(*x.v[r], *y.v[r]);
    if (p.points.isEmpty()) {
        p.error = !y.error.isEmpty() ? y.error : !x.error.isEmpty() ? x.error : QString("no rows with both values");
        return p;
    }
    p.ok = true;

    const int k = static_cast<int>(p.points.size());
    double mx = 0, my = 0;
    for (const QPointF& pt : p.points) {
        mx += pt.x();
        my += pt.y();
    }
    mx /= k;
    my /= k;
    double sxx = 0, sxy = 0, syy = 0;
    for (const QPointF& pt : p.points) {
        sxx += (pt.x() - mx) * (pt.x() - mx);
        sxy += (pt.x() - mx) * (pt.y() - my);
        syy += (pt.y() - my) * (pt.y() - my);
    }
    if (k < 2 || sxx <= 0) return p;
    p.fitted = true;
    p.slope = sxy / sxx;
    p.intercept = my - p.slope * mx;
    double res = 0;
    for (const QPointF& pt : p.points) res += std::pow(pt.y() - (p.slope * pt.x() + p.intercept), 2);
    p.r2 = syy > 0 ? 1 - res / syy : 1;

    // The slope's unit: y per x. "K⁻¹" per -> "K".
    auto wrap = [](const QString& u) { return u.contains(' ') || u.contains('/') || u.contains(QChar(0x00B7)) ? "(" + u + ")" : u; };
    QString su;
    const QString inv = QString::fromUtf8("⁻¹");
    if (x.unit.isEmpty()) su = y.unit;
    else if (y.unit.isEmpty()) su = x.unit.endsWith(inv) && !x.unit.chopped(2).contains(QChar(0x00B7)) ? x.unit.chopped(2) : wrap(x.unit) + inv;
    else su = y.unit + "/" + wrap(x.unit);
    const QString dot = QString::fromUtf8("  ·  ");
    p.summary = "slope " + calcNumber(p.slope) + (su.isEmpty() ? QString() : " " + su) + dot + "intercept " + calcNumber(p.intercept) +
                (y.unit.isEmpty() ? QString() : " " + y.unit) + dot + QString::fromUtf8("R² ") + QString::number(p.r2, 'f', 4);
    // ln(k) against 1/T: the Arrhenius activation energy, Ea = −R × slope.
    const std::array<int, 6> perKelvin{0, 0, 0, 0, -1, 0};
    if (ySpec.startsWith("ln(") && x.dims == perKelvin && y.unit.isEmpty()) {
        double scale = 1;
        CalcQuantity xq{1, perKelvin, QString()};
        quantityIn(xq, x.unit, &scale);  // the x unit in K⁻¹
        const CalcResult ea = evaluate(QString("-R * %1 K to kJ/mol").arg(p.slope * scale, 0, 'g', 12));
        if (ea.ok) p.summary += dot + QString::fromUtf8("Eₐ = −R·slope = ") + ea.value;
    }
    return p;
}

}  // namespace graphite
