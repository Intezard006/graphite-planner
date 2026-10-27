// Lab tables: a Markdown table whose columns can be worked out, and plotted.
//
//   | m / g | V / mL | ρ = m/V to g/mL |
//   |-------|--------|-----------------|
//   | 2.50  | 10.0   |                 |      ->  ρ column: 0.2500
//
// A header "name / unit" (or "name (unit)") makes the column a variable; a header with "= ..."
// is worked out for each row with quick maths (calc), using the row's other columns. The
// computed cells are only shown: the file keeps what was typed.
//
//   =plot{A vs c}            the points, a straight-line fit, slope, intercept and R²
//   =plot{ln(k) vs 1/T}      axes can be expressions of the columns too
//   =plot{}                  the first two number columns (the older "= plot A vs c" still reads)
//
// Pure functions, no widgets.
#pragma once

#include <QList>
#include <QPointF>
#include <QString>
#include <QStringList>
#include <optional>

#include "ui/calc.hpp"

namespace graphite {

struct LabColumn {
    QString header;    // as written
    QString name;      // the variable ("m"), empty when the header is not one
    QString unit;      // "g"; for a computed column, the unit its cells are shown in
    QString formula;   // computed: "m/V to g/mL"
    bool computed = false;
    bool numeric = false;  // every filled cell is a number
    QString error;     // computed: why a row could not be worked out (the first one)
};

struct LabTable {
    QList<LabColumn> columns;
    QList<QStringList> cells;  // rows x columns, as shown (computed cells filled in)
    QList<QList<std::optional<CalcQuantity>>> values;
    QString label(int column) const;  // "m (g)", "ρ (g/mL)"
};

// The lines of one Markdown table ("| a | b |", the "|---|" line is skipped). False when they
// are not a table.
bool parseLabTable(const QStringList& lines, LabTable* table);

// "=plot{A vs c}" or "= plot A vs c" -> "A vs c" ("" for "=plot{}"); a null string when the line
// is not a plot.
QString plotSpec(const QString& line);

struct LabPlot {
    bool ok = false;
    QString error;
    QString xLabel, yLabel;   // "c (M)", "ln(k)"
    QString xUnit, yUnit;
    QList<QPointF> points;
    bool fitted = false;      // two or more points with different x
    double slope = 0, intercept = 0, r2 = 0;
    QString summary;          // "slope 0.4521 M⁻¹ · intercept 0.0012 · R² 0.9993"
};

// y vs x from the table: column names or expressions of them; "" = the first two numeric
// columns (x first, as written in tables).
LabPlot labPlot(const LabTable& table, const QString& spec);

}  // namespace graphite
