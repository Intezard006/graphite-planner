// mhchem-style chemistry for the TeX renderer.
//
// MicroTeX has no \ce, so \ce{...} and \pu{...} are converted to plain LaTeX before the
// formula is typeset. This covers what shows up in lecture notes: formulas with subscripts,
// charges, state symbols, coefficients and fractions, arrows with conditions, equilibria,
// hydrates, isotopes, bonds, gas/precipitate arrows and simple units. It is not all of mhchem.
#pragma once

#include <QString>

namespace graphite {

QString chemToTex(const QString& ce);        // the inside of \ce{...}
QString physUnitToTex(const QString& pu);    // the inside of \pu{...}
QString expandChemistry(const QString& tex); // replaces every \ce{...} / \pu{...} in a TeX string

}  // namespace graphite
