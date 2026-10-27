// Graphite core: reads and writes Logseq-style markdown (file-based graphs).
//
// Design goal: a file you open and save without touching must come back
// byte-for-byte identical, and editing one block must only change that block.
// Each block therefore remembers the exact lines it was parsed from and only
// regenerates them when its text, depth or hidden metadata actually changed.
//
// No Qt in here on purpose, so it can be tested anywhere.
#pragma once

#include <string>
#include <vector>

namespace graphite {

struct Block {
    int depth = 0;                  // 0 = top level
    std::string text;               // editable content, lines joined by '\n'
    std::vector<std::string> meta;  // hidden lines: collapsed::, id::, :LOGBOOK: drawer

    // Where the block came from, used to write untouched blocks back verbatim.
    bool fresh = true;              // created in the editor, no original form
    bool bullet = true;             // false only for loose lines before the first "- "
    int origDepth = 0;
    std::string origText;
    std::vector<std::string> origMeta;
    std::vector<std::string> raw;

    // Spaces between the bullet's indent and its continuation lines: 2 normally,
    // but 1 in Logseq's own tab-indented output ("\t :LOGBOOK:"). Read from the
    // file and reused when the block is rewritten.
    int contSpaces = 2;

    bool dirty() const;
    bool collapsed() const;
    // Lines as Logseq would write them; `unit` is one level of indentation. `loose` writes
    // the page's bullet-less first block.
    std::vector<std::string> render(const std::string& unit = "\t", bool loose = false) const;
};

class Document {
public:
    static Document parse(const std::string& source);
    std::string serialize() const;

    // True if serialize() differs from what was last loaded or saved.
    bool modified() const;
    // Call after writing the file to disk.
    void markSaved();

    int size() const { return static_cast<int>(blocks_.size()); }
    const Block& at(int i) const { return blocks_[static_cast<size_t>(i)]; }

    // Structure queries.
    int subtreeEnd(int i) const;  // index just past the last descendant of i
    bool hasChildren(int i) const;
    std::vector<int> visible() const;  // indices not hidden by a collapsed ancestor

    // Editing. Indices refer to the flat block list.
    void setText(int i, const std::string& text);
    void setCollapsed(int i, bool collapsed);
    bool indent(int i);    // Tab: become a child of the previous sibling
    bool outdent(int i);   // Shift+Tab: one level up, in place
    int moveUp(int i);     // swap with previous sibling, returns new index or -1
    int moveDown(int i);   // swap with next sibling, returns new index or -1
    int insertBelow(int i, const std::string& text);  // Enter; returns new index
    bool remove(int i);    // only blocks without children
    int ensureNonEmpty();  // guarantees one block exists, returns its index

    // Several blocks at once (selection, clipboard, undo).
    // `roots` with their whole subtrees as Logseq-style markdown ("- a\n\t- b"), depths made
    // relative to the shallowest root. "id::" lines are left out: a pasted copy is a new block.
    std::string exportBlocks(const std::vector<int>& roots) const;
    // Insert the blocks of `src` as siblings after `after`'s subtree (keeping their relative
    // nesting); an empty leaf `after` is replaced. after < 0 inserts at the top. Returns the
    // index of the first inserted block.
    int insertBlocks(int after, const Document& src);
    // Remove blocks from..to (indices) with all their children.
    void removeBlocks(int from, int to);
    // Undo: take the blocks of an earlier copy, but keep what was last saved, so the
    // modified() check still compares against the file on disk.
    void restoreFrom(const Document& snapshot);

private:
    int previousSibling(int i) const;

    std::vector<Block> blocks_;
    bool trailingNewline_ = false;
    std::string unit_ = "\t";  // the file's own indentation style, copied for new blocks
    std::string original_;
};

}  // namespace graphite
