#include "ui/unitart.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSaveFile>
#include <QStandardPaths>
#include <QUrl>
#include <QUrlQuery>
#include <memory>
#include <QPainter>
#include <QPixmap>
#include <cmath>
#ifdef __GLIBC__
#include <malloc.h>
#endif

#include "desktop/palette.hpp"
#include "ui/theme.hpp"

namespace graphite {

namespace {
// The pixmap is made on first use (a QPixmap before the QApplication aborts).
QString& cachedKey() {
    static QString key;
    return key;
}
QPixmap& cached() {
    static QPixmap pixmap;
    return pixmap;
}
}

QString artDir() { return QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + "/graphite/art"; }

QByteArray unitArt(const QString& unit) {
    const QString name = unit == "custom" || unit == "unit00" || unit == "unit02" ? unit : QString("unit01");
    QFile f(artDir() + "/" + name + ".png");
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}

void forgetUnitArt() {
    cachedKey().clear();
    cached() = QPixmap();
}

QImage prepareArt(const QImage& image) {
    if (image.isNull()) return QImage();
    QImage img = image.convertToFormat(QImage::Format_ARGB32);
    int x0 = img.width(), y0 = img.height(), x1 = -1, y1 = -1;
    for (int y = 0; y < img.height(); ++y) {
        const QRgb* row = reinterpret_cast<const QRgb*>(img.constScanLine(y));
        for (int x = 0; x < img.width(); ++x) {
            if (qAlpha(row[x]) < 8) continue;
            x0 = std::min(x0, x);
            x1 = std::max(x1, x);
            y0 = std::min(y0, y);
            y1 = std::max(y1, y);
        }
    }
    if (x1 < 0) return QImage();  // fully transparent: nothing to show
    img = img.copy(QRect(QPoint(x0, y0), QPoint(x1, y1)));
    if (img.height() > 800) img = img.scaledToHeight(800, Qt::SmoothTransformation);
    return img;
}

static bool savePng(const QImage& img, const QString& path) {
    QDir().mkpath(QFileInfo(path).absolutePath());
    QSaveFile f(path);
    return f.open(QIODevice::WriteOnly) && img.save(&f, "PNG") && f.commit();
}

bool saveCustomArt(const QString& path, QString* error) {
    auto fail = [&](const QString& why) {
        if (error) *error = why;
        return false;
    };
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return fail("Cannot read that file");
    if (f.size() > 40 * 1024 * 1024) return fail("That image is too large");
    const QByteArray data = f.readAll();
    {
        QImageReader r(path, "PNG");
        if (!r.canRead()) return fail("That is not a PNG image");
        const QSize s = r.size();
        if (s.width() > 12000 || s.height() > 12000 || s.isEmpty()) return fail("That image has unusable dimensions");
    }
    const QImage img = QImage::fromData(data, "PNG");
    if (img.isNull()) return fail("That is not a PNG image");
    // A transparent border is cut and the height capped at 800 px.
    const QImage out = prepareArt(img);
    if (out.isNull()) return fail("That image is empty");
    if (!savePng(out, artDir() + "/custom.png")) return fail("Cannot save into " + artDir());
    forgetUnitArt();
    return true;
}

void ArtDownload::start() {
    // The file titles on evangelion.fandom.com; the real (hashed) URLs come from the API.
    struct File {
        const char* title;
        const char* unit;
    };
    static const File files[] = {{"File:Evangelion_Unit-01_front1.png", "unit01"},
                                 {"File:Evangelion_Unit-00_Blue_front.png", "unit00"},
                                 {"File:Evangelion_Unit-02_front.png", "unit02"}};
    auto* net = new QNetworkAccessManager(this);
    QStringList titles;
    for (const File& f : files) titles << f.title;
    QUrl api("https://evangelion.fandom.com/api.php");
    QUrlQuery q;
    q.addQueryItem("action", "query");
    q.addQueryItem("titles", titles.join('|'));
    q.addQueryItem("prop", "imageinfo");
    q.addQueryItem("iiprop", "url");
    q.addQueryItem("format", "json");
    api.setQuery(q);
    auto done = [this, net](int saved, const QString& error) {
        emit finished(saved, error);
        net->deleteLater();
    };
    auto request = [](const QUrl& url) {
        QNetworkRequest r(url);
        r.setTransferTimeout(30000);  // no hang
        r.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
        r.setHeader(QNetworkRequest::UserAgentHeader, "Graphite (personal notes app)");
        return r;
    };
    QNetworkReply* reply = net->get(request(api));
    connect(reply, &QNetworkReply::finished, this, [=] {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) return done(0, "Download failed: " + reply->errorString());
        const QJsonObject pages = QJsonDocument::fromJson(reply->readAll()).object()["query"].toObject()["pages"].toObject();
        // title (spaces or underscores) -> URL
        QMap<QString, QUrl> urls;
        for (const QJsonValue& v : pages) {
            const QJsonObject page = v.toObject();
            const QJsonArray info = page["imageinfo"].toArray();
            if (info.isEmpty()) continue;
            urls[page["title"].toString().replace(' ', '_')] = QUrl(info.at(0).toObject()["url"].toString());
        }
        auto pending = std::make_shared<int>(0);
        auto saved = std::make_shared<int>(0);
        auto failure = std::make_shared<QString>();
        for (const File& f : files) {
            QUrl url = urls.value(QString(f.title).replace(' ', '_'));
            if (!url.isValid() || url.scheme() != "https") continue;
            // The CDN answers WebP by default; format=original gives the uploaded PNG.
            QUrlQuery fq(url);
            fq.addQueryItem("format", "original");
            url.setQuery(fq);
            ++*pending;
            QNetworkReply* r = net->get(request(url));
            const QString unit = f.unit;
            connect(r, &QNetworkReply::downloadProgress, r, [r](qint64 got, qint64) {
                if (got > 30 * 1024 * 1024) r->abort();  // far larger than any of these
            });
            connect(r, &QNetworkReply::finished, this, [=] {
                r->deleteLater();
                if (r->error() != QNetworkReply::NoError) *failure = "Download failed: " + r->errorString();
                else {
                    const QImage img = prepareArt(QImage::fromData(r->readAll(), "PNG"));
                    if (!img.isNull() && savePng(img, artDir() + "/" + unit + ".png")) ++*saved;
                    else *failure = "A downloaded image could not be read";
                }
                if (--*pending > 0) return;
                forgetUnitArt();
                done(*saved, *saved == 3 ? QString() : failure->isEmpty() ? QString("Some images were not found") : *failure);
            });
        }
        if (*pending == 0) done(0, "The wiki did not list the images");
    });
}

QString unitForColours(const PaletteColors& c) {
    if (c.source == "Unit-00") return "unit00";
    if (c.source == "Unit-02") return "unit02";
    if (c.source == "Unit-01" || c.source == "built-in") return "unit01";
    auto hueDist = [](qreal a, qreal b) {
        const qreal d = std::fabs(a - b);
        return std::min(d, 360 - d);
    };
    struct Target {
        const char* id;
        QList<qreal> hues;
    };
    const QList<Target> targets = {{"unit01", {270, 100}}, {"unit00", {218}}, {"unit02", {355, 25}}};
    // The structure colour counts most; the accent a little less; greys say nothing.
    QList<QPair<QColor, qreal>> colours = {{c.structure, 1.0}, {c.accent, 0.8}};
    QString best = "unit01";
    qreal bestScore = 1e9;
    for (const Target& t : targets) {
        qreal score = 0, weight = 0;
        for (const auto& [col, w] : colours) {
            if (col.hslSaturationF() < 0.15) continue;
            qreal d = 360;
            for (qreal h : t.hues) d = std::min(d, hueDist(col.hslHueF() * 360, h));
            score += d * w;
            weight += w;
        }
        if (weight > 0) score /= weight;
        else score = 180;
        if (score < bestScore) {
            bestScore = score;
            best = QString::fromLatin1(t.id);
        }
    }
    return best;
}

void paintUnitArt(QPainter& p, const QRect& area, const QString& unit) {
    // Rendered once per unit and size into a pixmap; a repaint only blits it.
    QString& cachedKey = graphite::cachedKey();
    QPixmap& cached = graphite::cached();
    const qreal dpr = p.device() ? p.device()->devicePixelRatioF() : 1.0;
    const QString key = QString("%1 %2x%3 %4").arg(unit).arg(area.width()).arg(area.height()).arg(dpr);
    if (key != cachedKey) {
        cachedKey = key;
        cached = QPixmap();
        // Decoded here and dropped after scaling. Naming the format keeps Qt from loading every
        // image plugin to guess it (that cost ~8 MB).
        const QImage art = QImage::fromData(unitArt(unit), "PNG");
        if (!art.isNull()) {
            QSizeF size = art.size();
            size.scale(area.size(), Qt::KeepAspectRatio);
            // Premultiplied, the form painting wants: no conversion (and no buffer) per paint.
            cached = QPixmap::fromImage(art.scaled((size * dpr).toSize(), Qt::KeepAspectRatio, Qt::SmoothTransformation)
                                            .convertToFormat(QImage::Format_ARGB32_Premultiplied));
            cached.setDevicePixelRatio(dpr);
        }
#ifdef __GLIBC__
        malloc_trim(0);  // the decoded picture is gone: give its pages back
#endif
    }
    if (cached.isNull()) return;
    const QSizeF s = cached.deviceIndependentSize();
    p.save();
    p.setOpacity(0.26);  // clearly there, still well behind the page names
    p.drawPixmap(QPointF(area.left() + (area.width() - s.width()) / 2, area.top() + (area.height() - s.height()) / 2), cached);
    p.restore();
}

}  // namespace graphite
