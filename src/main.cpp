#include <QApplication>
#include <QDir>
#include <QFileDialog>
#include <QFontDatabase>
#include <QFileInfo>
#include <QSettings>
#include <QSocketNotifier>
#include <csignal>
#include <cstdio>
#include <sys/socket.h>
#include <unistd.h>

#include "ui/blur.hpp"
#include "ui/mainwindow.hpp"
#include "ui/palette.hpp"
#include "ui/theme.hpp"

// Usage: graphite [options] [path/to/graph]
//   --wallpaper FILE   take the colours from this picture
//   --palette unit01|unit00|unit02  ignore the wallpaper, use an Eva unit's colours
//                      (builtin = unit01)
//   --palette wallpaper  follow the wallpaper (the default)
//   --palette-info     print where the colours came from and exit
//   --version          print the version and exit
// With no path the graph used last time is opened; the first run asks for a folder.
// Ctrl+P switches between the two palettes and remembers the choice; --palette overrides it
// for one run.
namespace {
// SIGTERM / SIGINT / SIGHUP (kill, Ctrl+C, logout) end the event loop normally, so the
// window saves and Hyprland gets its blur back. The handler only writes a byte; Qt reads it.
int g_signalFd[2] = {-1, -1};
void onSignal(int) {
    const char c = 1;
    [[maybe_unused]] const auto n = ::write(g_signalFd[0], &c, 1);
}

// Fonts shipped with the app: every .ttf / .otf in a "fonts" folder beside the TeX resources
// (macOS bundle: Contents/Resources/fonts, else <prefix>/share/graphite/fonts). A Mac has none of
// Graphite's default families, so the release workflow puts the Noto faces there.
void registerBundledFonts() {
    const QString exe = QCoreApplication::applicationDirPath();
    for (const QString& dir : {exe + "/../Resources/fonts", exe + "/../share/graphite/fonts"}) {
        const QDir d(dir);
        if (!d.exists()) continue;
        for (const QString& f : d.entryList({"*.ttf", "*.otf", "*.TTF", "*.OTF"}, QDir::Files))
            QFontDatabase::addApplicationFont(d.absoluteFilePath(f));
    }
}
}  // namespace

int main(int argc, char** argv) {
    // Answered before Qt starts, so install.sh can ask an old copy without opening a window.
    for (int i = 1; i < argc; ++i)
        if (qstrcmp(argv[i], "--version") == 0) {
            std::printf("graphite %s\n", GRAPHITE_VERSION);
            return 0;
        }
    QApplication app(argc, argv);
    registerBundledFonts();  // before the theme looks up its families
    QApplication::setApplicationName("graphite");
    QApplication::setOrganizationName("graphite");
    QApplication::setApplicationDisplayName("Graphite");
    QApplication::setDesktopFileName("graphite");
    // Chosen with Ctrl+Shift+F; empty = the defaults.
    graphite::Theme::instance().setTitleFamily(QSettings().value("titleFont").toString());
    graphite::Theme::instance().setBodyFamily(QSettings().value("bodyFont").toString());
    graphite::Theme::instance().setImmersion(QSettings().value("immersion", true).toBool());
    graphite::Theme::instance().setTextSize(QSettings().value("textSize", graphite::Theme::kDefaultTextSize).toDouble());
    app.setFont(graphite::Theme::instance().bodyFont());

    graphite::BlurControl::recoverAfterCrash();  // blur left changed by a run that was killed
    graphite::PaletteProvider palette;
    {   // "palette": wallpaper, or a unit; "builtin" (older versions) is Unit-01
        const QString saved = QSettings().value("palette").toString();
        palette.setUnit(saved == "builtin" ? "unit01" : graphite::unitIds().contains(saved) ? saved : QString());
    }
    QString graph;
    bool info = false;
    const QStringList args = QCoreApplication::arguments().mid(1);
    for (int i = 0; i < args.size(); ++i) {
        const QString& a = args[i];
        if (a == "--wallpaper" && i + 1 < args.size()) palette.setWallpaperOverride(args[++i]);
        else if (a == "--palette" && i + 1 < args.size()) {
            const QString v = args[++i];
            palette.setUnit(v == "builtin" ? "unit01" : graphite::unitIds().contains(v) ? v : QString());
        }
        else if (a == "--palette-info") info = true;
        else if (!a.startsWith("--")) graph = QDir(a).absolutePath();
    }

    palette.start();
    const graphite::PaletteColors p = palette.current();
    if (info) {
        std::printf("colours from: %s\n", qPrintable(p.source));
        for (const auto& [name, c] : {std::pair<const char*, QColor>{"background", p.bg}, {"panel", p.panel},
                                      {"text", p.text}, {"accent", p.accent}, {"structure", p.structure},
                                      {"warm", p.warm}})
            std::printf("  %-10s %s\n", name, qPrintable(c.name()));
        return 0;
    }
    graphite::Theme::instance().setPalette(p);
    QObject::connect(&palette, &graphite::PaletteProvider::changed,
                     [](const graphite::PaletteColors& next) { graphite::Theme::instance().setPalette(next); });
    std::fprintf(stderr, "graphite: colours from %s\n", qPrintable(p.source));

    if (graph.isEmpty() || !QFileInfo(graph).isDir()) graph = QSettings().value("graph").toString();
    if (graph.isEmpty() || !QFileInfo(graph).isDir())
        graph = QFileDialog::getExistingDirectory(nullptr, "Choose your Logseq graph folder", QDir::homePath());
    if (graph.isEmpty()) return 0;

    if (::socketpair(AF_UNIX, SOCK_STREAM, 0, g_signalFd) == 0) {
        auto* notifier = new QSocketNotifier(g_signalFd[1], QSocketNotifier::Read, &app);
        QObject::connect(notifier, &QSocketNotifier::activated, &app, [] { QApplication::closeAllWindows(); QApplication::quit(); });
        for (int sig : {SIGTERM, SIGINT, SIGHUP}) std::signal(sig, onSignal);
    }

    graphite::MainWindow window(graph);
    QObject::connect(&window, &graphite::MainWindow::paletteRequested, [&palette](const QString& unit) {
        palette.setUnit(unit);
        QSettings().setValue("palette", unit.isEmpty() ? "wallpaper" : unit);
    });
    window.show();
    return app.exec();
}
