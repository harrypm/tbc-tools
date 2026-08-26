/************************************************************************

    main.cpp

    ld-lds-converter - 10-bit .lds to FLAC/s16 converter for ld-decode
    Copyright (C) 2019-2026 Simon Inns

    This file is part of tbc-tools.

    ld-lds-converter is free software: you can redistribute it and/or
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
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QColor>
#include <QPalette>
#include <QSet>
#include <QStringList>
#include <QUrl>
#include <QtGlobal>
#include <cstdio>
#include <clocale>
#ifdef Q_OS_WIN
#include <io.h>
#else
#include <unistd.h>
#endif

#include "tbc/logging.h"
#include "tbc/uistyle.h"
#include "converterdialog.h"
#include "dataconverter.h"

namespace {
void ldLdsConverterMessageHandler(QtMsgType type, const QMessageLogContext &context, const QString &msg)
{
    if (type == QtWarningMsg &&
        msg.contains(QStringLiteral("fromIccProfile: failed size sanity"), Qt::CaseInsensitive)) {
        // Benign warning from malformed ICC data published by some desktop environments.
        return;
    }
    debugOutputHandler(type, context, msg);
}


void ensureUtf8ProcessLocale()
{
    const char *activeLocale = setlocale(LC_ALL, nullptr);
    const QByteArray activeLocaleValue = activeLocale != nullptr ? QByteArray(activeLocale) : QByteArray();

    const bool alreadyUtf8 = activeLocaleValue.toLower().contains("utf-8");
    if (alreadyUtf8) {
        return;
    }

    const QList<QByteArray> utf8LocaleCandidates = {
        qgetenv("LC_ALL").trimmed(),
        qgetenv("LC_CTYPE").trimmed(),
        qgetenv("LANG").trimmed(),
        QByteArrayLiteral("C.UTF-8"),
        QByteArrayLiteral("en_GB.UTF-8"),
        QByteArrayLiteral("en_US.UTF-8")
    };

    for (const QByteArray &candidateRaw : utf8LocaleCandidates) {
        const QByteArray localeCandidate = candidateRaw.trimmed();
        if (localeCandidate.isEmpty()) {
            continue;
        }
        if (!localeCandidate.toLower().contains("utf-8")) {
            continue;
        }

        if (setlocale(LC_ALL, localeCandidate.constData()) != nullptr) {
            qputenv("LC_ALL", localeCandidate);
            qputenv("LANG", localeCandidate);
            qputenv("LC_CTYPE", localeCandidate);
            return;
        }
    }

    qputenv("LC_ALL", QByteArrayLiteral("C.UTF-8"));
    qputenv("LANG", QByteArrayLiteral("C.UTF-8"));
    qputenv("LC_CTYPE", QByteArrayLiteral("C.UTF-8"));
    if (setlocale(LC_ALL, "C.UTF-8") != nullptr) {
        return;
    }
    setlocale(LC_ALL, "");
}

void sanitizeGtkModulesEnvironment()
{
    QByteArray gtkModulesValue = qgetenv("GTK_MODULES");
    if (gtkModulesValue.isEmpty()) {
        return;
    }

    QString normalizedModules = QString::fromLocal8Bit(gtkModulesValue);
    normalizedModules.replace(QLatin1Char(';'), QLatin1Char(':'));
    normalizedModules.replace(QLatin1Char(','), QLatin1Char(':'));

    const QStringList moduleList = normalizedModules.split(QLatin1Char(':'), Qt::SkipEmptyParts);
    QStringList filteredModules;
    bool removedModule = false;
    for (const QString &moduleName : moduleList) {
        const QString trimmedModuleName = moduleName.trimmed();
        if (trimmedModuleName.compare(QStringLiteral("xapp-gtk3-module"), Qt::CaseInsensitive) == 0) {
            removedModule = true;
            continue;
        }
        if (!trimmedModuleName.isEmpty()) {
            filteredModules << trimmedModuleName;
        }
    }

    if (!removedModule) {
        return;
    }
    if (filteredModules.isEmpty()) {
        qunsetenv("GTK_MODULES");
        return;
    }

    qputenv("GTK_MODULES", filteredModules.join(QLatin1Char(':')).toLocal8Bit());
}

void sanitizeGuiStartupEnvironment()
{
    ensureUtf8ProcessLocale();
    sanitizeGtkModulesEnvironment();
    // prepareStockThemeEnvironment() calls setDesktopSettingsAware(false) and
    // normalizes QT_STYLE_OVERRIDE to Fusion. The stock palette itself is
    // applied via ThemedApplication after CLI parse (dark default, light opt-in).
    tbc::ui::prepareStockThemeEnvironment();
}
QString normalizePathForCurrentPlatform(const QString &path)
{
    QString normalizedInput = path.trimmed();
    if (normalizedInput.isEmpty()) {
        return QString();
    }

    const QUrl inputUrl(normalizedInput);
    if (inputUrl.isValid() && inputUrl.scheme().compare(QStringLiteral("file"), Qt::CaseInsensitive) == 0) {
        const QString localFile = inputUrl.toLocalFile();
        if (!localFile.isEmpty()) {
            normalizedInput = localFile;
        }
    }

    const QString normalized = QDir::cleanPath(QDir::fromNativeSeparators(normalizedInput));
    return QDir::toNativeSeparators(normalized);
}

void appendUniqueInputPath(QStringList &inputPaths, const QString &candidatePath)
{
    if (candidatePath.isEmpty()) {
        return;
    }
    for (const QString &existingPath : inputPaths) {
        if (existingPath.compare(candidatePath, Qt::CaseInsensitive) == 0) {
            return;
        }
    }
    inputPaths << candidatePath;
}

QString stripWrappingQuotes(const QString &value)
{
    QString strippedValue = value.trimmed();
    while (strippedValue.size() >= 2) {
        const QChar firstCharacter = strippedValue.front();
        const QChar lastCharacter = strippedValue.back();
        const bool wrappedInDoubleQuotes =
            firstCharacter == QLatin1Char('"') && lastCharacter == QLatin1Char('"');
        const bool wrappedInSingleQuotes =
            firstCharacter == QLatin1Char('\'') && lastCharacter == QLatin1Char('\'');
        if (!wrappedInDoubleQuotes && !wrappedInSingleQuotes) {
            break;
        }
        strippedValue = strippedValue.mid(1, strippedValue.size() - 2).trimmed();
    }
    return strippedValue;
}

void appendWhitespaceSeparatedCandidates(QStringList &candidateInputs, const QString &rawSegment)
{
    QString currentCandidate;
    for (const QChar character : rawSegment) {
        if (character.isSpace()) {
            const QString normalizedCandidate = stripWrappingQuotes(currentCandidate);
            if (!normalizedCandidate.isEmpty()) {
                candidateInputs << normalizedCandidate;
            }
            currentCandidate.clear();
            continue;
        }
        currentCandidate.append(character);
    }

    const QString normalizedCandidate = stripWrappingQuotes(currentCandidate);
    if (!normalizedCandidate.isEmpty()) {
        candidateInputs << normalizedCandidate;
    }
}

QStringList splitInputCandidates(const QString &rawInput)
{
    QStringList candidateInputs;
    const QString normalizedInput = stripWrappingQuotes(rawInput);
    if (normalizedInput.isEmpty()) {
        return candidateInputs;
    }

    const QString fileSchemePrefix = QStringLiteral("file://");
    if (!normalizedInput.contains(fileSchemePrefix, Qt::CaseInsensitive)) {
        candidateInputs << normalizedInput;
        return candidateInputs;
    }

    int scanPosition = 0;
    while (scanPosition < normalizedInput.size()) {
        const int fileSchemeStart =
            normalizedInput.indexOf(fileSchemePrefix, scanPosition, Qt::CaseInsensitive);
        if (fileSchemeStart < 0) {
            appendWhitespaceSeparatedCandidates(candidateInputs, normalizedInput.mid(scanPosition));
            break;
        }

        appendWhitespaceSeparatedCandidates(
            candidateInputs, normalizedInput.mid(scanPosition, fileSchemeStart - scanPosition));

        int fileSchemeEnd = normalizedInput.size();
        for (int characterIndex = fileSchemeStart; characterIndex < normalizedInput.size();
             characterIndex++) {
            if (normalizedInput.at(characterIndex).isSpace()) {
                fileSchemeEnd = characterIndex;
                break;
            }
        }

        const int nextEmbeddedScheme = normalizedInput.indexOf(
            fileSchemePrefix, fileSchemeStart + fileSchemePrefix.size(), Qt::CaseInsensitive);
        if (nextEmbeddedScheme >= 0 && nextEmbeddedScheme < fileSchemeEnd) {
            fileSchemeEnd = nextEmbeddedScheme;
        }

        const QString fileUriToken =
            stripWrappingQuotes(normalizedInput.mid(fileSchemeStart, fileSchemeEnd - fileSchemeStart));
        if (!fileUriToken.isEmpty()) {
            candidateInputs << fileUriToken;
        }
        scanPosition = fileSchemeEnd;
    }

    return candidateInputs;
}

void appendUniqueInputPathsFromRaw(QStringList &inputPaths, const QString &rawInput)
{
    const QStringList candidateInputs = splitInputCandidates(rawInput);
    for (const QString &candidateInput : candidateInputs) {
        appendUniqueInputPath(inputPaths, normalizePathForCurrentPlatform(candidateInput));
    }
}

QString outputPathCollisionKey(const QString &path)
{
    return QDir::cleanPath(QDir::fromNativeSeparators(path)).toLower();
}

QString reserveUniqueBatchOutputPath(const QString &baseOutputPath, QSet<QString> &reservedOutputPaths)
{
    const QFileInfo outputInfo(baseOutputPath);
    const QString outputDirectory = outputInfo.absolutePath();
    const QString outputStem = outputInfo.completeBaseName();
    const QString outputSuffix = outputInfo.completeSuffix();

    QString candidateOutputPath = baseOutputPath;
    int duplicateCounter = 2;
    QString collisionKey = outputPathCollisionKey(candidateOutputPath);
    while (reservedOutputPaths.contains(collisionKey)) {
        const QString suffixToken = outputSuffix.isEmpty()
                                        ? QString()
                                        : QStringLiteral(".") + outputSuffix;
        candidateOutputPath = QDir(outputDirectory).filePath(
            QStringLiteral("%1_%2%3")
                .arg(outputStem)
                .arg(duplicateCounter)
                .arg(suffixToken));
        collisionKey = outputPathCollisionKey(candidateOutputPath);
        duplicateCounter++;
    }

    reservedOutputPaths.insert(collisionKey);
    return candidateOutputPath;
}

DataConverter::OutputFormat parseOutputFormat(const QString &formatString, bool *ok = nullptr)
{
    const QString normalizedFormat = formatString.trimmed().toLower();
    if (normalizedFormat == QStringLiteral("flac")) {
        if (ok) *ok = true;
        return DataConverter::OutputFormat::Flac;
    }
    // "ldf" writes a real FLAC container with a .ldf extension, used as a
    // "LaserDisc FLAC" context label. It is not a separate codec.
    if (normalizedFormat == QStringLiteral("ldf")) {
        if (ok) *ok = true;
        return DataConverter::OutputFormat::FlacLdf;
    }
    if (normalizedFormat == QStringLiteral("s16") ||
        normalizedFormat == QStringLiteral("raw") ||
        normalizedFormat == QStringLiteral("raw16")) {
        if (ok) *ok = true;
        return DataConverter::OutputFormat::S16Raw;
    }
    if (normalizedFormat == QStringLiteral("riff") ||
        normalizedFormat == QStringLiteral("wav")) {
        if (ok) *ok = true;
        return DataConverter::OutputFormat::RiffWave;
    }

    if (ok) *ok = false;
    return DataConverter::OutputFormat::Flac;
}

bool wantsGui(int argc, char *argv[])
{
    const auto stdinIsInteractive = []() -> bool {
#ifdef Q_OS_WIN
        return _isatty(_fileno(stdin)) != 0;
#else
        return isatty(fileno(stdin)) != 0;
#endif
    };
    const auto stdinIsDevNull = []() -> bool {
#if defined(Q_OS_LINUX)
        const QFileInfo stdinDescriptor(QStringLiteral("/proc/self/fd/0"));
        if (!stdinDescriptor.exists() || !stdinDescriptor.isSymLink()) {
            return false;
        }
        return QDir::cleanPath(stdinDescriptor.symLinkTarget()) == QStringLiteral("/dev/null");
#else
        return false;
#endif
    };
    if (argc <= 1) {
        // Launching from desktop/file-manager often maps stdin to /dev/null.
        // Preserve GUI launch in that case while keeping redirected pipe/file CLI behaviour.
        if (!stdinIsInteractive() && stdinIsDevNull()) {
            return true;
        }
        // Keep legacy CLI piping behavior when stdin is redirected.
        return stdinIsInteractive();
    }

    bool hasExplicitCliOption = false;
    int positionalArgumentCount = 0;

    for (int argumentIndex = 1; argumentIndex < argc; argumentIndex++) {
        const QString argument = QString::fromLocal8Bit(argv[argumentIndex]);
        if (argument == QLatin1String("--gui") || argument == QLatin1String("-g")) {
            return true;
        }

        if (argument == QLatin1String("--help") || argument == QLatin1String("-h") ||
            argument == QLatin1String("--version") || argument == QLatin1String("-v")) {
            return false;
        }

        if (argument == QLatin1String("--pack") || argument == QLatin1String("-p") ||
            argument == QLatin1String("--unpack") || argument == QLatin1String("-u") ||
            argument == QLatin1String("--riff") || argument == QLatin1String("-r") ||
            argument == QLatin1String("--input") || argument == QLatin1String("-i") ||
            argument == QLatin1String("--output") || argument == QLatin1String("-o") ||
            argument == QLatin1String("--format") ||
            argument == QLatin1String("--s16") ||
            argument == QLatin1String("--flac") ||
            argument == QLatin1String("--ldf") ||
            argument == QLatin1String("--sample-rate") ||
            argument == QLatin1String("--compression-level")) {
            hasExplicitCliOption = true;
        }

        if (!argument.startsWith(QLatin1Char('-'))) {
            positionalArgumentCount++;
        }
    }

    // If positional arguments are passed without explicit CLI mode options
    // (common for desktop drag/drop/open-with), launch GUI and pre-fill queue.
    if (!hasExplicitCliOption && positionalArgumentCount >= 1) {
        return true;
    }

    return false;
}
} // namespace

int main(int argc, char *argv[])
{
    // Set 'binary mode' for stdin and stdout on Windows
    setBinaryMode();
    ensureUtf8ProcessLocale();

    // Install the local debug message handler
    setDebug(false);
    qInstallMessageHandler(ldLdsConverterMessageHandler);

    if (wantsGui(argc, argv)) {
        sanitizeGuiStartupEnvironment();
        tbc::ui::ThemedApplication a(argc, argv);

        QCoreApplication::setApplicationName("ld-lds-converter");
        QCoreApplication::setApplicationVersion(QString("tbc-tools - Branch: %1 / Commit: %2").arg(APP_BRANCH, APP_COMMIT));
        QCoreApplication::setOrganizationDomain("domesday86.com");

        QCommandLineParser parser;
        parser.setApplicationDescription(
            "ld-lds-converter - Convert 10-bit .lds files to FLAC (default) or s16 raw output\n"
            "\n"
            "(c)2018-2026 Simon Inns\n"
            "GPLv3 Open-Source - github: https://github.com/happycube/ld-decode");
        parser.addHelpOption();
        parser.addVersionOption();

        addStandardDebugOptions(parser);

        QCommandLineOption guiOption(QStringList() << "g" << "gui",
                                     QCoreApplication::translate("main", "Launch the GUI (default when no explicit CLI mode is provided)."));
        parser.addOption(guiOption);
        QCommandLineOption lightThemeOption("light-theme",
                                            QCoreApplication::translate("main", "Use the light Fusion theme instead of the stock dark theme"));
        parser.addOption(lightThemeOption);

        QCommandLineOption sourceVideoFileOption(QStringList() << "i" << "input",
                    QCoreApplication::translate("main", "Specify input laserdisc sample file"), QCoreApplication::translate("main", "file"));
        parser.addOption(sourceVideoFileOption);

        QCommandLineOption targetVideoFileOption(QStringList() << "o" << "output",
                    QCoreApplication::translate("main", "Specify output file"), QCoreApplication::translate("main", "file"));
        parser.addOption(targetVideoFileOption);

        QCommandLineOption outputFormatOption(QStringList() << "format",
                    QCoreApplication::translate("main", "Output format for unpack mode: flac (default), ldf, s16 or riff"),
                    QCoreApplication::translate("main", "format"), "flac");
        parser.addOption(outputFormatOption);

        parser.process(a);
        processStandardDebugOptions(parser);

        // Apply the stock theme (dark by default, light via --light-theme). Sets
        // the Fusion palette, isDarkTheme property, Qt 6.8 color scheme override,
        // and input-widget contrast guard; re-asserted on macOS switchover.
        if (parser.isSet(lightThemeOption)) {
            a.applyStockLightTheme();
        } else {
            a.applyStockDarkTheme();
        }

        bool outputFormatIsValid = false;
        const DataConverter::OutputFormat outputFormat = parseOutputFormat(parser.value(outputFormatOption), &outputFormatIsValid);
        if (!outputFormatIsValid) {
            qCritical("Invalid --format value. Use one of: flac, ldf, s16, riff");
            return -1;
        }

        QStringList inputFileNames;
        const QStringList optionInputs = parser.values(sourceVideoFileOption);
        for (const QString &optionInput : optionInputs) {
            appendUniqueInputPathsFromRaw(inputFileNames, optionInput);
        }
        const QStringList positionalArguments = parser.positionalArguments();
        for (const QString &positionalInput : positionalArguments) {
            appendUniqueInputPathsFromRaw(inputFileNames, positionalInput);
        }

        const QString outputFileName = normalizePathForCurrentPlatform(parser.value(targetVideoFileOption));

        ConverterDialog dialog;
        dialog.setDefaultFormat(outputFormat);
        if (!inputFileNames.isEmpty()) {
            dialog.setDefaultInputs(inputFileNames);
        }
        if (!outputFileName.isEmpty()) {
            dialog.setDefaultOutput(outputFileName);
        }
        dialog.exec();
        return 0;
    }

    QCoreApplication a(argc, argv);

    // Set application name and version
    QCoreApplication::setApplicationName("ld-lds-converter");
    QCoreApplication::setApplicationVersion(QString("tbc-tools - Branch: %1 / Commit: %2").arg(APP_BRANCH, APP_COMMIT));
    QCoreApplication::setOrganizationDomain("domesday86.com");

    // Set up the command line parser
    QCommandLineParser parser;
    parser.setApplicationDescription(
                "ld-lds-converter - Convert 10-bit .lds files to FLAC (default) or s16 raw output\n"
                "\n"
                "(c)2018-2026 Simon Inns\n"
                "GPLv3 Open-Source - github: https://github.com/happycube/ld-decode");
    parser.addHelpOption();
    parser.addVersionOption();

    // Add the standard debug options --debug and --quiet
    addStandardDebugOptions(parser);

    // Option to launch the GUI
    QCommandLineOption guiOption(QStringList() << "g" << "gui",
                                 QCoreApplication::translate("main", "Launch the GUI."));
    parser.addOption(guiOption);

    // Option to specify input video file (-i)
    QCommandLineOption sourceVideoFileOption(QStringList() << "i" << "input",
                QCoreApplication::translate("main", "Specify input laserdisc sample file (default is stdin)"),
                QCoreApplication::translate("main", "file"));
    parser.addOption(sourceVideoFileOption);

    // Option to specify output video file (-o)
    QCommandLineOption targetVideoFileOption(QStringList() << "o" << "output",
                QCoreApplication::translate("main", "Specify output file (default is inferred from input, or stdout for stdin input)"),
                QCoreApplication::translate("main", "file"));
    parser.addOption(targetVideoFileOption);

    // Option to unpack 10-bit data (-u)
    QCommandLineOption showUnpackOption(QStringList() << "u" << "unpack",
                                       QCoreApplication::translate("main", "Unpack 10-bit data into selected output format"));
    parser.addOption(showUnpackOption);

    // Option to pack 16-bit data (-p)
    QCommandLineOption showPackOption(QStringList() << "p" << "pack",
                                       QCoreApplication::translate("main", "Pack 16-bit data into 10-bit"));
    parser.addOption(showPackOption);

    // Option to unpack 10-bit data with RIFF WAV headers (-r)
    QCommandLineOption showRIFFOption(QStringList() << "r" << "riff",
                                        QCoreApplication::translate("main", "Unpack 10-bit data into 16-bit with RIFF WAV headers (legacy mode)"));
    parser.addOption(showRIFFOption);

    // Option to choose output format
    QCommandLineOption outputFormatOption(QStringList() << "format",
                QCoreApplication::translate("main", "Output format for unpack mode: flac (default), ldf, s16 or riff"),
                QCoreApplication::translate("main", "format"), "flac");
    parser.addOption(outputFormatOption);

    // Convenience options for unpack output formats
    QCommandLineOption showS16Option(QStringList() << "s16",
                                     QCoreApplication::translate("main", "Shortcut for --format s16"));
    parser.addOption(showS16Option);

    QCommandLineOption showFlacOption(QStringList() << "flac",
                                      QCoreApplication::translate("main", "Shortcut for --format flac"));
    parser.addOption(showFlacOption);

    QCommandLineOption showLdfOption(QStringList() << "ldf",
                                     QCoreApplication::translate("main", "Shortcut for --format ldf (FLAC container with .ldf extension)"));
    parser.addOption(showLdfOption);

    QCommandLineOption flacSampleRateOption(QStringList() << "sample-rate",
                                            QCoreApplication::translate("main", "FLAC sample rate in Hz (default: 40000)"),
                                            QCoreApplication::translate("main", "hz"), "40000");
    parser.addOption(flacSampleRateOption);

    QCommandLineOption flacCompressionLevelOption(QStringList() << "compression-level",
                                                  QCoreApplication::translate("main", "FLAC compression level 0-8 (default: 8)"),
                                                  QCoreApplication::translate("main", "level"), "8");
    parser.addOption(flacCompressionLevelOption);

    // Process the command line arguments given by the user
    parser.process(a);

    // Standard logging options
    processStandardDebugOptions(parser);

    // Get the configured settings from the parser
    const bool isUnpacking = parser.isSet(showUnpackOption);
    const bool isPacking = parser.isSet(showPackOption);
    const bool isRIFF = parser.isSet(showRIFFOption);
    const bool isS16 = parser.isSet(showS16Option);
    const bool isFlac = parser.isSet(showFlacOption);
    const bool isLdf = parser.isSet(showLdfOption);

    // Check that both pack and unpack are not set
    if (isUnpacking && isPacking) {
        qCritical("Specify only --unpack (-u) or --pack (-p) - not both!");
        return -1;
    }

    if (isPacking && (isRIFF || isS16 || isFlac || isLdf || parser.isSet(outputFormatOption))) {
        qCritical("Output-format options (--riff, --s16, --flac, --ldf, --format) are only valid with unpack mode.");
        return -1;
    }

    if (isRIFF && (isS16 || isFlac || isLdf || parser.isSet(outputFormatOption))) {
        qCritical("Specify only one unpack output format option: --riff OR --s16/--flac/--ldf/--format.");
        return -1;
    }

    if ((isS16 && isFlac) || (isS16 && isLdf) || (isFlac && isLdf)) {
        qCritical("Specify only one of --s16, --flac or --ldf.");
        return -1;
    }

    bool modePacking = false;
    if (isPacking) {
        modePacking = true;
    }

    DataConverter::OutputFormat outputFormat = DataConverter::OutputFormat::Flac;
    if (isRIFF) {
        outputFormat = DataConverter::OutputFormat::RiffWave;
    } else if (isS16) {
        outputFormat = DataConverter::OutputFormat::S16Raw;
    } else if (isFlac) {
        outputFormat = DataConverter::OutputFormat::Flac;
    } else if (isLdf) {
        outputFormat = DataConverter::OutputFormat::FlacLdf;
    } else if (!modePacking) {
        bool outputFormatIsValid = false;
        outputFormat = parseOutputFormat(parser.value(outputFormatOption), &outputFormatIsValid);
        if (!outputFormatIsValid) {
            qCritical("Invalid --format value. Use one of: flac, ldf, s16, riff");
            return -1;
        }
    }

    bool sampleRateOk = false;
    const int flacSampleRate = parser.value(flacSampleRateOption).toInt(&sampleRateOk);
    if (!sampleRateOk || flacSampleRate <= 0) {
        qCritical("Invalid --sample-rate value. It must be a positive integer.");
        return -1;
    }

    bool compressionLevelOk = false;
    const int flacCompressionLevel = parser.value(flacCompressionLevelOption).toInt(&compressionLevelOk);
    if (!compressionLevelOk || flacCompressionLevel < 0 || flacCompressionLevel > 8) {
        qCritical("Invalid --compression-level value. It must be in the range 0-8.");
        return -1;
    }

    QString outputFileName = normalizePathForCurrentPlatform(parser.value(targetVideoFileOption));
    QStringList inputFileNames;
    const QStringList optionInputs = parser.values(sourceVideoFileOption);
    for (const QString &optionInput : optionInputs) {
        appendUniqueInputPathsFromRaw(inputFileNames, optionInput);
    }
    const QStringList positionalArguments = parser.positionalArguments();
    for (const QString &positionalInput : positionalArguments) {
        appendUniqueInputPathsFromRaw(inputFileNames, positionalInput);
    }

    if (inputFileNames.size() > 1) {
        bool useOutputDirectoryOverride = false;
        QString outputDirectoryOverride;
        if (!outputFileName.isEmpty()) {
            const QFileInfo outputInfo(outputFileName);
            if (outputInfo.exists() && outputInfo.isDir()) {
                useOutputDirectoryOverride = true;
                outputDirectoryOverride = outputFileName;
            } else {
                qCritical("When multiple inputs are provided, --output must be an existing directory.");
                return -1;
            }
        }

        qint32 failedConversions = 0;
        QSet<QString> reservedOutputPaths;
        for (const QString &inputFileName : inputFileNames) {
            QString resolvedOutputFileName;
            if (useOutputDirectoryOverride) {
                const QFileInfo inputInfo(inputFileName);
                resolvedOutputFileName = QDir(outputDirectoryOverride).filePath(
                    inputInfo.completeBaseName() + DataConverter::outputExtensionForFormat(outputFormat));
            } else {
                resolvedOutputFileName = normalizePathForCurrentPlatform(
                    DataConverter::defaultOutputPath(inputFileName, modePacking, outputFormat));
            }
            resolvedOutputFileName = reserveUniqueBatchOutputPath(resolvedOutputFileName,
                                                                  reservedOutputPaths);

            DataConverter dataConverter(inputFileName,
                                        resolvedOutputFileName,
                                        modePacking,
                                        outputFormat,
                                        flacSampleRate,
                                        flacCompressionLevel);
            if (!dataConverter.process()) {
                qWarning() << "Conversion failed for input:" << inputFileName;
                failedConversions++;
            }
        }

        if (failedConversions > 0) {
            return 1;
        }
    } else {
        const QString inputFileName = inputFileNames.isEmpty() ? QString() : inputFileNames.constFirst();
        if (outputFileName.isEmpty() && !inputFileName.isEmpty()) {
            outputFileName = normalizePathForCurrentPlatform(
                DataConverter::defaultOutputPath(inputFileName, modePacking, outputFormat));
        }

        // Initialise the data conversion object
        DataConverter dataConverter(inputFileName,
                                    outputFileName,
                                    modePacking,
                                    outputFormat,
                                    flacSampleRate,
                                    flacCompressionLevel);

        // Process the data conversion
        if (!dataConverter.process()) {
            return 1;
        }
    }

    // Quit with success
    return 0;
}