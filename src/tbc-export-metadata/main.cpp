/************************************************************************

    main.cpp

    tbc-export-metadata - Export ld-decode metadata into other formats
    Copyright (C) 2020-2023 Adam Sampson
    Copyright (C) 2021-2025 Simon Inns

    This file is part of tbc-tools.

    tbc-export-metadata is free software: you can redistribute it and/or
    modify it under the terms of the GNU General Public License as
    published by the Free Software Foundation, either version 3 of the
    License, or (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.

************************************************************************/

#include <QApplication>
#include <QCoreApplication>
#include <QCommandLineParser>
#include <QPalette>
#include <QStyleFactory>
#include <QWindow>
#include <memory>

#include "audacity.h"
#include "csv.h"
#include "ffmetadata.h"
#include "vitcffmetadata.h"
#include "closedcaptions.h"
#include "metadataconverter.h"
#include "metadataexportdialog.h"

#include "tbc/logging.h"
#include "tbc/uistyle.h"
#include "tbcmetadata.h"
#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif
namespace {
struct ExportCommandLineOptions {
    QCommandLineOption guiOption;
    QCommandLineOption forceDarkThemeOption;
    QCommandLineOption lightThemeOption;
    QCommandLineOption parentWindowIdOption;
    QCommandLineOption inputOption;
    QCommandLineOption exportJsonOption;
    QCommandLineOption outputJsonOption;
    QCommandLineOption writeVitsCsvOption;
    QCommandLineOption writeVbiCsvOption;
    QCommandLineOption writeUserMarkersTxtOption;
    QCommandLineOption writeUserMarkersCsvOption;
    QCommandLineOption writeAudacityLabelsOption;
    QCommandLineOption writeFfmetadataOption;
    QCommandLineOption writeFfmpegVitcOption;
    QCommandLineOption ffmetadataNoVitcTimecodeOption;
    QCommandLineOption ffmetadataStartOption;
    QCommandLineOption ffmetadataLengthOption;
    QCommandLineOption writeClosedCaptionsOption;

    ExportCommandLineOptions() :
        guiOption(QStringList() << "g" << "gui",
                  QCoreApplication::translate("main", "Launch dedicated metadata export GUI")),
        forceDarkThemeOption("force-dark-theme",
                             QCoreApplication::translate("main", "Force dark theme regardless of system settings (default; no-op)")),
        lightThemeOption("light-theme",
                         QCoreApplication::translate("main", "Use the light Fusion theme instead of the stock dark theme")),
        parentWindowIdOption("parent-window-id",
                             QCoreApplication::translate("main", "Set transient parent window id for GUI integration"),
                             QCoreApplication::translate("main", "id")),
        inputOption(QStringList() << "input" << "input-sqlite",
                    QCoreApplication::translate("main", "Specify input metadata file"),
                    QCoreApplication::translate("main", "file")),
        exportJsonOption("export-json",
                         QCoreApplication::translate("main", "Write decode export metadata as JSON (default output: <input>.export.json)")),
        outputJsonOption("output-json",
                         QCoreApplication::translate("main", "Specify decode export metadata output JSON file (implies --export-json)"),
                         QCoreApplication::translate("main", "file")),
        writeVitsCsvOption("vits-csv",
                           QCoreApplication::translate("main", "Write VITS information as CSV"),
                           QCoreApplication::translate("main", "file")),
        writeVbiCsvOption("vbi-csv",
                          QCoreApplication::translate("main", "Write VBI information as CSV"),
                          QCoreApplication::translate("main", "file")),
        writeUserMarkersTxtOption("user-markers-txt",
                                  QCoreApplication::translate("main", "Write user markers as plain text log"),
                                  QCoreApplication::translate("main", "file")),
        writeUserMarkersCsvOption("user-markers-csv",
                                  QCoreApplication::translate("main", "Write user markers as CSV log"),
                                  QCoreApplication::translate("main", "file")),
        writeAudacityLabelsOption("audacity-labels",
                                  QCoreApplication::translate("main", "Write navigation information as Audacity labels"),
                                  QCoreApplication::translate("main", "file")),
        writeFfmetadataOption("ffmetadata",
                              QCoreApplication::translate("main", "Write navigation information as FFMETADATA1"),
                              QCoreApplication::translate("main", "file")),
        writeFfmpegVitcOption("ffmpeg-vitc",
                              QCoreApplication::translate("main", "Write FFmpeg metadata=print-style VITC text"),
                              QCoreApplication::translate("main", "file")),
        ffmetadataNoVitcTimecodeOption("ffmetadata-no-vitc-timecode",
                                       QCoreApplication::translate("main", "Disable FFmpeg-style VITC timecode output in FFMETADATA")),
        ffmetadataStartOption("start",
                              QCoreApplication::translate("main", "FFMETADATA export start frame (1-based)"),
                              QCoreApplication::translate("main", "frame")),
        ffmetadataLengthOption("length",
                               QCoreApplication::translate("main", "FFMETADATA export frame length"),
                               QCoreApplication::translate("main", "frames")),
        writeClosedCaptionsOption("closed-captions",
                                  QCoreApplication::translate("main", "Write closed captions as Scenarist SCC V1.0 format"),
                                  QCoreApplication::translate("main", "file"))
    {
    }

    void addToParser(QCommandLineParser &parser) const
    {
        parser.addOption(guiOption);
        parser.addOption(forceDarkThemeOption);
        parser.addOption(lightThemeOption);
        parser.addOption(parentWindowIdOption);
        parser.addOption(inputOption);
        parser.addOption(exportJsonOption);
        parser.addOption(outputJsonOption);
        parser.addOption(writeVitsCsvOption);
        parser.addOption(writeVbiCsvOption);
        parser.addOption(writeUserMarkersTxtOption);
        parser.addOption(writeUserMarkersCsvOption);
        parser.addOption(writeAudacityLabelsOption);
        parser.addOption(writeFfmetadataOption);
        parser.addOption(writeFfmpegVitcOption);
        parser.addOption(ffmetadataNoVitcTimecodeOption);
        parser.addOption(ffmetadataStartOption);
        parser.addOption(ffmetadataLengthOption);
        parser.addOption(writeClosedCaptionsOption);
    }
};

bool wantsGui(int argc, char *argv[])
{
    if (argc <= 1) {
        return true;
    }
    for (int i = 1; i < argc; ++i) {
        const QString arg = QString::fromLocal8Bit(argv[i]);
        if (arg == QLatin1String("--gui") || arg == QLatin1String("-g")) {
            return true;
        }
    }
    return false;
}

void detachConsoleWindowForGui()
{
#if defined(Q_OS_WIN)
    if (GetConsoleWindow() != nullptr) {
        FreeConsole();
    }
#endif
}
QString defaultExportJsonOutputPath(const QString &inputFilename)
{
    if (inputFilename.endsWith(QStringLiteral(".db"), Qt::CaseInsensitive)) {
        return inputFilename.left(inputFilename.length() - 3) + QStringLiteral(".export.json");
    }
    return inputFilename + QStringLiteral(".export.json");
}

QString resolveInputFilename(const QCommandLineParser &parser,
                             const ExportCommandLineOptions &options,
                             bool requireInput)
{
    if (parser.isSet(options.inputOption)) {
        return parser.value(options.inputOption);
    }

    const QStringList positionalArguments = parser.positionalArguments();
    if (positionalArguments.count() == 1) {
        return positionalArguments.constFirst();
    }
    if (!requireInput && positionalArguments.isEmpty()) {
        return QString();
    }
    return QString();
}

bool parsePositiveOption(const QCommandLineParser &parser,
                         const QCommandLineOption &option,
                         qint32 *outputValue,
                         QString *errorMessage)
{
    if (!parser.isSet(option)) {
        return true;
    }

    bool ok = false;
    const qint32 parsedValue = parser.value(option).toInt(&ok);
    if (!ok || parsedValue < 1) {
        if (errorMessage) {
            *errorMessage = QObject::tr("Invalid --%1 value: %2")
                                .arg(option.names().constFirst(), parser.value(option));
        }
        return false;
    }
    if (outputValue) {
        *outputValue = parsedValue;
    }
    return true;
}
} // namespace

int main(int argc, char *argv[])
{
    // Set 'binary mode' for stdin and stdout on Windows
    setBinaryMode();

    // Install the local debug message handler
    setDebug(true);
    qInstallMessageHandler(debugOutputHandler);
    if (wantsGui(argc, argv)) {
        detachConsoleWindowForGui();
        tbc::ui::prepareStockThemeEnvironment();
        tbc::ui::ThemedApplication a(argc, argv);

        // Set application name and version
        QCoreApplication::setApplicationName("tbc-export-metadata");
        QCoreApplication::setApplicationVersion(QString("tbc-tools - Branch: %1 / Commit: %2").arg(APP_BRANCH, APP_COMMIT));
        QCoreApplication::setOrganizationDomain("domesday86.com");

        // Set up the command line parser
        QCommandLineParser parser;
        parser.setApplicationDescription(
                    "tbc-export-metadata - Export ld-decode metadata into other formats\n"
                    "\n"
                    "(c)2020-2023 Adam Sampson\n"
                    "(c)2021-2025 Simon Inns\n"
                    "GPLv3 Open-Source - github: https://github.com/happycube/ld-decode");
        parser.addHelpOption();
        parser.addVersionOption();

        addStandardDebugOptions(parser);
        const ExportCommandLineOptions options;
        options.addToParser(parser);

        parser.addPositionalArgument("input", QCoreApplication::translate("main", "Specify input metadata file"));
        parser.process(a);
        processStandardDebugOptions(parser);
        // Apply the stock theme (dark by default, light via --light-theme). Sets
        // the Fusion palette, isDarkTheme property, Qt 6.8 color scheme override,
        // and input-widget contrast guard; re-asserted on macOS switchover.
        if (parser.isSet(options.lightThemeOption)) {
            a.applyStockLightTheme();
        } else {
            a.applyStockDarkTheme();
        }

        MetadataExportDialog::InitialOptions initialOptions;
        initialOptions.inputFile = resolveInputFilename(parser, options, false);
        initialOptions.exportVitsCsv = parser.isSet(options.writeVitsCsvOption);
        initialOptions.exportVbiCsv = parser.isSet(options.writeVbiCsvOption);
        initialOptions.exportUserMarkersTxt = parser.isSet(options.writeUserMarkersTxtOption);
        initialOptions.exportUserMarkersCsv = parser.isSet(options.writeUserMarkersCsvOption);
        initialOptions.exportAudacityLabels = parser.isSet(options.writeAudacityLabelsOption);
        initialOptions.exportFfmetadata = parser.isSet(options.writeFfmetadataOption);
        initialOptions.exportFfmpegVitc = parser.isSet(options.writeFfmpegVitcOption);
        initialOptions.exportFfmetadataVitcTimecode = !parser.isSet(options.ffmetadataNoVitcTimecodeOption);
        initialOptions.exportClosedCaptions = parser.isSet(options.writeClosedCaptionsOption);
        parsePositiveOption(parser, options.ffmetadataStartOption, &initialOptions.ffmetadataStart, nullptr);
        parsePositiveOption(parser, options.ffmetadataLengthOption, &initialOptions.ffmetadataLength, nullptr);
        initialOptions.debug = parser.isSet(QStringLiteral("debug"));
        initialOptions.quiet = parser.isSet(QStringLiteral("quiet")) && !initialOptions.debug;
        std::unique_ptr<QWindow> transientParentWindow;
        if (parser.isSet(options.parentWindowIdOption)) {
            bool ok = false;
            const qulonglong parentWindowId = parser.value(options.parentWindowIdOption).toULongLong(&ok, 0);
            if (ok && parentWindowId != 0) {
                transientParentWindow.reset(QWindow::fromWinId(static_cast<WId>(parentWindowId)));
            }
        }

        MetadataExportDialog dialog;
        dialog.setInitialOptions(initialOptions);
        if (transientParentWindow) {
            if (!dialog.windowHandle()) {
                dialog.winId();
            }
            if (dialog.windowHandle()) {
                dialog.windowHandle()->setTransientParent(transientParentWindow.get());
            }
        }
        dialog.show();
        dialog.raise();
        dialog.activateWindow();

        return a.exec();
    }

    QCoreApplication a(argc, argv);

    // Set application name and version
    QCoreApplication::setApplicationName("tbc-export-metadata");
    QCoreApplication::setApplicationVersion(QString("tbc-tools - Branch: %1 / Commit: %2").arg(APP_BRANCH, APP_COMMIT));
    QCoreApplication::setOrganizationDomain("domesday86.com");

    // Set up the command line parser
    QCommandLineParser parser;
    parser.setApplicationDescription(
                "tbc-export-metadata - Export ld-decode metadata into other formats\n"
                "\n"
                "(c)2020-2023 Adam Sampson\n"
                "(c)2021-2025 Simon Inns\n"
                "GPLv3 Open-Source - github: https://github.com/happycube/ld-decode");
    parser.addHelpOption();
    parser.addVersionOption();

    // -- General options --

    // Add the standard debug options --debug and --quiet
    addStandardDebugOptions(parser);
    const ExportCommandLineOptions options;
    options.addToParser(parser);

    // -- Positional arguments --

    // Positional argument to specify input metadata file
    parser.addPositionalArgument("input", QCoreApplication::translate("main", "Specify input metadata file"));

    // Process the command line options and arguments given by the user
    parser.process(a);

    // Standard logging options
    processStandardDebugOptions(parser);

    // Get the arguments from the parser
    const QString inputFileName = resolveInputFilename(parser, options, true);
    if (inputFileName.isEmpty()) {
        qCritical("You must specify the input metadata file");
        return 1;
    }
    const bool exportJsonRequested = parser.isSet(options.exportJsonOption) || parser.isSet(options.outputJsonOption);
    const bool hasFormatExports = parser.isSet(options.writeVitsCsvOption)
                                  || parser.isSet(options.writeVbiCsvOption)
                                  || parser.isSet(options.writeUserMarkersTxtOption)
                                  || parser.isSet(options.writeUserMarkersCsvOption)
                                  || parser.isSet(options.writeAudacityLabelsOption)
                                  || parser.isSet(options.writeFfmetadataOption)
                                  || parser.isSet(options.writeFfmpegVitcOption)
                                  || parser.isSet(options.writeClosedCaptionsOption);

    if (!exportJsonRequested && !hasFormatExports) {
        qCritical("You must specify at least one output option");
        return 1;
    }

    if (exportJsonRequested) {
        QString outputJsonFileName = parser.value(options.outputJsonOption).trimmed();
        if (outputJsonFileName.isEmpty()) {
            outputJsonFileName = defaultExportJsonOutputPath(inputFileName);
        }
        qInfo() << "Beginning SQLite DB to export JSON processing...";
        MetadataConverter metadataConverter(inputFileName, outputJsonFileName);
        if (!metadataConverter.process()) {
            return 1;
        }
    }

    if (!hasFormatExports) {
        return 0;
    }

    // Load the source video metadata
    TbcMetaData metaData;
    if (!metaData.read(inputFileName)) {
        qInfo() << "Unable to read metadata file";
        return 1;
    }

    // Write the selected output files
    if (parser.isSet(options.writeVitsCsvOption)) {
        const QString &fileName = parser.value(options.writeVitsCsvOption);
        if (!writeVitsCsv(metaData, fileName)) {
            qCritical() << "Failed to write output file:" << fileName;
            return 1;
        }
    }
    if (parser.isSet(options.writeVbiCsvOption)) {
        const QString &fileName = parser.value(options.writeVbiCsvOption);
        if (!writeVbiCsv(metaData, fileName)) {
            qCritical() << "Failed to write output file:" << fileName;
            return 1;
        }
    }
    if (parser.isSet(options.writeUserMarkersTxtOption)) {
        const QString &fileName = parser.value(options.writeUserMarkersTxtOption);
        if (!writeUserMarkersTxt(metaData, fileName)) {
            qCritical() << "Failed to write output file:" << fileName;
            return 1;
        }
    }
    if (parser.isSet(options.writeUserMarkersCsvOption)) {
        const QString &fileName = parser.value(options.writeUserMarkersCsvOption);
        if (!writeUserMarkersCsv(metaData, fileName)) {
            qCritical() << "Failed to write output file:" << fileName;
            return 1;
        }
    }
    if (parser.isSet(options.writeAudacityLabelsOption)) {
        const QString &fileName = parser.value(options.writeAudacityLabelsOption);
        if (!writeAudacityLabels(metaData, fileName)) {
            qCritical() << "Failed to write output file:" << fileName;
            return 1;
        }
    }
    if (parser.isSet(options.writeFfmetadataOption)) {
        const QString &fileName = parser.value(options.writeFfmetadataOption);
        qint32 startFrame = -1;
        qint32 lengthFrames = -1;
        const bool includeVitcTimecode = !parser.isSet(options.ffmetadataNoVitcTimecodeOption);
        QString errorMessage;
        if (!parsePositiveOption(parser, options.ffmetadataStartOption, &startFrame, &errorMessage)
            || !parsePositiveOption(parser, options.ffmetadataLengthOption, &lengthFrames, &errorMessage)) {
            qCritical() << errorMessage;
            return 1;
        }
        if (!writeFfmetadata(metaData, fileName, startFrame, lengthFrames, includeVitcTimecode)) {
            qCritical() << "Failed to write output file:" << fileName;
            return 1;
        }
    }
    if (parser.isSet(options.writeFfmpegVitcOption)) {
        const QString &fileName = parser.value(options.writeFfmpegVitcOption);
        if (!writeVitcFfmetadataText(metaData, fileName)) {
            qCritical() << "Failed to write output file:" << fileName;
            return 1;
        }
    }
    if (parser.isSet(options.writeClosedCaptionsOption)) {
        const QString &fileName = parser.value(options.writeClosedCaptionsOption);
        if (!writeClosedCaptions(metaData, fileName)) {
            qCritical() << "Failed to write output file:" << fileName;
            return 1;
        }
    }

    // Quit with success
    return 0;
}
