// Images in blocks (![alt](../assets/x.png)), drawn inside a QTextDocument.
//
// Memory: layout only reads the file header for the size; pixels are decoded the first time
// the image is painted, straight to display size (QImageReader::setScaledSize), into one
// shared cache capped at kCacheMB. Pages full of screenshots stay cheap, and images scrolled
// out of view are dropped first when the cache is full.
#pragma once

#include <QObject>
#include <QSizeF>
#include <QTextFormat>
#include <QTextObjectInterface>

class QTextDocument;

namespace graphite {

class ImageObject : public QObject, public QTextObjectInterface {
    Q_OBJECT
    Q_INTERFACES(QTextObjectInterface)
public:
    static constexpr int kType = QTextFormat::UserObject + 18;
    static constexpr int kCacheMB = 32;
    static constexpr qreal kMaxW = 640, kMaxH = 420;  // logical pixels
    enum Prop { Path = QTextFormat::UserProperty + 20, Size, Stamp };  // Stamp: file time, read once

    static void install(QTextDocument* doc);
    // A format for the image at `path` (absolute), or false if it is not a readable image.
    static bool format(QTextCharFormat* out, const QString& path);
    static bool isImagePath(const QString& path);
    static qint64 cachedBytes();  // for tests

    QSizeF intrinsicSize(QTextDocument* doc, int posInDocument, const QTextFormat& format) override;
    void drawObject(QPainter* painter, const QRectF& rect, QTextDocument* doc, int posInDocument,
                    const QTextFormat& format) override;
};

}  // namespace graphite
