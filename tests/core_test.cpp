// Tests for the Logseq markdown core. Run with: ctest --output-on-failure
//
// The fixtures copy the *structure* of real journal files (a loose first line,
// tab indentation, a LOGBOOK drawer, no trailing newline) with placeholder text.

#include "core/document.hpp"

#include <cstdio>
#include <random>
#include <string>
#include <vector>

using graphite::Document;

static int failures = 0;

#define CHECK(cond)                                                          \
    do {                                                                     \
        if (!(cond)) {                                                       \
            std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);      \
            ++failures;                                                      \
        }                                                                    \
    } while (0)

#define CHECK_EQ(a, b)                                                       \
    do {                                                                     \
        std::string _a = (a), _b = (b);                                      \
        if (_a != _b) {                                                      \
            std::printf("FAIL %s:%d\n  got:  [%s]\n  want: [%s]\n",          \
                        __FILE__, __LINE__, _a.c_str(), _b.c_str());         \
            ++failures;                                                      \
        }                                                                    \
    } while (0)

static void roundTrip(const char* label, const std::string& text) {
    Document d = Document::parse(text);
    if (d.serialize() != text) {
        std::printf("FAIL round trip [%s]\n  got:  [%s]\n  want: [%s]\n", label,
                    d.serialize().c_str(), text.c_str());
        ++failures;
    }
    if (d.modified()) {
        std::printf("FAIL [%s] reports modified right after parsing\n", label);
        ++failures;
    }
}

static const std::string kJournalA =
    "## Loose heading\n"
    "\t- first child with some text\n"
    "- ## Section\n"
    "\t- note one\n"
    "\t- note two :tag:";  // no trailing newline, like Logseq writes

static const std::string kJournalB =
    "## Another loose heading\n"
    "\t- question one?\n"
    "\t :LOGBOOK:\n"
    "\t CLOCK: [2026-10-17 Sat 20:19:16]--[2026-10-17 Sat 20:19:17] =>  00:00:01\n"
    "\t CLOCK: [2026-10-17 Sat 20:19:19]--[2026-10-17 Sat 20:19:23] =>  00:00:04\n"
    "\t :END:\n"
    "\t- question two?\n"
    "\t- question three?\n"
    "- ## Last section";

static void testRoundTrips() {
    roundTrip("A", kJournalA);
    roundTrip("B", kJournalB);
    roundTrip("empty file", "");
    roundTrip("bare dash", "-");
    roundTrip("bare dash newline", "-\n");
    roundTrip("only newline", "\n");
    roundTrip("trailing newline", "- a\n\t- b\n- c\n");
    roundTrip("trailing space bullet", "- \n- x\n");
    roundTrip("blank lines between", "- a\n\n- b\n\n\n- c\n");
    roundTrip("two space indent", "- a\n  - b\n    - c\n  - d\n- e");
    roundTrip("mixed indent", "- a\n\t- b\n  - c\n\t\t- d\n    - e");
    roundTrip("fence with dashes", "- code\n  ```\n  - not a block\n  ```\n- next");
    roundTrip("rule lines", "- a\n---\n- b");
    roundTrip("crlf", "- a\r\n- b\r\n");
    roundTrip("logbook two-space variant", "- t\n  :LOGBOOK:\n  CLOCK: x\n  :END:\n- u");
}

static void testStructure() {
    Document d = Document::parse(kJournalA);
    CHECK(d.size() == 5);
    const int want[] = {0, 1, 0, 1, 1};
    for (int i = 0; i < 5; ++i) CHECK(d.at(i).depth == want[i]);
    CHECK(!d.at(0).bullet);
    CHECK_EQ(d.at(0).text, "## Loose heading");
    CHECK_EQ(d.at(1).text, "first child with some text");
    CHECK_EQ(d.at(2).text, "## Section");
    CHECK_EQ(d.at(4).text, "note two :tag:");

    Document b = Document::parse(kJournalB);
    CHECK(b.size() == 5);
    CHECK_EQ(b.at(1).text, "question one?");
    CHECK(b.at(1).meta.size() == 4);  // the whole LOGBOOK drawer is hidden
    CHECK_EQ(b.at(1).meta.front(), ":LOGBOOK:");
    CHECK_EQ(b.at(1).meta.back(), ":END:");

    Document e = Document::parse("");
    CHECK(e.size() == 0);
    CHECK(e.ensureNonEmpty() == 0 && e.size() == 1);
}

static void testEditTouchesOnlyThatBlock() {
    Document d = Document::parse(kJournalB);
    d.setText(1, "question one, reworded?");
    std::string out = d.serialize();
    CHECK(d.modified());
    // Only the words changed: the LOGBOOK drawer keeps its exact original indentation
    // (a tab and ONE space, as Logseq writes it), and so does everything else.
    std::string expected = kJournalB;
    expected.replace(expected.find("question one?"), 13, "question one, reworded?");
    CHECK_EQ(out, expected);
    CHECK(out.back() == 'n');  // still no trailing newline

    // Changing the text back makes the block clean again.
    d.setText(1, "question one?");
    CHECK_EQ(d.serialize(), kJournalB);
    CHECK(!d.modified());
}

static void testCollapse() {
    const std::string src = "- parent\n\t- child\n- other";
    Document d = Document::parse(src);
    d.setCollapsed(0, true);
    CHECK(d.at(0).collapsed());
    CHECK_EQ(d.serialize(), "- parent\n  collapsed:: true\n\t- child\n- other");

    Document re = Document::parse(d.serialize());
    CHECK(re.at(0).collapsed());
    CHECK(re.visible() == (std::vector<int>{0, 2}));
    CHECK_EQ(re.at(0).text, "parent");

    d.setCollapsed(0, false);
    CHECK_EQ(d.serialize(), src);
    CHECK(!d.modified());
}

static void testOutliner() {
    const std::string src = "- a\n\t- a1\n\t- a2\n- b\n- c\n\t- c1";

    {  // indent moves a block under its previous sibling
        Document d = Document::parse(src);
        CHECK(d.indent(3));
        CHECK_EQ(d.serialize(), "- a\n\t- a1\n\t- a2\n\t- b\n- c\n\t- c1");
        CHECK(!d.indent(0));
        CHECK(!d.indent(1));  // first child has no previous sibling
    }
    {  // outdent works in place; later siblings become children (Logseq default)
        Document d = Document::parse(src);
        CHECK(d.outdent(1));
        CHECK_EQ(d.serialize(), "- a\n- a1\n\t- a2\n- b\n- c\n\t- c1");
        CHECK(!d.outdent(0));
    }
    {  // indenting under a collapsed block expands it
        Document d = Document::parse("- a\n  collapsed:: true\n\t- a1\n- b");
        CHECK(d.at(0).collapsed());
        CHECK(d.indent(2));
        CHECK(!d.at(0).collapsed());
    }
    {  // moving carries the whole subtree
        Document d = Document::parse(src);
        CHECK(d.moveDown(0) == 1);
        CHECK_EQ(d.serialize(), "- b\n- a\n\t- a1\n\t- a2\n- c\n\t- c1");
        CHECK(d.moveUp(4) == 1);  // c swaps with the whole "a" subtree above it
        CHECK_EQ(d.serialize(), "- b\n- c\n\t- c1\n- a\n\t- a1\n\t- a2");
    }
    {
        Document d = Document::parse(src);
        CHECK(d.moveUp(4) == 3);
        CHECK_EQ(d.serialize(), "- a\n\t- a1\n\t- a2\n- c\n\t- c1\n- b");
        CHECK(d.moveUp(0) == -1);
        CHECK(d.moveDown(5) == -1);
    }
    {  // Enter on a parent with visible children makes the new block its first child
        Document d = Document::parse(src);
        int n = d.insertBelow(0, "x");
        CHECK(n == 1 && d.at(1).depth == 1);
        CHECK_EQ(d.serialize(), "- a\n\t- x\n\t- a1\n\t- a2\n- b\n- c\n\t- c1");
    }
    {  // ... but a collapsed parent gets a sibling after its subtree
        Document d = Document::parse(src);
        d.setCollapsed(0, true);
        int n = d.insertBelow(0, "x");
        CHECK(n == 3 && d.at(3).depth == 0);
    }
    {
        Document d = Document::parse(src);
        CHECK(!d.remove(0));  // has children
        CHECK(d.remove(3));
        CHECK_EQ(d.serialize(), "- a\n\t- a1\n\t- a2\n- c\n\t- c1");
    }
    {  // multi-line blocks render with continuation indent and survive a re-parse
        Document d = Document::parse("- a");
        d.setText(0, "line one\nline two");
        d.indent(0);  // no-op, nothing before it
        int n = d.insertBelow(0, "child");
        d.indent(n);
        Document re = Document::parse(d.serialize());
        CHECK(re.size() == 2);
        CHECK_EQ(re.at(0).text, "line one\nline two");
    }
}

static void testContinuationIndentSurvivesEditsAndSaves() {
    Document d = Document::parse(kJournalB);
    d.setText(1, "first edit");
    d.markSaved();
    d.setText(1, "second edit");
    std::string expected = kJournalB;
    expected.replace(expected.find("question one?"), 13, "second edit");
    CHECK_EQ(d.serialize(), expected);

    // Moving the block changes its depth, so it falls back to the standard layout.
    Document m = Document::parse(kJournalB);
    CHECK(m.indent(1) == false);  // first child of a loose block has no previous sibling
    Document n = Document::parse(kJournalB);
    CHECK(n.indent(2));
    Document re = Document::parse(n.serialize());
    CHECK(re.at(2).depth == 2);
    CHECK_EQ(re.at(2).text, "question two?");
}

static void testHiddenFirstLineProperty() {
    // A journal whose first block is only "id:: ..." (written without a bullet, with
    // children under it) is an EMPTY block, not a block whose text is the id.
    const std::string src =
        "id:: 6ab0411c-a955-47a7-b34c-7f4aa5711473\n"
        "\t- Currently, I need to do the first thing\n"
        "\t- The second thing\n"
        "- ## Equilibrium\n"
        "\t- I feel decently confident";
    Document d = Document::parse(src);
    CHECK(d.size() == 5);
    CHECK(!d.at(0).bullet);
    CHECK_EQ(d.at(0).text, "");
    CHECK(d.at(0).meta.size() == 1);
    CHECK_EQ(d.at(0).meta[0], "id:: 6ab0411c-a955-47a7-b34c-7f4aa5711473");
    CHECK(d.at(1).depth == 1 && d.at(3).depth == 0 && d.at(4).depth == 1);
    CHECK_EQ(d.serialize(), src);

    // Editing a child leaves the id block untouched.
    d.setText(2, "The second thing, reworded");
    std::string expected = src;
    expected.replace(expected.find("The second thing"), 16, "The second thing, reworded");
    CHECK_EQ(d.serialize(), expected);

    // Typing into the empty block keeps it bullet-less and keeps the id attached.
    Document e = Document::parse(src);
    e.setText(0, "Heading");
    CHECK_EQ(e.serialize().substr(0, 48), "Heading\nid:: 6ab0411c-a955-47a7-b34c-7f4aa571147");
    Document re = Document::parse(e.serialize());
    CHECK_EQ(re.at(0).text, "Heading");
    CHECK(re.at(0).meta.size() == 1);

    // Collapsing it writes the flag next to the id and survives a re-read.
    Document f = Document::parse(src);
    f.setCollapsed(0, true);
    Document rf = Document::parse(f.serialize());
    CHECK(rf.at(0).collapsed());
    CHECK(rf.at(0).meta.size() == 2);

    // The same property as the first line of a bullet block.
    Document g = Document::parse("- id:: abc\n  real text\n- next");
    CHECK_EQ(g.at(0).text, "real text");
    CHECK(g.at(0).meta.size() == 1);
    CHECK_EQ(g.serialize(), "- id:: abc\n  real text\n- next");

    // The old behaviour for a plain loose heading is now: it stays a plain loose heading.
    Document h = Document::parse("## Heading\n\t- child");
    h.setText(0, "## Heading edited");
    CHECK_EQ(h.serialize(), "## Heading edited\n\t- child");
    h.markSaved();
    h.setText(0, "## Heading edited again");
    CHECK_EQ(h.serialize(), "## Heading edited again\n\t- child");

    // Moved off the first position it becomes an ordinary bullet.
    Document m = Document::parse("## Heading\n- other");
    CHECK(m.moveDown(0) == 1);
    CHECK_EQ(m.serialize(), "- other\n- ## Heading");
}

static void testSaveCycle() {
    Document d = Document::parse(kJournalA);
    d.setText(1, "changed");
    CHECK(d.modified());
    std::string saved = d.serialize();
    d.markSaved();
    CHECK(!d.modified());
    CHECK_EQ(d.serialize(), saved);
    d.setText(1, "changed again");
    CHECK(d.modified());
}

// Random files built from the line types real graphs contain.
static std::string randomSource(std::mt19937& rng) {
    static const char* indents[] = {"", "\t", "\t\t", "  ", "    ", " \t", "\t "};
    static const char* words[] = {"alpha", "beta", "gamma", "$x^2$", "[[Page]]", "#tag", "key:: value", "##"};
    auto pick = [&](size_t n) { return static_cast<size_t>(rng() % n); };

    std::string out;
    int lines = 1 + static_cast<int>(rng() % 25);
    bool fence = false;
    if (rng() % 4 == 0) out += (rng() % 2) ? "id:: abc\n" : "## Loose heading\n";
    for (int i = 0; i < lines; ++i) {
        if (i > 0) out += '\n';
        int kind = static_cast<int>(rng() % 10);
        if (fence) {
            out += (kind < 3) ? "  ```" : std::string("  - ") + words[pick(8)];
            if (kind < 3) fence = false;
        } else if (kind < 5) {
            out += std::string(indents[pick(7)]) + "- " + words[pick(8)];
        } else if (kind == 5) {
            out += std::string(indents[pick(7)]) + "-";
        } else if (kind == 6) {
            out += std::string(indents[pick(7)]) + "  " + words[pick(8)];
        } else if (kind == 7) {
            out += "";
        } else if (kind == 8) {
            out += "  :LOGBOOK:\n  CLOCK: x\n  :END:";
        } else {
            out += "  ```";
            fence = true;
        }
    }
    if (fence) out += "\n  ```";  // an unterminated fence would swallow the rest of the file
    if (rng() % 2) out += '\n';
    return out;
}

static void testFuzzRoundTrip() {
    std::mt19937 rng(12345);
    for (int n = 0; n < 2000; ++n) {
        std::string src = randomSource(rng);
        Document d = Document::parse(src);
        if (d.serialize() != src) {
            std::printf("FAIL fuzz round trip\n  src: [%s]\n  got: [%s]\n", src.c_str(), d.serialize().c_str());
            ++failures;
            return;
        }
    }
}

static bool wellFormed(const Document& d) {
    if (d.size() == 0) return true;
    if (d.at(0).depth != 0) return false;
    for (int i = 1; i < d.size(); ++i)
        if (d.at(i).depth > d.at(i - 1).depth + 1 || d.at(i).depth < 0) return false;
    return true;
}

static bool sameStructure(const Document& a, const Document& b) {
    if (a.size() != b.size()) {
        std::printf("  block count: edited %d, re-parsed %d\n", a.size(), b.size());
        return false;
    }
    for (int i = 0; i < a.size(); ++i) {
        if (a.at(i).depth != b.at(i).depth || a.at(i).text != b.at(i).text || a.at(i).meta != b.at(i).meta) {
            std::printf("  block %d differs: edited depth=%d text=[%s] meta=%zu | re-parsed depth=%d text=[%s] meta=%zu\n",
                        i, a.at(i).depth, a.at(i).text.c_str(), a.at(i).meta.size(),
                        b.at(i).depth, b.at(i).text.c_str(), b.at(i).meta.size());
            return false;
        }
    }
    return true;
}

static void testFuzzEditing() {
    std::mt19937 rng(777);
    static const char* texts[] = {"alpha", "beta gamma", "line one\nline two", "", "x\n", "\nstart blank", "$E=mc^2$"};
    for (int n = 0; n < 400; ++n) {
        Document d = Document::parse(randomSource(rng));
        d.ensureNonEmpty();
        for (int step = 0; step < 40; ++step) {
            int i = static_cast<int>(rng() % static_cast<unsigned>(d.size()));
            switch (rng() % 8) {
                case 0: d.setText(i, texts[rng() % 7]); break;
                case 1: d.indent(i); break;
                case 2: d.outdent(i); break;
                case 3: d.moveUp(i); break;
                case 4: d.moveDown(i); break;
                case 5: d.insertBelow(i, texts[rng() % 7]); break;
                case 6: if (d.size() > 1) d.remove(i); break;
                default: d.setCollapsed(i, rng() % 2); break;
            }
            if (!wellFormed(d)) {
                std::printf("FAIL fuzz editing: depth invariant broken (doc %d, step %d)\n", n, step);
                ++failures;
                return;
            }
        }
        // What we write must read back as the same outline.
        Document re = Document::parse(d.serialize());
        if (!sameStructure(d, re)) {
            std::printf("FAIL fuzz editing: re-parse differs (doc %d)\n  out: [%s]\n", n, d.serialize().c_str());
            ++failures;
            return;
        }
    }
}

static void testMultiBlock() {
    const std::string src = "- a\n\t- a1\n\t  id:: 64f0c1e2-0000-4000-8000-000000000001\n\t\t- a11\n- b\n- c";
    Document d = Document::parse(src);
    CHECK(d.size() == 5);
    // Export: whole subtrees, depths relative to the shallowest, no id:: lines.
    CHECK(d.exportBlocks({1}) == "- a1\n\t- a11");
    CHECK(d.exportBlocks({0, 1, 3}) == "- a\n\t- a1\n\t\t- a11\n- b");  // a1 is inside a: once
    // Paste after a subtree keeps the nesting, as siblings of the block.
    Document clip = Document::parse("- x\n\t- y");
    const int at = d.insertBlocks(0, clip);
    CHECK(at == 3);
    CHECK(d.at(3).text == "x" && d.at(3).depth == 0 && d.at(4).text == "y" && d.at(4).depth == 1);
    CHECK(d.at(5).text == "b");
    // Pasting into an empty leaf replaces it.
    Document e = Document::parse("- one\n- ");
    e.insertBlocks(1, Document::parse("- p\n- q"));
    CHECK(e.size() == 3 && e.at(1).text == "p" && e.at(2).text == "q");
    // Never deeper than one level below the block before.
    Document f = Document::parse("- top");
    f.insertBlocks(0, Document::parse("\t\t- deep"));
    CHECK(f.at(1).depth == 0);
    // Remove a range with its children.
    Document g = Document::parse(src);
    g.removeBlocks(0, 0);  // "a" and everything under it
    CHECK(g.size() == 2 && g.at(0).text == "b");
    g.removeBlocks(0, 1);
    CHECK(g.size() == 1 && g.at(0).text.empty());  // never left without a block
    // Undo keeps what is on disk: after a save, restoring an older copy is a real change.
    Document h = Document::parse("- one");
    const Document before = h;
    h.setText(0, "two");
    h.markSaved();
    h.restoreFrom(before);
    CHECK(h.at(0).text == "one" && h.modified());
    CHECK(h.serialize() == "- one");
}

int main() {
    testMultiBlock();
    testRoundTrips();
    testStructure();
    testEditTouchesOnlyThatBlock();
    testCollapse();
    testOutliner();
    testSaveCycle();
    testContinuationIndentSurvivesEditsAndSaves();
    testHiddenFirstLineProperty();
    testFuzzRoundTrip();
    testFuzzEditing();
    if (failures == 0) std::printf("all core tests passed\n");
    return failures == 0 ? 0 : 1;
}
