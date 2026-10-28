#include "maths/calc.hpp"

#include <QHash>
#include <QList>
#include <QRegularExpression>
#include <array>
#include <cmath>

#include "chem/molar.hpp"

namespace graphite {

namespace {

// SI base dimensions: m, kg, s, mol, K, A.
constexpr int kDims = 6;
using Dims = std::array<int, kDims>;

struct Q {
    double v = 1;
    Dims d{};
};

Q mul(const Q& a, const Q& b) {
    Q r{a.v * b.v, {}};
    for (int i = 0; i < kDims; ++i) r.d[i] = a.d[i] + b.d[i];
    return r;
}
Q div(const Q& a, const Q& b) {
    Q r{a.v / b.v, {}};
    for (int i = 0; i < kDims; ++i) r.d[i] = a.d[i] - b.d[i];
    return r;
}
Q pw(const Q& a, int n) {
    Q r{std::pow(a.v, n), {}};
    for (int i = 0; i < kDims; ++i) r.d[i] = a.d[i] * n;
    return r;
}
bool plain(const Q& q) {
    for (int x : q.d)
        if (x) return false;
    return true;
}

Q unitQ(double scale, int m, int kg, int s, int mol = 0, int k = 0, int a = 0) { return Q{scale, {m, kg, s, mol, k, a}}; }

// Units by symbol, and which of them take an SI prefix (kJ, mmol, µL, nM...).
const QHash<QString, Q>& units() {
    static const QHash<QString, Q> u = [] {
        QHash<QString, Q> t;
        const Q J = unitQ(1, 2, 1, -2), Pa = unitQ(1, -1, 1, -2), W = unitQ(1, 2, 1, -3);
        t["m"] = unitQ(1, 1, 0, 0);
        t["g"] = unitQ(1e-3, 0, 1, 0);
        t["s"] = unitQ(1, 0, 0, 1);
        t["mol"] = unitQ(1, 0, 0, 0, 1);
        t["K"] = unitQ(1, 0, 0, 0, 0, 1);
        t["A"] = unitQ(1, 0, 0, 0, 0, 0, 1);
        t["L"] = unitQ(1e-3, 3, 0, 0);
        t["M"] = unitQ(1e3, -3, 0, 0, 1);  // mol/L
        t["min"] = unitQ(60, 0, 0, 1);
        t["h"] = t["hr"] = unitQ(3600, 0, 0, 1);
        t["day"] = t["d"] = unitQ(86400, 0, 0, 1);
        t["J"] = J;
        t["cal"] = Q{4.184, J.d};
        t["eV"] = Q{1.602176634e-19, J.d};
        t["N"] = unitQ(1, 1, 1, -2);
        t["Pa"] = Pa;
        t["atm"] = Q{101325, Pa.d};
        t["bar"] = Q{1e5, Pa.d};
        t["mmHg"] = t["Torr"] = t["torr"] = Q{133.322368, Pa.d};
        t["W"] = W;
        t["C"] = unitQ(1, 0, 0, 1, 0, 0, 1);
        t["V"] = unitQ(1, 2, 1, -3, 0, 0, -1);
        t["Hz"] = unitQ(1, 0, 0, -1);
        t["ohm"] = t["Ω"] = unitQ(1, 2, 1, -3, 0, 0, -2);
        t["Da"] = t["amu"] = unitQ(1.66053906660e-27, 0, 1, 0);
        t["Å"] = t["angstrom"] = unitQ(1e-10, 1, 0, 0);
        t["°C"] = t["degC"] = t["℃"] = unitQ(1, 0, 0, 0, 0, 1);  // as a difference; "25 °C to K" is special
        t["%"] = unitQ(0.01, 0, 0, 0);
        t["ppm"] = unitQ(1e-6, 0, 0, 0);
        return t;
    }();
    return u;
}
bool prefixable(const QString& s) {
    static const QStringList p = {"m", "g", "s", "L", "mol", "M", "J", "W", "Pa", "N", "V", "C", "A", "Hz", "eV", "cal", "K", "Da"};
    return p.contains(s);
}
double prefixScale(QChar c) {
    switch (c.unicode()) {
        case 'T': return 1e12;
        case 'G': return 1e9;
        case 'M': return 1e6;
        case 'k': return 1e3;
        case 'h': return 1e2;
        case 'd': return 1e-1;
        case 'c': return 1e-2;
        case 'm': return 1e-3;
        case 'u': case 0x00B5: case 0x03BC: return 1e-6;  // u, µ (micro sign), μ (Greek mu)
        case 'n': return 1e-9;
        case 'p': return 1e-12;
        case 'f': return 1e-15;
        default: return 0;
    }
}

// Physical constants (CODATA 2018 exact / recommended values).
bool constant(const QString& name, Q* q) {
    if (name == "R") *q = unitQ(8.314462618, 2, 1, -2, -1, -1);
    else if (name == "NA" || name == "N_A") *q = unitQ(6.02214076e23, 0, 0, 0, -1);
    else if (name == "F") *q = unitQ(96485.33212, 0, 0, 1, -1, 0, 1);
    else if (name == "kB" || name == "k_B") *q = unitQ(1.380649e-23, 2, 1, -2, 0, -1);
    else if (name == "c") *q = unitQ(299792458, 1, 0, -1);
    else if (name == "e") *q = unitQ(1.602176634e-19, 0, 0, 1, 0, 0, 1);
    else if (name == "hP") *q = unitQ(6.62607015e-34, 2, 1, -1);
    else if (name == "pi" || name == "π") *q = Q{M_PI, {}};
    else return false;
    return true;
}

bool lookupUnit(const QString& name, Q* q) {
    const auto& u = units();
    if (auto it = u.constFind(name); it != u.constEnd()) {
        *q = *it;
        return true;
    }
    if (name.size() > 1) {
        const double ps = prefixScale(name[0]);
        const QString base = name.mid(name.startsWith("da") ? 2 : 1);
        if (ps > 0 && prefixable(base) && u.contains(base)) {
            *q = u[base];
            q->v *= ps;
            return true;
        }
    }
    return false;
}

int superscriptDigit(QChar c) {
    static const QString sup = QString::fromUtf8("⁰¹²³⁴⁵⁶⁷⁸⁹");
    return static_cast<int>(sup.indexOf(c));
}

class Parser {
public:
    explicit Parser(const QString& s, const CalcVars* vars = nullptr) : s_(s), vars_(vars) {}

    bool parse(Q* out, QString* err) {
        if (!expr(out)) {
            *err = err_.isEmpty() ? QString("can't read this") : err_;
            return false;
        }
        skip();
        if (pos_ < s_.size()) {
            *err = QString("can't read '%1'").arg(s_.mid(pos_, 8));
            return false;
        }
        return true;
    }
    // The units written in the expression, in order (for choosing how to show the result).
    QList<QPair<QString, Q>> used;

private:
    void skip() {
        while (pos_ < s_.size() && s_[pos_].isSpace()) ++pos_;
    }
    bool fail(const QString& e) {
        if (err_.isEmpty()) err_ = e;
        return false;
    }
    bool atOp(const QString& ops) {
        skip();
        return pos_ < s_.size() && ops.contains(s_[pos_]);
    }

    bool expr(Q* q) {
        if (!term(q)) return false;
        while (atOp("+-−")) {
            const bool plus = s_[pos_++] == '+';
            Q r;
            if (!term(&r)) return false;
            if (q->d != r.d) return fail("adding different kinds of quantity");
            q->v = plus ? q->v + r.v : q->v - r.v;
        }
        return true;
    }
    // term = juxt (('*' | '/') juxt)*  — written-together factors bind tighter: J/mol K = J/(mol K)
    bool term(Q* q) {
        if (!juxt(q)) return false;
        while (atOp("*/×·⋅")) {
            const QChar op = s_[pos_++];
            Q r;
            if (!juxt(&r)) return false;
            if (op == '/') {
                if (r.v == 0) return fail("division by zero");
                *q = div(*q, r);
            } else {
                *q = mul(*q, r);
            }
        }
        return true;
    }
    bool startsFactor() {
        skip();
        if (pos_ >= s_.size()) return false;
        const QChar c = s_[pos_];
        if (c == '(' || c.isDigit() || c == '.') return true;
        if (!(c.isLetter() || c == QChar(0x00B0) || c == QChar(0x2103) || c == '%')) return false;
        // "to" / "in" end the expression (the unit to convert to follows).
        static const QRegularExpression kw(R"(^(to|in)\b)");
        return !kw.match(s_.mid(pos_, 3)).hasMatch();
    }
    bool juxt(Q* q) {
        skip();
        bool neg = false;
        while (pos_ < s_.size() && (s_[pos_] == '-' || s_[pos_] == QChar(0x2212) || s_[pos_] == '+')) {
            neg = neg != (s_[pos_] != '+');
            ++pos_;
            skip();
        }
        if (!power(q)) return false;
        while (startsFactor()) {
            Q r;
            if (!power(&r)) return false;
            *q = mul(*q, r);
        }
        if (neg) q->v = -q->v;
        return true;
    }
    bool exponent(int* n) {
        // ^2, ^-1, ², ⁻¹, or digits right after a unit (cm3, s-1 is not: too ambiguous)
        skip();
        if (pos_ < s_.size() && s_[pos_] == '^') {
            ++pos_;
            skip();
            int sign = 1;
            if (pos_ < s_.size() && (s_[pos_] == '-' || s_[pos_] == QChar(0x2212))) {
                sign = -1;
                ++pos_;
            }
            const int start = pos_;
            while (pos_ < s_.size() && s_[pos_].isDigit()) ++pos_;
            if (pos_ == start) return fail("a whole number after ^");
            *n = sign * s_.mid(start, pos_ - start).toInt();
            return true;
        }
        int sign = 1, val = 0;
        bool any = false;
        if (pos_ < s_.size() && s_[pos_] == QChar(0x207B)) {  // ⁻
            sign = -1;
            ++pos_;
        }
        while (pos_ < s_.size() && superscriptDigit(s_[pos_]) >= 0) {
            val = val * 10 + superscriptDigit(s_[pos_++]);
            any = true;
        }
        *n = any ? sign * val : 1;
        return true;
    }
    bool power(Q* q) {
        if (!primary(q)) return false;
        // Any other power of a plain number: x^0.5, 2^x, x^(1/3), 10^-pH. A unit only takes
        // whole numbers (below).
        static const QRegularExpression whole(R"(^\^\s*[-\x{2212}]?\d+(?![\d.eE]))");
        skip();
        if (pos_ < s_.size() && s_[pos_] == '^' && !whole.match(s_.mid(pos_, 24)).hasMatch()) {
            ++pos_;
            skip();
            bool neg = false;
            if (pos_ < s_.size() && (s_[pos_] == '-' || s_[pos_] == QChar(0x2212))) {
                neg = true;
                ++pos_;
            }
            Q e;
            if (!power(&e)) return false;
            if (neg) e.v = -e.v;
            if (!plain(e)) return fail("a power must be a plain number");
            if (!plain(*q)) {
                if (e.v != std::round(e.v)) return fail("a unit to a power that is not a whole number");
                *q = pw(*q, static_cast<int>(e.v));
                return true;
            }
            q->v = std::pow(q->v, e.v);
            if (!std::isfinite(q->v)) return fail("the power is not defined there");
            return true;
        }
        int n = 1;
        if (!exponent(&n)) return false;
        if (n != 1) *q = pw(*q, n);
        return true;
    }
    bool number(Q* q) {
        static const QRegularExpression num(R"(^(\d+\.?\d*|\.\d+)([eE][-+]?\d+)?)");
        const QRegularExpressionMatch m = num.match(s_.mid(pos_));
        if (!m.hasMatch()) return false;
        pos_ += static_cast<int>(m.capturedLength());
        q->v = m.captured().toDouble();
        q->d = {};
        // "6.02×10^23" / "6.02×10²³": a power of ten belongs to the number
        static const QRegularExpression ten(R"(^\s*[x×]\s*10(\^[-+−]?\d+|[⁻]?[⁰¹²³⁴-⁹]+))");
        if (const QRegularExpressionMatch t = ten.match(s_.mid(pos_)); t.hasMatch()) {
            QString e = t.captured(1);
            int ex = 0;
            if (e.startsWith('^')) ex = e.mid(1).replace(QChar(0x2212), '-').toInt();
            else {
                int sign = 1;
                for (QChar c : e) {
                    if (c == QChar(0x207B)) sign = -1;
                    else ex = ex * 10 + superscriptDigit(c);
                }
                ex *= sign;
            }
            q->v *= std::pow(10.0, ex);
            pos_ += static_cast<int>(t.capturedLength());
        }
        return true;
    }
    bool primary(Q* q) {
        skip();
        if (pos_ >= s_.size()) return fail("something is missing at the end");
        const QChar c = s_[pos_];
        if (c == '(') {
            ++pos_;
            if (!expr(q)) return false;
            skip();
            if (pos_ >= s_.size() || s_[pos_] != ')') return fail("a ')' is missing");
            ++pos_;
            return true;
        }
        if (c.isDigit() || c == '.') return number(q) || fail("can't read the number");
        if (c == '%') {
            ++pos_;
            *q = units()["%"];
            return true;
        }
        // A name: unit, constant or function.
        const int start = pos_;
        while (pos_ < s_.size() && (s_[pos_].isLetter() || s_[pos_] == '_' || s_[pos_] == QChar(0x00B0) || s_[pos_] == QChar(0x2103)))
            ++pos_;
        if (pos_ == start) return fail(QString("can't read '%1'").arg(c));
        QString name = s_.mid(start, pos_ - start);
        if (vars_) {  // a variable: "m", "c_1", "T2" (digits belong to its name when it has them)
            int k = pos_;
            while (k < s_.size() && s_[k].isDigit()) ++k;
            const QString longer = s_.mid(start, k - start);
            const QString found = vars_->contains(longer) ? longer : vars_->contains(name) ? name : QString();
            const int end = start + static_cast<int>(found.size());
            if (!found.isEmpty() && (end >= s_.size() || s_[end] != '(')) {  // "M(" stays the molar mass
                pos_ = end;
                const CalcQuantity& v = vars_->value(found);
                q->v = v.si;
                for (int i = 0; i < kDims; ++i) q->d[i] = v.dims[static_cast<size_t>(i)];
                Q u;
                QString e;
                if (!v.unit.isEmpty() && Parser(v.unit).parse(&u, &e) && u.d == q->d) used.append({v.unit, u});
                return true;
            }
        }
        if (pos_ < s_.size() && s_[pos_] == '(') return function(name, q);
        // Digits glued to a unit are its power: cm3, m2.
        Q u;
        if (lookupUnit(name, &u)) {
            int glued = 0, k = pos_;
            while (k < s_.size() && s_[k].isDigit()) glued = glued * 10 + s_[k++].digitValue();
            if (k > pos_ && (k >= s_.size() || !s_[k].isLetter())) {
                pos_ = k;
                u = pw(u, glued);
            }
            used.append({name, u});
            *q = u;
            return true;
        }
        if (constant(name, q)) return true;
        return fail(QString("unknown unit '%1'").arg(name));
    }
    bool function(const QString& name, Q* q) {
        ++pos_;  // '('
        if (name == "M") {  // molar mass of a formula: M(CuSO4.5H2O)
            int depth = 1, k = pos_;
            while (k < s_.size() && depth > 0) {
                if (s_[k] == '(') ++depth;
                else if (s_[k] == ')') --depth;
                ++k;
            }
            if (depth) return fail("a ')' is missing");
            const QString formula = s_.mid(pos_, k - 1 - pos_).trimmed();
            pos_ = k;
            double m = 0;
            if (!formulaMass(formula, &m)) return fail(QString("'%1' is not a formula").arg(formula));
            *q = unitQ(m * 1e-3, 0, 1, 0, -1);  // kg/mol
            used.append({"g/mol", unitQ(1e-3, 0, 1, 0, -1)});
            return true;
        }
        // ln(k) of a column: its number in the column's unit (k in s⁻¹ -> ln of that number).
        double written = 0;
        bool byColumn = false;
        if (vars_) {
            const int close = static_cast<int>(s_.indexOf(')', pos_));
            const QString arg = close < 0 ? QString() : s_.mid(pos_, close - pos_).trimmed();
            if (vars_->contains(arg)) {
                const CalcQuantity& v = vars_->value(arg);
                Q u{1, {}};
                QString e;
                if (v.unit.isEmpty() || Parser(v.unit).parse(&u, &e)) {
                    written = v.si / u.v;
                    byColumn = true;
                }
            }
        }
        Q a;
        if (!expr(&a)) return false;
        skip();
        if (pos_ >= s_.size() || s_[pos_] != ')') return fail("a ')' is missing");
        ++pos_;
        // A concentration counts in mol/L inside a logarithm (pH = -log[H+]).
        const Q molar = units()["M"];
        double x = a.v;
        if (byColumn && name != "sqrt") x = written, a = Q{written, {}};
        else if (a.d == molar.d) x = a.v / molar.v;
        else if (!plain(a) && name != "sqrt") return fail(QString("%1 needs a plain number").arg(name));
        if (name == "ln") *q = Q{std::log(x), {}};
        else if (name == "log" || name == "lg") *q = Q{std::log10(x), {}};
        else if (name == "exp") *q = Q{std::exp(x), {}};
        else if (name == "sqrt") {
            for (int d : a.d)
                if (d % 2) return fail("sqrt of an odd power of a unit");
            Q r{std::sqrt(a.v), {}};
            for (int i = 0; i < kDims; ++i) r.d[i] = a.d[i] / 2;
            *q = r;
        } else if (name == "sin" || name == "cos" || name == "tan") {
            *q = Q{name == "sin" ? std::sin(x) : name == "cos" ? std::cos(x) : std::tan(x), {}};
        } else {
            return fail(QString("unknown function '%1'").arg(name));
        }
        if (!std::isfinite(q->v)) return fail(QString("%1 is not defined there").arg(name));
        return true;
    }

    QString s_;
    const CalcVars* vars_ = nullptr;
    int pos_ = 0;
    QString err_;
};

QString superscript(int n) {
    static const QString sup = QString::fromUtf8("⁰¹²³⁴⁵⁶⁷⁸⁹");
    QString out;
    const QString digits = QString::number(std::abs(n));
    if (n < 0) out += QChar(0x207B);
    for (QChar c : digits) out += sup[c.digitValue()];
    return out;
}

// Four significant figures; ×10ⁿ outside 0.001–99999.
QString formatNumber(double v) {
    if (v == 0) return "0";
    const double a = std::fabs(v);
    if (a >= 1e-3 && a < 1e5) {
        const int mag = static_cast<int>(std::floor(std::log10(a)));
        int decimals = qMax(0, 3 - mag);
        // Fewer decimals only when the value is exact there (25 mmol, 2.5 g); 607.95 stays 608.0.
        for (int k = 0; k < decimals; ++k) {
            const double f = std::pow(10.0, k);
            if (std::fabs(v - std::round(v * f) / f) <= 1e-9 * a) {
                decimals = k;
                break;
            }
        }
        return QString::number(v, 'f', decimals).replace('-', QChar(0x2212));
    }
    int ex = static_cast<int>(std::floor(std::log10(a)));
    double mant = v / std::pow(10.0, ex);
    QString m = QString::number(mant, 'f', 3);
    if (m.startsWith("10.") || m.startsWith("-10.")) {
        mant /= 10;
        ++ex;
        m = QString::number(mant, 'f', 3);
    }
    while (m.endsWith('0')) m.chop(1);
    if (m.endsWith('.')) m.chop(1);
    return m.replace('-', QChar(0x2212)) + QString::fromUtf8("×10") + superscript(ex);
}

// A unit for showing `q`: one written in the expression, or a common one (with a prefix that
// keeps the number between 1 and 1000), or the SI base units.
QString chooseUnit(const Q& q, const QList<QPair<QString, Q>>& used, double* scale) {
    for (const auto& [name, u] : used)
        if (u.d == q.d) {
            *scale = u.v;
            return name;
        }
    struct Common {
        const char* text;
        bool prefix;
    };
    static const Common common[] = {{"g", true},     {"mol", true},     {"L", true},      {"M", true},  {"J", true},
                                    {"J/mol", true}, {"J/(mol K)", false}, {"g/mol", false}, {"Pa", true}, {"N", true},
                                    {"W", true},     {"C", true},       {"V", true},      {"A", true},  {"s", false},
                                    {"m", true},     {"m/s", false},    {"K", false},     {"Hz", true}, {"M/s", false},
                                    {"mol/s", false}, {"m²", false}, {"m³", false}};
    for (const Common& c : common) {
        Parser p(QString::fromUtf8(c.text));
        Q u;
        QString err;
        if (!p.parse(&u, &err) || u.d != q.d) continue;
        QString text = QString::fromUtf8(c.text);
        double s = u.v;
        if (c.prefix) {
            static const QList<QPair<QString, double>> prefixes = {{"G", 1e9}, {"M", 1e6}, {"k", 1e3}, {"", 1}, {"m", 1e-3},
                                                                   {QString::fromUtf8("µ"), 1e-6}, {"n", 1e-9}, {"p", 1e-12}};
            for (const auto& [pre, f] : prefixes) {
                if (text == "M" && pre == "M") continue;  // no "MM"
                const double val = std::fabs(q.v / (u.v * f));
                if (val >= 1 || pre == "p") {
                    text = pre + text;
                    s = u.v * f;
                    break;
                }
            }
        }
        *scale = s;
        return text;
    }
    // SI base units: kg·m²·s⁻²
    static const char* names[kDims] = {"m", "kg", "s", "mol", "K", "A"};
    QStringList parts;
    for (int i : {1, 0, 2, 3, 4, 5})
        if (q.d[i]) parts << QString::fromLatin1(names[i]) + (q.d[i] == 1 ? QString() : superscript(q.d[i]));
    *scale = 1;
    return parts.join(QChar(0x00B7));
}

}  // namespace

QString calcExpression(const QString& line) {
    const QString t = line.trimmed();
    if (!t.startsWith("= ") && t != "=") return QString();
    return t.mid(1).trimmed();
}

CalcResult evaluate(const QString& input, const CalcVars* vars) {
    CalcResult r;
    QString text = input.trimmed();
    if (text.isEmpty()) {
        r.error = "type a calculation";
        return r;
    }
    // "... to kJ/mol": the unit to show it in.
    QString target;
    static const QRegularExpression to(R"(\s+(?:to|in)\s+(.+)$|\s*(?:->|→)\s*(.+)$)");
    if (const QRegularExpressionMatch m = to.match(text); m.hasMatch()) {
        target = (m.capturedLength(1) ? m.captured(1) : m.captured(2)).trimmed();
        text = text.left(m.capturedStart()).trimmed();
    }
    static const QRegularExpression celsius(QString(R"(^([-−]?\d+\.?\d*)\s*(°C|degC|℃)$)"));
    static const QRegularExpression celsiusTarget(QString(R"(^(°C|degC|℃)$)"));
    Q q;
    if (const QRegularExpressionMatch c = celsius.match(text); c.hasMatch() && !target.isEmpty()) {
        q = unitQ(QString(c.captured(1)).replace(QChar(0x2212), '-').toDouble() + 273.15, 0, 0, 0, 0, 1);  // a temperature
    } else {
        Parser p(text, vars);
        QString err;
        if (!p.parse(&q, &err)) {
            r.error = err;
            return r;
        }
        if (!std::isfinite(q.v)) {
            r.error = "the result is not a number";
            return r;
        }
        if (target.isEmpty()) {
            if (plain(q)) {
                r.ok = true;
                r.value = formatNumber(q.v);
                return r;
            }
            double scale = 1;
            const QString unit = chooseUnit(q, p.used, &scale);
            r.ok = true;
            r.value = formatNumber(q.v / scale) + " " + unit;
            return r;
        }
    }
    if (celsiusTarget.match(target).hasMatch()) {
        if (q.d != unitQ(1, 0, 0, 0, 0, 1).d) {
            r.error = "that is not a temperature";
            return r;
        }
        r.ok = true;
        r.value = formatNumber(q.v - 273.15) + QString::fromUtf8(" °C");
        return r;
    }
    Parser tp(target);
    Q t;
    QString err;
    if (!tp.parse(&t, &err)) {
        r.error = err;
        return r;
    }
    if (t.d != q.d) {
        r.error = QString("that is not in %1").arg(target);
        return r;
    }
    r.ok = true;
    r.value = formatNumber(q.v / t.v) + " " + target;
    return r;
}

bool evaluateQuantity(const QString& expression, CalcQuantity* out, QString* error, const CalcVars* vars) {
    Parser p(expression.trimmed(), vars);
    Q q;
    QString err;
    if (expression.trimmed().isEmpty()) err = "empty";
    else if (p.parse(&q, &err) && !std::isfinite(q.v)) err = "the result is not a number";
    if (!err.isEmpty()) {
        if (error) *error = err;
        return false;
    }
    out->si = q.v;
    for (int i = 0; i < kDims; ++i) out->dims[static_cast<size_t>(i)] = q.d[i];
    out->unit.clear();
    double scale = 1;
    if (!plain(q)) out->unit = chooseUnit(q, p.used, &scale);
    return true;
}

bool quantityIn(const CalcQuantity& q, const QString& unit, double* value) {
    Q u{1, {}};
    QString err;
    if (!unit.trimmed().isEmpty() && !Parser(unit).parse(&u, &err)) return false;
    for (int i = 0; i < kDims; ++i)
        if (u.d[i] != q.dims[static_cast<size_t>(i)]) return false;
    *value = q.si / u.v;
    return true;
}

QString quantityUnit(const CalcQuantity& q, double* scale) {
    Q x{q.si, {}};
    for (int i = 0; i < kDims; ++i) x.d[i] = q.dims[static_cast<size_t>(i)];
    *scale = 1;
    if (plain(x)) return QString();
    QList<QPair<QString, Q>> used;
    Q u;
    QString err;
    if (!q.unit.isEmpty() && Parser(q.unit).parse(&u, &err)) used.append({q.unit, u});
    return chooseUnit(x, used, scale);
}

QString calcNumber(double v) { return formatNumber(v); }

}  // namespace graphite

namespace graphite {

QString calcTex(const QString& value) {
    const int sp = static_cast<int>(value.indexOf(' '));
    QString num = sp < 0 ? value : value.left(sp), unit = sp < 0 ? QString() : value.mid(sp + 1);
    auto sup = [](const QString& s) {  // ⁻¹² -> ^{-12}
        static const QString digits = QString::fromUtf8("⁰¹²³⁴⁵⁶⁷⁸⁹");
        QString out, run;
        auto flush = [&] {
            if (!run.isEmpty()) out += "^{" + run + "}";
            run.clear();
        };
        for (QChar c : s) {
            if (c == QChar(0x207B)) run += '-';
            else if (digits.contains(c)) run += QString::number(digits.indexOf(c));
            else {
                flush();
                out += c;
            }
        }
        flush();
        return out;
    };
    num.replace(QChar(0x2212), '-');
    num.replace(QString::fromUtf8("×10"), "\\times 10");
    num = sup(num);
    if (unit.isEmpty()) return "=\\ " + num;
    unit = sup(unit);
    unit.replace(QChar(0x00B7), "\\cdot ").replace(' ', "\\ ");
    unit.replace(QChar(0x00B5), "\\mu ").replace(QChar(0x03A9), "\\Omega ").replace(QString::fromUtf8("°C"), "{}^{\\circ}C");
    unit.replace(QChar(0x00C5), "\\AA ");
    return "=\\ " + num + "\\ \\mathrm{" + unit + "}";
}

}  // namespace graphite
