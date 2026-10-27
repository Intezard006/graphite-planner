// Tests for the colour logic: Material (matugen), pywal, image extraction, tool output parsing.
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QImage>
#include <QJsonDocument>
#include <QPainter>
#include <QTemporaryDir>
#include <cmath>
#include <cstdio>

#include "ui/palette.hpp"

using namespace graphite;

static int failures = 0;
#define CHECK(cond)                                                      \
    do {                                                                 \
        if (!(cond)) {                                                   \
            std::printf("FAIL line %d: %s\n", __LINE__, #cond);          \
            ++failures;                                                  \
        }                                                                \
    } while (0)

static QJsonObject json(const char* text) { return QJsonDocument::fromJson(text).object(); }
static double hue(const QColor& c) { return c.hslHueF() * 360.0; }
static double hueDiff(double a, double b) {
    double d = std::fabs(a - b);
    return std::min(d, 360 - d);
}

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);

    // 1. A dark matugen scheme is used as it is.
    bool ok = false;
    PaletteColors dark = paletteFromMaterial(
        json(R"({"background":"#141218","on_surface":"#e6e0e9","primary":"#d0bcff","tertiary":"#efb8c8",
                 "surface_container_low":"#1d1b20","source_color":"#6750a4"})"), &ok);
    CHECK(ok);
    CHECK(dark.bg == QColor("#141218"));
    CHECK(dark.panel == QColor("#1d1b20"));
    CHECK(dark.text == QColor("#e6e0e9"));
    CHECK(hueDiff(hue(dark.accent), hue(QColor("#d0bcff"))) < 3);
    CHECK(dark.accent.lightnessF() >= 0.62);
    CHECK(hueDiff(hue(dark.warm), 28) < 4);

    // 2. A light scheme keeps the app dark and lends only its hue.
    PaletteColors light = paletteFromMaterial(
        json(R"({"background":"#fef7ff","on_surface":"#1d1b20","primary":"#6750a4","tertiary":"#7d5260",
                 "source_color":"#6750a4"})"), &ok);
    CHECK(ok);
    CHECK(light.bg.lightnessF() < 0.12);
    CHECK(light.text.lightnessF() > 0.85);
    CHECK(hueDiff(hue(light.bg), hue(QColor("#6750a4"))) < 6);
    CHECK(light.accent.lightnessF() > 0.6);

    // 3. Missing keys: not ok, built-in colours come back.
    PaletteColors bad = paletteFromMaterial(json(R"({"primary":"#ff0000"})"), &ok);
    CHECK(!ok && bad == builtinPalette());

    // 4. pywal: the most saturated real colour becomes the accent.
    PaletteColors wal = paletteFromPywal(
        json(R"({"special":{"background":"#0f1419","foreground":"#e6e1cf"},
                 "colors":{"color1":"#808080","color2":"#c2d94c","color3":"#7a7a7a","color4":"#3f6fd9",
                           "color5":"#909090"}})"), &ok);
    CHECK(ok);
    CHECK(wal.bg == QColor("#0f1419"));
    CHECK(wal.accent.hslSaturationF() > 0.5);
    CHECK(hueDiff(hue(wal.accent), hue(QColor("#c2d94c"))) < 4 || hueDiff(hue(wal.accent), hue(QColor("#3f6fd9"))) < 4);
    paletteFromPywal(json(R"({"colors":{"color0":"#000000"}})"), &ok);
    CHECK(!ok);

    // 5. Image: mostly teal, a patch of orange, a dark corner.
    QImage img(200, 120, QImage::Format_RGB32);
    img.fill(QColor(20, 140, 150));
    {
        QPainter p(&img);
        p.fillRect(QRect(0, 0, 200, 25), QColor(235, 120, 40));
        p.fillRect(QRect(0, 90, 60, 30), QColor(10, 10, 14));
    }
    PaletteColors pi = paletteFromImage(img);
    CHECK(hueDiff(hue(pi.accent), 184) < 14);              // teal
    CHECK(hueDiff(hue(pi.structure), hue(pi.accent)) >= 30); // a genuinely different second colour
    CHECK(pi.bg.lightnessF() < 0.1);
    CHECK(hueDiff(hue(pi.bg), hue(pi.accent)) < 8);          // the dark surface is tinted by it
    CHECK(pi.text.lightnessF() > 0.85);
    CHECK(pi.accent.lightnessF() > 0.65);

    // 6. A greyscale picture has no hue: the built-in accents stay.
    QImage grey(100, 60, QImage::Format_RGB32);
    grey.fill(QColor(120, 120, 120));
    CHECK(paletteFromImage(grey).accent == builtinPalette().accent);
    CHECK(paletteFromImage(QImage()) == builtinPalette());

    // 7. Same picture, same colours (the extraction is deterministic).
    CHECK(paletteFromImage(img) == paletteFromImage(img));

    // 8. Finding the wallpaper in what swww, awww and hyprpaper print.
    QTemporaryDir dir;
    const QString wall = dir.path() + "/wall one.jpg";
    QFile f(wall);
    CHECK(f.open(QIODevice::WriteOnly));
    f.write("x");
    f.close();
    CHECK(wallpaperFromToolOutput(": DP-1: 1920x1080, scale: 1, currently displaying: image: " + wall + "\n") == wall);
    CHECK(wallpaperFromToolOutput("DP-1 = " + wall + "\n") == wall);
    CHECK(wallpaperFromToolOutput("eDP-1 = /does/not/exist.png\nDP-1 = " + wall + "\n") == wall);
    CHECK(wallpaperFromToolOutput("nothing useful here").isEmpty());
    CHECK(wallpaperFromToolOutput(": DP-1: 1x1, currently displaying: color: 000000\n").isEmpty());

    // 9. Only the built-in set carries v0.1's hand-tuned tones; derived palettes leave them unset.
    CHECK(builtinPalette().tones.dim == QColor(0x8a, 0x82, 0xa0));
    CHECK(!paletteFromImage(img).tones.dim.isValid());

    // 10. Switching to the built-in set while running (Ctrl+P) and back.
    PaletteProvider provider;
    provider.start();
    int changes = 0;
    QObject::connect(&provider, &PaletteProvider::changed, [&changes] { ++changes; });
    provider.setBuiltinOnly(true);
    CHECK(provider.current() == builtinPalette());
    CHECK(provider.builtinOnly());
    CHECK(!provider.polling());  // nothing to poll for: no timer wakeups
    provider.setBuiltinOnly(false);
    CHECK(!provider.builtinOnly());
    CHECK(changes <= 2);

    if (failures == 0) std::printf("all palette tests passed\n");
    return failures == 0 ? 0 : 1;
}
