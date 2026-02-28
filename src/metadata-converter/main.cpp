/******************************************************************************
 * main.cpp
 * tbc-metadata-converter - Metadata converter tool for ld-decode
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025 Simon Inns
 *
 * This file is part of ld-decode-tools.
 ******************************************************************************/

#include <QApplication>
#include <QCoreApplication>
#include <QDebug>
#include <QtGlobal>
#include <QCommandLineParser>
#include <QThread>

#include "tbc/logging.h"
#include "jsonconverter.h"
#include "metadataconversiondialog.h"

namespace {
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
} // namespace

int main(int argc, char *argv[])
{
    // Set 'binary mode' for stdin and stdout on Windows
    setBinaryMode();

    // Install the local debug message handler
    setDebug(true);
    qInstallMessageHandler(debugOutputHandler);
    if (wantsGui(argc, argv)) {
        QApplication a(argc, argv);

        QCoreApplication::setApplicationName("tbc-metadata-converter");
        QCoreApplication::setApplicationVersion(QString("ld-decode-tools - Branch: %1 / Commit: %2").arg(APP_BRANCH, APP_COMMIT));
        QCoreApplication::setOrganizationDomain("domesday86.com");

        QCommandLineParser parser;
        parser.setApplicationDescription(
                    "tbc-metadata-converter - Metadata converter tool for ld-decode\n"
                    "\n"
                    "(c)2025 Simon Inns\n"
                    "GPLv3 Open-Source - github: https://github.com/happycube/ld-decode");
        parser.addHelpOption();
        parser.addVersionOption();

        addStandardDebugOptions(parser);

        QCommandLineOption guiOption(QStringList() << "g" << "gui",
                                     QCoreApplication::translate("main", "Launch the GUI (default when no arguments are provided)."));
        parser.addOption(guiOption);

        QCommandLineOption directionOption(QStringList() << "direction",
                                           QCoreApplication::translate("main", "Conversion direction: json-to-sqlite or sqlite-to-json (default json-to-sqlite)"),
                                           QCoreApplication::translate("main", "direction"),
                                           "json-to-sqlite");
        parser.addOption(directionOption);

        QCommandLineOption inputJsonOption(QStringList() << "input-json",
                                           QCoreApplication::translate("main", "Specify the input JSON file"),
                                           QCoreApplication::translate("main", "filename"));
        parser.addOption(inputJsonOption);

        QCommandLineOption inputSqliteOption(QStringList() << "input-sqlite",
                                             QCoreApplication::translate("main", "Specify the input SQLite file"),
                                             QCoreApplication::translate("main", "filename"));
        parser.addOption(inputSqliteOption);

        QCommandLineOption outputSqliteOption(QStringList() << "output-sqlite",
                                              QCoreApplication::translate("main", "Specify the output SQLite file (default same as input but with .db extension)"),
                                              QCoreApplication::translate("main", "filename"));
        parser.addOption(outputSqliteOption);

        QCommandLineOption outputJsonOption(QStringList() << "output-json",
                                            QCoreApplication::translate("main", "Specify the output JSON file (default same as input but with .json extension)"),
                                            QCoreApplication::translate("main", "filename"));
        parser.addOption(outputJsonOption);

        parser.process(a);
        processStandardDebugOptions(parser);

        QString inputFilename;
        if (parser.isSet(inputJsonOption)) {
            inputFilename = parser.value(inputJsonOption);
        } else if (parser.isSet(inputSqliteOption)) {
            inputFilename = parser.value(inputSqliteOption);
        } else {
            const QStringList positionalArguments = parser.positionalArguments();
            if (positionalArguments.count() == 1) {
                inputFilename = positionalArguments.at(0);
            }
        }

        MetadataConversionDialog dialog;
        if (!inputFilename.isEmpty()) {
            dialog.setDefaultInput(inputFilename);
        }
        dialog.exec();
        return 0;
    }

    QCoreApplication a(argc, argv);

    // Set application name and version
    QCoreApplication::setApplicationName("tbc-metadata-converter");
    QCoreApplication::setApplicationVersion(QString("ld-decode-tools - Branch: %1 / Commit: %2").arg(APP_BRANCH, APP_COMMIT));
    QCoreApplication::setOrganizationDomain("domesday86.com");

    // Set up the command line parser
    QCommandLineParser parser;
    parser.setApplicationDescription(
                "tbc-metadata-converter - Metadata converter tool for ld-decode\n"
                "\n"
                "(c)2025 Simon Inns\n"
                "GPLv3 Open-Source - github: https://github.com/happycube/ld-decode");
    parser.addHelpOption();
    parser.addVersionOption();

    // Add the standard debug options --debug and --quiet
    addStandardDebugOptions(parser);

    // Option to launch the GUI
    QCommandLineOption guiOption(QStringList() << "g" << "gui",
                                 QCoreApplication::translate("main", "Launch the GUI (default when no arguments are provided)."));
    parser.addOption(guiOption);

    // Option to specify conversion direction
    QCommandLineOption directionOption(QStringList() << "direction",
                                       QCoreApplication::translate("main", "Conversion direction: json-to-sqlite or sqlite-to-json (default json-to-sqlite)"),
                                       QCoreApplication::translate("main", "direction"),
                                       "json-to-sqlite");
    parser.addOption(directionOption);

    // Option to specify a JSON input file
    QCommandLineOption inputJsonOption(QStringList() << "input-json",
                                       QCoreApplication::translate("main", "Specify the input JSON file"),
                                       QCoreApplication::translate("main", "filename"));
    parser.addOption(inputJsonOption);

    // Option to specify an SQLite input file
    QCommandLineOption inputSqliteOption(QStringList() << "input-sqlite",
                                         QCoreApplication::translate("main", "Specify the input SQLite file"),
                                         QCoreApplication::translate("main", "filename"));
    parser.addOption(inputSqliteOption);

    // Option to specify a SQLite DB output file
    QCommandLineOption outputSqliteOption(QStringList() << "output-sqlite",
                                          QCoreApplication::translate("main", "Specify the output SQLite file (default same as input but with .db extension)"),
                                          QCoreApplication::translate("main", "filename"));
    parser.addOption(outputSqliteOption);

    // Option to specify a JSON output file
    QCommandLineOption outputJsonOption(QStringList() << "output-json",
                                        QCoreApplication::translate("main", "Specify the output JSON file (default same as input but with .json extension)"),
                                        QCoreApplication::translate("main", "filename"));
    parser.addOption(outputJsonOption);

    // Process the command line options and arguments given by the user
    parser.process(a);

    // Standard logging options
    processStandardDebugOptions(parser);

    // Resolve conversion direction
    QString directionValue = parser.value(directionOption).toLower();
    JsonConverter::Direction direction;
    if (directionValue == "json-to-sqlite") {
        direction = JsonConverter::Direction::JsonToSqlite;
    } else if (directionValue == "sqlite-to-json") {
        direction = JsonConverter::Direction::SqliteToJson;
    } else {
        qCritical() << "Invalid --direction value:" << directionValue
                    << "(expected json-to-sqlite or sqlite-to-json)";
        return -1;
    }

    if (parser.isSet(inputJsonOption) && parser.isSet(inputSqliteOption)) {
        qCritical("Specify only one input option: --input-json or --input-sqlite");
        return -1;
    }

    if (parser.isSet(outputJsonOption) && parser.isSet(outputSqliteOption)) {
        qCritical("Specify only one output option: --output-json or --output-sqlite");
        return -1;
    }

    // Determine input filename
    QString inputFilename;
    if (direction == JsonConverter::Direction::JsonToSqlite) {
        if (parser.isSet(inputJsonOption)) {
            inputFilename = parser.value(inputJsonOption);
        } else if (parser.isSet(inputSqliteOption)) {
            inputFilename = parser.value(inputSqliteOption);
        }
    } else {
        if (parser.isSet(inputSqliteOption)) {
            inputFilename = parser.value(inputSqliteOption);
        } else if (parser.isSet(inputJsonOption)) {
            inputFilename = parser.value(inputJsonOption);
        }
    }

    if (inputFilename.isEmpty()) {
        QStringList positionalArguments = parser.positionalArguments();
        if (positionalArguments.count() == 1) {
            inputFilename = positionalArguments.at(0);
        } else {
            if (direction == JsonConverter::Direction::JsonToSqlite) {
                qCritical("You must specify an input JSON file using --input-json or as a positional argument");
            } else {
                qCritical("You must specify an input SQLite file using --input-sqlite or as a positional argument");
            }
            return -1;
        }
    }

    // Work out the output filename
    QString outputFilename;
    if (direction == JsonConverter::Direction::JsonToSqlite) {
        if (parser.isSet(outputSqliteOption)) {
            outputFilename = parser.value(outputSqliteOption);
        } else {
            if (inputFilename.endsWith(".json", Qt::CaseInsensitive)) {
                outputFilename = inputFilename.left(inputFilename.length() - 5) + ".db";
            } else {
                outputFilename = inputFilename + ".db";
            }
        }
    } else {
        if (parser.isSet(outputJsonOption)) {
            outputFilename = parser.value(outputJsonOption);
        } else {
            if (inputFilename.endsWith(".db", Qt::CaseInsensitive)) {
                outputFilename = inputFilename.left(inputFilename.length() - 3) + ".json";
            } else {
                outputFilename = inputFilename + ".json";
            }
        }
    }

    // Perform the conversion processing
    qInfo() << "Beginning metadata conversion (" << directionValue << ")...";
    JsonConverter jsonConverter(inputFilename, outputFilename, direction);
    if (!jsonConverter.process()) return 1;

    // Quit with success
    return 0;
}
