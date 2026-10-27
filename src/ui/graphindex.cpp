#include "ui/graphindex.hpp"

#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSet>
#include <algorithm>

#include "core/document.hpp"

namespace graphite {

namespace {

qint64 mtimeOf(const QString& path) { return QFileInfo(path).lastModified().toMSecsSinceEpoch(); }

// Inline code and formulas are not links, whatever they contain.
QString blankCodeAndMath(const QString& line) {
    static const QRegularExpression skip(R"(`[^`\n]+`|\$\$.+?\$\$|\$[^$\n]+?\$)");
    QString out = line;
    QRegularExpressionMatchIterator it = skip.globalMatch(line);
    while (it.hasNext()) {
        const QRegularExpressionMatch m = it.next();
        out.replace(m.capturedStart(), m.capturedLength(), QString(m.capturedLength(), ' '));
    }
    return out;
}

QString stripBrackets(QString s) {
    s = s.trimmed();
    if (s.startsWith('#')) s = s.mid(1);
    if (s.startsWith("[[") && s.endsWith("]]")) s = s.mid(2, s.size() - 4);
    return s.trimmed();
}

}  // namespace

QStringList GraphIndex::referencesIn(const QString& text) {
    // Same tag shape as the outliner draws, but only at the start of a word, so "C#" and
    // "page#anchor" are not tags.
    static const QRegularExpression link(R"(\[\[([^\[\]\n]+)\]\])");
    static const QRegularExpression tag(R"((?<![^\s(])#([^\s#\[\]\(\),.;:!?"']+))");
    static const QRegularExpression tagsProperty(R"(^\s*tags::\s*(.*)$)", QRegularExpression::CaseInsensitiveOption);

    QStringList out;
    QSet<QString> seen;
    auto add = [&](const QString& name) {
        const QString n = name.trimmed();
        if (n.isEmpty()) return;
        const QString key = n.toLower();
        if (seen.contains(key)) return;
        seen.insert(key);
        out << n;
    };

    bool inFence = false;
    for (const QString& raw : text.split('\n')) {
        if (raw.trimmed().startsWith("```")) {
            inFence = !inFence;
            continue;
        }
        if (inFence) continue;
        const QString line = blankCodeAndMath(raw);
        if (const QRegularExpressionMatch p = tagsProperty.match(line); p.hasMatch()) {
            for (const QString& v : p.captured(1).split(',')) add(stripBrackets(v));
            continue;
        }
        for (auto it = link.globalMatch(line); it.hasNext();) {
            const QString target = it.next().captured(1);
            if (!target.startsWith("eq:")) add(target);  // [[eq:x]] is a formula on the page, not a page
        }
        for (auto it = tag.globalMatch(line); it.hasNext();) add(it.next().captured(1));
    }
    return out;
}

void GraphIndex::setBlocks(int page, const Document& doc) {
    QStringList blocks;
    blocks.reserve(doc.size());
    QVector<qint8> depths;
    depths.reserve(doc.size());
    for (int i = 0; i < doc.size(); ++i) {
        blocks << QString::fromStdString(doc.at(i).text);
        depths << static_cast<qint8>(qMin(doc.at(i).depth, 127));
    }
    pages_[page].blocks = blocks;
    pages_[page].depths = depths;
    refsDirty_ = true;
}

void GraphIndex::sync(const QList<File>& files) {
    QVector<Page> next;
    QHash<QString, int> nextBy;
    next.reserve(files.size());
    bool changed = files.size() != pages_.size();
    for (const File& f : files) {
        const qint64 mt = mtimeOf(f.path);
        Page p;
        const int old = byPath_.value(f.path, -1);
        if (old >= 0 && pages_[old].mtime == mt) {
            p = std::move(pages_[old]);  // unchanged on disk: keep what was read before
            changed = changed || p.title != f.title || old != next.size();
        } else {
            QFile in(f.path);
            if (in.open(QIODevice::ReadOnly)) {
                const Document doc = Document::parse(in.readAll().toStdString());
                for (int i = 0; i < doc.size(); ++i) {
                    p.blocks << QString::fromStdString(doc.at(i).text);
                    p.depths << static_cast<qint8>(qMin(doc.at(i).depth, 127));
                }
            }
            changed = true;
        }
        p.path = f.path;
        p.title = f.title;
        p.journal = f.journal;
        p.mtime = mt;
        nextBy.insert(f.path, next.size());
        next.push_back(std::move(p));
    }
    pages_ = std::move(next);
    byPath_ = std::move(nextBy);
    if (changed) refsDirty_ = true;
}

void GraphIndex::update(const QString& path, const QString& title, bool journal, const Document& doc, qint64 mtime) {
    int k = byPath_.value(path, -1);
    if (k < 0) {
        k = pages_.size();
        pages_.push_back(Page());
        byPath_.insert(path, k);
    }
    Page& p = pages_[k];
    p.path = path;
    p.title = title;
    p.journal = journal;
    p.mtime = mtime;
    setBlocks(k, doc);
}

void GraphIndex::ensureRefs() const {
    if (!refsDirty_) return;
    refs_.clear();
    for (int pi = 0; pi < pages_.size(); ++pi) {
        const QStringList& blocks = pages_[pi].blocks;
        for (int bi = 0; bi < blocks.size(); ++bi) {
            if (!blocks[bi].contains("[[") && !blocks[bi].contains('#') && !blocks[bi].contains("::")) continue;
            for (const QString& target : referencesIn(blocks[bi])) refs_[target.toLower()].push_back({pi, bi});
        }
    }
    refs_.squeeze();
    refsDirty_ = false;
}

QList<GraphIndex::Reference> GraphIndex::backlinks(const QString& title) const {
    ensureRefs();
    QList<Reference> out;
    const QString self = title.toLower();
    for (const Reference& r : refs_.value(self))
        if (pages_[r.page].title.toLower() != self) out << r;
    std::stable_sort(out.begin(), out.end(), [this](const Reference& a, const Reference& b) {
        const Page& pa = pages_[a.page];
        const Page& pb = pages_[b.page];
        if (a.page == b.page) return a.block < b.block;
        if (pa.journal != pb.journal) return pa.journal;  // journals first
        if (pa.journal) return pa.path > pb.path;          // file names sort by date: newest first
        return pa.title.compare(pb.title, Qt::CaseInsensitive) < 0;
    });
    return out;
}

int GraphIndex::backlinkCount(const QString& title) const { return backlinks(title).size(); }

QList<GraphIndex::Hit> GraphIndex::search(const QString& query, int limit) const {
    const QString q = query.simplified();
    QList<Hit> out;
    if (q.isEmpty()) return out;
    const QStringList words = q.split(' ');
    auto containsAll = [&words](const QString& s) {
        for (const QString& w : words)
            if (!s.contains(w, Qt::CaseInsensitive)) return false;
        return true;
    };

    for (int pi = 0; pi < pages_.size(); ++pi) {
        const QString& t = pages_[pi].title;
        int score = 0;
        if (t.compare(q, Qt::CaseInsensitive) == 0) score = 1000;
        else if (t.startsWith(q, Qt::CaseInsensitive)) score = 700;
        else if (containsAll(t)) score = 400;
        if (score) out << Hit{pi, -1, score - qMin<int>(t.size(), 100)};  // shorter titles first
    }
    QList<Hit> blocks;
    for (int pi = 0; pi < pages_.size(); ++pi) {
        const QStringList& bs = pages_[pi].blocks;
        for (int bi = 0; bi < bs.size(); ++bi) {
            if (bs[bi].isEmpty() || !containsAll(bs[bi])) continue;
            // A whole-phrase match ranks above scattered words; recent journals above old ones.
            int score = bs[bi].contains(q, Qt::CaseInsensitive) ? 200 : 100;
            if (pages_[pi].journal) score += 1;
            blocks << Hit{pi, bi, score};
        }
    }
    std::stable_sort(out.begin(), out.end(), [](const Hit& a, const Hit& b) { return a.score > b.score; });
    std::stable_sort(blocks.begin(), blocks.end(), [this](const Hit& a, const Hit& b) {
        if (a.score != b.score) return a.score > b.score;
        return pages_[a.page].path > pages_[b.page].path;
    });
    out += blocks;
    if (out.size() > limit) out = out.mid(0, limit);
    return out;
}

}  // namespace graphite
