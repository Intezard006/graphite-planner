// Quick maths with units, for a line that starts with "= ":
//
//   = 0.5 mol * 58.44 g/mol          ->  29.22 g
//   = 2.5 g / M(CuSO4.5H2O)          ->  0.01001 mol
//   = 8.314 J/(mol K) * 298 K to kJ/mol  ->  2.478 kJ/mol
//   = 25 °C to K                     ->  298.1 K
//
// Numbers carry SI dimensions; "to" / "in" converts to a unit, otherwise the result is shown
// in a unit from the expression, or a common one (J, Pa, M, g/mol...). Constants: R, NA, F,
// kB, c, e, hP (Planck; h is the hour). M(formula) is a molar mass. ln, log, exp, sqrt.
// Pure functions, no state.
#pragma once

#include <QHash>
#include <QString>
#include <array>

namespace graphite {

struct CalcResult {
    bool ok = false;
    QString value;  // "29.22 g", or "6.022×10²³ mol⁻¹"
    QString error;  // why not, when !ok: "unknown unit 'gm'"
};

// A quantity in SI base units (m kg s mol K A), for variables: a table column's value. `unit`
// is how it was written ("g"), so a result in the same kind of unit is shown in it too.
struct CalcQuantity {
    double si = 0;
    std::array<int, 6> dims{};
    QString unit;
};
using CalcVars = QHash<QString, CalcQuantity>;  // name -> value; names shadow units ("m", "V")

CalcResult evaluate(const QString& expression, const CalcVars* vars = nullptr);

// The bare quantity (no "to"); false with the reason when it can't be worked out.
bool evaluateQuantity(const QString& expression, CalcQuantity* q, QString* error = nullptr, const CalcVars* vars = nullptr);
// `q` as a number of `unit` ("" = a plain number); false when the kinds differ.
bool quantityIn(const CalcQuantity& q, const QString& unit, double* value);
// The unit evaluate() would show `q` in, and its size in SI.
QString quantityUnit(const CalcQuantity& q, double* scale);
// A number as quick maths shows it: four significant figures, ×10ⁿ when large or small.
QString calcNumber(double v);

// The expression of a "= ..." line, or a null string when the line is not one.
QString calcExpression(const QString& line);

// The result as TeX, for the live preview card: "=\ 29.22\ \mathrm{g}".
QString calcTex(const QString& value);

}  // namespace graphite
