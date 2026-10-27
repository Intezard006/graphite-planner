// Definitions: a line "Term:: what it means" anywhere in the tree defines Term; wherever Term
// is written afterwards it is underlined with dots and hovering it shows the definition.
//
//   Enthalpy:: heat content at constant pressure, H = U + pV
//   Le Chatelier's principle:: a system at equilibrium shifts to oppose a change
//
// Property names the app itself uses (tags::, description::, id::...) are not definitions.
// Pure functions, no widgets.
#pragma once

#include <QHash>
#include <QList>
#include <QRegularExpression>
#include <QString>

namespace graphite {

struct Definition {
    QString term, text, page;  // page: the title it was found on
};

// "Enthalpy:: heat content" -> "Enthalpy", "heat content". False for other lines.
bool definitionLine(const QString& line, QString* term, QString* text);

struct Glossary {
    QHash<QString, Definition> byKey;  // lower-case term -> its definition (the first one wins)
    QRegularExpression pattern;        // every term, whole words, longest first, plurals too
    bool isEmpty() const { return byKey.isEmpty(); }
    // The definition for matched text ("enthalpies" -> Enthalpy), or nullptr.
    const Definition* find(const QString& matched) const;
};

Glossary makeGlossary(const QList<Definition>& definitions);

}  // namespace graphite
