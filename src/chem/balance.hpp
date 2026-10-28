// Balancing chemical equations, for the inside of \ce{...}:
//
//   Fe + O2 -> Fe2O3                       ->  4Fe + 3O2 -> 2Fe2O3
//   MnO4- + Fe^2+ + H+ -> Mn^2+ + Fe^3+ + H2O  ->  MnO4- + 5Fe^2+ + 8H+ -> Mn^2+ + 5Fe^3+ + 4H2O
//
// Atoms of each element and the total charge must match on both sides; the smallest whole
// coefficients come from the null space of that matrix (exact integer elimination). The
// species keep their charges, states and gas/precipitate marks; coefficients are replaced.
// Pure functions, no state.
#pragma once

#include <QString>

namespace graphite {

struct BalanceResult {
    bool ok = false;       // `ce` holds the balanced equation
    bool already = false;  // it was balanced as written
    QString ce;            // "4Fe + 3O2 -> 2Fe2O3"
    QString error;         // why not: "no arrow", "can't read 'Xq'", "O can't balance"...
};

BalanceResult balanceEquation(const QString& ce);

// The first \ce{...} with a reaction arrow in `text`, at or around `pos` when one holds it:
// start and length of its inside. False when there is none.
bool reactionAt(const QString& text, int pos, int* start, int* length);

}  // namespace graphite
