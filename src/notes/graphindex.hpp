// The graph's link index and text search: which blocks mention which pages.
//
// Built for battery and memory: it keeps only each page's block texts (no rich text, no
// layout) and never polls. sync() is called when something happened (startup, a save, the
// window regaining focus) and re-reads only the files whose modification time changed.
// The reference table is rebuilt lazily, the first time it is asked for after a change.
#pragma once

#include <QHash>
#include <QList>
#include <QString>
#include <QStringList>
#include <QVector>

namespace graphite {

class Document;

class GraphIndex {
public:
    struct File {
        QString path;
        QString title;
        bool journal = false;
    };
    struct Page {
        QString path;
        QString title;
        bool journal = false;
        qint64 mtime = -1;
        QStringList blocks;  // Block::text, same order as Document blocks
        QVector<qint8> depths;  // Block::depth of each (for subtasks)
    };
    struct Reference {
        int page = -1;   // index into pages()
        int block = -1;  // block index in that page
    };
    struct Hit {
        int page = -1;
        int block = -1;  // -1: the page title matched
        int score = 0;
    };

    // Makes the index match these files: new or changed files are read, missing ones dropped.
    void sync(const QList<File>& files);
    // The page being edited, straight from memory (it may not be saved yet).
    void update(const QString& path, const QString& title, bool journal, const Document& doc, qint64 mtime);

    const QVector<Page>& pages() const { return pages_; }
    int pageOf(const QString& path) const { return byPath_.value(path, -1); }

    // Blocks on *other* pages that link to `title` ([[title]], #title, #[[title]], tags:: title),
    // journals newest first, then pages A-Z.
    QList<Reference> backlinks(const QString& title) const;
    int backlinkCount(const QString& title) const;

    // Pages whose title contains every word, then blocks that do. Best first.
    QList<Hit> search(const QString& query, int limit = 40) const;

    // The page names a block's text refers to (original spelling, each once).
    static QStringList referencesIn(const QString& text);

private:
    void setBlocks(int page, const Document& doc);
    void ensureRefs() const;

    QVector<Page> pages_;
    QHash<QString, int> byPath_;
    mutable QHash<QString, QVector<Reference>> refs_;  // lower-case title -> referencing blocks
    mutable bool refsDirty_ = true;
};

}  // namespace graphite
