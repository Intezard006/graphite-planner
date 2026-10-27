// Graphs of functions: a line "=graph{...}" draws one or more curves, units and all, and marks
// where they cross zero, turn, meet each other and run off to infinity.
//
//   =graph{x^2 - 2, x from -3 to 3}
//   =graph{sin(x); cos(x), x from 0 to 2*pi}
//   =graph{k = A*exp(-Ea/(R*T)), T from 250 K to 400 K, A = 1e10 s^-1, Ea = 50 kJ/mol}
//   =graph{1/x, x from -2 to 2, y from -10 to 10}
//
// Clauses are separated by commas (curves by ';' too). "v from a to b" is the variable and its
// range (the first one; a second one is the y range); "name = value" that works out without the
// variable is a constant; everything else is a curve, "name = expression" naming it. Without a
// range: x from -10 to 10. The curve is sampled with quick maths (calc), so units, constants,
// M(...), ln/log/exp/sqrt/sin/cos/tan all work. Under a lab table, the table's points are drawn
// too when its columns are named like the variable and the curve.
#pragma once

#include <QImage>
#include <QList>
#include <QPointF>
#include <QString>
#include <QVector>
#include <array>

namespace graphite {

class Theme;

struct GraphCurve {
    QString label;      // "k", or the expression when unnamed
    QString expr;
    QVector<double> y;  // at Graph::xs, in Graph::yUnit; NaN where it is not defined
    QVector<bool> breakAfter;  // no line from sample i to i+1 (an asymptote between them)
};

struct GraphMark {
    enum Kind { Root, Max, Min, Cross };
    Kind kind = Root;
    int curve = 0, other = -1;  // other: the second curve of a crossing
    double x = 0, y = 0;
};

struct Graph {
    bool ok = false;
    QString error;
    QString xName = "x", xUnit, yName = "y", yUnit;
    double xScale = 1, yScale = 1;  // SI size of one xUnit / yUnit
    std::array<int, 6> xDims{}, yDims{};
    double x0 = -10, x1 = 10, y0 = -1, y1 = 1;  // the view
    bool yGiven = false;
    QVector<double> xs;
    QList<GraphCurve> curves;
    QList<GraphMark> marks;  // in x order
    QList<double> poles;     // x of each asymptote
    QList<QPointF> points;   // a lab table's measurements, when under one
};

// "=graph{...}" -> what is inside the braces (the closing brace may still be missing while
// typing); a null string when the line is not a graph.
QString graphSpec(const QString& line);

Graph makeGraph(const QString& spec, int samples = 400);

// The marks as one line of text under the graph: "TARGET 01  x = −1.414 · NADIR (0, −2)" with
// immersion, "Roots x = −1.414, 1.414 · min (0, −2)" without.
QString graphSummary(const Graph& g, bool eva);

QImage drawGraph(const Graph& g, const Theme& th, qreal dpr, int w, int h);

// Tick spacing of 1, 2 or 5 times a power of ten, about `count` ticks over the range.
double niceStep(double range, int count);

}  // namespace graphite
