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

#include "audioalignmentdialog.h"
#include "tbc/logging.h"
#include "tbc/uistyle.h"

int main(int argc, char *argv[])
{
    // Set 'binary mode' for stdin and stdout on Windows
    setBinaryMode();

    // Install the local debug message handler
    setDebug(true);
    qInstallMessageHandler(debugOutputHandler);

    tbc::ui::prepareStockThemeEnvironment();

    tbc::ui::ThemedApplication app(argc, argv);

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
                                        QCoreApplication::translate("main", "Force dark theme regardless of system settings (default; no-op)")));
    parser.addOption(QCommandLineOption("light-theme",
                                        QCoreApplication::translate("main", "Use the light Fusion theme instead of the stock dark theme")));

    parser.process(app);
    processStandardDebugOptions(parser);

    // Apply the stock theme (dark by default, light via --light-theme). Sets
    // the Fusion palette, isDarkTheme property, Qt 6.8 color scheme override,
    // and input-widget contrast guard; re-asserted on macOS switchover.
    if (parser.isSet(QStringLiteral("light-theme"))) {
        app.applyStockLightTheme();
    } else {
        app.applyStockDarkTheme();
    }

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
