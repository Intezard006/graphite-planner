// Draws typeset formulas inside a QTextDocument, sitting on the text baseline.
#pragma once

#include <QColor>
#include <QObject>
#include <QTextFormat>
#include <QTextObjectInterface>

class QTextDocument;

namespace graphite {

class MathObject : public QObject, public QTextObjectInterface {
    Q_OBJECT
    Q_INTERFACES(QTextObjectInterface)
public:
    static constexpr int kType = QTextFormat::UserObject + 17;
    enum Prop { Image = QTextFormat::UserProperty + 1, Depth, Ascent, Source };  // Source: the TeX text

    // Makes `doc` able to show formula objects.
    static void install(QTextDocument* doc);

    // Typesets `latex` and returns a character format for it, or a null-type format when
    // TeX fails (the caller then shows the source text).
    static bool format(QTextCharFormat* out, const QString& latex, qreal pixelSize, const QColor& color,
                       bool display, qreal dpr);

    // The cached typeset image of `latex` (null when TeX fails), for the editor's live preview.
    static QImage image(const QString& latex, qreal pixelSize, const QColor& color, bool display, qreal dpr);

    QSizeF intrinsicSize(QTextDocument* doc, int posInDocument, const QTextFormat& format) override;
    void drawObject(QPainter* painter, const QRectF& rect, QTextDocument* doc, int posInDocument,
                    const QTextFormat& format) override;
};

}  // namespace graphite
