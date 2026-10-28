#include "notes/glossary.hpp"

#include <QSet>
#include <algorithm>

namespace graphite {

namespace {

const QSet<QString>& knownProperties() {
    static const QSet<QString> p = {"tags", "alias", "title", "description", "id", "collapsed", "type", "template",
                                    "template-including-parent", "public", "icon", "exam", "date", "filters",
                                    "heading", "background-color", "query-table", "query-properties"};
    return p;
}

// The forms a term is found under: itself, and simple plurals ("enthalpies", "bases").
QStringList forms(const QString& key) {
    QStringList f = {key, key + "s", key + "es"};
    if (key.endsWith('y') && key.size() > 2) f << key.chopped(1) + "ies";
    return f;
}

}  // namespace

bool definitionLine(const QString& line, QString* term, QString* text) {
    static const QRegularExpression re(R"(^\s*([^\s:#\[$`>|*][^:\n]{0,58}?)::\s+(\S.*)$)");
    const QRegularExpressionMatch m = re.match(line);
    if (!m.hasMatch()) return false;
    const QString t = m.captured(1).trimmed();
    if (knownProperties().contains(t.toLower()) || t.startsWith("http")) return false;
    *term = t;
    *text = m.captured(2).trimmed();
    return true;
}

const Definition* Glossary::find(const QString& matched) const {
    const QString k = matched.toLower();
    if (auto it = byKey.constFind(k); it != byKey.constEnd()) return &it.value();
    for (const QString& base : {k.chopped(qMin<qsizetype>(1, k.size())), k.chopped(qMin<qsizetype>(2, k.size()))})
        if (auto it = byKey.constFind(base); it != byKey.constEnd()) return &it.value();
    if (k.endsWith("ies"))
        if (auto it = byKey.constFind(k.chopped(3) + "y"); it != byKey.constEnd()) return &it.value();
    return nullptr;
}

Glossary makeGlossary(const QList<Definition>& definitions) {
    Glossary g;
    for (const Definition& d : definitions) {
        const QString k = d.term.toLower();
        if (k.size() < 2 || g.byKey.contains(k)) continue;
        g.byKey.insert(k, d);
    }
    if (g.byKey.isEmpty()) return g;
    QStringList alts;
    for (auto it = g.byKey.cbegin(); it != g.byKey.cend(); ++it)
        for (const QString& f : forms(it.key())) alts << QRegularExpression::escape(f);
    std::sort(alts.begin(), alts.end(), [](const QString& a, const QString& b) { return a.size() > b.size(); });
    g.pattern = QRegularExpression("(?<![\\w])(?:" + alts.join('|') + ")(?![\\w])",
                                   QRegularExpression::CaseInsensitiveOption | QRegularExpression::UseUnicodePropertiesOption);
    g.pattern.optimize();
    return g;
}

}  // namespace graphite
