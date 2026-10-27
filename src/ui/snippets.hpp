// Snippets: ";dH" -> ΔH°, ";eq" -> ⇌, "\ce" -> $\ce{|}$ ... Each has one form for plain text
// and one for inside $...$; "|" marks where the cursor goes. A fixed table: no files, no state.
#pragma once

#include <QList>
#include <QString>

namespace graphite {

struct Snippet {
    const char* trigger;  // ";dH", "\\ce"
    const char* text;     // outside maths (may contain $...$); nullptr = not offered there
    const char* math;     // inside $...$; nullptr = not offered there
    const char* what;     // short description for the popup and the key sheet
};

const QList<Snippet>& snippets();

// The trigger being typed just before `cursor` (";d", "\\ce"), and where it starts. Empty if
// the cursor is not right after one: ";" only counts at the start of a word.
QString snippetPrefix(const QString& text, int cursor, int* start);

// Snippets whose trigger starts with `prefix`, for the right context. Exact match first.
QList<Snippet> matchSnippets(const QString& prefix, bool inMath);

// True when `cursor` sits inside $...$ or $$...$$ in `text`.
bool insideMath(const QString& text, int cursor);

// The text to insert and the cursor offset within it ("|" removed). `today` fills ;today.
QString expandSnippet(const Snippet& s, bool inMath, const QString& today, int* cursorOffset);

}  // namespace graphite
