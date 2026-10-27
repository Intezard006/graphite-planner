#include "core/document.hpp"

#include <algorithm>

namespace graphite {

namespace {

const char* const kCollapsedLine = "collapsed:: true";

std::string ltrim(const std::string& s) {
    size_t p = 0;
    while (p < s.size() && (s[p] == ' ' || s[p] == '\t')) ++p;
    return s.substr(p);
}

std::string trim(const std::string& s) {
    std::string t = ltrim(s);
    while (!t.empty() && (t.back() == ' ' || t.back() == '\t' || t.back() == '\r')) t.pop_back();
    return t;
}

bool startsWith(const std::string& s, const char* prefix) {
    return s.rfind(prefix, 0) == 0;
}

std::vector<std::string> splitLines(const std::string& s, bool& trailingNewline) {
    std::vector<std::string> lines;
    trailingNewline = !s.empty() && s.back() == '\n';
    size_t start = 0;
    while (start <= s.size()) {
        size_t nl = s.find('\n', start);
        if (nl == std::string::npos) {
            lines.push_back(s.substr(start));
            break;
        }
        lines.push_back(s.substr(start, nl - start));
        start = nl + 1;
    }
    // "a\nb\n" splits into a, b and an empty tail that is not a real line.
    if (trailingNewline) lines.pop_back();
    if (s.empty()) lines.clear();
    return lines;
}

std::string join(const std::vector<std::string>& lines, size_t from = 0) {
    std::string out;
    for (size_t i = from; i < lines.size(); ++i) {
        if (i > from) out += '\n';
        out += lines[i];
    }
    return out;
}

std::vector<std::string> splitText(const std::string& text) {
    std::vector<std::string> lines;
    size_t start = 0;
    while (true) {
        size_t nl = text.find('\n', start);
        if (nl == std::string::npos) {
            lines.push_back(text.substr(start));
            break;
        }
        lines.push_back(text.substr(start, nl - start));
        start = nl + 1;
    }
    return lines;
}

// "- text", "-" (empty block). Rejects "--", "-x" and "---".
bool matchBullet(const std::string& line, std::string& indent, std::string& rest) {
    size_t p = 0;
    while (p < line.size() && (line[p] == ' ' || line[p] == '\t')) ++p;
    if (p >= line.size() || line[p] != '-') return false;
    if (p + 1 < line.size() && line[p + 1] != ' ') return false;
    indent = line.substr(0, p);
    rest = (p + 1 < line.size()) ? line.substr(p + 2) : std::string();
    return true;
}

int widthOf(const std::string& indent) {
    int w = 0;
    for (char c : indent) w += (c == '\t') ? 4 : 1;
    return w;
}

void updateFence(const std::string& content, bool& inFence) {
    std::string t = ltrim(content);
    if (startsWith(t, "```") && t.find("```", 3) == std::string::npos) inFence = !inFence;
}

std::string stripContinuation(const std::string& line, const std::string& indent, int spaces) {
    std::string s = line;
    if (!indent.empty() && startsWith(s, indent.c_str())) s = s.substr(indent.size());
    size_t n = 0;
    while (static_cast<int>(n) < spaces && n < s.size() && s[n] == ' ') ++n;
    return s.substr(n);
}

// Spaces after the bullet's indent on the first non-blank continuation line (capped at 2).
int detectContSpaces(const std::vector<std::string>& raw, const std::string& indent) {
    for (size_t i = 1; i < raw.size(); ++i) {
        if (trim(raw[i]).empty()) continue;
        std::string s = raw[i];
        if (!indent.empty() && startsWith(s, indent.c_str())) s = s.substr(indent.size());
        int n = 0;
        while (n < 2 && n < static_cast<int>(s.size()) && s[static_cast<size_t>(n)] == ' ') ++n;
        return n;
    }
    return 2;
}

struct Pending {
    Block block;
    std::string indent;
    std::vector<std::string> content;  // first line, then continuation lines (prefix stripped)
};

Block finalize(Pending& p) {
    Block& b = p.block;
    if (b.bullet) {
        b.contSpaces = detectContSpaces(b.raw, p.indent);
        for (size_t i = 1; i < p.content.size(); ++i)
            p.content[i] = stripContinuation(p.content[i], p.indent, b.contSpaces);
    }
    std::vector<std::string> kept;
    if (!p.content.empty()) {
        // A block that opens with a hidden property ("id:: ..." on the bullet line, or as
        // the page's first line) is an empty block as far as the user is concerned.
        const std::string first = trim(p.content[0]);
        if (first == kCollapsedLine || startsWith(first, "id:: ")) b.meta.push_back(first);
        else kept.push_back(p.content[0]);
    }

    // Hidden lines (never shown in the editor, always written back): the
    // collapsed flag, block ids and the LOGBOOK clock drawer.
    const std::vector<std::string>& c = p.content;
    for (size_t i = 1; i < c.size(); ++i) {
        std::string t = trim(c[i]);
        if (t == ":LOGBOOK:") {
            size_t end = i + 1;
            while (end < c.size() && trim(c[end]) != ":END:") ++end;
            if (end < c.size()) {
                for (size_t k = i; k <= end; ++k) b.meta.push_back(ltrim(c[k]));
                i = end;
                continue;
            }
        }
        if (t == kCollapsedLine || startsWith(t, "id:: ")) {
            b.meta.push_back(t);
            continue;
        }
        kept.push_back(c[i]);
    }

    b.text = join(kept);
    b.origText = b.text;
    b.origMeta = b.meta;
    b.origDepth = b.depth;
    b.fresh = false;
    return b;
}

}  // namespace

bool Block::dirty() const {
    return fresh || depth != origDepth || text != origText || meta != origMeta;
}

bool Block::collapsed() const {
    return std::find(meta.begin(), meta.end(), std::string(kCollapsedLine)) != meta.end();
}

std::vector<std::string> Block::render(const std::string& unit, bool loose) const {
    std::vector<std::string> textLines = splitText(text);
    std::vector<std::string> out;

    if (loose) {
        // The page's first block written without a bullet: a bare heading, page properties,
        // or a property-only block such as "id:: ...". It stays that way when edited.
        if (!(text.empty() && !meta.empty())) out.insert(out.end(), textLines.begin(), textLines.end());
        for (const std::string& m : meta) out.push_back(m);
        if (out.empty()) out.push_back(std::string());
        return out;
    }

    std::string indent;
    for (int i = 0; i < depth; ++i) indent += unit;
    const std::string cont = indent + std::string(static_cast<size_t>(contSpaces), ' ');

    out.push_back(textLines[0].empty() ? indent + "-" : indent + "- " + textLines[0]);
    for (const std::string& m : meta) out.push_back(cont + m);
    for (size_t i = 1; i < textLines.size(); ++i) {
        out.push_back(textLines[i].empty() ? std::string() : cont + textLines[i]);
    }
    return out;
}

Document Document::parse(const std::string& source) {
    Document d;
    d.original_ = source;
    std::vector<std::string> lines = splitLines(source, d.trailingNewline_);

    std::vector<int> stack;  // indentation widths of the open ancestors
    bool inFence = false;
    bool have = false;
    bool unitKnown = false;
    Pending cur;

    auto flush = [&]() {
        if (have) d.blocks_.push_back(finalize(cur));
        have = false;
    };

    for (const std::string& line : lines) {
        std::string indent, rest;
        bool isBullet = !inFence && matchBullet(line, indent, rest);

        if (isBullet) {
            flush();
            int w = widthOf(indent);
            while (!stack.empty() && stack.back() >= w) stack.pop_back();
            cur = Pending();
            cur.block.bullet = true;
            cur.block.depth = static_cast<int>(stack.size());
            if (cur.block.depth == 1 && !unitKnown) {
                d.unit_ = indent;  // how this file indents one level
                unitKnown = true;
            }
            cur.indent = indent;
            cur.block.raw = {line};
            cur.content = {rest};
            stack.push_back(w);
            have = true;
            updateFence(rest, inFence);
        } else if (!have) {
            // Loose lines before the first bullet form a block of their own.
            cur = Pending();
            cur.block.bullet = false;
            cur.block.depth = 0;
            cur.block.raw = {line};
            cur.content = {line};
            stack.push_back(0);
            have = true;
            updateFence(line, inFence);
        } else {
            cur.block.raw.push_back(line);
            cur.content.push_back(line);
            updateFence(line, inFence);
        }
    }
    flush();
    return d;
}

std::string Document::serialize() const {
    if (blocks_.empty()) return std::string();
    const size_t n = blocks_.size();

    // Untouched blocks are written back exactly as read. A loose (bullet-less)
    // block is only valid as the very first block of the file.
    std::vector<bool> useRaw(n);
    for (size_t i = 0; i < n; ++i) useRaw[i] = !blocks_[i].dirty() && (blocks_[i].bullet || i == 0);

    auto linesOf = [&](size_t i) {
        return useRaw[i] ? blocks_[i].raw : blocks_[i].render(unit_, i == 0 && !blocks_[i].bullet);
    };

    // Raw blocks keep their original indentation, which can read as the wrong
    // depth once neighbours were moved or re-indented (files that mix tabs and
    // spaces). Check every block's depth the way parse() would read it back, and
    // rewrite the nearest raw block in the file's own style until it all agrees.
    for (bool again = true; again;) {
        again = false;
        std::vector<int> stack;
        for (size_t i = 0; i < n && !again; ++i) {
            std::vector<std::string> lines = linesOf(i);
            std::string indent, rest;
            int w = matchBullet(lines[0], indent, rest) ? widthOf(indent) : 0;
            while (!stack.empty() && stack.back() >= w) stack.pop_back();
            if (static_cast<int>(stack.size()) == blocks_[i].depth) {
                stack.push_back(w);
                continue;
            }
            size_t j = i + 1;
            while (j > 0 && !useRaw[j - 1]) --j;
            if (j == 0) break;  // everything is already rewritten; cannot disagree
            useRaw[j - 1] = false;
            again = true;
        }
    }

    std::vector<std::string> all;
    for (size_t i = 0; i < n; ++i) {
        const std::vector<std::string> lines = linesOf(i);
        all.insert(all.end(), lines.begin(), lines.end());
    }
    std::string out = join(all);
    // A last line that is blank can only be read back correctly if the file also
    // ends with a newline, so in that case one is always written.
    if (trailingNewline_ || (!all.empty() && all.back().empty())) out += '\n';
    return out;
}

bool Document::modified() const {
    return serialize() != original_;
}

void Document::markSaved() {
    original_ = serialize();
    trailingNewline_ = !original_.empty() && original_.back() == '\n';
    for (size_t i = 0; i < blocks_.size(); ++i) {
        Block& b = blocks_[i];
        // Untouched blocks keep their exact original lines, even after a save.
        if (b.dirty()) {
            const bool loose = (i == 0 && !b.bullet);
            b.raw = b.render(unit_, loose);
            if (!loose) b.bullet = true;
            b.fresh = false;
        }
        b.origDepth = b.depth;
        b.origText = b.text;
        b.origMeta = b.meta;
        b.fresh = false;
    }
}

int Document::subtreeEnd(int i) const {
    int n = size();
    int j = i + 1;
    while (j < n && blocks_[static_cast<size_t>(j)].depth > blocks_[static_cast<size_t>(i)].depth) ++j;
    return j;
}

bool Document::hasChildren(int i) const {
    return i + 1 < size() && blocks_[static_cast<size_t>(i + 1)].depth > blocks_[static_cast<size_t>(i)].depth;
}

std::vector<int> Document::visible() const {
    std::vector<int> v;
    for (int i = 0; i < size(); ++i) {
        v.push_back(i);
        if (blocks_[static_cast<size_t>(i)].collapsed() && hasChildren(i)) i = subtreeEnd(i) - 1;
    }
    return v;
}

int Document::previousSibling(int i) const {
    int d = blocks_[static_cast<size_t>(i)].depth;
    for (int k = i - 1; k >= 0; --k) {
        int kd = blocks_[static_cast<size_t>(k)].depth;
        if (kd < d) return -1;
        if (kd == d) return k;
    }
    return -1;
}

namespace {
// Trailing newlines mean nothing inside a block, and a blank last line would be
// indistinguishable from the file's own final newline when read back.
std::string stripTrailingNewlines(std::string s) {
    while (!s.empty() && s.back() == '\n') s.pop_back();
    return s;
}
}  // namespace

void Document::setText(int i, const std::string& text) {
    Block& b = blocks_[static_cast<size_t>(i)];
    std::string clean = stripTrailingNewlines(text);
    // Keep a block's original trailing blank line (parsed from the file) if the
    // visible text is otherwise unchanged.
    if (clean == stripTrailingNewlines(b.origText) && text == b.origText) clean = b.origText;
    b.text = clean;
}

void Document::setCollapsed(int i, bool collapsed) {
    Block& b = blocks_[static_cast<size_t>(i)];
    if (b.collapsed() == collapsed) return;
    if (collapsed) {
        b.meta.insert(b.meta.begin(), kCollapsedLine);
    } else {
        b.meta.erase(std::remove(b.meta.begin(), b.meta.end(), std::string(kCollapsedLine)), b.meta.end());
    }
}

bool Document::indent(int i) {
    int sibling = previousSibling(i);
    if (sibling < 0) return false;
    int end = subtreeEnd(i);
    for (int j = i; j < end; ++j) ++blocks_[static_cast<size_t>(j)].depth;
    setCollapsed(sibling, false);  // keep the moved block visible
    return true;
}

bool Document::outdent(int i) {
    if (blocks_[static_cast<size_t>(i)].depth == 0) return false;
    int end = subtreeEnd(i);
    for (int j = i; j < end; ++j) --blocks_[static_cast<size_t>(j)].depth;
    return true;
}

int Document::moveUp(int i) {
    int sibling = previousSibling(i);
    if (sibling < 0) return -1;
    int end = subtreeEnd(i);
    std::rotate(blocks_.begin() + sibling, blocks_.begin() + i, blocks_.begin() + end);
    return sibling;
}

int Document::moveDown(int i) {
    int next = subtreeEnd(i);
    if (next >= size() || blocks_[static_cast<size_t>(next)].depth != blocks_[static_cast<size_t>(i)].depth) return -1;
    int nextEnd = subtreeEnd(next);
    std::rotate(blocks_.begin() + i, blocks_.begin() + next, blocks_.begin() + nextEnd);
    return i + (nextEnd - next);
}

int Document::insertBelow(int i, const std::string& text) {
    Block nb;
    nb.fresh = true;
    nb.text = stripTrailingNewlines(text);
    int pos;
    const Block& cur = blocks_[static_cast<size_t>(i)];
    if (hasChildren(i) && !cur.collapsed()) {
        nb.depth = cur.depth + 1;
        pos = i + 1;
    } else {
        nb.depth = cur.depth;
        pos = subtreeEnd(i);
    }
    blocks_.insert(blocks_.begin() + pos, nb);
    return pos;
}

bool Document::remove(int i) {
    if (hasChildren(i)) return false;
    blocks_.erase(blocks_.begin() + i);
    return true;
}

int Document::ensureNonEmpty() {
    if (blocks_.empty()) {
        Block b;
        b.fresh = true;
        blocks_.push_back(b);
    }
    return 0;
}

std::string Document::exportBlocks(const std::vector<int>& roots) const {
    int minDepth = 1 << 20;
    for (int r : roots) minDepth = std::min(minDepth, blocks_[static_cast<size_t>(r)].depth);
    std::vector<std::string> lines;
    int covered = -1;  // a root inside an earlier root's subtree is already included
    for (int r : roots) {
        if (r < covered) continue;
        const int end = subtreeEnd(r);
        for (int i = r; i < end; ++i) {
            Block b = blocks_[static_cast<size_t>(i)];
            b.depth -= minDepth;
            b.meta.erase(std::remove_if(b.meta.begin(), b.meta.end(),
                                        [](const std::string& m) { return startsWith(m, "id:: "); }),
                         b.meta.end());
            const std::vector<std::string> out = b.render("\t", false);
            lines.insert(lines.end(), out.begin(), out.end());
        }
        covered = end;
    }
    return join(lines);
}

int Document::insertBlocks(int after, const Document& src) {
    if (src.blocks_.empty()) return after;
    int pos = 0, base = 0;
    if (after >= 0) {
        const Block& cur = blocks_[static_cast<size_t>(after)];
        base = cur.depth;
        if (cur.text.empty() && cur.meta.empty() && !hasChildren(after)) {
            pos = after;  // an empty block is simply replaced by what is pasted
            blocks_.erase(blocks_.begin() + after);
        } else {
            pos = subtreeEnd(after);
        }
    }
    int minDepth = 1 << 20;
    for (const Block& b : src.blocks_) minDepth = std::min(minDepth, b.depth);
    std::vector<Block> add;
    for (const Block& sb : src.blocks_) {
        if (sb.text.empty() && sb.meta.empty() && src.blocks_.size() > 1) continue;  // blank lines in a paste
        Block nb;
        nb.fresh = true;
        nb.text = sb.text;
        nb.depth = base + sb.depth - minDepth;
        for (const std::string& m : sb.meta)
            if (!startsWith(m, "id:: ")) nb.meta.push_back(m);
        add.push_back(nb);
    }
    // Never deeper than one level below the block before (a valid outline).
    int prevDepth = pos > 0 ? blocks_[static_cast<size_t>(pos - 1)].depth : -1;
    for (Block& b : add) {
        b.depth = std::min(b.depth, prevDepth + 1);
        prevDepth = b.depth;
    }
    blocks_.insert(blocks_.begin() + pos, add.begin(), add.end());
    return pos;
}

void Document::removeBlocks(int from, int to) {
    if (blocks_.empty() || from < 0 || to < from || to >= size()) return;
    const int end = subtreeEnd(to);
    blocks_.erase(blocks_.begin() + from, blocks_.begin() + end);
    ensureNonEmpty();
}

void Document::restoreFrom(const Document& snapshot) {
    blocks_ = snapshot.blocks_;
    trailingNewline_ = snapshot.trailingNewline_;
    unit_ = snapshot.unit_;
    // original_ stays: it is what is on disk now.
}

}  // namespace graphite
