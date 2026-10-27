#include "ui/molar.hpp"

#include <QHash>
#include <QMap>
#include <QRegularExpression>
#include <QSet>
#include <QStringList>

namespace graphite {

namespace {

const QHash<QString, double>& weights() {
    static const QHash<QString, double> w = {
        {"H", 1.008},     {"D", 2.014},     {"He", 4.0026},   {"Li", 6.94},     {"Be", 9.0122},
        {"B", 10.81},     {"C", 12.011},    {"N", 14.007},    {"O", 15.999},    {"F", 18.998},
        {"Ne", 20.180},   {"Na", 22.990},   {"Mg", 24.305},   {"Al", 26.982},   {"Si", 28.085},
        {"P", 30.974},    {"S", 32.06},     {"Cl", 35.45},    {"Ar", 39.95},    {"K", 39.098},
        {"Ca", 40.078},   {"Sc", 44.956},   {"Ti", 47.867},   {"V", 50.942},    {"Cr", 51.996},
        {"Mn", 54.938},   {"Fe", 55.845},   {"Co", 58.933},   {"Ni", 58.693},   {"Cu", 63.546},
        {"Zn", 65.38},    {"Ga", 69.723},   {"Ge", 72.630},   {"As", 74.922},   {"Se", 78.971},
        {"Br", 79.904},   {"Kr", 83.798},   {"Rb", 85.468},   {"Sr", 87.62},    {"Y", 88.906},
        {"Zr", 91.224},   {"Nb", 92.906},   {"Mo", 95.95},    {"Tc", 98},       {"Ru", 101.07},
        {"Rh", 102.91},   {"Pd", 106.42},   {"Ag", 107.87},   {"Cd", 112.41},   {"In", 114.82},
        {"Sn", 118.71},   {"Sb", 121.76},   {"Te", 127.60},   {"I", 126.90},    {"Xe", 131.29},
        {"Cs", 132.91},   {"Ba", 137.33},   {"La", 138.91},   {"Ce", 140.12},   {"Pr", 140.91},
        {"Nd", 144.24},   {"Pm", 145},      {"Sm", 150.36},   {"Eu", 151.96},   {"Gd", 157.25},
        {"Tb", 158.93},   {"Dy", 162.50},   {"Ho", 164.93},   {"Er", 167.26},   {"Tm", 168.93},
        {"Yb", 173.05},   {"Lu", 174.97},   {"Hf", 178.49},   {"Ta", 180.95},   {"W", 183.84},
        {"Re", 186.21},   {"Os", 190.23},   {"Ir", 192.22},   {"Pt", 195.08},   {"Au", 196.97},
        {"Hg", 200.59},   {"Tl", 204.38},   {"Pb", 207.2},    {"Bi", 208.98},   {"Po", 209},
        {"At", 210},      {"Rn", 222},      {"Fr", 223},      {"Ra", 226},      {"Ac", 227},
        {"Th", 232.04},   {"Pa", 231.04},   {"U", 238.03},    {"Np", 237},      {"Pu", 244},
        {"Am", 243},      {"Cm", 247},      {"Bk", 247},      {"Cf", 251},      {"Es", 252},
        {"Fm", 257},      {"Md", 258},      {"No", 259},      {"Lr", 266},      {"Rf", 267},
        {"Db", 268},      {"Sg", 269},      {"Bh", 270},      {"Hs", 269},      {"Mt", 278},
        {"Ds", 281},      {"Rg", 282},      {"Cn", 285},      {"Nh", 286},      {"Fl", 289},
        {"Mc", 290},      {"Lv", 293},      {"Ts", 294},      {"Og", 294},
    };
    return w;
}

// Recursive descent over one hydrate part: elements, counts and bracketed groups.
class Parser {
public:
    explicit Parser(const QString& s) : s_(s) {}
    bool parse(QMap<QString, int>* atoms, int times) {
        return group(atoms, times, QChar()) && i_ == s_.size() && atoms_ > 0;
    }

private:
    int count() {
        int n = 0;
        bool any = false;
        while (i_ < s_.size() && s_[i_].isDigit()) {
            n = n * 10 + s_[i_].digitValue();
            ++i_;
            any = true;
        }
        return any ? n : 1;
    }
    // Adds the group's atoms times `times`; the count after a bracket is only known at its
    // end, so a bracket is read into its own map first.
    bool group(QMap<QString, int>* atoms, int times, QChar close) {
        while (i_ < s_.size()) {
            const QChar c = s_[i_];
            if (c == close) {
                ++i_;
                return true;
            }
            if (c == '(' || c == '[' || c == '{') {
                ++i_;
                QMap<QString, int> inner;
                if (!group(&inner, 1, c == '(' ? ')' : c == '[' ? ']' : '}')) return false;
                const int k = count();
                for (auto it = inner.cbegin(); it != inner.cend(); ++it) (*atoms)[it.key()] += it.value() * k * times;
            } else if (c.isUpper()) {
                QString sym(c);
                ++i_;
                if (i_ < s_.size() && s_[i_].isLower()) sym += s_[i_++];
                if (!weights().contains(sym)) return false;
                (*atoms)[sym] += count() * times;
                ++atoms_;
            } else {
                return false;
            }
        }
        return close.isNull();  // false: an unclosed bracket
    }
    QString s_;
    int i_ = 0;
    int atoms_ = 0;
};

}  // namespace

bool formulaAtoms(const QString& species, QMap<QString, int>* atoms, int* charge, QString* cleaned) {
    QString s = species.trimmed();
    static const QRegularExpression coefficient(R"(^(\d+(/\d+)?|\d*\.\d+)\s*(?=[A-Z(\[]))");
    static const QRegularExpression state(R"(\((s|l|g|aq|cr)\)$)");
    static const QRegularExpression isotope(R"(^\^\{?\d+\}?(_\{?\d+\}?)?)");
    static const QRegularExpression chargeRe(R"((\^\{?[0-9]*[+\-]+\}?|[+\-]+|\^\{?[0-9]*\}?)$)");
    s.remove(coefficient);
    s.remove(state);
    s.remove(isotope);
    const QRegularExpressionMatch cm = chargeRe.match(s);
    int z = 0;
    if (cm.hasMatch()) {
        // "^2-" -> -2, "^{3+}" -> +3, "+" -> +1, "++" -> +2; "^2" alone is no charge.
        const QString c = cm.captured(1);
        const int signs = static_cast<int>(c.count('+') + c.count('-'));
        QString digits;
        for (QChar ch : c) if (ch.isDigit()) digits += ch;
        const int n = digits.isEmpty() ? signs : digits.toInt();
        z = signs == 0 ? 0 : c.contains('-') ? -n : n;
        s.chop(cm.capturedLength());
    }
    s.remove(state);  // "Fe^3+(aq)" has both
    if (s.isEmpty()) return false;

    QMap<QString, int> total;
    for (QString part : s.split(QRegularExpression(R"([.*·•])"))) {
        if (part.isEmpty()) return false;
        int k = 1;
        int d = 0;
        while (d < part.size() && part[d].isDigit()) ++d;
        if (d > 0) {
            k = part.left(d).toInt();
            part = part.mid(d);
        }
        Parser p(part);
        if (!p.parse(&total, k)) return false;
    }
    if (atoms) *atoms = total;
    if (charge) *charge = z;
    if (cleaned) *cleaned = s;
    return true;
}

bool formulaMass(const QString& species, double* mass, QString* cleaned) {
    QMap<QString, int> atoms;
    if (!formulaAtoms(species, &atoms, nullptr, cleaned)) return false;
    double m = 0;
    for (auto it = atoms.cbegin(); it != atoms.cend(); ++it) m += weights().value(it.key()) * it.value();
    *mass = m;
    return true;
}

QList<SpeciesMass> chemMasses(const QString& latex) {
    QList<SpeciesMass> out;
    QSet<QString> seen;
    int from = 0;
    while ((from = static_cast<int>(latex.indexOf("\\ce{", from))) >= 0) {
        int depth = 0, end = -1;
        for (int k = from + 3; k < latex.size(); ++k) {
            if (latex[k] == '{') ++depth;
            else if (latex[k] == '}' && --depth == 0) {
                end = k;
                break;
            }
        }
        if (end < 0) break;
        const QString body = latex.mid(from + 4, end - from - 4);
        from = end + 1;
        // Species are separated by spaces in mhchem: "2H2 + O2 -> 2H2O".
        for (const QString& tok : body.split(' ', Qt::SkipEmptyParts)) {
            double m = 0;
            QString f;
            if (!formulaMass(tok, &m, &f) || seen.contains(f)) continue;
            seen.insert(f);
            out << SpeciesMass{f, m};
        }
    }
    return out;
}

QString massSummary(const QString& latex) {
    const QList<SpeciesMass> ms = chemMasses(latex);
    if (ms.isEmpty()) return QString();
    QStringList parts;
    for (const SpeciesMass& m : ms) parts << QString("%1 %2").arg(m.formula).arg(m.mass, 0, 'f', 2);
    return "M: " + parts.join("  \xC2\xB7  ") + " g/mol";
}

}  // namespace graphite
