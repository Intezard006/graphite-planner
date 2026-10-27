// Oxidation states, for the formula card and the hover:
//
//   KMnO4        ->  K +1 · Mn +7 · O −2
//   MnO4- + Fe^2+ + H+ -> Mn^2+ + Fe^3+ + H2O
//                ->  Mn +7 → +2 (reduced) · Fe +2 → +3 (oxidised)
//                    reduction  MnO4- + 8H+ + 5e- -> Mn^2+ + 4H2O
//                    oxidation  Fe^2+ -> Fe^3+ + e-
//
// The usual rules, in order: F −1; group 1 +1; group 2 +2; Al +3, Zn +2; H +1; O −2; halogens
// −1; then S/Se/Te −2, N/P/As −3; the last element left takes what the charge needs (so H in
// NaH is −1 and O in H2O2 −1). An average can be a fraction (Fe in Fe3O4: +8/3). Elements the
// rules can't separate are left out. Pure functions.
#pragma once

#include <QList>
#include <QString>

namespace graphite {

struct OxState {
    QString element;
    int num = 0, den = 1;  // +8/3 -> 8, 3
    QString text() const;  // "+7", "−2", "0", "+8/3"
};

// The states in one species ("SO4^2-"), in the order the elements first appear. Empty when
// it is not a formula, or no element can be worked out.
QList<OxState> oxidationStates(const QString& species);

// "K +1 · Mn +7 · O −2"
QString oxidationLine(const QString& species);

struct RedoxReport {
    QStringList changes;          // "Mn +7 → +2 (reduced)"
    QString reduction, oxidation;  // half equations when they balance (acidic: H+, H2O, e-)
};

// What changes state in a reaction (the inside of \ce{...}); empty when nothing does.
RedoxReport redoxReport(const QString& ce);

}  // namespace graphite
