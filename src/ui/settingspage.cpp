#include "ui/settingspage.hpp"

#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QScrollBar>

#include "ui/theme.hpp"

namespace graphite {

namespace {
constexpr int kMaxColumn = 760;
constexpr int kSide = 32;
}  // namespace

// Everything on the page, painted: the header, then the rows.
//   [] SETTINGS ||||||||||||||||||||||||||||||||| GRAPHITE
//   Settings
//   APPEARANCE ───────────────────────────────────────────
//   Colours            WALLPAPER  UNIT-01  UNIT-00  UNIT-02
//   Text size          ▮▮▮▮▮▮▮▮▯▯▯▯▯▯▯▯   11.5 PT
class SettingsPage::Content : public QWidget {
public:
    explicit Content(QWidget* parent) : QWidget(parent) { setMouseTracking(true); }
    QList<SettingRow> rows;
    int current = -1;

    int headerH() const {
        const Theme& th = Theme::instance();
        return 20 + QFontMetrics(th.labelFont()).height() + 12 + QFontMetrics(th.titleFont()).height() + 18;
    }
    int rowH(int i) const {
        const Theme& th = Theme::instance();
        const QFontMetrics lm(th.labelFont());
        if (rows[i].kind == SettingRow::Heading) return lm.height() + 34;
        return QFontMetrics(th.bodyFont()).lineSpacing() + (rows[i].note.isEmpty() ? 0 : lm.height() + 4) + 18;
    }
    int rowY(int i) const {
        int y = headerH();
        for (int k = 0; k < i; ++k) y += rowH(k);
        return y;
    }
    QRect column() const {
        const int w = qMin(kMaxColumn, width() - 2 * kSide);
        return QRect((width() - w) / 2, 0, w, height());
    }
    int controlLeft() const { return column().left() + column().width() * 2 / 5; }
    QSize sizeHint() const override { return QSize(600, rows.isEmpty() ? headerH() : rowY(rows.size()) + 80); }

    // Hit areas of a row's segments or choices.
    QList<QRect> parts(int i) const {
        QList<QRect> out;
        const SettingRow& r = rows[i];
        const Theme& th = Theme::instance();
        const int y = rowY(i) + 9, h = QFontMetrics(th.bodyFont()).lineSpacing();
        int x = controlLeft();
        if (r.kind == SettingRow::Slider) {
            const int room = column().right() - controlLeft() - 110;
            const int segW = qBound(4, room / (r.steps + 1) - 3, 22);
            for (int k = 0; k <= r.steps; ++k) out << QRect(x + k * (segW + 3), y, segW, h);
        } else if (r.kind == SettingRow::Choice) {
            const QFontMetrics lm(th.labelFont());
            for (const QString& c : r.choices) {
                const int w = lm.horizontalAdvance(c.toUpper()) + 20;
                out << QRect(x, y, w, h);
                x += w + 6;
            }
        }
        return out;
    }

    void paintEvent(QPaintEvent*) override {
        const Theme& th = Theme::instance();
        QPainter p(this);
        // The theme's own background, replacing pixels (alpha 0 really clears in transparent mode),
        // as the outliner does; the scroll area's default grey must never show.
        p.setCompositionMode(QPainter::CompositionMode_Source);
        p.fillRect(rect(), th.bg());
        p.setCompositionMode(QPainter::CompositionMode_SourceOver);
        p.setRenderHint(QPainter::Antialiasing);
        const QRect col = column();
        const QFont label = th.labelFont();
        const QFontMetrics lm(label);

        // Header, as on a page: kind label, ruler, readout; the title in the title font.
        const int rowY0 = 20 + lm.ascent();
        p.setRenderHint(QPainter::Antialiasing, false);
        p.fillRect(QRect(col.left(), rowY0 - 8, 6, 6), th.accent());
        p.setFont(label);
        p.setPen(th.dim());
        p.drawText(QPointF(col.left() + 15, rowY0), "Settings");
        const QString readout = "Graphite " GRAPHITE_VERSION_TEXT;
        const int rw = lm.horizontalAdvance(readout.toUpper());
        p.drawText(QPointF(col.right() - rw, rowY0), readout);
        const int x0 = col.left() + 15 + lm.horizontalAdvance("SETTINGS") + 16, x1 = col.right() - rw - 16;
        p.setPen(QPen(th.line(), 1));
        if (x1 > x0) {
            p.drawLine(x0, rowY0 - 5, x1, rowY0 - 5);
            for (int tx = x0, i = 0; tx <= x1; tx += 12, ++i) p.drawLine(tx, rowY0 - 5, tx, rowY0 - 5 + (i % 5 == 0 ? 5 : 2));
        }
        p.setRenderHint(QPainter::Antialiasing, true);
        p.setFont(th.titleFont());
        p.setPen(th.text());
        p.drawText(QPointF(col.left(), rowY0 + 12 + QFontMetrics(th.titleFont()).ascent()), "Settings");

        for (int i = 0; i < rows.size(); ++i) {
            const SettingRow& r = rows[i];
            const int top = rowY(i);
            if (r.kind == SettingRow::Heading) {
                const int y = top + 22;
                p.setFont(label);
                p.setPen(th.accent());
                p.drawText(QPointF(col.left(), y + lm.ascent()), r.name);
                const int lx = col.left() + lm.horizontalAdvance(r.name.toUpper()) + 14;
                p.setPen(QPen(th.line(), 1));
                p.drawLine(lx, y + lm.ascent() - 4, col.right(), y + lm.ascent() - 4);
                continue;
            }
            const int y = top + 9, h = QFontMetrics(th.bodyFont()).lineSpacing();
            const bool sel = i == current;
            if (sel) {
                QColor band = th.structure();
                band.setAlpha(22);
                p.fillRect(QRect(col.left() - 12, top + 2, col.width() + 24, rowH(i) - 4), band);
                p.fillRect(QRect(col.left() - 12, top + 2, 2, rowH(i) - 4), th.accent());
            }
            p.setFont(th.bodyFont());
            p.setPen(sel ? th.text() : th.dim());
            p.drawText(QRect(col.left(), y, controlLeft() - col.left() - 12, h), Qt::AlignVCenter | Qt::AlignLeft, r.name);
            const QList<QRect> ps = parts(i);
            p.setFont(label);
            if (r.kind == SettingRow::Slider) {
                const int v = r.get();
                const int segH = lm.height() - 2;
                for (int k = 0; k < ps.size(); ++k) {
                    QRect seg = ps[k];
                    seg.setTop(seg.center().y() - segH / 2);
                    seg.setHeight(segH);
                    p.fillRect(seg, k < v ? th.structure() : k == v ? th.accent() : th.line());
                }
                p.setPen(sel ? th.accent() : th.dim());
                p.drawText(QRect(ps.isEmpty() ? controlLeft() : ps.back().right() + 14, y, 120, h), Qt::AlignVCenter | Qt::AlignLeft,
                           r.label ? r.label(v) : QString::number(v));
            } else if (r.kind == SettingRow::Choice) {
                const int v = r.get();
                for (int k = 0; k < ps.size(); ++k) {
                    const bool on = k == v;
                    if (on) {
                        QColor fill = th.accent();
                        fill.setAlpha(34);
                        p.setPen(Qt::NoPen);
                        p.setBrush(fill);
                        p.drawRoundedRect(QRectF(ps[k]).adjusted(0, 1, 0, -1), 3, 3);
                    }
                    p.setPen(on ? th.accent() : th.faint());
                    p.drawText(ps[k], Qt::AlignCenter, r.choices[k]);
                }
            } else {
                // Action: its value (if any) and "Enter ›"; Info: the value only.
                const QString v = r.value ? r.value() : QString();
                p.setFont(th.bodyFont());
                p.setPen(th.dim());
                const int room = col.right() - controlLeft() - (r.kind == SettingRow::Action ? 80 : 0);
                p.drawText(QRect(controlLeft(), y, room, h), Qt::AlignVCenter | Qt::AlignLeft,
                           QFontMetrics(th.bodyFont()).elidedText(v, Qt::ElideMiddle, room));
                if (r.kind == SettingRow::Action) {
                    p.setFont(label);
                    p.setPen(sel ? th.accent() : th.faint());
                    p.drawText(QRect(controlLeft(), y, col.right() - controlLeft(), h), Qt::AlignVCenter | Qt::AlignRight,
                               "Enter \xE2\x80\xBA");
                }
            }
            if (!r.note.isEmpty()) {
                p.setFont(label);
                p.setPen(th.faint());
                p.drawText(QRect(col.left(), y + h + 4, col.width(), lm.height()), Qt::AlignLeft | Qt::AlignTop,
                           lm.elidedText(r.note.toUpper(), Qt::ElideRight, col.width()));  // long notes at big sizes
            }
        }
    }

    int rowAt(int y) const {
        for (int i = 0; i < rows.size(); ++i)
            if (y >= rowY(i) && y < rowY(i) + rowH(i)) return i;
        return -1;
    }
    void mousePressEvent(QMouseEvent* e) override {
        const QPoint pt = e->position().toPoint();
        const int i = rowAt(pt.y());
        if (i < 0 || rows[i].kind == SettingRow::Heading || rows[i].kind == SettingRow::Info) return;
        current = i;
        const SettingRow& r = rows[i];
        if (r.kind == SettingRow::Action) {
            if (r.run) r.run();
            update();
            return;
        }
        const QList<QRect> ps = parts(i);
        for (int k = 0; k < ps.size(); ++k)
            if (ps[k].adjusted(-2, -8, 2, 8).contains(pt) && k != r.get()) r.set(k);
        update();
    }
    void mouseMoveEvent(QMouseEvent* e) override {
        if (e->buttons() & Qt::LeftButton) mousePressEvent(e);  // drag along a slider
        const int i = rowAt(e->position().toPoint().y());
        const bool hot = i >= 0 && rows[i].kind != SettingRow::Heading && rows[i].kind != SettingRow::Info;
        setCursor(hot ? Qt::PointingHandCursor : Qt::ArrowCursor);
    }
};

SettingsPage::SettingsPage(QWidget* parent) : QScrollArea(parent) {
    setFrameShape(QFrame::NoFrame);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setWidgetResizable(true);
    setFocusPolicy(Qt::StrongFocus);
    viewport()->setAutoFillBackground(false);
    setStyleSheet("QScrollArea { background: transparent; border: none; }");
    content_ = new Content(this);
    content_->setAutoFillBackground(false);
    // Never shorter than its rows: a resizable scroll area would otherwise squeeze it to the
    // window, and the rows below the fold could not be scrolled to.
    content_->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Minimum);
    setWidget(content_);
    connect(&Theme::instance(), &Theme::changed, this, [this] {
        content_->updateGeometry();
        content_->update();
    });
}

void SettingsPage::setRows(const QList<SettingRow>& rows) {
    content_->rows = rows;
    content_->current = -1;
    move(1);  // the first row that can be changed
    content_->updateGeometry();
    content_->adjustSize();
    content_->update();
}

int SettingsPage::currentRow() const { return content_->current; }

void SettingsPage::select(int row) {
    content_->current = row;
    ensureVisible(0, content_->rowY(row) + content_->rowH(row) / 2, 0, content_->rowH(row));
    content_->update();
}

void SettingsPage::move(int delta) {
    const QList<SettingRow>& rows = content_->rows;
    const int n = static_cast<int>(rows.size());
    if (n == 0) return;
    int i = content_->current;
    for (int step = 0; step < n; ++step) {  // skip headings and information
        i = (i + delta + n) % n;
        if (rows[i].kind != SettingRow::Heading && rows[i].kind != SettingRow::Info) break;
    }
    select(i);
}

void SettingsPage::change(int delta) {
    const int i = content_->current;
    if (i < 0) return;
    const SettingRow& r = content_->rows[i];
    if (r.kind != SettingRow::Slider && r.kind != SettingRow::Choice) return;
    const int count = r.kind == SettingRow::Slider ? r.steps + 1 : static_cast<int>(r.choices.size());
    const int v = qBound(0, r.get() + delta, count - 1);
    if (v != r.get()) r.set(v);
    content_->update();
}

void SettingsPage::activate() {
    const int i = content_->current;
    if (i < 0) return;
    const SettingRow& r = content_->rows[i];
    if (r.kind == SettingRow::Action && r.run) r.run();
    else change(1);
    content_->update();
}

void SettingsPage::keyPressEvent(QKeyEvent* e) {
    switch (e->key()) {
        case Qt::Key_Up: move(-1); return;
        case Qt::Key_Down: move(1); return;
        case Qt::Key_Left: change(-1); return;
        case Qt::Key_Right: change(1); return;
        case Qt::Key_Return:
        case Qt::Key_Enter: activate(); return;
        case Qt::Key_Escape: emit closeRequested(); return;
        default: QScrollArea::keyPressEvent(e);
    }
}

bool SettingsPage::focusNextPrevChild(bool next) {
    move(next ? 1 : -1);
    return true;
}

void SettingsPage::resizeEvent(QResizeEvent* e) {
    QScrollArea::resizeEvent(e);
    content_->update();
}

}  // namespace graphite
