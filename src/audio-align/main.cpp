/******************************************************************************
 * main.cpp
 * tbc-audio-align - Audio alignment tool for ld-decode
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 *
 * This file is part of tbc-tools.
 ******************************************************************************/

#include <QApplication>
#include <QColor>
#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QPalette>
#include <QStyleFactory>
#if defined(Q_OS_WIN)
#include <QSettings>
#elif defined(Q_OS_MACOS)
#include <QProcess>
#elif defined(Q_OS_LINUX)
#include <QProcess>
#endif

#include "audioalignmentdialog.h"
#include "tbc/logging.h"
#include "tbc/uistyle.h"
namespace {
// Cross-platform function to detect if system is in dark mode
bool isDarkModeEnabled()
{
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
void applyDarkTheme(QApplication &app)
{
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
} // namespace

int main(int argc, char *argv[])
{
    // Set 'binary mode' for stdin and stdout on Windows
    setBinaryMode();

    // Install the local debug message handler
    setDebug(true);
    qInstallMessageHandler(debugOutputHandler);

    tbc::ui::normalizeUnsupportedStyleOverrideToFusion();

    QApplication app(argc, argv);
    tbc::ui::applyFusionStyleIfAvailable(app);

    // Set application name and version
    QCoreApplication::setApplicationName("tbc-audio-align");
    QCoreApplication::setApplicationVersion(QString("tbc-tools - Branch: %1 / Commit: %2").arg(APP_BRANCH, APP_COMMIT));
    QCoreApplication::setOrganizationDomain("domesday86.com");

    // Set up the command line parser
    QCommandLineParser parser;
    parser.setApplicationDescription(
                "tbc-audio-align - Audio alignment tool for ld-decode\n"
                "\n"
                "(c)2026 Simon Inns\n"
                "GPLv3 Open-Source - github: https://github.com/happycube/ld-decode");
    parser.addHelpOption();
    parser.addVersionOption();

    // Add the standard debug options --debug and --quiet
    addStandardDebugOptions(parser);

    QCommandLineOption sourceDirectoryOption(QStringList() << "source-dir",
                                             QCoreApplication::translate("main", "Initial source directory used by browse dialogs"),
                                             QCoreApplication::translate("main", "path"));
    parser.addOption(sourceDirectoryOption);

    QCommandLineOption jsonOption(QStringList() << "json",
                                  QCoreApplication::translate("main", "Prefill metadata JSON input"),
                                  QCoreApplication::translate("main", "filename"));
    parser.addOption(jsonOption);

    QCommandLineOption inputFileOption(QStringList() << "input-file",
                                       QCoreApplication::translate("main", "Prefill input audio stream"),
                                       QCoreApplication::translate("main", "filename"));
    parser.addOption(inputFileOption);

    QCommandLineOption outputFileOption(QStringList() << "output-file",
                                        QCoreApplication::translate("main", "Prefill output audio stream"),
                                        QCoreApplication::translate("main", "filename"));
    parser.addOption(outputFileOption);

    QCommandLineOption rfVideoSampleRateOption(QStringList() << "rf-video-sample-rate-hz",
                                               QCoreApplication::translate("main", "Prefill RF video sample rate in Hz"),
                                               QCoreApplication::translate("main", "hz"));
    parser.addOption(rfVideoSampleRateOption);

    QCommandLineOption exportTrackFileOption(QStringList() << "export-track-file",
                                             QCoreApplication::translate("main", "Write aligned track details for ld-analyse export auto-load"),
                                             QCoreApplication::translate("main", "filename"));
    parser.addOption(exportTrackFileOption);
    parser.addOption(QCommandLineOption("force-dark-theme",
                                        QCoreApplication::translate("main", "Force dark theme regardless of system settings")));

    parser.process(app);
    processStandardDebugOptions(parser);
    const bool forceDarkTheme = parser.isSet(QStringLiteral("force-dark-theme"));
    const bool shouldApplyDarkTheme = forceDarkTheme ? true : isDarkModeEnabled();
    if (shouldApplyDarkTheme) {
        app.setProperty("isDarkTheme", true);
        applyDarkTheme(app);
    }
    tbc::ui::enforceInputWidgetContrast(app);

    AudioAlignmentDialog dialog;
    if (parser.isSet(sourceDirectoryOption)) {
        dialog.setSourceDirectory(parser.value(sourceDirectoryOption));
    }
    if (parser.isSet(jsonOption)) {
        dialog.setDefaultJson(parser.value(jsonOption));
    }
    if (parser.isSet(inputFileOption)) {
        dialog.setDefaultInputFile(parser.value(inputFileOption));
    }
    if (parser.isSet(outputFileOption)) {
        dialog.setDefaultOutputFile(parser.value(outputFileOption));
    }
    if (parser.isSet(exportTrackFileOption)) {
        dialog.setExportTrackOutputFile(parser.value(exportTrackFileOption));
    }

    bool ok = false;
    if (parser.isSet(rfVideoSampleRateOption)) {
        const quint32 rfSampleRate = parser.value(rfVideoSampleRateOption).toUInt(&ok);
        if (ok && rfSampleRate > 0) {
            dialog.setDefaultRfVideoSampleRate(rfSampleRate);
        }
    }

    dialog.exec();
    return 0;
}
