#include "Engine/Core/InstallPaths.h"
#include "Engine/Network/NodeLoader.h"
#include "Gui/Controllers/SceneController.h"
#include "Gui/Network/NetworkViewModel.h"
#include "Gui/Parameters/ParametersViewModel.h"
#include "Gui/Spreadsheet/SpreadsheetViewModel.h"
#include "Gui/Style/ThemeViewModel.h"
#include "Gui/Timeline/TimelineViewModel.h"
#include "Gui/Viewport/QtGraphicsBridge.h"
#include "Gui/Viewport/ViewportViewModel.h"
#include "Graphics/GraphicsDevice.h"
#include <argparse/argparse.hpp>

#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QFontDatabase>
#include <QSet>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QSurfaceFormat>
#include <QUrl>

#include <iostream>
#include <memory>

namespace {

#ifdef HELIX_QML_SOURCE_DIR
/// @brief Returns the QML source root and every folder beneath it.
///
/// Feature folders each hold their own QML, so the whole tree is watched.
QStringList qmlSourceDirs()
{
    const QString root = QStringLiteral(HELIX_QML_SOURCE_DIR);
    QStringList dirs{root};
    QDirIterator it(root, QDir::Dirs | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
    while (it.hasNext())
        dirs << it.next();
    return dirs;
}

/// @brief Reloads the window from disk whenever a QML file changes.
///
/// @note The watch follows folders rather than files because a save often
/// replaces the file and drops a plain file watch.
void installHotReload(QQmlApplicationEngine& engine, const QUrl& entry)
{
    auto* watcher = new QFileSystemWatcher(&engine);
    watcher->addPaths(qmlSourceDirs());

    auto reload = [&engine, entry, watcher]() {
        const QList<QObject*> previousRoots = engine.rootObjects();
        engine.clearComponentCache();
        engine.load(entry);
        for (QObject* root : previousRoots)
            root->deleteLater();

        // Re-arm any folders a save may have replaced.
        const QStringList watched = watcher->directories();
        for (const QString& dir : qmlSourceDirs())
            if (!watched.contains(dir)) watcher->addPath(dir);
    };

    QObject::connect(watcher, &QFileSystemWatcher::directoryChanged, &engine, reload);
}
#endif

} // namespace

namespace {

/// @brief Returns the scene file path given on the command line, empty when none was.
///
/// @note Exits the process on a bad argument, and on --help or --version,
/// before any of the interface is built.
QString parseCommandLine(int argc, char** argv)
{
    argparse::ArgumentParser parser("helix", HELIX_VERSION);
    parser.add_description("Procedural 3D modelling.");
    parser.add_argument("scene")
        .help("a .kut scene to open on startup")
        .nargs(argparse::nargs_pattern::optional)
        .default_value(std::string{});

    try
    {
        parser.parse_args(argc, argv);
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << "\n\n" << parser;
        std::exit(1);
    }

    return QString::fromStdString(parser.get<std::string>("scene"));
}

} // namespace

namespace {

/// @brief Registers the bundled Public Sans and Inconsolata weights.
/// @return The font families they belong to, alphabetically.
QStringList loadFonts()
{
    QSet<QString> families;
    QDirIterator it(
        QString::fromStdString((helix::getStaticDir() / "fonts").string()),
        {"*.ttf"},
        QDir::Files,
        QDirIterator::Subdirectories
    );
    while (it.hasNext())
    {
        const int font = QFontDatabase::addApplicationFont(it.next());
        for (const QString& family : QFontDatabase::applicationFontFamilies(font))
            families.insert(family);
    }

    QStringList sorted(families.constBegin(), families.constEnd());
    sorted.sort();
    return sorted;
}

} // namespace

int main(int argc, char** argv)
{
    const QString scenePath = parseCommandLine(argc, argv);

    // The viewport draws with Metal, so a machine with no Metal device cannot show it. The
    // message is fixed, since the smoke test of the CI accepts exactly this one.
    if (!helix::gfx::GraphicsDevice::isMetalAvailable())
    {
        std::cerr << helix::gfx::GraphicsDevice::noDeviceMessage << "\n";
        return 1;
    }

    // Icons recolour their SVG markup through a local file XMLHttpRequest.
    qputenv("QML_XHR_ALLOW_FILE_READ", "1");

    // Send Qt and QML debug logging to the terminal.
    qputenv("QT_LOGGING_RULES", "default.debug=true;qml.debug=true;js.debug=true");

    // Multisampling smooths the scene graph geometry, including the link curves.
    QSurfaceFormat format = QSurfaceFormat::defaultFormat();
    format.setSamples(4);
    QSurfaceFormat::setDefaultFormat(format);

    // The viewport hands a Metal texture to the scene graph, which only the Metal one can show.
    helix::ui::QtGraphicsBridge::selectMetal();

    QGuiApplication app(argc, argv);
    app.setOrganizationName("Helix");
    app.setApplicationName("Helix");

    const QStringList fontFamilies = loadFonts();
    QQuickStyle::setStyle("Basic");

    // The view-models bridge the engine to QML.
    helix::ui::SpreadsheetViewModel spreadsheet;
    helix::ui::NetworkViewModel network;
    helix::ui::ViewportViewModel viewport;
    helix::ui::ParametersViewModel parameters;
    helix::ui::TimelineViewModel timeline;
    helix::ui::SceneController scene;
    helix::ui::ThemeViewModel themeSettings;
    themeSettings.setFontFamilies(fontFamilies);

    helix::nt::NodeLoader::loadNodes();

    if (!scenePath.isEmpty())
    {
        if (!QFileInfo::exists(scenePath))
        {
            std::cerr << "no such file: " << scenePath.toStdString() << "\n";
            return 1;
        }

        try
        {
            scene.openPath(scenePath);
        }
        catch (const std::exception& error)
        {
            std::cerr << "could not open " << scenePath.toStdString() << ": " << error.what()
                      << "\n";
            return 1;
        }
    }

    QQmlApplicationEngine engine;

    engine.rootContext()->setContextProperty("spreadsheet", &spreadsheet);
    engine.rootContext()->setContextProperty("network", &network);
    engine.rootContext()->setContextProperty("viewport", &viewport);
    engine.rootContext()->setContextProperty("parameters", &parameters);
    engine.rootContext()->setContextProperty("timeline", &timeline);
    engine.rootContext()->setContextProperty("scene", &scene);
    engine.rootContext()->setContextProperty("themeSettings", &themeSettings);

    // A run from a build directory loads QML straight from the source tree and
    // reloads on edit. An installed run loads the module compiled into the binary.
    const QString qmlSourceDir = QStringLiteral(HELIX_QML_SOURCE_DIR);
    if (QFileInfo::exists(qmlSourceDir + "/App.qml"))
    {
        const QUrl entry = QUrl::fromLocalFile(qmlSourceDir + "/App.qml");
        engine.addImportPath(qmlSourceDir);
        installHotReload(engine, entry);
        engine.load(entry);
    }
    else
    {
        engine.loadFromModule("Helix", "App");
    }

    if (engine.rootObjects().isEmpty()) return -1;

    return app.exec();
}
