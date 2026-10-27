#include "ui/snippets.hpp"

#include <QRegularExpression>

namespace graphite {

const QList<Snippet>& snippets() {
    static const QList<Snippet> list = {
        // Formatting (also Ctrl+B / Ctrl+I around a selection)
        {";b", "**|**", nullptr, "bold"},
        {";i", "*|*", nullptr, "italics"},
        {";s", "~~|~~", nullptr, "strikethrough"},
        {";hl", "^^|^^", nullptr, "highlight"},
        {";code", "`|`", nullptr, "inline code"},
        {";fence", "```\n|\n```", nullptr, "code block"},
        {";page", "[[|]]", nullptr, "link to a page"},
        {";tag", "#[[|]]", nullptr, "tag (several words)"},
        {";link", "[|](https://)", nullptr, "web link: text, then the address"},
        {";quote", "> |", nullptr, "quotation"},
        // Chemistry
        {"\\ce", "$\\ce{|}$", "\\ce{|}", "chemical formula / equation"},
        {"\\pu", "$\\pu{|}$", "\\pu{|}", "physical unit"},
        {";ce", "$\\ce{|}$", "\\ce{|}", "chemical formula / equation"},
        {";eq", "\xE2\x87\x8C", "<=>", "equilibrium arrow"},
        {";ra", "\xE2\x86\x92", "->", "reaction arrow"},
        {";la", "\xE2\x86\x90", "<-", "reverse arrow"},
        {";dH", "$\\Delta H^\\circ$", "\\Delta H^\\circ", "standard enthalpy change"},
        {";dG", "$\\Delta G^\\circ$", "\\Delta G^\\circ", "standard Gibbs energy change"},
        {";dS", "$\\Delta S^\\circ$", "\\Delta S^\\circ", "standard entropy change"},
        {";Ka", "$K_\\mathrm{a}$", "K_\\mathrm{a}", "acid dissociation constant"},
        {";pKa", "$\\mathrm{p}K_\\mathrm{a}$", "\\mathrm{p}K_\\mathrm{a}", "pKa"},
        {";Keq", "$K_\\mathrm{eq}$", "K_\\mathrm{eq}", "equilibrium constant"},
        {";deg", "\xC2\xB0", "^\\circ", "degree sign"},
        // Maths
        {";frac", "$\\frac{|}{}$", "\\frac{|}{}", "fraction"},
        {";sqrt", "$\\sqrt{|}$", "\\sqrt{|}", "square root"},
        {";mm", "$$|$$", nullptr, "centred display formula"},
        {";pm", "\xC2\xB1", "\\pm", "plus-minus"},
        {";x", "\xC3\x97", "\\times", "times"},
        {";approx", "\xE2\x89\x88", "\\approx", "approximately"},
        {";le", "\xE2\x89\xA4", "\\le", "less or equal"},
        {";ge", "\xE2\x89\xA5", "\\ge", "greater or equal"},
        {";inf", "\xE2\x88\x9E", "\\infty", "infinity"},
        // Greek
        {";alpha", "\xCE\xB1", "\\alpha", "alpha"},
        {";beta", "\xCE\xB2", "\\beta", "beta"},
        {";gamma", "\xCE\xB3", "\\gamma", "gamma"},
        {";delta", "\xCE\xB4", "\\delta", "delta"},
        {";Delta", "\xCE\x94", "\\Delta", "capital delta"},
        {";epsilon", "\xCE\xB5", "\\varepsilon", "epsilon"},
        {";theta", "\xCE\xB8", "\\theta", "theta"},
        {";kappa", "\xCE\xBA", "\\kappa", "kappa"},
        {";lambda", "\xCE\xBB", "\\lambda", "lambda"},
        {";mu", "\xCE\xBC", "\\mu", "mu"},
        {";nu", "\xCE\xBD", "\\nu", "nu"},
        {";pi", "\xCF\x80", "\\pi", "pi"},
        {";rho", "\xCF\x81", "\\rho", "rho"},
        {";sigma", "\xCF\x83", "\\sigma", "sigma"},
        {";tau", "\xCF\x84", "\\tau", "tau"},
        {";phi", "\xCF\x86", "\\phi", "phi"},
        {";chi", "\xCF\x87", "\\chi", "chi"},
        {";psi", "\xCF\x88", "\\psi", "psi"},
        {";omega", "\xCF\x89", "\\omega", "omega"},
        {";Omega", "\xCE\xA9", "\\Omega", "capital omega"},
        // Notes
        {";today", "[[{today}]]", nullptr, "link to today's journal"},
        {";todo", "TODO ", nullptr, "task marker"},
    };
    return list;
}

QString snippetPrefix(const QString& text, int cursor, int* start) {
    int k = cursor;
    while (k > 0 && text[k - 1].isLetter()) --k;
    if (k == 0) return QString();
    const QChar lead = text[k - 1];
    if (lead != ';' && lead != '\\') return QString();
    const int s = k - 1;
    if (lead == ';' && s > 0 && !text[s - 1].isSpace() && text[s - 1] != '(' && text[s - 1] != '$') return QString();
    // "\ce" only as its own command, not inside "\cell" or an existing "\ce{".
    if (cursor < text.size() && (text[cursor].isLetter() || text[cursor] == '{')) return QString();
    if (start) *start = s;
    return text.mid(s, cursor - s);
}

QList<Snippet> matchSnippets(const QString& prefix, bool inMath) {
    QList<Snippet> exact, rest;
    if (prefix.isEmpty()) return exact;
    for (const Snippet& s : snippets()) {
        if (!(inMath ? s.math : s.text)) continue;
        const QString t = QString::fromUtf8(s.trigger);
        if (!t.startsWith(prefix, Qt::CaseSensitive)) continue;
        if (prefix.startsWith('\\') && prefix.size() < 3) continue;  // "\c" is too common to pop up for
        (t == prefix ? exact : rest) << s;
    }
    return exact + rest;
}

bool insideMath(const QString& text, int cursor) {
    static const QRegularExpression math(R"(\$\$.+?\$\$|\$[^$\n]+?\$)");
    for (auto it = math.globalMatch(text); it.hasNext();) {
        const QRegularExpressionMatch m = it.next();
        if (cursor > m.capturedStart() && cursor < m.capturedEnd()) return true;
    }
    return false;
}

QString expandSnippet(const Snippet& s, bool inMath, const QString& today, int* cursorOffset) {
    QString out = QString::fromUtf8(inMath ? s.math : s.text);
    out.replace("{today}", today);
    const int bar = static_cast<int>(out.indexOf('|'));
    if (bar >= 0) out.remove(bar, 1);
    if (cursorOffset) *cursorOffset = bar >= 0 ? bar : static_cast<int>(out.size());
    return out;
}

}  // namespace graphite
