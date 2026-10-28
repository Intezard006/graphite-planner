#include "chem/balance.hpp"

#include <QList>
#include <QMap>
#include <QRegularExpression>
#include <QStringList>
#include <algorithm>
#include <numeric>
#include <vector>

#include "chem/molar.hpp"

namespace graphite {

namespace {

// The arrow between the two sides, with its conditions: "->", "<=>[H+]", "->[\Delta][]".
const QRegularExpression& arrowRe() {
    static const QRegularExpression re(R"((?:^|\s)(<=>>|<<=>|<=>|<->|<-->|->|<-|=|→|⟶|⇌|⇄)((?:\[[^\]]*\]){0,2})(?=\s|$))");
    return re;
}

struct Species {
    QString formula;  // "Fe^3+", "e-"
    QString tail;     // " ^", " v", "(aq)" written after a space: kept as is
    long long written = 1;  // the coefficient as written (fractions count as not balanced)
    bool fraction = false;
    QMap<QString, int> atoms;
    int charge = 0;
};

bool readSide(const QString& side, QList<Species>* out, QString* error) {
    static const QRegularExpression plus(R"(\s\+\s)");
    static const QRegularExpression parts(R"(^(\d+(?:/\d+)?|\d*\.\d+)?\s*(\S+)(.*)$)");
    for (const QString& raw : side.trimmed().split(plus, Qt::SkipEmptyParts)) {
        const QRegularExpressionMatch m = parts.match(raw.trimmed());
        if (!m.hasMatch()) {
            *error = "can't read '" + raw.trimmed() + "'";
            return false;
        }
        Species s;
        const QString coef = m.captured(1);
        if (!coef.isEmpty()) {
            if (coef.contains('/') || coef.contains('.')) s.fraction = true;
            else s.written = coef.toLongLong();
        }
        s.formula = m.captured(2);
        s.tail = m.captured(3);
        if (s.formula == "e-" || s.formula == "e^-" || s.formula == "e^{-}") {
            s.charge = -1;  // an electron: charge, no atoms
        } else if (!formulaAtoms(s.formula, &s.atoms, &s.charge)) {
            *error = "can't read '" + s.formula + "'";
            return false;
        }
        *out << s;
    }
    if (out->isEmpty()) {
        *error = "one side is empty";
        return false;
    }
    return true;
}

long long gcdll(long long a, long long b) { return std::gcd(a < 0 ? -a : a, b < 0 ? -b : b); }

}  // namespace

BalanceResult balanceEquation(const QString& ceText) {
    BalanceResult r;
    const QRegularExpressionMatch am = arrowRe().match(ceText);
    if (!am.hasMatch()) {
        r.error = "no reaction arrow";
        return r;
    }
    const QString arrow = am.captured(1) + am.captured(2);
    const QString left = ceText.left(am.capturedStart(1));
    const QString right = ceText.mid(am.capturedEnd(2));
    if (arrowRe().match(right).hasMatch()) {
        r.error = "more than one arrow";
        return r;
    }
    QList<Species> sp;
    if (!readSide(left, &sp, &r.error)) return r;
    const int nLeft = static_cast<int>(sp.size());
    if (!readSide(right, &sp, &r.error)) return r;
    const int n = static_cast<int>(sp.size());
    if (n > 16) {
        r.error = "too many species";
        return r;
    }

    // One row per element, plus one for charge; reactants count +, products -.
    QStringList elements;
    for (const Species& s : sp)
        for (auto it = s.atoms.cbegin(); it != s.atoms.cend(); ++it)
            if (!elements.contains(it.key())) elements << it.key();
    bool charged = false;
    for (const Species& s : sp) charged = charged || s.charge != 0;
    std::vector<std::vector<long long>> a;
    auto addRow = [&](auto value) {
        std::vector<long long> row(static_cast<size_t>(n));
        for (int j = 0; j < n; ++j) row[static_cast<size_t>(j)] = value(sp[j]) * (j < nLeft ? 1 : -1);
        a.push_back(row);
    };
    for (const QString& e : elements) addRow([&](const Species& s) { return static_cast<long long>(s.atoms.value(e)); });
    if (charged) addRow([](const Species& s) { return static_cast<long long>(s.charge); });

    // Whole-number reduced row echelon form.
    const int rows = static_cast<int>(a.size());
    std::vector<int> pivotCol;
    int pr = 0;
    for (int c = 0; c < n && pr < rows; ++c) {
        int p = pr;
        while (p < rows && a[static_cast<size_t>(p)][static_cast<size_t>(c)] == 0) ++p;
        if (p == rows) continue;
        std::swap(a[static_cast<size_t>(p)], a[static_cast<size_t>(pr)]);
        auto& piv = a[static_cast<size_t>(pr)];
        for (int k = 0; k < rows; ++k) {
            if (k == pr) continue;
            auto& row = a[static_cast<size_t>(k)];
            const long long f = row[static_cast<size_t>(c)];
            if (f == 0) continue;
            const long long pv = piv[static_cast<size_t>(c)];
            long long g = 0;
            for (int j = 0; j < n; ++j) {
                row[static_cast<size_t>(j)] = row[static_cast<size_t>(j)] * pv - piv[static_cast<size_t>(j)] * f;
                g = gcdll(g, row[static_cast<size_t>(j)]);
            }
            if (g > 1)
                for (long long& v : row) v /= g;
        }
        pivotCol.push_back(c);
        ++pr;
    }
    const int rank = static_cast<int>(pivotCol.size());
    if (n - rank == 0) {
        r.error = "can't balance with these species";
        return r;
    }
    if (n - rank > 1) {
        r.error = "more than one way to balance it";
        return r;
    }
    int free = 0;
    while (std::find(pivotCol.begin(), pivotCol.end(), free) != pivotCol.end()) ++free;

    // x_free = lcm of the pivots; each pivot column follows from its row.
    long long l = 1;
    for (int i = 0; i < rank; ++i) {
        const long long pv = a[static_cast<size_t>(i)][static_cast<size_t>(pivotCol[static_cast<size_t>(i)])];
        l = l / gcdll(l, pv) * (pv < 0 ? -pv : pv);
    }
    std::vector<long long> x(static_cast<size_t>(n), 0);
    x[static_cast<size_t>(free)] = l;
    for (int i = 0; i < rank; ++i) {
        const auto& row = a[static_cast<size_t>(i)];
        const int pc = pivotCol[static_cast<size_t>(i)];
        x[static_cast<size_t>(pc)] = -row[static_cast<size_t>(free)] * l / row[static_cast<size_t>(pc)];
    }
    long long g = 0;
    for (long long v : x) g = gcdll(g, v);
    if (x[0] < 0) g = -g;
    for (long long& v : x) v /= g;
    for (int j = 0; j < n; ++j) {
        if (x[static_cast<size_t>(j)] <= 0) {
            r.error = x[static_cast<size_t>(j)] == 0 ? sp[j].formula + " takes no part" : sp[j].formula + " is on the wrong side";
            return r;
        }
    }

    r.already = true;
    for (int j = 0; j < n && r.already; ++j) r.already = !sp[j].fraction && sp[j].written * x[0] == x[static_cast<size_t>(j)] * sp[0].written;
    auto side = [&](int from, int to) {
        QStringList out;
        for (int j = from; j < to; ++j) {
            const long long k = x[static_cast<size_t>(j)];
            out << (k == 1 ? QString() : QString::number(k)) + sp[j].formula + sp[j].tail;
        }
        return out.join(" + ");
    };
    r.ce = side(0, nLeft) + " " + arrow + " " + side(nLeft, n);
    r.ok = true;
    return r;
}

bool reactionAt(const QString& text, int pos, int* start, int* length) {
    int from = 0, first = -1, firstLen = 0;
    while ((from = static_cast<int>(text.indexOf("\\ce{", from))) >= 0) {
        int depth = 0, end = -1;
        for (int k = from + 3; k < text.size(); ++k) {
            if (text[k] == '{') ++depth;
            else if (text[k] == '}' && --depth == 0) {
                end = k;
                break;
            }
        }
        if (end < 0) break;
        const int s = from + 4, len = end - s;
        if (arrowRe().match(text.mid(s, len)).hasMatch()) {
            if (pos >= from && pos <= end + 1) {
                *start = s;
                *length = len;
                return true;
            }
            if (first < 0) {
                first = s;
                firstLen = len;
            }
        }
        from = end + 1;
    }
    if (first < 0) return false;
    *start = first;
    *length = firstLen;
    return true;
}

}  // namespace graphite
