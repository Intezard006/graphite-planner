// The explorer: the tree's journals and books, with their pages.
//
//   JOURNALS
//     Oct 23rd, 2026
//   ▾ Organic Chem      12
//       Lecture 7
//   ▸ Quantum            4
//
// A tree is a folder; a book is a folder in it; a page is a .md file in a book. Click opens,
// F2 renames, Delete deletes, a page dragged onto a book moves there, and the
// context menu has the rest. The explorer only asks; the window does the work on disk.
#pragma once

#include <QSet>
#include <QTreeWidget>

namespace graphite {

class Explorer : public QTreeWidget {
    Q_OBJECT
public:
    struct Page {
        QString path;
        QString title;
    };
    struct Book {
        QString name;  // as shown ("Unsorted" for a Logseq pages/ folder)
        QString dir;   // absolute
        QList<Page> pages;
    };
    explicit Explorer(QWidget* parent = nullptr);

    // Rebuild from the tree's contents; `current` is highlighted. Folded books stay folded.
    void setContent(const QList<Page>& journals, const QList<Book>& books, const QString& current);
    void setCurrent(const QString& path);
    QSet<QString> folded() const { return folded_; }  // book dirs (and "journals") the user folded
    void setFolded(const QSet<QString>& f) { folded_ = f; }
    // The book under the highlight (for "new page here"), or empty.
    QString currentBookDir() const;

signals:
    void openRequested(const QString& path);
    void moveRequested(const QString& pagePath, const QString& bookDir);
    void renameRequested(const QString& path, const QString& newName);  // a page or a book dir
    void deleteRequested(const QString& path);                          // a page or a book dir
    void newPageRequested(const QString& bookDir);
    void newBookRequested();
    void foldedChanged();

protected:
    void dropEvent(QDropEvent*) override;
    void dragMoveEvent(QDragMoveEvent*) override;
    void contextMenuEvent(QContextMenuEvent*) override;
    void keyPressEvent(QKeyEvent*) override;
    void startDrag(Qt::DropActions supported) override;

private:
    enum Kind { Header = 1, Journal, BookItem, PageItem };
    QString bookDirOf(QTreeWidgetItem* it) const;
    void setFoldGlyph(QTreeWidgetItem* book);
    void beginRename(QTreeWidgetItem* it);  // the bare name in an edit box
    QSet<QString> folded_;
    bool filling_ = false;
};

}  // namespace graphite
