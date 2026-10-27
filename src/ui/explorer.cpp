#include "ui/explorer.hpp"

#include <QContextMenuEvent>
#include <QDrag>
#include <QHeaderView>
#include <QKeyEvent>
#include <QMenu>
#include <QMimeData>

#include "ui/theme.hpp"

namespace graphite {

namespace {
constexpr int kPathRole = Qt::UserRole;
constexpr int kKindRole = Qt::UserRole + 1;
constexpr int kNameRole = Qt::UserRole + 2;  // the bare name, for renaming
const char* kMime = "application/x-graphite-page";
}  // namespace

Explorer::Explorer(QWidget* parent) : QTreeWidget(parent) {
    setObjectName("nav");
    setHeaderHidden(true);
    setRootIsDecorated(false);  // our own ▸ / ▾ in the book row
    setIndentation(14);
    setFrameShape(QFrame::NoFrame);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setFocusPolicy(Qt::ClickFocus);
    setEditTriggers(QAbstractItemView::NoEditTriggers);  // renaming goes through beginRename()
    setDragEnabled(true);
    setAcceptDrops(true);
    setDropIndicatorShown(true);
    setDragDropMode(QAbstractItemView::DragDrop);
    setExpandsOnDoubleClick(false);
    setUniformRowHeights(true);
    viewport()->setAutoFillBackground(false);  // the sidebar's colour (and the unit art) show through
    setAutoFillBackground(false);

    connect(this, &QTreeWidget::itemClicked, this, [this](QTreeWidgetItem* it) {
        const int kind = it->data(0, kKindRole).toInt();
        if (kind == Journal || kind == PageItem) {
            emit openRequested(it->data(0, kPathRole).toString());
        } else if (kind == BookItem || kind == Header) {  // a click folds or unfolds
            it->setExpanded(!it->isExpanded());
        }
    });
    auto remember = [this](QTreeWidgetItem* it) {
        if (filling_) return;
        const int kind = it->data(0, kKindRole).toInt();
        if (kind != BookItem && kind != Header) return;
        const QString key = kind == Header ? QString("journals") : it->data(0, kPathRole).toString();
        if (it->isExpanded()) folded_.remove(key);
        else folded_.insert(key);
        if (kind == BookItem) setFoldGlyph(it);
        emit foldedChanged();
    };
    connect(this, &QTreeWidget::itemExpanded, this, remember);
    connect(this, &QTreeWidget::itemCollapsed, this, remember);
    connect(this, &QTreeWidget::itemChanged, this, [this](QTreeWidgetItem* it) {
        if (filling_) return;
        const QString name = it->text(0).trimmed();
        if (name.isEmpty() || name == it->data(0, kNameRole).toString()) {
            if (it->data(0, kKindRole).toInt() == BookItem) setFoldGlyph(it);
            return;
        }
        emit renameRequested(it->data(0, kPathRole).toString(), name);
    });
}

void Explorer::beginRename(QTreeWidgetItem* it) {
    filling_ = true;  // dropping the glyph and count is not a rename
    it->setText(0, it->data(0, kNameRole).toString());
    filling_ = false;
    editItem(it);
}

void Explorer::setFoldGlyph(QTreeWidgetItem* book) {
    const bool was = filling_;
    filling_ = true;
    const int n = book->childCount();
    book->setText(0, QString(book->isExpanded() ? "\xE2\x96\xBE  " : "\xE2\x96\xB8  ") + book->data(0, kNameRole).toString() +
                         (n ? QString("   %1").arg(n) : QString()));
    filling_ = was;
}

void Explorer::setContent(const QList<Page>& journals, const QList<Book>& books, const QString& current) {
    filling_ = true;
    clear();
    const Theme& th = Theme::instance();
    QFont label = th.labelFont();

    // Headings get air above them, as the old list had.
    auto* jh = new QTreeWidgetItem(this, {"Journals"});
    jh->setData(0, kKindRole, Header);
    jh->setFont(0, label);
    jh->setForeground(0, th.faint());
    jh->setFlags(Qt::ItemIsEnabled);
    for (const Page& j : journals) {
        auto* it = new QTreeWidgetItem(jh, {j.title});
        it->setData(0, kPathRole, j.path);
        it->setData(0, kKindRole, Journal);
        it->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
    }
    jh->setExpanded(!folded_.contains("journals"));

    auto* bh = new QTreeWidgetItem(this, {"Books"});
    bh->setData(0, kKindRole, Header);
    bh->setFont(0, label);
    bh->setForeground(0, th.faint());
    bh->setFlags(Qt::ItemIsEnabled | Qt::ItemIsDropEnabled);
    bh->setExpanded(true);
    for (const Book& b : books) {
        auto* bi = new QTreeWidgetItem(bh);
        bi->setData(0, kPathRole, b.dir);
        bi->setData(0, kKindRole, BookItem);
        bi->setData(0, kNameRole, b.name);
        QFont bf = th.bodyFont();
        bf.setWeight(QFont::DemiBold);
        bi->setFont(0, bf);
        bi->setFlags(Qt::ItemIsEnabled | Qt::ItemIsDropEnabled | Qt::ItemIsEditable);
        for (const Page& p : b.pages) {
            auto* pi = new QTreeWidgetItem(bi, {p.title});
            pi->setData(0, kPathRole, p.path);
            pi->setData(0, kKindRole, PageItem);
            pi->setData(0, kNameRole, p.title);
            pi->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsDragEnabled | Qt::ItemIsEditable);
        }
        bi->setExpanded(!folded_.contains(b.dir));
        setFoldGlyph(bi);
    }
    filling_ = false;
    setCurrent(current);
}

void Explorer::setCurrent(const QString& path) {
    clearSelection();
    for (QTreeWidgetItemIterator it(this); *it; ++it)
        if ((*it)->data(0, kPathRole).toString() == path && (*it)->data(0, kKindRole).toInt() != BookItem) {
            setCurrentItem(*it);
            scrollToItem(*it);
            return;
        }
}

QString Explorer::bookDirOf(QTreeWidgetItem* it) const {
    if (!it) return QString();
    if (it->data(0, kKindRole).toInt() == BookItem) return it->data(0, kPathRole).toString();
    if (it->data(0, kKindRole).toInt() == PageItem && it->parent()) return it->parent()->data(0, kPathRole).toString();
    return QString();
}

QString Explorer::currentBookDir() const { return bookDirOf(currentItem()); }

void Explorer::startDrag(Qt::DropActions) {
    QTreeWidgetItem* it = currentItem();
    if (!it || it->data(0, kKindRole).toInt() != PageItem) return;
    auto* mime = new QMimeData;
    mime->setData(kMime, it->data(0, kPathRole).toString().toUtf8());
    auto* drag = new QDrag(this);
    drag->setMimeData(mime);
    drag->exec(Qt::MoveAction);
}

void Explorer::dragMoveEvent(QDragMoveEvent* e) {
    const bool ok = e->mimeData()->hasFormat(kMime) && !bookDirOf(itemAt(e->position().toPoint())).isEmpty();
    if (ok) e->acceptProposedAction();
    else e->ignore();
}

void Explorer::dropEvent(QDropEvent* e) {
    const QString dir = bookDirOf(itemAt(e->position().toPoint()));
    if (!e->mimeData()->hasFormat(kMime) || dir.isEmpty()) {
        e->ignore();
        return;
    }
    e->acceptProposedAction();
    emit moveRequested(QString::fromUtf8(e->mimeData()->data(kMime)), dir);  // the window moves it and rebuilds
}

void Explorer::keyPressEvent(QKeyEvent* e) {
    QTreeWidgetItem* it = currentItem();
    const int kind = it ? it->data(0, kKindRole).toInt() : 0;
    if (e->key() == Qt::Key_Delete && (kind == PageItem || kind == BookItem || kind == Journal)) {
        emit deleteRequested(it->data(0, kPathRole).toString());
        return;
    }
    if ((e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter) && (kind == PageItem || kind == Journal)) {
        emit openRequested(it->data(0, kPathRole).toString());
        return;
    }
    if (e->key() == Qt::Key_F2 && (kind == BookItem || kind == PageItem)) {
        beginRename(it);
        return;
    }
    QTreeWidget::keyPressEvent(e);
}

void Explorer::contextMenuEvent(QContextMenuEvent* e) {
    QTreeWidgetItem* it = itemAt(e->pos());
    const int kind = it ? it->data(0, kKindRole).toInt() : 0;
    const QString path = it ? it->data(0, kPathRole).toString() : QString();
    const QString book = bookDirOf(it);
    const Theme& th = Theme::instance();
    QMenu menu(this);
    auto css = [](const QColor& c) { return QString("rgba(%1,%2,%3,%4)").arg(c.red()).arg(c.green()).arg(c.blue()).arg(c.alpha()); };
    QColor panel = th.palette().panel;
    panel.setAlpha(255);
    menu.setStyleSheet(QString("QMenu { background: %1; color: %2; border: 1px solid %3; padding: 4px; }"
                               "QMenu::item { padding: 5px 18px; border-radius: 3px; }"
                               "QMenu::item:selected { background: %4; color: %5; }"
                               "QMenu::separator { height: 1px; background: %3; margin: 4px 8px; }")
                           .arg(css(panel), css(th.dim()), css(th.line()), css(QColor(th.structure().red(), th.structure().green(),
                                                                                        th.structure().blue(), 60)),
                                css(th.text())));
    if (!book.isEmpty()) menu.addAction("New page here", this, [this, book] { emit newPageRequested(book); });
    menu.addAction("New book", this, [this] { emit newBookRequested(); });
    if (kind == PageItem || kind == BookItem) {
        menu.addSeparator();
        menu.addAction("Rename", this, [this, it] { beginRename(it); });
    }
    if (kind == PageItem) {
        QMenu* move = menu.addMenu("Move to");
        move->setStyleSheet(menu.styleSheet());
        for (int i = 0; i < topLevelItemCount(); ++i)
            for (int k = 0; k < topLevelItem(i)->childCount(); ++k) {
                QTreeWidgetItem* b = topLevelItem(i)->child(k);
                if (b->data(0, kKindRole).toInt() != BookItem || b->data(0, kPathRole).toString() == book) continue;
                const QString dir = b->data(0, kPathRole).toString();
                move->addAction(b->data(0, kNameRole).toString(), this, [this, path, dir] { emit moveRequested(path, dir); });
            }
    }
    if (kind == PageItem || kind == BookItem || kind == Journal) {
        menu.addSeparator();
        menu.addAction(kind == BookItem ? "Delete book" : "Delete", this, [this, path] { emit deleteRequested(path); });
    }
    menu.exec(e->globalPos());
}

}  // namespace graphite
