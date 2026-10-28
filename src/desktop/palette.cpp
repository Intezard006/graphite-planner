#include "desktop/palette.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QProcess>
#include <QRegularExpression>
#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace graphite {

namespace {

qreal clamp01(qreal v) { return std::min<qreal>(1.0, std::max<qreal>(0.0, v)); }

QColor hsl(qreal hueDeg, qreal s, qreal l) {
    qreal h = std::fmod(hueDeg, 360.0);
    if (h < 0) h += 360.0;
    return QColor::fromHslF(h / 360.0, clamp01(s), clamp01(l));
}

qreal hueOf(const QColor& c) {
    const qreal h = c.hslHueF();
    return h < 0 ? 0 : h * 360.0;
}

QColor mixColors(const QColor& a, const QColor& b, qreal t) {
    return QColor::fromRgbF(a.redF() * (1 - t) + b.redF() * t, a.greenF() * (1 - t) + b.greenF() * t,
                            a.blueF() * (1 - t) + b.blueF() * t);
}

// Lift a colour until it reads on a dark background.
QColor readable(const QColor& c, qreal minLightness = 0.62) {
    qreal h = hueOf(c);
    qreal s = c.hslSaturationF();
    qreal l = c.lightnessF();
    if (l < minLightness) l = minLightness;
    return hsl(h, s, l);
}

QColor warmFrom(const QColor& accent) {
    // Warm orange with the accent's own saturation and lightness, so it belongs to the set.
    return hsl(28, std::max<qreal>(0.55, accent.hslSaturationF()), std::min<qreal>(0.72, std::max<qreal>(0.58, accent.lightnessF())));
}

// A dark, hue-tinted surface set around `hue`.
void darkSurfaces(PaletteColors& p, qreal hue, qreal tint) {
    p.bg = hsl(hue, tint, 0.065);
    p.panel = hsl(hue, tint, 0.085);
    p.text = hsl(hue, 0.16, 0.92);
}

QColor parseHex(const QJsonValue& v) {
    const QColor c(v.toString());
    return c;
}

}  // namespace

PaletteColors builtinPalette() {
    PaletteColors p;
    p.bg = QColor(0x0c, 0x0a, 0x12);
    p.panel = QColor(0x10, 0x0d, 0x18);
    p.text = QColor(0xe8, 0xe4, 0xf2);
    p.accent = QColor(0x9c, 0xff, 0x3c);     // neon green
    p.structure = QColor(0x9a, 0x6c, 0xff);  // EVA-01 purple
    p.warm = QColor(0xff, 0x7a, 0x1a);
    p.tones.dim = QColor(0x8a, 0x82, 0xa0);
    p.tones.faint = QColor(0x4d, 0x45, 0x66);
    p.tones.line = QColor(0x2a, 0x22, 0x3d);
    p.tones.dimClear = QColor(0xc2, 0xbb, 0xd6);
    p.tones.faintClear = QColor(0x9d, 0x95, 0xb8);
    p.tones.lineClear = QColor(0xc9, 0xb8, 0xff, 85);
    p.tones.selection = QColor(0x9a, 0x6c, 0xff, 90);
    p.source = "built-in";
    return p;
}

QStringList unitIds() { return {"unit01", "unit00", "unit02"}; }

QString unitName(const QString& id) {
    if (id == "unit00") return "Unit-00";
    if (id == "unit02") return "Unit-02";
    return "Unit-01";
}

PaletteColors unitPalette(const QString& id) {
    if (id == "unit00") {  // Rei: blue armour, white, the original yellow-orange
        PaletteColors p;
        p.bg = QColor(0x09, 0x0c, 0x13);
        p.panel = QColor(0x0d, 0x11, 0x1b);
        p.text = QColor(0xe4, 0xea, 0xf4);
        p.accent = QColor(0xd8, 0xe6, 0xff);     // white-blue
        p.structure = QColor(0x3f, 0x7c, 0xff);  // armour blue
        p.warm = QColor(0xff, 0xb0, 0x2e);
        p.source = "Unit-00";
        return p;
    }
    if (id == "unit02") {  // Asuka: red armour, orange, yellow
        PaletteColors p;
        p.bg = QColor(0x12, 0x09, 0x0a);
        p.panel = QColor(0x17, 0x0c, 0x0d);
        p.text = QColor(0xf3, 0xe6, 0xe4);
        p.accent = QColor(0xff, 0x8a, 0x1e);     // orange
        p.structure = QColor(0xe8, 0x33, 0x3e);  // armour red
        p.warm = QColor(0xff, 0xd2, 0x3f);
        p.source = "Unit-02";
        return p;
    }
    PaletteColors p = builtinPalette();
    p.source = "Unit-01";
    return p;
}

PaletteColors paletteFromMaterial(const QJsonObject& o, bool* ok) {
    const QColor bg = parseHex(o.value("background"));
    const QColor text = parseHex(o.value("on_surface"));
    const QColor primary = parseHex(o.value("primary"));
    if (ok) *ok = bg.isValid() && text.isValid() && primary.isValid();
    if (!bg.isValid() || !text.isValid() || !primary.isValid()) return builtinPalette();

    QColor tertiary = parseHex(o.value("tertiary"));
    if (!tertiary.isValid()) tertiary = parseHex(o.value("secondary"));
    if (!tertiary.isValid()) tertiary = primary;

    PaletteColors p;
    if (bg.lightnessF() < 0.45) {
        // Dark scheme: use it as it is.
        p.bg = bg;
        const QColor low = parseHex(o.value("surface_container_low"));
        p.panel = low.isValid() ? low : mixColors(bg, text, 0.04);
        p.text = text;
        p.accent = readable(primary);
        p.structure = readable(tertiary, 0.64);
    } else {
        // Light scheme (matugen follows the desktop's light/dark setting): keep this app dark
        // and borrow only the hues.
        QColor seed = parseHex(o.value("source_color"));
        if (!seed.isValid()) seed = primary;
        darkSurfaces(p, hueOf(seed), 0.28);
        p.accent = hsl(hueOf(primary), std::max<qreal>(0.55, primary.hslSaturationF()), 0.74);
        p.structure = hsl(hueOf(tertiary), std::max<qreal>(0.45, tertiary.hslSaturationF()), 0.72);
    }
    p.warm = warmFrom(p.accent);
    p.source = "matugen colours";
    return p;
}

PaletteColors paletteFromPywal(const QJsonObject& wal, bool* ok) {
    const QJsonObject special = wal.value("special").toObject();
    const QJsonObject colors = wal.value("colors").toObject();

    QColor bg = parseHex(special.value("background"));
    if (!bg.isValid()) bg = parseHex(colors.value("color0"));
    QColor fg = parseHex(special.value("foreground"));
    if (!fg.isValid()) fg = parseHex(colors.value("color15"));

    // The most saturated of the real accent slots.
    std::vector<QColor> candidates;
    for (int i = 1; i <= 14; ++i) {
        const QColor c = parseHex(colors.value(QString("color%1").arg(i)));
        if (c.isValid() && c.hslSaturationF() > 0.15 && c.lightnessF() > 0.2) candidates.push_back(c);
    }
    std::sort(candidates.begin(), candidates.end(), [](const QColor& a, const QColor& b) {
        return a.hslSaturationF() > b.hslSaturationF();
    });
    if (ok) *ok = bg.isValid() && fg.isValid() && !candidates.empty();
    if (!bg.isValid() || !fg.isValid() || candidates.empty()) return builtinPalette();

    PaletteColors p;
    if (bg.lightnessF() < 0.45) {
        p.bg = bg;
        p.panel = mixColors(bg, fg, 0.04);
        p.text = fg;
    } else {
        darkSurfaces(p, hueOf(candidates.front()), 0.28);
    }
    p.accent = readable(candidates.front());
    p.structure = readable(candidates.size() > 1 ? candidates[1] : candidates.front(), 0.64);
    p.warm = warmFrom(p.accent);
    p.source = "pywal colours";
    return p;
}

PaletteColors paletteFromImage(const QImage& source) {
    PaletteColors fallback = builtinPalette();
    if (source.isNull()) return fallback;

    const QImage img = source.scaled(96, 96, Qt::KeepAspectRatio, Qt::SmoothTransformation)
                           .convertToFormat(QImage::Format_RGB32);
    struct Px {
        double r, g, b;
    };
    std::vector<Px> px;
    px.reserve(static_cast<size_t>(img.width() * img.height()));
    for (int y = 0; y < img.height(); ++y) {
        const QRgb* row = reinterpret_cast<const QRgb*>(img.constScanLine(y));
        for (int x = 0; x < img.width(); ++x) px.push_back({qRed(row[x]) / 255.0, qGreen(row[x]) / 255.0, qBlue(row[x]) / 255.0});
    }
    if (px.empty()) return fallback;

    // k-means, k = 8, started from pixels spread over the brightness range (deterministic).
    constexpr int K = 8;
    std::vector<Px> sorted = px;
    std::sort(sorted.begin(), sorted.end(), [](const Px& a, const Px& b) { return a.r + a.g + a.b < b.r + b.g + b.b; });
    std::array<Px, K> centre;
    for (int k = 0; k < K; ++k) centre[static_cast<size_t>(k)] = sorted[(sorted.size() - 1) * static_cast<size_t>(2 * k + 1) / (2 * K)];

    std::vector<int> label(px.size(), 0);
    for (int iter = 0; iter < 10; ++iter) {
        std::array<Px, K> sum{};
        std::array<int, K> n{};
        for (size_t i = 0; i < px.size(); ++i) {
            int best = 0;
            double bd = 1e9;
            for (int k = 0; k < K; ++k) {
                const Px& c = centre[static_cast<size_t>(k)];
                const double d = (px[i].r - c.r) * (px[i].r - c.r) + (px[i].g - c.g) * (px[i].g - c.g) + (px[i].b - c.b) * (px[i].b - c.b);
                if (d < bd) {
                    bd = d;
                    best = k;
                }
            }
            label[i] = best;
            sum[static_cast<size_t>(best)].r += px[i].r;
            sum[static_cast<size_t>(best)].g += px[i].g;
            sum[static_cast<size_t>(best)].b += px[i].b;
            ++n[static_cast<size_t>(best)];
        }
        for (int k = 0; k < K; ++k) {
            const size_t ks = static_cast<size_t>(k);
            if (n[ks] > 0) centre[ks] = {sum[ks].r / n[ks], sum[ks].g / n[ks], sum[ks].b / n[ks]};
        }
    }
    std::array<int, K> count{};
    for (int l : label) ++count[static_cast<size_t>(l)];

    struct Cluster {
        QColor colour;
        double weight;
        double score;
    };
    std::vector<Cluster> clusters;
    for (int k = 0; k < K; ++k) {
        const size_t ks = static_cast<size_t>(k);
        if (count[ks] == 0) continue;
        const QColor c = QColor::fromRgbF(centre[ks].r, centre[ks].g, centre[ks].b);
        const double share = static_cast<double>(count[ks]) / static_cast<double>(px.size());
        const double s = c.hsvSaturationF();
        const double v = c.valueF();
        // Colourful, reasonably bright, reasonably common clusters make the best accents.
        const double usable = (v > 0.22 && v < 0.98) ? 1.0 : 0.25;
        clusters.push_back({c, share, std::sqrt(share) * std::pow(s, 1.3) * usable});
    }
    std::sort(clusters.begin(), clusters.end(), [](const Cluster& a, const Cluster& b) { return a.score > b.score; });

    // A near-greyscale picture has no hue to offer: keep the built-in accents.
    const bool colourful = !clusters.empty() && clusters.front().colour.hsvSaturationF() > 0.14 && clusters.front().score > 0.02;

    PaletteColors p = fallback;
    if (colourful) {
        const QColor main = clusters.front().colour;
        const qreal h0 = hueOf(main);
        const qreal s0 = std::min<qreal>(0.9, std::max<qreal>(0.5, main.hslSaturationF()));
        p.accent = hsl(h0, s0, 0.74);

        // The next colourful cluster whose hue is clearly different gives the second colour.
        qreal h1 = h0 + 40;
        qreal s1 = s0;
        for (size_t i = 1; i < clusters.size(); ++i) {
            const qreal h = hueOf(clusters[i].colour);
            qreal diff = std::fabs(h - h0);
            diff = std::min(diff, 360 - diff);
            if (diff >= 35 && clusters[i].colour.hsvSaturationF() > 0.2) {
                h1 = h;
                s1 = std::min<qreal>(0.85, std::max<qreal>(0.45, clusters[i].colour.hslSaturationF()));
                break;
            }
        }
        p.structure = hsl(h1, s1, 0.72);
        p.warm = warmFrom(p.accent);
        darkSurfaces(p, h0, std::min<qreal>(0.34, 0.14 + 0.25 * main.hsvSaturationF()));
        p.tones = {};  // the built-in tones only suit the built-in surfaces
    }
    p.source = "wallpaper image";
    return p;
}

QString wallpaperFromToolOutput(const QString& output) {
    // swww / awww: "...currently displaying: image: /path/to/file.jpg"
    static const QRegularExpression swww(R"(image:\s*(\S.*?)\s*$)", QRegularExpression::MultilineOption);
    QRegularExpressionMatchIterator it = swww.globalMatch(output);
    while (it.hasNext()) {
        const QString path = it.next().captured(1);
        if (QFileInfo::exists(path)) return path;
    }
    // hyprpaper: "DP-1 = /path/to/file.png"
    static const QRegularExpression eq(R"(^\s*[^=\n]+=\s*(/\S.*?)\s*$)", QRegularExpression::MultilineOption);
    it = eq.globalMatch(output);
    while (it.hasNext()) {
        const QString path = it.next().captured(1);
        if (QFileInfo::exists(path)) return path;
    }
    return QString();
}

// ------------------------------------------------------------------ provider

namespace {

QString home(const QString& rel) { return QDir::homePath() + "/" + rel; }

QByteArray readSmall(const QString& path) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return QByteArray();
    return f.read(1 << 20);
}

QString runTool(const QString& program, const QStringList& args) {
    QProcess p;
    p.start(program, args);
    if (!p.waitForStarted(300)) return QString();
    if (!p.waitForFinished(1500)) {
        p.kill();
        return QString();
    }
    return QString::fromUtf8(p.readAllStandardOutput());
}

}  // namespace

PaletteProvider::PaletteProvider(QObject* parent) : QObject(parent) {
    debounce_.setSingleShot(true);
    debounce_.setInterval(450);
    connect(&debounce_, &QTimer::timeout, this, &PaletteProvider::refresh);
    connect(&watcher_, &QFileSystemWatcher::fileChanged, this, [this] { debounce_.start(); });
    connect(&watcher_, &QFileSystemWatcher::directoryChanged, this, [this] { debounce_.start(); });
    // Wallpaper changers that leave no file to watch are caught by a slow poll.
    poll_.setInterval(20000);
    connect(&poll_, &QTimer::timeout, this, &PaletteProvider::refresh);
}

QString PaletteProvider::findWallpaper() const {
    // ML4W keeps the path of the current wallpaper in a one-line cache file.
    const QString ml4w = QString::fromUtf8(readSmall(home(".cache/ml4w/hyprland-dotfiles/current_wallpaper"))).trimmed();
    if (!ml4w.isEmpty() && QFileInfo::exists(ml4w)) return ml4w;

    // matugen's pywal-style file records the image it was made from.
    const QJsonObject wal = QJsonDocument::fromJson(readSmall(home(".cache/wal/colors.json"))).object();
    const QString fromWal = wal.value("wallpaper").toString();
    if (!fromWal.isEmpty() && QFileInfo::exists(fromWal)) return fromWal;

#ifdef Q_OS_MACOS
    return QString();  // no wallpaper daemons here: the built-in palette
#endif
    for (const QStringList& cmd : {QStringList{"awww", "query"}, QStringList{"swww", "query"}}) {
        const QString path = wallpaperFromToolOutput(runTool(cmd[0], cmd.mid(1)));
        if (!path.isEmpty()) return path;
    }
    const QString hp = wallpaperFromToolOutput(runTool("hyprctl", {"hyprpaper", "listactive"}));
    if (!hp.isEmpty()) return hp;
    return QString();
}

PaletteColors PaletteProvider::detect() {
    if (!unit_.isEmpty()) return unitPalette(unit_);

    QString wall = override_;
    if (wall.isEmpty()) wall = qEnvironmentVariable("GRAPHITE_WALLPAPER");
    if (!wall.isEmpty()) {
        PaletteColors p = paletteFromImage(QImage(wall));
        p.source = "wallpaper image " + wall;
        return p;
    }

    bool ok = false;
    const QString material = home(".config/ml4w/colors/colors.json");
    if (QFileInfo::exists(material)) {
        PaletteColors p = paletteFromMaterial(QJsonDocument::fromJson(readSmall(material)).object(), &ok);
        if (ok) {
            p.source = "matugen colours " + material;
            return p;
        }
    }
    const QString walFile = home(".cache/wal/colors.json");
    if (QFileInfo::exists(walFile)) {
        const QJsonObject o = QJsonDocument::fromJson(readSmall(walFile)).object();
        PaletteColors p = paletteFromPywal(o, &ok);
        if (ok) {
            p.source = "pywal colours " + walFile;
            return p;
        }
    }
    const QString image = findWallpaper();
    if (!image.isEmpty()) {
        PaletteColors p = paletteFromImage(QImage(image));
        p.source = "wallpaper image " + image;
        return p;
    }
    return builtinPalette();
}

void PaletteProvider::rewatch() {
    const QStringList files = {home(".config/ml4w/colors/colors.json"), home(".cache/wal/colors.json"),
                               home(".cache/ml4w/hyprland-dotfiles/current_wallpaper")};
    const QStringList dirs = {home(".config/ml4w/colors"), home(".cache/wal"), home(".cache/ml4w/hyprland-dotfiles")};
    // Tools often replace a file instead of rewriting it, which drops the watch: add again.
    for (const QString& f : files)
        if (QFileInfo::exists(f) && !watcher_.files().contains(f)) watcher_.addPath(f);
    for (const QString& d : dirs)
        if (QFileInfo(d).isDir() && !watcher_.directories().contains(d)) watcher_.addPath(d);
}

void PaletteProvider::refresh() {
    rewatch();
    const PaletteColors next = detect();
    if (next != current_) {
        current_ = next;
        emit changed(current_);
    } else {
        current_.source = next.source;
    }
    updatePoll();
}

// The slow poll is only for wallpapers found through swww/awww/hyprpaper, which leave no file
// to watch. Colour files (matugen, pywal) and fixed choices are covered by the watcher, so the
// app does not wake up every 20 s for nothing.
void PaletteProvider::updatePoll() {
    const QString& s = current_.source;
    const bool watched = s.startsWith("matugen") || s.startsWith("pywal");
    const bool fixed = !unit_.isEmpty() || !override_.isEmpty() || !qEnvironmentVariable("GRAPHITE_WALLPAPER").isEmpty();
#ifdef Q_OS_MACOS
    const bool nothingToAsk = true;  // findWallpaper() asks no tool there, so a poll would find nothing
#else
    const bool nothingToAsk = false;
#endif
    if (watched || fixed || nothingToAsk) poll_.stop();
    else if (!poll_.isActive()) poll_.start();
}

void PaletteProvider::start() {
    started_ = true;
    current_ = detect();
    rewatch();
    updatePoll();
}

void PaletteProvider::setUnit(const QString& id) {
    unit_ = id.isEmpty() || unitIds().contains(id) ? id : QString("unit01");
    if (!started_) return;
    refresh();
}

}  // namespace graphite
