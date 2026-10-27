#include "ui/thirdimpact.hpp"

#include <QApplication>
#include <QKeyEvent>
#include <QPainter>
#include <QPainterPath>
#include <cmath>

#include "ui/theme.hpp"

namespace graphite {

namespace {
const QColor kBody(0x22, 0x09, 0x02);     // the sea between the characters
const QColor kCrest(0xff, 0xc2, 0x66);    // the surface line
const QColor kTones[] = {QColor(0xff, 0x8c, 0x1a), QColor(0xff, 0x6a, 0x00, 190), QColor(0xc8, 0x4b, 0x00, 150),
                         QColor(0xff, 0xb3, 0x47, 235)};
const QString kGlyphs = QString::fromUtf8("0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ#$%&*+=<>/\\|:;~^\xCE\xA3\xCE\x94\xCE\xA9\xCE\x9B\xCE\xA8\xCE\x9E");

qreal easeOut(qreal t) { return 1 - std::pow(1 - t, 3); }
}  // namespace

ThirdImpact::ThirdImpact(QWidget* host, const QString& caption, std::function<void()> atFlood)
    : QWidget(host), caption_(caption), atFlood_(std::move(atFlood)) {
    setGeometry(host->rect());
    setFocusPolicy(Qt::NoFocus);
    qApp->installEventFilter(this);
    glyphFont_ = Theme::instance().monoFont();
    glyphFont_.setBold(true);
    makeSea();
    frame_.setInterval(33);
    connect(&frame_, &QTimer::timeout, this, [this] {
        const qint64 ms = clock_.elapsed();
        if (!flooded_ && ms >= kRise) flood();
        if (ms >= kRise + kHold + kDrain) {
            finish();
            return;
        }
        churn(cols_ * rows_ / 18);  // the sea is never still
        update();
    });
    clock_.start();
    frame_.start();
    show();
    raise();
}

quint32 ThirdImpact::next() {  // xorshift: cheap, and no shared generator to disturb
    seed_ ^= seed_ << 13;
    seed_ ^= seed_ >> 17;
    seed_ ^= seed_ << 5;
    return seed_;
}

void ThirdImpact::makeSea() {
    const qreal dpr = devicePixelRatioF();
    const QFontMetrics fm(glyphFont_);
    cellW_ = qMax(6, fm.horizontalAdvance('M') + 2);
    cellH_ = qMax(10, fm.height());
    cols_ = width() / cellW_ + 1;
    rows_ = height() / cellH_ + 1;
    sea_ = QImage(QSize(width(), height()) * dpr, QImage::Format_ARGB32_Premultiplied);
    sea_.setDevicePixelRatio(dpr);
    sea_.fill(kBody);
    churn(cols_ * rows_);
}

void ThirdImpact::churn(int n) {
    if (sea_.isNull() || cols_ <= 0 || rows_ <= 0) return;
    QPainter p(&sea_);
    p.setFont(glyphFont_);
    const QFontMetrics fm(glyphFont_);
    for (int k = 0; k < n; ++k) {
        const int cell = static_cast<int>(next() % static_cast<quint32>(cols_ * rows_));
        const QRect r((cell % cols_) * cellW_, (cell / cols_) * cellH_, cellW_, cellH_);
        p.fillRect(r, kBody);
        const quint32 roll = next();
        if (roll % 7 == 0) continue;  // a gap now and then
        p.setPen(kTones[(roll >> 8) % 16 == 0 ? 3 : (roll >> 4) % 3]);
        p.drawText(r.x() + 1, r.y() + fm.ascent(), kGlyphs.at(static_cast<int>((roll >> 12) % kGlyphs.size())));
    }
}

qreal ThirdImpact::level(qint64 ms) const {
    if (ms < kRise) return 1.08 - 1.22 * easeOut(qreal(ms) / kRise);
    if (ms < kRise + kHold) return -0.14;
    const qreal t = qMin<qreal>(1, qreal(ms - kRise - kHold) / kDrain);
    return -0.14 + 1.26 * t * t;
}

void ThirdImpact::flood() {
    if (flooded_) return;
    flooded_ = true;
    if (atFlood_) atFlood_();  // under the sea, where nobody sees the page empty out
}

void ThirdImpact::skip() {
    flood();
    finish();
}

void ThirdImpact::finish() {
    frame_.stop();
    qApp->removeEventFilter(this);
    hide();
    sea_ = QImage();
    deleteLater();
}

bool ThirdImpact::eventFilter(QObject* obj, QEvent* ev) {
    if (obj == parentWidget() && ev->type() == QEvent::Resize) {
        setGeometry(parentWidget()->rect());
        makeSea();
    }
    if (ev->type() == QEvent::KeyPress && isVisible()) {
        skip();
        return true;  // this key only ends the effect
    }
    return false;
}

void ThirdImpact::mousePressEvent(QMouseEvent*) { skip(); }

void ThirdImpact::paintEvent(QPaintEvent*) {
    const qint64 ms = clock_.elapsed();
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const qreal w = width(), h = height();

    // The world goes dark above the rising sea, and comes back as it drains.
    const qreal dark = ms < kRise ? easeOut(qreal(ms) / kRise)
                                  : ms < kRise + kHold ? 1.0 : 1.0 - qMin<qreal>(1, qreal(ms - kRise - kHold) / kDrain);
    p.fillRect(rect(), QColor(0, 0, 0, static_cast<int>(150 * dark)));

    // The surface: two slow swells, sampled every 8 px.
    const qreal y0 = level(ms) * h, amp = h * 0.03, t = ms / 1000.0;
    QPainterPath surface;
    for (qreal x = 0; x <= w + 8; x += 8) {
        const qreal y = y0 + amp * std::sin(x / w * 9.0 + t * 2.6) + amp * 0.55 * std::sin(x / w * 23.0 - t * 4.1);
        if (x == 0) surface.moveTo(x, y);
        else surface.lineTo(x, y);
    }
    QPainterPath sea = surface;
    sea.lineTo(w + 8, h + 1);
    sea.lineTo(0, h + 1);
    sea.closeSubpath();
    p.save();
    p.setClipPath(sea);
    p.drawImage(0, 0, sea_);
    p.restore();
    p.setBrush(Qt::NoBrush);
    p.setPen(QPen(QColor(kCrest.red(), kCrest.green(), kCrest.blue(), 60), 7));
    p.drawPath(surface);
    p.setPen(QPen(kCrest, 1.6));
    p.drawPath(surface);

    // THIRD IMPACT, on the sea while it covers everything.
    if (ms < kRise - 150) return;
    const qreal in = qMin<qreal>(1, (ms - kRise + 150) / 300.0);
    const qreal out = ms < kRise + kHold ? 1.0 : qMax<qreal>(0, 1 - (ms - kRise - kHold) / (kDrain * 0.55));
    const qreal a = in * out;
    if (a <= 0) return;
    QFont title = Theme::instance().titleFont();
    title.setPointSizeF(100);
    title.setLetterSpacing(QFont::PercentageSpacing, 104);
    const QString headline = "THIRD IMPACT";
    title.setPointSizeF(qBound<qreal>(18, 100 * (w * 0.78) / qMax(1, QFontMetrics(title).horizontalAdvance(headline)), 160));
    const QFontMetrics tm(title);
    QFont label = Theme::instance().labelFont();
    label.setLetterSpacing(QFont::PercentageSpacing, 130);
    label.setCapitalization(QFont::AllUppercase);
    const QFontMetrics lm(label);
    const qreal blockH = tm.height() + 18 + lm.height() * 2 + 8;
    qreal y = (h - blockH) / 2;
    const qreal tx = (w - tm.horizontalAdvance(headline)) / 2;
    p.setOpacity(a);
    p.setFont(title);
    p.setPen(QColor(0xff, 0x6a, 0x00, 110));  // a glow, as if lit from the sea
    for (const QPointF d : {QPointF(-2, 0), QPointF(2, 0), QPointF(0, -2), QPointF(0, 2)})
        p.drawText(QPointF(tx, y + tm.ascent()) + d, headline);
    p.setPen(QColor(0xf4, 0xf2, 0xee));
    p.drawText(QPointF(tx, y + tm.ascent()), headline);
    y += tm.height() + 6;
    p.fillRect(QRectF(w * 0.3, y, w * 0.4, 1.5), QColor(0xff, 0x8c, 0x1a));
    y += 12;
    p.setFont(label);
    p.setPen(QColor(0xff, 0xc2, 0x66));
    p.drawText(QRectF(0, y, w, lm.height()), Qt::AlignHCenter, caption_);
    y += lm.height() + 8;
    p.setPen(QColor(0xc9, 0xa0, 0x80));
    p.drawText(QRectF(0, y, w, lm.height()), Qt::AlignHCenter, "Ctrl+Z restores the day");
}

}  // namespace graphite
