// Formula typesetting: LaTeX (with \ce and \pu) -> QImage, through MicroTeX.
#pragma once

#include <QColor>
#include <QImage>
#include <QString>

namespace graphite {

struct TexImage {
    QImage image;       // device-pixel image, devicePixelRatio set
    qreal ascent = 0;   // logical pixels above the baseline
    qreal depth = 0;    // logical pixels below the baseline
    bool ok = false;
    QString error;
};

namespace Tex {

// Loads MicroTeX's fonts. Needs a QGuiApplication. Safe to call twice. Returns false if the
// resource folder cannot be found (formulas then stay as plain text).
bool init();
bool ready();

// `pixelSize` is the body text size in logical pixels. `display` gives $$..$$ layout.
TexImage render(const QString& latex, qreal pixelSize, const QColor& color, bool display, qreal dpr);

}  // namespace Tex
}  // namespace graphite
