#include "render/imageobject.hpp"

#include <QAbstractTextDocumentLayout>
#include <QCache>
#include <QCoreApplication>
#include <QFileInfo>
#include <QImageReader>
#include <QPainter>
#include <QTextCharFormat>
#include <QTextDocument>

#include "ui/theme.hpp"

namespace graphite {

namespace {
// Key: path, display size in device pixels, file time (an edited image is read again).
QCache<QString, QImage>& cache() {
    static QCache<QString, QImage> c(ImageObject::kCacheMB * 1024);  // cost in KiB
    return c;
}
}  // namespace

void ImageObject::install(QTextDocument* doc) {
    static ImageObject* handler = [] {
        auto* h = new ImageObject;
        h->setParent(QCoreApplication::instance());
        return h;
    }();
    doc->documentLayout()->registerHandler(kType, handler);
}

bool ImageObject::isImagePath(const QString& path) {
    static const QStringList ext = {"png", "jpg", "jpeg", "gif", "webp", "bmp", "svg"};
    return ext.contains(QFileInfo(path).suffix().toLower());
}

bool ImageObject::format(QTextCharFormat* out, const QString& path) {
    if (!isImagePath(path) || !QFileInfo::exists(path)) return false;
    QImageReader reader(path);
    reader.setAutoTransform(true);
    QSize s = reader.size();  // header only: no pixels decoded here
    if (!s.isValid()) return false;
    if (reader.transformation() & QImageIOHandler::TransformationRotate90) s.transpose();
    const qreal scale = qMin<qreal>(1.0, qMin(kMaxW / s.width(), kMaxH / s.height()));
    QTextCharFormat f;
    f.setObjectType(kType);
    f.setProperty(Path, path);
    f.setProperty(Size, QSizeF(s.width() * scale, s.height() * scale));
    f.setProperty(Stamp, QFileInfo(path).lastModified().toMSecsSinceEpoch());
    *out = f;
    return true;
}

qint64 ImageObject::cachedBytes() { return static_cast<qint64>(cache().totalCost()) * 1024; }

QSizeF ImageObject::intrinsicSize(QTextDocument* doc, int, const QTextFormat& format) {
    QSizeF s = format.property(Size).toSizeF();
    const qreal room = doc ? doc->textWidth() : -1;  // narrow window: shrink to the column
    if (room > 0 && s.width() > room) s *= room / s.width();
    return s;
}

void ImageObject::drawObject(QPainter* painter, const QRectF& rect, QTextDocument*, int, const QTextFormat& format) {
    const QString path = format.property(Path).toString();
    // Printing (PDF export): decode at ~250 dpi instead of screen resolution.
    const bool printing = painter->device() && painter->device()->devType() == QInternal::Printer;
    const qreal dpr = printing ? 2.6 : painter->device() ? painter->device()->devicePixelRatioF() : 1.0;
    const QSize px = (rect.size() * dpr).toSize();
    const QString key = QString("%1|%2x%3|%4").arg(path).arg(px.width()).arg(px.height())
                            .arg(format.property(Stamp).toLongLong());  // no disk access while painting
    QImage* img = cache().object(key);
    if (!img) {
        QImageReader reader(path);
        reader.setAutoTransform(true);
        reader.setScaledSize(px);  // decode at display size, not at the file's size
        auto* decoded = new QImage(reader.read());
        if (decoded->isNull()) {
            delete decoded;
            // Unreadable: a faint frame where the image would be.
            painter->setPen(QPen(Theme::instance().line(), 1, Qt::DashLine));
            painter->drawRect(rect.adjusted(0.5, 0.5, -0.5, -0.5));
            return;
        }
        decoded->setDevicePixelRatio(dpr);
        const int kib = qMax<int>(1, static_cast<int>(decoded->sizeInBytes() / 1024));
        cache().insert(key, decoded, kib);
        img = cache().object(key);
        if (!img) return;  // larger than the whole cache
    }
    painter->drawImage(rect.topLeft(), *img);
}

}  // namespace graphite
