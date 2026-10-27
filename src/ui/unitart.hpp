// The Evangelion unit drawn faintly into the sidebar's background, full body, front view:
// Art files live in the user's data folder (artDir()), none ship with Graphite: custom.png is the
// user's own picture, unit00/01/02.png come from "Download Eva art" (after a consent click).
// Scaled once per size into a pixmap; a repaint only draws that.
#pragma once

#include <QRect>
#include <QString>
#include <QByteArray>
#include <QImage>
#include <QObject>

class QPainter;

namespace graphite {

struct PaletteColors;

// <GenericDataLocation>/graphite/art (~/.local/share/graphite/art on Linux).
QString artDir();

// The PNG for "unit01", "unit00", "unit02" or "custom"; empty when the file is missing.
QByteArray unitArt(const QString& unit);

// Drop the cached pixmap (after the files changed).
void forgetUnitArt();

// Transparent margins cut off, at most 800 px tall.
QImage prepareArt(const QImage& image);

// Validate a PNG chosen by the user and save it as custom.png. False (with `error`) when it is
// not a PNG, is empty or is absurdly large.
bool saveCustomArt(const QString& path, QString* error);

// "Download Eva art": the three files named by the MediaWiki API of evangelion.fandom.com,
// prepared and saved in artDir(). One request round at a time; finished() says how it went.
// Call start() only after the user said yes.
class ArtDownload : public QObject {
    Q_OBJECT
public:
    explicit ArtDownload(QObject* parent = nullptr) : QObject(parent) {}
    void start();
signals:
    void finished(int saved, const QString& error);  // error empty when all three were saved
};

// The unit that suits these colours: a unit palette names itself; for wallpaper colours the
// one whose hues are nearest (violet / green -> 01, blue -> 00, red / orange -> 02).
QString unitForColours(const PaletteColors& colours);

// Paint `unit` centred in `area`, as large as fits.
void paintUnitArt(QPainter& p, const QRect& area, const QString& unit);

}  // namespace graphite
