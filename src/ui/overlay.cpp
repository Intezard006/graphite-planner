#include "ui/overlay.hpp"

#include <QApplication>
#include <QEventLoop>
#include <QFontDatabase>
#include <QFontInfo>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QListWidget>
#include <QLocale>
#include <QMouseEvent>
#include <QPainter>
#include <QTime>
#include <QPolygonF>
#include <QPainterPath>
#include <QStyledItemDelegate>
#include <QVBoxLayout>
#include <cmath>

#include "ui/mathobject.hpp"
#include "ui/progress.hpp"
#include "ui/tasks.hpp"
#include "ui/theme.hpp"

namespace graphite {

namespace {
QString rgba(const QColor& c) {
    return QString("rgba(%1,%2,%3,%4)").arg(c.red()).arg(c.green()).arg(c.blue()).arg(c.alpha());
}
QString colorCss(const QColor& c) { return "color: " + rgba(c) + "; background: transparent;"; }
}  // namespace

// ------------------------------------------------------------------ Overlay

Overlay::Overlay(QWidget* host, int cardWidth) : QWidget(host), cardWidth_(cardWidth) {
    previousFocus_ = QApplication::focusWidget();
    setFocusPolicy(Qt::StrongFocus);
    card_ = new QWidget(this);
    body_ = new QVBoxLayout(card_);
    body_->setContentsMargins(28, 24, 28, 22);
    body_->setSpacing(0);
    host->installEventFilter(this);
    connect(&Theme::instance(), &Theme::changed, this, [this] {
        restyle();
        update();
    });
}

void Overlay::finish() {
    restyle();
    place();
    show();
    raise();
    setFocus();
}

void Overlay::place() {
    setGeometry(parentWidget()->rect());
    // Cards grow with the text size too (up to 1.45x), within the window.
    const int w = qMin(static_cast<int>(cardWidth_ * qBound<qreal>(1.0, Theme::instance().scale(), 1.45)), width() - 48);
    card_->setFixedWidth(w);
    card_->layout()->invalidate();  // a child changed size (the title box grows): measure afresh
    // Wrapped labels make adjustSize() guess too tall: ask the layout for this width.
    card_->setFixedHeight(card_->layout()->hasHeightForWidth() ? card_->layout()->heightForWidth(w)
                                                                : card_->sizeHint().height());
    card_->layout()->activate();
    const int x = (width() - card_->width()) / 2;
    const int y = qMax(24, qMin((height() - card_->height()) / 2, height() / 5));
    card_->move(x, y);
}

bool Overlay::eventFilter(QObject* obj, QEvent* ev) {
    if (obj == parentWidget() && ev->type() == QEvent::Resize) place();
    return QWidget::eventFilter(obj, ev);
}

void Overlay::paintEvent(QPaintEvent*) {
    const Theme& th = Theme::instance();
    const PaletteColors& pal = th.palette();
    QPainter p(this);
    QColor scrim = pal.bg;
    scrim.setAlpha(th.transparent() ? 110 : 160);
    p.fillRect(rect(), scrim);

    paintCard(p, card_->geometry());
}

void Overlay::paintCard(QPainter& p, const QRect& c) {
    const Theme& th = Theme::instance();
    const PaletteColors& pal = th.palette();
    // The card stays nearly opaque even in transparent mode: it has to be readable over a page.
    QColor fill = pal.panel;
    fill.setAlpha(th.transparent() ? 225 : 255);
    p.fillRect(c, fill);
    p.setPen(QPen(th.line(), 1));
    p.drawRect(c.adjusted(0, 0, -1, -1));
    p.fillRect(QRect(c.left(), c.top(), 44, 3), th.accent());  // the accent tab, as under v0.1's title
}

void Overlay::mousePressEvent(QMouseEvent* e) {
    if (!card_->geometry().contains(e->position().toPoint())) dismiss();
}

void Overlay::keyPressEvent(QKeyEvent* e) {
    if (e->key() == Qt::Key_Escape) {
        dismiss();
        return;
    }
    QWidget::keyPressEvent(e);
}

void Overlay::dismiss() {
    if (!isVisible()) return;
    hide();
    if (previousFocus_) previousFocus_->setFocus();
    emit dismissed();
    deleteLater();
}

// --------------------------------------------------------------- PagePrompt

namespace {
// Title-card ink: near-white on near-black, whatever the palette, as in the series.
const QColor kCardInk(0xf4, 0xf2, 0xee);
const QColor kCardBlack(4, 4, 6);

QFont cardTitleFont(qreal scale) {
    QFont f = Theme::instance().titleFont();
    f.setPointSizeF(f.pointSizeF() * scale);
    return f;
}
QFont cardCaptionFont(qreal scale) {
    QFont f = Theme::instance().titleFont();
    f.setWeight(QFont::Normal);
    f.setPointSizeF(f.pointSizeF() * scale);
    return f;
}
}  // namespace

PagePrompt::PagePrompt(QWidget* host, std::function<bool(const QString&)> exists, int episode,
                       const QList<QPair<QString, QString>>& books, int book)
    : Overlay(host, 680), exists_(std::move(exists)), books_(books), book_(qBound(0, book, qMax(0, static_cast<int>(books.size()) - 1))) {
    const Theme& th = Theme::instance();
    const bool eva = th.immersion();

    // The layout of the episode cards: the title stacked in a box on the left, then the
    // episode label, then the caption set to the right.
    input_ = new QPlainTextEdit(card());
    input_->setFont(eva ? cardTitleFont(1.45) : [&] {
        QFont big = th.bodyFont();
        big.setPointSizeF(big.pointSizeF() * 1.45);
        return big;
    }());
    input_->setPlaceholderText(eva ? "Title" : "Page name");
    input_->setFrameShape(QFrame::NoFrame);
    input_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    input_->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    input_->setWordWrapMode(QTextOption::WordWrap);
    input_->setTabChangesFocus(true);
    input_->document()->setDocumentMargin(0);
    input_->installEventFilter(this);
    auto* titleRow = new QHBoxLayout();
    titleRow->setContentsMargins(0, 0, 0, 0);
    titleRow->addWidget(input_, eva ? 3 : 1);
    if (eva) titleRow->addStretch(2);  // the box keeps to the left ~60%
    body()->addLayout(titleRow);
    body()->addSpacing(eva ? 8 : 12);

    title_ = new QLabel(eva ? QString("EPISODE:%1").arg(episode) : QString("New page"), card());
    QFont ep = th.bodyFont();
    ep.setWeight(QFont::Bold);
    ep.setPointSizeF(ep.pointSizeF() * (eva ? 1.25 : 1.0));
    title_->setFont(eva ? ep : th.labelFont());
    if (eva) body()->addWidget(title_);
    else body()->insertWidget(0, title_), body()->insertSpacing(1, 12);
    body()->addSpacing(eva ? 4 : 6);

    caption_ = new QLineEdit(card());
    caption_->setFont(eva ? cardCaptionFont(0.78) : th.bodyFont());
    caption_->setPlaceholderText(eva ? "Caption, set to the right" : "Description (optional)");
    caption_->setFrame(false);
    caption_->setAlignment(eva ? Qt::AlignRight : Qt::AlignLeft);
    caption_->installEventFilter(this);
    body()->addWidget(caption_);
    body()->addSpacing(14);

    bookLabel_ = new QLabel(card());  // the book it goes into
    bookLabel_->setFont(th.labelFont());
    body()->addWidget(bookLabel_);
    if (books_.size() < 2 && books_.isEmpty()) bookLabel_->hide();
    body()->addSpacing(8);

    hint_ = new QLabel(card());
    hint_->setFont(th.labelFont());
    body()->addWidget(hint_);
    showBook();

    connect(input_, &QPlainTextEdit::textChanged, this, [this] {
        updateHint();
        fitTitle();
    });
    connect(caption_, &QLineEdit::returnPressed, this, &PagePrompt::accept);
    updateHint();
    fitTitle();
    finish();
    input_->setFocus();
}

QString PagePrompt::titleText() const { return input_->toPlainText().simplified(); }  // no line breaks in a name

void PagePrompt::fitTitle() {
    // A plain-text document measures its height in lines, not pixels.
    const QFontMetrics fm(input_->font());
    const int lines = qMax(1, static_cast<int>(std::ceil(input_->document()->size().height())));
    const int h = lines * fm.lineSpacing() + 8;
    if (input_->height() != h) {
        input_->setFixedHeight(h);
        // The layouts take the new height on the next pass of the event loop: place the card after.
        QTimer::singleShot(0, this, [this] {
            if (isVisible()) place();
        });
    }
}

void PagePrompt::showBook() {
    if (books_.isEmpty()) return;
    const bool many = books_.size() > 1;
    bookLabel_->setText(QString("Book   %1%2%3").arg(many ? "\xE2\x80\xB9  " : "", books_[book_].first, many ? "  \xE2\x80\xBA" : ""));
}

void PagePrompt::stepBook(int delta) {
    if (books_.size() < 2) return;
    book_ = (book_ + delta + static_cast<int>(books_.size())) % static_cast<int>(books_.size());
    showBook();
}

void PagePrompt::accept() {
    const QString name = titleText();
    if (name.isEmpty()) {
        input_->setFocus();
        return;
    }
    const QString caption = caption_->text().trimmed();
    const QString dir = books_.isEmpty() ? QString() : books_[book_].second;
    dismiss();
    emit accepted(name, caption, dir);
}

bool PagePrompt::eventFilter(QObject* obj, QEvent* ev) {
    if ((obj == input_ || obj == caption_) && ev->type() == QEvent::KeyPress) {
        const int key = static_cast<QKeyEvent*>(ev)->key();
        if ((static_cast<QKeyEvent*>(ev)->modifiers() & Qt::AltModifier) && (key == Qt::Key_Left || key == Qt::Key_Right)) {
            stepBook(key == Qt::Key_Left ? -1 : 1);  // Alt+←/→: which book it goes into
            return true;
        }
        if (key == Qt::Key_Tab || key == Qt::Key_Backtab) {  // between the two fields
            if (obj == input_) caption_->setFocus();
            else input_->setFocus();
            return true;
        }
        if (obj == input_ && (key == Qt::Key_Return || key == Qt::Key_Enter)) {
            // Enter never breaks the title: an existing page opens, a new one goes on to its caption.
            if (exists_ && exists_(titleText())) accept();
            else if (!titleText().isEmpty()) caption_->setFocus();
            return true;
        }
    }
    return Overlay::eventFilter(obj, ev);
}

void PagePrompt::updateHint() {
    const QString name = titleText();
    if (name.isEmpty()) hint_->setText("Esc  cancel");
    else if (exists_ && exists_(name)) hint_->setText("Enter  open existing page     Esc  cancel");
    else hint_->setText(QString("Enter  caption, then create     Tab  fields%1     Esc  cancel")
                            .arg(books_.size() > 1 ? "     Alt+\xE2\x86\x90 \xE2\x86\x92  book" : ""));
}

void PagePrompt::paintCard(QPainter& p, const QRect& c) {
    if (!Theme::instance().immersion()) {
        Overlay::paintCard(p, c);
        return;
    }
    p.fillRect(c, kCardBlack);  // a title card: black, no frame
}

void PagePrompt::restyle() {
    const Theme& th = Theme::instance();
    const bool eva = th.immersion();
    const QColor ink = eva ? kCardInk : th.text();
    QColor faint = eva ? QColor(0x8a, 0x88, 0x84) : th.faint();
    title_->setStyleSheet(colorCss(eva ? ink : th.accent()));
    hint_->setStyleSheet(colorCss(faint));
    if (bookLabel_) bookLabel_->setStyleSheet(colorCss(eva ? QColor(0xc9, 0xc6, 0xc0) : th.dim()));
    const QString field = QString("QLineEdit { background: transparent; color: %1; border: none;"
                                  " border-bottom: 1px solid %2; padding: 2px 0 6px 0;"
                                  " selection-background-color: %3; }");
    QColor rule = eva ? QColor(255, 255, 255, 40) : th.line();
    input_->setStyleSheet(QString("QPlainTextEdit { background: transparent; color: %1; border: none;"
                                  " border-bottom: 1px solid %2; selection-background-color: %3; }")
                              .arg(rgba(ink), rgba(rule), rgba(th.selection())));
    caption_->setStyleSheet(field.arg(rgba(eva ? QColor(0xc9, 0xc6, 0xc0) : th.dim()), rgba(eva ? QColor(0, 0, 0, 0) : th.line()),
                                      rgba(th.selection())));
}

// ------------------------------------------------------------------ Eyecatch

Eyecatch::Eyecatch(QWidget* host, const QString& headline, const QString& detail)
    : QWidget(host), headline_(headline), detail_(detail) {
    setGeometry(host->rect());
    setFocusPolicy(Qt::NoFocus);
    qApp->installEventFilter(this);
    frame_.setInterval(33);
    connect(&frame_, &QTimer::timeout, this, [this] {
        opacity_ = qMin<qreal>(1.0, clock_.elapsed() / 220.0);  // fade in, then hold
        update();
        if (opacity_ >= 1.0) frame_.stop();
    });
    clock_.start();
    frame_.start();
    show();
    raise();
}

void Eyecatch::finish() {
    frame_.stop();
    qApp->removeEventFilter(this);
    hide();
    deleteLater();
}

bool Eyecatch::eventFilter(QObject*, QEvent* ev) {
    if (ev->type() == QEvent::KeyPress && isVisible()) {
        finish();
        return true;  // this key only dismisses the card
    }
    return false;
}

void Eyecatch::mousePressEvent(QMouseEvent*) { finish(); }

void Eyecatch::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setOpacity(opacity_);
    p.fillRect(rect(), kCardBlack);
    const QFont small = cardTitleFont(1.05), big = cardTitleFont(3.0);
    QFont sans = Theme::instance().bodyFont();
    sans.setWeight(QFont::Bold);
    sans.setPointSizeF(sans.pointSizeF() * 1.3);
    const QFontMetrics sm(small), bm(big), am(sans), dm(Theme::instance().bodyFont());
    const int x = width() / 7;
    const int blockH = sm.height() + bm.height() + 22 + am.height() + 6 + dm.height();
    int y = (height() - blockH) / 2;
    p.setPen(kCardInk);
    p.setFont(small);
    p.drawText(QPointF(x, y + sm.ascent()), "NEON GENESIS");
    y += sm.height() - 6;
    p.setFont(big);
    p.drawText(QPointF(x, y + bm.ascent()), "EVANGELION");
    y += bm.height() + 22;
    p.setFont(sans);
    p.drawText(QPointF(x, y + am.ascent()), headline_);
    y += am.height() + 6;
    p.setFont(Theme::instance().bodyFont());
    p.setPen(QColor(0xc9, 0xc6, 0xc0));
    p.drawText(QPointF(x, y + dm.ascent()), detail_);
    p.setFont(Theme::instance().labelFont());
    p.setPen(QColor(0x8a, 0x88, 0x84));
    p.drawText(QRect(0, height() - 60, width(), 30), Qt::AlignHCenter | Qt::AlignVCenter, "Click or press a key to continue");
}

// ---------------------------------------------------------------- NamePrompt

NamePrompt::NamePrompt(QWidget* host, const QString& title, const QString& placeholder) : Overlay(host, 460) {
    const Theme& th = Theme::instance();
    title_ = new QLabel(title, card());
    title_->setFont(th.labelFont());
    body()->addWidget(title_);
    body()->addSpacing(14);
    input_ = new QLineEdit(card());
    QFont big = th.bodyFont();
    big.setPointSizeF(big.pointSizeF() * 1.4);
    input_->setFont(big);
    input_->setPlaceholderText(placeholder);
    input_->setFrame(false);
    body()->addWidget(input_);
    body()->addSpacing(12);
    hint_ = new QLabel("Enter  create     Esc  cancel", card());
    hint_->setFont(th.labelFont());
    body()->addWidget(hint_);
    connect(input_, &QLineEdit::returnPressed, this, [this] {
        const QString name = input_->text().trimmed();
        if (name.isEmpty()) return;
        dismiss();
        emit accepted(name);
    });
    finish();
    input_->setFocus();
}

void NamePrompt::restyle() {
    const Theme& th = Theme::instance();
    title_->setStyleSheet(colorCss(th.accent()));
    hint_->setStyleSheet(colorCss(th.faint()));
    input_->setStyleSheet(QString("QLineEdit { background: transparent; color: %1; border: none;"
                                  " border-bottom: 1px solid %2; padding: 2px 0 8px 0; selection-background-color: %3; }")
                              .arg(rgba(th.text()), rgba(th.line()), rgba(th.selection())));
}

// ---------------------------------------------------------------- TitleFlash

TitleFlash::TitleFlash(QWidget* host, int episode, const QString& title, const QString& caption)
    : QWidget(host), episode_(QString("EPISODE:%1").arg(episode)), title_(title), caption_(caption) {
    setGeometry(host->rect());
    setFocusPolicy(Qt::NoFocus);
    qApp->installEventFilter(this);  // any key ends it (and still goes through); follows the window's size
    frame_.setInterval(33);
    connect(&frame_, &QTimer::timeout, this, [this] {
        const qint64 t = clock_.elapsed();
        constexpr qint64 kIn = 160, kHold = 1250, kOut = 380;
        opacity_ = t < kIn ? t / qreal(kIn) : t < kIn + kHold ? 1.0 : 1.0 - (t - kIn - kHold) / qreal(kOut);
        if (opacity_ <= 0) {
            finish();
            return;
        }
        update();
    });
    clock_.start();
    frame_.start();
    show();
    raise();
}

void TitleFlash::finish() {
    frame_.stop();
    qApp->removeEventFilter(this);
    hide();
    deleteLater();
}

bool TitleFlash::eventFilter(QObject* obj, QEvent* ev) {
    if (ev->type() == QEvent::KeyPress && isVisible()) finish();
    if (obj == parentWidget() && ev->type() == QEvent::Resize) setGeometry(parentWidget()->rect());
    return false;
}

void TitleFlash::mousePressEvent(QMouseEvent*) { finish(); }

void TitleFlash::paintEvent(QPaintEvent*) {
    // As on the episode cards: the title stacked large in a box on the left, then
    // "EPISODE:N" in a bold sans, then the caption set to the right of the block.
    QPainter p(this);
    p.setOpacity(qBound<qreal>(0, opacity_, 1));
    p.fillRect(rect(), kCardBlack);
    // The block (title, EPISODE, caption) is centred in the window; in a small window the
    // title shrinks until its longest word fits.
    const int margin = qMax(16, width() / 14);
    const int room = qMax(40, width() - 2 * margin);
    QFont big = cardTitleFont(2.1);
    for (qreal s = 2.1; s > 0.8; s -= 0.1) {
        big = cardTitleFont(s);
        const QFontMetrics fm(big);
        int longest = 0;
        for (const QString& word : title_.split(' ', Qt::SkipEmptyParts)) longest = qMax(longest, fm.horizontalAdvance(word));
        if (longest <= room && fm.height() * 3 <= height() / 2) break;
    }
    const QFont cap = cardCaptionFont(0.95);
    QFont ep = Theme::instance().bodyFont();
    ep.setWeight(QFont::Bold);
    ep.setPointSizeF(ep.pointSizeF() * 1.6);
    const QFontMetrics bm(big), em(ep), cm(cap);
    const int boxW = qMin(room, qMax(bm.horizontalAdvance("MMMMMM"), width() * 11 / 20));
    const QRect titleBox = bm.boundingRect(QRect(0, 0, boxW, 10000), Qt::TextWordWrap, title_);
    // The caption ends at the block's right edge and starts at least a quarter of the title in.
    const int titleW = qMax(titleBox.width(), em.horizontalAdvance(episode_));
    const int capFull = caption_.isEmpty() ? 0 : cm.horizontalAdvance(caption_);
    const int blockW = qMin(room, qMax(titleW, capFull ? capFull + titleW / 4 : 0));
    const int x = (width() - blockW) / 2;
    // A caption longer than the window allows wraps under the title instead.
    const bool capWraps = capFull > blockW;
    const int capH = caption_.isEmpty() ? 0
                     : capWraps ? cm.boundingRect(QRect(0, 0, blockW, 10000), Qt::TextWordWrap, caption_).height()
                                : cm.height();
    const int blockH = titleBox.height() + 10 + em.height() + (caption_.isEmpty() ? 0 : 4 + capH);
    int y = (height() - blockH) / 2;
    p.setPen(kCardInk);
    p.setFont(big);
    p.drawText(QRect(x, y, blockW, titleBox.height()), Qt::AlignLeft | Qt::AlignTop | Qt::TextWordWrap, title_);
    y += titleBox.height() + 10;
    p.setFont(ep);
    p.drawText(QRect(x, y, width() - x, em.height()), Qt::AlignLeft | Qt::AlignTop, episode_);
    y += em.height() + 4;
    if (!caption_.isEmpty()) {
        p.setFont(cap);
        // Right edge at the block's right edge, as "Take care of yourself." sits under EVANGELION.
        if (capWraps) {
            p.drawText(QRect(x, y, blockW, capH), Qt::AlignLeft | Qt::AlignTop | Qt::TextWordWrap, caption_);
        } else {
            const int capW = qMin(capFull, blockW - qMin(titleW / 4, blockW - capFull));
            p.drawText(QRect(x + blockW - capW, y, capW, cm.height()), Qt::AlignLeft | Qt::AlignTop, caption_);
        }
    }
}

// ----------------------------------------------------------------- KeySheet

KeySheet::KeySheet(QWidget* host, const QList<Section>& sections, const QList<QPair<QString, QString>>& snippets)
    : Overlay(host, 880) {
    const Theme& th = Theme::instance();
    auto* tabs = new QHBoxLayout();
    tabs->setSpacing(22);
    for (int t : {0, 1}) {
        tab_[t] = new QLabel(t == 0 ? "Keys" : "Snippets", card());
        tab_[t]->setFont(th.labelFont());
        tabs->addWidget(tab_[t]);
    }
    tabs->addStretch(1);
    body()->addLayout(tabs);
    body()->addSpacing(16);

    page_[0] = new QWidget(card());
    auto* columns = new QHBoxLayout(page_[0]);
    columns->setContentsMargins(0, 0, 0, 0);
    columns->setSpacing(36);
    for (const Section& s : sections) {
        auto* grid = new QGridLayout();
        grid->setHorizontalSpacing(16);
        grid->setVerticalSpacing(7);
        auto* heading = new QLabel(s.name, card());
        heading->setFont(th.labelFont());
        headings_ << heading;
        grid->addWidget(heading, 0, 0, 1, 2);
        grid->setRowMinimumHeight(0, 26);
        int row = 1;
        for (const auto& [combo, what] : s.keys) {
            auto* capsRow = new QHBoxLayout();
            capsRow->setSpacing(4);
            for (const QString& part : combo.split('+')) {
                auto* cap = new QLabel(part.trimmed(), card());
                const bool arrows = part.contains(QChar(0x2191));  // the label face draws ↑↓ too small
                QFont arrowFont = th.monoFont();
                arrowFont.setPointSizeF(9 * th.scale());
                cap->setFont(arrows ? arrowFont : th.labelFont());
                cap->setFixedHeight(QFontMetrics(th.labelFont()).height() + 9);
                cap->setAlignment(Qt::AlignCenter);
                caps_ << cap;
                capsRow->addWidget(cap);
            }
            capsRow->addStretch(1);
            grid->addLayout(capsRow, row, 0);
            auto* text = new QLabel(what, card());
            text->setFont(th.bodyFont());
            text->setWordWrap(true);
            texts_ << text;
            grid->addWidget(text, row, 1);
            ++row;
        }
        grid->setColumnStretch(1, 1);
        grid->setRowStretch(row, 1);
        columns->addLayout(grid, 1);
    }
    body()->addWidget(page_[0]);

    // Snippets, three columns of  trigger  result.
    page_[1] = new QWidget(card());
    auto* sgrid = new QGridLayout(page_[1]);
    sgrid->setContentsMargins(0, 0, 0, 0);
    sgrid->setHorizontalSpacing(14);
    sgrid->setVerticalSpacing(6);
    const int perCol = (static_cast<int>(snippets.size()) + 2) / 3;
    for (int i = 0; i < snippets.size(); ++i) {
        const int col = (i / qMax(1, perCol)) * 2, row = i % qMax(1, perCol);
        const QString result = snippets[i].second.section('\t', 0, 0);
        const QString what = snippets[i].second.section('\t', 1);
        auto* trig = new QLabel(snippets[i].first, card());
        trig->setFont(th.monoFont());
        trig->setToolTip(what);
        triggers_ << trig;
        auto* give = new QLabel(card());
        give->setToolTip(what);
        // TeX results typeset, the rest as text.
        QString tex = result;
        if (tex.startsWith('$') && tex.endsWith('$') && !tex.startsWith("$$")) tex = tex.mid(1, tex.size() - 2);
        tex.replace("{}", "{\\square}");
        const bool chem = result.contains("\\ce{") || result.contains("\\pu{");  // an empty \ce says nothing typeset
        const QImage img = tex.contains('\\') && !result.startsWith("$$") && !chem
                               ? MathObject::image(tex, QFontInfo(th.bodyFont()).pixelSize() * 1.05, th.text(), false, devicePixelRatioF())
                               : QImage();
        if (!img.isNull()) give->setPixmap(QPixmap::fromImage(img));
        else if (chem) give->setText(QString(result).replace("{}", "{\xE2\x80\xA6}"));
        else give->setText(result.startsWith("$$") ? QString("$$ \xE2\x80\xA6 $$") : result);
        give->setFont(th.bodyFont());
        texts_ << give;
        sgrid->addWidget(trig, row, col);
        sgrid->addWidget(give, row, col + 1);
    }
    sgrid->setColumnStretch(1, 1);
    sgrid->setColumnStretch(3, 1);
    sgrid->setColumnStretch(5, 1);
    body()->addWidget(page_[1]);

    body()->addSpacing(18);
    footer_ = new QLabel(snippets.isEmpty() ? "Esc  close" : "Tab  keys / snippets     Esc  close", card());
    footer_->setFont(th.labelFont());
    body()->addWidget(footer_);
    if (snippets.isEmpty()) tab_[1]->hide();
    showPage(0);
    finish();
}

void KeySheet::showPage(int page) {
    current_ = page;
    page_[0]->setVisible(page == 0);
    page_[1]->setVisible(page == 1);
    restyle();
    if (isVisible()) place();
}

bool KeySheet::focusNextPrevChild(bool) {
    if (!tab_[1]->isHidden()) showPage(1 - current_);
    return true;
}

void KeySheet::restyle() {
    const Theme& th = Theme::instance();
    for (QLabel* l : headings_) l->setStyleSheet(colorCss(th.accent()));
    for (QLabel* l : texts_) l->setStyleSheet(colorCss(th.text()));
    for (QLabel* l : triggers_) l->setStyleSheet(colorCss(th.accent()));
    for (int t : {0, 1}) tab_[t]->setStyleSheet(colorCss(t == current_ ? th.accent() : th.faint()));
    for (QLabel* l : caps_)
        l->setStyleSheet(QString("color: %1; background: transparent; border: 1px solid %2;"
                                 " border-radius: 3px; padding: 0 6px;")
                             .arg(rgba(th.dim()), rgba(th.line())));
    footer_->setStyleSheet(colorCss(th.faint()));
}

// ------------------------------------------------------------ QuickSwitcher

namespace {
// One result row: a page (square marker + title), a block (its text, the page on the
// right), or "Create page".
class ResultDelegate : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;
    QSize sizeHint(const QStyleOptionViewItem&, const QModelIndex&) const override {
        return QSize(0, QFontMetrics(Theme::instance().bodyFont()).lineSpacing() + 12);
    }
    void paint(QPainter* p, const QStyleOptionViewItem& opt, const QModelIndex& idx) const override {
        const Theme& th = Theme::instance();
        const int kind = idx.data(Qt::UserRole).toInt();
        const QString primary = idx.data(Qt::DisplayRole).toString();
        const QString secondary = idx.data(Qt::UserRole + 1).toString();
        const bool selected = opt.state & QStyle::State_Selected;
        const QRect r = opt.rect;
        p->save();
        p->setRenderHint(QPainter::Antialiasing);
        if (selected) {
            QColor tint = th.structure();
            tint.setAlpha(52);
            p->setPen(Qt::NoPen);
            p->setBrush(tint);
            p->drawRoundedRect(QRectF(r).adjusted(0, 1, 0, -1), 3, 3);
            p->fillRect(QRect(r.left(), r.top() + 1, 2, r.height() - 2), th.accent());
        }
        const QColor mark = kind == QuickSwitcher::Result::Block ? th.faint() : th.structure();
        if (kind == QuickSwitcher::Result::Block) {
            p->setPen(QPen(mark, 1.2));
            p->setBrush(Qt::NoBrush);
            p->drawEllipse(QPointF(r.left() + 14, r.center().y() + 0.5), 2.6, 2.6);
        } else {
            p->fillRect(QRectF(r.left() + 12, r.center().y() - 1.5, 4, 4),
                        kind == QuickSwitcher::Result::Create ? th.accent() : mark);
        }
        QRect text = r.adjusted(28, 0, -10, 0);
        if (!secondary.isEmpty() && kind != QuickSwitcher::Result::Create) {
            p->setFont(th.labelFont());
            const QFontMetrics lm(th.labelFont());
            const QString s = lm.elidedText(secondary, Qt::ElideRight, text.width() / 3);
            p->setPen(th.faint());
            p->drawText(text, Qt::AlignVCenter | Qt::AlignRight, s);
            text.setRight(text.right() - lm.horizontalAdvance(s.toUpper()) - 16);
        }
        p->setFont(th.bodyFont());
        p->setPen(kind == QuickSwitcher::Result::Create ? th.accent()
                  : selected                            ? th.text()
                  : kind == QuickSwitcher::Result::Block ? th.dim()
                                                         : th.text());
        p->drawText(text, Qt::AlignVCenter | Qt::AlignLeft,
                    QFontMetrics(th.bodyFont()).elidedText(primary, Qt::ElideRight, text.width()));
        p->restore();
    }
};
}  // namespace

QuickSwitcher::QuickSwitcher(QWidget* host, std::function<QList<Result>(const QString&)> search)
    : Overlay(host, 640), search_(std::move(search)) {
    const Theme& th = Theme::instance();
    title_ = new QLabel("Search", card());
    title_->setFont(th.labelFont());
    body()->addWidget(title_);
    body()->addSpacing(14);

    input_ = new QLineEdit(card());
    QFont big = th.bodyFont();
    big.setPointSizeF(big.pointSizeF() * 1.3);
    input_->setFont(big);
    input_->setPlaceholderText("Pages and notes");
    input_->setFrame(false);
    input_->installEventFilter(this);
    body()->addWidget(input_);
    body()->addSpacing(8);

    list_ = new QListWidget(card());
    list_->setFrameShape(QFrame::NoFrame);
    list_->setFocusPolicy(Qt::NoFocus);
    list_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    list_->setUniformItemSizes(true);
    list_->setItemDelegate(new ResultDelegate(list_));
    body()->addWidget(list_);
    body()->addSpacing(12);

    hint_ = new QLabel("\xE2\x86\x91 \xE2\x86\x93  choose     Enter  open     Esc  close", card());
    hint_->setFont(th.labelFont());
    body()->addWidget(hint_);

    connect(input_, &QLineEdit::textChanged, this, &QuickSwitcher::refresh);
    auto open = [this] {
        const int row = list_->currentRow();
        if (row < 0 || row >= results_.size()) return;
        const Result r = results_[row];
        dismiss();
        emit chosen(r);
    };
    connect(input_, &QLineEdit::returnPressed, this, open);
    connect(list_, &QListWidget::itemClicked, this, open);
    refresh(QString());
    finish();
    input_->setFocus();
}

void QuickSwitcher::refresh(const QString& text) {
    results_ = search_ ? search_(text) : QList<Result>();
    list_->clear();
    for (const Result& r : results_) {
        auto* item = new QListWidgetItem(r.primary, list_);
        item->setData(Qt::UserRole, static_cast<int>(r.kind));
        item->setData(Qt::UserRole + 1, r.secondary);
    }
    if (!results_.isEmpty()) list_->setCurrentRow(0);
    // The card hugs its results (up to ten rows, then the list scrolls).
    const int rowH = QFontMetrics(Theme::instance().bodyFont()).lineSpacing() + 12;
    list_->setFixedHeight(rowH * qBound(1, static_cast<int>(results_.size()), 10) + 4);
    if (isVisible()) place();
}

bool QuickSwitcher::eventFilter(QObject* obj, QEvent* ev) {
    if (obj == input_ && ev->type() == QEvent::KeyPress) {
        const int key = static_cast<QKeyEvent*>(ev)->key();
        if (key == Qt::Key_Up || key == Qt::Key_Down || key == Qt::Key_PageUp || key == Qt::Key_PageDown) {
            QApplication::sendEvent(list_, ev);
            return true;
        }
    }
    return Overlay::eventFilter(obj, ev);
}

void QuickSwitcher::restyle() {
    const Theme& th = Theme::instance();
    title_->setStyleSheet(colorCss(th.accent()));
    hint_->setStyleSheet(colorCss(th.faint()));
    input_->setStyleSheet(QString("QLineEdit { background: transparent; color: %1; border: none;"
                                  " border-bottom: 1px solid %2; padding: 2px 0 8px 0;"
                                  " selection-background-color: %3; }")
                              .arg(rgba(th.text()), rgba(th.line()), rgba(th.selection())));
    list_->setStyleSheet("QListWidget { background: transparent; outline: none; border: none; }");
    list_->viewport()->update();
}

// --------------------------------------------------------------- TasksPanel

namespace {
// Section heading ("OVERDUE  2") or a task: [TODO] text ........ page  OCT 30
class TaskDelegate : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;
    QSize sizeHint(const QStyleOptionViewItem&, const QModelIndex& idx) const override {
        const int line = QFontMetrics(Theme::instance().bodyFont()).lineSpacing();
        return QSize(0, idx.data(Qt::UserRole).toBool() ? line + 14 : line + 12);
    }
    void paint(QPainter* p, const QStyleOptionViewItem& opt, const QModelIndex& idx) const override {
        const Theme& th = Theme::instance();
        const QRect r = opt.rect;
        p->save();
        p->setRenderHint(QPainter::Antialiasing);
        const QFont label = th.labelFont();
        const QFontMetrics lm(label);
        if (idx.data(Qt::UserRole).toBool()) {  // section heading
            p->setFont(label);
            p->setPen(th.faint());
            p->drawText(r.adjusted(4, 0, 0, -3), Qt::AlignLeft | Qt::AlignBottom, idx.data(Qt::DisplayRole).toString());
            p->restore();
            return;
        }
        const bool selected = opt.state & QStyle::State_Selected;
        if (selected) {
            QColor tint = th.structure();
            tint.setAlpha(52);
            p->setPen(Qt::NoPen);
            p->setBrush(tint);
            p->drawRoundedRect(QRectF(r).adjusted(0, 1, 0, -1), 3, 3);
            p->fillRect(QRect(r.left(), r.top() + 1, 2, r.height() - 2), th.accent());
        }
        // A subtask hangs off the task above it:  ├─ [TODO] plot the curves
        const int level = idx.data(Qt::UserRole + 6).toInt();
        int x0 = r.left() + 10;
        if (level > 0) {
            const int lx = x0 + 6 + (level - 1) * 22;
            p->setPen(QPen(th.faint(), 1));
            p->drawLine(lx, r.top(), lx, idx.data(Qt::UserRole + 7).toBool() ? r.center().y() : r.bottom());
            p->drawLine(lx, r.center().y(), lx + 10, r.center().y());
            x0 = lx + 16;
        }
        // Marker chip
        const QString marker = idx.data(Qt::UserRole + 1).toString();
        const bool active = marker == "DOING" || marker == "NOW";
        const QColor chip = active ? th.accent() : th.warm();
        const int chipW = lm.horizontalAdvance(marker) + 12;
        const QRectF chipR(x0, r.center().y() - lm.height() / 2.0 - 1, chipW, lm.height() + 2);
        QColor fill = chip;
        fill.setAlpha(36);
        p->setPen(Qt::NoPen);
        p->setBrush(fill);
        p->drawRoundedRect(chipR, 3, 3);
        p->setFont(label);
        p->setPen(chip);
        p->drawText(chipR, Qt::AlignCenter, marker);

        // Right side: page, then date
        QRect text = r.adjusted(static_cast<int>(chipR.right()) - r.left() + 10, 0, -10, 0);
        const QString date = idx.data(Qt::UserRole + 2).toString();
        const qint64 left = idx.data(Qt::UserRole + 10).toLongLong();
        if (!date.isEmpty() && left < (qint64(1) << 40)) {  // a countdown: amber, red in the last hour
            const QColor tone = left < 3600 ? QColor(0xff, 0x4d, 0x4d) : QColor(0xff, 0xb0, 0x00);
            QFont f = th.monoFont();
            f.setBold(true);
            const int w = QFontMetrics(f).horizontalAdvance(date) + 10;
            const QRect chipC(text.right() - w, r.center().y() - lm.height() / 2 - 1, w, lm.height() + 2);
            QColor bg = tone;
            bg.setAlpha(36);
            p->fillRect(chipC, bg);
            p->setFont(f);
            p->setPen(tone);
            p->drawText(chipC, Qt::AlignCenter, date);
            p->setFont(label);
            text.setRight(chipC.left() - 14);
        } else if (!date.isEmpty()) {
            p->setPen(idx.data(Qt::UserRole + 4).toBool() ? th.warm() : th.dim());
            p->drawText(text, Qt::AlignVCenter | Qt::AlignRight, date);
            text.setRight(text.right() - lm.horizontalAdvance(date.toUpper()) - 14);
        }
        // A task with subtasks: its sync ratio.  SYNC ▮▮▯▯▯ 2/5
        const double ratio = idx.data(Qt::UserRole + 9).toDouble();
        if (ratio >= 0) {
            const QString count = idx.data(Qt::UserRole + 8).toString();
            p->setPen(th.dim());
            p->drawText(text, Qt::AlignVCenter | Qt::AlignRight, count);
            int right = text.right() - lm.horizontalAdvance(count) - 8;
            const int cells = 5, cw = 6, gap = 2, cy = r.center().y();
            const int lit = static_cast<int>(std::round(cells * ratio));
            for (int c = 0; c < cells; ++c) {
                const QRect cell(right - (cells - c) * (cw + gap), cy - 4, cw, 9);
                if (c < lit) p->fillRect(cell, th.accent());
                else {
                    p->setPen(QPen(th.faint(), 1));
                    p->setBrush(Qt::NoBrush);
                    p->drawRect(cell.adjusted(0, 0, -1, -1));
                }
            }
            right -= cells * (cw + gap) + 6;
            p->setPen(th.faint());
            p->drawText(QRect(text.left(), r.top(), right - text.left(), r.height()), Qt::AlignVCenter | Qt::AlignRight, "SYNC");
            text.setRight(right - lm.horizontalAdvance("SYNC") - 14);
        }
        const QString page = lm.elidedText(idx.data(Qt::UserRole + 3).toString(), Qt::ElideRight, text.width() / 3);
        p->setPen(th.faint());
        p->drawText(text, Qt::AlignVCenter | Qt::AlignRight, page);
        text.setRight(text.right() - lm.horizontalAdvance(page.toUpper()) - 16);

        p->setFont(th.bodyFont());
        p->setPen(selected ? th.text() : th.dim());
        p->drawText(text, Qt::AlignVCenter | Qt::AlignLeft,
                    QFontMetrics(th.bodyFont()).elidedText(idx.data(Qt::DisplayRole).toString(), Qt::ElideRight, text.width()));
        p->restore();
    }
};
}  // namespace

TasksPanel::TasksPanel(QWidget* host, Fetch fetch, Complete complete)
    : Overlay(host, 720), fetch_(std::move(fetch)), complete_(std::move(complete)) {
    const Theme& th = Theme::instance();
    title_ = new QLabel("Tasks", card());
    title_->setFont(th.labelFont());
    body()->addWidget(title_);
    body()->addSpacing(14);

    input_ = new QLineEdit(card());
    input_->setFont(th.bodyFont());
    input_->setPlaceholderText("Filter tasks");
    input_->setFrame(false);
    input_->installEventFilter(this);
    body()->addWidget(input_);
    body()->addSpacing(8);

    list_ = new QListWidget(card());
    list_->setFrameShape(QFrame::NoFrame);
    list_->setFocusPolicy(Qt::NoFocus);
    list_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    list_->setItemDelegate(new TaskDelegate(list_));
    body()->addWidget(list_);
    body()->addSpacing(12);

    hint_ = new QLabel("\xE2\x86\x91 \xE2\x86\x93  choose     Enter  open     Ctrl+Enter  done     Alt+= / \xE2\x88\x92  progress     Esc  close", card());
    hint_->setFont(th.labelFont());
    body()->addWidget(hint_);

    connect(input_, &QLineEdit::textChanged, this, &TasksPanel::filter);
    connect(input_, &QLineEdit::returnPressed, this, [this] {
        const int k = selectedTask();
        if (k < 0) return;
        const Row r = rows_[k];
        dismiss();
        emit open(r.path, r.block);
    });
    connect(list_, &QListWidget::itemDoubleClicked, input_, &QLineEdit::returnPressed);
    reload();
    finish();
    input_->setFocus();
}

void TasksPanel::reload() {
    rows_ = fetch_ ? fetch_() : QList<Row>();
    list_->clear();
    for (int k = 0; k < rows_.size(); ++k) {
        const Row& r = rows_[k];
        auto* item = new QListWidgetItem(r.section.isEmpty() ? r.text : r.section, list_);
        item->setData(Qt::UserRole, !r.section.isEmpty());
        item->setData(Qt::UserRole + 1, r.marker);
        item->setData(Qt::UserRole + 2, r.due.isValid() ? QLocale::c().toString(r.due, "MMM d") : QString());
        item->setData(Qt::UserRole + 3, r.page);
        item->setData(Qt::UserRole + 4, r.late);
        item->setData(Qt::UserRole + 5, k);
        item->setData(Qt::UserRole + 6, r.level);
        item->setData(Qt::UserRole + 7, r.lastSibling);
        item->setData(Qt::UserRole + 8, r.subTotal ? QString("%1/%2").arg(r.subDone).arg(r.subTotal) : QString());
        item->setData(Qt::UserRole + 9, r.subTotal ? double(r.subDone) / r.subTotal : -1.0);
        if (r.at.isValid()) item->setData(Qt::UserRole + 2, countdownText(r.at));
        item->setData(Qt::UserRole + 10, r.at.isValid() ? QDateTime::currentDateTime().secsTo(r.at) : qint64(1) << 40);
        if (!r.section.isEmpty()) item->setFlags(Qt::NoItemFlags);
    }
    if (rows_.isEmpty()) {
        auto* item = new QListWidgetItem("Nothing to do. TODO in any block shows up here.", list_);
        item->setData(Qt::UserRole, true);
        item->setFlags(Qt::NoItemFlags);
    }
    filter(input_->text());
}

void TasksPanel::filter(const QString& text) {
    const QString needle = text.trimmed();
    // A heading stays only if a task under it does.
    int heading = -1;
    bool any = false;
    QListWidgetItem* first = nullptr;
    for (int i = 0; i < list_->count(); ++i) {
        QListWidgetItem* item = list_->item(i);
        if (item->data(Qt::UserRole).toBool()) {
            if (heading >= 0) list_->item(heading)->setHidden(!any && !needle.isEmpty());
            heading = i;
            any = false;
            continue;
        }
        const Row& r = rows_[item->data(Qt::UserRole + 5).toInt()];
        const bool show = needle.isEmpty() || r.text.contains(needle, Qt::CaseInsensitive) ||
                          r.page.contains(needle, Qt::CaseInsensitive);
        item->setHidden(!show);
        any = any || show;
        if (show && !first) first = item;
    }
    if (heading >= 0) list_->item(heading)->setHidden(!any && !needle.isEmpty());
    if (!list_->currentItem() || list_->currentItem()->isHidden() || !(list_->currentItem()->flags() & Qt::ItemIsSelectable))
        list_->setCurrentItem(first);
    int shown = 0;
    for (int i = 0; i < list_->count(); ++i) shown += !list_->item(i)->isHidden();
    const int rowH = QFontMetrics(Theme::instance().bodyFont()).lineSpacing() + 13;
    list_->setFixedHeight(rowH * qBound(2, shown, 12) + 4);
    if (isVisible()) place();
}

int TasksPanel::selectedTask() const {
    const QListWidgetItem* item = list_->currentItem();
    if (!item || !(item->flags() & Qt::ItemIsSelectable)) return -1;
    return item->data(Qt::UserRole + 5).toInt();
}

bool TasksPanel::eventFilter(QObject* obj, QEvent* ev) {
    if (obj == input_ && ev->type() == QEvent::KeyPress) {
        auto* e = static_cast<QKeyEvent*>(ev);
        const int key = e->key();
        if (key == Qt::Key_Up || key == Qt::Key_Down || key == Qt::Key_PageUp || key == Qt::Key_PageDown) {
            QApplication::sendEvent(list_, ev);
            return true;
        }
        const int stepDir = progressStepKey(e);
        if (stepDir) {
            const int k = selectedTask();
            const int row = list_->currentRow();
            if (k >= 0 && step_ && step_(rows_[k], stepDir)) {
                reload();  // the new percentage; a task that finished drops out
                list_->setCurrentRow(qMin(row, list_->count() - 1));
            }
            return true;
        }
        if ((key == Qt::Key_Return || key == Qt::Key_Enter) && (e->modifiers() & Qt::ControlModifier)) {
            const int k = selectedTask();
            const int row = list_->currentRow();
            if (k >= 0 && complete_ && complete_(rows_[k])) {
                reload();  // the done task drops out; stay where it was
                for (int i = qMin(row, list_->count() - 1); i >= 0; --i)
                    if (!list_->item(i)->isHidden() && (list_->item(i)->flags() & Qt::ItemIsSelectable)) {
                        list_->setCurrentRow(i);
                        break;
                    }
            }
            return true;
        }
    }
    return Overlay::eventFilter(obj, ev);
}

void TasksPanel::restyle() {
    const Theme& th = Theme::instance();
    title_->setStyleSheet(colorCss(th.accent()));
    hint_->setStyleSheet(colorCss(th.faint()));
    input_->setStyleSheet(QString("QLineEdit { background: transparent; color: %1; border: none;"
                                  " border-bottom: 1px solid %2; padding: 2px 0 8px 0;"
                                  " selection-background-color: %3; }")
                              .arg(rgba(th.text()), rgba(th.line()), rgba(th.selection())));
    list_->setStyleSheet("QListWidget { background: transparent; outline: none; border: none; }");
    list_->viewport()->update();
}

// ------------------------------------------------------------- TimelinePanel

namespace {
enum TimelineKind { kSlot = 0, kNow = 1, kGap = 2 };
// 09:00–10:30  ●  [TODO] problem set ............... Organic
//              │
// ── NOW · 16:55 · next in 1H 05M ──────────────────────────
//              ┊  1H 30M FREE
class TimelineDelegate : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;
    QSize sizeHint(const QStyleOptionViewItem&, const QModelIndex& idx) const override {
        const int line = QFontMetrics(Theme::instance().bodyFont()).lineSpacing();
        return QSize(0, idx.data(Qt::UserRole).toInt() == kSlot ? line + 14 : line + 4);
    }
    static int timeWidth() {
        QFont f = Theme::instance().monoFont();
        f.setBold(true);
        return QFontMetrics(f).horizontalAdvance(QString::fromUtf8("00:00–00:00")) + 8;
    }
    void paint(QPainter* p, const QStyleOptionViewItem& opt, const QModelIndex& idx) const override {
        const Theme& th = Theme::instance();
        const QRect r = opt.rect;
        const int kind = idx.data(Qt::UserRole).toInt();
        const int railX = r.left() + 10 + timeWidth() + 12;
        const int cy = r.center().y();
        const bool first = idx.row() == 0, last = idx.row() == idx.model()->rowCount() - 1;
        p->save();
        p->setRenderHint(QPainter::Antialiasing);
        const QFont label = th.labelFont();
        const QFontMetrics lm(label);
        // The rail runs through every row.
        p->setPen(QPen(th.line(), 1, kind == kGap ? Qt::DotLine : Qt::SolidLine));
        p->drawLine(railX, first ? cy : r.top(), railX, last ? cy : r.bottom());
        if (kind == kNow) {
            QColor glow = th.accent();
            glow.setAlpha(60);
            p->setPen(QPen(glow, 1));
            p->drawLine(r.left() + 10, cy, r.right() - 10, cy);
            QPainterPath tri;
            tri.moveTo(railX - 8, cy - 4);
            tri.lineTo(railX - 1, cy);
            tri.lineTo(railX - 8, cy + 4);
            tri.closeSubpath();
            p->setPen(Qt::NoPen);
            p->setBrush(th.accent());
            p->drawPath(tri);
            const QString text = idx.data(Qt::DisplayRole).toString();
            const int w = lm.horizontalAdvance(text.toUpper()) + 16;
            p->fillRect(QRect(railX + 12, cy - lm.height() / 2, w, lm.height()), th.palette().bg);
            p->setFont(label);
            p->setPen(th.accent());
            p->drawText(QRect(railX + 20, r.top(), w, r.height()), Qt::AlignVCenter | Qt::AlignLeft, text);
            p->restore();
            return;
        }
        if (kind == kGap) {
            p->setFont(label);
            p->setPen(th.faint());
            p->drawText(r.adjusted(railX - r.left() + 20, 0, 0, 0), Qt::AlignVCenter | Qt::AlignLeft, idx.data(Qt::DisplayRole).toString());
            p->restore();
            return;
        }
        const bool selected = opt.state & QStyle::State_Selected;
        if (selected) {
            QColor tint = th.structure();
            tint.setAlpha(52);
            p->setPen(Qt::NoPen);
            p->setBrush(tint);
            p->drawRoundedRect(QRectF(r).adjusted(0, 1, 0, -1), 3, 3);
            p->fillRect(QRect(r.left(), r.top() + 1, 2, r.height() - 2), th.accent());
        }
        const int state = idx.data(Qt::UserRole + 2).toInt();  // 0 past, 1 running, 2 later
        const bool done = idx.data(Qt::UserRole + 3).toBool();
        const bool deadline = idx.data(Qt::UserRole + 6).toBool();
        // Time
        QFont mono = th.monoFont();
        mono.setBold(true);
        p->setFont(mono);
        p->setPen(done ? th.faint() : deadline ? th.warm() : state == 1 ? th.accent() : state == 0 ? th.dim() : th.text());
        p->drawText(QRect(r.left() + 10, r.top(), timeWidth(), r.height()), Qt::AlignVCenter | Qt::AlignRight,
                    idx.data(Qt::UserRole + 1).toString());
        // Dot: past small, running filled, later hollow; a deadline is a diamond.
        const QPointF at(railX, cy);
        if (deadline) {
            QPolygonF d{at + QPointF(0, -4.5), at + QPointF(4.5, 0), at + QPointF(0, 4.5), at + QPointF(-4.5, 0)};
            p->setPen(QPen(th.warm(), 1.3));
            p->setBrush(state == 0 ? th.warm() : th.palette().bg);
            p->drawPolygon(d);
        } else if (state == 1) {
            p->setPen(Qt::NoPen);
            p->setBrush(th.accent());
            p->drawEllipse(at, 4.0, 4.0);
        } else if (state == 0 || done) {
            p->setPen(Qt::NoPen);
            p->setBrush(th.faint());
            p->drawEllipse(at, 2.5, 2.5);
        } else {
            p->setPen(QPen(th.structure(), 1.3));
            p->setBrush(th.palette().bg);
            p->drawEllipse(at, 3.5, 3.5);
        }
        int x = railX + 18;
        QRect text = r.adjusted(0, 0, -10, 0);
        // Marker chip
        const QString marker = idx.data(Qt::UserRole + 4).toString();
        if (!marker.isEmpty()) {
            const bool active = marker == "DOING" || marker == "NOW";
            const QColor chip = done ? th.faint() : active ? th.accent() : th.warm();
            const int chipW = lm.horizontalAdvance(marker) + 12;
            const QRectF chipR(x, cy - lm.height() / 2.0 - 1, chipW, lm.height() + 2);
            QColor fill = chip;
            fill.setAlpha(36);
            p->setPen(Qt::NoPen);
            p->setBrush(fill);
            p->drawRoundedRect(chipR, 3, 3);
            p->setFont(label);
            p->setPen(chip);
            p->drawText(chipR, Qt::AlignCenter, marker);
            x = static_cast<int>(chipR.right()) + 10;
        }
        text.setLeft(x);
        // Where it sits, on the right
        const QString context = idx.data(Qt::UserRole + 5).toString();
        if (!context.isEmpty()) {
            const QString c = lm.elidedText(context, Qt::ElideRight, text.width() / 3);
            p->setFont(label);
            p->setPen(th.faint());
            p->drawText(text, Qt::AlignVCenter | Qt::AlignRight, c);
            text.setRight(text.right() - lm.horizontalAdvance(c.toUpper()) - 16);
        }
        QFont body = th.bodyFont();
        body.setStrikeOut(done);
        p->setFont(body);
        p->setPen(done ? th.faint() : selected || state != 0 ? th.text() : th.dim());
        p->drawText(text, Qt::AlignVCenter | Qt::AlignLeft,
                    QFontMetrics(body).elidedText(idx.data(Qt::DisplayRole).toString(), Qt::ElideRight, text.width()));
        p->restore();
    }
};

QString hoursText(int minutes) {
    return minutes >= 60 ? QString("%1H %2M").arg(minutes / 60).arg(minutes % 60, 2, 10, QChar('0')) : QString("%1M").arg(minutes);
}
}  // namespace

TimelinePanel::TimelinePanel(QWidget* host, const QString& title, Fetch fetch, Complete complete)
    : Overlay(host, 720), fetch_(std::move(fetch)), complete_(std::move(complete)) {
    const Theme& th = Theme::instance();
    title_ = new QLabel(title, card());
    title_->setFont(th.labelFont());
    body()->addWidget(title_);
    body()->addSpacing(14);

    list_ = new QListWidget(card());
    list_->setFrameShape(QFrame::NoFrame);
    list_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    list_->setItemDelegate(new TimelineDelegate(list_));
    list_->installEventFilter(this);
    body()->addWidget(list_);
    body()->addSpacing(12);

    hint_ = new QLabel("\xE2\x86\x91 \xE2\x86\x93  choose     Enter  open     Ctrl+Enter  done     Esc  close", card());
    hint_->setFont(th.labelFont());
    body()->addWidget(hint_);

    connect(list_, &QListWidget::itemDoubleClicked, this, [this] {
        const int k = selected();
        if (k < 0) return;
        const Row r = rows_[k];
        dismiss();
        emit open(r.path, r.block);
    });
    reload();
    finish();
    list_->setFocus();
}

void TimelinePanel::reload() {
    const int keep = list_->currentRow();
    rows_ = fetch_ ? fetch_() : QList<Row>();
    list_->clear();
    const int now = QTime::currentTime().msecsSinceStartOfDay() / 60000;
    bool nowShown = false;
    int firstLater = -1, running = -1;
    auto nowRow = [&](int next) {
        QString text = QString("Now  \xC2\xB7  ") + QTime::currentTime().toString("HH:mm");
        if (next >= 0) text += QString("  \xC2\xB7  next in ") + hoursText(next - now);
        auto* item = new QListWidgetItem(text, list_);
        item->setData(Qt::UserRole, kNow);
        item->setFlags(Qt::NoItemFlags);
        nowShown = true;
    };
    for (int k = 0; k < rows_.size(); ++k) {
        const Row& r = rows_[k];
        const bool isRunning = r.end > 0 && r.start <= now && now < r.end;
        if (!nowShown && r.start > now) nowRow(r.start);
        // Free time: after a block with an end, until the next one starts (half an hour or more).
        if (k > 0 && rows_[k - 1].end > 0 && r.start - rows_[k - 1].end >= 30) {
            auto* gap = new QListWidgetItem(hoursText(r.start - rows_[k - 1].end) + "  free", list_);
            gap->setData(Qt::UserRole, kGap);
            gap->setFlags(Qt::NoItemFlags);
        }
        auto* item = new QListWidgetItem(r.text, list_);
        item->setData(Qt::UserRole, kSlot);
        TimeSlot s;
        s.start = r.start;
        s.end = r.end;
        item->setData(Qt::UserRole + 1, slotText(s));
        item->setData(Qt::UserRole + 2, isRunning ? 1 : r.start <= now ? 0 : 2);
        item->setData(Qt::UserRole + 3, r.done);
        item->setData(Qt::UserRole + 4, r.marker);
        item->setData(Qt::UserRole + 5, r.context);
        item->setData(Qt::UserRole + 6, r.deadline);
        item->setData(Qt::UserRole + 7, k);
        if (isRunning) running = list_->count() - 1;
        if (r.start > now && firstLater < 0) firstLater = list_->count() - 1;
    }
    if (!rows_.isEmpty() && !nowShown) nowRow(-1);
    if (rows_.isEmpty()) {
        auto* item = new QListWidgetItem("No times today. Start a block in today's journal with one: 09:00 TODO problem set", list_);
        item->setData(Qt::UserRole, kGap);
        item->setFlags(Qt::NoItemFlags);
    }
    // Start on what is running, else on what is next, else the last one.
    int start = keep >= 0 && keep < list_->count() && (list_->item(keep)->flags() & Qt::ItemIsSelectable) ? keep
              : running >= 0 ? running : firstLater;
    if (start < 0)
        for (int i = list_->count() - 1; i >= 0; --i)
            if (list_->item(i)->flags() & Qt::ItemIsSelectable) {
                start = i;
                break;
            }
    if (start >= 0) list_->setCurrentRow(start);
    int h = 4;
    for (int i = 0; i < list_->count(); ++i) h += list_->sizeHintForRow(i);
    const int rowH = QFontMetrics(Theme::instance().bodyFont()).lineSpacing() + 14;
    list_->setFixedHeight(qMin(h, rowH * 14));
    if (isVisible()) place();
}

int TimelinePanel::selected() const {
    const QListWidgetItem* item = list_->currentItem();
    if (!item || !(item->flags() & Qt::ItemIsSelectable)) return -1;
    return item->data(Qt::UserRole + 7).toInt();
}

bool TimelinePanel::eventFilter(QObject* obj, QEvent* ev) {
    if (obj == list_ && ev->type() == QEvent::KeyPress) {
        auto* e = static_cast<QKeyEvent*>(ev);
        if (e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter) {
            const int k = selected();
            if (k < 0) return true;
            if (e->modifiers() & Qt::ControlModifier) {
                if (!rows_[k].done && complete_ && complete_(rows_[k])) reload();  // stays, struck through
                return true;
            }
            const Row r = rows_[k];
            dismiss();
            emit open(r.path, r.block);
            return true;
        }
        if (e->key() == Qt::Key_Escape) {
            dismiss();
            return true;
        }
    }
    return Overlay::eventFilter(obj, ev);
}

void TimelinePanel::restyle() {
    const Theme& th = Theme::instance();
    title_->setStyleSheet(colorCss(th.accent()));
    hint_->setStyleSheet(colorCss(th.faint()));
    list_->setStyleSheet("QListWidget { background: transparent; outline: none; border: none; }");
    list_->viewport()->update();
}

// -------------------------------------------------------------- ConfirmPanel

ConfirmPanel::ConfirmPanel(QWidget* host, const QString& title, const QString& message, const QString& action)
    : Overlay(host, 520) {
    const Theme& th = Theme::instance();
    title_ = new QLabel(title, card());
    title_->setFont(th.labelFont());
    body()->addWidget(title_);
    body()->addSpacing(12);
    message_ = new QLabel(message, card());
    message_->setFont(th.bodyFont());
    message_->setWordWrap(true);
    body()->addWidget(message_);
    body()->addSpacing(18);
    hint_ = new QLabel(QString("Enter  %1     Esc  keep").arg(action), card());
    hint_->setFont(th.labelFont());
    body()->addWidget(hint_);
    connect(this, &Overlay::dismissed, this, [this] { decided_ = true; });
    finish();
}

void ConfirmPanel::answer(bool yes) {
    yes_ = yes;
    decided_ = true;
    dismiss();
}

bool ConfirmPanel::exec() {
    QEventLoop loop;
    connect(this, &Overlay::dismissed, &loop, &QEventLoop::quit);
    if (!decided_) loop.exec();
    return yes_;
}

void ConfirmPanel::keyPressEvent(QKeyEvent* e) {
    if (e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter) {
        answer(true);
        return;
    }
    Overlay::keyPressEvent(e);  // Esc: dismissed, yes_ stays false
}

void ConfirmPanel::restyle() {
    const Theme& th = Theme::instance();
    title_->setStyleSheet(colorCss(QColor(0xff, 0x4d, 0x4d)));
    message_->setStyleSheet(colorCss(th.text()));
    hint_->setStyleSheet(colorCss(th.faint()));
}

// ---------------------------------------------------------------- MagiPanel

// The three MAGI, side by side; the chosen one says APPROVE, the others REJECT.
class MagiPanel::Votes : public QWidget {
public:
    explicit Votes(QWidget* parent) : QWidget(parent) { setCursor(Qt::PointingHandCursor); }
    int current = MagiPanel::Wait;
    std::function<void(int)> picked;  // a click decides
    QSize sizeHint() const override {
        const Theme& th = Theme::instance();
        return QSize(560, QFontMetrics(th.labelFont()).height() * 2 + QFontMetrics(th.bodyFont()).lineSpacing() * 2 + 44);
    }

protected:
    QRect box(int i) const {
        const int gap = 10, w = (width() - 2 * gap) / 3;
        return QRect(i * (w + gap), 0, w, height() - 1);
    }
    void paintEvent(QPaintEvent*) override {
        const Theme& th = Theme::instance();
        const bool eva = th.immersion();
        static const char* names[] = {"MELCHIOR-1", "BALTHASAR-2", "CASPER-3"};
        static const char* plain[] = {"1", "2", "3"};
        static const char* props[] = {"Overwrite with what is here", "Reload the version on disk", "Wait: decide later"};
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        const QFontMetrics lm(th.labelFont());
        for (int i = 0; i < 3; ++i) {
            const QRect r = box(i);
            const bool on = i == current;
            QColor fill = on ? th.accent() : th.structure();
            fill.setAlpha(on ? 30 : 12);
            p.fillRect(r, fill);
            p.setPen(QPen(on ? th.accent() : th.line(), on ? 2 : 1));
            p.drawRect(QRectF(r).adjusted(0.5, 0.5, -0.5, -0.5));
            p.setFont(th.labelFont());
            p.setPen(on ? th.accent() : th.dim());
            p.drawText(r.adjusted(12, 10, -12, 0), Qt::AlignLeft | Qt::AlignTop, eva ? names[i] : plain[i]);
            p.setFont(th.bodyFont());
            p.setPen(on ? th.text() : th.dim());
            p.drawText(r.adjusted(12, 12 + lm.height(), -12, -lm.height() - 12), Qt::AlignLeft | Qt::AlignTop | Qt::TextWordWrap,
                       props[i]);
            p.setFont(th.labelFont());
            p.setPen(on ? th.accent() : th.faint());
            p.drawText(r.adjusted(12, 0, -12, -10), Qt::AlignLeft | Qt::AlignBottom, on ? "Approve" : "Reject");
        }
    }
    void mousePressEvent(QMouseEvent* e) override {
        for (int i = 0; i < 3; ++i)
            if (box(i).contains(e->position().toPoint())) {
                current = i;
                update();
                if (picked) picked(i);
            }
    }
};

MagiPanel::MagiPanel(QWidget* host, const QString& fileName) : Overlay(host, 620) {
    const Theme& th = Theme::instance();
    const bool eva = th.immersion();
    title_ = new QLabel(eva ? "MAGI \xC2\xB7 Deliberation" : "File changed on disk", card());
    title_->setFont(th.labelFont());
    body()->addWidget(title_);
    body()->addSpacing(12);
    message_ = new QLabel(QString("%1 was changed by another program while you were editing it. "
                                  "Reloading drops your unsaved edits on this page.").arg(fileName), card());
    message_->setFont(th.bodyFont());
    message_->setWordWrap(true);
    body()->addWidget(message_);
    body()->addSpacing(16);
    votes_ = new Votes(card());
    votes_->picked = [this](int c) {
        result_ = static_cast<Choice>(c);
        decide();
    };
    body()->addWidget(votes_);
    body()->addSpacing(14);
    hint_ = new QLabel("\xE2\x86\x90 \xE2\x86\x92  choose     Enter  decide     Esc  wait", card());
    hint_->setFont(th.labelFont());
    body()->addWidget(hint_);
    connect(this, &Overlay::dismissed, this, [this] { decided_ = true; });
    finish();
}

void MagiPanel::choose(int c) {
    votes_->current = qBound(0, c, 2);
    result_ = static_cast<Choice>(votes_->current);
    votes_->update();
}

void MagiPanel::decide() {
    result_ = static_cast<Choice>(votes_->current);
    decided_ = true;
    dismiss();
}

MagiPanel::Choice MagiPanel::exec() {
    QEventLoop loop;
    connect(this, &Overlay::dismissed, &loop, &QEventLoop::quit);
    if (!decided_) loop.exec();
    return result_;
}

void MagiPanel::keyPressEvent(QKeyEvent* e) {
    switch (e->key()) {
        case Qt::Key_Left: choose(votes_->current - 1); return;
        case Qt::Key_Right: choose(votes_->current + 1); return;
        case Qt::Key_1:
        case Qt::Key_2:
        case Qt::Key_3: choose(e->key() - Qt::Key_1); return;
        case Qt::Key_Return:
        case Qt::Key_Enter: decide(); return;
        case Qt::Key_Escape:
            choose(Wait);
            decide();
            return;
        default: Overlay::keyPressEvent(e);
    }
}

void MagiPanel::restyle() {
    const Theme& th = Theme::instance();
    title_->setStyleSheet(colorCss(th.warm()));
    message_->setStyleSheet(colorCss(th.text()));
    hint_->setStyleSheet(colorCss(th.faint()));
    if (votes_) votes_->update();
}

// --------------------------------------------------------------- FontPicker

FontPicker::FontPicker(QWidget* host, const QString& sample) : Overlay(host, 540), pageTitle_(sample) {
    Theme& th = Theme::instance();
    original_[Title] = chosen_[Title] = th.titleFamily();
    original_[Body] = chosen_[Body] = th.bodyFamily();

    // Two tabs, "TITLE FONT  BODY FONT": click one or press Tab.
    auto* tabs = new QHBoxLayout();
    tabs->setSpacing(22);
    for (int t : {Title, Body}) {
        tab_[t] = new QLabel(t == Title ? "Title font" : "Body font", card());
        tab_[t]->setFont(th.labelFont());
        tab_[t]->setCursor(Qt::PointingHandCursor);
        tab_[t]->installEventFilter(this);
        tabs->addWidget(tab_[t]);
    }
    tabs->addStretch(1);
    tabHint_ = new QLabel("Tab  switch", card());
    tabHint_->setFont(th.labelFont());
    tabs->addWidget(tabHint_);
    body()->addLayout(tabs);
    body()->addSpacing(14);

    input_ = new QLineEdit(card());
    input_->setFont(th.bodyFont());
    input_->setPlaceholderText("Search installed fonts");
    input_->setFrame(false);
    input_->installEventFilter(this);
    body()->addWidget(input_);
    body()->addSpacing(8);

    list_ = new QListWidget(card());
    list_->setFont(th.bodyFont());
    list_->setFixedHeight(240);
    list_->setFrameShape(QFrame::NoFrame);
    list_->setFocusPolicy(Qt::NoFocus);
    list_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    list_->setUniformItemSizes(true);  // ~1000 families: keeps layout cheap
    new QListWidgetItem(QString(), list_);  // row 0: "Default", labelled per tab in showTarget()
    list_->item(0)->setData(Qt::UserRole, QString());
    QStringList families = QFontDatabase::families();
    families.removeDuplicates();
    for (const QString& f : families) {
        if (QFontDatabase::isPrivateFamily(f)) continue;
        auto* item = new QListWidgetItem(f, list_);
        item->setData(Qt::UserRole, f);
    }
    body()->addWidget(list_);
    body()->addSpacing(16);

    sample_ = new QLabel(card());
    sample_->setFixedHeight(64);
    sample_->setWordWrap(true);
    body()->addWidget(sample_);
    body()->addSpacing(10);

    hint_ = new QLabel("\xE2\x86\x91 \xE2\x86\x93  preview     Enter  keep both     Esc  cancel", card());
    hint_->setFont(th.labelFont());
    body()->addWidget(hint_);

    livePreview_.setSingleShot(true);
    livePreview_.setInterval(120);
    connect(&livePreview_, &QTimer::timeout, this, [this] { apply(chosen_[Title], chosen_[Body]); });
    connect(input_, &QLineEdit::textChanged, this, &FontPicker::filter);
    connect(list_, &QListWidget::currentItemChanged, this, &FontPicker::preview);
    connect(list_, &QListWidget::itemDoubleClicked, input_, &QLineEdit::returnPressed);
    connect(input_, &QLineEdit::returnPressed, this, [this] {
        accepted_ = true;
        livePreview_.stop();
        apply(chosen_[Title], chosen_[Body]);
        dismiss();
        emit chosen(chosen_[Title], chosen_[Body]);
    });
    connect(this, &Overlay::dismissed, this, [this] {
        livePreview_.stop();
        if (!accepted_) apply(original_[Title], original_[Body]);
    });

    showTarget(Title);
    finish();
    input_->setFocus();
}

void FontPicker::apply(const QString& title, const QString& bodyFamily) {
    Theme::instance().setTitleFamily(title);
    Theme::instance().setBodyFamily(bodyFamily);
}

void FontPicker::showTarget(int target) {
    target_ = target;
    const QString def = target == Title ? "Default (v0.1)  \xC2\xB7  " + QFontInfo(Theme::titleFontFor(QString())).family()
                                        : "Default  \xC2\xB7  " + QFontInfo(Theme::bodyFontFor(QString())).family();
    list_->item(0)->setText(def);
    switching_ = true;  // selecting the saved row is not a new choice
    for (int i = 0; i < list_->count(); ++i)
        if (list_->item(i)->data(Qt::UserRole).toString() == chosen_[target]) list_->setCurrentRow(i);
    if (list_->currentItem() && list_->currentItem()->isHidden()) list_->setCurrentItem(nullptr);
    switching_ = false;
    if (list_->currentItem()) list_->scrollToItem(list_->currentItem(), QAbstractItemView::PositionAtCenter);
    restyle();
    preview();
}

QString FontPicker::current() const {
    const QListWidgetItem* item = list_->currentItem();
    return item ? item->data(Qt::UserRole).toString() : chosen_[target_];
}

void FontPicker::filter(const QString& text) {
    const QString needle = text.trimmed();
    QListWidgetItem* first = nullptr;
    for (int i = 0; i < list_->count(); ++i) {
        QListWidgetItem* item = list_->item(i);
        const bool show = needle.isEmpty() || item->text().contains(needle, Qt::CaseInsensitive);
        item->setHidden(!show);
        if (show && !first) first = item;
    }
    if (!list_->currentItem() || list_->currentItem()->isHidden()) list_->setCurrentItem(first);
}

void FontPicker::preview() {
    if (!switching_ && list_->currentItem()) chosen_[target_] = current();
    // The sample shows the font at once; the page follows a moment later.
    if (target_ == Title) {
        sample_->setText(pageTitle_.isEmpty() ? QString("Lecture notes") : pageTitle_);
        sample_->setFont(Theme::titleFontFor(chosen_[Title]));
    } else {
        sample_->setText("Rate = k[A][B] \xE2\x80\x94 the order is found from initial rates. 0123456789");
        sample_->setFont(Theme::bodyFontFor(chosen_[Body]));
    }
    livePreview_.start();
}

bool FontPicker::eventFilter(QObject* obj, QEvent* ev) {
    if (obj == input_ && ev->type() == QEvent::KeyPress) {
        const int key = static_cast<QKeyEvent*>(ev)->key();
        if (key == Qt::Key_Up || key == Qt::Key_Down || key == Qt::Key_PageUp || key == Qt::Key_PageDown) {
            QApplication::sendEvent(list_, ev);  // the list moves while typing stays in the search box
            return true;
        }
        if (key == Qt::Key_Tab || key == Qt::Key_Backtab) {
            showTarget(target_ == Title ? Body : Title);
            return true;
        }
    }
    if (ev->type() == QEvent::MouseButtonPress) {
        for (int t : {Title, Body})
            if (obj == tab_[t]) {
                showTarget(t);
                input_->setFocus();
                return true;
            }
    }
    return Overlay::eventFilter(obj, ev);
}

void FontPicker::restyle() {
    const Theme& th = Theme::instance();
    for (int t : {Title, Body}) tab_[t]->setStyleSheet(colorCss(t == target_ ? th.accent() : th.faint()));
    tabHint_->setStyleSheet(colorCss(th.faint()));
    hint_->setStyleSheet(colorCss(th.faint()));
    sample_->setStyleSheet(colorCss(th.text()));
    input_->setStyleSheet(QString("QLineEdit { background: transparent; color: %1; border: none;"
                                  " border-bottom: 1px solid %2; padding: 2px 0 8px 0;"
                                  " selection-background-color: %3; }")
                              .arg(rgba(th.text()), rgba(th.line()), rgba(th.selection())));
    QColor selected = th.structure();
    selected.setAlpha(52);
    list_->setStyleSheet(QString("QListWidget { background: transparent; color: %1; outline: none; }"
                                 " QListWidget::item { padding: 4px 8px; border-radius: 3px;"
                                 " border-left: 2px solid transparent; }"
                                 " QListWidget::item:selected { background: %2; color: %3;"
                                 " border-left: 2px solid %4; }")
                             .arg(rgba(th.dim()), rgba(selected), rgba(th.text()), rgba(th.accent())));
}

}  // namespace graphite
