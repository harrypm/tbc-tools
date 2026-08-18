/******************************************************************************
 * main.cpp
 * tbc-analyse - TBC output analysis GUI
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2018-2025 Simon Inns
 *
 * This file is part of tbc-tools.
 ******************************************************************************/

#include "mainwindow.h"
#include <QApplication>
#include <QDebug>
#include <QtGlobal>
#include <QCommandLineParser>
#include <QLoggingCategory>
#include <QPixmap>
#include <QStyleFactory>
#include <QDir>
#include <QFileInfo>

#ifdef Q_OS_WIN
#include <QSettings>
#elif defined(Q_OS_MACOS)
#include <QProcess>
#elif defined(Q_OS_LINUX)
#include <QProcess>
#endif

#include "tbc/logging.h"
#include "tbc/uistyle.h"
namespace {
QIcon bundledApplicationIcon()
{
    QIcon icon;
    const QStringList iconResources = {
        QStringLiteral(":/icons/Graphics/16-analyse.png"),
        QStringLiteral(":/icons/Graphics/32-analyse.png"),
        QStringLiteral(":/icons/Graphics/64-analyse.png"),
        QStringLiteral(":/icons/Graphics/128-analyse.png"),
        QStringLiteral(":/icons/Graphics/256-analyse.png")
    };

    for (const QString &resource : iconResources) {
        QPixmap pixmap(resource);
        if (!pixmap.isNull()) {
            icon.addPixmap(pixmap);
        }
    }

    return icon;
}

QString resolvedExecutableDirectory(const char *argv0)
{
    if (!argv0 || argv0[0] == '\0') {
        return QDir::currentPath();
    }

    const QString rawPath = QString::fromLocal8Bit(argv0);
    const QFileInfo info(rawPath);
    if (info.isAbsolute()) {
        return info.absolutePath();
    }

    return QFileInfo(QDir::current().absoluteFilePath(rawPath)).absolutePath();
}

void prependEnvSearchPath(const char *envKey, const QStringList &paths)
{
    if (!envKey || paths.isEmpty()) {
        return;
    }

    QStringList normalizedPaths;
    for (const QString &path : paths) {
        const QString cleanPath = QDir::cleanPath(path.trimmed());
        if (!cleanPath.isEmpty() && !normalizedPaths.contains(cleanPath)) {
            normalizedPaths << cleanPath;
        }
    }
    if (normalizedPaths.isEmpty()) {
        return;
    }

    QStringList envEntries =
        QString::fromLocal8Bit(qgetenv(envKey)).split(QDir::listSeparator(), Qt::SkipEmptyParts);
    for (int i = normalizedPaths.size() - 1; i >= 0; --i) {
        if (!envEntries.contains(normalizedPaths.at(i))) {
            envEntries.prepend(normalizedPaths.at(i));
        }
    }

    qputenv(envKey, envEntries.join(QDir::listSeparator()).toLocal8Bit());
}

void configureBundledQtPluginPaths(int argc, char *argv[])
{
#if defined(Q_OS_LINUX)
    const QString exeDir = resolvedExecutableDirectory((argv && argc > 0) ? argv[0] : nullptr);
    QStringList pluginRoots;

    auto appendPluginRoot = [&pluginRoots](const QString &path) {
        if (path.isEmpty()) {
            return;
        }
        const QString cleanPath = QDir::cleanPath(path);
        if (QDir(cleanPath).exists() && !pluginRoots.contains(cleanPath)) {
            pluginRoots << cleanPath;
        }
    };

    appendPluginRoot(QDir(exeDir).filePath(QStringLiteral("../plugins")));
    appendPluginRoot(QDir(exeDir).filePath(QStringLiteral("plugins")));

    if (qEnvironmentVariableIsSet("APPDIR")) {
        const QString appDirRoot = qEnvironmentVariable("APPDIR");
        appendPluginRoot(QDir(appDirRoot).filePath(QStringLiteral("usr/plugins")));
        appendPluginRoot(QDir(appDirRoot).filePath(QStringLiteral("usr/lib/qt6/plugins")));
        appendPluginRoot(QDir(appDirRoot).filePath(QStringLiteral("usr/lib/plugins")));
    }

    if (pluginRoots.isEmpty()) {
        return;
    }

    QStringList platformPluginPaths;
    for (const QString &pluginRoot : pluginRoots) {
        const QString platformsDir = QDir(pluginRoot).filePath(QStringLiteral("platforms"));
        if (QDir(platformsDir).exists() && !platformPluginPaths.contains(platformsDir)) {
            platformPluginPaths << platformsDir;
        }
    }

    if (qEnvironmentVariable("QT_QPA_PLATFORM_PLUGIN_PATH").trimmed().isEmpty()
        && !platformPluginPaths.isEmpty()) {
        qputenv("QT_QPA_PLATFORM_PLUGIN_PATH", platformPluginPaths.constFirst().toLocal8Bit());
    }

    prependEnvSearchPath("QT_PLUGIN_PATH", pluginRoots);
#else
    Q_UNUSED(argc)
    Q_UNUSED(argv)
#endif
}
} // namespace

// Cross-platform function to detect if system is in dark mode
bool isDarkModeEnabled() {
#ifdef Q_OS_WIN
    QSettings settings("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize", QSettings::NativeFormat);
    return settings.value("AppsUseLightTheme", 1).toInt() == 0;
#elif defined(Q_OS_MACOS)
    QProcess process;
    process.start("defaults", QStringList() << "read" << "-g" << "AppleInterfaceStyle");
    process.waitForFinished();
    QString result = process.readAllStandardOutput().trimmed();
    return result == "Dark";
#elif defined(Q_OS_LINUX)
    // Try gsettings for GNOME color-scheme (modern approach)
    QProcess process;
    process.start("gsettings", QStringList() << "get" << "org.gnome.desktop.interface" << "color-scheme");
    process.waitForFinished();
    QString result = process.readAllStandardOutput().trimmed();
    result = result.remove('\'').remove('"'); // Remove quotes
    if (result.contains("dark", Qt::CaseInsensitive)) {
        return true;
    }
    
    // Fallback: Check GTK theme name
    process.start("gsettings", QStringList() << "get" << "org.gnome.desktop.interface" << "gtk-theme");
    process.waitForFinished();
    result = process.readAllStandardOutput().trimmed();
    result = result.remove('\'').remove('"');
    return result.contains("dark", Qt::CaseInsensitive);
#endif
    return false;
}

// Apply a dark theme palette to the application
void applyDarkTheme(QApplication &app) {
    QPalette darkPalette;
    
    // Define dark theme colors
    QColor windowColor(53, 53, 53);
    QColor baseColor(25, 25, 25);
    QColor alternateColor(64, 64, 64);
    QColor textColor(255, 255, 255);
    QColor buttonColor(53, 53, 53);
    QColor highlightColor(42, 130, 218);
    QColor highlightTextColor(255, 255, 255);
    
    // Set palette colors
    darkPalette.setColor(QPalette::Window, windowColor);
    darkPalette.setColor(QPalette::WindowText, textColor);
    darkPalette.setColor(QPalette::Base, baseColor);
    darkPalette.setColor(QPalette::AlternateBase, alternateColor);
    darkPalette.setColor(QPalette::Text, textColor);
    darkPalette.setColor(QPalette::Button, buttonColor);
    darkPalette.setColor(QPalette::ButtonText, textColor);
    darkPalette.setColor(QPalette::Highlight, highlightColor);
    darkPalette.setColor(QPalette::HighlightedText, highlightTextColor);
    
    app.setPalette(darkPalette);
}

// Custom message handler that filters out harmless Qt system warnings
void filteredDebugOutputHandler(QtMsgType type, const QMessageLogContext &context, const QString &msg)
{
    // Filter out harmless Qt system warnings that don't affect functionality
    if (msg.contains("Wayland does not support QWindow::requestActivate()") ||
        msg.contains("QSocketNotifier: Can only be used with threads started with QThread")) {
        return; // Don't output these warnings
    }
    
    // Call the original handler for all other messages
    debugOutputHandler(type, context, msg);
}

int main(int argc, char *argv[])
{
    // Install the local debug message handler with Qt system warning filtering
    qInstallMessageHandler(filteredDebugOutputHandler);
    configureBundledQtPluginPaths(argc, argv);

    tbc::ui::normalizeUnsupportedStyleOverrideToFusion();

    QApplication a(argc, argv);
    tbc::ui::applyFusionStyleIfAvailable(a);

    // Set application name and version
    QCoreApplication::setApplicationName("ld-analyse");
    QCoreApplication::setApplicationVersion(QString(APP_VERSION));
    QCoreApplication::setOrganizationDomain("github.com");

    // Set desktop file name for proper GNOME integration
    // This must match the installed .desktop file name (without .desktop extension)
    QGuiApplication::setDesktopFileName("ld-analyse");
    
    // Set application icon (for window decorations and taskbar/dock).
    // QIcon::fromTheme works on desktops with an installed theme; otherwise use bundled assets.
    QIcon appIcon = QIcon::fromTheme(QStringLiteral("ld-analyse"));
    if (appIcon.isNull()) {
        appIcon = bundledApplicationIcon();
    }
    if (!appIcon.isNull()) {
        a.setWindowIcon(appIcon);
    }

    // Set up the command line parser
    QCommandLineParser parser;
    parser.setApplicationDescription(
        "ld-analyse - analysis & adjustment tool for the decode projects 4fsc TBC format\n"
        "\n"
        "(c)2018-2025 Simon Inns\n"
        "(c)2020-2022 Adam Sampson\n"
        "GPLv3 Open-Source - github: https://github.com/happycube/ld-decode");
    parser.addHelpOption();
    parser.addVersionOption();

    // Add the standard debug options --debug and --quiet
    addStandardDebugOptions(parser);

    // Add theme override option
    parser.addOption(QCommandLineOption("force-dark-theme", "Force dark theme regardless of system settings"));
    parser.addOption(QCommandLineOption("metadata-only", "Load metadata (.db or .json) without TBC data"));

    // Positional argument to specify input video file
    parser.addPositionalArgument("input", QCoreApplication::translate("main", "Specify input TBC or metadata file"));

    // Process the command line arguments given by the user
    parser.process(a);

    // Standard logging options
    processStandardDebugOptions(parser);

    // Check for theme override
    bool forceDarkTheme = parser.isSet("force-dark-theme");

    // Determine theme: command line override takes precedence over system detection
    bool shouldApplyDarkTheme;
    if (forceDarkTheme) {
        shouldApplyDarkTheme = true;
        a.setProperty("isDarkTheme", true);
    } else {
        // Qt on Linux doesn't automatically pick up GTK themes, so detect manually
        shouldApplyDarkTheme = isDarkModeEnabled();
        // Don't set property - let PlotWidget detect from applied palette
    }
    
    // Apply dark theme if needed (Qt doesn't do this automatically on Linux)
    if (shouldApplyDarkTheme) {
        applyDarkTheme(a);
    }
    tbc::ui::enforceInputWidgetContrast(a);

    // Get the arguments from the parser
    QString inputFileName;
    QStringList positionalArguments = parser.positionalArguments();
    if (positionalArguments.count() == 1) {
        inputFileName = positionalArguments.at(0);
    } else {
        inputFileName.clear();
    }
    const bool metadataOnly = parser.isSet("metadata-only");

    // Start the GUI application
    MainWindow w(inputFileName, metadataOnly);
    w.show();

    return a.exec();
}
