#include "ui/chem.hpp"

#include <QRegularExpression>
#include <QStringList>

namespace graphite {

namespace {

// Index of the bracket closing the one at `open`, or -1.
int matching(const QString& s, int open) {
    const QChar o = s[open];
    const QChar c = (o == '(') ? ')' : (o == '[') ? ']' : '}';
    int depth = 0;
    for (int k = open; k < s.size(); ++k) {
        if (s[k] == o) ++depth;
        else if (s[k] == c && --depth == 0) return k;
    }
    return -1;
}

QString escapeChar(QChar c) {
    switch (c.unicode()) {
        case '%': return "\\%";
        case '&': return "\\&";
        case '$': return "\\$";
        default: return QString(c);
    }
}

class Chem {
public:
    explicit Chem(const QString& text) : s_(text), n_(text.size()) {}

    // A whole expression: species, operators, arrows.
    QString expression() {
        QString out;
        while (i_ < n_) {
            if (s_[i_].isSpace()) {
                ++i_;
                continue;
            }
            const QString arrow = tryArrow();
            if (!arrow.isNull()) {
                out += " " + arrow + " ";
                continue;
            }
            const QChar c = s_[i_];
            if (c == '+') {
                out += " + ";
                ++i_;
            } else if (c == '=') {
                out += " = ";
                ++i_;
            } else if (c == '^' && atWordEnd(i_ + 1)) {
                out += "\\uparrow ";  // gas evolved
                ++i_;
            } else if (c == 'v' && atWordEnd(i_ + 1) && (i_ == 0 || s_[i_ - 1].isSpace())) {
                out += "\\downarrow ";  // precipitate
                ++i_;
            } else {
                out += term();
            }
        }
        return out.trimmed();
    }

private:
    bool atWordEnd(int k) const { return k >= n_ || s_[k].isSpace(); }
    QChar at(int k) const { return (k >= 0 && k < n_) ? s_[k] : QChar(); }

    // "->", "<-", "<->", "<=>" with optional [above][below] labels.
    QString tryArrow() {
        struct A {
            const char* sym;
            const char* plain;
            const char* wide;  // with labels: %1 above, %2 below
        };
        static const A arrows[] = {
            {"<=>>", "\\rightleftharpoons", ""}, {"<<=>", "\\rightleftharpoons", ""},
            {"<=>", "\\rightleftharpoons", ""},  {"<->", "\\leftrightarrow", ""},
            {"->", "\\rightarrow", "\\xrightarrow"}, {"<-", "\\leftarrow", "\\xleftarrow"},
        };
        for (const A& a : arrows) {
            const QString sym = QString::fromLatin1(a.sym);
            if (!s_.mid(i_, sym.size()).startsWith(sym)) continue;
            i_ += sym.size();
            QString above, below;
            bool hasAbove = false, hasBelow = false;
            if (at(i_) == '[') {
                const int close = matching(s_, i_);
                if (close > 0) {
                    above = Chem(s_.mid(i_ + 1, close - i_ - 1)).expression();
                    hasAbove = true;
                    i_ = close + 1;
                    if (at(i_) == '[') {
                        const int close2 = matching(s_, i_);
                        if (close2 > 0) {
                            below = Chem(s_.mid(i_ + 1, close2 - i_ - 1)).expression();
                            hasBelow = true;
                            i_ = close2 + 1;
                        }
                    }
                }
            }
            if (!hasAbove) return QString::fromLatin1(a.plain);
            if (QString::fromLatin1(a.wide).isEmpty()) {  // equilibrium or double arrow: stack the labels
                QString r = QString::fromLatin1(a.plain);
                if (hasBelow) r = "\\underset{" + below + "}{" + r + "}";
                return "\\overset{" + above + "}{" + r + "}";
            }
            if (hasBelow) return QString::fromLatin1(a.wide) + "[" + below + "]{" + above + "}";
            return QString::fromLatin1(a.wide) + "{" + above + "}";
        }
        return QString();
    }

    // One term: an optional coefficient and a species.
    QString term() {
        QString out;
        // Leading coefficient: "2", "1/2", "0.5".
        if (s_[i_].isDigit()) {
            int j = i_;
            while (j < n_ && (s_[j].isDigit() || (s_[j] == '.' && at(j + 1).isDigit()))) ++j;
            if (at(j) == '/' && at(j + 1).isDigit()) {
                int k = j + 1;
                while (k < n_ && s_[k].isDigit()) ++k;
                out += "\\frac{" + s_.mid(i_, j - i_) + "}{" + s_.mid(j + 1, k - j - 1) + "}\\,";
                i_ = k;
            } else {
                out += "\\mathrm{" + s_.mid(i_, j - i_) + "}";
                i_ = j;
                if (i_ < n_ && at(i_) != '+') out += "\\,";
            }
            while (i_ < n_ && s_[i_].isSpace()) ++i_;
            if (i_ >= n_) return out;
            if (s_[i_] == '+' || s_[i_] == '=' || !tryPeekArrow().isNull()) return out;  // a bare number
        }
        const size_t before = static_cast<size_t>(i_);
        const QString body = species(false);
        if (static_cast<size_t>(i_) == before) {  // nothing consumed: never loop
            out += escapeChar(s_[i_++]);
            return out;
        }
        out += "\\mathrm{" + body + "}";
        return out;
    }

    QString tryPeekArrow() {
        const int save = i_;
        const QString a = tryArrow();
        i_ = save;
        return a;
    }

    // The characters of one species, as the inside of \mathrm{...}.
    // `inGroup` allows spaces (inside parentheses).
    QString species(bool inGroup) {
        QString out;
        bool atom = false;  // the previous item can take a subscript or a charge
        while (i_ < n_) {
            const QChar c = s_[i_];
            if (c.isSpace()) {
                if (!inGroup) break;
                out += "\\,";
                ++i_;
                continue;
            }
            if (!inGroup && (c == '<' || (c == '-' && at(i_ + 1) == '>'))) break;  // an arrow starts

            if (c == '+') {
                const QChar next = at(i_ + 1);
                const bool charge = atom && (next.isNull() || next.isSpace() || next == ')' || next == ']' || next == '^' || next == '}');
                if (!charge) break;  // an operator between species
                out += "^{+}";
                ++i_;
            } else if (c == '-') {
                const QChar next = at(i_ + 1);
                if (next == '>') break;
                const bool bond = atom && (next.isLetterOrNumber() || next == '(' || next == '[');
                if (bond) out += "{-}";
                else if (atom) out += "^{-}";
                else break;
                ++i_;
            } else if (c == '=') {
                if (!(atom && !at(i_ + 1).isSpace() && !at(i_ + 1).isNull())) break;
                out += "{=}";
                atom = false;
                ++i_;
            } else if (c == '#') {
                out += "\\equiv ";
                atom = false;
                ++i_;
            } else if (c.isUpper()) {
                int j = i_ + 1;
                if (at(j).isLower()) {
                    int k = j;
                    while (at(k).isLower()) ++k;
                    // "Cl", "Ca": element; "Delta", "Heat": a word.
                    j = (k - i_ <= 2) ? j + 1 : k;
                }
                out += s_.mid(i_, j - i_);
                i_ = j;
                atom = true;
                continue;
            } else if (c.isLower()) {
                int j = i_;
                while (at(j).isLower()) ++j;
                out += s_.mid(i_, j - i_);
                i_ = j;
                atom = true;
                continue;
            } else if (c.isDigit()) {
                int j = i_;
                if (atom) {
                    while (at(j).isDigit()) ++j;
                    out += "_{" + s_.mid(i_, j - i_) + "}";
                } else {
                    while (at(j).isDigit() || (at(j) == '.' && at(j + 1).isDigit())) ++j;
                    out += s_.mid(i_, j - i_);
                }
                i_ = j;
                continue;
            } else if (c == '(' || c == '[') {
                const int close = matching(s_, i_);
                if (close < 0) {
                    out += escapeChar(c);
                    ++i_;
                    continue;
                }
                const QChar closeCh = s_[close];
                Chem inner(s_.mid(i_ + 1, close - i_ - 1));
                out += QString(c) + inner.species(true) + QString(closeCh);
                i_ = close + 1;
                atom = true;
                continue;
            } else if (c == '{') {
                const int close = matching(s_, i_);
                if (close < 0) {
                    ++i_;
                    continue;
                }
                Chem inner(s_.mid(i_ + 1, close - i_ - 1));
                out += "{" + inner.species(true) + "}";
                i_ = close + 1;
                atom = true;
                continue;
            } else if (c == '^' || c == '_') {
                if (out.isEmpty() || out.endsWith("}") == false) {
                    if (out.isEmpty()) out += "{}";  // prefix script, as in isotopes: {}^{14}C
                }
                const QString mark = (c == '^') ? "^" : "_";
                ++i_;
                QString body;
                if (at(i_) == '{') {
                    const int close = matching(s_, i_);
                    if (close < 0) break;
                    body = s_.mid(i_ + 1, close - i_ - 1);
                    i_ = close + 1;
                } else {
                    int j = i_;
                    while (j < n_ && (s_[j].isDigit() || s_[j] == '+' || s_[j] == '-')) ++j;
                    body = s_.mid(i_, j - i_);
                    i_ = j;
                }
                out += mark + "{" + body + "}";
                continue;
            } else if (c == '.' || c == '*') {
                out += "\\cdot ";  // hydrate dot; decimals only occur in coefficients
                atom = false;
                ++i_;
                continue;
            } else if (c == '\\') {
                int j = i_ + 1;
                while (at(j).isLetter()) ++j;
                if (j == i_ + 1) ++j;  // "\," and friends
                out += s_.mid(i_, j - i_) + " ";
                i_ = j;
                atom = false;
                continue;
            } else {
                out += escapeChar(c);
                ++i_;
                continue;
            }
        }
        return out;
    }

    QString s_;
    int n_;
    int i_ = 0;
};

// "1.5e3" -> 1.5\times10^{3}
QString number(const QString& tok) {
    static const QRegularExpression sci(R"(^([+-]?\d*\.?\d+)[eE]([+-]?\d+)$)");
    const QRegularExpressionMatch m = sci.match(tok);
    if (m.hasMatch()) return m.captured(1) + "\\times10^{" + m.captured(2) + "}";
    return tok;
}

// "kJ.mol-1", "m/s", "m2" -> inside \mathrm{...}
QString unit(const QString& u) {
    QString out;
    int i = 0;
    const int n = u.size();
    while (i < n) {
        const QChar c = u[i];
        if (c.isLetter()) {
            int j = i;
            while (j < n && u[j].isLetter()) ++j;
            out += u.mid(i, j - i);
            i = j;
            // an exponent straight after the letters: "-1", "2", "^-1", "^{2}"
            int k = i;
            if (k < n && u[k] == '^') ++k;
            if (k < n && u[k] == '{') {
                const int close = u.indexOf('}', k);
                if (close > 0) {
                    out += "^{" + u.mid(k + 1, close - k - 1) + "}";
                    i = close + 1;
                }
            } else {
                int m = k;
                if (m < n && (u[m] == '-' || u[m] == '+')) ++m;
                int d = m;
                while (d < n && u[d].isDigit()) ++d;
                if (d > m) {
                    out += "^{" + u.mid(k, d - k) + "}";
                    i = d;
                }
            }
        } else if (c == '.' || c == '*') {
            out += "\\,";
            ++i;
        } else {
            out += escapeChar(c);
            ++i;
        }
    }
    return out;
}

}  // namespace

QString chemToTex(const QString& ce) { return Chem(ce.trimmed()).expression(); }

QString physUnitToTex(const QString& pu) {
    const QStringList parts = pu.trimmed().split(QRegularExpression(R"(\s+)"), Qt::SkipEmptyParts);
    if (parts.isEmpty()) return QString();
    static const QRegularExpression isNumber(R"(^[+-]?\d*\.?\d+([eE][+-]?\d+)?$)");
    QString out;
    int first = 0;
    if (isNumber.match(parts[0]).hasMatch()) {
        out = number(parts[0]);
        first = 1;
    }
    QString units;
    for (int k = first; k < parts.size(); ++k) {
        if (!units.isEmpty()) units += "\\,";
        units += unit(parts[k]);
    }
    if (units.isEmpty()) return out;
    if (!out.isEmpty()) out += "\\,";
    return out + "\\mathrm{" + units + "}";
}

QString expandChemistry(const QString& tex) {
    QString out;
    int i = 0;
    while (i < tex.size()) {
        const bool ce = tex.mid(i, 4) == "\\ce{";
        const bool pu = tex.mid(i, 4) == "\\pu{";
        if (ce || pu) {
            const int open = i + 3;
            const int close = matching(tex, open);
            if (close > 0) {
                const QString inner = tex.mid(open + 1, close - open - 1);
                out += ce ? chemToTex(inner) : physUnitToTex(inner);
                i = close + 1;
                continue;
            }
        }
        out += tex[i++];
    }
    return out;
}

}  // namespace graphite
