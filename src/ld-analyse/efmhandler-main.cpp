/******************************************************************************
 * efmhandler-main.cpp
 * tbc-efm-handler - Dedicated EFM/AC3 handling workflow GUI
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
#include <QFileInfo>
#include <QPalette>
#include <QStringList>
#include <QStyleFactory>

#include "efmhandlerdialog.h"
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

    QCoreApplication::setApplicationName("tbc-efm-handler");
    QCoreApplication::setApplicationVersion(
        QString("tbc-tools - Branch: %1 / Commit: %2").arg(APP_BRANCH, APP_COMMIT));
    QCoreApplication::setOrganizationDomain("github.com");

    QCommandLineParser parser;
    parser.setApplicationDescription(
        "tbc-efm-handler - Dedicated EFM/AC3 handling workflow GUI\n"
        "\n"
        "(c)2026 Simon Inns\n"
        "GPLv3 Open-Source - github: https://github.com/happycube/ld-decode");
    parser.addHelpOption();
    parser.addVersionOption();

    // Add the standard debug options --debug and --quiet
    addStandardDebugOptions(parser);

    QCommandLineOption sourceDirectoryOption(QStringList() << "source-dir",
                                             QCoreApplication::translate(
                                                 "main", "Initial source directory used by browse dialogs"),
                                             QCoreApplication::translate("main", "path"));
    parser.addOption(sourceDirectoryOption);

    QCommandLineOption efmInputOption(QStringList() << "efm-input",
                                      QCoreApplication::translate(
                                          "main", "Prefill EFM input file (can be passed multiple times)"),
                                      QCoreApplication::translate("main", "filename"));
    parser.addOption(efmInputOption);

    QCommandLineOption ac3InputOption(QStringList() << "ac3-input",
                                      QCoreApplication::translate("main", "Prefill AC3 symbols input file"),
                                      QCoreApplication::translate("main", "filename"));
    parser.addOption(ac3InputOption);

    QCommandLineOption outputBaseOption(QStringList() << "output-base",
                                        QCoreApplication::translate("main", "Prefill EFM output base path"),
                                        QCoreApplication::translate("main", "path"));
    parser.addOption(outputBaseOption);
    parser.addOption(QCommandLineOption("force-dark-theme",
                                        QCoreApplication::translate("main", "Force dark theme regardless of system settings (default; no-op)")));
    parser.addOption(QCommandLineOption("light-theme",
                                        QCoreApplication::translate("main", "Use the light Fusion theme instead of the stock dark theme")));

    parser.addPositionalArgument(
        "input",
        QCoreApplication::translate("main",
                                    "Optional input file(s) to preload (.efm => EFM input, others => AC3 input)"),
        "[input ...]");

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

    EfmHandlerDialog dialog;

    if (parser.isSet(sourceDirectoryOption)) {
        dialog.setSourceDirectory(parser.value(sourceDirectoryOption));
    }

    const QStringList efmInputs = parser.values(efmInputOption);
    for (const QString &efmInput : efmInputs) {
        dialog.setDefaultEfmInput(efmInput);
    }

    if (parser.isSet(ac3InputOption)) {
        dialog.setDefaultAc3Input(parser.value(ac3InputOption));
    }

    if (parser.isSet(outputBaseOption)) {
        dialog.setSuggestedOutputBase(parser.value(outputBaseOption));
    }

    const QStringList positionalInputs = parser.positionalArguments();
    for (const QString &positionalInput : positionalInputs) {
        const QString suffix = QFileInfo(positionalInput).suffix().trimmed().toLower();
        if (suffix == QStringLiteral("efm")) {
            dialog.setDefaultEfmInput(positionalInput);
        } else {
            dialog.setDefaultAc3Input(positionalInput);
        }
    }

    dialog.exec();
    return 0;
}
