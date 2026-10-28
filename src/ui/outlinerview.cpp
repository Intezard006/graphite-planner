#include "ui/outlinerview.hpp"

#include <QCache>
#include <QAbstractTextDocumentLayout>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QStyleHints>
#include <QToolTip>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QRegularExpression>
#include <QScrollBar>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextTable>
#include <QTextDocument>
#include <QAbstractTextDocumentLayout>
#include <QFontInfo>
#include "render/imageobject.hpp"
#include "render/mathobject.hpp"
#include <QClipboard>
#include <QLineEdit>
#include <QLocale>
#include <QFileDialog>
#include <QTime>
#include <QPdfWriter>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QMimeData>
#include <QUrl>
#include <QTextEdit>
#include <QTextLayout>
#include <QTextOption>
#include <algorithm>
#include <utility>
#include <cmath>

#include "chem/molar.hpp"
#include "planner/operation.hpp"
#include "ui/snippets.hpp"
#include "planner/tasks.hpp"
#include "maths/calc.hpp"
#include "chem/balance.hpp"
#include "maths/graph.hpp"
#include "notes/glossary.hpp"
#include "maths/labtable.hpp"
#include "chem/oxidation.hpp"
#include "planner/progress.hpp"
#include "ui/theme.hpp"

namespace graphite {

namespace {

constexpr int kMaxColumn = 860;   // width of the text column
constexpr int kSideMargin = 24;
constexpr int kTopMargin = 8;
constexpr size_t kUndoDepth = 100;              // steps per page
constexpr size_t kUndoBytes = 8 * 1024 * 1024;  // and never more than this in total

void trimUndo(std::vector<std::string>& v) {
    size_t total = 0;
    for (const std::string& s : v) total += s.size();
    while (!v.empty() && (v.size() > kUndoDepth || total > kUndoBytes)) {
        total -= v.front().size();
        v.erase(v.begin());
    }
}
constexpr int kBottomMargin = 160;
constexpr int kIndent = 28;       // per nesting level
constexpr int kGutter = 28;       // room left of the text for chevron + bullet
constexpr int kPadY = 4;          // vertical padding above and below each block
// The page title strip (label row + title), from the current fonts, so it grows with text size.
int labelRowY() { return kTopMargin + QFontMetrics(Theme::instance().labelFont()).ascent() + 10; }
int titleBaseline() { return labelRowY() + 12 + QFontMetrics(Theme::instance().titleFont()).ascent(); }
int headerHeight() { return titleBaseline() + QFontMetrics(Theme::instance().titleFont()).descent() + 18 - kTopMargin; }
// The caption: the title's own heavy serif, smaller, as on the episode cards.
QFont captionFont() {
    QFont f = Theme::instance().titleFont();
    f.setPointSizeF(f.pointSizeF() * 0.55);
    return f;
}
int captionHeight() { return QFontMetrics(captionFont()).height() + 8; }

int headingLevel(const QString& raw) {
    static const QRegularExpression re(R"(^(#{1,6})\s)");
    QRegularExpressionMatch m = re.match(raw);
    return m.hasMatch() ? static_cast<int>(m.captured(1).size()) : 0;
}

qreal headingScale(int level) {
    static const qreal s[] = {1.0, 1.7, 1.45, 1.25, 1.12, 1.05, 1.0};
    return s[level];
}

// The text property holding a defined term's [term, meaning, page], for the hover.
constexpr int kDefinitionProperty = QTextFormat::UserProperty + 40;

// A small chart for "=plot{y vs x}": the points, the fitted line, ticks and axis labels.
QImage drawPlot(const LabPlot& p, const Theme& th, qreal dpr, int w, int h) {
    QImage img(QSize(w, h) * dpr, QImage::Format_ARGB32_Premultiplied);
    img.setDevicePixelRatio(dpr);
    img.fill(Qt::transparent);
    QPainter g(&img);
    g.setRenderHint(QPainter::Antialiasing);
    double x0 = p.points[0].x(), x1 = x0, y0 = p.points[0].y(), y1 = y0;
    for (const QPointF& pt : p.points) {
        x0 = qMin(x0, pt.x());
        x1 = qMax(x1, pt.x());
        y0 = qMin(y0, pt.y());
        y1 = qMax(y1, pt.y());
    }
    auto pad = [](double& a, double& b) {
        const double r = b - a;
        const double m = r > 0 ? r * 0.08 : (std::fabs(a) > 0 ? std::fabs(a) * 0.1 : 1);
        a -= m;
        b += m;
    };
    pad(x0, x1);
    pad(y0, y1);
    const double xs = niceStep(x1 - x0, 5), ys = niceStep(y1 - y0, 4);

    QFont lf = th.labelFont();
    lf.setCapitalization(QFont::MixedCase);  // units are case-sensitive: mL is not ML
    const QFontMetrics fm(lf);
    QStringList yTicks;
    for (double v = std::ceil(y0 / ys) * ys; v <= y1 + ys * 1e-9; v += ys) yTicks << calcNumber(std::fabs(v) < ys * 1e-9 ? 0 : v);
    int tickW = 0;
    for (const QString& t : yTicks) tickW = qMax(tickW, fm.horizontalAdvance(t));
    const QRectF area(fm.height() + 8 + tickW + 8, 6, w - (fm.height() + 8 + tickW + 8) - 10, h - 6 - (fm.height() * 2 + 12));
    auto X = [&](double v) { return area.left() + (v - x0) / (x1 - x0) * area.width(); };
    auto Y = [&](double v) { return area.bottom() - (v - y0) / (y1 - y0) * area.height(); };

    QColor grid = th.line();
    g.setFont(lf);
    for (double v = std::ceil(x0 / xs) * xs; v <= x1 + xs * 1e-9; v += xs) {
        const double x = X(v);
        g.setPen(QPen(grid, 1));
        g.drawLine(QPointF(x, area.top()), QPointF(x, area.bottom()));
        g.setPen(th.dim());
        const QString t = calcNumber(std::fabs(v) < xs * 1e-9 ? 0 : v);
        g.drawText(QRectF(x - 60, area.bottom() + 4, 120, fm.height()), Qt::AlignHCenter | Qt::AlignTop, t);
    }
    for (double v = std::ceil(y0 / ys) * ys; v <= y1 + ys * 1e-9; v += ys) {
        const double y = Y(v);
        g.setPen(QPen(grid, 1));
        g.drawLine(QPointF(area.left(), y), QPointF(area.right(), y));
        g.setPen(th.dim());
        g.drawText(QRectF(area.left() - 8 - tickW, y - fm.height() / 2.0, tickW, fm.height()), Qt::AlignRight | Qt::AlignVCenter,
                   calcNumber(std::fabs(v) < ys * 1e-9 ? 0 : v));
    }
    g.setPen(QPen(th.faint(), 1));
    g.drawLine(area.bottomLeft(), area.bottomRight());
    g.drawLine(area.bottomLeft(), area.topLeft());

    if (p.fitted) {  // the fitted line, across the plot, clipped to it
        g.save();
        g.setClipRect(area);
        QColor line = th.structure();
        g.setPen(QPen(line, 1.6));
        g.drawLine(QPointF(X(x0), Y(p.slope * x0 + p.intercept)), QPointF(X(x1), Y(p.slope * x1 + p.intercept)));
        g.restore();
    }
    g.setPen(QPen(th.palette().panel, 1));
    g.setBrush(th.accent());
    for (const QPointF& pt : p.points) g.drawRect(QRectF(X(pt.x()) - 3, Y(pt.y()) - 3, 6, 6));

    auto axisLabel = [](const QString& label, const QString& unit) {
        return unit.isEmpty() || label.contains("(" + unit + ")") ? label : label + " / " + unit;
    };
    g.setPen(th.text());
    g.drawText(QRectF(area.left(), h - fm.height() - 2, area.width(), fm.height()), Qt::AlignHCenter, axisLabel(p.xLabel, p.xUnit));
    g.save();
    g.translate(2, area.center().y());
    g.rotate(-90);
    g.drawText(QRectF(-area.height() / 2, 0, area.height(), fm.height()), Qt::AlignHCenter | Qt::AlignTop, axisLabel(p.yLabel, p.yUnit));
    g.restore();
    return img;
}

// Turns raw block text into styled, clickable rich text.
// `baseDir`: the folder of the page the text is from, for relative image paths.
void buildRich(QTextDocument* doc, const QString& raw, const Theme& th, qreal dpr, const QString& baseDir,
               const RichContext* ctx = nullptr) {
    doc->clear();
    MathObject::install(doc);
    ImageObject::install(doc);
    doc->setDocumentMargin(0);
    QTextOption opt;
    opt.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
    doc->setDefaultTextOption(opt);

    const QFont body = th.bodyFont();
    const QFont mono = th.monoFont();
    doc->setDefaultFont(body);

    QString text = raw;
    const int level = headingLevel(raw);
    QFont baseFont = body;
    if (level > 0) {
        text = raw.mid(level + 1);  // hide the "## " marker in the rendered view
        baseFont.setPointSizeF(body.pointSizeF() * headingScale(level));
        baseFont.setBold(true);
    }

    QTextCharFormat base;
    base.setFont(baseFont);
    base.setForeground(th.text());

    auto with = [&](const QColor& color) {
        QTextCharFormat f = base;
        f.setForeground(color);
        return f;
    };
    auto anchor = [&](QTextCharFormat f, const QString& href) {
        f.setAnchor(true);
        f.setAnchorHref(href);
        return f;
    };

    static const QRegularExpression marker(R"(^(TODO|DOING|NOW|LATER|DONE|WAITING|CANCELED|CANCELLED)(\s|$))");
    static const QRegularExpression property(R"(^[A-Za-z][\w\-]*::(\s|$))");
    static const QRegularExpression token(
        R"((?<prog>\{progress\s[^}\n]*\})|(?<date>(?:SCHEDULED|DEADLINE):\s*<\d{4}-\d{2}-\d{2}[^>]*>)|(?<img>!\[[^\]\n]*\]\([^)\s]+\)(?:\{[^}\n]*\})?)|(?<math2>\$\$.+?\$\$)|(?<math>\$[^$\n]+?\$)|(?<code>`[^`\n]+`)|(?<wl>\[\[[^\]\n]+\]\])|)"
        R"((?<tag>(?<![^\s(])#\[\[[^\]\n]+\]\]|(?<![^\s(])#[^\s#\[\]\(\),.;:!?"']+)|(?<bold>\*\*[^*\n]+\*\*)|)"
        R"((?<strike>~~[^~\n]+~~)|(?<mark>\^\^[^\^\n]+\^\^)|)"
        R"((?<ital>(?<![\*\w])\*[^*\n]+\*(?!\w))|(?<md>\[[^\]\n]+\]\([^)\n]+\))|(?<url>https?://[^\s<>\)]+))");

    QTextCursor c(doc);
    c.setCharFormat(base);
    QStringList lines = text.split('\n');
    bool inCode = false;

    // SCHEDULED / DEADLINE dates show on a line under the task, wherever they were typed
    // (the file keeps them where they are).
    static const QRegularExpression dateToken(R"(\s*(SCHEDULED|DEADLINE):\s*<(\d{4}-\d{2}-\d{2})[^>]*>)");
    QStringList dates;
    {
        bool fence = false;
        for (int li = 0; li < lines.size(); ++li) {
            if (lines[li].trimmed().startsWith("```")) fence = !fence;
            if (fence || !lines[li].contains(':')) continue;
            bool had = false;
            for (QRegularExpressionMatch m = dateToken.match(lines[li]); m.hasMatch(); m = dateToken.match(lines[li])) {
                dates << m.captured().trimmed();
                lines[li].remove(m.capturedStart(), m.capturedLength());
                had = true;
            }
            if (had && lines[li].trimmed().isEmpty() && lines.size() > 1) lines.removeAt(li--);
        }
    }

    // Plain text, with defined terms dotted and carrying their definition (for the hover).
    const Glossary* gloss = ctx && ctx->glossary && !ctx->glossary->isEmpty() ? ctx->glossary.get() : nullptr;
    auto plain = [&](const QString& s, const QTextCharFormat& f) {
        if (!gloss || s.isEmpty()) {
            c.insertText(s, f);
            return;
        }
        int at = 0;
        for (auto gi = gloss->pattern.globalMatch(s); gi.hasNext();) {
            const QRegularExpressionMatch gm = gi.next();
            const Definition* d = gloss->find(gm.captured());
            if (!d) continue;
            if (gm.capturedStart() > at) c.insertText(s.mid(at, gm.capturedStart() - at), f);
            QTextCharFormat df = f;
            df.setUnderlineStyle(QTextCharFormat::DotLine);
            df.setUnderlineColor(th.structure());
            df.setProperty(kDefinitionProperty, QStringList{d->term, d->text, d->page});
            c.insertText(gm.captured(), df);
            at = static_cast<int>(gm.capturedEnd());
        }
        if (at < s.size()) c.insertText(s.mid(at), f);
    };
    // One line of ordinary text: links, tags, formulas, code, emphasis, images.
    int bars = 0;
    auto inlineRun = [&](const QString& rest) {
        int pos = 0;
        QRegularExpressionMatchIterator it = token.globalMatch(rest);
        while (it.hasNext()) {
            QRegularExpressionMatch m = it.next();
            if (m.capturedStart() > pos) plain(rest.mid(pos, m.capturedStart() - pos), base);
            const QString s = m.captured();

            if (m.hasCaptured("date")) {
                // SCHEDULED: <2026-10-24 Sat>  ->  SCHEDULED · SAT 24 OCT, deadlines in the warm colour;
                // on the task's own line or on a line of its own.
                static const QRegularExpression parts(R"(^(SCHEDULED|DEADLINE):\s*<(\d{4}-\d{2}-\d{2}))");
                const QRegularExpressionMatch dm = parts.match(s);
                const QDate d = QDate::fromString(dm.captured(2), "yyyy-MM-dd");
                QTextCharFormat f = with(dm.captured(1) == "DEADLINE" ? th.warm() : th.dim());
                f.setFont(th.labelFont());
                f.setFontCapitalization(QFont::AllUppercase);
                c.insertText(dm.captured(1) + "  \xC2\xB7  " + (d.isValid() ? QLocale::c().toString(d, "ddd d MMM") : dm.captured(2)), f);
            } else if (m.hasCaptured("img")) {
                // ![alt](../assets/x.png){:width 300}: the picture itself; the raw text if missing.
                const int open = static_cast<int>(s.indexOf("]("));
                const int close = static_cast<int>(s.indexOf(')', open));
                const QString url = QUrl::fromPercentEncoding(s.mid(open + 2, close - open - 2).toUtf8());
                const QString path = QDir::isAbsolutePath(url) ? url : QDir(baseDir).absoluteFilePath(url);
                QTextCharFormat imf;
                if (!baseDir.isEmpty() && ImageObject::format(&imf, QDir::cleanPath(path)))
                    c.insertText(QString(QChar::ObjectReplacementCharacter), imf);
                else
                    c.insertText(s, with(th.faint()));
            } else if (m.hasCaptured("wl") && s.startsWith("[[eq:")) {
                // [[eq:arrhenius]] -> "Eq. (3)", a link to the formula on this page
                const QString label = s.mid(5, s.size() - 7);
                const int n = ctx ? ctx->equations.value(label, 0) : 0;
                if (n) c.insertText(QString("Eq. (%1)").arg(n), anchor(with(th.accent()), "eq:" + label));
                else c.insertText("Eq. (" + label + "?)", with(th.faint()));
            } else if (m.hasCaptured("wl")) {
                const QString name = s.mid(2, s.size() - 4);
                c.insertText("[[", with(th.faint()));
                c.insertText(name, anchor(with(th.accent()), name));
                c.insertText("]]", with(th.faint()));
            } else if (m.hasCaptured("tag")) {
                QString name = s.mid(1);
                if (name.startsWith("[[")) name = name.mid(2, name.size() - 4);
                c.insertText(s, anchor(with(th.structure()), name));
            } else if (m.hasCaptured("bold")) {
                QTextCharFormat f = base;
                f.setFontWeight(QFont::Bold);
                plain(s.mid(2, s.size() - 4), f);
            } else if (m.hasCaptured("strike")) {
                QTextCharFormat f = with(th.dim());
                f.setFontStrikeOut(true);
                plain(s.mid(2, s.size() - 4), f);
            } else if (m.hasCaptured("mark")) {  // ^^highlight^^, as in Logseq
                QTextCharFormat f = base;
                const QColor a = th.accent();
                f.setBackground(QColor(a.red(), a.green(), a.blue(), 70));
                plain(s.mid(2, s.size() - 4), f);
            } else if (m.hasCaptured("ital")) {
                QTextCharFormat f = base;
                f.setFontItalic(true);
                plain(s.mid(1, s.size() - 2), f);
            } else if (m.hasCaptured("code")) {
                QTextCharFormat f = with(th.text());
                f.setFont(mono);
                f.setBackground(QColor(th.structure().red(), th.structure().green(), th.structure().blue(), 45));
                c.insertText(s.mid(1, s.size() - 2), f);
            } else if (m.hasCaptured("math") || m.hasCaptured("math2")) {
                const bool display = m.hasCaptured("math2");
                const QString tex = s.mid(display ? 2 : 1, s.size() - (display ? 4 : 2));
                // "$5 and $6" is money, not maths: formulas do not start or end with a space.
                const bool plausible = !tex.isEmpty() && !tex.front().isSpace() && !tex.back().isSpace();
                QTextCharFormat mf;
                const qreal px = QFontInfo(baseFont).pixelSize() * 1.1;
                if (plausible && MathObject::format(&mf, tex, px, th.text(), display, dpr)) {
                    if (display && rest.trimmed() == s) {
                        QTextBlockFormat bf;
                        bf.setAlignment(Qt::AlignHCenter);
                        c.setBlockFormat(bf);
                    }
                    c.insertText(QString(QChar::ObjectReplacementCharacter), mf);
                } else {
                    QTextCharFormat f = with(plausible ? th.structure() : th.text());
                    if (plausible) f.setFont(mono);
                    c.insertText(s, f);
                }
            } else if (m.hasCaptured("md")) {
                const int close = static_cast<int>(s.indexOf("]("));
                const QString label = s.mid(1, close - 1);
                const QString url = s.mid(close + 2, s.size() - close - 3);
                QTextCharFormat f = anchor(with(th.accent()), url);
                f.setFontUnderline(true);
                c.insertText(label, f);
            } else if (Progress pr; m.hasCaptured("prog") && findProgress(s, &pr)) {
                // A NERV gauge, then "SYNC 38%  45 / 120 PAGES" in the label font.
                const QFontMetrics lm(th.labelFont());
                const int h = qMax(8, lm.ascent()), w = static_cast<int>(h * 13);
                const QString name = QString("graphite-progress:%1").arg(++bars);
                doc->addResource(QTextDocument::ImageResource, QUrl(name), drawProgress(pr, th, dpr, w, h));
                QTextImageFormat imf;
                imf.setName(name);
                imf.setWidth(w);
                imf.setHeight(h);
                imf.setVerticalAlignment(QTextCharFormat::AlignMiddle);
                c.insertImage(imf);
                QTextCharFormat lf = with(pr.full() ? th.accent() : th.warm());
                lf.setFont(th.labelFont());
                lf.setVerticalAlignment(QTextCharFormat::AlignMiddle);
                const int pct = static_cast<int>(std::floor(pr.ratio() * 100 + 1e-9));
                c.insertText(QString("  %1%2%").arg(Theme::instance().immersion() ? "SYNC " : "").arg(pct), lf);
                lf.setForeground(th.dim());
                c.insertText("  " + progressNumbers(pr), lf);
            } else if (m.hasCaptured("url")) {
                QTextCharFormat f = anchor(with(th.accent()), s);
                f.setFontUnderline(true);
                c.insertText(s, f);
            }
            pos = static_cast<int>(m.capturedEnd());
        }
        if (pos < rest.size()) plain(rest.mid(pos), base);
    };

    // A lab table: computed columns in the accent colour, numbers to the right.
    LabTable lastTable;
    bool haveTable = false;
    bool afterFrame = false;  // a table was just inserted: the block after it is already there
    auto insertLabTable = [&](const LabTable& t) {
        const int cols = static_cast<int>(t.columns.size()), rows = static_cast<int>(t.cells.size()) + 1;
        QTextTableFormat tf;
        tf.setBorder(1);
        tf.setBorderStyle(QTextFrameFormat::BorderStyle_Solid);
        tf.setBorderBrush(th.line());
        tf.setBorderCollapse(true);
        tf.setCellPadding(4);
        tf.setCellSpacing(0);
        tf.setMargin(0);
        tf.setTopMargin(3);
        tf.setBottomMargin(3);
        tf.setHeaderRowCount(1);
        QTextTable* table = c.insertTable(rows, cols, tf);
        QColor head = th.structure();
        head.setAlpha(34);
        QTextBlockFormat right;
        right.setAlignment(Qt::AlignRight);
        for (int col = 0; col < cols; ++col) {
            const LabColumn& lc = t.columns[col];
            QTextTableCell hcell = table->cellAt(0, col);
            QTextTableCellFormat hcf = hcell.format().toTableCellFormat();
            hcf.setBackground(head);
            hcell.setFormat(hcf);
            QTextCursor hc = hcell.firstCursorPosition();
            if (lc.numeric) hc.setBlockFormat(right);
            QTextCharFormat hf = with(lc.computed ? th.accent() : th.dim());
            hf.setFontWeight(QFont::DemiBold);
            hc.insertText(t.label(col), hf);
            if (lc.computed) {
                QTextCharFormat ff = with(th.faint());
                ff.setFont(mono);
                ff.setFontPointSize(mono.pointSizeF() * 0.85);
                hc.insertText("  = " + lc.formula.section(QRegularExpression(R"(\s+(?:to|in)\s+|\s*(?:->|→)\s*)"), 0, 0), ff);
            }
            for (int r = 0; r < t.cells.size(); ++r) {
                QTextCursor cc = table->cellAt(r + 1, col).firstCursorPosition();
                if (lc.numeric) cc.setBlockFormat(right);
                if (lc.computed) {
                    QTextCharFormat f = with(th.accent());
                    f.setFont(mono);
                    cc.insertText(t.cells[r][col], f);
                } else {
                    const QTextCursor saved = c;
                    c = cc;
                    inlineRun(t.cells[r][col]);
                    c = saved;
                }
            }
        }
        c.movePosition(QTextCursor::End);
        afterFrame = true;
        QStringList errors;
        for (const LabColumn& lc : t.columns)
            if (!lc.error.isEmpty()) errors << (lc.name.isEmpty() ? lc.header : lc.name) + QString::fromUtf8(": ") + lc.error;
        if (!errors.isEmpty()) {
            QTextCharFormat f = with(th.faint());
            f.setFontItalic(true);
            c.insertText(errors.join(QString::fromUtf8("   ·   ")), f);
            afterFrame = false;
        }
    };
    int plots = 0;
    auto insertPlot = [&](const QString& spec) {
        auto failed = [&](const QString& why) {
            QTextCharFormat ef = with(th.dim());
            ef.setFont(mono);
            c.insertText("plot " + spec, ef);
            QTextCharFormat f = with(th.faint());
            f.setFontItalic(true);
            f.setFont(mono);
            c.insertText(QString::fromUtf8("   ·  ") + why, f);
        };
        if (!haveTable) return failed("put it under a table in the same block");
        const LabPlot pl = labPlot(lastTable, spec);
        if (!pl.ok) return failed(pl.error);
        const int w = static_cast<int>(440 * th.scale()), h = static_cast<int>(250 * th.scale());
        const QString name = QString("graphite-plot:%1").arg(++plots);
        doc->addResource(QTextDocument::ImageResource, QUrl(name), drawPlot(pl, th, dpr, w, h));
        QTextImageFormat imf;
        imf.setName(name);
        imf.setWidth(w);
        imf.setHeight(h);
        c.insertImage(imf);
        if (pl.fitted) {
            c.insertBlock(QTextBlockFormat(), base);
            QTextCharFormat lf = with(th.accent());
            lf.setFont(th.labelFont());
            lf.setFontCapitalization(QFont::AllUppercase);
            c.insertText(th.immersion() ? QString::fromUtf8("MAGI fit  ·  ") : QString::fromUtf8("Fit  ·  "), lf);
            QTextCharFormat f = with(th.dim());
            f.setFont(mono);
            c.insertText(pl.summary, f);
        }
    };

    // "=graph{...}": the curves, worked out once per spec (rebuilds and theme changes only redraw).
    auto insertGraph = [&](const QString& spec) {
        static QCache<QString, Graph> made(24);
        Graph* cached = made.object(spec);
        if (!cached) {
            cached = new Graph(makeGraph(spec));
            made.insert(spec, cached);
        }
        Graph gr = *cached;
        if (!gr.ok) {
            QTextCharFormat ef = with(th.dim());
            ef.setFont(mono);
            c.insertText("graph{" + spec + "}", ef);
            QTextCharFormat f = with(th.faint());
            f.setFontItalic(true);
            f.setFont(mono);
            c.insertText(QString::fromUtf8("   ·  ") + gr.error, f);
            return;
        }
        // Under a lab table: its measurements, when columns are named like the variable and curve.
        if (haveTable) {
            int xc = -1, yc = -1;
            for (int k = 0; k < lastTable.columns.size(); ++k) {
                if (lastTable.columns[k].name == gr.xName) xc = k;
                if (lastTable.columns[k].name == gr.curves[0].label) yc = k;
            }
            if (xc >= 0 && yc >= 0)
                for (const auto& row : lastTable.values) {
                    if (xc >= row.size() || yc >= row.size() || !row[xc] || !row[yc]) continue;
                    if (row[xc]->dims != gr.xDims || row[yc]->dims != gr.yDims) continue;
                    const QPointF pt(row[xc]->si / gr.xScale, row[yc]->si / gr.yScale);
                    gr.points << pt;
                    if (!gr.yGiven) {
                        const double pad = (gr.y1 - gr.y0) * 0.05;
                        gr.y0 = qMin(gr.y0, pt.y() - pad);
                        gr.y1 = qMax(gr.y1, pt.y() + pad);
                    }
                }
        }
        const int w = static_cast<int>(480 * th.scale()), h = static_cast<int>(290 * th.scale());
        const QString name = QString("graphite-plot:%1").arg(++plots);
        doc->addResource(QTextDocument::ImageResource, QUrl(name), drawGraph(gr, th, dpr, w, h));
        QTextImageFormat imf;
        imf.setName(name);
        imf.setWidth(w);
        imf.setHeight(h);
        c.insertImage(imf);
        if (const QString summary = graphSummary(gr, th.immersion()); !summary.isEmpty()) {
            c.insertBlock(QTextBlockFormat(), base);
            QTextCharFormat f = with(th.dim());
            f.setFont(mono);
            c.insertText(summary, f);
        }
    };

    for (int li = 0; li < lines.size(); ++li) {
        const QString& line = lines[li];
        if (li > 0 && !afterFrame) c.insertBlock(QTextBlockFormat(), base);
        afterFrame = false;

        if (line.trimmed().startsWith("```")) {
            inCode = !inCode;
            QTextCharFormat f = with(th.faint());
            f.setFont(mono);
            c.insertText(line.trimmed(), f);
            continue;
        }
        if (inCode) {
            QTextCharFormat f = with(th.text());
            f.setFont(mono);
            c.insertText(line, f);
            continue;
        }
        // A numbered display formula: $$ ... $$ {#label}, centred, its number on the right.
        static const QRegularExpression numbered(R"(^\s*\$\$(.+?)\$\$\s*\{#([\w:.\-]+)\}\s*$)");
        if (const QRegularExpressionMatch nm = numbered.match(line); nm.hasMatch()) {
            QTextCharFormat mf;
            if (MathObject::format(&mf, nm.captured(1), QFontInfo(baseFont).pixelSize() * 1.1, th.text(), true, dpr)) {
                QTextTableFormat tf;
                tf.setBorder(0);
                tf.setCellPadding(0);
                tf.setCellSpacing(0);
                tf.setMargin(0);
                tf.setWidth(QTextLength(QTextLength::PercentageLength, 100));
                tf.setColumnWidthConstraints({QTextLength(QTextLength::PercentageLength, 12), QTextLength(QTextLength::PercentageLength, 76),
                                              QTextLength(QTextLength::PercentageLength, 12)});
                QTextTable* table = c.insertTable(1, 3, tf);
                for (int col = 0; col < 3; ++col) {
                    QTextTableCell cell = table->cellAt(0, col);
                    QTextTableCellFormat cf = cell.format().toTableCellFormat();
                    cf.setVerticalAlignment(QTextCharFormat::AlignMiddle);
                    cell.setFormat(cf);
                }
                QTextBlockFormat centre, right;
                centre.setAlignment(Qt::AlignHCenter);
                right.setAlignment(Qt::AlignRight);
                QTextCursor mc = table->cellAt(0, 1).firstCursorPosition();
                mc.setBlockFormat(centre);
                mc.insertText(QString(QChar::ObjectReplacementCharacter), mf);
                QTextCursor nc = table->cellAt(0, 2).firstCursorPosition();
                nc.setBlockFormat(right);
                const int n = ctx ? ctx->equations.value(nm.captured(2), 0) : 0;
                nc.insertText(n ? QString("(%1)").arg(n) : "(" + nm.captured(2) + ")", with(th.dim()));
                c.movePosition(QTextCursor::End);
                afterFrame = true;
                continue;
            }
        }
        // | a | b | rows: a lab table (computed columns worked out); "=plot{y vs x}" lines draw it.
        if (line.trimmed().startsWith('|')) {
            int last = li;
            while (last + 1 < lines.size() && lines[last + 1].trimmed().startsWith('|')) ++last;
            LabTable t;
            if (parseLabTable(lines.mid(li, last - li + 1), &t)) {
                insertLabTable(t);
                lastTable = t;
                haveTable = true;
                li = last;
                continue;
            }
        }
        if (const QString spec = plotSpec(line); !spec.isNull()) {
            insertPlot(spec);
            continue;
        }
        if (const QString spec = graphSpec(line); !spec.isNull()) {
            insertGraph(spec);
            continue;
        }
        // A page's description:: reads as the caption under its title (the raw line while editing).
        if (line.startsWith("description:: ")) {
            QTextCharFormat f = with(th.dim());
            f.setFontItalic(true);
            f.setFontPointSize(baseFont.pointSizeF() * 1.12);
            c.insertText(line.mid(14).trimmed(), f);
            continue;
        }
        if (QString term, meaning; definitionLine(line, &term, &meaning)) {
            QTextCharFormat tf = base;
            tf.setFontWeight(QFont::DemiBold);
            c.insertText(term, tf);
            c.insertText(QString::fromUtf8("  —  "), with(th.faint()));
            inlineRun(meaning);
            continue;
        }
        if (property.match(line).hasMatch()) {
            c.insertText(line, with(th.dim()));
            continue;
        }
        // "= 0.5 mol * 58.44 g/mol"  ->  0.5 mol * 58.44 g/mol  →  29.22 g
        if (const QString expr = calcExpression(line); !expr.isNull()) {
            const CalcResult res = evaluate(expr);
            QTextCharFormat ef = with(th.dim());
            ef.setFont(mono);
            c.insertText(expr, ef);
            QTextCharFormat rf = with(res.ok ? th.accent() : th.faint());
            rf.setFont(mono);
            if (res.ok) rf.setFontWeight(QFont::Bold);
            else rf.setFontItalic(true);
            c.insertText(res.ok ? QString::fromUtf8("   \u2192  ") + res.value : QString::fromUtf8("   \u00B7  ") + res.error, rf);
            continue;
        }

        QString rest = line;
        if (li == 0) {
            // A time block may put its time before the marker: "09:00 TODO problem set".
            if (TimeSlot slot; !rest.isEmpty() && rest[0].isDigit() && blockTime(rest, &slot) && slot.at == 0) {
                QTextCharFormat tf = with(slot.done ? th.faint() : th.accent());
                tf.setFont(mono);
                tf.setFontWeight(QFont::Bold);
                c.insertText(rest.left(slot.length), tf);
                rest = rest.mid(slot.length);
                int spaces = 0;
                while (spaces < rest.size() && rest[spaces] == ' ') ++spaces;
                c.insertText(rest.left(spaces), base);
                rest = rest.mid(spaces);
            }
            QRegularExpressionMatch m = marker.match(rest);
            if (m.hasMatch()) {
                const QString word = m.captured(1);
                const bool done = (word == "DONE" || word.startsWith("CANCEL"));
                QTextCharFormat f = with(done ? th.faint() : th.warm());
                f.setFontWeight(QFont::DemiBold);
                f.setFontLetterSpacing(105);
                c.insertText(word + " ", f);
                rest = rest.mid(m.capturedLength());
            }
            // A time block: "09:00 problem set" — the time as a small mono label.
            if (TimeSlot slot; !rest.isEmpty() && rest[0].isDigit() && blockTime(rest, &slot) && slot.at == 0) {
                QTextCharFormat tf = with(slot.done || (m.hasMatch() && (m.captured(1) == "DONE" || m.captured(1).startsWith("CANCEL"))) ? th.faint() : th.accent());
                tf.setFont(mono);
                tf.setFontWeight(QFont::Bold);
                c.insertText(rest.left(slot.length), tf);
                rest = rest.mid(slot.length);
            }
        }

        inlineRun(rest);
        if (li == 0 && ctx && ctx->block >= 0) {  // a heading's subtasks: "SYNC 2/5", accent when all are in
            if (auto pi = ctx->progress.constFind(ctx->block); pi != ctx->progress.constEnd()) {
                QTextCharFormat f = with(pi->first == pi->second ? th.accent() : th.dim());
                f.setFont(th.labelFont());
                c.insertText("   ", base);
                c.insertText((ctx->eva ? QStringLiteral("SYNC ") : QString()) + QString("%1/%2").arg(pi->first).arg(pi->second), f);
            }
        }
    }

    if (afterFrame) {  // the empty block a table at the end leaves behind takes no room
        QTextBlockFormat tight;
        tight.setLineHeight(1, QTextBlockFormat::FixedHeight);
        c.setBlockFormat(tight);
    }

    // The dates:  SCHEDULED · SAT 24 OCT    DEADLINE · SUN 25 OCT · 14:00  [T−03H 12M]
    if (!dates.isEmpty()) {
        if (!(lines.size() == 1 && lines[0].trimmed().isEmpty())) c.insertBlock(QTextBlockFormat(), base);
        static const QRegularExpression parts(R"(^(SCHEDULED|DEADLINE):\s*<(\d{4}-\d{2}-\d{2}))");
        const QDateTime now = QDateTime::currentDateTime();
        for (int k = 0; k < dates.size(); ++k) {
            const QRegularExpressionMatch dm = parts.match(dates[k]);
            const bool deadline = dm.captured(1) == "DEADLINE";
            const QDate d = QDate::fromString(dm.captured(2), "yyyy-MM-dd");
            const QDateTime at = deadline ? blockDeadlineTime(dates[k]) : QDateTime();
            QTextCharFormat f = with(deadline ? th.warm() : th.dim());
            f.setFont(th.labelFont());
            f.setFontCapitalization(QFont::AllUppercase);
            if (k > 0) c.insertText("      ", f);
            QString label = dm.captured(1) + "  \xC2\xB7  " + (d.isValid() ? QLocale::c().toString(d, "ddd d MMM") : dm.captured(2));
            if (at.isValid()) label += "  \xC2\xB7  " + at.toString("HH:mm");
            c.insertText(label, f);
            if (at.isValid()) {
                // The countdown, like an Eva's internal power: amber, red in the last hour and after.
                const qint64 left = now.secsTo(at);
                const QColor tone = left < 3600 ? QColor(0xff, 0x4d, 0x4d) : QColor(0xff, 0xb0, 0x00);
                QTextCharFormat cf = with(tone);
                cf.setFont(th.monoFont());
                cf.setFontPointSize(th.labelFont().pointSizeF() * 1.05);
                cf.setFontWeight(QFont::Bold);
                cf.setFontLetterSpacing(108);
                QColor bg = tone;
                bg.setAlpha(34);
                cf.setBackground(bg);
                c.insertText("    ", f);
                c.insertText(" " + countdownText(at, now) + " ", cf);
            }
        }
    }
}

// Suggestions under the cursor, for snippets and the "/" command menu:
//   ;dH   ΔH°   STANDARD ENTHALPY CHANGE        Heading 2   ##
struct SuggestRow {
    QString key;    // mono, accent (snippet trigger); may be empty
    QString shown;  // body text (result or command name)
    QImage img;     // typeset result instead of `shown`, if any
    QString what;   // small caps on the right
};

class SuggestPopup : public QWidget {
public:
    explicit SuggestPopup(QWidget* parent) : QWidget(parent) {
        setAttribute(Qt::WA_TransparentForMouseEvents);
        hide();
    }
    void set(const QList<SuggestRow>& rows) {
        rows_ = rows;
        current_ = 0;
        top_ = 0;
        const Theme& th = Theme::instance();
        const QFontMetrics bm(th.bodyFont()), mm(th.monoFont()), lm(th.labelFont());
        int w = 0;
        for (const SuggestRow& r : rows_) {
            const int keyW = r.key.isEmpty() ? 0 : mm.horizontalAdvance(r.key) + 16;
            const int midW = r.img.isNull() ? bm.horizontalAdvance(r.shown) : static_cast<int>(r.img.width() / r.img.devicePixelRatio());
            w = qMax(w, keyW + midW + 18 + lm.horizontalAdvance(r.what.toUpper()));
        }
        resize(w + 24, rowH() * qMin(kMax, static_cast<int>(rows_.size())) + 8);
        update();
    }
    void move(int delta) {
        if (rows_.isEmpty()) return;
        const int n = static_cast<int>(rows_.size());
        current_ = (current_ + delta + n) % n;
        if (current_ < top_) top_ = current_;  // keep the highlighted row in the window
        if (current_ >= top_ + kMax) top_ = current_ - kMax + 1;
        update();
    }
    int current() const { return rows_.isEmpty() ? -1 : current_; }

protected:
    void paintEvent(QPaintEvent*) override {
        const Theme& th = Theme::instance();
        QPainter p(this);
        QColor fill = th.palette().panel;
        fill.setAlpha(th.transparent() ? 235 : 255);
        p.fillRect(rect(), fill);
        p.setPen(QPen(th.line(), 1));
        p.drawRect(rect().adjusted(0, 0, -1, -1));
        const QFontMetrics mm(th.monoFont());
        const int last = qMin(static_cast<int>(rows_.size()), top_ + kMax);
        for (int i = top_; i < last; ++i) {
            const QRect r(4, 4 + (i - top_) * rowH(), width() - 8, rowH());
            if (i == current_) {
                QColor tint = th.structure();
                tint.setAlpha(52);
                p.fillRect(r, tint);
                p.fillRect(QRect(r.left(), r.top(), 2, r.height()), th.accent());
            }
            const SuggestRow& row = rows_[i];
            int x = 8;
            if (!row.key.isEmpty()) {
                p.setFont(th.monoFont());
                p.setPen(th.accent());
                p.drawText(r.adjusted(x, 0, 0, 0), Qt::AlignVCenter | Qt::AlignLeft, row.key);
                x += mm.horizontalAdvance(row.key) + 16;
            }
            if (!row.img.isNull()) {
                const qreal h = row.img.height() / row.img.devicePixelRatio();
                p.drawImage(QPointF(r.left() + x, r.center().y() - h / 2 + 1), row.img);
            } else {
                p.setFont(th.bodyFont());
                p.setPen(th.text());
                p.drawText(r.adjusted(x, 0, 0, 0), Qt::AlignVCenter | Qt::AlignLeft, row.shown);
            }
            p.setFont(th.labelFont());
            p.setPen(th.faint());
            p.drawText(r.adjusted(0, 0, -8, 0), Qt::AlignVCenter | Qt::AlignRight, row.what);
        }
        if (rows_.size() > kMax) {  // a thin scroll mark on the right edge
            const int track = height() - 8;
            const int barH = qMax(8, track * kMax / static_cast<int>(rows_.size()));
            const int barY = 4 + (track - barH) * top_ / qMax(1, static_cast<int>(rows_.size()) - kMax);
            p.fillRect(QRect(width() - 3, barY, 2, barH), th.faint());
        }
    }

private:
    static constexpr int kMax = 8;
    int rowH() const { return QFontMetrics(Theme::instance().bodyFont()).lineSpacing() + 8; }
    QList<SuggestRow> rows_;
    int current_ = 0;
    int top_ = 0;
};

// A snippet's result for display: typeset TeX when it has some, plain text otherwise.
SuggestRow snippetRow(const Snippet& s, bool inMath, qreal dpr) {
    SuggestRow row;
    row.key = QString::fromUtf8(s.trigger);
    row.what = QString::fromUtf8(s.what);
    QString t = QString::fromUtf8(inMath ? s.math : s.text);
    t.replace("|", t.contains("\\ce{") || t.contains("\\pu{") ? "\xE2\x80\xA6" : "");
    t.replace("{today}", "today");
    row.shown = t;
    if (t.contains('\\') && !t.startsWith("$$") && !t.contains("\\ce{") && !t.contains("\\pu{")) {
        if (t.startsWith('$') && t.endsWith('$')) t = t.mid(1, t.size() - 2);
        t.replace("{}", "{\\square}");  // an empty slot shows as a box
        const Theme& th = Theme::instance();
        row.img = MathObject::image(t, QFontInfo(th.bodyFont()).pixelSize() * 1.05, th.text(), false, dpr);
    }
    return row;
}

// The "/" command menu. `words` are extra names it answers to ("/h2", "/chem", "/due").
enum class Cmd {
    H1, H2, H3, Text, Todo, Doing, Done, Later, Now, Scheduled, Deadline, Today, Tomorrow, Yesterday,
    PageLink, Tag, Math, Display, Chem, Unit, Calc, Balance, LabTable, Plot, Graph, Code, Quote, Divider, Image, Time, Lecture, Timer,
    Scrap, Progress
};
struct CommandDef {
    Cmd id;
    const char* name;
    const char* words;
    const char* hint;
};
const QList<CommandDef>& commands() {
    static const QList<CommandDef> list = {
        {Cmd::H1, "Heading 1", "h1 title heading", "#"},
        {Cmd::H2, "Heading 2", "h2 heading section", "##"},
        {Cmd::H3, "Heading 3", "h3 heading subsection", "###"},
        {Cmd::Text, "Plain text", "text normal paragraph p clear", "no heading"},
        {Cmd::Todo, "TODO", "todo task", "task"},
        {Cmd::Doing, "DOING", "doing started", "task"},
        {Cmd::Done, "DONE", "done finished", "task"},
        {Cmd::Later, "LATER", "later someday", "task"},
        {Cmd::Now, "NOW", "now", "task"},
        {Cmd::Scheduled, "Scheduled", "scheduled date plan", "pick a date"},
        {Cmd::Deadline, "Deadline", "deadline due date time limit", "date and time"},
        {Cmd::Today, "Today", "today date journal link", "[[ ]]"},
        {Cmd::Tomorrow, "Tomorrow", "tomorrow date", "[[ ]]"},
        {Cmd::Yesterday, "Yesterday", "yesterday date", "[[ ]]"},
        {Cmd::PageLink, "Page link", "link page reference", "[[ ]]"},
        {Cmd::Tag, "Tag", "tag hashtag", "#"},
        {Cmd::Math, "Formula", "math formula latex equation inline", "$ $"},
        {Cmd::Display, "Display formula", "display centred equation block", "$$ $$"},
        {Cmd::Chem, "Chemistry", "ce chem chemistry reaction mhchem", "\\ce{ }"},
        {Cmd::Unit, "Unit", "pu unit quantity", "\\pu{ }"},
        {Cmd::Calc, "Quick maths", "calc calculate maths compute units moles convert", "= 0.5 mol * M(NaCl)"},
        {Cmd::Balance, "Balance equation", "balance reaction equation stoichiometry coefficients", "\\ce{Fe + O2 -> Fe2O3}"},
        {Cmd::LabTable, "Lab table", "table data columns lab results measurements", "| m / g | V / mL | = m/V |"},
        {Cmd::Plot, "Plot", "plot chart fit regression linear calibration table", "=plot{y vs x}"},
        {Cmd::Graph, "Graph", "graph function curve sketch roots maxima asymptote", "=graph{x^2 - 2, x from -3 to 3}"},
        {Cmd::Code, "Code block", "code program snippet", "```"},
        {Cmd::Quote, "Quote", "quote cite", ">"},
        {Cmd::Divider, "Divider", "divider rule line hr", "---"},
        {Cmd::Image, "Image from file", "image picture photo figure file", "assets/"},
        {Cmd::Time, "Current time", "time now clock", "hh:mm"},
        {Cmd::Lecture, "Lecture notes", "lecture template class notes", "template"},
        {Cmd::Timer, "Operation timer", "timer operation pomodoro study focus countdown", "/timer 20m 15s"},
        {Cmd::Progress, "Progress bar", "progress bar sync ratio track pages steps", "{progress 0/10 by 1}"},
        {Cmd::Scrap, "Scrap the day", "scrap clear wipe journal day third impact", "Ctrl+Shift+Backspace"},
    };
    return list;
}

// Commands matching what follows the "/": the name first, then its other words.
QList<int> matchCommands(const QString& query) {
    QList<int> byName, byWord;
    const QString q = query.toLower();
    for (int i = 0; i < commands().size(); ++i) {
        const QString name = QString::fromUtf8(commands()[i].name).toLower();
        if (name.startsWith(q)) byName << i;
        else if (QString::fromUtf8(commands()[i].words).split(' ').filter(QRegularExpression("^" + QRegularExpression::escape(q))).size())
            byWord << i;
    }
    return byName + byWord;
}

// "/que" just before the cursor, at the start of a word; empty (and false) when not.
bool commandQuery(const QString& text, int cursor, int* start, QString* query) {
    int k = cursor;
    while (k > 0 && (text[k - 1].isLetterOrNumber())) --k;
    if (k == 0 || text[k - 1] != '/') return false;
    const int s = k - 1;
    if (s > 0 && !text[s - 1].isSpace()) return false;  // "a/b", "https://" are not commands
    if (cursor < text.size() && text[cursor].isLetterOrNumber()) return false;
    *start = s;
    *query = text.mid(k, cursor - k);
    return true;
}

// Find bar, top right of the page:  FIND [ query        ]  3 / 12
class FindBar : public QWidget {
public:
    FindBar(OutlinerView* view, QWidget* parent) : QWidget(parent), view_(view) {
        input_ = new QLineEdit(this);
        input_->setFrame(false);
        input_->setPlaceholderText("Find in page");
        input_->installEventFilter(this);
        QObject::connect(input_, &QLineEdit::textChanged, this, [this](const QString& t) {
            view_->setFindQuery(t);
            update();
        });
        hide();
    }
    QLineEdit* input() const { return input_; }
    void restyle() {
        const Theme& th = Theme::instance();
        input_->setFont(th.bodyFont());
        input_->setStyleSheet(QString("QLineEdit { background: transparent; color: rgb(%1,%2,%3); border: none; }")
                                  .arg(th.text().red()).arg(th.text().green()).arg(th.text().blue()));
        const QFontMetrics bm(th.bodyFont()), lm(th.labelFont());
        const int h = bm.lineSpacing() + 16;
        const int labelW = lm.horizontalAdvance("FIND") + 22;
        resize(qMin(420, parentWidget()->width() - 24), h);
        input_->setGeometry(labelW, 0, width() - labelW - lm.horizontalAdvance("000 / 000") - 24, h);
        update();
    }

protected:
    bool eventFilter(QObject* obj, QEvent* ev) override {
        if (obj == input_ && ev->type() == QEvent::KeyPress) {
            auto* e = static_cast<QKeyEvent*>(ev);
            if (e->key() == Qt::Key_Escape) {
                view_->closeFind();
                return true;
            }
            if (e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter) {
                view_->findStep(e->modifiers() & Qt::ShiftModifier ? -1 : 1);
                update();
                return true;
            }
        }
        return QWidget::eventFilter(obj, ev);
    }
    void paintEvent(QPaintEvent*) override {
        const Theme& th = Theme::instance();
        QPainter p(this);
        QColor fill = th.palette().panel;
        fill.setAlpha(th.transparent() ? 235 : 255);
        p.fillRect(rect(), fill);
        p.setPen(QPen(th.line(), 1));
        p.drawRect(rect().adjusted(0, 0, -1, -1));
        p.fillRect(QRect(0, 0, 3, height()), th.accent());
        p.setFont(th.labelFont());
        p.setPen(th.accent());
        p.drawText(QRect(12, 0, 60, height()), Qt::AlignVCenter | Qt::AlignLeft, "Find");
        const int n = view_->findCount();
        const QString count = input_->text().isEmpty() ? QString()
                              : n == 0              ? QString("none")
                                                    : QString("%1 / %2").arg(view_->findCurrent() + 1).arg(n);
        p.setPen(n == 0 && !input_->text().isEmpty() ? th.warm() : th.dim());
        p.drawText(rect().adjusted(0, 0, -12, 0), Qt::AlignVCenter | Qt::AlignRight, count);
    }

private:
    OutlinerView* view_;
    QLineEdit* input_;
};

// Molar masses, and for a reaction whether it balances: the card's small print, two lines.
QString chemNotes(const QString& tex) {
    QStringList lines;
    if (const QString m = massSummary(tex); !m.isEmpty()) lines << m;
    const QString dot = QString::fromUtf8("  \u00B7  ");
    int s = 0, len = 0;
    if (!reactionAt(tex, 0, &s, &len)) {
        // One species: its oxidation states ("Ox · Mn +7 · O −2").
        static const QRegularExpression ce(R"(\\ce\{\s*([^\s{}]+(?:\{[^{}]*\}[^\s{}]*)*)\s*\})");
        const QRegularExpressionMatch m = ce.match(tex);
        if (m.hasMatch())
            if (const QString ox = oxidationLine(m.captured(1)); !ox.isEmpty()) lines << "Ox" + dot + ox;
        return lines.join('\n');
    }
    const QString body = tex.mid(s, len);
    const BalanceResult b = balanceEquation(body);
    const bool eva = Theme::instance().immersion();
    if (b.ok && b.already) lines << (eva ? "Balanced  \u00B7  sync 100%" : "Balanced");
    else if (b.ok) lines << (eva ? "Unbalanced  \u00B7  " : "Balance:  ") + b.ce + "   /balance";
    else lines << "Can't balance: " + b.error;
    const RedoxReport r = redoxReport(body);
    if (!r.changes.isEmpty()) lines << "Redox" + dot + r.changes.join(dot);
    if (!r.reduction.isEmpty()) lines << "Reduction" + dot + r.reduction;
    if (!r.oxidation.isEmpty()) lines << "Oxidation" + dot + r.oxidation;
    return lines.join('\n');
}

// The live preview: the formula under the cursor, typeset, with molar masses for \ce.
class FormulaPreview : public QWidget {
public:
    explicit FormulaPreview(QWidget* parent) : QWidget(parent) {
        setAttribute(Qt::WA_TransparentForMouseEvents);
        hide();
    }
    // Returns false when there is nothing to show (TeX failed).
    // `label` replaces the molar-mass line (the quick-maths card says what it is).
    bool set(const QString& tex, bool display, qreal dpr, const QString& label = QString()) {
        const Theme& th = Theme::instance();
        const qreal px = QFontInfo(th.bodyFont()).pixelSize() * 1.25;
        img_ = MathObject::image(tex, px, th.text(), display, dpr);
        mass_ = label.isNull() ? chemNotes(tex) : label;
        if (img_.isNull()) return false;
        const QFontMetrics lm(noteFont());
        const QStringList lines = mass_.isEmpty() ? QStringList() : mass_.split('\n');
        int textW = 0;
        for (const QString& l : lines) textW = qMax(textW, lm.horizontalAdvance(l));
        const int w = qMax(static_cast<int>(img_.width() / img_.devicePixelRatio()), textW);
        const int h = static_cast<int>(img_.height() / img_.devicePixelRatio()) + (lines.isEmpty() ? 0 : lm.height() * static_cast<int>(lines.size()) + 8);
        resize(w + 2 * kPad, h + 2 * kPad);
        update();
        return true;
    }

protected:
    void paintEvent(QPaintEvent*) override {
        const Theme& th = Theme::instance();
        QPainter p(this);
        QColor fill = th.palette().panel;
        fill.setAlpha(th.transparent() ? 235 : 255);
        p.fillRect(rect(), fill);
        p.setPen(QPen(th.line(), 1));
        p.drawRect(rect().adjusted(0, 0, -1, -1));
        p.fillRect(QRect(0, 0, 3, height()), th.structure());  // a structure-coloured edge, like a tag
        p.drawImage(QPointF(kPad, kPad), img_);
        if (!mass_.isEmpty()) {
            const QStringList lines = mass_.split('\n');
            p.setFont(noteFont());
            const int lh = QFontMetrics(noteFont()).height();
            int y = height() - kPad - lh * static_cast<int>(lines.size());
            for (int i = 0; i < lines.size(); ++i, y += lh) {
                // A reaction line that needs fixing is in the warm colour.
                const bool needsWork = lines[i].startsWith("Unbalanced") || lines[i].startsWith("Balance:") || lines[i].startsWith("Can't");
                p.setPen(needsWork ? th.warm() : th.dim());
                p.drawText(QRect(kPad, y, width(), lh), Qt::AlignLeft | Qt::AlignTop, lines[i]);
            }
        }
    }

private:
    // The small print, in its own case: element symbols and units are case-sensitive (Fe, mL).
    static QFont noteFont() {
        QFont f = Theme::instance().labelFont();
        f.setCapitalization(QFont::MixedCase);
        return f;
    }
    static constexpr int kPad = 10;
    QImage img_;
    QString mass_;
};

}  // namespace

OutlinerView::OutlinerView(QWidget* parent) : QAbstractScrollArea(parent) {
    setFrameShape(QFrame::NoFrame);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setFocusPolicy(Qt::StrongFocus);
    viewport()->setMouseTracking(true);
    viewport()->setAutoFillBackground(false);
    verticalScrollBar()->setSingleStep(36);

    editor_ = new QTextEdit(viewport());
    editor_->setFrameShape(QFrame::NoFrame);
    editor_->setAcceptRichText(false);
    editor_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    editor_->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    editor_->setLineWrapMode(QTextEdit::WidgetWidth);
    editor_->setWordWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
    editor_->document()->setDocumentMargin(0);
    editor_->setContentsMargins(0, 0, 0, 0);
    editor_->setCursorWidth(2);
    editor_->viewport()->setAutoFillBackground(false);
    editor_->installEventFilter(this);
    editor_->hide();
    applyEditorStyle();

    connect(editor_, &QTextEdit::textChanged, this, &OutlinerView::syncFromEditor);
    connect(editor_, &QTextEdit::cursorPositionChanged, this, &OutlinerView::ensureCursorVisible);

    // A blinking cursor repaints twice a second forever, which keeps the laptop from idling.
    // Like GNOME, blink while you work and hold it solid 10 s after the last key or click.
    flashTime_ = QGuiApplication::styleHints()->cursorFlashTime();
    countdown_.setSingleShot(true);
    connect(&countdown_, &QTimer::timeout, this, &OutlinerView::countdownTick);
    blinkIdle_.setSingleShot(true);
    blinkIdle_.setInterval(10000);
    connect(&blinkIdle_, &QTimer::timeout, this, [this] {
        if (flashTime_ <= 0) return;
        blinking_ = false;
        QGuiApplication::styleHints()->setCursorFlashTime(0);
        if (editor_->hasFocus()) {  // the editor reads the blink time when it gains focus
            QFocusEvent out(QEvent::FocusOut, Qt::ActiveWindowFocusReason), in(QEvent::FocusIn, Qt::ActiveWindowFocusReason);
            QCoreApplication::sendEvent(editor_, &out);
            QCoreApplication::sendEvent(editor_, &in);
        }
    });
    connect(editor_, &QTextEdit::cursorPositionChanged, this, &OutlinerView::wakeCursor);

    preview_ = new FormulaPreview(viewport());
    snippetPopup_ = new SuggestPopup(viewport());
    findBar_ = new FindBar(this, viewport());
    pickImage_ = [this] {
        return QFileDialog::getOpenFileName(this, "Insert image", QDir::homePath(),
                                            "Images (*.png *.jpg *.jpeg *.gif *.webp *.bmp *.svg)");
    };
    connect(editor_, &QTextEdit::cursorPositionChanged, this, &OutlinerView::updatePreview);
    connect(editor_, &QTextEdit::cursorPositionChanged, this, &OutlinerView::updateSnippets);
}

QList<QPair<QString, QString>> OutlinerView::snippetHelp() {
    QList<QPair<QString, QString>> out;
    for (const Snippet& s : snippets()) {
        QString give = QString::fromUtf8(s.text ? s.text : s.math);
        give.remove('|');
        give.replace("{today}", "today");
        out.append({QString::fromUtf8(s.trigger), give + '\t' + QString::fromUtf8(s.what)});  // result, tab, description
    }
    return out;
}

void OutlinerView::updateSnippets() {
    auto* pop = static_cast<SuggestPopup*>(snippetPopup_);
    // The date list stays while the cursor stays where the date will go.
    if ((suggestMode_ == Suggest::Date || suggestMode_ == Suggest::Time) && pop->isVisible() &&
        editor_->textCursor().position() == datePos_)
        return;
    suggestMode_ = Suggest::None;
    if (active_ < 0 || !editor_->isVisible() || editor_->textCursor().hasSelection()) {
        pop->hide();
        return;
    }
    const QString text = editor_->toPlainText();
    const int pos = editor_->textCursor().position();
    int start = 0;
    QList<SuggestRow> rows;
    QString query;
    const QString lineBefore = editor_->textCursor().block().text().left(editor_->textCursor().positionInBlock());
    const QString timerArgs = timerCommand(lineBefore);
    if (!timerArgs.isNull() && lineBefore.contains(QRegularExpression("/timer\\s"))) {
        // "/timer …": what Enter will start, then presets. Stays up while you type.
        timerRows_.clear();
        const int secs = parseDuration(timerArgs);
        if (timerArgs.trimmed().toLower() == "stop") {
            rows << SuggestRow{"Enter", "Stop the operation", QImage(), "stop"};
            timerRows_ << QString();
        } else if (secs > 0) {
            rows << SuggestRow{"Enter", "Start  " + formatDuration(secs), QImage(), "operation"};
            timerRows_ << QString();
        } else {  // nothing readable yet: Enter only explains, it never starts a preset
            rows << SuggestRow{"?", "Type a duration", QImage(), "20m 15s  \xC2\xB7  25  \xC2\xB7  1h 5m"};
            timerRows_ << QString();
        }
        static const char* presets[][2] = {{"25m", "Pomodoro"}, {"50m", "Long session"}, {"1h 30m", "Lecture"}, {"10m", "Break"},
                                           {"stop", "End the running one"}};
        for (const auto& pr : presets) {
            rows << SuggestRow{QString::fromUtf8(pr[0]), QString::fromUtf8(pr[1]), QImage(), QString()};
            timerRows_ << QString::fromUtf8(pr[0]);
        }
        suggestMode_ = Suggest::Timer;
    } else if (commandQuery(text, pos, &start, &query)) {  // "/..." : the command menu
        commandMatches_ = matchCommands(query);
        for (int c : commandMatches_)
            rows << SuggestRow{QString(), QString::fromUtf8(commands()[c].name), QImage(), QString::fromUtf8(commands()[c].hint)};
        if (!rows.isEmpty()) suggestMode_ = Suggest::Commands;
    } else {  // ";..." or "\ce" : snippets
        const QString prefix = snippetPrefix(text, pos, &start);
        snippetMath_ = insideMath(text, start) || (start > 0 && text[start - 1] == '$');
        snippetMatches_ = matchSnippets(prefix, snippetMath_);
        for (const Snippet& sn : snippetMatches_) rows << snippetRow(sn, snippetMath_, devicePixelRatioF());
        if (!rows.isEmpty()) suggestMode_ = Suggest::Snippets;
    }
    if (suggestMode_ == Suggest::None) {
        pop->hide();
        return;
    }
    pop->set(rows);
    const QRect cr = editor_->cursorRect();
    QPoint at = editor_->mapTo(viewport(), QPoint(cr.left(), cr.bottom() + 6));
    if (at.y() + pop->height() > viewport()->height())
        at.setY(editor_->mapTo(viewport(), QPoint(0, cr.top() - 6)).y() - pop->height());
    at.setX(qBound(8, at.x() - 12, viewport()->width() - pop->width() - 8));
    pop->QWidget::move(at);
    pop->show();
    pop->raise();
    preview_->hide();  // one card at a time
}

// The list after /scheduled or /deadline: a blank date to type first, then the usual ones.
void OutlinerView::openDatePicker(const QString& kind) {
    const QDate today = QDate::currentDate();
    auto nextWeekday = [&](int dow) {  // the coming one, never today
        int add = (dow - today.dayOfWeek() + 7) % 7;
        return today.addDays(add == 0 ? 7 : add);
    };
    dateKind_ = kind;
    datePos_ = editor_->textCursor().position();
    dateRows_ = {QDate(), today, today.addDays(1), today.addDays(3), nextWeekday(6), nextWeekday(1),
                 today.addDays(7), today.addDays(14), today.addMonths(1)};
    static const char* names[] = {"Type a date", "Today", "Tomorrow", "In 3 days", "Saturday", "Next Monday",
                                  "In a week", "In two weeks", "In a month"};
    QList<SuggestRow> rows;
    for (int i = 0; i < dateRows_.size(); ++i)
        rows << SuggestRow{i == 0 ? kind : QString(), QString::fromUtf8(names[i]), QImage(),
                           i == 0 ? QString(kind == "DEADLINE" ? "yyyy-mm-dd hh:mm" : "yyyy-mm-dd")
                                  : QLocale::c().toString(dateRows_[i], "ddd d MMM")};
    static_cast<SuggestPopup*>(snippetPopup_)->set(rows);
    showDatePopup(Suggest::Date);
}

// After a deadline's date: no time (first), one to type, or the usual ones. A deadline with a
// time counts down in hours and minutes.
void OutlinerView::openTimePicker(const QDate& day) {
    const QTime now = QTime::currentTime();
    timeRows_ = {QTime(), QTime()};
    QList<SuggestRow> rows = {{"DEADLINE", "No time", QImage(), "the whole day"}, {QString(), "Type a time", QImage(), "hh:mm"}};
    auto add = [&](const QTime& t, const QString& name) {
        if (day == QDate::currentDate() && t <= now) return;  // already past
        timeRows_ << t;
        rows << SuggestRow{QString(), name, QImage(), t.toString("HH:mm")};
    };
    if (day == QDate::currentDate()) {
        for (int h : {1, 3}) {
            const QTime t = now.addSecs(h * 3600 + (5 - now.minute() % 5) * 60).addSecs(-now.second());
            if (t > now) add(QTime(t.hour(), t.minute()), h == 1 ? "In an hour" : "In 3 hours");
        }
    }
    add(QTime(9, 0), "Morning");
    add(QTime(12, 0), "Noon");
    add(QTime(17, 0), "Evening");
    add(QTime(23, 59), "Before midnight");
    static_cast<SuggestPopup*>(snippetPopup_)->set(rows);
    showDatePopup(Suggest::Time);
}

void OutlinerView::showDatePopup(Suggest mode) {
    auto* pop = static_cast<SuggestPopup*>(snippetPopup_);
    suggestMode_ = mode;
    const QRect cr = editor_->cursorRect();
    QPoint at = editor_->mapTo(viewport(), QPoint(cr.left(), cr.bottom() + 6));
    if (at.y() + pop->height() > viewport()->height())
        at.setY(editor_->mapTo(viewport(), QPoint(0, cr.top() - 6)).y() - pop->height());
    at.setX(qBound(8, at.x() - 12, viewport()->width() - pop->width() - 8));
    pop->QWidget::move(at);
    pop->show();
    pop->raise();
    preview_->hide();
}

bool OutlinerView::acceptDate(int row) {
    const QString text = editor_->toPlainText();
    const int pos = qBound(0, datePos_, static_cast<int>(text.size()));
    const QDate d = dateRows_.value(row);
    const bool spaceBefore = pos > 0 && !text[pos - 1].isSpace();
    const QString chip = dateKind_ + ": <" + (d.isValid() ? QLocale::c().toString(d, "yyyy-MM-dd ddd") : QString()) + ">";
    QTextCursor c = editor_->textCursor();
    c.beginEditBlock();
    c.setPosition(pos);
    c.insertText((spaceBefore ? " " : "") + chip);
    c.endEditBlock();
    // A blank date leaves the cursor inside the brackets, to type it.
    if (!d.isValid()) c.setPosition(pos + (spaceBefore ? 1 : 0) + static_cast<int>(dateKind_.size()) + 3);
    suggestMode_ = Suggest::None;
    snippetPopup_->hide();
    datePos_ = -1;
    if (d.isValid() && dateKind_ == "DEADLINE") {
        c.setPosition(c.position() - 1);  // before the ">": the time goes there
        editor_->setTextCursor(c);
        datePos_ = c.position();
        openTimePicker(d);
        return true;
    }
    editor_->setTextCursor(c);
    return true;
}

bool OutlinerView::acceptTime(int row) {
    QTextCursor c = editor_->textCursor();
    c.setPosition(qBound(0, datePos_, static_cast<int>(editor_->toPlainText().size())));
    const QTime t = timeRows_.value(row);
    if (t.isValid()) c.insertText(" " + t.toString("HH:mm"));
    else if (row == 1) c.insertText(" ");  // "Type a time": the cursor waits in the brackets
    if (row != 1) c.movePosition(QTextCursor::NextCharacter);  // past the ">"
    editor_->setTextCursor(c);
    suggestMode_ = Suggest::None;
    snippetPopup_->hide();
    datePos_ = -1;
    return true;
}

// "/timer 20m 15s" ending the line before the cursor: take it out of the note and run it.
bool OutlinerView::runTimerLine() {
    QTextCursor c = editor_->textCursor();
    const QString line = c.block().text().left(c.positionInBlock());
    int start = 0;
    const QString args = timerCommand(line, &start);
    if (args.isNull()) return false;
    c.beginEditBlock();
    c.setPosition(c.block().position() + start);
    c.setPosition(editor_->textCursor().position(), QTextCursor::KeepAnchor);
    c.removeSelectedText();
    while (c.position() > c.block().position() && editor_->document()->characterAt(c.position() - 1) == ' ')
        c.deletePreviousChar();
    c.endEditBlock();
    editor_->setTextCursor(c);
    snippetPopup_->hide();
    suggestMode_ = Suggest::None;
    emit operationCommand(args);
    return true;
}

bool OutlinerView::acceptSnippet() {
    auto* pop = static_cast<SuggestPopup*>(snippetPopup_);
    if (!pop->isVisible() || pop->current() < 0) return false;
    if (suggestMode_ == Suggest::Date) return acceptDate(pop->current());
    if (suggestMode_ == Suggest::Time) return acceptTime(pop->current());
    if (suggestMode_ == Suggest::Timer) {
        // The typed duration (first row, when it reads) or a preset in its place, then run.
        const QString preset = timerRows_.value(pop->current());
        if (!preset.isEmpty()) {
            QTextCursor c = editor_->textCursor();
            const QString line = c.block().text().left(c.positionInBlock());
            int start = 0;
            timerCommand(line, &start);
            c.setPosition(c.block().position() + start);
            c.setPosition(editor_->textCursor().position(), QTextCursor::KeepAnchor);
            c.insertText("/timer " + preset);
            editor_->setTextCursor(c);
        }
        return runTimerLine();
    }
    if (suggestMode_ == Suggest::Commands) {
        const int c = commandMatches_.value(pop->current(), -1);
        pop->hide();
        return c >= 0 && runCommand(c);
    }
    const Snippet s = snippetMatches_.value(pop->current());
    const QString text = editor_->toPlainText();
    const int pos = editor_->textCursor().position();
    int start = 0;
    if (snippetPrefix(text, pos, &start).isEmpty()) return false;
    // "$\\ce" typed before the closing $ exists: use the maths form and close the formula.
    const bool lonelyDollar = start > 0 && text[start - 1] == '$' && !insideMath(text, start);
    const bool math = insideMath(text, start) || lonelyDollar;
    int offset = 0;
    QString insert = expandSnippet(s, math, todayTitle_ ? todayTitle_() : QString(), &offset);
    if (lonelyDollar && !(pos < text.size() && text[pos] == '$')) insert += '$';
    QTextCursor c = editor_->textCursor();
    c.beginEditBlock();  // one undo step
    c.setPosition(start);
    c.setPosition(pos, QTextCursor::KeepAnchor);
    c.insertText(insert);
    c.endEditBlock();
    c.setPosition(start + offset);
    editor_->setTextCursor(c);
    pop->hide();
    return true;
}

bool OutlinerView::runCommand(int index) {
    const Cmd cmd = commands()[index].id;
    QString text = editor_->toPlainText();
    const int pos = editor_->textCursor().position();
    int start = 0;
    QString query;
    if (!commandQuery(text, pos, &start, &query)) return false;
    text.remove(start, pos - start);  // the "/query" goes away first
    int cursor = start;

    auto dateLink = [this](int days) {
        const QString t = journalTitleFor_ ? journalTitleFor_(QDate::currentDate().addDays(days)) : QString();
        return "[[" + t + "]]";
    };
    // Text to put at the cursor; "|" is where the cursor ends up.
    QString insert;
    bool whole = false;  // the block's text was rewritten in place (prefix changes)

    static const QRegularExpression heading(R"(^#{1,6} )");
    auto setHeading = [&](int level) {
        const int old = static_cast<int>(heading.match(text).capturedLength());
        const QString prefix = level > 0 ? QString(level, '#') + ' ' : QString();
        text = prefix + text.mid(old);
        cursor = qMax(0, cursor - old + static_cast<int>(prefix.size()));
        whole = true;
    };
    auto setMarker = [&](const QString& marker) {
        const int before = static_cast<int>(text.size());
        text = setTaskMarker(text, marker);
        cursor = qBound(0, cursor + static_cast<int>(text.size()) - before, static_cast<int>(text.size()));
        whole = true;
    };

    switch (cmd) {
        case Cmd::H1: setHeading(1); break;
        case Cmd::H2: setHeading(2); break;
        case Cmd::H3: setHeading(3); break;
        case Cmd::Text: setHeading(0); break;
        case Cmd::Todo: setMarker("TODO"); break;
        case Cmd::Doing: setMarker("DOING"); break;
        case Cmd::Done: setMarker("DONE"); break;
        case Cmd::Later: setMarker("LATER"); break;
        case Cmd::Now: setMarker("NOW"); break;
        case Cmd::Scheduled:
        case Cmd::Deadline: {
            // The date goes where the command was typed (the task's own line, or a line of its
            // own); an older date of the same kind in the block goes. Then the date list opens.
            const QString kind = cmd == Cmd::Scheduled ? "SCHEDULED" : "DEADLINE";
            const QRegularExpression old("\\s*" + kind + ":\\s*<[^>]*>");
            for (QRegularExpressionMatch om = old.match(text); om.hasMatch(); om = old.match(text)) {
                if (om.capturedStart() < cursor) cursor -= qMin(static_cast<int>(om.capturedLength()), cursor - static_cast<int>(om.capturedStart()));
                text.remove(om.capturedStart(), om.capturedLength());
            }
            QTextCursor c(editor_->document());
            c.beginEditBlock();
            c.select(QTextCursor::Document);
            c.insertText(text);
            c.endEditBlock();
            c.setPosition(qBound(0, cursor, static_cast<int>(text.size())));
            editor_->setTextCursor(c);
            openDatePicker(kind);
            return true;
        }
        case Cmd::Today: insert = dateLink(0); break;
        case Cmd::Tomorrow: insert = dateLink(1); break;
        case Cmd::Yesterday: insert = dateLink(-1); break;
        case Cmd::PageLink: insert = "[[|]]"; break;
        case Cmd::Tag: insert = "#"; break;
        case Cmd::Math: insert = "$|$"; break;
        case Cmd::Display: insert = "$$|$$"; break;
        case Cmd::Chem: insert = "$\\ce{|}$"; break;
        case Cmd::Unit: insert = "$\\pu{|}$"; break;
        case Cmd::Calc:  // a line of its own that starts with "= "
            insert = (cursor == 0 || text[cursor - 1] == '\n' ? QString() : QString("\n")) + "= |";
            break;
        case Cmd::Balance: {  // the reaction around the cursor, or the block's first one
            whole = true;
            int s = 0, len = 0;
            if (!reactionAt(text, cursor, &s, &len)) {
                emit message("No \\ce{\u2026} reaction with an arrow in this block");
                break;
            }
            const BalanceResult b = balanceEquation(text.mid(s, len));
            if (!b.ok) {
                emit message("Can't balance: " + b.error);
                break;
            }
            if (b.already) {
                emit message("Already balanced");
                break;
            }
            text.replace(s, len, b.ce);
            if (cursor > s + len) cursor += static_cast<int>(b.ce.size()) - len;
            else if (cursor > s) cursor = s + static_cast<int>(b.ce.size());
            emit message("Balanced: " + b.ce);
            break;
        }
        case Cmd::LabTable: {  // headers "name / unit"; a "= ..." header is worked out per row
            // Inserted directly: the table is made of the "|" that marks the cursor elsewhere.
            const QString pre = cursor == 0 || text[cursor - 1] == '\n' ? QString() : QString("\n");
            const QString table = QString::fromUtf8("| m / g | V / mL | ρ = m/V to g/mL |\n|---|---|---|\n|  |  |  |");
            text.insert(cursor, pre + table);
            cursor += static_cast<int>(pre.size() + table.size()) - 8;  // in the first empty cell
            whole = true;
            break;
        }
        case Cmd::Plot:
            insert = (cursor == 0 || text[cursor - 1] == '\n' ? QString() : QString("\n")) + "=plot{|}";
            break;
        case Cmd::Graph:
            insert = (cursor == 0 || text[cursor - 1] == '\n' ? QString() : QString("\n")) + "=graph{|, x from -5 to 5}";
            break;
        case Cmd::Code: insert = "```\n|\n```"; break;
        case Cmd::Quote:
            if (!text.startsWith("> ")) {
                text = "> " + text;
                cursor += 2;
            }
            whole = true;
            break;
        case Cmd::Divider: insert = "---"; break;
        case Cmd::Time: insert = QTime::currentTime().toString("HH:mm"); break;
        case Cmd::Timer: insert = "/timer "; break;  // then the duration, then Enter
        case Cmd::Progress:  // the end selected, to type over
            insert = (cursor > 0 && !text.at(cursor - 1).isSpace() ? " " : "") + QString("{progress 0/|10 by 1}");
            selectAfter_ = 2;
            break;
        case Cmd::Scrap:  // asked once the "/scrap" is out of the text
            QTimer::singleShot(0, this, [this] { emit scrapRequested(); });
            break;
        case Cmd::Image: {
            const QString file = pickImage_ ? pickImage_() : QString();
            const QString link = file.isEmpty() ? QString() : importImage(file);
            if (link.isEmpty()) {
                whole = true;  // cancelled: just drop the "/image"
                break;
            }
            insert = link;
            break;
        }
        case Cmd::Lecture: {
            // This block becomes the lecture heading, with the usual parts as children.
            const int block = active_;
            pushUndoPoint();
            const QString title = text.trimmed().isEmpty()
                                      ? "## Lecture \xC2\xB7 " + (journalTitleFor_ ? journalTitleFor_(QDate::currentDate()) : QString())
                                      : text;
            doc_->setText(block, title.toStdString());
            int prev = -1;
            for (const char* part : {"Topics", "Key equations", "Examples", "Questions to ask"}) {
                if (prev < 0) {
                    prev = doc_->insertBelow(block, part);
                    if (doc_->at(prev).depth == doc_->at(block).depth) doc_->indent(prev);
                } else {
                    prev = doc_->insertBelow(prev, part);
                }
            }
            active_ = -1;
            rebuild();
            focusBlock(block + 1);  // "Topics", ready to type under
            changed();
            return true;
        }
    }

    QTextCursor c(editor_->document());
    c.beginEditBlock();  // the whole command is one undo step
    c.select(QTextCursor::Document);
    if (whole) {
        c.insertText(text);
    } else {
        const int bar = static_cast<int>(insert.indexOf('|'));
        insert.remove('|');
        c.insertText(text.left(cursor) + insert + text.mid(cursor));
        cursor += bar >= 0 ? bar : static_cast<int>(insert.size());
    }
    c.endEditBlock();
    c.setPosition(qBound(0, cursor, static_cast<int>(editor_->toPlainText().size())));
    if (selectAfter_ > 0) c.setPosition(qMin(cursor + std::exchange(selectAfter_, 0), static_cast<int>(editor_->toPlainText().size())), QTextCursor::KeepAnchor);
    editor_->setTextCursor(c);
    return true;
}


void OutlinerView::updatePreview() {
    auto* pv = static_cast<FormulaPreview*>(preview_);
    if (active_ < 0 || !editor_->isVisible()) {
        pv->hide();
        return;
    }
    // The $...$ or $$...$$ around the cursor, on the cursor's line.
    const QString text = editor_->toPlainText();
    const int pos = editor_->textCursor().position();
    auto place = [&] {  // under the cursor's line; above it when there is no room below
        const QRect cr = editor_->cursorRect();
        const QPoint below = editor_->mapTo(viewport(), QPoint(0, cr.bottom() + 8));
        int y = below.y();
        if (y + pv->height() > viewport()->height()) y = editor_->mapTo(viewport(), QPoint(0, cr.top() - 8)).y() - pv->height();
        const int x = qMin(editor_->mapTo(viewport(), QPoint(0, 0)).x(), viewport()->width() - pv->width() - 8);
        pv->move(qMax(8, x), y);
        pv->show();
        pv->raise();
    };
    // A "= ..." line: its result, typeset, while you type (a plot line is not a calculation).
    const QString cursorLine = editor_->textCursor().block().text();
    if (const QString expr = calcExpression(cursorLine); !expr.isNull() && !expr.isEmpty() && plotSpec(cursorLine).isNull()) {
        const CalcResult res = evaluate(expr);
        const QString label = res.ok ? (Theme::instance().immersion() ? QString("MAGI  \u00B7  Result") : QString("Result")) : res.error;
        if (pv->set(res.ok ? calcTex(res.value) : QString("\\text{?}"), false, devicePixelRatioF(), label)) {
            place();
            return;
        }
    }
    static const QRegularExpression math(R"(\$\$(.+?)\$\$|\$([^$\n]+?)\$)");
    for (auto it = math.globalMatch(text); it.hasNext();) {
        const QRegularExpressionMatch m = it.next();
        if (pos <= m.capturedStart() || pos >= m.capturedEnd()) continue;
        const bool display = m.capturedLength(1) > 0;
        const QString tex = display ? m.captured(1) : m.captured(2);
        if (tex.trimmed().isEmpty() || !pv->set(tex, display, devicePixelRatioF())) break;
        // Under the cursor's line; above it when there is no room below.
        const QRect cr = editor_->cursorRect();
        const QPoint below = editor_->mapTo(viewport(), QPoint(0, cr.bottom() + 8));
        int y = below.y();
        if (y + pv->height() > viewport()->height()) y = editor_->mapTo(viewport(), QPoint(0, cr.top() - 8)).y() - pv->height();
        const int x = qMin(editor_->mapTo(viewport(), QPoint(0, 0)).x(), viewport()->width() - pv->width() - 8);
        pv->move(qMax(8, x), y);
        pv->show();
        pv->raise();
        return;
    }
    pv->hide();
}

QString OutlinerView::formulaAt(const QPoint& vp) const {
    const Hit h = hitTest(vp);
    if (h.item < 0 || h.region != Region::Text) return QString();
    const Item& it = items_[static_cast<size_t>(h.item)];
    if (it.block == active_) return QString();
    const QPoint c = toContent(vp);
    const int pos = it.doc->documentLayout()->hitTest(QPointF(c.x() - it.textX, c.y() - (it.y + kPadY)), Qt::ExactHit);
    if (pos < 0) return QString();
    QTextCursor cur(it.doc.get());
    cur.setPosition(pos);
    cur.movePosition(QTextCursor::NextCharacter);
    const QTextCharFormat f = cur.charFormat();
    return f.objectType() == MathObject::kType ? f.property(MathObject::Source).toString() : QString();
}

QString OutlinerView::definitionAt(const QPoint& vp) const {
    const Hit h = hitTest(vp);
    if (h.item < 0 || h.region != Region::Text) return QString();
    const Item& it = items_[static_cast<size_t>(h.item)];
    if (it.block == active_) return QString();
    const QPoint c = toContent(vp);
    const int pos = it.doc->documentLayout()->hitTest(QPointF(c.x() - it.textX, c.y() - (it.y + kPadY)), Qt::ExactHit);
    if (pos < 0) return QString();
    QTextCursor cur(it.doc.get());
    cur.setPosition(pos);
    cur.movePosition(QTextCursor::NextCharacter);
    const QStringList d = cur.charFormat().property(kDefinitionProperty).toStringList();
    if (d.size() < 3) return QString();
    // Term, meaning, then where it is defined.
    return "<b>" + d[0].toHtmlEscaped() + "</b><br>" + d[1].toHtmlEscaped() +
           (d[2].isEmpty() ? QString() : "<br><span style=\"color:" + Theme::instance().faint().name() + "\">" + d[2].toHtmlEscaped() + "</span>");
}

void OutlinerView::setGlossary(const QList<Definition>& definitions) {
    auto same = [](const QList<Definition>& a, const QList<Definition>& b) {
        if (a.size() != b.size()) return false;
        for (int i = 0; i < a.size(); ++i)
            if (a[i].term != b[i].term || a[i].text != b[i].text || a[i].page != b[i].page) return false;
        return true;
    };
    if (same(definitions, definitions_)) return;
    definitions_ = definitions;
    rich_.glossary = definitions.isEmpty() ? nullptr : std::make_shared<const Glossary>(makeGlossary(definitions));
    if (doc_) {
        const int editing = active_;
        if (editing < 0) rebuild();  // while editing, the next rebuild picks it up
    }
}

bool OutlinerView::goToEquation(const QString& label) {
    if (!doc_) return false;
    const QString tag = "{#" + label + "}";
    for (int b = 0; b < doc_->size(); ++b) {
        if (QString::fromStdString(doc_->at(b).text).contains(tag)) {
            const int k = itemOf(b);
            if (k < 0) return false;  // inside a folded block
            verticalScrollBar()->setValue(qMax(0, items_[static_cast<size_t>(k)].y - viewport()->height() / 3));
            selectBlocks(b, b);
            return true;
        }
    }
    return false;
}

void OutlinerView::wakeCursor() {
    if (flashTime_ <= 0) return;
    if (!blinking_) {
        blinking_ = true;
        QGuiApplication::styleHints()->setCursorFlashTime(flashTime_);
        if (editor_->hasFocus()) {
            QFocusEvent out(QEvent::FocusOut, Qt::ActiveWindowFocusReason), in(QEvent::FocusIn, Qt::ActiveWindowFocusReason);
            QCoreApplication::sendEvent(editor_, &out);
            QCoreApplication::sendEvent(editor_, &in);
        }
    }
    blinkIdle_.start();
}

OutlinerView::~OutlinerView() = default;

void OutlinerView::applyEditorStyle() {
    const Theme& th = Theme::instance();
    auto css = [](const QColor& c) {
        return QString("rgba(%1,%2,%3,%4)").arg(c.red()).arg(c.green()).arg(c.blue()).arg(c.alpha());
    };
    editor_->setStyleSheet(QString("QTextEdit { background: transparent; border: none; color: %1;"
                                   " selection-background-color: %2; selection-color: %1; }")
                               .arg(css(th.text()), css(th.selection())));
}

void OutlinerView::refreshTheme() {
    applyEditorStyle();
    if (findBar_ && findBar_->isVisible()) static_cast<FindBar*>(findBar_)->restyle();
    if (active_ >= 0 && doc_) editor_->setFont(fontForText(editor_->toPlainText()));
    buildReferences();
    rebuild();
}

void OutlinerView::setReferences(const QList<RefGroup>& groups, int total) {
    refGroups_ = groups;
    refTotal_ = total;
    hoverRef_ = -1;
    buildReferences();
    relayout();
}

void OutlinerView::setUpcoming(const QString& label, const QList<RefGroup>& groups) {
    if (label == upcomingLabel_ && groups == upcoming_) return;
    upcomingLabel_ = label;
    upcoming_ = groups;
    hoverRef_ = -1;
    buildReferences();
    relayout();
}

void OutlinerView::buildReferences() {
    refItems_.clear();
    const Theme& th = Theme::instance();
    RichContext refContext;  // other pages: their terms are underlined, their equations are not numbered here
    refContext.glossary = rich_.glossary;
    for (int section : {0, 1}) {
        const QList<RefGroup>& groups = groupsOf(section);
        for (int g = 0; g < groups.size(); ++g) {
            RefItem title;
            title.section = section;
            title.group = g;
            refItems_.push_back(std::move(title));
            for (int r = 0; r < groups[g].blocks.size(); ++r) {
                RefItem it;
                it.section = section;
                it.group = g;
                it.row = r;
                it.doc = std::make_unique<QTextDocument>();
                buildRich(it.doc.get(), groups[g].blocks[r].second, th, devicePixelRatioF(),
                          QFileInfo(groups[g].path).absolutePath(), &refContext);
                refItems_.push_back(std::move(it));
            }
        }
    }
}

QRect OutlinerView::upcomingRect(int group, int row) const {
    for (const RefItem& it : refItems_)
        if (it.section == 0 && it.group == group && it.row == row)
            return QRect(it.textX, it.y + kPadY - verticalScrollBar()->value(), it.textW, it.h);
    return QRect();
}

QRect OutlinerView::referenceRect(int group, int row) const {
    for (const RefItem& it : refItems_)
        if (it.section == 1 && it.group == group && it.row == row)
            return QRect(it.textX, it.y + kPadY - verticalScrollBar()->value(), it.textW, it.h);
    return QRect();
}

int OutlinerView::refAt(const QPoint& c) const {
    for (size_t k = 0; k < refItems_.size(); ++k) {
        const RefItem& it = refItems_[k];
        if (c.y() >= it.y && c.y() < it.y + it.h + 2 * kPadY && c.x() >= it.textX - kGutter && c.x() < it.textX + it.textW)
            return static_cast<int>(k);
    }
    return -1;
}

void OutlinerView::setDocument(Document* doc) {
    pendingFold_.clear();  // not folded on the way out: the page is being left
    endEdit();
    refGroups_.clear();
    upcoming_.clear();
    upcomingLabel_.clear();
    refItems_.clear();
    refTotal_ = 0;
    doc_ = doc;
    revealCaption_ = false;
    lastActive_ = 0;
    hoverBlock_ = -1;
    undo_.clear();
    redo_.clear();
    editBlock_ = -1;
    findQuery_.clear();
    matches_.clear();
    findIndex_ = -1;
    if (findBar_) findBar_->hide();
    if (doc_) doc_->ensureNonEmpty();
    verticalScrollBar()->setValue(0);
    rebuild();
}

QFont OutlinerView::fontForText(const QString& raw) const {
    QFont f = Theme::instance().bodyFont();
    const int level = headingLevel(raw);
    if (level > 0) {
        f.setPointSizeF(f.pointSizeF() * headingScale(level));
        f.setBold(true);
    }
    return f;
}

QRect OutlinerView::textRect(int block) const {
    const int k = itemOf(block);
    if (k < 0) return QRect();
    const Item& it = items_[static_cast<size_t>(k)];
    return QRect(it.textX, it.y + kPadY - verticalScrollBar()->value(), it.textW, it.h);
}

int OutlinerView::itemOf(int block) const {
    for (size_t k = 0; k < items_.size(); ++k)
        if (items_[k].block == block) return static_cast<int>(k);
    return -1;
}

void OutlinerView::rebuild() {
    items_.clear();
    if (!doc_) {
        editor_->hide();
        viewport()->update();
        return;
    }
    // Display formulas labelled {#name}, numbered down the page (folded blocks count too).
    rich_.equations.clear();
    static const QRegularExpression label(R"(\$\$.+?\$\$\s*\{#([\w:.\-]+)\})");
    for (int b = 0; b < doc_->size(); ++b) {
        const QString t = QString::fromStdString(doc_->at(b).text);
        if (!t.contains("{#")) continue;
        for (auto m = label.globalMatch(t); m.hasNext();) {
            const QString l = m.next().captured(1);
            if (!rich_.equations.contains(l)) rich_.equations.insert(l, static_cast<int>(rich_.equations.size()) + 1);
        }
    }
    const Theme& th = Theme::instance();
    countProgress();
    for (int b : doc_->visible()) {
        if (captionBlock(b) && !revealCaption_ && active_ != b) continue;  // drawn in the header instead
        Item it;
        it.block = b;
        it.doc = std::make_unique<QTextDocument>();
        rich_.block = b;
        buildRich(it.doc.get(), QString::fromStdString(doc_->at(b).text), th, devicePixelRatioF(), pageDir_, &rich_);
        items_.push_back(std::move(it));
    }
    rich_.block = -1;
    if (!findQuery_.isEmpty()) applyFind();
    if (active_ >= 0 && itemOf(active_) < 0) endEdit();  // hidden by a collapse
    if (selAnchor_ >= static_cast<int>(items_.size()) || selHead_ >= static_cast<int>(items_.size())) clearSelection();
    relayout();
    scheduleCountdown();
}

QString OutlinerView::renderedText(int block) const {
    const int k = itemOf(block);
    return k >= 0 && items_[static_cast<size_t>(k)].doc ? items_[static_cast<size_t>(k)].doc->toPlainText() : QString();
}

bool OutlinerView::captionBlock(int block) const {
    return doc_ && block == 0 && doc_->size() > 1 && !doc_->at(0).bullet && doc_->at(0).text.rfind("description:: ", 0) == 0 &&
           doc_->at(0).text.find('\n') == std::string::npos;
}

QString OutlinerView::caption() const {
    if (!captionBlock(0)) return QString();
    return QString::fromStdString(doc_->at(0).text).mid(14).trimmed();
}

QRect OutlinerView::captionRect() const {
    const QString cap = caption();
    if (cap.isEmpty() || headerTitle_.isEmpty()) return QRect();
    int left, colW;
    columnGeometry(left, colW);
    const int x = left + kGutter, w = colW - kGutter;
    const QFontMetrics tm(Theme::instance().titleFont()), cm(captionFont());
    const int capW = qMin(w, cm.horizontalAdvance(cap) + 4);  // a little slack: rounding must not elide it
    const int cx = x;  // flush left under the title (the right-set caption is for the cards only)
    const int top = titleBaseline() + tm.descent() + 2 - verticalScrollBar()->value();
    return QRect(cx, top, capW, cm.height());
}

void OutlinerView::setTimelineToday(bool on) {
    if (!on && !timeline_) return;
    timeline_ = on;
    viewport()->update();
}

void OutlinerView::setAlerts(const QList<Alert>& alerts) {
    if (alerts == alerts_) return;
    alerts_ = alerts;
    relayout();
    scheduleCountdown();
}

int alertHeight() { return QFontMetrics(Theme::instance().labelFont()).height() + 16; }
int alertRowHeight() { return QFontMetrics(Theme::instance().bodyFont()).lineSpacing() + 6; }
int alertFullHeight(const OutlinerView::Alert& a) {
    const int rows = static_cast<int>(a.rows.size()) + (a.more > 0 ? 1 : 0);
    return alertHeight() + (rows ? rows * alertRowHeight() + 6 : 0);
}

QRect OutlinerView::alertRect(int i) const {
    if (alertTop_ < 0 || i < 0 || i >= alerts_.size()) return QRect();
    int left, colW;
    columnGeometry(left, colW);
    int y = alertTop_;
    for (int k = 0; k < i; ++k) y += alertFullHeight(alerts_[k]) + 6;
    return QRect(left + kGutter, y - verticalScrollBar()->value(), colW - kGutter, alertFullHeight(alerts_[i]));
}

// The task row of an alert strip under `viewportPos`, or -1.
int OutlinerView::alertRowAt(const QPoint& pos, int* alert) const {
    for (int a = 0; a < alerts_.size(); ++a) {
        const QRect r = alertRect(a);
        if (!r.contains(pos)) continue;
        const int k = (pos.y() - r.top() - alertHeight()) / alertRowHeight();
        if (pos.y() - r.top() < alertHeight() || k >= alerts_[a].rows.size()) return -1;
        if (alert) *alert = a;
        return k;
    }
    return -1;
}

// A deadline with a time on the page, or in a strip: redraw its countdown on each minute.
void OutlinerView::scheduleCountdown() {
    bool any = false;
    for (const Alert& a : alerts_)
        for (const AlertRow& r : a.rows) any = any || r.at.isValid();
    for (size_t k = 0; !any && doc_ && k < items_.size(); ++k) {
        const std::string& t = doc_->at(items_[k].block).text;
        any = t.find("DEADLINE:") != std::string::npos && blockDeadlineTime(QString::fromStdString(t)).isValid();
    }
    if (!any) {
        countdown_.stop();
        return;
    }
    const QTime now = QTime::currentTime();
    countdown_.start((60 - now.second()) * 1000 - now.msec() + 50);
}

void OutlinerView::countdownTick() {
    if (!doc_) return;
    const Theme& th = Theme::instance();
    for (Item& it : items_) {
        if (it.block == active_) continue;  // the editor shows the raw text
        const QString t = QString::fromStdString(doc_->at(it.block).text);
        rich_.block = it.block;
        if (t.contains("DEADLINE:") && blockDeadlineTime(t).isValid())
            buildRich(it.doc.get(), t, th, devicePixelRatioF(), pageDir_, &rich_);
    }
    rich_.block = -1;
    if (!findQuery_.isEmpty()) applyFind();
    relayout();
    viewport()->update();
    scheduleCountdown();
}

void OutlinerView::setHeader(const QString& title, const QString& kind, const QDate& date) {
    headerTitle_ = title;
    headerKind_ = kind;
    headerDate_ = date;
    relayout();
}

void OutlinerView::columnGeometry(int& left, int& width) const {
    const int vw = viewport()->width();
    width = qMin(vw - 2 * kSideMargin, kMaxColumn);
    left = qMax(kSideMargin, (vw - width) / 2);
}

void OutlinerView::relayout() {
    if (!doc_) return;
    int left, colW;
    columnGeometry(left, colW);
    const int minH = QFontMetrics(Theme::instance().bodyFont()).lineSpacing();

    int y = kTopMargin + (headerTitle_.isEmpty() ? 0 : headerHeight());
    if (!headerTitle_.isEmpty() && !caption().isEmpty()) y += captionHeight();
    alertTop_ = -1;
    if (!alerts_.isEmpty()) {
        alertTop_ = y;
        for (const Alert& a : alerts_) y += alertFullHeight(a) + 6;
        y += 4;
    }
    for (Item& it : items_) {
        it.depth = doc_->at(it.block).depth;
        it.textX = left + kGutter + it.depth * kIndent;
        it.textW = qMax(80, left + colW - it.textX);
        it.y = y;
        double h;
        if (it.block == active_) {
            editor_->document()->setTextWidth(it.textW);
            h = editor_->document()->size().height();
        } else {
            it.doc->setTextWidth(it.textW);
            h = it.doc->size().height();
        }
        it.h = qMax(minH, static_cast<int>(std::ceil(h)));
        y += it.h + 2 * kPadY;
    }

    refsTop_ = upcomingTop_ = bottomTop_ = -1;
    if (!refItems_.empty()) {
        const int titleH = QFontMetrics(Theme::instance().bodyFont()).lineSpacing();
        int section = -1;
        for (RefItem& it : refItems_) {
            if (it.section != section) {  // a section starts: air, then its label row
                section = it.section;
                y += 44;
                (section == 0 ? upcomingTop_ : refsTop_) = y;
                if (bottomTop_ < 0) bottomTop_ = y;
                y += 30;
            }
            it.textX = left + kGutter + (it.row < 0 ? 0 : kIndent / 2);
            it.textW = qMax(80, left + colW - it.textX);
            if (it.row < 0 && it.group > 0) y += 12;  // air between pages
            it.y = y;
            if (it.row < 0) {
                it.h = titleH;
            } else {
                it.doc->setTextWidth(it.textW);
                it.h = qMax(minH, static_cast<int>(std::ceil(it.doc->size().height())));
            }
            y += it.h + 2 * kPadY;
        }
    }
    contentHeight_ = y + kBottomMargin;
    verticalScrollBar()->setRange(0, qMax(0, contentHeight_ - viewport()->height()));
    verticalScrollBar()->setPageStep(viewport()->height());
    positionEditor();
    viewport()->update();
}

void OutlinerView::positionEditor() {
    const int k = (active_ >= 0) ? itemOf(active_) : -1;
    if (k < 0) {
        editor_->hide();
        return;
    }
    const Item& it = items_[static_cast<size_t>(k)];
    editor_->setGeometry(it.textX, it.y + kPadY - verticalScrollBar()->value(), it.textW, it.h);
    if (!editor_->isVisible()) editor_->show();
}

void OutlinerView::resizeEvent(QResizeEvent* e) {
    QAbstractScrollArea::resizeEvent(e);
    if (findBar_ && findBar_->isVisible()) {
        static_cast<FindBar*>(findBar_)->restyle();
        findBar_->move(viewport()->width() - findBar_->width() - 12, 10);
    }
    relayout();
}

void OutlinerView::scrollContentsBy(int, int) {
    positionEditor();
    updatePreview();
    viewport()->update();
}

void OutlinerView::loadEditor(int block) {
    const QString raw = QString::fromStdString(doc_->at(block).text);
    syncing_ = true;
    editor_->setFont(fontForText(raw));
    editor_->setPlainText(raw);
    editor_->document()->clearUndoRedoStacks();
    syncing_ = false;
    editBlock_ = block;
    editStartText_ = doc_->at(block).text;
}

void OutlinerView::pushUndo(const Document& before) {
    undo_.push_back(before.serialize());
    trimUndo(undo_);
    redo_.clear();
}

void OutlinerView::pushUndoPoint() {
    if (!doc_) return;
    commitTyping();
    pushUndo(*doc_);
}

// Typing in a block is undone by the text field while you are in it; once you leave (or do
// something structural) the whole edit becomes one step of the page's undo.
void OutlinerView::commitTyping() {
    if (!doc_ || editBlock_ < 0 || editBlock_ >= doc_->size()) return;
    if (doc_->at(editBlock_).text == editStartText_) return;
    Document before = *doc_;
    before.setText(editBlock_, editStartText_);
    pushUndo(before);
    editStartText_ = doc_->at(editBlock_).text;
}

void OutlinerView::restoreSnapshot(const std::string& to, std::vector<std::string>& other) {
    other.push_back(doc_->serialize());
    trimUndo(other);
    const int keep = active_ >= 0 ? active_ : lastActive_;
    active_ = -1;
    editor_->hide();
    doc_->restoreFrom(Document::parse(to));  // what was on the page, byte for byte
    doc_->ensureNonEmpty();
    editBlock_ = -1;
    clearSelection();
    rebuild();
    const int b = qBound(0, keep, doc_->size() - 1);
    if (itemOf(b) >= 0) focusBlock(b);
    emit edited();
}

bool OutlinerView::undo() {
    if (!doc_) return false;
    commitTyping();
    if (undo_.empty()) return false;
    const std::string to = undo_.back();
    undo_.pop_back();
    restoreSnapshot(to, redo_);
    return true;
}

bool OutlinerView::redo() {
    if (!doc_ || redo_.empty()) return false;
    commitTyping();
    const std::string to = redo_.back();
    redo_.pop_back();
    std::vector<std::string> keepRedo = std::move(redo_);
    restoreSnapshot(to, undo_);
    redo_ = std::move(keepRedo);  // restoring pushed onto undo_; the rest of redo stays
    return true;
}

void OutlinerView::focusBlock(int block, int cursorPos) {
    if (!doc_ || block < 0 || block >= doc_->size()) return;
    // Make sure the block is actually showing (a collapsed ancestor would hide it).
    if (itemOf(block) < 0) return;

    const int previous = active_;
    if (previous != block) commitTyping();
    if (block != 0 && revealCaption_) revealCaption_ = false;  // the caption goes back to the header
    clearSelection();
    active_ = block;
    lastActive_ = block;
    loadEditor(block);
    if (previous >= 0 && previous != block) rebuild();  // draw the old block as rich text again
    else relayout();

    QTextCursor c = editor_->textCursor();
    const int len = editor_->toPlainText().size();
    c.setPosition(cursorPos < 0 ? len : qMin(cursorPos, len));
    editor_->setTextCursor(c);
    editor_->setFocus();
    ensureCursorVisible();
    viewport()->update();
}

// ------------------------------------------------------------------- find

void OutlinerView::openFind() {
    auto* bar = static_cast<FindBar*>(findBar_);
    if (active_ >= 0) endEdit();  // every block rendered, so every match can be highlighted
    clearSelection();
    bar->restyle();
    bar->move(viewport()->width() - bar->width() - 12, 10);
    bar->show();
    bar->raise();
    bar->input()->setFocus();
    bar->input()->selectAll();
    if (!bar->input()->text().isEmpty()) setFindQuery(bar->input()->text());
}

void OutlinerView::closeFind() {
    findBar_->hide();
    findQuery_.clear();
    matches_.clear();
    findIndex_ = -1;
    rebuild();
    setFocus();
}

void OutlinerView::setFindQuery(const QString& q) {
    findQuery_ = q;
    findIndex_ = -1;
    if (active_ >= 0) endEdit();
    rebuild();  // fresh documents, highlighted by applyFind()
    if (!matches_.empty()) findStep(1);
}

void OutlinerView::applyFind() {
    matches_.clear();
    if (findQuery_.isEmpty()) return;
    for (size_t k = 0; k < items_.size(); ++k) {
        QTextDocument* d = items_[k].doc.get();
        for (QTextCursor c = d->find(findQuery_); !c.isNull(); c = d->find(findQuery_, c))
            matches_.push_back({static_cast<int>(k), c.selectionStart(), c.selectionEnd() - c.selectionStart()});
    }
    const Theme& th = Theme::instance();
    for (size_t i = 0; i < matches_.size(); ++i) {
        const Match& m = matches_[i];
        QTextCursor c(items_[static_cast<size_t>(m.item)].doc.get());
        c.setPosition(m.pos);
        c.setPosition(m.pos + m.len, QTextCursor::KeepAnchor);
        QTextCharFormat f;
        QColor bg = static_cast<int>(i) == findIndex_ ? th.accent() : th.structure();
        bg.setAlpha(static_cast<int>(i) == findIndex_ ? 150 : 70);
        f.setBackground(bg);
        c.mergeCharFormat(f);
    }
}

void OutlinerView::findStep(int delta) {
    if (matches_.empty()) return;
    const int n = static_cast<int>(matches_.size());
    const int next = findIndex_ < 0 ? (delta > 0 ? 0 : n - 1) : (findIndex_ + delta + n) % n;
    findIndex_ = next;
    rebuild();  // re-highlight with the new current match (rebuild runs applyFind)
    viewport()->update();
    if (matches_.empty()) return;
    // Scroll the current match into view (a third from the top) when it is off screen.
    const Match& m = matches_[static_cast<size_t>(qMin(findIndex_, n - 1))];
    const Item& it = items_[static_cast<size_t>(m.item)];
    QTextCursor c(it.doc.get());
    c.setPosition(m.pos);
    const QTextBlock tb = c.block();
    double y = it.y + kPadY;
    if (QTextLayout* lay = tb.layout(); lay && lay->lineCount() > 0) {
        const QTextLine line = lay->lineForTextPosition(m.pos - tb.position());
        y += lay->position().y() + (line.isValid() ? line.y() : 0);
    }
    QScrollBar* sb = verticalScrollBar();
    const int vh = viewport()->height();
    if (y < sb->value() + 40 || y > sb->value() + vh - 60) sb->setValue(static_cast<int>(y) - vh / 3);
    if (findBar_->isVisible()) findBar_->update();
}

// ------------------------------------------------------------ block selection

std::vector<int> OutlinerView::selectedBlocks() const {
    std::vector<int> out;
    if (selAnchor_ < 0 || items_.empty()) return out;
    const int a = qBound(0, qMin(selAnchor_, selHead_), static_cast<int>(items_.size()) - 1);
    const int b = qBound(0, qMax(selAnchor_, selHead_), static_cast<int>(items_.size()) - 1);
    for (int k = a; k <= b; ++k) out.push_back(items_[static_cast<size_t>(k)].block);
    return out;
}

std::vector<int> OutlinerView::selectionRoots() const {
    std::vector<int> roots;
    int covered = -1;
    for (int b : selectedBlocks()) {
        if (b < covered) continue;
        roots.push_back(b);
        covered = doc_->subtreeEnd(b);
    }
    return roots;
}

void OutlinerView::selectBlocks(int fromBlock, int toBlock) {
    const int a = itemOf(fromBlock), b = itemOf(toBlock);
    if (a < 0 || b < 0) return;
    if (active_ >= 0) endEdit();
    selAnchor_ = a;
    selHead_ = b;
    setFocus();
    viewport()->update();
}

void OutlinerView::clearSelection() {
    if (selAnchor_ < 0) return;
    selAnchor_ = selHead_ = -1;
    viewport()->update();
}

void OutlinerView::copySelection() {
    const std::vector<int> roots = selectionRoots();
    if (roots.empty()) return;
    QGuiApplication::clipboard()->setText(QString::fromStdString(doc_->exportBlocks(roots)));
}

void OutlinerView::deleteSelection() {
    const std::vector<int> sel = selectedBlocks();
    if (sel.empty()) return;
    pushUndoPoint();
    const int first = sel.front();
    doc_->removeBlocks(first, sel.back());
    clearSelection();
    rebuild();
    const int keep = qBound(0, first - 1, doc_->size() - 1);
    if (itemOf(keep) >= 0) selectBlocks(keep, keep);
    changed();
}

// Clipboard text with several bullets ("- a\n\t- b", what Logseq copies) becomes blocks.
bool OutlinerView::pasteBlocks(int afterBlock) {
    const QString text = QGuiApplication::clipboard()->text();
    if (!text.contains('\n')) return false;
    static const QRegularExpression bullet(R"(^\s*[-*] )", QRegularExpression::MultilineOption);
    if (text.count(bullet) < 2) return false;
    const Document clip = Document::parse(text.toStdString());
    if (clip.size() < 2) return false;
    pushUndoPoint();
    if (active_ >= 0) {
        active_ = -1;
        editor_->hide();
    }
    // An empty leaf is replaced by the paste (Logseq does the same).
    const bool replaces = afterBlock >= 0 && doc_->at(afterBlock).text.empty() && doc_->at(afterBlock).meta.empty() &&
                          !doc_->hasChildren(afterBlock);
    const int before = doc_->size();
    const int first = doc_->insertBlocks(afterBlock, clip);
    const int last = first + doc_->size() - before + (replaces ? 1 : 0) - 1;
    rebuild();
    focusBlock(last);
    changed();
    return true;
}

// Shift+Up on the first line (Shift+Down on the last) leaves the text and selects blocks.
void OutlinerView::extendFromEditor(bool up) {
    const int block = active_;
    const int k = itemOf(block);
    const int to = qBound(0, k + (up ? -1 : 1), static_cast<int>(items_.size()) - 1);
    endEdit();
    selAnchor_ = k;
    selHead_ = to;
    setFocus();
    viewport()->update();
}

bool OutlinerView::selectionKey(QKeyEvent* e) {
    if (selAnchor_ < 0 || !doc_) return false;
    const Qt::KeyboardModifiers m = e->modifiers() & ~Qt::KeypadModifier;
    const int n = static_cast<int>(items_.size());
    const bool shift = m == Qt::ShiftModifier, plain = m == Qt::NoModifier;
    if (e->matches(QKeySequence::Copy)) {
        copySelection();
        return true;
    }
    if (e->matches(QKeySequence::Cut)) {
        copySelection();
        deleteSelection();
        return true;
    }
    if (e->matches(QKeySequence::Paste)) {
        const std::vector<int> roots = selectionRoots();
        if (!roots.empty() && !pasteBlocks(roots.back())) {
            // plain text: a new block under the selection
            pushUndoPoint();
            const int nb = doc_->insertBelow(roots.back(), QGuiApplication::clipboard()->text().toStdString());
            rebuild();
            focusBlock(nb);
            changed();
        }
        return true;
    }
    if (e->matches(QKeySequence::SelectAll)) {
        selAnchor_ = 0;
        selHead_ = n - 1;
        viewport()->update();
        return true;
    }
    const int stepDir = progressStepKey(e);  // Alt+= / Alt+-, also the Mac's Option forms
    switch (stepDir ? static_cast<int>(Qt::Key_Equal) : e->key()) {
        case Qt::Key_Up:
        case Qt::Key_Down: {
            const int d = e->key() == Qt::Key_Up ? -1 : 1;
            if (m == (Qt::AltModifier | Qt::ShiftModifier)) {  // move the selected blocks
                std::vector<int> roots = selectionRoots();
                pushUndoPoint();
                bool moved = false;
                std::vector<int> now;
                if (d < 0) {
                    for (int r : roots) {
                        const int to = doc_->moveUp(r);
                        moved = moved || to >= 0;
                        now.push_back(to >= 0 ? to : r);
                        if (to < 0) break;  // the top one is stuck: the rest keep their order
                    }
                } else {
                    for (auto it = roots.rbegin(); it != roots.rend(); ++it) {
                        const int to = doc_->moveDown(*it);
                        moved = moved || to >= 0;
                        now.push_back(to >= 0 ? to : *it);
                        if (to < 0) break;
                    }
                }
                if (!moved) {
                    undo_.pop_back();
                    return true;
                }
                rebuild();
                const auto [lo, hi] = std::minmax_element(now.begin(), now.end());
                selAnchor_ = itemOf(*lo);
                selHead_ = itemOf(doc_->subtreeEnd(*hi) - 1) >= 0 ? itemOf(doc_->subtreeEnd(*hi) - 1) : itemOf(*hi);
                viewport()->update();
                changed();
                return true;
            }
            if (shift) selHead_ = qBound(0, selHead_ + d, n - 1);
            else if (plain) selAnchor_ = selHead_ = qBound(0, selHead_ + d, n - 1);
            else return false;
            ensureItemVisible(selHead_);  // the highlight never leaves the view
            viewport()->update();
            return true;
        }
        case Qt::Key_Tab:
        case Qt::Key_Backtab: {
            const bool out = e->key() == Qt::Key_Backtab;
            const std::vector<int> roots = selectionRoots();
            const Document before = *doc_;
            bool any = false;
            if (out) for (auto it = roots.rbegin(); it != roots.rend(); ++it) any = doc_->outdent(*it) || any;
            else for (int r : roots) any = doc_->indent(r) || any;
            if (!any) return true;
            pushUndo(before);
            const int a = selectedBlocks().front(), b = selectedBlocks().back();
            rebuild();
            selAnchor_ = itemOf(a);
            selHead_ = itemOf(b);
            viewport()->update();
            changed();
            return true;
        }
        case Qt::Key_Backspace:
        case Qt::Key_Delete:
            deleteSelection();
            return true;
        case Qt::Key_Return:
        case Qt::Key_Enter:
            if (m == Qt::ControlModifier) {  // cycle the task marker of every selected block
                pushUndoPoint();
                const std::vector<int> sel = selectedBlocks();
                for (int b : sel) doc_->setText(b, cycleTask(QString::fromStdString(doc_->at(b).text)).toStdString());
                runTaskSync(QList<int>(sel.begin(), sel.end()));
                rebuild();
                selAnchor_ = itemOf(sel.front());
                selHead_ = itemOf(sel.back());
                viewport()->update();
                emit edited();
                return true;
            }
            focusBlock(items_[static_cast<size_t>(qBound(0, selHead_, n - 1))].block);
            return true;
        case Qt::Key_Equal:
        case Qt::Key_Plus:
        case Qt::Key_Minus: {
            if (!(m & Qt::AltModifier)) return false;
            const std::vector<int> sel = selectedBlocks();
            QList<int> moved;
            const Document before = *doc_;
            for (int b : sel) {
                const QString t = QString::fromStdString(doc_->at(b).text);
                const QString after = stepProgress(t, stepDir ? stepDir : e->key() == Qt::Key_Minus ? -1 : 1, taskOpts_.finish, taskOpts_.start);
                if (after == t) continue;
                doc_->setText(b, after.toStdString());
                moved << b;
            }
            if (moved.isEmpty()) {
                emit message("No progress bar to move here: /progress adds one");
                return true;
            }
            pushUndo(before);
            runTaskSync(moved);
            rebuild();
            selAnchor_ = itemOf(sel.front());
            selHead_ = itemOf(sel.back());
            viewport()->update();
            if (moved.size() == 1) announceProgress(QString::fromStdString(doc_->at(moved.front()).text));
            emit edited();
            return true;
        }
        case Qt::Key_Escape:
            clearSelection();
            return true;
        default: return false;
    }
}

void OutlinerView::reloadBlocks() {
    endEdit();
    rebuild();
}

void OutlinerView::endEdit() {
    if (active_ < 0) return;
    revealCaption_ = false;
    commitTyping();
    editBlock_ = -1;
    active_ = -1;
    lastCascade_ = {};
    bool folded = false;
    for (int b : std::exchange(pendingFold_, {}))  // finished while you were inside them
        if (doc_ && b < doc_->size() && taskMarker(QString::fromStdString(doc_->at(b).text)) == "DONE" &&
            doc_->hasChildren(b) && !doc_->at(b).collapsed()) {
            doc_->setCollapsed(b, true);
            folded = true;
        }
    editor_->hide();
    preview_->hide();
    snippetPopup_->hide();
    if (doc_) rebuild();
    setFocus();
    if (folded) emit edited();
}

void OutlinerView::syncFromEditor() {
    if (syncing_ || active_ < 0 || !doc_) return;
    const QString t = editor_->toPlainText();
    const QString was = QString::fromStdString(doc_->at(active_).text);
    doc_->setText(active_, t.toStdString());
    const QFont want = fontForText(t);
    if (want != editor_->font()) editor_->setFont(want);  // heading prefix typed or removed
    if (taskMarker(t) != taskMarker(was)) {
        // The heading this task sits under (or its subtasks) may follow; one page undo step
        // covers your edit and theirs.
        Document before = *doc_;
        if (editBlock_ == active_) before.setText(active_, editStartText_);
        bool any = false;
        if (lastCascade_.heading == active_ && taskMarker(t) != "DONE") {  // undid a DONE: the subtasks come back
            for (const auto& [b, texts] : lastCascade_.blocks)
                if (b < doc_->size() && doc_->at(b).text == texts.second) {
                    doc_->setText(b, texts.first);
                    any = true;
                }
            if (any && doc_->at(active_).collapsed()) doc_->setCollapsed(active_, false);
            lastCascade_ = {};
        }
        const bool synced = runTaskSync({active_});
        if (any || synced) {
            pushUndo(before);
            editStartText_ = doc_->at(active_).text;
        }
        if (!synced && (any || taskProgress_)) rebuild();  // the count on the heading above
    }
    relayout();
    ensureCursorVisible();
    updatePreview();
    updateSnippets();
    emit edited();
}

void OutlinerView::countProgress() {
    rich_.progress.clear();
    rich_.eva = Theme::instance().immersion();
    if (!taskProgress_ || !doc_) return;
    QVector<int> depths;
    QStringList texts;
    bool any = false;
    for (int b = 0; b < doc_->size(); ++b) {
        depths << doc_->at(b).depth;
        texts << QString::fromStdString(doc_->at(b).text);
        any = any || !taskMarker(texts.back()).isEmpty();
    }
    if (any) rich_.progress = subtaskCounts(depths, texts);
}

void OutlinerView::setTaskOptions(const TaskSyncOptions& options, bool progress) {
    const bool redraw = progress != taskProgress_;
    taskOpts_ = options;
    taskProgress_ = progress;
    if (redraw && doc_) rebuild();
}

bool OutlinerView::runTaskSync(const QList<int>& userChanged) {
    if (!doc_ || (!taskOpts_.finish && !taskOpts_.start)) return false;
    TaskSyncOptions o = taskOpts_;
    o.fold = false;  // folded here: not around the block being edited
    std::vector<std::string> was;  // for undoing a cascade
    if (userChanged.size() == 1)
        for (int b = 0; b < doc_->size(); ++b) was.push_back(doc_->at(b).text);
    QList<int> finished;
    const QList<TaskEdit> edits = syncTasks(*doc_, userChanged, o, &finished);
    if (edits.isEmpty()) return false;
    for (const TaskEdit& e : edits) {
        if (e.kind == TaskEdit::Reopened && taskOpts_.fold && doc_->at(e.block).collapsed()) {
            doc_->setCollapsed(e.block, false);
            pendingFold_.removeAll(e.block);
        }
    }
    if (userChanged.size() == 1) {  // a heading you marked DONE: remember what it closed
        const int u = userChanged.front();
        Cascade c;
        c.heading = u;
        for (const TaskEdit& e : edits)
            if (e.kind == TaskEdit::Cascaded)
                c.blocks.append({e.block, {was[static_cast<size_t>(e.block)], e.text.toStdString()}});
        if (!c.blocks.isEmpty()) lastCascade_ = c;
    }
    QStringList names;
    for (int b : finished) {
        names << taskTitle(QString::fromStdString(doc_->at(b).text));
        if (!taskOpts_.fold || !doc_->hasChildren(b)) continue;
        if (active_ > b && active_ < doc_->subtreeEnd(b)) {
            if (!pendingFold_.contains(b)) pendingFold_.append(b);
        } else {
            doc_->setCollapsed(b, true);
        }
    }
    if (active_ >= 0) {  // rolled up itself (after a structural edit): keep the editor in step
        const QString now = QString::fromStdString(doc_->at(active_).text);
        if (now != editor_->toPlainText()) {
            const int pos = editor_->textCursor().position() + static_cast<int>(now.size() - editor_->toPlainText().size());
            syncing_ = true;
            editor_->setPlainText(now);
            QTextCursor c = editor_->textCursor();
            c.setPosition(qBound(0, pos, static_cast<int>(now.size())));
            editor_->setTextCursor(c);
            syncing_ = false;
            editStartText_ = doc_->at(active_).text;
        }
    }
    rebuild();
    if (!names.isEmpty()) emit tasksFinished(names);
    return true;
}

void OutlinerView::announceProgress(const QString& text) {
    const QString m = progressMessage(text, Theme::instance().immersion());
    if (!m.isEmpty()) emit message(m);
}

void OutlinerView::clearPage() {
    if (!doc_) return;
    endEdit();
    clearSelection();
    pushUndoPoint();
    doc_->removeBlocks(0, doc_->size() - 1);
    doc_->ensureNonEmpty();
    lastActive_ = 0;
    verticalScrollBar()->setValue(0);
    rebuild();
    setFocus();
    emit edited();
}

void OutlinerView::changed() {
    lastCascade_ = {};
    pendingFold_.clear();  // block numbers may have moved
    runTaskSync({});
    emit edited();
}

void OutlinerView::ensureCursorVisible() {
    if (active_ < 0) return;
    const int k = itemOf(active_);
    if (k < 0) return;
    const Item& it = items_[static_cast<size_t>(k)];
    const QRect cr = editor_->cursorRect();
    const int top = (k == 0) ? 0 : it.y + kPadY + cr.top() - 24;  // first block: show the page title too
    const int bottom = it.y + kPadY + cr.bottom() + 24;
    QScrollBar* sb = verticalScrollBar();
    if (top < sb->value()) sb->setValue(top);
    else if (bottom > sb->value() + viewport()->height()) sb->setValue(bottom - viewport()->height());
}

void OutlinerView::ensureItemVisible(int k) {
    if (k < 0 || k >= static_cast<int>(items_.size())) return;
    const Item& it = items_[static_cast<size_t>(k)];
    const int top = (k == 0) ? 0 : it.y - 24;  // the first block: the page title too
    const int bottom = it.y + it.h + 2 * kPadY + 24;
    QScrollBar* sb = verticalScrollBar();
    if (top < sb->value() || bottom - top > viewport()->height()) sb->setValue(top);
    else if (bottom > sb->value() + viewport()->height()) sb->setValue(bottom - viewport()->height());
}

QRect OutlinerView::selectionRect() const {
    if (selHead_ < 0 || selHead_ >= static_cast<int>(items_.size())) return QRect();
    const Item& it = items_[static_cast<size_t>(selHead_)];
    return QRect(0, it.y - verticalScrollBar()->value(), viewport()->width(), it.h + 2 * kPadY);
}

QPoint OutlinerView::toContent(const QPoint& p) const {
    return QPoint(p.x(), p.y() + verticalScrollBar()->value());
}

OutlinerView::Hit OutlinerView::hitTest(const QPoint& vp) const {
    Hit h;
    const QPoint p = toContent(vp);
    for (size_t k = 0; k < items_.size(); ++k) {
        const Item& it = items_[k];
        if (p.y() < it.y || p.y() >= it.y + it.h + 2 * kPadY) continue;
        h.item = static_cast<int>(k);
        if (p.x() < it.textX - kGutter + 12 && doc_ && doc_->hasChildren(it.block)) h.region = Region::Chevron;
        else if (p.x() >= it.textX - 6) h.region = Region::Text;
        else h.region = Region::None;
        return h;
    }
    return h;
}

// ---------------------------------------------------------------- painting

void OutlinerView::paintEvent(QPaintEvent*) {
    const Theme& th = Theme::instance();
    QPainter p(viewport());
    p.setCompositionMode(QPainter::CompositionMode_Source);  // replaces pixels, so alpha 0 really clears
    p.fillRect(viewport()->rect(), th.bg());
    p.setCompositionMode(QPainter::CompositionMode_SourceOver);
    if (!doc_) return;

    p.setRenderHint(QPainter::Antialiasing);
    const int sy = verticalScrollBar()->value();
    const int vh = viewport()->height();
    p.translate(0, -sy);
    const int n = static_cast<int>(items_.size());

    if (!headerTitle_.isEmpty()) {
        // Title strip:   [] KIND  |||||||||||||||||||||||||||||||||||||||  READOUT
        //                Title in a light weight, no box around it.
        int left, colW;
        columnGeometry(left, colW);
        const int x = left + kGutter;
        const int w = colW - kGutter;
        const int rowY = labelRowY();  // baseline of the label row
        const QFont label = th.labelFont();
        const QFontMetrics lm(label);

        QString readout;
        if (headerDate_.isValid()) {
            readout = QString("%1  \xC2\xB7  %2 / %3")
                          .arg(QLocale::c().toString(headerDate_, "ddd"))
                          .arg(headerDate_.dayOfYear(), 3, 10, QChar('0'))
                          .arg(headerDate_.daysInYear());
        } else {
            int blocks = 0;
            for (int i = 0; i < doc_->size(); ++i)
                if (!doc_->at(i).text.empty() && !captionBlock(i)) ++blocks;
            readout = QString("%1 block%2").arg(blocks).arg(blocks == 1 ? "" : "s");
        }

        p.setRenderHint(QPainter::Antialiasing, false);
        p.fillRect(QRect(x, rowY - 8, 6, 6), th.accent());
        p.setFont(label);
        p.setPen(th.dim());
        p.drawText(QPointF(x + 15, rowY), headerKind_);
        const int readoutW = lm.horizontalAdvance(readout);
        p.drawText(QPointF(x + w - readoutW, rowY), readout);

        // Hairline with ruler ticks between the two labels.
        const int lineX0 = x + 15 + lm.horizontalAdvance(headerKind_) + 16;
        const int lineX1 = x + w - readoutW - 16;
        if (lineX1 > lineX0) {
            const int ly = rowY - 5;
            p.setPen(QPen(th.line(), 1));
            p.drawLine(lineX0, ly, lineX1, ly);
            for (int tx = lineX0, i = 0; tx <= lineX1; tx += 12, ++i) p.drawLine(tx, ly, tx, ly + (i % 5 == 0 ? 5 : 2));
        }
        p.setRenderHint(QPainter::Antialiasing, true);

        p.setPen(th.text());
        p.setFont(th.titleFont());
        p.drawText(QPointF(x, titleBaseline()), QFontMetrics(th.titleFont()).elidedText(headerTitle_, Qt::ElideRight, w));

        // The caption, in the title's serif, under the title.
        const QRect cr = captionRect();
        if (!cr.isNull() && !(revealCaption_ || active_ == 0)) {
            p.setFont(captionFont());
            p.setPen(th.text());
            p.drawText(cr.translated(0, sy), Qt::AlignLeft | Qt::AlignTop,
                       QFontMetrics(captionFont()).elidedText(caption(), Qt::ElideRight, cr.width()));
        }
    }

    // Alert strips:  [////] PATTERN BLUE   2 targets · 1 overdue ............ tasks ›
    for (int a = 0; a < alerts_.size(); ++a) {
        const Alert& al = alerts_[a];
        const QRect r = alertRect(a).translated(0, sy);
        const QRect head(r.left(), r.top(), r.width(), alertHeight());
        QColor tint = al.color;
        tint.setAlpha(26);
        p.fillRect(r, tint);
        p.save();
        p.setClipRect(QRect(head.left(), head.top(), 30, head.height()));
        p.setPen(QPen(al.color, 3));
        for (int x = head.left() - head.height(); x < head.left() + 30; x += 8) p.drawLine(x, head.bottom(), x + head.height(), head.top());
        p.restore();
        if (r.height() > head.height()) p.fillRect(QRect(r.left(), head.bottom() + 1, 2, r.bottom() - head.bottom()), al.color);
        p.setFont(th.labelFont());
        p.setPen(al.color);
        const QFontMetrics lm(th.labelFont());
        p.drawText(head.adjusted(42, 0, 0, 0), Qt::AlignVCenter | Qt::AlignLeft, al.title);
        p.setFont(th.bodyFont());
        p.setPen(th.text());
        p.drawText(head.adjusted(42 + lm.horizontalAdvance(al.title.toUpper()) + 16, 0, 0, 0), Qt::AlignVCenter | Qt::AlignLeft,
                   al.detail);
        p.setFont(th.labelFont());
        p.setPen(th.dim());
        p.drawText(head.adjusted(0, 0, -12, 0), Qt::AlignVCenter | Qt::AlignRight, "Tasks \xE2\x80\xBA");

        // The targets:  ◆ Lab report ........ SYNC ▮▮▯▯▯ 2/5   T−03H 12M
        //                 ├─ plot the curves                 SUN 25 OCT
        const QDateTime now = QDateTime::currentDateTime();
        const QFontMetrics bm(th.bodyFont());
        const QFont mono = th.monoFont();
        const QFontMetrics mm(mono);
        const int rh = alertRowHeight();
        for (int k = 0; k < al.rows.size() + (al.more > 0 ? 1 : 0); ++k) {
            const QRect row(r.left() + 42, head.bottom() + 1 + k * rh, r.width() - 42 - 12, rh);
            if (k == al.rows.size()) {  // "+ 3 more"
                p.setFont(th.labelFont());
                p.setPen(th.faint());
                p.drawText(row, Qt::AlignVCenter | Qt::AlignLeft, QString("+ %1 more").arg(al.more));
                break;
            }
            const AlertRow& ar = al.rows[k];
            int x = row.left() + (ar.level > 0 ? (ar.level - 1) * 18 : 0);
            const int cy = row.center().y();
            if (ar.level == 0) {  // a target: a lit diamond
                QPolygonF dia({QPointF(x + 4, cy - 4), QPointF(x + 8, cy), QPointF(x + 4, cy + 4), QPointF(x, cy)});
                p.setRenderHint(QPainter::Antialiasing);
                p.setPen(Qt::NoPen);
                p.setBrush(al.color);
                p.drawPolygon(dia);
                p.setRenderHint(QPainter::Antialiasing, false);
            } else {  // a subtask: hangs off the one above
                const bool last = k + 1 >= al.rows.size() || al.rows[k + 1].level < ar.level;
                p.setPen(QPen(th.faint(), 1));
                p.drawLine(x + 4, row.top(), x + 4, last ? cy : row.bottom());
                p.drawLine(x + 4, cy, x + 12, cy);
            }
            x += ar.level == 0 ? 16 : 18;
            int right = row.right();
            // On the right: the countdown, or the date.
            if (ar.at.isValid()) {
                const qint64 left = now.secsTo(ar.at);
                const QColor tone = left < 3600 ? QColor(0xff, 0x4d, 0x4d) : QColor(0xff, 0xb0, 0x00);
                QFont f = mono;
                f.setBold(true);
                const QString cd = countdownText(ar.at, now);
                const int w = QFontMetrics(f).horizontalAdvance(cd) + 10;
                QColor bg = tone;
                bg.setAlpha(36);
                const QRect chip(right - w, cy - mm.height() / 2 - 1, w, mm.height() + 2);
                p.fillRect(chip, bg);
                p.setFont(f);
                p.setPen(tone);
                p.drawText(chip, Qt::AlignCenter, cd);
                right -= w + 12;
            } else if (ar.due.isValid()) {
                p.setFont(th.labelFont());
                p.setPen(ar.due <= now.date() ? al.color : th.dim());
                const QString ds = QLocale::c().toString(ar.due, "ddd d MMM").toUpper();
                p.drawText(QRect(x, row.top(), right - x, rh), Qt::AlignVCenter | Qt::AlignRight, ds);
                right -= lm.horizontalAdvance(ds) + 14;
            }
            // A target with subtasks: its sync ratio, five cells and the count.
            if (ar.subTotal > 0) {
                p.setFont(th.labelFont());
                const QString count = QString("%1/%2").arg(ar.subDone).arg(ar.subTotal);
                p.setPen(th.dim());
                p.drawText(QRect(x, row.top(), right - x, rh), Qt::AlignVCenter | Qt::AlignRight, count);
                right -= lm.horizontalAdvance(count) + 8;
                const int cells = 5, cw = 6, gap = 2;
                const int lit = static_cast<int>(std::round(cells * double(ar.subDone) / ar.subTotal));
                for (int c = 0; c < cells; ++c) {
                    const QRect cell(right - (cells - c) * (cw + gap), cy - 4, cw, 9);
                    if (c < lit) p.fillRect(cell, al.color);
                    else {
                        p.setPen(QPen(th.faint(), 1));
                        p.setBrush(Qt::NoBrush);
                        p.drawRect(cell.adjusted(0, 0, -1, -1));
                    }
                }
                right -= cells * (cw + gap) + 6;
                p.setPen(th.faint());
                p.drawText(QRect(x, row.top(), right - x, rh), Qt::AlignVCenter | Qt::AlignRight, "SYNC");
                right -= lm.horizontalAdvance("SYNC") + 12;
            }
            p.setFont(th.bodyFont());
            p.setPen(ar.level == 0 ? th.text() : th.dim());
            p.drawText(QRect(x, row.top(), right - x, rh), Qt::AlignVCenter | Qt::AlignLeft,
                       bm.elidedText(ar.text, Qt::ElideRight, qMax(0, right - x)));
        }
    }

    // Nesting guides: a thin line under each expanded parent's bullet.
    p.setPen(QPen(th.line(), 1));
    for (int i = 0; i < n; ++i) {
        const Item& it = items_[static_cast<size_t>(i)];
        if (!doc_->hasChildren(it.block) || doc_->at(it.block).collapsed()) continue;
        int j = i + 1;
        while (j < n && items_[static_cast<size_t>(j)].depth > it.depth) ++j;
        if (j == i + 1) continue;
        const Item& last = items_[static_cast<size_t>(j - 1)];
        const int x = it.textX - 11;
        const int y0 = it.y + kPadY + 14;
        const int y1 = last.y + kPadY + last.h - 2;
        if (y1 < sy || y0 > sy + vh) continue;
        p.drawLine(x, y0, x, y1);
    }

    // Today's timeline, in the margin left of the outline:  ·  past   ●  now   ○  later
    if (timeline_) {
        struct Mark { double y; TimeSlot slot; };
        QList<Mark> marks;
        for (int i = 0; i < n; ++i) {
            const Item& it = items_[static_cast<size_t>(i)];
            TimeSlot slot;
            if (!blockTime(QString::fromStdString(doc_->at(it.block).text), &slot)) continue;
            double mid = 10;
            if (QTextLayout* lay = it.doc->firstBlock().layout(); lay && lay->lineCount() > 0) mid = lay->lineAt(0).rect().center().y();
            marks.append({it.y + kPadY + mid, slot});
        }
        if (!marks.isEmpty()) {
            int left, colW;
            columnGeometry(left, colW);
            const double x = left - 10;
            const int now = QTime::currentTime().msecsSinceStartOfDay() / 60000;
            p.setPen(QPen(th.line(), 1));
            if (marks.size() > 1) p.drawLine(QPointF(x, marks.front().y), QPointF(x, marks.back().y));
            // "Now": on a block that is running, else between the last one begun and the next.
            int running = -1, after = -1;  // after: the number of blocks begun, in page order
            for (int k = 0; k < marks.size(); ++k) {
                const TimeSlot& s = marks[k].slot;
                if (s.start <= now && s.end > now) running = k;
                if (s.start <= now) after = k;
            }
            for (int k = 0; k < marks.size(); ++k) {
                const TimeSlot& s = marks[k].slot;
                const QPointF at(x, marks[k].y);
                if (k == running) {
                    p.setPen(Qt::NoPen);
                    p.setBrush(th.accent());
                    p.drawEllipse(at, 3.5, 3.5);
                } else if (s.start <= now || s.done) {
                    p.setPen(Qt::NoPen);
                    p.setBrush(th.faint());
                    p.drawEllipse(at, 2.0, 2.0);
                } else {
                    p.setPen(QPen(th.structure(), 1.2));
                    p.setBrush(th.bg());
                    p.drawEllipse(at, 3.0, 3.0);
                }
            }
            if (running < 0) {
                const double ny = after < 0 ? marks.front().y - 12
                                : after == marks.size() - 1 ? marks.back().y + 12
                                                            : (marks[after].y + marks[after + 1].y) / 2;
                QPainterPath tri;  // a small pointer at the margin's edge
                tri.moveTo(x - 6, ny - 3.5);
                tri.lineTo(x - 0.5, ny);
                tri.lineTo(x - 6, ny + 3.5);
                tri.closeSubpath();
                p.setPen(Qt::NoPen);
                p.setBrush(th.accent());
                p.drawPath(tri);
                p.setPen(QPen(th.accent(), 1));
                p.drawLine(QPointF(x + 1, ny), QPointF(x + 5, ny));
            }
        }
    }

    // Selected blocks: a tinted band with an accent edge.
    if (selAnchor_ >= 0 && n > 0) {
        const int a = qBound(0, qMin(selAnchor_, selHead_), n - 1), b = qBound(0, qMax(selAnchor_, selHead_), n - 1);
        int left, colW;
        columnGeometry(left, colW);
        QColor band = th.structure();
        band.setAlpha(40);
        int x = items_[static_cast<size_t>(a)].textX;
        for (int k = a; k <= b; ++k) x = qMin(x, items_[static_cast<size_t>(k)].textX);
        const Item& first = items_[static_cast<size_t>(a)];
        const Item& last = items_[static_cast<size_t>(b)];
        const QRect r(x - 20, first.y, left + colW - x + 24, last.y + last.h + 2 * kPadY - first.y);  // one band
        p.fillRect(r, band);
        p.fillRect(QRect(r.left(), r.top(), 2, r.height()), th.accent());
    }

    for (int i = 0; i < n; ++i) {
        const Item& it = items_[static_cast<size_t>(i)];
        const int top = it.y;
        const int bottom = it.y + it.h + 2 * kPadY;
        if (bottom < sy || top > sy + vh) continue;

        const bool isActive = (it.block == active_);
        const bool hover = (it.block == hoverBlock_);
        const bool hasKids = doc_->hasChildren(it.block);
        const bool collapsed = hasKids && doc_->at(it.block).collapsed();

        // Vertical centre of the first line, for aligning the bullet.
        double lineMid = 10;
        if (isActive) {
            lineMid = editor_->cursorRect(QTextCursor(editor_->document())).center().y();
        } else if (QTextLayout* lay = it.doc->firstBlock().layout(); lay && lay->lineCount() > 0) {
            lineMid = lay->lineAt(0).rect().center().y();
        }
        const double by = it.y + kPadY + lineMid;
        const double bx = it.textX - 11;

        // A bullet-less first block holding the page's description:: reads as a caption: no bullet.
        const bool caption = !doc_->at(it.block).bullet && !isActive &&
                             doc_->at(it.block).text.rfind("description:: ", 0) == 0;
        // Bullet
        if (caption) {
        } else if (collapsed) {
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(th.structure().red(), th.structure().green(), th.structure().blue(), 60));
            p.drawEllipse(QPointF(bx, by), 7.0, 7.0);
        }
        if (!caption) {
            p.setPen(Qt::NoPen);
            p.setBrush(isActive ? th.accent() : th.structure());
            p.drawEllipse(QPointF(bx, by), 3.0, 3.0);
        }

        // Chevron, only for blocks with children, brighter on hover
        if (hasKids) {
            const double cx = it.textX - kGutter + 5;
            QPainterPath tri;
            if (collapsed) {
                tri.moveTo(cx - 2.5, by - 4);
                tri.lineTo(cx + 2.5, by);
                tri.lineTo(cx - 2.5, by + 4);
            } else {
                tri.moveTo(cx - 4, by - 2);
                tri.lineTo(cx + 4, by - 2);
                tri.lineTo(cx, by + 3);
            }
            tri.closeSubpath();
            p.setBrush(hover ? th.dim() : th.faint());
            p.drawPath(tri);
        }

        // Text (the block being edited is drawn by the editor instead)
        if (!isActive) {
            p.save();
            p.translate(it.textX, it.y + kPadY);
            it.doc->drawContents(&p, QRectF(0, 0, it.textW, it.h));
            p.restore();
        }
    }

    paintReferences(p, sy, vh);

    // Faint prompt when the page is empty
    if (n == 1 && active_ < 0 && doc_->at(0).text.empty()) {
        p.setPen(th.faint());
        p.setFont(th.bodyFont());
        const Item& it = items_[0];
        p.drawText(QRect(it.textX, it.y + kPadY, it.textW, it.h), Qt::AlignLeft | Qt::AlignTop,
                   "Click here to start writing");
    }
}

void OutlinerView::paintReferences(QPainter& p, int sy, int vh) {
    if (bottomTop_ < 0) return;
    const Theme& th = Theme::instance();
    int left, colW;
    columnGeometry(left, colW);
    const int x = left + kGutter;
    const int w = colW - kGutter;

    // Section label rows, in the header's language:  [] LINKED REFERENCES ---------- 3
    int upcomingCount = 0;
    for (const RefGroup& g : upcoming_) upcomingCount += static_cast<int>(g.blocks.size());
    for (int section : {0, 1}) {
        const int top = section == 0 ? upcomingTop_ : refsTop_;
        if (top < 0 || top + 30 < sy || top > sy + vh) continue;
        const QFont label = th.labelFont();
        const QFontMetrics lm(label);
        const int rowY = top + 16;
        const QString kind = section == 0 ? upcomingLabel_ : QString("Linked references");
        const QString count = QString::number(section == 0 ? upcomingCount : refTotal_);
        p.setRenderHint(QPainter::Antialiasing, false);
        p.fillRect(QRect(x, rowY - 8, 6, 6), section == 0 ? th.accent() : th.structure());
        p.setFont(label);
        p.setPen(th.dim());
        p.drawText(QPointF(x + 15, rowY), kind);
        const int countW = lm.horizontalAdvance(count);
        p.drawText(QPointF(x + w - countW, rowY), count);
        const int x0 = x + 15 + lm.horizontalAdvance(kind.toUpper()) + 20;
        const int x1 = x + w - countW - 16;
        if (x1 > x0) {
            p.setPen(QPen(th.line(), 1));
            p.drawLine(x0, rowY - 5, x1, rowY - 5);
        }
        p.setRenderHint(QPainter::Antialiasing, true);
    }

    QFont titleFont = th.bodyFont();
    titleFont.setWeight(QFont::DemiBold);
    for (size_t k = 0; k < refItems_.size(); ++k) {
        const RefItem& it = refItems_[k];
        if (it.y + it.h + 2 * kPadY < sy || it.y > sy + vh) continue;
        const bool hot = static_cast<int>(k) == hoverRef_;
        if (hot) {
            QColor tint = th.structure();
            tint.setAlpha(22);
            p.setPen(Qt::NoPen);
            p.setBrush(tint);
            p.drawRoundedRect(QRectF(it.textX - 18, it.y, it.textW + 24, it.h + 2 * kPadY), 3, 3);
        }
        if (it.row < 0) {
            p.setFont(titleFont);
            p.setPen(hot ? th.accent() : th.structure());
            p.drawText(QRect(it.textX, it.y + kPadY, it.textW, it.h), Qt::AlignLeft | Qt::AlignVCenter,
                       groupsOf(it.section)[it.group].title);
            continue;
        }
        double lineMid = 10;
        if (QTextLayout* lay = it.doc->firstBlock().layout(); lay && lay->lineCount() > 0)
            lineMid = lay->lineAt(0).rect().center().y();
        p.setPen(QPen(th.faint(), 1.2));
        p.setBrush(Qt::NoBrush);
        p.drawEllipse(QPointF(it.textX - 11, it.y + kPadY + lineMid), 2.6, 2.6);
        p.save();
        p.translate(it.textX, it.y + kPadY);
        it.doc->drawContents(&p, QRectF(0, 0, it.textW, it.h));
        p.restore();
    }
}

// ------------------------------------------------------------------- mouse

void OutlinerView::mousePressEvent(QMouseEvent* e) {
    if (e->button() != Qt::LeftButton || !doc_) return;
    const QPoint cp = toContent(e->position().toPoint());
    if (bottomTop_ >= 0 && cp.y() >= bottomTop_) {
        const int k = refAt(cp);
        if (k < 0) return;
        const RefItem& it = refItems_[static_cast<size_t>(k)];
        const RefGroup& g = groupsOf(it.section)[it.group];
        if (it.row >= 0) {
            const QString href = it.doc->documentLayout()->anchorAt(QPointF(cp.x() - it.textX, cp.y() - (it.y + kPadY)));
            if (!href.isEmpty()) {
                emit linkActivated(href);
                return;
            }
        }
        emit referenceActivated(g.path, it.row < 0 ? -1 : g.blocks[it.row].first);
        return;
    }
    if (const QRect cr = captionRect(); !cr.isNull() && cr.adjusted(-6, -4, 6, 4).contains(e->position().toPoint())) {
        revealCaption_ = true;  // edit it as the block it is; it returns to the header after
        rebuild();
        focusBlock(0);
        return;
    }
    int alertNo = -1;
    const int alertRow = alertRowAt(e->position().toPoint(), &alertNo);
    if (alertRow >= 0) {
        const AlertRow& r = alerts_[alertNo].rows[alertRow];
        emit taskActivated(r.path, r.block);
        return;
    }
    bool onAlert = false;
    for (int a = 0; a < alerts_.size(); ++a) onAlert = onAlert || alertRect(a).contains(e->position().toPoint());
    if (onAlert) {
        emit alertActivated();
        return;
    }
    const Hit h = hitTest(e->position().toPoint());

    if (h.item < 0) {  // empty space below the last block
        if (!items_.empty()) focusBlock(items_.back().block);
        return;
    }
    const Item& it = items_[static_cast<size_t>(h.item)];

    if (h.region == Region::Chevron) {
        pushUndoPoint();
        doc_->setCollapsed(it.block, !doc_->at(it.block).collapsed());
        rebuild();
        emit edited();
        return;
    }
    if (h.region != Region::Text) return;

    // Shift+click: select the blocks from the one being edited (or selected) to this one.
    if (e->modifiers() & Qt::ShiftModifier) {
        const int from = selAnchor_ >= 0 ? items_[static_cast<size_t>(selAnchor_)].block : active_ >= 0 ? active_ : it.block;
        selectBlocks(from, it.block);
        return;
    }
    pressItem_ = h.item;

    const QPoint c = toContent(e->position().toPoint());
    const QString href = it.doc->documentLayout()->anchorAt(QPointF(c.x() - it.textX, c.y() - (it.y + kPadY)));
    if (href.startsWith("eq:")) {  // [[eq:x]]: the formula on this page
        goToEquation(href.mid(3));
        return;
    }
    if (!href.isEmpty()) {
        emit linkActivated(href);
        return;
    }
    const int block = it.block;
    const QPoint click = e->position().toPoint();
    focusBlock(block, 0);
    editor_->setTextCursor(editor_->cursorForPosition(editor_->mapFrom(viewport(), click)));
    pressCursor_ = editor_->textCursor().position();
}

void OutlinerView::mouseReleaseEvent(QMouseEvent* e) {
    pressItem_ = -1;
    QAbstractScrollArea::mouseReleaseEvent(e);
}

void OutlinerView::mouseMoveEvent(QMouseEvent* e) {
    // Dragging: inside the pressed block it selects text, across blocks it selects blocks.
    if ((e->buttons() & Qt::LeftButton) && pressItem_ >= 0 && pressItem_ < static_cast<int>(items_.size())) {
        const Hit dh = hitTest(e->position().toPoint());
        if (dh.item >= 0 && dh.item != pressItem_) {
            if (active_ >= 0) endEdit();
            selAnchor_ = pressItem_;
            selHead_ = dh.item;
            setFocus();
            viewport()->update();
            return;
        }
        if (dh.item == pressItem_ && active_ >= 0 && pressCursor_ >= 0) {
            QTextCursor c = editor_->cursorForPosition(editor_->mapFrom(viewport(), e->position().toPoint()));
            const int to = c.position();
            c.setPosition(pressCursor_);
            c.setPosition(to, QTextCursor::KeepAnchor);
            editor_->setTextCursor(c);
            return;
        }
    }
    const QPoint cp = toContent(e->position().toPoint());
    const int ref = (bottomTop_ >= 0 && cp.y() >= bottomTop_) ? refAt(cp) : -1;
    if (ref != hoverRef_) {
        hoverRef_ = ref;
        viewport()->update();
    }
    if (bottomTop_ >= 0 && cp.y() >= bottomTop_) {
        if (hoverBlock_ != -1) {
            hoverBlock_ = -1;
            viewport()->update();
        }
        viewport()->setCursor(ref >= 0 ? Qt::PointingHandCursor : Qt::ArrowCursor);
        return;
    }
    const Hit h = hitTest(e->position().toPoint());
    const int block = (h.item >= 0) ? items_[static_cast<size_t>(h.item)].block : -1;
    if (block != hoverBlock_) {
        hoverBlock_ = block;
        viewport()->update();
    }
    bool pointer = (h.region == Region::Chevron);
    if (h.region == Region::Text && h.item >= 0) {
        const Item& it = items_[static_cast<size_t>(h.item)];
        const QPoint c = toContent(e->position().toPoint());
        pointer = !it.doc->documentLayout()->anchorAt(QPointF(c.x() - it.textX, c.y() - (it.y + kPadY))).isEmpty();
    }
    viewport()->setCursor(pointer ? Qt::PointingHandCursor : (h.region == Region::Text ? Qt::IBeamCursor : Qt::ArrowCursor));

    // Hovering a typeset \ce formula shows its molar masses (and how it balances, what is
    // oxidised); hovering a defined term shows its definition.
    const QString tex = formulaAt(e->position().toPoint());
    hoverFormula_ = tex;
    const QString tip = !tex.isEmpty() ? chemNotes(tex) : definitionAt(e->position().toPoint());
    if (tip != hoverTip_) {
        hoverTip_ = tip;
        if (tip.isEmpty()) QToolTip::hideText();
        else QToolTip::showText(e->globalPosition().toPoint() + QPoint(12, 16), tip, viewport());
    }
}

void OutlinerView::leaveEvent(QEvent* e) {
    QAbstractScrollArea::leaveEvent(e);
    if (hoverBlock_ != -1 || hoverRef_ != -1) {
        hoverBlock_ = -1;
        hoverRef_ = -1;
        viewport()->update();
    }
}

// --------------------------------------------------------------- keyboard

void OutlinerView::keyPressEvent(QKeyEvent* e) {
    if (doc_ && e->matches(QKeySequence::Undo)) {
        undo();
        return;
    }
    if (doc_ && (e->matches(QKeySequence::Redo) || e->keyCombination() == QKeyCombination(Qt::ControlModifier, Qt::Key_Y))) {
        redo();
        return;
    }
    if (selectionKey(e)) return;
    // The view has focus (not the editor) and nothing is selected: arrows or Enter resume editing.
    if (doc_ && active_ < 0 && !items_.empty()) {
        switch (e->key()) {
            case Qt::Key_Up:
            case Qt::Key_Down:
            case Qt::Key_Return:
            case Qt::Key_Enter: {
                int b = qMin(lastActive_, doc_->size() - 1);
                if (itemOf(b) < 0) b = items_.front().block;
                focusBlock(b);
                return;
            }
            default: break;
        }
    }
    QAbstractScrollArea::keyPressEvent(e);
}

QList<QPair<QString, QString>> OutlinerView::keyHelp() {
    return {
        {"Enter", "Split block"},
        {"Shift+Enter", "New line in the block"},
        {"Ctrl+Enter", "TODO \xE2\x86\x92 DOING \xE2\x86\x92 DONE"},
        {"Ctrl+B / Ctrl+I", "Bold / italics (around a selection)"},
        {"Alt+= / Alt+\xE2\x88\x92", "Progress bar: a step forward / back (/progress adds one)"},
        {"Tab", "Indent"},
        {"Shift+Tab", "Outdent"},
        {"Alt+Shift+↑ ↓", "Move block with children"},
        {"Ctrl+↑ ↓", "Fold / unfold children"},
        {"↑ ↓", "Previous / next block"},
        {"Backspace", "At the start: join with block above"},
        {"( [ { `", "Typed in pairs; wraps a selection"},
        {"; \\ce", "Snippets: ;dH ;eq ;alpha \xE2\x80\xA6  Tab inserts"},
        {"= \xE2\x80\xA6", "Quick maths with units: = 0.5 mol * M(NaCl)"},
        {"/balance", "Balance the \\ce{} reaction: Fe + O2 -> Fe2O3"},
        {"/table  =plot{y vs x}", "Lab table with worked-out columns; plot it with a fit"},
        {"/graph  =graph{f(x), x from a to b}", "Graph of a function: roots, turning points, asymptotes, units"},
        {"$$ \xE2\x80\xA6 $$ {#name}", "Numbered formula; [[eq:name]] refers to it"},
        {"Term:: meaning", "A definition: the term is dotted everywhere, hover it"},
        {"/", "Commands: /h2 /todo /chem /lecture \xE2\x80\xA6"},
        {"/timer 20m 15s", "Operation timer (/timer stop ends it)"},
        {"Esc", "Stop editing (the block stays selected)"},
        {"Shift+\xE2\x86\x91 \xE2\x86\x93", "Select blocks (also Shift+click, drag)"},
        {"Ctrl+C / X / V", "Copy, cut, paste selected blocks"},
        {"Ctrl+Z", "Undo, across blocks (Ctrl+Shift+Z redo)"},
    };
}

namespace {
QChar closerFor(QChar open) {
    switch (open.unicode()) {
        case '(': return ')';
        case '[': return ']';
        case '{': return '}';
        case '`': return '`';
        default: return QChar();
    }
}
}  // namespace

// Bracket pairing. "(" gives "()" with the cursor inside, unless text follows directly;
// with a selection it wraps it, so selecting a word and typing "[" twice links it. Typing
// the closer that is already next to the cursor steps over it.
bool OutlinerView::pairBrackets(const QString& typed) {
    if (typed.size() != 1) return false;
    const QChar ch = typed.front();
    QTextCursor c = editor_->textCursor();
    const QString t = editor_->toPlainText();
    const QChar next = c.position() < t.size() ? t.at(c.position()) : QChar();

    const bool isCloser = ch == ')' || ch == ']' || ch == '}' || ch == '`';
    if (isCloser && !c.hasSelection() && next == ch) {
        c.movePosition(QTextCursor::NextCharacter);
        editor_->setTextCursor(c);
        return true;
    }
    const QChar close = closerFor(ch);
    if (close.isNull()) return false;

    if (c.hasSelection()) {
        const int start = c.selectionStart(), end = c.selectionEnd();
        c.beginEditBlock();  // one undo step
        c.setPosition(end);
        c.insertText(QString(close));
        c.setPosition(start);
        c.insertText(QString(ch));
        c.endEditBlock();
        c.setPosition(start + 1);
        c.setPosition(end + 1, QTextCursor::KeepAnchor);
        editor_->setTextCursor(c);
        return true;
    }
    // Pair only where nothing is in the way: end of text, a space, or a closer/punctuation.
    if (!next.isNull() && !next.isSpace() && !QStringLiteral(")]}.,;:!?$").contains(next)) return false;
    // A backtick after a word is left alone, and so is the third of a ``` fence.
    const QChar prev = c.position() > 0 ? t.at(c.position() - 1) : QChar();
    if (ch == '`' && (prev.isLetterOrNumber() || prev == '`')) return false;
    c.beginEditBlock();
    c.insertText(QString(ch) + close);
    c.endEditBlock();
    c.movePosition(QTextCursor::PreviousCharacter);
    editor_->setTextCursor(c);
    return true;
}

// Backspace between an empty pair, "(|)", removes both halves.
bool OutlinerView::deletePair() {
    QTextCursor c = editor_->textCursor();
    const QString t = editor_->toPlainText();
    const int pos = c.position();
    if (c.hasSelection() || pos == 0 || pos >= t.size()) return false;
    const QChar close = closerFor(t.at(pos - 1));
    if (close.isNull() || t.at(pos) != close) return false;
    c.setPosition(pos - 1);
    c.setPosition(pos + 1, QTextCursor::KeepAnchor);
    c.removeSelectedText();
    editor_->setTextCursor(c);
    return true;
}

bool OutlinerView::exportPdf(const QString& file, const QString& title, const QString& kind, const QDate& date) const {
    if (!doc_) return false;
    const Theme::PrintScope print;  // white paper for the rest of this function
    const Theme& th = Theme::instance();

    QPdfWriter pdf(file);
    pdf.setPageSize(QPageSize(QPageSize::A4));
    pdf.setPageMargins(QMarginsF(18, 18, 18, 14), QPageLayout::Millimeter);
    pdf.setResolution(96);  // 1 unit = 1 screen pixel, so the layout matches the window
    pdf.setTitle(title);
    pdf.setCreator("Graphite");
    const QRect area = pdf.pageLayout().paintRectPixels(96);
    const int W = area.width(), H = area.height();
    constexpr int kFooter = 28, kHeader = 92, kIndentPx = 22, kGutterPx = 16;
    constexpr qreal kDpr = 3.0;  // formulas rasterised at ~300 dpi

    // Pass 1: lay out every non-empty block and assign pages.
    struct Row {
        int depth;
        int page;
        int y;
        int h;
        std::unique_ptr<QTextDocument> doc;
    };
    std::vector<Row> rows;
    int page = 0, y = kHeader;
    for (int i = 0; i < doc_->size(); ++i) {
        const QString text = QString::fromStdString(doc_->at(i).text);
        if (text.trimmed().isEmpty()) continue;
        Row r;
        r.depth = doc_->at(i).depth;
        r.doc = std::make_unique<QTextDocument>();
        RichContext ctx = rich_;
        ctx.block = i;
        buildRich(r.doc.get(), text, th, kDpr, pageDir_, &ctx);
        r.doc->setTextWidth(W - kGutterPx - r.depth * kIndentPx);
        r.h = static_cast<int>(std::ceil(r.doc->size().height()));
        if (y + r.h > H - kFooter && y > (page == 0 ? kHeader : 0)) {
            ++page;
            y = 0;
        }
        r.page = page;
        r.y = y;
        y += r.h + 8;
        rows.push_back(std::move(r));
    }
    const int pages = page + 1;

    // Pass 2: paint.
    QPainter p;
    if (!p.begin(&pdf)) return false;
    p.setRenderHint(QPainter::Antialiasing);
    size_t k = 0;
    for (int pg = 0; pg < pages; ++pg) {
        if (pg > 0) pdf.newPage();
        if (pg == 0) {  // header: [] KIND ------------------------ readout / Title
            const QFont label = th.labelFont();
            p.fillRect(QRect(0, 12, 6, 6), th.accent());
            p.setFont(label);
            p.setPen(th.dim());
            p.drawText(QPointF(15, 20), kind);
            const QString readout = date.isValid() ? QLocale::c().toString(date, "dddd d MMMM yyyy") : QString();
            const int rw = QFontMetrics(label).horizontalAdvance(readout.toUpper());
            p.drawText(QPointF(W - rw, 20), readout);
            p.setPen(QPen(th.line(), 1));
            p.drawLine(15 + QFontMetrics(label).horizontalAdvance(kind.toUpper()) + 16, 15, W - rw - 16, 15);
            p.setPen(th.text());
            p.setFont(th.titleFont());
            p.drawText(QPointF(0, 66), QFontMetrics(th.titleFont()).elidedText(title, Qt::ElideRight, W));
            p.fillRect(QRect(0, 78, 44, 3), th.accent());
        }
        for (; k < rows.size() && rows[k].page == pg; ++k) {
            const Row& r = rows[k];
            const int x = kGutterPx + r.depth * kIndentPx;
            double mid = 9;
            if (QTextLayout* lay = r.doc->firstBlock().layout(); lay && lay->lineCount() > 0)
                mid = lay->lineAt(0).rect().center().y();
            p.setPen(Qt::NoPen);
            p.setBrush(th.structure());
            p.drawEllipse(QPointF(x - 10, r.y + mid), 2.6, 2.6);
            p.save();
            p.translate(x, r.y);
            r.doc->drawContents(&p, QRectF(0, 0, W - x, r.h));
            p.restore();
        }
        // footer: title · page n / N
        p.setFont(th.labelFont());
        p.setPen(th.faint());
        p.drawText(QRect(0, H - 14, W, 14), Qt::AlignLeft | Qt::AlignBottom, title);
        p.drawText(QRect(0, H - 14, W, 14), Qt::AlignRight | Qt::AlignBottom, QString("%1 / %2").arg(pg + 1).arg(pages));
    }
    return p.end();
}

// Copy an image file into assets/ and return the ![name](../assets/...) link to it.
QString OutlinerView::importImage(const QString& file) {
    if (assetsDir_.isEmpty() || pageDir_.isEmpty() || !ImageObject::isImagePath(file) || !QFileInfo::exists(file)) return QString();
    QDir().mkpath(assetsDir_);
    const QString wanted = QFileInfo(file).fileName().replace(' ', '_');
    QFileInfo fi(QDir(assetsDir_).filePath(wanted));
    for (int n = 1; fi.exists(); ++n)
        fi.setFile(QDir(assetsDir_).filePath(QString("%1_%2.%3").arg(QFileInfo(wanted).completeBaseName()).arg(n).arg(QFileInfo(wanted).suffix())));
    if (!QFile::copy(file, fi.absoluteFilePath())) return QString();
    const QString rel = QDir(pageDir_).relativeFilePath(fi.absoluteFilePath());
    return QString("![%1](%2)").arg(fi.completeBaseName(), QString(QUrl::toPercentEncoding(rel, "/._-")));
}

// Ctrl+V with a picture (a screenshot, or copied image files): save it in the graph's assets
// folder the way Logseq names them, and insert ![image](../assets/...) at the cursor.
bool OutlinerView::pasteImage() {
    if (assetsDir_.isEmpty() || pageDir_.isEmpty()) return false;
    const QMimeData* md = QGuiApplication::clipboard()->mimeData();
    if (!md) return false;
    QStringList saved;
    const QString stamp = QString::number(QDateTime::currentMSecsSinceEpoch());
    auto freeName = [this](const QString& wanted) {
        QFileInfo fi(QDir(assetsDir_).filePath(wanted));
        for (int n = 1; fi.exists(); ++n)
            fi.setFile(QDir(assetsDir_).filePath(QString("%1_%2.%3").arg(QFileInfo(wanted).completeBaseName()).arg(n).arg(QFileInfo(wanted).suffix())));
        return fi.absoluteFilePath();
    };
    if (md->hasUrls()) {
        QStringList links;
        for (const QUrl& u : md->urls())
            if (u.isLocalFile()) {
                const QString l = importImage(u.toLocalFile());
                if (!l.isEmpty()) links << l;
            }
        if (!links.isEmpty()) {
            editor_->textCursor().insertText(links.join(' '));
            return true;
        }
    }
    if (saved.isEmpty() && md->hasImage()) {
        const QImage img = qvariant_cast<QImage>(md->imageData());
        if (img.isNull()) return false;
        QDir().mkpath(assetsDir_);
        const QString to = freeName("image_" + stamp + "_0.png");
        if (!img.save(to, "PNG")) return false;
        saved << to;
    }
    if (saved.isEmpty()) return false;
    QStringList links;
    for (const QString& f : saved) {
        const QString rel = QDir(pageDir_).relativeFilePath(f);
        links << QString("![%1](%2)").arg(QFileInfo(f).completeBaseName(), QString(QUrl::toPercentEncoding(rel, "/._-")));
    }
    editor_->textCursor().insertText(links.join(' '));
    return true;
}

bool OutlinerView::eventFilter(QObject* obj, QEvent* ev) {
    if (obj != editor_ || ev->type() != QEvent::KeyPress || !doc_ || active_ < 0)
        return QAbstractScrollArea::eventFilter(obj, ev);

    auto* e = static_cast<QKeyEvent*>(ev);
    wakeCursor();

    // While the snippet list shows: arrows choose, Tab/Enter insert, Esc closes it,
    // and Space right after an exact trigger inserts it too.
    if (snippetPopup_->isVisible()) {
        auto* pop = static_cast<SuggestPopup*>(snippetPopup_);
        switch (e->key()) {
            case Qt::Key_Up: pop->move(-1); return true;
            case Qt::Key_Down: pop->move(1); return true;
            case Qt::Key_Tab:
            case Qt::Key_Return:
            case Qt::Key_Enter:
                if (e->modifiers() == Qt::NoModifier && acceptSnippet()) return true;
                break;
            case Qt::Key_Escape:
                if (suggestMode_ == Suggest::Time) return acceptTime(0);  // no time; the cursor leaves the brackets
                pop->hide();
                return true;
            case Qt::Key_Space: {
                if (suggestMode_ != Suggest::Snippets) break;  // "/menu " just closes the menu; dates and timer go on
                int start = 0;
                const QString prefix = snippetPrefix(editor_->toPlainText(), editor_->textCursor().position(), &start);
                const int cur = pop->current();
                if (cur >= 0 && prefix == QString::fromUtf8(snippetMatches_.value(cur).trigger) && acceptSnippet()) {
                    // Text snippets keep the space; ones that leave the cursor inside braces do not.
                    if (editor_->textCursor().atBlockEnd() || editor_->toPlainText().at(editor_->textCursor().position()).isSpace())
                        editor_->insertPlainText(" ");
                    return true;
                }
                break;
            }
            default:
                // Typing a time straight away: it goes after a space, inside the brackets.
                if (suggestMode_ == Suggest::Time && e->text().size() == 1 && e->text()[0].isDigit()) acceptTime(1);
                break;
        }
    }
    if (e->matches(QKeySequence::Paste) && (pasteImage() || pasteBlocks(active_))) return true;
    // Undo / redo: inside the block while the text field has steps, then the page's own.
    if (e->matches(QKeySequence::Undo) && !editor_->document()->isUndoAvailable()) return undo() || true;
    if ((e->matches(QKeySequence::Redo) || e->keyCombination() == QKeyCombination(Qt::ControlModifier, Qt::Key_Y)) &&
        !editor_->document()->isRedoAvailable())
        return redo() || true;
    // Ctrl+A with the whole block already selected: every block on the page.
    if (e->matches(QKeySequence::SelectAll) && editor_->textCursor().selectedText().size() == editor_->toPlainText().size() &&
        !items_.empty()) {
        endEdit();
        selAnchor_ = 0;
        selHead_ = static_cast<int>(items_.size()) - 1;
        setFocus();
        viewport()->update();
        return true;
    }

    const Qt::KeyboardModifiers m = e->modifiers() & ~Qt::KeypadModifier;
    const bool plain = (m == Qt::NoModifier);
    const bool alt_shift = (m == (Qt::AltModifier | Qt::ShiftModifier));
    const bool ctrl = (m == Qt::ControlModifier);

    if ((m & ~Qt::ShiftModifier) == Qt::NoModifier && pairBrackets(e->text())) return true;

    const int stepDir = progressStepKey(e);
    switch (stepDir ? static_cast<int>(Qt::Key_Equal) : e->key()) {
        case Qt::Key_Return:
        case Qt::Key_Enter:
            if (plain) {
                // "/timer 20m 15s" ending the line: run it, and take the command out of the note.
                if (runTimerLine()) return true;
                splitBlock();
                return true;
            }
            if (ctrl) {  // TODO -> DOING -> DONE -> plain, as in Logseq
                const QString before = editor_->toPlainText();
                const QString after = cycleTask(before);
                const int pos = editor_->textCursor().position();
                QTextCursor c(editor_->document());
                c.beginEditBlock();  // one undo step
                c.select(QTextCursor::Document);
                c.insertText(after);
                c.endEditBlock();
                c.setPosition(qBound(0, pos + static_cast<int>(after.size() - before.size()), static_cast<int>(after.size())));
                editor_->setTextCursor(c);
                return true;
            }
            return false;  // Shift+Enter: newline inside the block

        case Qt::Key_Equal:
        case Qt::Key_Plus:
        case Qt::Key_Minus:
            if (m & Qt::AltModifier) {  // the block's progress bar, one step
                const QString before = editor_->toPlainText();
                const QString after = stepProgress(before, stepDir ? stepDir : e->key() == Qt::Key_Minus ? -1 : 1, taskOpts_.finish, taskOpts_.start);
                if (after == before) {
                    Progress p;
                    emit message(findProgress(before, &p) ? (p.full() ? "Already at the end" : "Already at the start")
                                                         : "No progress bar here: /progress adds one");
                    return true;
                }
                const int pos = editor_->textCursor().position();
                QTextCursor c(editor_->document());
                c.beginEditBlock();
                c.select(QTextCursor::Document);
                c.insertText(after);
                c.endEditBlock();
                c.setPosition(qBound(0, pos + static_cast<int>(after.size() - before.size()), static_cast<int>(after.size())));
                editor_->setTextCursor(c);
                announceProgress(after);
                return true;
            }
            return false;

        case Qt::Key_B:
        case Qt::Key_I:
            if (m == Qt::ControlModifier) {  // **bold** / *italics* around the selection, or an empty pair
                const QString mark = e->key() == Qt::Key_B ? "**" : "*";
                QTextCursor c = editor_->textCursor();
                const QString sel = c.selectedText();
                c.beginEditBlock();
                if (sel.size() > 2 * mark.size() && sel.startsWith(mark) && sel.endsWith(mark)) {  // again: undo it
                    c.insertText(sel.mid(mark.size(), sel.size() - 2 * mark.size()));
                } else {
                    const int at = c.selectionStart();
                    c.insertText(mark + sel + mark);
                    if (sel.isEmpty()) c.setPosition(at + static_cast<int>(mark.size()));
                }
                c.endEditBlock();
                editor_->setTextCursor(c);
                return true;
            }
            return false;

        case Qt::Key_Backspace:
            if (plain && !editor_->textCursor().hasSelection() && editor_->textCursor().position() == 0)
                return joinWithPrevious() || true;
            return plain && deletePair();

        case Qt::Key_Tab:
            if (plain) {
                structural(&Document::indent);
                return true;
            }
            return false;

        case Qt::Key_Backtab:
            structural(&Document::outdent);
            return true;

        case Qt::Key_Escape: {  // stop editing; the block stays selected (Esc again clears it)
            const int b = active_;
            endEdit();
            selectBlocks(b, b);
            return true;
        }

        case Qt::Key_Up:
            if (alt_shift) {
                moveBlock(true);
                return true;
            }
            if (ctrl) {
                setCollapsedActive(true);
                return true;
            }
            if (plain && editor_->cursorRect().top() <= 1) {
                navigate(true);
                return true;
            }
            // Shift+Up with the text already selected up to its start: select blocks instead.
            if (m == Qt::ShiftModifier && editor_->textCursor().position() == 0) {
                extendFromEditor(true);
                return true;
            }
            return false;

        case Qt::Key_Down:
            if (alt_shift) {
                moveBlock(false);
                return true;
            }
            if (ctrl) {
                setCollapsedActive(false);
                return true;
            }
            if (plain && editor_->cursorRect().bottom() >= static_cast<int>(editor_->document()->size().height()) - 2) {
                navigate(false);
                return true;
            }
            if (m == Qt::ShiftModifier && editor_->textCursor().position() == editor_->toPlainText().size()) {
                extendFromEditor(false);
                return true;
            }
            return false;

        default: break;
    }
    return false;
}

void OutlinerView::splitBlock() {
    const QString t = editor_->toPlainText();
    const int pos = editor_->textCursor().position();

    // Enter on an empty nested block steps out one level instead of stacking empties.
    if (t.isEmpty() && doc_->at(active_).depth > 0 && !doc_->hasChildren(active_)) {
        structural(&Document::outdent);
        return;
    }
    pushUndoPoint();
    doc_->setText(active_, t.left(pos).toStdString());
    const int nb = doc_->insertBelow(active_, t.mid(pos).toStdString());
    active_ = -1;  // force the old block to be redrawn as rich text
    rebuild();
    focusBlock(nb, 0);
    changed();
}

bool OutlinerView::joinWithPrevious() {
    const std::vector<int> vis = doc_->visible();
    int k = -1;
    for (size_t i = 0; i < vis.size(); ++i)
        if (vis[i] == active_) k = static_cast<int>(i);
    if (k <= 0) return true;                       // first block: nothing to join into
    if (doc_->hasChildren(active_)) return true;   // a parent cannot be folded away

    const int prev = vis[static_cast<size_t>(k - 1)];
    const QString prevText = QString::fromStdString(doc_->at(prev).text);
    const QString cur = editor_->toPlainText();
    pushUndoPoint();
    doc_->setText(prev, (prevText + cur).toStdString());
    doc_->remove(active_);
    active_ = -1;
    rebuild();
    focusBlock(prev, static_cast<int>(prevText.size()));
    changed();
    return true;
}

void OutlinerView::navigate(bool up) {
    const std::vector<int> vis = doc_->visible();
    int k = -1;
    for (size_t i = 0; i < vis.size(); ++i)
        if (vis[i] == active_) k = static_cast<int>(i);
    const int target = up ? k - 1 : k + 1;
    if (k < 0 || target < 0 || target >= static_cast<int>(vis.size())) return;

    const int viewportX = editor_->mapTo(viewport(), QPoint(editor_->cursorRect().center().x(), 0)).x();
    focusBlock(vis[static_cast<size_t>(target)], 0);
    const QPoint local = editor_->mapFrom(viewport(), QPoint(viewportX, 0));
    const int y = up ? qMax(0, editor_->height() - 2) : 1;
    editor_->setTextCursor(editor_->cursorForPosition(QPoint(local.x(), y)));
}

void OutlinerView::structural(bool (Document::*op)(int)) {
    const int pos = editor_->textCursor().position();
    const int block = active_;
    commitTyping();
    const Document before = *doc_;
    if (!(doc_->*op)(block)) return;
    pushUndo(before);
    rebuild();
    focusBlock(block, pos);
    changed();
}

void OutlinerView::moveBlock(bool up) {
    const int pos = editor_->textCursor().position();
    commitTyping();
    const Document before = *doc_;
    const int to = up ? doc_->moveUp(active_) : doc_->moveDown(active_);
    if (to < 0) return;
    pushUndo(before);
    active_ = -1;
    rebuild();
    focusBlock(to, pos);
    changed();
}

void OutlinerView::setCollapsedActive(bool collapse) {
    if (!doc_->hasChildren(active_)) return;
    pushUndoPoint();
    doc_->setCollapsed(active_, collapse);
    const int block = active_;
    const int pos = editor_->textCursor().position();
    active_ = -1;
    rebuild();
    focusBlock(block, pos);
    emit edited();
}

}  // namespace graphite
