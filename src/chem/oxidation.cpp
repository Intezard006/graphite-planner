#include "chem/oxidation.hpp"

#include <QMap>
#include <QRegularExpression>
#include <QSet>
#include <QStringList>
#include <numeric>

#include "chem/balance.hpp"
#include "chem/molar.hpp"

namespace graphite {

namespace {

struct Rule {
    QStringList elements;
    int state;
};

const QList<Rule>& rules() {
    static const QList<Rule> r = {
        {{"F"}, -1},
        {{"Li", "Na", "K", "Rb", "Cs", "Fr"}, 1},
        {{"Be", "Mg", "Ca", "Sr", "Ba", "Ra"}, 2},
        {{"Al"}, 3},
        {{"Zn"}, 2},
        {{"H"}, 1},
        {{"O"}, -2},
        {{"Cl", "Br", "I", "At"}, -1},
        {{"S", "Se", "Te"}, -2},
        {{"N", "P", "As"}, -3},
    };
    return r;
}

// The species of one side of a reaction, coefficients and marks removed.
QStringList speciesOf(const QString& side) {
    static const QRegularExpression plus(R"(\s\+\s)");
    static const QRegularExpression parts(R"(^(?:\d+(?:/\d+)?|\d*\.\d+)?\s*(\S+))");
    QStringList out;
    for (const QString& raw : side.trimmed().split(plus, Qt::SkipEmptyParts)) {
        const QRegularExpressionMatch m = parts.match(raw.trimmed());
        if (m.hasMatch()) out << m.captured(1);
    }
    return out;
}

bool splitReaction(const QString& ce, QString* left, QString* right) {
    static const QRegularExpression arrow(R"((?:^|\s)(<=>>|<<=>|<=>|<->|<-->|->|<-|=|→|⟶|⇌|⇄)((?:\[[^\]]*\]){0,2})(?=\s|$))");
    const QRegularExpressionMatch m = arrow.match(ce);
    if (!m.hasMatch()) return false;
    *left = ce.left(m.capturedStart(1));
    *right = ce.mid(m.capturedEnd(2));
    return true;
}

bool isElectron(const QString& s) { return s == "e-" || s == "e^-" || s == "e^{-}"; }

// The state of `element` in `species`, if the rules give one.
bool stateIn(const QString& species, const QString& element, OxState* out) {
    for (const OxState& s : oxidationStates(species))
        if (s.element == element) {
            *out = s;
            return true;
        }
    return false;
}

bool same(const OxState& a, const OxState& b) { return a.num * b.den == b.num * a.den; }

}  // namespace

QString OxState::text() const {
    if (num == 0) return "0";
    const QString sign = num > 0 ? QString("+") : QString(QChar(0x2212));
    const QString n = QString::number(std::abs(num));
    return sign + (den == 1 ? n : n + "/" + QString::number(den));
}

QList<OxState> oxidationStates(const QString& species) {
    QMap<QString, int> atoms;
    int charge = 0;
    if (isElectron(species) || !formulaAtoms(species, &atoms, &charge) || atoms.isEmpty()) return {};
    // In the order written: "KMnO4" -> K, Mn, O.
    QStringList order;
    static const QRegularExpression sym(R"([A-Z][a-z]?)");
    for (auto it = sym.globalMatch(species); it.hasNext();) {
        const QString e = it.next().captured();
        if (atoms.contains(e) && !order.contains(e)) order << e;
    }
    QMap<QString, int> fixed;
    int left = static_cast<int>(atoms.size());
    for (const Rule& r : rules()) {
        for (const QString& e : r.elements) {
            if (left <= 1) break;  // the last one is worked out from the charge
            if (atoms.contains(e) && !fixed.contains(e)) {
                fixed.insert(e, r.state);
                --left;
            }
        }
    }
    QList<OxState> out;
    QString unknown;
    int sum = 0;
    for (auto it = fixed.cbegin(); it != fixed.cend(); ++it) sum += it.value() * atoms.value(it.key());
    for (const QString& e : order)
        if (!fixed.contains(e)) unknown = unknown.isEmpty() ? e : QString("?");
    for (const QString& e : order) {
        OxState s;
        s.element = e;
        if (fixed.contains(e)) {
            s.num = fixed.value(e);
        } else if (unknown == e) {
            const int total = charge - sum, n = atoms.value(e);
            const int g = std::gcd(std::abs(total), n);
            s.num = g ? total / g : 0;
            s.den = g ? n / g : 1;
        } else {
            continue;  // two elements no rule separates: say nothing about them
        }
        out << s;
    }
    if (unknown == "?") {  // the fixed ones alone may not add up: keep only rule-given states
        QList<OxState> sure;
        for (const OxState& s : out)
            if (fixed.contains(s.element)) sure << s;
        return sure;
    }
    return out;
}

QString oxidationLine(const QString& species) {
    QStringList parts;
    for (const OxState& s : oxidationStates(species)) parts << s.element + " " + s.text();
    return parts.join(QString::fromUtf8("  ·  "));
}

RedoxReport redoxReport(const QString& ce) {
    RedoxReport r;
    QString left, right;
    if (!splitReaction(ce, &left, &right)) return r;
    const QStringList reactants = speciesOf(left), products = speciesOf(right);

    // Each element that is in one state on the left and another on the right.
    QStringList elements;
    for (const QString& s : reactants + products)
        for (const OxState& o : oxidationStates(s))
            if (!elements.contains(o.element)) elements << o.element;
    struct Change {
        QString element;
        OxState from, to;
        QStringList a, b;  // the species involved, each side
    };
    QList<Change> changes;
    for (const QString& e : elements) {
        for (const QString& ra : reactants) {
            OxState a;
            if (!stateIn(ra, e, &a)) continue;
            for (const QString& pb : products) {
                OxState b;
                if (!stateIn(pb, e, &b) || same(a, b)) continue;
                bool merged = false;
                for (Change& c : changes)
                    if (c.element == e && same(c.from, a) && same(c.to, b)) {
                        if (!c.a.contains(ra)) c.a << ra;
                        if (!c.b.contains(pb)) c.b << pb;
                        merged = true;
                    }
                if (!merged) changes << Change{e, a, b, {ra}, {pb}};
            }
        }
    }
    // H and O only "change" as a side effect of the others, unless nothing else does.
    QList<Change> main;
    for (const Change& c : changes)
        if (c.element != "H" && c.element != "O") main << c;
    if (main.isEmpty()) main = changes;

    for (const Change& c : main) {
        const bool up = c.to.num * c.from.den > c.from.num * c.to.den;
        r.changes << c.element + " " + c.from.text() + QString::fromUtf8(" → ") + c.to.text() + (up ? " (oxidised)" : " (reduced)");
        QString& half = up ? r.oxidation : r.reduction;
        if (!half.isEmpty()) continue;
        // The half equation: the species, then electrons on the taking side, and whatever of
        // H+ / H2O makes it balance (acidic solution), simplest first.
        const QString e = "e-";
        const QList<QPair<QStringList, QStringList>> extras = {
            {{}, {}}, {{}, {"H2O"}}, {{"H2O"}, {}}, {{"H+"}, {"H2O"}}, {{"H2O"}, {"H+"}}, {{"H+"}, {}}, {{}, {"H+"}}};
        for (const auto& [l, rr] : extras) {
            QStringList ls = c.a + l, rs = c.b + rr;
            (up ? rs : ls) << e;
            const BalanceResult b = balanceEquation(ls.join(" + ") + " -> " + rs.join(" + "));
            if (b.ok) {
                half = b.ce;
                break;
            }
        }
    }
    return r;
}

}  // namespace graphite
