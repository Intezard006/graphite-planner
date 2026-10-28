#include "render/tex.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QPainter>
#include <cmath>
#include <exception>

#include "graphic/graphic.h"
#include "latex.h"
#include "platform/qt/graphic_qt.h"
#include "core/formula.h"
#include "render.h"
#include "chem/chem.hpp"

namespace graphite::Tex {

namespace {
bool g_ready = false;

QString findResources() {
    QStringList candidates;
    const QByteArray env = qgetenv("GRAPHITE_TEX_RES");
    if (!env.isEmpty()) candidates << QString::fromLocal8Bit(env);
    const QString exe = QCoreApplication::applicationDirPath();
    candidates << exe + "/../share/graphite/tex" << exe + "/../../share/graphite/tex";
    candidates << exe + "/../Resources/tex";  // macOS app bundle: Graphite.app/Contents/{MacOS,Resources}
#ifdef GRAPHITE_SOURCE_DIR
    candidates << QStringLiteral(GRAPHITE_SOURCE_DIR) + "/third_party/microtex/res";
#endif
    for (const QString& c : candidates)
        if (QFileInfo::exists(c + "/.clatexmath-res_root")) return QDir(c).absolutePath();
    return QString();
}
}  // namespace

bool init() {
    if (g_ready) return true;
    const QString res = findResources();
    if (res.isEmpty()) return false;
    try {
        tex::LaTeX::init(res.toStdString());
        g_ready = true;
    } catch (const std::exception&) {
        g_ready = false;
    }
    return g_ready;
}

bool ready() { return g_ready; }

TexImage render(const QString& latex, qreal pixelSize, const QColor& color, bool display, qreal dpr) {
    TexImage out;
    if (!g_ready) {
        out.error = "TeX resources not found";
        return out;
    }
    try {
        const QString expanded = expandChemistry(latex);
        tex::Formula formula(expanded.toStdWString());
        tex::TeXRenderBuilder builder;
        builder.setStyle(display ? tex::TexStyle::display : tex::TexStyle::text)
            .setTextSize(static_cast<float>(pixelSize * dpr))
            .setForeground(static_cast<tex::color>(color.rgba()))
            .setTrueValues(true);
        std::unique_ptr<tex::TeXRender> r(builder.build(formula));
        const int w = r->getWidth(), h = r->getHeight(), d = r->getDepth();
        if (w <= 0 || h <= 0) {
            out.error = "empty formula";
            return out;
        }
        const int pad = 1;
        QImage img(w + 2 * pad, h + 2 * pad, QImage::Format_ARGB32_Premultiplied);
        img.fill(Qt::transparent);
        {
            QPainter p(&img);
            p.setRenderHint(QPainter::Antialiasing);
            p.setRenderHint(QPainter::TextAntialiasing);
            tex::Graphics2D_qt g2(&p);
            r->draw(g2, pad, pad);
        }
        img.setDevicePixelRatio(dpr);
        out.image = img;
        out.ascent = (h - d + pad) / dpr;  // getHeight() already includes the depth
        out.depth = (d + pad) / dpr;
        out.ok = true;
    } catch (const std::exception& e) {
        out.error = QString::fromUtf8(e.what());
    } catch (...) {
        out.error = "unknown TeX error";
    }
    return out;
}

}  // namespace graphite::Tex
