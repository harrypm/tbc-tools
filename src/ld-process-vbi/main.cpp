/************************************************************************

    main.cpp

    ld-process-vbi - VBI and IEC NTSC specific processor for ld-decode
    Copyright (C) 2018-2026 Simon Inns
    Copyright (C) 2026 Harry Munday

    This file is part of tbc-tools.

    ld-process-vbi is free software: you can redistribute it and/or
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

#include <QCoreApplication>
#include <QDebug>
#include <QtGlobal>
#include <QCommandLineParser>
#include <QThread>
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QDateTime>

#include "tbc/logging.h"
#include "decoderpool.h"
#include "teletextintegration.h"
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

int main(int argc, char *argv[])
{
    // Set 'binary mode' for stdin and stdout on Windows
    setBinaryMode();

    // Install the local debug message handler
    setDebug(true);
    qInstallMessageHandler(debugOutputHandler);

    QCoreApplication a(argc, argv);

    // Set application name and version
    QCoreApplication::setApplicationName("ld-process-vbi");
    QCoreApplication::setApplicationVersion(QString("tbc-tools - Branch: %1 / Commit: %2").arg(APP_BRANCH, APP_COMMIT));
    QCoreApplication::setOrganizationDomain("domesday86.com");

    // Set up the command line parser
    QCommandLineParser parser;
    parser.setApplicationDescription(
                "ld-process-vbi - VBI and IEC NTSC specific processor for ld-decode\n"
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
                                                                        "Disable integrated teletext HTML export"));
    parser.addOption(noTeletextHtmlOption);
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
    DecoderPool decoderPool(inputFilename, outputMetadataFilename, maxThreads, metaData);
    if (!decoderPool.process()) return 1;

    const bool teletextExportEnabled = !parser.isSet(noTeletextHtmlOption);
    if (teletextExportEnabled) {
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
            qCritical().noquote() << "Teletext export failed:" << teletextError;
            return 1;
        }
    }

    // Quit with success
    return 0;
}
