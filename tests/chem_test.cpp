// Chemistry conversion (exact strings) and a real MicroTeX render.
#include <QGuiApplication>
#include <QPainter>
#include <cstdio>

#include "chem/chem.hpp"
#include "chem/molar.hpp"
#include "ui/snippets.hpp"
#include "render/tex.hpp"

using namespace graphite;
static int failures = 0;
static void eq(const QString& got, const QString& want, const char* what) {
    if (got != want) {
        ++failures;
        std::fprintf(stderr, "FAIL %s\n  got:  %s\n  want: %s\n", what, qPrintable(got), qPrintable(want));
    }
}

int main(int argc, char** argv) {
    QGuiApplication app(argc, argv);
    eq(chemToTex("H2O"), "\\mathrm{H_{2}O}", "water");
    eq(chemToTex("SO4^2-"), "\\mathrm{SO_{4}^{2-}}", "sulfate");
    eq(chemToTex("Na+"), "\\mathrm{Na^{+}}", "sodium ion");
    eq(chemToTex("OH-"), "\\mathrm{OH^{-}}", "hydroxide");
    eq(chemToTex("Ca(OH)2"), "\\mathrm{Ca(OH)_{2}}", "group");
    eq(chemToTex("[Fe(CN)6]^3-"), "\\mathrm{[Fe(CN)_{6}]^{3-}}", "complex");
    eq(chemToTex("CuSO4.5H2O"), "\\mathrm{CuSO_{4}\\cdot 5H_{2}O}", "hydrate");
    eq(chemToTex("1/2 O2"), "\\frac{1}{2}\\,\\mathrm{O_{2}}", "fraction");
    eq(chemToTex("N2 + 3H2 <=> 2NH3"),
       "\\mathrm{N_{2}} + \\mathrm{3}\\,\\mathrm{H_{2}} \\rightleftharpoons \\mathrm{2}\\,\\mathrm{NH_{3}}", "haber");
    eq(chemToTex("A ->[H2O][heat] B"), "\\mathrm{A} \\xrightarrow[\\mathrm{heat}]{\\mathrm{H_{2}O}} \\mathrm{B}", "arrow labels");
    eq(chemToTex("CO2 ^"), "\\mathrm{CO_{2}}\\uparrow", "gas");
    eq(chemToTex("^14C"), "\\mathrm{{}^{14}C}", "isotope");
    eq(physUnitToTex("25 kJ.mol-1"), "25\\,\\mathrm{kJ\\,mol^{-1}}", "pu");
    eq(expandChemistry("x + \\ce{H2O}"), "x + \\mathrm{H_{2}O}", "expand");

    // Molar masses, checked against values worked out by hand from the same atomic weights.
    auto mass = [](const char* f) {
        double m = -1;
        return formulaMass(f, &m) ? QString::number(m, 'f', 2) : QString("none");
    };
    eq(mass("H2O"), "18.02", "M water");
    eq(mass("H2SO4"), "98.07", "M sulfuric acid");
    eq(mass("CuSO4.5H2O"), "249.68", "M hydrate");
    eq(mass("CuSO4*5H2O"), "249.68", "M hydrate, star");
    eq(mass("Ca(OH)2"), "74.09", "M group");
    eq(mass("K3[Fe(CN)6]"), "329.25", "M complex");
    eq(mass("[Fe(CN)6]^3-"), "211.95", "M ion: charge ignored");
    eq(mass("2H2O(l)"), "18.02", "M coefficient and state ignored");
    eq(mass("SO4^2-(aq)"), "96.06", "M charged, aqueous");
    eq(mass("Na+"), "22.99", "M cation");
    eq(mass("D2O"), "20.03", "M heavy water");
    eq(mass("^{14}_{6}C"), "12.01", "M isotope label ignored");
    eq(mass("C6H12O6"), "180.16", "M glucose");
    eq(mass("e-"), "none", "electron");
    eq(mass("->"), "none", "arrow");
    eq(mass("heat"), "none", "word");
    eq(mass("Xy2"), "none", "unknown element");
    eq(mass("Fe(CN"), "none", "unclosed group");
    eq(massSummary("\\ce{H2SO4 + 2NaOH -> Na2SO4 + 2H2O}"),
       "M: H2SO4 98.07  \xC2\xB7  NaOH 40.00  \xC2\xB7  Na2SO4 142.04  \xC2\xB7  H2O 18.02 g/mol", "summary");
    eq(massSummary("E = mc^2"), "", "no chemistry");

    // Snippets: where a trigger starts, what matches, what gets inserted.
    int at = -1;
    eq(snippetPrefix("heat ;d", 7, &at), ";d", "prefix");
    eq(QString::number(at), "5", "prefix start");
    eq(snippetPrefix("x;d", 3, nullptr), "", "; inside a word is not a trigger");
    eq(snippetPrefix("see \\ce", 7, nullptr), "\\ce", "backslash trigger");
    eq(snippetPrefix("\\ce{H2O}", 3, nullptr), "", "existing \\ce{ is left alone");
    eq(matchSnippets(";dH", false).value(0).trigger, ";dH", "exact first");
    eq(QString::number(matchSnippets(";d", false).size() >= 3), "1", "several ;d...");
    eq(QString::number(matchSnippets(";mm", true).size()), "0", "display maths not offered inside maths");
    eq(QString::number(insideMath("a $x+y$ b", 4)), "1", "inside maths");
    eq(QString::number(insideMath("a $x+y$ b", 8)), "0", "after maths");
    int off = -1;
    const Snippet* ce = nullptr;
    for (const Snippet& s : snippets()) if (QString(s.trigger) == "\\ce") ce = &s;
    eq(ce ? expandSnippet(*ce, false, QString(), &off) : QString(), "$\\ce{}$", "\\ce outside maths");
    eq(QString::number(off), "5", "cursor inside the braces");
    eq(ce ? expandSnippet(*ce, true, QString(), &off) : QString(), "\\ce{}", "\\ce inside maths");

    if (!Tex::init()) {
        std::fprintf(stderr, "FAIL TeX resources not found\n");
        return 1;
    }
    const char* formulas[] = {"E = mc^2", "\\frac{a}{b} + \\sqrt{x^2+1}", "\\ce{2H2 + O2 -> 2H2O}",
                              "\\ce{SO4^2-}", "\\ce{A ->[H2O][heat] B}", "\\sum_{i=1}^{n} x_i",
                              "\\ce{CuSO4.5H2O}", "\\pu{25 kJ.mol-1}"};
    for (const char* f : formulas) {
        const TexImage t = Tex::render(f, 16, Qt::white, false, 2.0);
        std::printf("%-32s ok=%d  %dx%d  ascent=%.1f depth=%.1f %s\n", f, t.ok, t.image.width(), t.image.height(),
                    t.ascent, t.depth, qPrintable(t.error));
        if (!t.ok) ++failures;
        if (t.ok && qstrcmp(f, "E = mc^2") == 0) {
            if (t.ascent < 8 || t.ascent > 20) { ++failures; std::fprintf(stderr, "FAIL size looks wrong\n"); }
            QImage bg(t.image.size(), QImage::Format_ARGB32);
            bg.fill(QColor("#101018"));
            QPainter(&bg).drawImage(0, 0, t.image);
            bg.save(qEnvironmentVariable("GRAPHITE_SHOTS", ".") + "/tex_probe.png");
        }
    }
    {   // contact sheet for eyeballing
        const char* sheet[] = {"E = mc^2", "\\int_0^\\infty e^{-x^2}\\,dx = \\frac{\\sqrt{\\pi}}{2}",
            "\\ce{2H2 + O2 -> 2H2O}", "\\ce{SO4^2-}", "\\ce{CH3COOH <=> CH3COO- + H+}", "\\ce{A ->[H2O][heat] B}",
            "\\ce{[Fe(CN)6]^3-}", "\\ce{CuSO4.5H2O}", "\\ce{MnO4- + 8H+ + 5e- -> Mn^2+ + 4H2O}",
            "\\ce{^{227}_{90}Th}", "\\pu{25 kJ.mol-1}", "\\ce{CO2 ^} \\quad \\ce{AgCl v}", "\\Delta G = \\Delta H - T\\Delta S",
            "\\hat{H}\\psi = E\\psi", "\\ce{C6H5-CH3}"};
        QImage canvas(900, 40 * 15 + 20, QImage::Format_ARGB32);
        canvas.fill(QColor("#0c0a12"));
        QPainter p(&canvas);
        int y = 10;
        for (const char* f : sheet) {
            const TexImage t = Tex::render(f, 20, QColor("#e8e4f2"), false, 1.0);
            if (t.ok) p.drawImage(QPointF(20, y + 30 - t.ascent), t.image);
            else p.setPen(Qt::red), p.drawText(20, y + 25, QString("ERR ") + f + " " + t.error);
            y += 40;
        }
        p.end();
        canvas.save(qEnvironmentVariable("GRAPHITE_SHOTS", ".") + "/tex_sheet.png");
    }
    const TexImage bad = Tex::render("\\frac{a", 16, Qt::white, false, 1.0);
    std::printf("bad input: ok=%d (%s)\n", bad.ok, qPrintable(bad.error));
    std::puts(failures ? "chem_test FAILED" : "chem_test passed");
    return failures ? 1 : 0;
}
