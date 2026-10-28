// Molar masses for the formulas inside \ce{...}, for the hover and the live preview.
//
// Standard atomic weights (IUPAC, abridged; mass number of the longest-lived isotope for
// elements without a stable one), plus D for deuterium. Pure functions, no state.
#pragma once

#include <QList>
#include <QMap>
#include <QString>

namespace graphite {

struct SpeciesMass {
    QString formula;  // as written, without coefficient, charge or state
    double mass = 0;  // g/mol
};

// Mass of one formula such as "CuSO4.5H2O", "[Fe(CN)6]^3-", "2H2O(l)", "Ca(OH)2".
// The coefficient, charge, state symbol and isotope labels do not change the result.
// Returns false for anything that is not a formula ("e-", "->", "heat", unknown elements).
bool formulaMass(const QString& species, double* mass, QString* cleaned = nullptr);

// The atoms of one formula and its charge: "SO4^2-" -> {O: 4, S: 1}, -2; "CuSO4.5H2O" counts
// the water too. The coefficient is ignored, as above. False for what is not a formula.
bool formulaAtoms(const QString& species, QMap<QString, int>* atoms, int* charge = nullptr, QString* cleaned = nullptr);

// Every formula in every \ce{...} of a TeX string, each once, in order.
QList<SpeciesMass> chemMasses(const QString& latex);

// "M: H2SO4 98.08 · NaOH 40.00 g/mol", or empty when there is nothing to weigh.
QString massSummary(const QString& latex);

}  // namespace graphite
