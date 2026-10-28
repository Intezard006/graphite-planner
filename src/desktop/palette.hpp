// Where the colours come from: the wallpaper currently on screen.
//
// Order of preference (first that works wins):
//   1. a wallpaper given on the command line / GRAPHITE_WALLPAPER   -> extracted here
//   2. matugen's Material colours (ML4W writes ~/.config/ml4w/colors/colors.json)
//   3. pywal-style ~/.cache/wal/colors.json
//   4. the wallpaper image found via ML4W / hyprpaper / swww / awww  -> extracted here
//   5. the built-in violet-and-green palette
//
// The app is always dark: a light colour scheme only lends its hues.
#pragma once

#include <QColor>
#include <QFileSystemWatcher>
#include <QImage>
#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QTimer>

namespace graphite {

struct PaletteColors {
    QColor bg;         // window background (opaque mode)
    QColor panel;      // sidebar
    QColor text;
    QColor accent;     // links, active bullet, state
    QColor structure;  // bullets, tags, formulas
    QColor warm;       // TODO markers
    // Hand-tuned secondary tones (the built-in set keeps v0.1's exactly). Invalid = Theme
    // derives them from the colours above. "Clear" = transparent mode.
    struct Tones {
        QColor dim, faint, line, dimClear, faintClear, lineClear, selection;
        bool operator==(const Tones& o) const {
            return dim == o.dim && faint == o.faint && line == o.line && dimClear == o.dimClear &&
                   faintClear == o.faintClear && lineClear == o.lineClear && selection == o.selection;
        }
    } tones;
    QString source = "built-in";

    bool operator==(const PaletteColors& o) const {
        return bg == o.bg && panel == o.panel && text == o.text && accent == o.accent &&
               structure == o.structure && warm == o.warm && tones == o.tones;
    }
    bool operator!=(const PaletteColors& o) const { return !(*this == o); }
};

PaletteColors builtinPalette();  // Unit-01: violet and green

// Evangelion unit palettes: "unit01" (the built-in set), "unit00" (blue and white), "unit02"
// (red and orange). unitPalette() of anything else is the built-in set.
QStringList unitIds();
QString unitName(const QString& id);  // "Unit-02"
PaletteColors unitPalette(const QString& id);

// Pure functions (unit tested). `ok` is set false when the input lacks what is needed.
PaletteColors paletteFromMaterial(const QJsonObject& colors, bool* ok);   // matugen colors.json
PaletteColors paletteFromPywal(const QJsonObject& wal, bool* ok);         // ~/.cache/wal/colors.json
PaletteColors paletteFromImage(const QImage& image);                      // k-means on the picture
// Wallpaper path out of `swww query`, `awww query` or `hyprctl hyprpaper listactive` output.
QString wallpaperFromToolOutput(const QString& output);

class PaletteProvider : public QObject {
    Q_OBJECT
public:
    explicit PaletteProvider(QObject* parent = nullptr);

    void setWallpaperOverride(const QString& path) { override_ = path; }
    // A fixed unit palette instead of the wallpaper ("" = follow the wallpaper). Can change
    // while running.
    void setUnit(const QString& id);
    QString unit() const { return unit_; }
    // Older name for setUnit("unit01") / setUnit("").
    void setBuiltinOnly(bool on) { setUnit(on ? "unit01" : QString()); }
    bool builtinOnly() const { return !unit_.isEmpty(); }

    // Detect now (synchronous) and start watching the files that feed the palette.
    void start();
    PaletteColors current() const { return current_; }
    bool polling() const { return poll_.isActive(); }  // for tests
    QString describe() const { return current_.source; }

signals:
    void changed(const graphite::PaletteColors& palette);

private:
    PaletteColors detect();
    QString findWallpaper() const;
    void rewatch();
    void refresh();
    void updatePoll();

    QString override_;
    QString unit_;
    bool started_ = false;
    PaletteColors current_ = builtinPalette();
    QFileSystemWatcher watcher_;
    QTimer debounce_;
    QTimer poll_;
};

}  // namespace graphite
