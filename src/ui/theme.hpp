// Colours, fonts and the Ctrl+T translucency switch.
//
// The six base colours come from a PaletteColors (the wallpaper, or the built-in set); the
// dim/faint/line tones are derived from them here. Text is always fully opaque: only the
// *backgrounds* lose their alpha in transparent mode, so the compositor (Hyprland) can blur
// what is behind the window.
#pragma once

#include <QColor>
#include <QFont>
#include <QObject>
#include <QString>

#include "ui/palette.hpp"

namespace graphite {

class Theme : public QObject {
    Q_OBJECT
public:
    static Theme& instance();

    bool transparent() const { return transparent_; }
    void setTransparent(bool on);
    void toggleTransparent() { setTransparent(!transparent_); }

    const PaletteColors& palette() const { return pal_; }
    void setPalette(const PaletteColors& p);

    // Backgrounds: alpha drops to the tint in transparent mode (0 = fully clear).
    QColor bg() const;
    QColor panel() const;
    // Foregrounds: always opaque.
    QColor text() const { return pal_.text; }
    QColor accent() const { return pal_.accent; }
    QColor structure() const { return pal_.structure; }
    QColor warm() const { return pal_.warm; }
    // Secondary tones. Lifted in transparent mode: a grey that reads fine on the solid
    // background disappears against a bright wallpaper.
    QColor dim() const;
    QColor faint() const;
    QColor line() const;       // hairlines and guides (translucent in transparent mode)
    QColor selection() const;  // text selection

    QFont bodyFont() const;
    QFont monoFont() const;
    QFont titleFont() const;
    // Family for page titles; empty = the default (v0.1's serif).
    QString titleFamily() const { return titleFamily_; }
    void setTitleFamily(const QString& family);
    static QFont titleFontFor(const QString& family);  // what titleFont() would be with `family`
    // Text size: the body font's point size. Every other font is scaled from it (titles,
    // headings, mono, labels, the NERV panel, formulas), so one number zooms the whole app.
    static constexpr qreal kDefaultTextSize = 11.5, kMinTextSize = 9, kMaxTextSize = 18;
    qreal textSize() const { return textSize_; }
    void setTextSize(qreal pt);  // clamped, rounded to 0.5 pt
    qreal scale() const { return textSize_ / kDefaultTextSize; }

    // Immersion: the Evangelion touches (title cards, MAGI, Pattern Blue, entry-plug boot, unit
    // names). Off = the same features with plain wording and no flourishes.
    bool immersion() const { return immersion_; }
    void setImmersion(bool on);

    // Family for all other text; empty = the default (Inter).
    QString bodyFamily() const { return bodyFamily_; }
    void setBodyFamily(const QString& family);
    static QFont bodyFontFor(const QString& family);
    QFont labelFont() const;  // small, upper-case, letter-spaced

    QString styleSheet() const;

    // For PDF export: white paper, dark ink, the accents darkened for print. Swapped in and out
    // silently (no changed() signal), so the window on screen never flashes.
    class PrintScope {
    public:
        PrintScope();
        ~PrintScope();
        PrintScope(const PrintScope&) = delete;
        PrintScope& operator=(const PrintScope&) = delete;

    private:
        PaletteColors saved_;
        bool savedTransparent_;
    };
    static PaletteColors printPalette();

signals:
    void changed();

private:
    Theme() : pal_(builtinPalette()) {}
    PaletteColors pal_;
    bool transparent_ = false;
    QString titleFamily_;
    QString bodyFamily_;
    qreal textSize_ = kDefaultTextSize;
    bool immersion_ = true;
};

}  // namespace graphite
