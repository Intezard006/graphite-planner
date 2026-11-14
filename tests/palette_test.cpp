// Tests for the colour logic: pywal, image extraction, tool output parsing.
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QImage>
#include <QJsonDocument>
#include <QPainter>
#include <QTemporaryDir>
#include <cmath>
#include <cstdio>

#include "desktop/palette.hpp"
#include "ui/unitart.hpp"

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
    // Never read the real home: HOME and XDG_STATE_HOME point into a temp dir.
    QTemporaryDir fakeHome;
    CHECK(fakeHome.isValid());
    qputenv("HOME", fakeHome.path().toUtf8());
    qputenv("XDG_STATE_HOME", (fakeHome.path() + "/state").toUtf8());
    qputenv("XDG_CONFIG_HOME", (fakeHome.path() + "/config").toUtf8());
    qunsetenv("GRAPHITE_WALLPAPER");

    bool ok = false;

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

    // 11. NERV-HQ wall.toml parsing and precedence.
    {
        WallState a = parseWallToml("# c\nmode = \"image\"  # x\n  image='/a/b c.png'\n");
        CHECK(a.mode == "image" && a.image == "/a/b c.png");
        WallState b = parseWallToml("[wall]\r\nmode=live\r\nimage = \"\"\n");
        CHECK(b.mode == "live" && b.image.isEmpty());
        CHECK(parseWallToml("").mode.isEmpty());
        CHECK(parseWallToml("garbage\nmode").mode.isEmpty());

        CHECK(nervWallTomlPath() == fakeHome.path() + "/state/nerv/wall.toml");
        qunsetenv("XDG_STATE_HOME");
        CHECK(nervWallTomlPath() == fakeHome.path() + "/.local/state/nerv/wall.toml");
        qputenv("XDG_STATE_HOME", (fakeHome.path() + "/state").toUtf8());

        QImage red(32, 32, QImage::Format_RGB32);
        red.fill(QColor(200, 30, 30));
        const QString png = fakeHome.path() + "/wall.png";
        CHECK(red.save(png));
        const QString good = "mode = \"image\"\nimage = \"" + png + "\"\n";
        CHECK(wallpaperFromWallToml(good) == png);
        CHECK(wallpaperFromWallToml("mode = \"live\"\nimage = \"" + png + "\"\n").isEmpty());
        CHECK(wallpaperFromWallToml("mode = \"image\"\nimage = \"/no/such.png\"\n").isEmpty());

        // Provider: no wall.toml and nothing else -> built-in; image mode -> that image; live -> built-in.
        auto writeToml = [&](const QString& text) {
            QDir().mkpath(fakeHome.path() + "/state/nerv");
            QFile f(nervWallTomlPath());
            CHECK(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
            f.write(text.toUtf8());
        };
        {
            PaletteProvider p1;
            p1.start();
            CHECK(!p1.describe().contains(png));
        }
        writeToml(good);
        {
            PaletteProvider p2;
            p2.start();
            CHECK(p2.describe() == "wallpaper image " + png);
        }
        writeToml("mode = \"live\"\n");
        {
            PaletteProvider p3;
            p3.start();
            CHECK(!p3.describe().contains(png));
        }

        // Precedence: wall.toml image > pywal colours > (live) wallpaper finders; override beats all.
        QDir().mkpath(fakeHome.path() + "/.cache/wal");
        {
            QFile f(fakeHome.path() + "/.cache/wal/colors.json");
            CHECK(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
            f.write(R"({"special":{"background":"#0f1419","foreground":"#e6e1cf"},
                        "colors":{"color1":"#ff3333","color2":"#33ff33","color4":"#3333ff","color5":"#909090"}})");
        }
        {
            PaletteProvider p4;  // wall.toml is "live": pywal answers
            p4.start();
            CHECK(p4.describe().startsWith("pywal colours"));
            CHECK(!p4.polling());
        }
        writeToml(good);
        {
            PaletteProvider p5;  // an NERV image wins over pywal
            p5.start();
            CHECK(p5.describe() == "wallpaper image " + png);
        }
        {
            QImage blue(32, 32, QImage::Format_RGB32);
            blue.fill(QColor(30, 30, 200));
            const QString png2 = fakeHome.path() + "/other.png";
            CHECK(blue.save(png2));
            PaletteProvider p6;  // --wallpaper beats wall.toml
            p6.setWallpaperOverride(png2);
            p6.start();
            CHECK(p6.describe() == "wallpaper image " + png2);
        }
    }

    // NERV palette: defaults without a file, partial and invalid files, derived surfaces.
    {
        CHECK(nervPaletteTomlPath() == fakeHome.path() + "/config/nerv/palette.toml");
        const PaletteColors d = nervPalette();  // no file
        CHECK(d.source == "NERV" && d.bg == QColor("#000000") && d.text == QColor("#FF8324"));
        CHECK(d.warm == QColor("#FF3810") && d.alarm == QColor("#FF2030") && d.tones.dim == QColor("#A8521A"));
        CHECK(d.tones.line.rgb() == QColor("#4CE8A8").rgb() && d.panel != d.bg && d.panel.redF() < 0.06);
        CHECK(unitPalette("nerv") == d && unitIds().contains("nerv"));
        const PaletteColors part = paletteFromNervToml("# c\nsignal = \"#112233\"  # note\ndim='#445566'\nfield = #000001\n[x]\n");
        CHECK(part.text == QColor("#112233") && part.tones.dim == QColor("#445566") && part.bg == QColor("#000001"));
        const PaletteColors bad = paletteFromNervToml("signal = \"red\"\nhalo = #12345\nalarm = \"#GGGGGG\"\ngrid\n= =\naccent = \"#00FF00\"\n");
        CHECK(bad == d);  // invalid keys keep defaults; accent is not used
        QDir().mkpath(fakeHome.path() + "/config/nerv");
        QFile f(nervPaletteTomlPath());
        CHECK(f.open(QIODevice::WriteOnly));
        f.write("signal = \"#102030\"\n");
        f.close();
        CHECK(nervPalette().text == QColor("#102030"));
        QFile::remove(nervPaletteTomlPath());
    }

    // NERV logo mask: red is opaque, the checkerboard greys and white are clear, edges in between.
    {
        QImage logo(4, 1, QImage::Format_RGB32);
        logo.setPixel(0, 0, qRgb(255, 0, 0));
        logo.setPixel(1, 0, qRgb(235, 235, 235));
        logo.setPixel(2, 0, qRgb(255, 255, 255));
        logo.setPixel(3, 0, qRgb(255, 128, 128));
        const QImage m = nervLogoMask(logo);
        CHECK(m.format() == QImage::Format_Alpha8);
        CHECK(m.constScanLine(0)[0] == 255 && m.constScanLine(0)[1] == 0 && m.constScanLine(0)[2] == 0);
        CHECK(std::abs(m.constScanLine(0)[3] - 127) <= 1);
        QImage a(1, 1, QImage::Format_ARGB32);  // a real alpha channel multiplies in
        a.setPixel(0, 0, qRgba(255, 0, 0, 128));
        CHECK(std::abs(nervLogoMask(a).constScanLine(0)[0] - 128) <= 1);
        CHECK(nervLogoMask(QImage()).isNull());
    }

    if (failures == 0) std::printf("all palette tests passed\n");
    return failures == 0 ? 0 : 1;
}
