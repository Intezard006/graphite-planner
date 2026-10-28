#include "render/mathobject.hpp"

#include <QAbstractTextDocumentLayout>
#include <QGuiApplication>
#include <QHash>
#include <QPainter>
#include <QTextCharFormat>
#include <QTextDocument>

#include "render/tex.hpp"

namespace graphite {

namespace {
struct Cached {
    TexImage img;
};
QHash<QString, Cached>& cache() {
    static QHash<QString, Cached> c;
    return c;
}
}  // namespace

void MathObject::install(QTextDocument* doc) {
    static MathObject* handler = [] {
        auto* h = new MathObject;
        h->setParent(QCoreApplication::instance());
        return h;
    }();
    doc->documentLayout()->registerHandler(kType, handler);
}

bool MathObject::format(QTextCharFormat* out, const QString& latex, qreal pixelSize, const QColor& color,
                        bool display, qreal dpr) {
    if (!Tex::ready() && !Tex::init()) return false;
    const QString key = QString("%1|%2|%3|%4|%5").arg(latex).arg(pixelSize, 0, 'f', 1).arg(color.rgba(), 0, 16)
                            .arg(display).arg(dpr);
    auto& c = cache();
    auto it = c.find(key);
    if (it == c.end()) {
        if (c.size() > 600) c.clear();
        it = c.insert(key, Cached{Tex::render(latex, pixelSize, color, display, dpr)});
    }
    const TexImage& t = it->img;
    if (!t.ok) return false;
    QTextCharFormat f;
    f.setObjectType(kType);
    f.setProperty(Image, t.image);
    f.setProperty(Depth, t.depth);
    // The object's own height is the ascent; the depth hangs below the baseline.
    f.setProperty(Ascent, t.ascent);
    f.setProperty(Source, latex);
    *out = f;
    return true;
}

QImage MathObject::image(const QString& latex, qreal pixelSize, const QColor& color, bool display, qreal dpr) {
    QTextCharFormat f;
    if (!format(&f, latex, pixelSize, color, display, dpr)) return QImage();
    return qvariant_cast<QImage>(f.property(Image));
}

QSizeF MathObject::intrinsicSize(QTextDocument*, int, const QTextFormat& format) {
    const QImage img = qvariant_cast<QImage>(format.property(Image));
    const qreal dpr = img.devicePixelRatio();
    return QSizeF(img.width() / dpr, format.property(Ascent).toReal());
}

void MathObject::drawObject(QPainter* painter, const QRectF& rect, QTextDocument*, int, const QTextFormat& format) {
    const QImage img = qvariant_cast<QImage>(format.property(Image));
    painter->drawImage(QPointF(rect.left(), rect.top()), img);
}

}  // namespace graphite
