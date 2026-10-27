#include "ui/theme.hpp"

#include <QFontDatabase>
#include <QFontInfo>
#include <cmath>

namespace graphite {

namespace {
// Background alpha in transparent mode. 0 = nothing painted, the compositor's blur shows
// through completely. Raise it (0-255) for a faint tint.
constexpr int kTransparentTint = 0;

QColor mix(const QColor& a, const QColor& b, qreal t) {
    return QColor::fromRgbF(a.redF() * (1 - t) + b.redF() * t, a.greenF() * (1 - t) + b.greenF() * t,
                            a.blueF() * (1 - t) + b.blueF() * t);
}

QString css(const QColor& c) {
    return QString("rgba(%1,%2,%3,%4)").arg(c.red()).arg(c.green()).arg(c.blue()).arg(c.alpha());
}

QColor withAlpha(QColor c, int a) {
    c.setAlpha(a);
    return c;
}
}  // namespace

Theme& Theme::instance() {
    static Theme t;
    return t;
}

void Theme::setTransparent(bool on) {
    if (on == transparent_) return;
    transparent_ = on;
    emit changed();
}

PaletteColors Theme::printPalette() {
    PaletteColors p;
    p.bg = QColor(0xff, 0xff, 0xff);
    p.panel = QColor(0xf4, 0xf2, 0xf8);
    p.text = QColor(0x15, 0x13, 0x1a);
    p.accent = QColor(0x3d, 0x7a, 0x00);     // EVA green, darkened to read on paper
    p.structure = QColor(0x62, 0x36, 0xd9);  // EVA purple
    p.warm = QColor(0xc2, 0x41, 0x0c);
    p.tones.dim = p.tones.dimClear = QColor(0x5a, 0x55, 0x66);
    p.tones.faint = p.tones.faintClear = QColor(0x8f, 0x8a, 0x9c);
    p.tones.line = p.tones.lineClear = QColor(0xd9, 0xd5, 0xe3);
    p.tones.selection = QColor(0x62, 0x36, 0xd9, 60);
    p.source = "print";
    return p;
}

Theme::PrintScope::PrintScope() {
    Theme& t = Theme::instance();
    saved_ = t.pal_;
    savedTransparent_ = t.transparent_;
    t.pal_ = printPalette();
    t.transparent_ = false;
}

Theme::PrintScope::~PrintScope() {
    Theme& t = Theme::instance();
    t.pal_ = saved_;
    t.transparent_ = savedTransparent_;
}

void Theme::setPalette(const PaletteColors& p) {
    if (p == pal_) {
        pal_.source = p.source;
        return;
    }
    pal_ = p;
    emit changed();
}

QColor Theme::bg() const {
    QColor c = pal_.bg;
    if (transparent_) c.setAlpha(kTransparentTint);
    return c;
}

QColor Theme::panel() const {
    QColor c = pal_.panel;
    if (transparent_) c.setAlpha(kTransparentTint);
    return c;
}

// The share of the background mixed into the text colour; larger = quieter.
// A palette may bring its own hand-tuned tones (the built-in one does); otherwise derive them.
QColor Theme::dim() const {
    const QColor& t = transparent_ ? pal_.tones.dimClear : pal_.tones.dim;
    return t.isValid() ? t : mix(pal_.text, pal_.bg, transparent_ ? 0.17 : 0.43);
}

QColor Theme::faint() const {
    const QColor& t = transparent_ ? pal_.tones.faintClear : pal_.tones.faint;
    return t.isValid() ? t : mix(pal_.text, pal_.bg, transparent_ ? 0.34 : 0.70);
}

QColor Theme::line() const {
    const QColor& t = transparent_ ? pal_.tones.lineClear : pal_.tones.line;
    if (t.isValid()) return t;
    if (transparent_) return withAlpha(pal_.text, 70);
    return mix(pal_.bg, pal_.text, 0.14);
}

QColor Theme::selection() const {
    return pal_.tones.selection.isValid() ? pal_.tones.selection : withAlpha(pal_.accent, 95);
}

QFont Theme::bodyFont() const { return bodyFontFor(bodyFamily_); }

// Default: Inter, else the usual sans faces. A family chosen in the font picker is used in its
// regular weight, with the defaults behind it for any character it lacks.
QFont Theme::bodyFontFor(const QString& family) {
    QFont f;
    QStringList families = {"Inter", "Noto Sans", "Cantarell", "DejaVu Sans", "sans-serif"};
    if (!family.isEmpty() && QFontDatabase::hasFamily(family)) families.prepend(family);
    f.setFamilies(families);
    f.setPointSizeF(Theme::instance().textSize());
    return f;
}

void Theme::setImmersion(bool on) {
    if (on == immersion_) return;
    immersion_ = on;
    emit changed();
}

void Theme::setTextSize(qreal pt) {
    pt = qBound(kMinTextSize, std::round(pt * 2) / 2, kMaxTextSize);
    if (qFuzzyCompare(pt, textSize_)) return;
    textSize_ = pt;
    emit changed();
}

void Theme::setBodyFamily(const QString& family) {
    if (family == bodyFamily_) return;
    bodyFamily_ = family;
    emit changed();
}

QFont Theme::monoFont() const {
    QFont f;
    f.setFamilies({"JetBrains Mono", "Fira Code", "Noto Sans Mono", "DejaVu Sans Mono", "monospace"});
    f.setStyleHint(QFont::Monospace);
    f.setPointSizeF(10.5 * scale());
    return f;
}

void Theme::setTitleFamily(const QString& family) {
    if (family == titleFamily_) return;
    titleFamily_ = family;
    emit changed();
}

// Default: v0.1's heavy serif. Matisse EB (the Evangelion title-card face; Fontworks ships it
// under "FOT-" names) when installed, otherwise Noto Serif Display Black. A family chosen in
// the font picker (Ctrl+Shift+F) is used in its heaviest style.
QFont Theme::titleFont() const { return titleFontFor(titleFamily_); }

QFont Theme::titleFontFor(const QString& family) {
    QFont f;
    if (family.isEmpty() || !QFontDatabase::hasFamily(family)) {  // also: a saved font since uninstalled
        f.setFamilies({"FOT-Matisse Pro EB", "FOT-Matisse ProN EB", "FOT-Matisse Std EB", "FOT-Matisse Pro",
                       "FOT-Matisse ProN", "Matisse EB", "Noto Serif Display", "Noto Serif", "DejaVu Serif",
                       "serif"});
        f.setPointSizeF(26 * Theme::instance().scale());
        f.setWeight(QFont::Black);
        return f;
    }
    f.setFamilies({family, "Noto Serif Display", "serif"});
    // Condensed faces look small at the same size; the word may be in the family or the style.
    auto isNarrow = [](const QString& name) {
        if (name.contains("SemiCond", Qt::CaseInsensitive)) return false;  // barely narrower
        return name.contains("Cond", Qt::CaseInsensitive) || name.contains("Narrow", Qt::CaseInsensitive) ||
               name.contains("Compressed", Qt::CaseInsensitive);
    };
    int heaviest = QFont::Normal;
    bool narrow = isNarrow(family);
    for (const QString& style : QFontDatabase::styles(family)) {
        const int w = QFontDatabase::weight(family, style);
        if (w > heaviest) {
            heaviest = w;
            narrow = isNarrow(family) || isNarrow(style);
        }
    }
    f.setWeight(static_cast<QFont::Weight>(qMin<int>(heaviest, QFont::Black)));
    f.setPointSizeF((narrow ? 32 : 26) * Theme::instance().scale());
    return f;
}

QFont Theme::labelFont() const {
    QFont f = bodyFont();
    f.setPointSizeF(8 * scale());
    f.setWeight(QFont::DemiBold);
    f.setCapitalization(QFont::AllUppercase);
    f.setLetterSpacing(QFont::AbsoluteSpacing, 1.6 * scale());
    return f;
}

QString Theme::styleSheet() const {
    const QColor hover = withAlpha(pal_.structure, 30);
    const QColor selected = withAlpha(pal_.structure, 52);
    return QString(R"(
        QWidget#root, QWidget#sidebar, QWidget#content { background: transparent; }
        QListWidget#nav {
            background: transparent; border: none; outline: none;
            color: %1; padding: 0 8px;
        }
        QListWidget#nav::item {
            padding: 5px 10px; border-radius: 3px; border-left: 2px solid transparent;
        }
        QListWidget#nav::item:hover { background: %6; }
        QListWidget#nav::item:selected {
            background: %7; color: %2; border-left: 2px solid %3;
        }
        QTreeWidget#nav {
            background: transparent; border: none; outline: none;
            color: %1; padding: 0 8px; show-decoration-selected: 0;
        }
        QTreeWidget#nav::item {
            padding: 5px 6px; border-radius: 3px; border-left: 2px solid transparent;
        }
        QTreeWidget#nav::item:hover { background: %6; }
        QTreeWidget#nav::item:selected {
            background: %7; color: %2; border-left: 2px solid %3;
        }
        QTreeWidget#nav::branch { background: transparent; image: none; border-image: none; }
        QTreeWidget#nav QLineEdit {
            background: %8; color: %2; border: 1px solid %3; border-radius: 3px; padding: 2px 4px;
        }
        QScrollBar:vertical { background: transparent; width: 8px; margin: 0; }
        QScrollBar::handle:vertical { background: %4; border-radius: 3px; min-height: 28px; }
        QScrollBar::handle:vertical:hover { background: %5; }
        QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }
        QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical { background: transparent; }
        QToolTip { background: %8; color: %2; border: 1px solid %4; padding: 4px 6px; }
    )")
        .arg(css(dim()), css(text()), css(accent()), css(line()), css(faint()))
        .arg(css(hover), css(selected), css(mix(pal_.bg, pal_.text, 0.06).toRgb()));
}

}  // namespace graphite
