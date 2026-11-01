/************************************************************************

    main.cpp

    tbc-process-vbi - VBI and IEC NTSC specific processor for ld-decode
    Copyright (C) 2018-2026 Simon Inns
    Copyright (C) 2026 Harry Munday

    This file is part of tbc-tools.

    tbc-process-vbi is free software: you can redistribute it and/or
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

// Single binary with two modes:
//  - CLI (default when processing arguments are given): headless VBI/VITS
//    processing, command-line compatible with the former standalone
//    ld-process-vbi tool
//  - GUI: the standalone VBI/VITS processing window, used when the binary is
//    launched bare (no arguments) or with --gui. The GUI spawns this same
//    binary in CLI mode to perform the processing.

#include <QApplication>
#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDebug>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStringList>
#include <QThread>
#include <QtGlobal>

#include "decoderpool.h"
#include "processvbidialog.h"
#include "processingpool.h"
#include "teletextintegration.h"
#include "tbc/logging.h"
#include "tbc/uistyle.h"

namespace {
void appendMetadataCandidate(QStringList &candidates, const QString &candidate)
{
    if (candidate.isEmpty()) {
        return;
    }
    if (!candidates.contains(candidate)) {
        candidates << candidate;
    }
}

QStringList metadataCandidatesForInput(const QString &inputFilename)
{
    QStringList candidates;
    appendMetadataCandidate(candidates, inputFilename + QStringLiteral(".db"));
    appendMetadataCandidate(candidates, inputFilename + QStringLiteral(".json"));

    const QFileInfo inputInfo(inputFilename);
    const QString basePath = QDir(inputInfo.absolutePath()).filePath(inputInfo.completeBaseName());
    appendMetadataCandidate(candidates, basePath + QStringLiteral(".db"));
    appendMetadataCandidate(candidates, basePath + QStringLiteral(".json"));

    const QString suffix = inputInfo.suffix().toLower();
    if (suffix == QStringLiteral("ytbc")
        || suffix == QStringLiteral("ctbc")
        || suffix == QStringLiteral("tbcy")
        || suffix == QStringLiteral("tbcc")) {
        appendMetadataCandidate(candidates, basePath + QStringLiteral(".tbc.db"));
        appendMetadataCandidate(candidates, basePath + QStringLiteral(".tbc.json"));
    }

    return candidates;
}

QString resolveDefaultMetadataFilename(const QString &inputFilename)
{
    const QStringList candidates = metadataCandidatesForInput(inputFilename);
    for (const QString &candidate : candidates) {
        if (QFileInfo::exists(candidate)) {
            return candidate;
        }
    }
    return candidates.isEmpty() ? (inputFilename + QStringLiteral(".db")) : candidates.first();
}

QString backupFilenameForMetadata(const QString &inputMetadataFilename, const QString &backupSuffix)
{
    const QString defaultBackupFilename = inputMetadataFilename + backupSuffix;
    if (!QFileInfo::exists(defaultBackupFilename)) {
        return defaultBackupFilename;
    }

    const QString timestamp = QDateTime::currentDateTimeUtc().toString(QStringLiteral("yyyyMMdd_HHmmss"));
    QString backupFilename = inputMetadataFilename + QStringLiteral(".") + timestamp + backupSuffix;
    qint32 collisionCounter = 1;
    while (QFileInfo::exists(backupFilename)) {
        backupFilename = inputMetadataFilename + QStringLiteral(".") + timestamp
                         + QStringLiteral("_") + QString::number(collisionCounter) + backupSuffix;
        collisionCounter++;
    }
    return backupFilename;
}

QString defaultTeletextHtmlDirectoryForInput(const QString &inputFilename)
{
    const QFileInfo inputInfo(inputFilename);
    return QDir(inputInfo.absolutePath())
        .filePath(inputInfo.completeBaseName() + QStringLiteral("_teletext_html"));
}
}

static int runCli(int argc, char *argv[])
{
    QCoreApplication a(argc, argv);

    // Set application name and version
    QCoreApplication::setApplicationName("tbc-process-vbi");
    QCoreApplication::setApplicationVersion(QString("tbc-tools - Branch: %1 / Commit: %2").arg(APP_BRANCH, APP_COMMIT));
    QCoreApplication::setOrganizationDomain("domesday86.com");

    // Set up the command line parser
    QCommandLineParser parser;
    parser.setApplicationDescription(
                "tbc-process-vbi - VBI and IEC NTSC specific processor for ld-decode\n"
                "\n"
                "(c)2018-2026 Simon Inns, Harry Munday\n"
                "GPLv3 Open-Source - github: https://github.com/happycube/ld-decode");
    parser.addHelpOption();
    parser.addVersionOption();

    // Add the standard debug options --debug and --quiet
    addStandardDebugOptions(parser);

    // Option to specify a different metadata input file
    QCommandLineOption inputMetadataOption(QStringList() << "input-metadata",
                                       QCoreApplication::translate("main", "Specify the input metadata file (default auto-detect .db/.json)"),
                                       QCoreApplication::translate("main", "filename"));
    parser.addOption(inputMetadataOption);
    QCommandLineOption inputJsonOption(QStringList() << "input-json",
                                       QCoreApplication::translate("main", "Specify the input metadata file (legacy alias for --input-metadata)"),
                                       QCoreApplication::translate("main", "filename"));
    parser.addOption(inputJsonOption);

    // Option to specify a different metadata output file
    QCommandLineOption outputMetadataOption(QStringList() << "output-metadata",
                                        QCoreApplication::translate("main", "Specify the output metadata file (default same as input)"),
                                        QCoreApplication::translate("main", "filename"));
    parser.addOption(outputMetadataOption);
    QCommandLineOption outputJsonOption(QStringList() << "output-json",
                                        QCoreApplication::translate("main", "Specify the output metadata file (legacy alias for --output-metadata)"),
                                        QCoreApplication::translate("main", "filename"));
    parser.addOption(outputJsonOption);

    // Option to disable metadata back-up (-n)
    QCommandLineOption showNoBackupOption(QStringList() << "n" << "nobackup",
                                       QCoreApplication::translate("main", "Do not create a backup of the input metadata"));
    parser.addOption(showNoBackupOption);

    // Option to select the number of threads (-t)
    QCommandLineOption threadsOption(QStringList() << "t" << "threads",
                                        QCoreApplication::translate("main", "Specify the number of concurrent threads (default is the number of logical CPUs)"),
                                        QCoreApplication::translate("main", "number"));
    parser.addOption(threadsOption);

    // Teletext HTML export integration
    QCommandLineOption teletextHtmlDirOption(QStringList() << "teletext-html-dir",
                                             QCoreApplication::translate("main",
                                                                         "Decode teletext and write generated HTML pages to the specified directory (default: <input>_teletext_html beside input TBC)"),
                                             QCoreApplication::translate("main", "directory"));
    parser.addOption(teletextHtmlDirOption);
    QCommandLineOption noTeletextHtmlOption(QStringList() << "no-teletext-html",
                                            QCoreApplication::translate("main",
                                                                        "Disable integrated teletext HTML export (legacy alias; teletext is opt-in via --teletext)"));
    parser.addOption(noTeletextHtmlOption);
    QCommandLineOption teletextOption(QStringList() << "teletext",
                                      QCoreApplication::translate("main",
                                                                  "Enable integrated teletext HTML export (off by default; failure is non-fatal)"));
    parser.addOption(teletextOption);
    QCommandLineOption teletextTapeFormatOption(QStringList() << "teletext-tape-format",
                                                QCoreApplication::translate("main",
                                                                            "Teletext decoder tape format profile (default: vhs)"),
                                                QCoreApplication::translate("main", "name"),
                                                QCoreApplication::translate("main", "vhs"));
    parser.addOption(teletextTapeFormatOption);
    QCommandLineOption teletextMinDuplicatesOption(QStringList() << "teletext-min-duplicates",
                                                   QCoreApplication::translate("main",
                                                                               "Minimum duplicate subpages used when squashing teletext packets (default: 1)"),
                                                   QCoreApplication::translate("main", "number"),
                                                   QCoreApplication::translate("main", "1"));
    parser.addOption(teletextMinDuplicatesOption);

    // VBI processing options - select which data types to decode (default: the four
    // in-process VBI types on; teletext and VITS are opt-in via --teletext/--vits).
    QCommandLineOption noVbiCoreOption(QStringList() << "no-vbi-core",
                                       QCoreApplication::translate("main",
                                                                   "Disable biphase VBI decoding (frame number/timecode/chapter/user code on lines 16-18)"));
    parser.addOption(noVbiCoreOption);
    QCommandLineOption noNtscOption(QStringList() << "no-ntsc",
                                    QCoreApplication::translate("main",
                                                                "Disable NTSC-specific decoding (FM code/white flag/video ID)"));
    parser.addOption(noNtscOption);
    QCommandLineOption noVitcOption(QStringList() << "no-vitc",
                                    QCoreApplication::translate("main",
                                                                "Disable VITC (vertical interval timecode) decoding"));
    parser.addOption(noVitcOption);
    QCommandLineOption noClosedCaptionsOption(QStringList() << "no-closed-captions",
                                              QCoreApplication::translate("main",
                                                                          "Disable closed caption (CEA-608) decoding"));
    parser.addOption(noClosedCaptionsOption);
    QCommandLineOption vitsOption(QStringList() << "vits",
                                  QCoreApplication::translate("main",
                                                              "Enable VITS metrics processing (off by default; runs the VITS analyser in-process; failure is non-fatal)"));
    parser.addOption(vitsOption);

    // Positional argument to specify input TBC file
    parser.addPositionalArgument("input", QCoreApplication::translate("main", "Specify input TBC file"));

    // Process the command line options and arguments given by the user
    parser.process(a);

    // Standard logging options
    processStandardDebugOptions(parser);

    // Get the options from the parser
    bool noBackup = parser.isSet(showNoBackupOption);

    qint32 maxThreads = QThread::idealThreadCount();
    if (parser.isSet(threadsOption)) {
        maxThreads = parser.value(threadsOption).toInt();

        if (maxThreads < 1) {
            // Quit with error
            qCritical("Specified number of threads must be greater than zero");
            return -1;
        }
    }

    qint32 teletextMinDuplicates = parser.value(teletextMinDuplicatesOption).toInt();
    if (teletextMinDuplicates < 1) {
        qCritical("Specified teletext minimum duplicates must be greater than zero");
        return -1;
    }

    // Build the VBI processing options from the CLI flags. Defaults match the
    // struct defaults (four in-process decoders on; teletext/VITS off). The
    // legacy --no-teletext-html alias forces teletext off for backward compat.
    VbiProcessingOptions processingOptions;
    if (parser.isSet(noVbiCoreOption)) processingOptions.vbiCore = false;
    if (parser.isSet(noNtscOption)) processingOptions.ntsc = false;
    if (parser.isSet(noVitcOption)) processingOptions.vitc = false;
    if (parser.isSet(noClosedCaptionsOption)) processingOptions.closedCaptions = false;
    if (parser.isSet(vitsOption)) processingOptions.vits = true;
    processingOptions.teletext = parser.isSet(teletextOption);
    if (parser.isSet(noTeletextHtmlOption)) processingOptions.teletext = false;

    // Get the arguments from the parser
    QString inputFilename;
    QStringList positionalArguments = parser.positionalArguments();
    if (positionalArguments.count() == 1) {
        inputFilename = positionalArguments.at(0);
    } else {
        // Quit with error
        qCritical("You must specify an input TBC file");
        return -1;
    }

    // Work out the metadata filenames
    QString inputMetadataFilename;
    if (parser.isSet(inputMetadataOption) && parser.isSet(inputJsonOption)) {
        qCritical("Specify only one of --input-metadata or --input-json");
        return -1;
    }
    if (parser.isSet(outputMetadataOption) && parser.isSet(outputJsonOption)) {
        qCritical("Specify only one of --output-metadata or --output-json");
        return -1;
    }

    if (parser.isSet(inputMetadataOption)) {
        inputMetadataFilename = parser.value(inputMetadataOption);
    } else if (parser.isSet(inputJsonOption)) {
        inputMetadataFilename = parser.value(inputJsonOption);
    } else {
        inputMetadataFilename = resolveDefaultMetadataFilename(inputFilename);
    }
    QString outputMetadataFilename = inputMetadataFilename;
    if (parser.isSet(outputMetadataOption)) {
        outputMetadataFilename = parser.value(outputMetadataOption);
    } else if (parser.isSet(outputJsonOption)) {
        outputMetadataFilename = parser.value(outputJsonOption);
    }

    // Open the source video metadata
    TbcMetaData metaData;
    qInfo().nospace().noquote() << "Reading metadata from " << inputMetadataFilename;
    if (!metaData.read(inputMetadataFilename)) {
        qCritical() << "Unable to open TBC metadata file";
        return 1;
    }

    // If we're overwriting the input metadata file, back it up first
    if (inputMetadataFilename == outputMetadataFilename && !noBackup) {
        const QString backupFilename = backupFilenameForMetadata(inputMetadataFilename, QStringLiteral(".bup"));
        qInfo().nospace().noquote() << "Backing up metadata to " << backupFilename;
        if (!QFile::copy(inputMetadataFilename, backupFilename)) {
            qCritical() << "Unable to back-up input metadata file";
            return 1;
        }
    }

    // Perform the processing
    qInfo() << "Beginning VBI processing...";
    DecoderPool decoderPool(inputFilename, outputMetadataFilename, maxThreads, metaData, processingOptions);
    if (!decoderPool.process()) return 1;

    if (processingOptions.teletext) {
        TeletextIntegrationOptions teletextOptions;
        teletextOptions.inputFilename = inputFilename;
        if (parser.isSet(teletextHtmlDirOption)) {
            teletextOptions.outputDirectory = parser.value(teletextHtmlDirOption);
        } else {
            teletextOptions.outputDirectory = defaultTeletextHtmlDirectoryForInput(inputFilename);
        }
        teletextOptions.maxThreads = maxThreads;
        teletextOptions.tapeFormat = parser.value(teletextTapeFormatOption);
        teletextOptions.minDuplicates = teletextMinDuplicates;
        qInfo().nospace().noquote() << "Teletext export output directory: " << teletextOptions.outputDirectory;

        QString teletextError;
        if (!runTeletextHtmlExport(teletextOptions, &teletextError)) {
            // Non-fatal: VBI metadata is already written. Warn and continue so the
            // run still exits 0 (teletext is an opt-in post-step most tapes lack).
            qWarning().noquote() << "Teletext export failed (non-fatal, VBI metadata preserved):" << teletextError;
        }
    }

    // VITS processing mode (opt-in). Runs the shared VITS analyser in-process on
    // the same metadata so tbc-process-vbi is the single VBI/VITS processing tool.
    if (processingOptions.vits) {
        qInfo() << "Beginning VITS processing (in-process mode)...";
        ProcessingPool vitsPool(inputFilename, outputMetadataFilename, maxThreads, metaData);
        if (!vitsPool.process()) {
            // Non-fatal: VBI metadata is already written.
            qWarning() << "VITS processing failed (non-fatal, VBI metadata preserved)";
        }
    }

    // Quit with success
    return 0;
}

static int runGui(int argc, char *argv[])
{
    tbc::ui::prepareStockThemeEnvironment();

    tbc::ui::ThemedApplication app(argc, argv);

    QCoreApplication::setApplicationName("tbc-process-vbi");
    QCoreApplication::setApplicationVersion(
        QString("tbc-tools - Branch: %1 / Commit: %2").arg(APP_BRANCH, APP_COMMIT));
    QCoreApplication::setOrganizationDomain("github.com");

    QCommandLineParser parser;
    parser.setApplicationDescription(
        "tbc-process-vbi - standalone GUI for VBI + VITS processing\n"
        "\n"
        "(c)2026 Harry Munday\n"
        "GPLv3 Open-Source - github: https://github.com/harrypm/tbc-tools");
    parser.addHelpOption();
    parser.addVersionOption();

    // Add the standard debug options --debug and --quiet
    addStandardDebugOptions(parser);

    // Mode flag accepted for explicitness; main() dispatches on it before this
    // parser runs. Processing without the GUI is available by passing the CLI
    // processing arguments instead (see the CLI mode help).
    parser.addOption(QCommandLineOption(QStringLiteral("gui"),
                    QCoreApplication::translate("main",
                                                "Open the processing GUI (default when no processing arguments are given)")));

    QCommandLineOption sourceDirectoryOption(QStringList() << "source-dir",
                                             QCoreApplication::translate(
                                                 "main", "Initial source directory used by browse dialogs"),
                                             QCoreApplication::translate("main", "path"));
    parser.addOption(sourceDirectoryOption);

    QCommandLineOption inputTbcOption(QStringList() << "input-tbc",
                                      QCoreApplication::translate(
                                          "main", "Prefill the input TBC file"),
                                      QCoreApplication::translate("main", "filename"));
    parser.addOption(inputTbcOption);

    QCommandLineOption outputMetadataOption(QStringList() << "output-metadata",
                                            QCoreApplication::translate(
                                                "main", "Prefill the output metadata file (defaults to the input's auto-detected sidecar)"),
                                            QCoreApplication::translate("main", "filename"));
    parser.addOption(outputMetadataOption);

    parser.addOption(QCommandLineOption("force-dark-theme",
                                        QCoreApplication::translate("main", "Pin the dark Fusion theme instead of following the system light/dark setting")));
    parser.addOption(QCommandLineOption("light-theme",
                                        QCoreApplication::translate("main", "Pin the light Fusion theme instead of following the system light/dark setting")));

    parser.addPositionalArgument(
        "input",
        QCoreApplication::translate("main",
                                    "Optional input TBC file to preload"),
        "[input]");

    parser.process(app);
    processStandardDebugOptions(parser);

    // Apply the stock theme: follow the OS light/dark colour scheme by
    // default (dark when unknown), or pin light/dark via the explicit flags.
    if (parser.isSet(QStringLiteral("light-theme"))) {
        app.applyStockLightTheme();
    } else if (parser.isSet(QStringLiteral("force-dark-theme"))) {
        app.applyStockDarkTheme();
    } else {
        app.applyStockThemeFollowingSystem();
    }

    ProcessVbiDialog dialog;

    if (parser.isSet(sourceDirectoryOption)) {
        dialog.setSourceDirectory(parser.value(sourceDirectoryOption));
    }

    if (parser.isSet(inputTbcOption)) {
        dialog.setDefaultInputTbc(parser.value(inputTbcOption));
    }

    if (parser.isSet(outputMetadataOption)) {
        dialog.setDefaultOutputMetadata(parser.value(outputMetadataOption));
    }

    const QStringList positionalInputs = parser.positionalArguments();
    for (const QString &positionalInput : positionalInputs) {
        // Only preload the first positional argument as the TBC input.
        dialog.setDefaultInputTbc(positionalInput);
        break;
    }

    dialog.exec();
    return 0;
}

int main(int argc, char *argv[])
{
    // Set 'binary mode' for stdin and stdout on Windows
    setBinaryMode();

    // Install the local debug message handler
    setDebug(true);
    qInstallMessageHandler(debugOutputHandler);

    // Mode dispatch: GUI when launched bare (no arguments) or with --gui;
    // otherwise CLI processing (compatible with the former ld-process-vbi).
    bool guiMode = (argc <= 1);
    for (int i = 1; i < argc; ++i) {
        if (QString::fromLocal8Bit(argv[i]) == QLatin1String("--gui")) {
            guiMode = true;
            break;
        }
    }

    return guiMode ? runGui(argc, argv) : runCli(argc, argv);
}
