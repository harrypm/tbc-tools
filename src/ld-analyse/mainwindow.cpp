/******************************************************************************
 * mainwindow.cpp
 * tbc-analyse - TBC output analysis GUI
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2018-2026 Simon Inns
 * SPDX-FileCopyrightText: 2022 Adam Sampson
 * SPDX-FileCopyrightText: 2026 Harry Munday
 *
 * This file is part of tbc-tools.
 ******************************************************************************/

#include "mainwindow.h"
#include "ui_mainwindow.h"
#include "tbc/logging.h"

#include <QAbstractButton>
#include <QApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QIcon>
#include <QMenu>
#include <QPainter>
#include <QPen>
#include <QPixmap>
#include <QProcess>
#include <QDialog>
#include <QProgressBar>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QRegularExpression>
#include <QSignalBlocker>
#include <QStyle>
#include <QStyleFactory>
#include <QStyleOptionSlider>
#include <QSvgRenderer>
#include <QStringList>
#include <QTextStream>
#include <QDateTime>
#include <QDesktopServices>
#include <QFileOpenEvent>
#include <QMimeData>
#include <QScreen>
#include <QtMath>
#include <QUrl>
#include <QClipboard>
#include <QWheelEvent>
#include <QWindow>
#include <QHash>
#include <QInputDialog>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QTextEdit>
#include <QAbstractSpinBox>
#include <QKeySequence>
#include <QShortcut>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMap>
#include <QPushButton>
#include <QCheckBox>
#include <QRadioButton>
#include <QVBoxLayout>
#include <QtConcurrent/QtConcurrent>
#include <optional>
#if defined(Q_OS_UNIX)
#include <signal.h>
#endif
#if defined(Q_OS_WIN)
#include <windows.h>
#endif

#include "metadataconverterutil.h"
#include "notesviewerdialog.h"
#include "teletextviewerdialog.h"
#include "timelinemarkerslider.h"
#include "efmhandlerdialog.h"
#include "../audio-align/audioalignmentdialog.h"
#include "../tbc-export-metadata/metadataexportdialog.h"
#include "tbc/uistyle.h"
namespace {
QString chromaDecoderNameFromConfig(VideoSystem system,
                                    const PalColour::Configuration &palConfig,
                                    const Comb::Configuration &ntscConfig)
{
    const bool isPal = (system == PAL || system == PAL_M);
    const bool isSecam = (system == SECAM || system == MESECAM);
    if (isPal) {
        switch (palConfig.chromaFilter) {
        case PalColour::palColourFilter:
            return QStringLiteral("pal2d");
        case PalColour::transform2DFilter:
            return QStringLiteral("transform2d");
        case PalColour::transform3DFilter:
            return QStringLiteral("transform3d");
        case PalColour::mono:
            return QStringLiteral("mono");
        default:
            break;
        }
    } else if (isSecam) {
        // SECAM is its own system: only SECAM decoders (and mono) are named here.
        switch (palConfig.chromaFilter) {
        case PalColour::secam:
            return QStringLiteral("secam");
        case PalColour::secamPredemod:
            return QStringLiteral("secam-predemod");
        case PalColour::mono:
            return QStringLiteral("mono");
        default:
            break;
        }
    }

    if (system == NTSC) {
        if (ntscConfig.dimensions <= 0) {
            return QStringLiteral("mono");
        }
        switch (ntscConfig.dimensions) {
        case 1:
            return QStringLiteral("ntsc1d");
        case 2:
            return QStringLiteral("ntsc2d");
        case 3:
            return ntscConfig.nnTransform3D ? QStringLiteral("nntransform3d")
                                            : QStringLiteral("ntsc3d");
        default:
            break;
        }
    }

    return QString();
}


QString sanitizedFileToken(const QString &value)
{
    QString token = value.trimmed().toLower();
    token.replace(QRegularExpression(QStringLiteral("[^a-z0-9]+")), QStringLiteral("_"));
    token.remove(QRegularExpression(QStringLiteral("^_+|_+$")));
    if (token.isEmpty()) {
        token = QStringLiteral("state");
    }
    return token;
}

struct EfmAutoloadCandidates {
    QStringList efmInputs;
    QStringList pcmInputs;
    QString ac3Input;
};

EfmAutoloadCandidates discoverEfmAutoloadCandidates(const QString &directoryPath,
                                                    const QString &sourceBaseName)
{
    EfmAutoloadCandidates candidates;
    const QString normalizedDirectory = QDir::cleanPath(directoryPath.trimmed());
    if (normalizedDirectory.isEmpty()) {
        return candidates;
    }

    const QDir directory(normalizedDirectory);
    if (!directory.exists()) {
        return candidates;
    }

    const QFileInfoList entries = directory.entryInfoList(QDir::Files | QDir::NoSymLinks,
                                                           QDir::Name | QDir::IgnoreCase);
    const QString sourceBaseToken = sourceBaseName.trimmed().toLower();
    QString bestAc3Candidate;
    int bestAc3Score = -1;

    for (const QFileInfo &entry : entries) {
        const QString suffix = entry.suffix().toLower();
        const QString fileNameLower = entry.fileName().toLower();
        const QString absolutePath = entry.absoluteFilePath();

        if (suffix == QStringLiteral("efm")) {
            candidates.efmInputs << absolutePath;
        }
        if (suffix == QStringLiteral("pcm")) {
            candidates.pcmInputs << absolutePath;
        }

        if (!fileNameLower.contains(QStringLiteral("ac3"))) {
            continue;
        }

        int score = 0;
        if (!sourceBaseToken.isEmpty() && fileNameLower.contains(sourceBaseToken)) {
            score += 2;
        }
        if (suffix == QStringLiteral("bin")
            || suffix == QStringLiteral("dat")
            || suffix == QStringLiteral("txt")
            || suffix == QStringLiteral("sym")
            || suffix == QStringLiteral("symbols")
            || suffix == QStringLiteral("raw")) {
            score += 4;
        } else if (suffix == QStringLiteral("ac3")) {
            score += 1;
        }
        if (fileNameLower.contains(QStringLiteral("symbol"))
            || fileNameLower.contains(QStringLiteral("sym"))) {
            score += 3;
        }

        if (score > bestAc3Score) {
            bestAc3Score = score;
            bestAc3Candidate = absolutePath;
        }
    }

    candidates.ac3Input = bestAc3Candidate;
    return candidates;
}

void ensureSvgButtonIcon(QAbstractButton *button, const QString &resourcePath)
{
    if (!button) {
        return;
    }

    const QSize iconSize = button->iconSize().isValid() ? button->iconSize() : QSize(24, 24);
    QIcon icon(resourcePath);
    if (icon.isNull() || icon.availableSizes().isEmpty()) {
        QSvgRenderer renderer(resourcePath);
        if (renderer.isValid()) {
            QPixmap pixmap(iconSize);
            pixmap.fill(Qt::transparent);
            QPainter painter(&pixmap);
            renderer.render(&painter);
            icon = QIcon(pixmap);
        }
    }

    if (!icon.isNull()) {
        button->setIcon(icon);
        button->setIconSize(iconSize);
    }
}


QString formatOptionalString(const QString &value)
{
    return value.isEmpty() ? QStringLiteral("—") : value;
}

QString formatOptionalDouble(double value, int precision = 3)
{
    if (value < 0.0) {
        return QStringLiteral("—");
    }
    return QString::number(value, 'f', precision);
}


QString formatOptionalBoolFromInt(qint32 value)
{
    if (value < 0) {
        return QStringLiteral("—");
    }
    return value != 0 ? QStringLiteral("Yes") : QStringLiteral("No");
}

QString backupFilenameWithTimestampFallback(const QString &inputMetadataFilename,
                                            const QString &backupSuffix)
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

bool toolHelpListsOption(const QString &toolPath, const QString &optionName)
{
    if (toolPath.isEmpty() || optionName.isEmpty()) {
        return false;
    }

    const QString cacheKey = QDir::cleanPath(toolPath) + QLatin1Char('|') + optionName;
    static QHash<QString, bool> supportCache;
    if (supportCache.contains(cacheKey)) {
        return supportCache.value(cacheKey);
    }

    QProcess probeProcess;
    probeProcess.setProcessChannelMode(QProcess::MergedChannels);
    probeProcess.start(toolPath, {QStringLiteral("--help")});

    bool supportsOption = false;
    if (probeProcess.waitForStarted(2000)) {
        if (!probeProcess.waitForFinished(6000)) {
            probeProcess.kill();
            probeProcess.waitForFinished(1000);
        } else {
            const QString helpOutput = QString::fromLocal8Bit(probeProcess.readAllStandardOutput());
            supportsOption = helpOutput.contains(optionName);
        }
    }

    supportCache.insert(cacheKey, supportsOption);
    return supportsOption;
}

QString metadataInputOptionForTool(const QString &toolPath)
{
    if (toolHelpListsOption(toolPath, QStringLiteral("--input-metadata"))) {
        return QStringLiteral("--input-metadata");
    }
    if (toolHelpListsOption(toolPath, QStringLiteral("--input-json"))) {
        return QStringLiteral("--input-json");
    }
    return QStringLiteral("--input-metadata");
}

QString noBackupOptionForTool(const QString &toolPath)
{
    if (toolHelpListsOption(toolPath, QStringLiteral("--nobackup"))) {
        return QStringLiteral("--nobackup");
    }
    if (toolHelpListsOption(toolPath, QStringLiteral("-n"))) {
        return QStringLiteral("-n");
    }
    return QString();
}

bool createTimestampedMetadataBackup(const QString &metadataFilename,
                                     const QString &backupSuffix,
                                     QString *errorMessage)
{
    if (errorMessage) {
        errorMessage->clear();
    }
    if (metadataFilename.trimmed().isEmpty()) {
        if (errorMessage) {
            *errorMessage = QObject::tr("Metadata filename is empty.");
        }
        return false;
    }
    if (!QFileInfo::exists(metadataFilename)) {
        if (errorMessage) {
            *errorMessage = QObject::tr("Metadata file does not exist:\n%1").arg(metadataFilename);
        }
        return false;
    }

    const QString backupFilename = backupFilenameWithTimestampFallback(metadataFilename, backupSuffix);
    if (!QFile::copy(metadataFilename, backupFilename)) {
        if (errorMessage) {
            *errorMessage = QObject::tr("Unable to create backup file:\n%1").arg(backupFilename);
        }
        return false;
    }
    return true;
}

void appendUniqueCandidate(QStringList &candidates, const QString &candidate)
{
    if (candidate.isEmpty()) {
        return;
    }

    for (const QString &existing : candidates) {
        if (existing.compare(candidate, Qt::CaseInsensitive) == 0) {
            return;
        }
    }
    candidates.append(candidate);
}

QString normalizedTeletextBaseName(const QFileInfo &pathInfo)
{
    QString baseName = pathInfo.completeBaseName();
    QString baseNameLower = baseName.toLower();
    const QStringList suffixesToStrip = {
        QStringLiteral(".tbc"),
        QStringLiteral(".ytbc"),
        QStringLiteral(".ctbc"),
        QStringLiteral(".tbcy"),
        QStringLiteral(".tbcc")
    };
    for (const QString &suffix : suffixesToStrip) {
        if (!baseNameLower.endsWith(suffix)) {
            continue;
        }
        baseName.chop(suffix.size());
        break;
    }
    return baseName;
}

QString defaultTeletextHtmlDirectoryForInput(const QString &inputPath)
{
    const QFileInfo inputInfo(inputPath);
    if (inputInfo.absolutePath().isEmpty()) {
        return QString();
    }

    QString baseName = normalizedTeletextBaseName(inputInfo);
    if (baseName.isEmpty()) {
        baseName = inputInfo.completeBaseName();
    }
    if (baseName.isEmpty()) {
        return QString();
    }
    return QDir(inputInfo.absolutePath()).filePath(baseName + QStringLiteral("_teletext_html"));
}

QString resolveTeletextHtmlDirectoryFromHint(const QString &pathHint)
{
    const QFileInfo pathInfo(pathHint);
    if (!pathInfo.exists()) {
        return QString();
    }

    if (pathInfo.isFile() && pathInfo.suffix().compare(QStringLiteral("html"), Qt::CaseInsensitive) == 0) {
        const QDir htmlDirectory(pathInfo.absolutePath());
        const QStringList htmlPages = htmlDirectory.entryList(
            QStringList() << QStringLiteral("*.html"),
            QDir::Files,
            QDir::Name | QDir::IgnoreCase
        );
        if (!htmlPages.isEmpty()) {
            return htmlDirectory.absolutePath();
        }
    }

    QDir baseDirectory;
    if (pathInfo.isDir()) {
        baseDirectory = QDir(pathInfo.absoluteFilePath());
    } else {
        baseDirectory = pathInfo.absoluteDir();
    }

    QStringList candidateDirectories;
    QStringList baseNames;
    if (pathInfo.isDir()) {
        appendUniqueCandidate(baseNames, pathInfo.fileName());
    } else {
        appendUniqueCandidate(baseNames, normalizedTeletextBaseName(pathInfo));
        appendUniqueCandidate(baseNames, pathInfo.completeBaseName());
    }

    for (const QString &baseName : baseNames) {
        if (baseName.isEmpty()) {
            continue;
        }
        appendUniqueCandidate(candidateDirectories, baseDirectory.filePath(baseName + QStringLiteral("_teletext_html")));
        appendUniqueCandidate(candidateDirectories, baseDirectory.filePath(baseName + QStringLiteral(".teletext_html")));
        appendUniqueCandidate(candidateDirectories, baseDirectory.filePath(baseName + QStringLiteral("_teletext")));
    }
    appendUniqueCandidate(candidateDirectories, baseDirectory.filePath(QStringLiteral("teletext_html")));
    appendUniqueCandidate(candidateDirectories, baseDirectory.filePath(QStringLiteral("teletext")));

    for (const QString &candidate : candidateDirectories) {
        const QDir candidateDirectory(candidate);
        if (!candidateDirectory.exists()) {
            continue;
        }
        const QStringList htmlPages = candidateDirectory.entryList(
            QStringList() << QStringLiteral("*.html"),
            QDir::Files,
            QDir::Name | QDir::IgnoreCase
        );
        if (!htmlPages.isEmpty()) {
            return candidateDirectory.absolutePath();
        }
    }

    return QString();
}

QString resolveTeletextHtmlDirectoryFromHints(const QStringList &pathHints)
{
    for (const QString &pathHint : pathHints) {
        const QString resolvedDirectory = resolveTeletextHtmlDirectoryFromHint(pathHint);
        if (!resolvedDirectory.isEmpty()) {
            return resolvedDirectory;
        }
    }
    return QString();
}
QString resolveSourceFilenameForMetadata(const QString &metadataFilename)
{
    const QFileInfo metadataInfo(metadataFilename);
    if (!metadataInfo.exists()) {
        return QString();
    }

    const QString metadataStem = metadataInfo.completeBaseName();
    const QString metadataStemLower = metadataStem.toLower();
    const QDir metadataDir(metadataInfo.absolutePath());

    QStringList sourceCandidates;
    const auto appendFileNameCandidate = [&metadataDir, &sourceCandidates](const QString &fileName) {
        appendUniqueCandidate(sourceCandidates, metadataDir.filePath(fileName));
    };

    auto replaceSuffix = [](const QString &fileName, const QString &suffix, const QString &replacement) {
        QString output = fileName;
        output.chop(suffix.size());
        output.append(replacement);
        return output;
    };

    if (metadataStemLower.endsWith(QStringLiteral(".tbc"))
        || metadataStemLower.endsWith(QStringLiteral(".ytbc"))
        || metadataStemLower.endsWith(QStringLiteral(".ctbc"))
        || metadataStemLower.endsWith(QStringLiteral(".tbcy"))
        || metadataStemLower.endsWith(QStringLiteral(".tbcc"))) {
        appendFileNameCandidate(metadataStem);

        if (metadataStemLower.endsWith(QStringLiteral(".ctbc"))) {
            appendFileNameCandidate(replaceSuffix(metadataStem, QStringLiteral(".ctbc"), QStringLiteral(".ytbc")));
        } else if (metadataStemLower.endsWith(QStringLiteral(".tbcc"))) {
            appendFileNameCandidate(replaceSuffix(metadataStem, QStringLiteral(".tbcc"), QStringLiteral(".tbcy")));
        } else if (metadataStemLower.endsWith(QStringLiteral(".ytbc"))) {
            appendFileNameCandidate(replaceSuffix(metadataStem, QStringLiteral(".ytbc"), QStringLiteral(".ctbc")));
        } else if (metadataStemLower.endsWith(QStringLiteral(".tbcy"))) {
            appendFileNameCandidate(replaceSuffix(metadataStem, QStringLiteral(".tbcy"), QStringLiteral(".tbcc")));
        } else if (metadataStemLower.endsWith(QStringLiteral("_chroma.tbc"))) {
            appendFileNameCandidate(metadataStem.left(metadataStem.size() - QStringLiteral("_chroma.tbc").size()) + QStringLiteral(".tbc"));
        } else if (metadataStemLower.startsWith(QStringLiteral("chroma_"))
                   && metadataStemLower.endsWith(QStringLiteral(".tbc"))) {
            appendFileNameCandidate(metadataStem.mid(QStringLiteral("chroma_").size()));
        }
    } else {
        appendFileNameCandidate(metadataStem + QStringLiteral(".tbc"));
        appendFileNameCandidate(metadataStem + QStringLiteral(".ytbc"));
        appendFileNameCandidate(metadataStem + QStringLiteral(".tbcy"));
        appendFileNameCandidate(metadataStem + QStringLiteral(".ctbc"));
        appendFileNameCandidate(metadataStem + QStringLiteral(".tbcc"));
    }

    for (const QString &candidate : sourceCandidates) {
        if (QFileInfo::exists(candidate)) {
            return candidate;
        }
    }

    return QString();
}
QString resolveMetadataFilenameForSource(const QString &sourceFilename,
                                         const QString &preferredMetadataFilename = QString())
{
    if (!preferredMetadataFilename.isEmpty() && QFileInfo::exists(preferredMetadataFilename)) {
        return preferredMetadataFilename;
    }

    const QFileInfo sourceInfo(sourceFilename);
    if (!sourceInfo.exists()) {
        return QString();
    }

    QStringList metadataCandidates;
    const auto appendMetadataCandidate = [&metadataCandidates](const QString &candidate) {
        appendUniqueCandidate(metadataCandidates, candidate);
    };

    const auto appendCandidatesForFile = [&appendMetadataCandidate](const QFileInfo &info) {
        appendMetadataCandidate(info.filePath() + QStringLiteral(".json"));
        appendMetadataCandidate(info.filePath() + QStringLiteral(".db"));

        const QString basePath = QDir(info.absolutePath()).filePath(info.completeBaseName());
        appendMetadataCandidate(basePath + QStringLiteral(".json"));
        appendMetadataCandidate(basePath + QStringLiteral(".db"));

        const QString suffix = info.suffix().toLower();
        if (suffix == QStringLiteral("ytbc")
            || suffix == QStringLiteral("ctbc")
            || suffix == QStringLiteral("tbcy")
            || suffix == QStringLiteral("tbcc")) {
            appendMetadataCandidate(QDir(info.absolutePath())
                                        .filePath(info.completeBaseName() + QStringLiteral(".tbc.json")));
            appendMetadataCandidate(QDir(info.absolutePath())
                                        .filePath(info.completeBaseName() + QStringLiteral(".tbc.db")));
        }
    };

    appendCandidatesForFile(sourceInfo);

    const QString sourceLowerFileName = sourceInfo.fileName().toLower();
    QString lumaSourceCandidate;
    if (sourceLowerFileName.endsWith(QStringLiteral("_chroma.tbc"))) {
        lumaSourceCandidate = QDir(sourceInfo.absolutePath())
                                  .filePath(sourceInfo.fileName().left(sourceInfo.fileName().size()
                                          - QStringLiteral("_chroma.tbc").size()) + QStringLiteral(".tbc"));
    } else if (sourceLowerFileName.startsWith(QStringLiteral("chroma_"))
               && sourceLowerFileName.endsWith(QStringLiteral(".tbc"))) {
        lumaSourceCandidate = QDir(sourceInfo.absolutePath())
                                  .filePath(sourceInfo.fileName().mid(QStringLiteral("chroma_").size()));
    } else if (sourceLowerFileName.endsWith(QStringLiteral(".ctbc"))) {
        lumaSourceCandidate = QDir(sourceInfo.absolutePath())
                                  .filePath(sourceInfo.completeBaseName() + QStringLiteral(".ytbc"));
    } else if (sourceLowerFileName.endsWith(QStringLiteral(".tbcc"))) {
        lumaSourceCandidate = QDir(sourceInfo.absolutePath())
                                  .filePath(sourceInfo.completeBaseName() + QStringLiteral(".tbcy"));
    }

    if (!lumaSourceCandidate.isEmpty()) {
        appendCandidatesForFile(QFileInfo(lumaSourceCandidate));
    }

    for (const QString &candidate : metadataCandidates) {
        if (QFileInfo::exists(candidate)) {
            return candidate;
        }
    }

    return QString();
}

double frameRateForSystem(VideoSystem system)
{
    switch (system) {
    case PAL:
        return 25.0;
    case PAL_M:
    case NTSC:
    default:
        return 30000.0 / 1001.0;
    }
}

int nominalFrameRateForSystem(VideoSystem system)
{
    switch (system) {
    case PAL:
        return 25;
    case PAL_M:
    case NTSC:
    default:
        return 30;
    }
}

qint32 minActiveFrameLineForSystem(VideoSystem system)
{
    switch (system) {
    case PAL:
        return 2;
    case PAL_M:
    case NTSC:
    default:
        return 1;
    }
}

QString resolveExternalExecutable(const QStringList &toolNames)
{
    if (toolNames.isEmpty()) {
        return QString();
    }
    const auto isRunnableFile = [](const QString &candidatePath) {
        const QFileInfo candidateInfo(candidatePath);
#if defined(Q_OS_WIN)
        return candidateInfo.exists() && candidateInfo.isFile();
#else
        return candidateInfo.exists() && candidateInfo.isFile() && candidateInfo.isExecutable();
#endif
    };

    const QStringList candidateToolNames = [&toolNames]() {
        QStringList names;
        for (const QString &toolName : toolNames) {
            appendUniqueCandidate(names, toolName);
#if defined(Q_OS_WIN)
            if (!toolName.endsWith(QStringLiteral(".exe"), Qt::CaseInsensitive)) {
                appendUniqueCandidate(names, toolName + QStringLiteral(".exe"));
            }
#endif
        }
        return names;
    }();

    QStringList searchRoots;
    const auto appendRoot = [&searchRoots](const QString &rootPath) {
        appendUniqueCandidate(searchRoots, QDir::cleanPath(rootPath));
    };

    const QString appDir = QCoreApplication::applicationDirPath();
    appendRoot(appDir);
    const QStringList relativeRoots = {
        QStringLiteral("."),
        QStringLiteral(".."),
        QStringLiteral("../bin"),
        QStringLiteral("../../bin"),
        QStringLiteral("../../../"),
        QStringLiteral("../../../bin"),
        QStringLiteral("../../../../bin"),
        QStringLiteral("../Resources"),
        QStringLiteral("../Resources/bin"),
        QStringLiteral("../../Resources/bin"),
        QStringLiteral("../../../Resources/bin"),
        QStringLiteral("../libexec"),
        QStringLiteral("../../libexec"),
        QStringLiteral("../../../libexec")
    };
    for (const QString &root : relativeRoots) {
        appendRoot(QDir(appDir).filePath(root));
    }

    const QString currentDir = QDir::currentPath();
    appendRoot(currentDir);
    appendRoot(QDir(currentDir).filePath(QStringLiteral("bin")));
    appendRoot(QDir(currentDir).filePath(QStringLiteral("build/bin")));
    appendRoot(QDir(currentDir).filePath(QStringLiteral("../build/bin")));

#if defined(Q_OS_WIN)
    appendRoot(QDir(appDir).filePath(QStringLiteral("tools")));
    appendRoot(QDir(appDir).filePath(QStringLiteral("../tools")));
#endif

    if (qEnvironmentVariableIsSet("APPDIR")) {
        const QString appImageRoot = qEnvironmentVariable("APPDIR");
        appendRoot(QDir(appImageRoot).filePath(QStringLiteral("usr/bin")));
        appendRoot(QDir(appImageRoot).filePath(QStringLiteral("usr/libexec")));
    }

    if (qEnvironmentVariableIsSet("APPIMAGE")) {
        const QString appImageFile = qEnvironmentVariable("APPIMAGE");
        const QString appImageDir = QFileInfo(appImageFile).absolutePath();
        appendRoot(appImageDir);
        appendRoot(QDir(appImageDir).filePath(QStringLiteral("bin")));
    }

    for (const QString &dir : searchRoots) {
        const QDir searchDir(dir);
        for (const QString &name : candidateToolNames) {
            const QString localCandidate = searchDir.filePath(name);
            if (isRunnableFile(localCandidate)) {
                return localCandidate;
            }
        }
    }

    for (const QString &name : candidateToolNames) {
        const QString toolPath = QStandardPaths::findExecutable(name);
        if (!toolPath.isEmpty() && isRunnableFile(toolPath)) {
            return toolPath;
        }
    }

    return QString();
}

bool isSupportedInputExtension(const QString &filePath)
{
    const QString suffix = QFileInfo(filePath).suffix().toLower();
    return suffix == QStringLiteral("tbc")
        || suffix == QStringLiteral("ytbc")
        || suffix == QStringLiteral("ctbc")
        || suffix == QStringLiteral("tbcy")
        || suffix == QStringLiteral("tbcc")
        || suffix == QStringLiteral("db")
        || suffix == QStringLiteral("json");
}

bool isTeletextStreamInputExtension(const QString &filePath)
{
    static const QRegularExpression teletextSuffixPattern(
        QStringLiteral("^t\\d\\d$"),
        QRegularExpression::CaseInsensitiveOption
    );
    const QString suffix = QFileInfo(filePath).suffix();
    return teletextSuffixPattern.match(suffix).hasMatch();
}

QString firstSupportedDroppedFile(const QMimeData *mimeData)
{
    if (!mimeData || !mimeData->hasUrls()) {
        return QString();
    }

    const QList<QUrl> urls = mimeData->urls();
    for (const QUrl &url : urls) {
        if (!url.isLocalFile()) {
            continue;
        }
        const QString localPath = url.toLocalFile();
        if (localPath.isEmpty()) {
            continue;
        }
        if (isSupportedInputExtension(localPath) || isTeletextStreamInputExtension(localPath)) {
            return localPath;
        }
    }

    return QString();
}

enum class ExternalToolStage {
    Starting,
    LoadingMetadata,
    AnalysingMetadata,
    WritingMetadata,
    TeletextDependencyCheck,
    TeletextBackendProbe,
    TeletextDeconvolution,
    TeletextSquashing,
    TeletextHtmlGeneration,
    Finishing
};
bool externalToolStageIsTeletext(ExternalToolStage stage)
{
    return stage == ExternalToolStage::TeletextDependencyCheck
        || stage == ExternalToolStage::TeletextBackendProbe
        || stage == ExternalToolStage::TeletextDeconvolution
        || stage == ExternalToolStage::TeletextSquashing
        || stage == ExternalToolStage::TeletextHtmlGeneration;
}

QString externalToolStageLabel(const QString &toolDisplayName, ExternalToolStage stage)
{
    switch (stage) {
    case ExternalToolStage::Starting:
        return QObject::tr("%1: Preparing...").arg(toolDisplayName);
    case ExternalToolStage::LoadingMetadata:
        return QObject::tr("%1: Loading metadata...").arg(toolDisplayName);
    case ExternalToolStage::AnalysingMetadata:
        return QObject::tr("%1: Analysing metadata...").arg(toolDisplayName);
    case ExternalToolStage::WritingMetadata:
        return QObject::tr("%1: Writing metadata...").arg(toolDisplayName);
    case ExternalToolStage::TeletextDependencyCheck:
        return QObject::tr("%1: Checking teletext runtime...").arg(toolDisplayName);
    case ExternalToolStage::TeletextBackendProbe:
        return QObject::tr("%1: Probing teletext backends...").arg(toolDisplayName);
    case ExternalToolStage::TeletextDeconvolution:
        return QObject::tr("%1: Teletext deconvolution...").arg(toolDisplayName);
    case ExternalToolStage::TeletextSquashing:
        return QObject::tr("%1: Squashing teletext packets...").arg(toolDisplayName);
    case ExternalToolStage::TeletextHtmlGeneration:
        return QObject::tr("%1: Generating teletext HTML...").arg(toolDisplayName);
    case ExternalToolStage::Finishing:
    default:
        return QObject::tr("%1: Finalising...").arg(toolDisplayName);
    }
}

QString externalToolProgressSummary(qint32 processedFields, qint32 totalFields)
{
    if (totalFields <= 0) {
        return QObject::tr("Fields -----/----- (----- to go) | Frames -----/----- (----- to go)");
    }

    const qint32 clampedProcessedFields = qBound<qint32>(0, processedFields, totalFields);
    const qint32 remainingFields = totalFields - clampedProcessedFields;
    const qint32 totalFrames = (totalFields + 1) / 2;
    const qint32 processedFrames = (clampedProcessedFields + 1) / 2;
    const qint32 remainingFrames = qMax<qint32>(0, totalFrames - processedFrames);

    return QObject::tr("Fields %1/%2 (%3 to go) | Frames %4/%5 (%6 to go)")
        .arg(clampedProcessedFields, 5, 10, QChar('0'))
        .arg(totalFields, 5, 10, QChar('0'))
        .arg(remainingFields, 5, 10, QChar('0'))
        .arg(processedFrames, 5, 10, QChar('0'))
        .arg(totalFrames, 5, 10, QChar('0'))
        .arg(remainingFrames, 5, 10, QChar('0'));
}

int externalToolProgressPercent(qint32 processedFields, qint32 totalFields)
{
    if (totalFields <= 0) {
        return 0;
    }

    const qint32 clampedProcessedFields = qBound<qint32>(0, processedFields, totalFields);
    return static_cast<int>((static_cast<double>(clampedProcessedFields) / static_cast<double>(totalFields)) * 100.0);
}

QString externalToolTeletextProgressSummary(qint32 teletextPercent)
{
    if (teletextPercent < 0) {
        return QObject::tr("Teletext progress: --");
    }
    return QObject::tr("Teletext progress: %1%").arg(teletextPercent);
}

QString normalizedPathForCompare(const QString &path)
{
    if (path.isEmpty()) {
        return QString();
    }

    const QFileInfo info(path);
    const QString canonicalPath = info.canonicalFilePath();
    if (!canonicalPath.isEmpty()) {
        return canonicalPath;
    }
    return info.absoluteFilePath();
}

bool sameFilePath(const QString &left, const QString &right)
{
    const QString normalizedLeft = normalizedPathForCompare(left);
    const QString normalizedRight = normalizedPathForCompare(right);
    if (normalizedLeft.isEmpty() || normalizedRight.isEmpty()) {
        return false;
    }
    return normalizedLeft == normalizedRight;
}

struct UserNoteMarker
{
    qint32 frame = -1;
    QString comment;
};

qint32 jsonValueToInt(const QJsonValue &value, bool *ok = nullptr)
{
    bool conversionOk = false;
    qint32 output = -1;
    if (value.isDouble()) {
        const double numericValue = value.toDouble();
        output = static_cast<qint32>(numericValue);
        conversionOk = true;
    } else if (value.isString()) {
        output = value.toString().toInt(&conversionOk);
    }
    if (ok) {
        *ok = conversionOk;
    }
    return conversionOk ? output : -1;
}

QVector<UserNoteMarker> normaliseUserNoteMarkers(const QVector<UserNoteMarker> &noteMarkers,
                                                 qint32 totalFrames)
{
    QMap<qint32, QString> notesByFrame;
    const bool clampToKnownRange = (totalFrames > 0);
    for (const UserNoteMarker &noteMarker : noteMarkers) {
        if (noteMarker.frame <= 0) {
            continue;
        }
        qint32 frame = noteMarker.frame;
        if (clampToKnownRange) {
            frame = qBound<qint32>(1, frame, totalFrames);
        }
        if (frame <= 0) {
            continue;
        }
        notesByFrame.insert(frame, noteMarker.comment.trimmed());
    }

    QVector<UserNoteMarker> normalizedMarkers;
    normalizedMarkers.reserve(notesByFrame.size());
    for (auto it = notesByFrame.cbegin(); it != notesByFrame.cend(); ++it) {
        normalizedMarkers.append({it.key(), it.value()});
    }
    return normalizedMarkers;
}

QVector<UserNoteMarker> parseUserMarkersJson(const QString &userMarkersJson, qint32 totalFrames)
{
    const QByteArray jsonBytes = userMarkersJson.trimmed().toUtf8();
    if (jsonBytes.isEmpty()) {
        return {};
    }

    QJsonParseError parseError;
    const QJsonDocument jsonDocument = QJsonDocument::fromJson(jsonBytes, &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        return {};
    }

    QJsonArray markersArray;
    if (jsonDocument.isArray()) {
        markersArray = jsonDocument.array();
    } else if (jsonDocument.isObject()) {
        const QJsonObject rootObject = jsonDocument.object();
        const QJsonValue notesValue = rootObject.value(QStringLiteral("notes"));
        if (notesValue.isArray()) {
            markersArray = notesValue.toArray();
        }
    }

    QVector<UserNoteMarker> parsedMarkers;
    parsedMarkers.reserve(markersArray.size());
    for (const QJsonValue &markerValue : markersArray) {
        if (!markerValue.isObject()) {
            continue;
        }
        const QJsonObject markerObject = markerValue.toObject();
        bool frameOk = false;
        qint32 frame = -1;
        if (markerObject.contains(QStringLiteral("frame"))) {
            frame = jsonValueToInt(markerObject.value(QStringLiteral("frame")), &frameOk);
        } else if (markerObject.contains(QStringLiteral("selection"))) {
            frame = jsonValueToInt(markerObject.value(QStringLiteral("selection")), &frameOk);
        }
        if (!frameOk || frame <= 0) {
            continue;
        }

        QString comment;
        const QJsonValue commentValue = markerObject.value(QStringLiteral("comment"));
        if (commentValue.isString()) {
            comment = commentValue.toString();
        } else {
            const QJsonValue textValue = markerObject.value(QStringLiteral("text"));
            if (textValue.isString()) {
                comment = textValue.toString();
            }
        }

        parsedMarkers.append({frame, comment});
    }

    return normaliseUserNoteMarkers(parsedMarkers, totalFrames);
}

QVector<UserNoteMarker> userNoteMarkersFromVideoParameters(
    const TbcMetaData::VideoParameters &videoParameters,
    qint32 totalFrames)
{
    QVector<UserNoteMarker> combinedMarkers;
    if (videoParameters.userMarkerSelection > 0) {
        combinedMarkers.append({videoParameters.userMarkerSelection, videoParameters.userMarkerComment});
    }
    combinedMarkers += parseUserMarkersJson(videoParameters.userMarkersJson, totalFrames);
    return normaliseUserNoteMarkers(combinedMarkers, totalFrames);
}

qint32 nextUserMarkerChapterFrame(const QVector<UserNoteMarker> &noteMarkers,
                                  qint32 currentFrameNumber,
                                  qint32 totalFrames)
{
    for (const UserNoteMarker &noteMarker : noteMarkers) {
        if (noteMarker.frame > currentFrameNumber) {
            return noteMarker.frame;
        }
    }
    return totalFrames;
}

qint32 previousUserMarkerChapterFrame(const QVector<UserNoteMarker> &noteMarkers,
                                      qint32 currentFrameNumber)
{
    for (qint32 i = noteMarkers.size() - 1; i >= 0; --i) {
        if (noteMarkers.at(i).frame < currentFrameNumber) {
            return noteMarkers.at(i).frame;
        }
    }
    return 1;
}

QString serializeUserNoteMarkersJson(const QVector<UserNoteMarker> &noteMarkers)
{
    if (noteMarkers.isEmpty()) {
        return QString();
    }

    QJsonArray markersArray;
    for (const UserNoteMarker &noteMarker : noteMarkers) {
        if (noteMarker.frame <= 0) {
            continue;
        }
        QJsonObject markerObject;
        markerObject.insert(QStringLiteral("frame"), noteMarker.frame);
        if (!noteMarker.comment.isEmpty()) {
            markerObject.insert(QStringLiteral("comment"), noteMarker.comment);
        }
        markersArray.append(markerObject);
    }

    if (markersArray.isEmpty()) {
        return QString();
    }
    return QString::fromUtf8(QJsonDocument(markersArray).toJson(QJsonDocument::Compact));
}

bool applyUserNoteMarkersToVideoParameters(TbcMetaData::VideoParameters &videoParameters,
                                           const QVector<UserNoteMarker> &noteMarkers)
{
    const QVector<UserNoteMarker> normalizedMarkers = normaliseUserNoteMarkers(noteMarkers, -1);
    const QString userMarkersJson = serializeUserNoteMarkersJson(normalizedMarkers);
    const qint32 legacyMarkerFrame = normalizedMarkers.isEmpty() ? -1 : normalizedMarkers.first().frame;
    const QString legacyMarkerComment = normalizedMarkers.isEmpty() ? QString() : normalizedMarkers.first().comment;

    bool changed = false;
    if (videoParameters.userMarkersJson != userMarkersJson) {
        videoParameters.userMarkersJson = userMarkersJson;
        changed = true;
    }
    if (videoParameters.userMarkerSelection != legacyMarkerFrame) {
        videoParameters.userMarkerSelection = legacyMarkerFrame;
        changed = true;
    }
    if (videoParameters.userMarkerComment != legacyMarkerComment) {
        videoParameters.userMarkerComment = legacyMarkerComment;
        changed = true;
    }
    return changed;
}

void noteMarkerListsFromMarkers(const QVector<UserNoteMarker> &noteMarkers,
                                QVector<qint32> &noteFrames,
                                QStringList &noteComments)
{
    noteFrames.clear();
    noteComments.clear();
    noteFrames.reserve(noteMarkers.size());
    noteComments.reserve(noteMarkers.size());
    for (const UserNoteMarker &noteMarker : noteMarkers) {
        if (noteMarker.frame <= 0) {
            continue;
        }
        noteFrames.append(noteMarker.frame);
        noteComments.append(noteMarker.comment);
    }
}

qint32 noteMarkerIndexForFrame(const QVector<UserNoteMarker> &noteMarkers, qint32 frame)
{
    for (qint32 i = 0; i < noteMarkers.size(); ++i) {
        if (noteMarkers.at(i).frame == frame) {
            return i;
        }
    }
    return -1;
}

qint32 nearestNoteMarkerIndexForFrame(const QVector<UserNoteMarker> &noteMarkers, qint32 frame)
{
    if (noteMarkers.isEmpty() || frame <= 0) {
        return -1;
    }

    qint32 nearestIndex = -1;
    qint32 nearestDistance = std::numeric_limits<qint32>::max();
    for (qint32 i = 0; i < noteMarkers.size(); ++i) {
        const qint32 candidateDistance = qAbs(noteMarkers.at(i).frame - frame);
        if (candidateDistance < nearestDistance) {
            nearestDistance = candidateDistance;
            nearestIndex = i;
        }
    }
    return nearestIndex;
}

qint32 sliderValueForContextPoint(const QSlider *slider, const QPoint &contextPos)
{
    if (!slider) {
        return 0;
    }

    QStyleOptionSlider option;
    option.initFrom(slider);
    option.orientation = slider->orientation();
    option.minimum = slider->minimum();
    option.maximum = slider->maximum();
    option.sliderPosition = slider->sliderPosition();
    option.sliderValue = slider->value();
    option.upsideDown = slider->invertedAppearance();

    const QRect grooveRect = slider->style()->subControlRect(QStyle::CC_Slider,
                                                              &option,
                                                              QStyle::SC_SliderGroove,
                                                              slider);
    if (!grooveRect.isValid() || grooveRect.width() <= 1) {
        return slider->value();
    }

    const qint32 boundedX = qBound(grooveRect.left(), contextPos.x(), grooveRect.right());
    const qint32 relativeX = boundedX - grooveRect.left();
    return QStyle::sliderValueFromPosition(slider->minimum(),
                                           slider->maximum(),
                                           relativeX,
                                           grooveRect.width() - 1,
                                           option.upsideDown);
}

#if defined(Q_OS_MACOS)
QString chooseFileViaAppleScript(const QString &startPath)
{
    QString directoryPath = startPath;
    QFileInfo pathInfo(directoryPath);
    if (directoryPath.isEmpty() || !pathInfo.exists()) {
        directoryPath = QDir::homePath();
    } else if (pathInfo.isFile()) {
        directoryPath = pathInfo.absolutePath();
    }

    QString escapedPath = directoryPath;
    escapedPath.replace(QStringLiteral("\\"), QStringLiteral("\\\\"));
    escapedPath.replace(QStringLiteral("\""), QStringLiteral("\\\""));

    const QString script = QStringLiteral(
        "set defaultLocation to POSIX file \"%1\"\n"
        "set chosenFile to choose file with prompt \"Open TBC/metadata file\" default location defaultLocation\n"
        "POSIX path of chosenFile").arg(escapedPath);

    QProcess process;
    process.start(QStringLiteral("/usr/bin/osascript"), {QStringLiteral("-e"), script});
    if (!process.waitForFinished(120000)) {
        return QString();
    }
    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
        return QString();
    }

    return QString::fromUtf8(process.readAllStandardOutput()).trimmed();
}
#endif
} // namespace

MainWindow::MainWindow(QString inputFilenameParam, bool metadataOnlyParam, QWidget *parent) :
    QMainWindow(parent),
    ui(new Ui::MainWindow)
{
    ui->setupUi(this);

    if (ui->posHorizontalSlider && ui->mediaControl_frame) {
        QSlider *existingSlider = ui->posHorizontalSlider;
        auto *replacementSlider = new TimelineMarkerSlider(ui->mediaControl_frame);
        replacementSlider->setObjectName(existingSlider->objectName());
        replacementSlider->setOrientation(existingSlider->orientation());
        replacementSlider->setMinimum(existingSlider->minimum());
        replacementSlider->setMaximum(existingSlider->maximum());
        replacementSlider->setSingleStep(existingSlider->singleStep());
        replacementSlider->setPageStep(existingSlider->pageStep());
        replacementSlider->setTracking(existingSlider->hasTracking());
        replacementSlider->setValue(existingSlider->value());
        replacementSlider->setEnabled(existingSlider->isEnabled());
        replacementSlider->setMinimumSize(existingSlider->minimumSize());
        replacementSlider->setMaximumSize(existingSlider->maximumSize());
        replacementSlider->setSizePolicy(existingSlider->sizePolicy());
        replacementSlider->setInvertedAppearance(existingSlider->invertedAppearance());
        replacementSlider->setInvertedControls(existingSlider->invertedControls());
        replacementSlider->setContextMenuPolicy(existingSlider->contextMenuPolicy());

        if (QLayout *sliderLayout = ui->mediaControl_frame->layout()) {
            sliderLayout->replaceWidget(existingSlider, replacementSlider);
        }

        existingSlider->deleteLater();
        ui->posHorizontalSlider = replacementSlider;
        timelineMarkerSlider = replacementSlider;

        connect(timelineMarkerSlider, &QSlider::valueChanged,
                this, &MainWindow::on_posHorizontalSlider_valueChanged);
        connect(timelineMarkerSlider, &QSlider::sliderPressed,
                this, &MainWindow::on_posHorizontalSlider_sliderPressed);
        connect(timelineMarkerSlider, &QSlider::sliderReleased,
                this, &MainWindow::on_posHorizontalSlider_sliderReleased);
        connect(timelineMarkerSlider, &QWidget::customContextMenuRequested,
                this, &MainWindow::on_posHorizontalSlider_customContextMenuRequested);
    }
    if (ui->posHorizontalSlider) {
        ui->posHorizontalSlider->setToolTip(tr("Use [ & ] keys to set in & out points at the current frame\n"
                                               "M — add/edit a marker comment at the current frame\n"
                                               "C — open the marker viewer"));
    }
    copyCurrentDisplayAction = new QAction(tr("Copy current display"), this);
    copyCurrentDisplayAction->setShortcut(QKeySequence::Copy);
    copyCurrentDisplayAction->setShortcutContext(Qt::WindowShortcut);
    connect(copyCurrentDisplayAction, &QAction::triggered,
            this, &MainWindow::on_actionCopy_current_display_to_clipboard_triggered);
    addAction(copyCurrentDisplayAction);

    saveAllModesPngAction = new QAction(tr("Save all mode views as PNGs..."), this);
    connect(saveAllModesPngAction, &QAction::triggered,
            this, &MainWindow::on_actionSave_all_modes_as_PNGs_triggered);
    if (ui->menuFile) {
        if (ui->actionExit) {
            ui->menuFile->insertAction(ui->actionExit, saveAllModesPngAction);
        } else {
            ui->menuFile->addAction(saveAllModesPngAction);
        }
    }
    setAcceptDrops(true);
    if (centralWidget()) {
        centralWidget()->setAcceptDrops(true);
    }
    if (ui->mainTabWidget) {
        ui->mainTabWidget->setAcceptDrops(true);
    }
    if (ui->viewerTab) {
        ui->viewerTab->setAcceptDrops(true);
    }
    if (ui->imageViewerLabel) {
        ui->imageViewerLabel->setAcceptDrops(true);
    }
    if (ui->mainTabWidget && ui->viewerTab) {
        ui->mainTabWidget->setCurrentWidget(ui->viewerTab);
    }
    if (ui->mainTabWidget) {
        connect(ui->mainTabWidget, &QTabWidget::currentChanged, this, [this](int) {
            if (!isViewerTabActive()) {
                clearCursorReadout();
                exportBoundaryDragHandle = ExportBoundaryHandle::None;
                exportBoundarySelectedHandle = ExportBoundaryHandle::None;
                updateExportBoundaryHoverCursor(QPoint(-1, -1));
                return;
            }
            if (resizeFrameWithWindow && tbcSource.getIsSourceLoaded() && isViewerTabActive()) {
                resizeTimer->start();
            }
        });
    }
    if (ui->posTimecodeLineEdit) {
        ui->posTimecodeLineEdit->setPlaceholderText(tr("HH:MM:SS:FF"));
        ui->posTimecodeLineEdit->setMaxLength(15);
        ui->posTimecodeLineEdit->setAlignment(Qt::AlignCenter);
        ui->posTimecodeLineEdit->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
        QFont valueFont = ui->posTimecodeLineEdit->font();
        valueFont.setPointSize(qMax(12, valueFont.pointSize() + 3));
        ui->posTimecodeLineEdit->setFont(valueFont);
        ui->posTimecodeLineEdit->setTextMargins(3, 0, 1, 0);
        ui->posTimecodeLineEdit->setMinimumHeight(30);
        ui->posTimecodeLineEdit->setMaximumHeight(30);
        const QFontMetrics valueMetrics(valueFont);
        const int valueMinWidth = valueMetrics.horizontalAdvance(QStringLiteral("00:00:00:00")) + 8;
        ui->posTimecodeLineEdit->setMinimumWidth(valueMinWidth);
        ui->posTimecodeLineEdit->setMaximumWidth(valueMinWidth + 4);
        ui->posTimecodeLineEdit->setVisible(false);
    }
    if (ui->posNumberSpinBox) {
        QFont spinFont = ui->posNumberSpinBox->font();
        spinFont.setPointSize(qMax(11, spinFont.pointSize() + 2));
        ui->posNumberSpinBox->setFont(spinFont);
        ui->posNumberSpinBox->setAlignment(Qt::AlignCenter);
        const QFontMetrics spinMetrics(spinFont);
        const int spinMinWidth = spinMetrics.horizontalAdvance(QStringLiteral("000000")) + 30;
        ui->posNumberSpinBox->setMinimumWidth(spinMinWidth);
        ui->posNumberSpinBox->setMaximumWidth(220);
    }
    if (ui->horizontalLayout_3 && ui->posTimecodeLineEdit) {
        const int timecodeIndex = ui->horizontalLayout_3->indexOf(ui->posTimecodeLineEdit);
        if (timecodeIndex >= 0) {
            ui->horizontalLayout_3->setStretch(timecodeIndex, 0);
        }
    }
    ui->posHorizontalSlider->setContextMenuPolicy(Qt::CustomContextMenu);
    ensureSvgButtonIcon(ui->startPushButton, QStringLiteral(":/icons/Graphics/start-frame.svg"));
    ensureSvgButtonIcon(ui->previousPushButton, QStringLiteral(":/icons/Graphics/prev-frame.svg"));
    ensureSvgButtonIcon(ui->playPushButton, QStringLiteral(":/icons/Graphics/start-playback.svg"));
    ensureSvgButtonIcon(ui->nextPushButton, QStringLiteral(":/icons/Graphics/next-frame.svg"));
    ensureSvgButtonIcon(ui->endPushButton, QStringLiteral(":/icons/Graphics/end-frame.svg"));
    ensureSvgButtonIcon(ui->zoomInPushButton, QStringLiteral(":/icons/Graphics/zoom-in.svg"));
    ensureSvgButtonIcon(ui->zoomOutPushButton, QStringLiteral(":/icons/Graphics/zoom-out.svg"));
    ensureSvgButtonIcon(ui->originalSizePushButton, QStringLiteral(":/icons/Graphics/zoom-original.svg"));
    ensureSvgButtonIcon(ui->mouseModePushButton, QStringLiteral(":/icons/Graphics/oscilloscope-target.svg"));
    vectorscopeSelectionPushButton = new QPushButton(ui->mediaControl_frame);
    vectorscopeSelectionPushButton->setObjectName(QStringLiteral("vectorscopeSelectionPushButton"));
    vectorscopeSelectionPushButton->setMinimumSize(QSize(30, 30));
    vectorscopeSelectionPushButton->setMaximumSize(QSize(30, 30));
    vectorscopeSelectionPushButton->setCheckable(true);
    vectorscopeSelectionPushButton->setChecked(false);
    vectorscopeSelectionPushButton->setToolTip(tr("Enable vectorscope custom-area selection on the main viewer"));
    ensureSvgButtonIcon(vectorscopeSelectionPushButton, QStringLiteral(":/icons/Graphics/highlight-selection.svg"));
    if (ui->horizontalLayout_3 && ui->mouseModePushButton) {
        const int mouseModeButtonIndex = ui->horizontalLayout_3->indexOf(ui->mouseModePushButton);
        if (mouseModeButtonIndex >= 0) {
            ui->horizontalLayout_3->insertWidget(mouseModeButtonIndex + 1, vectorscopeSelectionPushButton);
        } else {
            ui->horizontalLayout_3->addWidget(vectorscopeSelectionPushButton);
        }
    }
    connect(vectorscopeSelectionPushButton, &QPushButton::toggled,
            this, &MainWindow::on_vectorscopeSelectionPushButton_toggled);
    populateThemesMenu();

    // Set up dialogues
    oscilloscopeDialog = new OscilloscopeDialog(this);
    rgbScopeDialog = new RgbScopeDialog(this);
    yuvRangeDialog = new YuvRangeDialog(this);
    yuvRangeSettings = yuvRangeDialog->settings();
    vectorscopeDialog = new VectorscopeDialog(this);
    waveformMonitorDialog = new WaveformMonitorDialog(this);
    fieldTimingDialog = new FieldTimingDialog(this);
    aboutDialog = new AboutDialog(this);
    vbiDialog = new VbiDialog(this);
    dropoutAnalysisDialog = new DropoutAnalysisDialog(this);
    visibleDropoutAnalysisDialog = new VisibleDropOutAnalysisDialog(this);
    blackSnrAnalysisDialog = new BlackSnrAnalysisDialog(this);
    whiteSnrAnalysisDialog = new WhiteSnrAnalysisDialog(this);
    busyDialog = new BusyDialog(this);
    closedCaptionDialog = new ClosedCaptionsDialog(this);
    videoParametersDialog = new VideoParametersDialog(this);
    chromaDecoderConfigDialog = new ChromaDecoderConfigDialog(this);
    metadataConversionDialog = new MetadataConversionDialog(this);
    metadataStatusDialog = new MetadataStatusDialog(this);
    metadataEditorDialog = new MetadataEditorDialog(this);
    connect(metadataEditorDialog, &MetadataEditorDialog::videoParametersChanged,
            this, &MainWindow::videoParametersChangedSignalHandler);
    connect(metadataEditorDialog, &MetadataEditorDialog::pcmAudioParametersChanged,
            this, [this](const TbcMetaData::PcmAudioParameters &pcmAudioParameters) {
        if (!tbcSource.getIsSourceLoaded()) return;
        tbcSource.setPcmAudioParameters(pcmAudioParameters);
        ui->actionSave_Metadata->setEnabled(true);
    });
    // Apply: save the metadata to disk. TbcSource::saveSourceMetadata() writes
    // to a .new file, backs up the original to .bup (timestamped fallback),
    // renames .new to the target, then on_finishedSaving reloads the source
    // with the new metadata.
    connect(metadataEditorDialog, &MetadataEditorDialog::refreshRequested,
            this, [this]() {
        if (!tbcSource.getIsSourceLoaded()) return;
        tbcSource.saveSourceMetadata();
    });
    // SECAM per-field first-line-identity edits from the Metadata Editor.
    connect(metadataEditorDialog, &MetadataEditorDialog::secamFirstLineIsRedChanged,
            this, [this](qint32 fieldNumber, bool value, bool applyToAll) {
        if (!tbcSource.getIsSourceLoaded()) return;
        tbcSource.setSecamFirstLineIsRed(fieldNumber, value, applyToAll);
        ui->actionSave_Metadata->setEnabled(true);
        updateMetadataStatusPanel();
        updateImage();
    });
    notesViewerDialog = new NotesViewerDialog(this);
    notesViewerDialog->setWindowFlag(Qt::Window, true);
    connect(notesViewerDialog, &NotesViewerDialog::goToFrameRequested, this, [this](qint32 frameNumber) {
        if (!tbcSource.getIsSourceLoaded()) {
            return;
        }
        setPlaybackRunning(false);
        const qint32 clampedFrame = qBound<qint32>(1, frameNumber, qMax<qint32>(1, tbcSource.getNumberOfFrames()));
        setCurrentFrame(clampedFrame);
        const qint32 currentNumber = tbcSource.getFieldViewEnabled() ? currentFieldNumber : currentFrameNumber;
        updatePositionEditorValue(currentNumber);
        ui->posHorizontalSlider->setValue(currentNumber);
    });
    connect(notesViewerDialog, &NotesViewerDialog::notesUpdated, this,
            [this](qint32 inFrame,
                   qint32 outFrame,
                   const QVector<qint32> &noteFrames,
                   const QStringList &noteComments) {
                if (!tbcSource.getIsSourceLoaded()) {
                    return;
                }

                const qint32 totalFrames = qMax<qint32>(1, tbcSource.getNumberOfFrames());
                qint32 clampedIn = (inFrame > 0) ? qBound<qint32>(1, inFrame, totalFrames) : -1;
                qint32 clampedOut = (outFrame > 0) ? qBound<qint32>(1, outFrame, totalFrames) : -1;
                if (clampedIn > 0 && clampedOut > 0 && clampedOut < clampedIn) {
                    clampedOut = clampedIn;
                }

                QVector<UserNoteMarker> updatedMarkers;
                updatedMarkers.reserve(noteFrames.size());
                for (qint32 i = 0; i < noteFrames.size(); ++i) {
                    const QString noteComment = (i < noteComments.size()) ? noteComments.at(i) : QString();
                    updatedMarkers.append({noteFrames.at(i), noteComment});
                }
                updatedMarkers = normaliseUserNoteMarkers(updatedMarkers, totalFrames);

                TbcMetaData::VideoParameters videoParameters = tbcSource.getVideoParameters();
                bool changed = false;
                if (videoParameters.userEditInSelection != clampedIn) {
                    videoParameters.userEditInSelection = clampedIn;
                    changed = true;
                }
                if (videoParameters.userEditOutSelection != clampedOut) {
                    videoParameters.userEditOutSelection = clampedOut;
                    changed = true;
                }
                if (applyUserNoteMarkersToVideoParameters(videoParameters, updatedMarkers)) {
                    changed = true;
                }
                if (!changed) {
                    return;
                }

                tbcSource.setVideoParameters(videoParameters);
                ui->actionSave_Metadata->setEnabled(true);
                updateMetadataStatusPanel();
                updateTimelineMarkers();
                updateNotesViewerState();
            });
    exportDialog = new ExportDialog(this);
    ui->mainTabWidget->addTab(exportDialog, tr("Export"));
    exportDialog->setGenerateProxyEnabledPreference(configuration.getGenerateProxyEnabled());
    exportDialog->setExportProfileConfigPreference(configuration.getExportProfileConfigEnabled(),
                                                   configuration.getExportProfileConfigPath());
    connect(exportDialog, &ExportDialog::userEditRangeSelectionChanged,
            this, &MainWindow::exportRangeSelectionChangedSignalHandler);
    connect(exportDialog, &ExportDialog::proxyGenerationPreferenceChanged, this, [this](bool enabled) {
        configuration.setGenerateProxyEnabled(enabled);
        configuration.writeConfiguration();
    });
    connect(exportDialog, &ExportDialog::exportProfileConfigPreferenceChanged, this,
            [this](bool enabled, const QString &configPath) {
                configuration.setExportProfileConfigEnabled(enabled);
                configuration.setExportProfileConfigPath(configPath);
                configuration.writeConfiguration();
            });

    notesViewerAction = new QAction(tr("Marker Viewer..."), this);
    if (ui->menuWindow) {
        ui->menuWindow->addSeparator();
        ui->menuWindow->addAction(notesViewerAction);
    }
    connect(notesViewerAction, &QAction::triggered, this, [this]() {
        updateNotesViewerState();
        notesViewerDialog->show();
        notesViewerDialog->raise();
        notesViewerDialog->activateWindow();
    });

    // Add a status bar to show the state of the source video file
    ui->statusBar->addWidget(&sourceVideoStatus);
    ui->statusBar->addWidget(&fieldNumberStatus);
    ui->statusBar->addWidget(&vbiStatus);
    ui->statusBar->addWidget(&timeCodeStatus);
    ui->statusBar->addPermanentWidget(&cursorStatus, 1);
    sourceVideoStatus.setText(tr("No source video file loaded"));
    fieldNumberStatus.setText(tr(" Frames: ./. Fields: ./."));
    vbiStatus.hide();
    timeCodeStatus.hide();
    cursorStatus.setText(tr(" Cursor: --"));

    // Center every pop-out sub-window over this main window when it is shown
    // (project UX rule). The filter acts on Qt::Window/Qt::Dialog top-levels
    // that belong to this MainWindow, excluding popups/menus/tooltips.
    qApp->installEventFilter(this);

    if (ui->imageViewerLabel) {
        ui->imageViewerLabel->setMouseTracking(true);
        ui->imageViewerLabel->installEventFilter(this);
    }
    if (ui->scrollArea) {
        ui->scrollArea->setMouseTracking(true);
        if (ui->scrollArea->viewport()) {
            ui->scrollArea->viewport()->setMouseTracking(true);
            ui->scrollArea->viewport()->installEventFilter(this);
        }
    }

    // Set the initial field/frame number
    setCurrentFrame(1);

    // Connect to the scan line changed signal from the oscilloscope dialogue
    connect(oscilloscopeDialog, &OscilloscopeDialog::scopeCoordsChanged, this, &MainWindow::scopeCoordsChangedSignalHandler);
    lastScopeLine = 350;
    lastScopeDot = 1;

    // Make shift-clicking on the oscilloscope change the black/white level
    connect(oscilloscopeDialog, &OscilloscopeDialog::scopeLevelSelect, chromaDecoderConfigDialog, &ChromaDecoderConfigDialog::levelSelected);

    // Connect to the changed signal from the vectorscope dialogue
    connect(vectorscopeDialog, &VectorscopeDialog::scopeChanged, this, &MainWindow::vectorscopeChangedSignalHandler);
    connect(rgbScopeDialog, &RgbScopeDialog::renderTargetSizeChanged, this, [this](const QSize &) {
        if (!rgbScopeDialog || !rgbScopeDialog->isVisible() || rgbScopeDialog->isMinimized()) {
            return;
        }
        updateRgbScopeDialogue(false);
    });
    connect(yuvRangeDialog, &YuvRangeDialog::renderTargetSizeChanged, this, [this](const QSize &) {
        if (!yuvRangeDialog || !yuvRangeDialog->isVisible() || yuvRangeDialog->isMinimized()) {
            return;
        }
        updateYuvRangeScopeDialogue(false);
    });
    connect(yuvRangeDialog, &YuvRangeDialog::settingsChanged, this, [this](const YuvRangeSettings &settings) {
        yuvRangeSettings = settings;
        if (yuvRangeDialog && yuvRangeDialog->isVisible() && !yuvRangeDialog->isMinimized()) {
            updateYuvRangeScopeDialogue(true);
        }
        updateImageViewer();
    });

    // Connect to the video parameters changed signal
    connect(videoParametersDialog, &VideoParametersDialog::videoParametersChanged, this, &MainWindow::videoParametersChangedSignalHandler);
    connect(videoParametersDialog, &VideoParametersDialog::exportBoundaryToggled, this, &MainWindow::exportBoundaryToggledSignalHandler);
    connect(videoParametersDialog, &VideoParametersDialog::exportBoundaryThicknessChanged, this, &MainWindow::exportBoundaryThicknessChangedSignalHandler);

    showExportBoundary = configuration.getShowExportBoundary();
    videoParametersDialog->setShowExportBoundary(showExportBoundary);
    exportBoundaryThickness = configuration.getExportBoundaryThickness();
    videoParametersDialog->setExportBoundaryThickness(exportBoundaryThickness);

    // Connect to the chroma decoder configuration changed signal
    connect(chromaDecoderConfigDialog, &ChromaDecoderConfigDialog::chromaDecoderConfigChanged, this, &MainWindow::chromaDecoderConfigChangedSignalHandler);
    connect(chromaDecoderConfigDialog, &ChromaDecoderConfigDialog::videoLevelsChanged, this, &MainWindow::videoLevelsChangedSignalHandler);

    // Connect to the TbcSource signals (busy and finished loading)
    connect(&tbcSource, &TbcSource::busy, this, &MainWindow::on_busy);
    connect(&tbcSource, &TbcSource::finishedLoading, this, &MainWindow::on_finishedLoading);
    connect(&tbcSource, &TbcSource::finishedSaving, this, &MainWindow::on_finishedSaving);
    connect(&asyncFrameRenderWatcher, &QFutureWatcher<QImage>::finished,
            this, &MainWindow::on_asyncFrameRenderFinished);

    // Load the window geometry and settings from the configuration
    const QByteArray savedMainGeometry = configuration.getMainWindowGeometry();
    if (!savedMainGeometry.isEmpty()) {
        restoreGeometry(savedMainGeometry);
    }
    scaleFactor = configuration.getMainWindowScaleFactor();

    vbiDialog->restoreGeometry(configuration.getVbiDialogGeometry());
    oscilloscopeDialog->restoreGeometry(configuration.getOscilloscopeDialogGeometry());
    if (!vectorscopeDialog->restoreGeometry(configuration.getVectorscopeDialogGeometry())) {
        vectorscopeDialog->resize(900, 730);
    }
    if (!waveformMonitorDialog->restoreGeometry(configuration.getWaveformMonitorDialogGeometry())) {
        waveformMonitorDialog->resize(900, 500);
    }
    dropoutAnalysisDialog->restoreGeometry(configuration.getDropoutAnalysisDialogGeometry());
    visibleDropoutAnalysisDialog->restoreGeometry(configuration.getVisibleDropoutAnalysisDialogGeometry());
    blackSnrAnalysisDialog->restoreGeometry(configuration.getBlackSnrAnalysisDialogGeometry());
    whiteSnrAnalysisDialog->restoreGeometry(configuration.getWhiteSnrAnalysisDialogGeometry());
    closedCaptionDialog->restoreGeometry(configuration.getClosedCaptionDialogGeometry());
    videoParametersDialog->restoreGeometry(configuration.getVideoParametersDialogGeometry());
    chromaDecoderConfigDialog->restoreGeometry(configuration.getChromaDecoderConfigDialogGeometry());

    // Load view options from configuration
    resizeFrameWithWindow = configuration.getResizeFrameWithWindow();
    ui->actionResizeFrameWithWindow->setChecked(resizeFrameWithWindow);

    // Store the current button palette for the show dropouts button
    // Use application palette to ensure it respects theme settings
    buttonPalette = QApplication::palette();

    // Initialize playback timer
    playbackTimer = new QTimer(this);
    playbackTimer->setSingleShot(true);
    playbackTimer->setTimerType(Qt::PreciseTimer);
    connect(playbackTimer, &QTimer::timeout, this, &MainWindow::stepPlayback);
    
    // Initialize resize timer for delayed frame resizing
    resizeTimer = new QTimer(this);
    resizeTimer->setSingleShot(true);
    resizeTimer->setInterval(100); // 100ms delay for resize calculations
    connect(resizeTimer, &QTimer::timeout, this, &MainWindow::resizeFrameToWindow);
    rgbScopeRefreshTimer = new QTimer(this);
    rgbScopeRefreshTimer->setSingleShot(true);
    connect(rgbScopeRefreshTimer, &QTimer::timeout, this, [this]() {
        rgbScopeRefreshPending = false;
        if (rgbScopeDialog && rgbScopeDialog->isVisible()) {
            updateRgbScopeDialogue(true);
        }
    });
    yuvRangeScopeRefreshTimer = new QTimer(this);
    yuvRangeScopeRefreshTimer->setSingleShot(true);
    connect(yuvRangeScopeRefreshTimer, &QTimer::timeout, this, [this]() {
        yuvRangeScopeRefreshPending = false;
        if (yuvRangeDialog && yuvRangeDialog->isVisible()) {
            updateYuvRangeScopeDialogue(true);
        }
    });
    
    // Initialize chroma seek mode tracking
    chromaSeekMode = false;
    originalChromaState = false;
    
    // Set up button hold detection timer
    seekTimer = new QTimer(this);
    seekTimer->setSingleShot(true);
    seekTimer->setInterval(200); // 200ms to distinguish click from hold
    connect(seekTimer, &QTimer::timeout, this, [this]() {
        // Timer expired - enter chroma seek mode
        if (configuration.getToggleChromaDuringSeek() && tbcSource.getChromaDecoder()) {
            chromaSeekMode = true;
            originalChromaState = true;
            tbcSource.setChromaDecoder(false);
            updateVideoPushButton();
        }
    });

    chapterSkipHoldDelayTimer = new QTimer(this);
    chapterSkipHoldDelayTimer->setSingleShot(true);
    chapterSkipHoldDelayTimer->setInterval(220);
    chapterSkipHoldRepeatTimer = new QTimer(this);
    chapterSkipHoldRepeatTimer->setSingleShot(false);
    chapterSkipHoldRepeatTimer->setInterval(110);
    connect(chapterSkipHoldDelayTimer, &QTimer::timeout, this, [this]() {
        if (!chapterSkipHoldButton || !chapterSkipHoldButton->isDown() || !tbcSource.getIsSourceLoaded()) {
            return;
        }
        chapterSkipHoldTriggered = true;
        setPlaybackRunning(false);
        skipFramesBy(chapterSkipHoldDeltaFrames);
        chapterSkipHoldRepeatTimer->start();
    });
    connect(chapterSkipHoldRepeatTimer, &QTimer::timeout, this, [this]() {
        if (!chapterSkipHoldButton || !chapterSkipHoldButton->isDown() || !tbcSource.getIsSourceLoaded()) {
            chapterSkipHoldRepeatTimer->stop();
            return;
        }
        setPlaybackRunning(false);
        skipFramesBy(chapterSkipHoldDeltaFrames);
    });
    // Button press/release signals for chroma seek mode are auto-connected by Qt's auto-connection mechanism

    // Update checker - manual checks via the Help menu plus an automatic weekly
    // background check. The auto check is deferred so it never delays startup.
    updateChecker = new UpdateChecker(this);
    connect(updateChecker, &UpdateChecker::updateAvailable,
            this, &MainWindow::onUpdateAvailable);
    connect(updateChecker, &UpdateChecker::upToDate,
            this, &MainWindow::onUpdateUpToDate);
    connect(updateChecker, &UpdateChecker::checkFailed,
            this, &MainWindow::onUpdateCheckFailed);
    QTimer::singleShot(3000, this, [this]() { maybePerformWeeklyUpdateCheck(); });

    // Set the GUI to unloaded
    updateGuiUnloaded();
    
    // Load configuration settings
    ui->actionToggleChromaDuringSeek->setChecked(configuration.getToggleChromaDuringSeek());

    // Was a filename specified on the command line?
    if (!inputFilenameParam.isEmpty()) {
        lastFilename = inputFilenameParam;
        loadTbcFile(inputFilenameParam, metadataOnlyParam);
    } else {
        lastFilename.clear();
    }
}

MainWindow::~MainWindow()
{
    if (asyncFrameRenderInProgress) {
        cancelInFlightAsyncFrameRender();
    }
    // Save the window geometry and settings to the configuration
    configuration.setMainWindowGeometry(saveGeometry());
    configuration.setMainWindowScaleFactor(scaleFactor);
    configuration.setVbiDialogGeometry(vbiDialog->saveGeometry());
    configuration.setOscilloscopeDialogGeometry(oscilloscopeDialog->saveGeometry());
    configuration.setVectorscopeDialogGeometry(vectorscopeDialog->saveGeometry());
    configuration.setWaveformMonitorDialogGeometry(waveformMonitorDialog->saveGeometry());
    configuration.setDropoutAnalysisDialogGeometry(dropoutAnalysisDialog->saveGeometry());
    configuration.setVisibleDropoutAnalysisDialogGeometry(visibleDropoutAnalysisDialog->saveGeometry());
    configuration.setBlackSnrAnalysisDialogGeometry(blackSnrAnalysisDialog->saveGeometry());
    configuration.setWhiteSnrAnalysisDialogGeometry(whiteSnrAnalysisDialog->saveGeometry());
    configuration.setClosedCaptionDialogGeometry(closedCaptionDialog->saveGeometry());
    configuration.setVideoParametersDialogGeometry(videoParametersDialog->saveGeometry());
    configuration.setChromaDecoderConfigDialogGeometry(chromaDecoderConfigDialog->saveGeometry());
    configuration.writeConfiguration();

    // Close the source video if open
    if (tbcSource.getIsSourceLoaded()) {
        tbcSource.unloadSource();
    }
    cleanupTempMetadataFile();
    if (teletextViewerDialog) {
        teletextViewerDialog->close();
        delete teletextViewerDialog;
        teletextViewerDialog = nullptr;
    }

    delete ui;
}

void MainWindow::populateThemesMenu()
{
    if (!ui || !ui->menuThemes) {
        return;
    }

    if (!themesActionGroup) {
        themesActionGroup = new QActionGroup(this);
        themesActionGroup->setExclusive(true);
    }

    const QList<QAction *> existingActions = themesActionGroup->actions();
    for (QAction *action : existingActions) {
        themesActionGroup->removeAction(action);
    }
    ui->menuThemes->clear();

    QStringList availableThemes = QStyleFactory::keys();
    availableThemes.removeDuplicates();
    availableThemes.sort(Qt::CaseInsensitive);

    // Dedicated stock theme presets at the top of the dropdown. Dark is the
    // standard default; Light is a manual opt-in. Both drive the absolute
    // Fusion palette via ThemedApplication and are re-asserted across macOS
    // appearance switches. Exclusive with the Qt-style actions below.
    QAction *darkAction = ui->menuThemes->addAction(tr("Dark"));
    darkAction->setCheckable(true);
    darkAction->setData(QStringLiteral("stock-dark"));
    themesActionGroup->addAction(darkAction);
    connect(darkAction, &QAction::triggered, this, [this, darkAction](bool checked) {
        if (!checked) {
            return;
        }
        tbc::ui::applyStockDarkThemeToApp();
        refreshThemeDependentUi();
        // Mirror the user's successful "second press" behavior: re-apply once
        // on a short delay so late-resolving widgets (cached scope renders,
        // scene-backed controls) settle fully in one user click.
        QTimer::singleShot(24, this, [this, darkAction]() {
            if (!darkAction->isChecked()) {
                return;
            }
            tbc::ui::applyStockDarkThemeToApp();
            refreshThemeDependentUi();
        });
    });

    QAction *lightAction = ui->menuThemes->addAction(tr("Light"));
    lightAction->setCheckable(true);
    lightAction->setData(QStringLiteral("stock-light"));
    themesActionGroup->addAction(lightAction);
    connect(lightAction, &QAction::triggered, this, [this, lightAction](bool checked) {
        if (!checked) {
            return;
        }
        tbc::ui::applyStockLightThemeToApp();
        refreshThemeDependentUi();
        QTimer::singleShot(24, this, [this, lightAction]() {
            if (!lightAction->isChecked()) {
                return;
            }
            tbc::ui::applyStockLightThemeToApp();
            refreshThemeDependentUi();
        });
    });

    ui->menuThemes->addSeparator();

    if (availableThemes.isEmpty()) {
        QAction *placeholderAction = ui->menuThemes->addAction(tr("No Qt themes available"));
        placeholderAction->setEnabled(false);
        darkAction->setChecked(true);
        return;
    }

    const QString currentStyleName = QApplication::style() ? QApplication::style()->objectName() : QString();

    for (const QString &styleName : availableThemes) {
        QAction *themeAction = ui->menuThemes->addAction(styleName);
        themeAction->setCheckable(true);
        themeAction->setData(styleName);
        themesActionGroup->addAction(themeAction);

        if (!currentStyleName.isEmpty() &&
            styleName.compare(currentStyleName, Qt::CaseInsensitive) == 0) {
            themeAction->setChecked(true);
        }

        connect(themeAction, &QAction::triggered, this, [this, themeAction](bool checked) {
            if (!checked) {
                return;
            }
            applyThemeStyle(themeAction->data().toString());
        });
    }

    // Dark is the stock default; ensure it is checked. This unchecks any Qt
    // style action the loop above may have auto-checked (the stock preset
    // owns the active palette; style actions below are advanced overrides).
    darkAction->setChecked(true);
}

void MainWindow::applyThemeStyle(const QString &styleName)
{
    if (styleName.isEmpty()) {
        return;
    }

    const QString currentStyleName = QApplication::style() ? QApplication::style()->objectName() : QString();
    if (!currentStyleName.isEmpty() &&
        styleName.compare(currentStyleName, Qt::CaseInsensitive) == 0) {
        return;
    }

    QStyle *style = QStyleFactory::create(styleName);
    if (!style) {
        return;
    }

    QApplication::setStyle(style);
    buttonPalette = QApplication::palette();

    const QString activeStyleName = QApplication::style() ? QApplication::style()->objectName() : styleName;
    if (themesActionGroup) {
        for (QAction *action : themesActionGroup->actions()) {
            action->setChecked(action->data().toString().compare(activeStyleName, Qt::CaseInsensitive) == 0);
        }
    }
    refreshThemeDependentUi();
}

void MainWindow::refreshThemeDependentUi()
{
    buttonPalette = QApplication::palette();

    if (ui && ui->dropoutsPushButton) {
        if (tbcSource.getIsDropoutPresent()) {
            QPalette tempPalette = buttonPalette;
            tempPalette.setColor(QPalette::Button, QColor(Qt::lightGray));
            ui->dropoutsPushButton->setAutoFillBackground(true);
            ui->dropoutsPushButton->setPalette(tempPalette);
        } else {
            ui->dropoutsPushButton->setAutoFillBackground(true);
            ui->dropoutsPushButton->setPalette(buttonPalette);
        }
        ui->dropoutsPushButton->update();
    }

    if (!tbcSource.getIsSourceLoaded()) {
        return;
    }

    updateImageViewer();
    if (oscilloscopeDialog && oscilloscopeDialog->isVisible()) {
        updateOscilloscopeDialogue();
    }
    if (rgbScopeDialog && rgbScopeDialog->isVisible() && !rgbScopeDialog->isMinimized()) {
        updateRgbScopeDialogue(true);
    }
    if (yuvRangeDialog && yuvRangeDialog->isVisible() && !yuvRangeDialog->isMinimized()) {
        updateYuvRangeScopeDialogue(true);
    }
    if (vectorscopeDialog && vectorscopeDialog->isVisible()) {
        updateVectorscopeDialogue();
    }
    if (waveformMonitorDialog && waveformMonitorDialog->isVisible()) {
        updateWaveformMonitorDialogue();
    }
    if (fieldTimingDialog && fieldTimingDialog->isVisible()) {
        updateFieldTimingDialogue();
    }
    if (blackSnrAnalysisDialog && blackSnrAnalysisDialog->isVisible()) {
        blackSnrAnalysisDialog->update();
    }
    if (whiteSnrAnalysisDialog && whiteSnrAnalysisDialog->isVisible()) {
        whiteSnrAnalysisDialog->update();
    }
    if (dropoutAnalysisDialog && dropoutAnalysisDialog->isVisible()) {
        dropoutAnalysisDialog->update();
    }
    if (visibleDropoutAnalysisDialog && visibleDropoutAnalysisDialog->isVisible()) {
        visibleDropoutAnalysisDialog->update();
    }
}

bool MainWindow::event(QEvent *event)
{
    if (event && event->type() == QEvent::FileOpen) {
        const auto *fileOpenEvent = static_cast<QFileOpenEvent *>(event);
        QString filePath = fileOpenEvent->file();
        if (filePath.isEmpty() && fileOpenEvent->url().isLocalFile()) {
            filePath = fileOpenEvent->url().toLocalFile();
        }

        if (!filePath.isEmpty() && isSupportedInputExtension(filePath)) {
            requestSourceOpen(filePath);
            return true;
        }
    }

    const bool baseHandled = QMainWindow::event(event);

    if (event && (event->type() == QEvent::PaletteChange
                  || event->type() == QEvent::StyleChange)) {
        if (!themeRefreshPending) {
            themeRefreshPending = true;
            QTimer::singleShot(0, this, [this]() {
                themeRefreshPending = false;
                refreshThemeDependentUi();
            });
        }
    }

    return baseHandled;
}

// Update GUI methods for when TBC source files are loaded and unloaded -----------------------------------------------

// Enable or disable all the GUI controls
void MainWindow::setGuiEnabled(bool enabled)
{
    // Enable the field/frame controls
    ui->posNumberSpinBox->setEnabled(enabled);
    if (ui->posTimecodeLineEdit) {
        ui->posTimecodeLineEdit->setEnabled(enabled);
    }
    ui->previousPushButton->setEnabled(enabled);
    ui->nextPushButton->setEnabled(enabled);
    ui->startPushButton->setEnabled(enabled);
    ui->endPushButton->setEnabled(enabled);
    ui->playPushButton->setEnabled(enabled);
    ui->posHorizontalSlider->setEnabled(enabled);
    ui->mediaControl_frame->setEnabled(enabled);

    // Enable menu options
    ui->actionLine_scope->setEnabled(enabled);
    ui->actionRGB_scope->setEnabled(enabled);
    ui->actionYUV_range_scope->setEnabled(enabled);
    ui->actionVectorscope->setEnabled(enabled);
    ui->actionField_timing_scope->setEnabled(enabled);
    ui->actionVBI->setEnabled(enabled);
    ui->actionNTSC->setEnabled(enabled);
    ui->actionVideo_metadata->setEnabled(enabled);
    ui->actionVITS_Metrics->setEnabled(enabled);
    ui->actionZoom_In->setEnabled(enabled);
    ui->actionZoom_Out->setEnabled(enabled);
    ui->actionZoom_1x->setEnabled(enabled);
    ui->actionZoom_2x->setEnabled(enabled);
    ui->actionZoom_3x->setEnabled(enabled);
    ui->actionDropout_analysis->setEnabled(enabled);
    ui->actionVisible_Dropout_analysis->setEnabled(enabled);
    ui->actionSNR_analysis->setEnabled(enabled); // Black SNR
    ui->actionWhite_SNR_analysis->setEnabled(enabled);
    ui->actionSave_frame_as_PNG->setEnabled(enabled);
    if (saveAllModesPngAction) {
        saveAllModesPngAction->setEnabled(enabled);
    }
    if (copyCurrentDisplayAction) {
        copyCurrentDisplayAction->setEnabled(enabled);
    }
    ui->actionClosed_Captions->setEnabled(enabled);
    ui->actionVideo_parameters->setEnabled(enabled);
    ui->actionChroma_decoder_configuration->setEnabled(enabled);
    ui->actionReload_TBC->setEnabled(enabled);
    ui->actionOpen_TBC_file->setEnabled(true);
    if (notesViewerAction) {
        notesViewerAction->setEnabled(enabled);
    }

    // "Save Metadata" should be disabled by default
    ui->actionSave_Metadata->setEnabled(false);

    // Set zoom button states
    ui->zoomInPushButton->setEnabled(enabled);
    ui->zoomOutPushButton->setEnabled(enabled);
    ui->originalSizePushButton->setEnabled(enabled);
    if (vectorscopeSelectionPushButton) {
        vectorscopeSelectionPushButton->setEnabled(enabled);
    }
}


bool MainWindow::eventFilter(QObject *watched, QEvent *event)
{
    if (!ui || !event) {
        return QMainWindow::eventFilter(watched, event);
    }

    // Center pop-out sub-windows over the main window when they are shown.
    if (event->type() == QEvent::Show) {
        if (auto *widget = qobject_cast<QWidget *>(watched)) {
            if (widget != this && widget->isWindow()) {
                const Qt::WindowFlags flags = widget->windowFlags();
                const Qt::WindowType baseType =
                    static_cast<Qt::WindowType>(static_cast<int>(flags & Qt::WindowType_Mask));
                if (baseType == Qt::Window || baseType == Qt::Dialog) {
                    for (QWidget *p = widget->parentWidget(); p; p = p->parentWidget()) {
                        if (p == this) {
                            tbc::ui::centerDialogOverParent(widget);
                            break;
                        }
                    }
                }
            }
        }
    }

    const bool watchingImageLabel = (ui->imageViewerLabel && watched == ui->imageViewerLabel);
    const bool watchingViewport = (ui->scrollArea && ui->scrollArea->viewport() && watched == ui->scrollArea->viewport());
    if (watchingImageLabel || watchingViewport) {
        if (!tbcSource.getIsSourceLoaded() || !isViewerTabActive()) {
            if (event->type() == QEvent::MouseMove || event->type() == QEvent::Leave) {
                clearCursorReadout();
                updateExportBoundaryHoverCursor(QPoint(-1, -1));
            }
            return QMainWindow::eventFilter(watched, event);
        }

        if (event->type() == QEvent::MouseMove) {
            const auto *mouseEvent = static_cast<QMouseEvent *>(event);
            QPoint globalPos;
            if (watchingImageLabel) {
                globalPos = ui->imageViewerLabel->mapToGlobal(mouseEvent->pos());
            } else {
                globalPos = ui->scrollArea->viewport()->mapToGlobal(mouseEvent->pos());
            }
            const QPoint viewerPos = ui->imageViewerLabel->mapFromGlobal(globalPos);
            updateCursorReadout(viewerPos);
            if (exportBoundaryDragHandle == ExportBoundaryHandle::None) {
                updateExportBoundaryHoverCursor(viewerPos);
            }
        } else if (event->type() == QEvent::Wheel) {
            if (exportBoundarySelectedHandle == ExportBoundaryHandle::None) {
                return QMainWindow::eventFilter(watched, event);
            }
            const auto *wheelEvent = static_cast<QWheelEvent *>(event);
            const int rawDelta = (wheelEvent->angleDelta().y() != 0)
                                     ? wheelEvent->angleDelta().y()
                                     : wheelEvent->pixelDelta().y();
            if (rawDelta == 0) {
                return QMainWindow::eventFilter(watched, event);
            }
            const qint32 step = (rawDelta > 0) ? -1 : 1;
            applyExportBoundaryWheelStep(step);
            updateExportBoundaryHoverCursor(QPoint(-1, -1));
            event->accept();
            return true;
        } else if (event->type() == QEvent::Leave) {
            clearCursorReadout();
            updateExportBoundaryHoverCursor(QPoint(-1, -1));
        }
    }

    return QMainWindow::eventFilter(watched, event);
}

bool MainWindow::mapViewerToSourceCoordinates(const QPoint &viewerPoint, qint32 &sourceX, qint32 &sourceY) const
{
    if (!tbcSource.getIsSourceLoaded() || !ui || !ui->imageViewerLabel) {
        return false;
    }

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    const QPixmap viewerPixmap = ui->imageViewerLabel->pixmap();
#elif QT_VERSION >= QT_VERSION_CHECK(5, 15, 0)
    const QPixmap viewerPixmap = ui->imageViewerLabel->pixmap(Qt::ReturnByValue);
#else
    const QPixmap viewerPixmap = *(ui->imageViewerLabel->pixmap());
#endif
    if (viewerPixmap.isNull() || viewerPixmap.width() <= 0 || viewerPixmap.height() <= 0) {
        return false;
    }

    const double offsetX = (static_cast<double>(ui->imageViewerLabel->width()) - static_cast<double>(viewerPixmap.width())) / 2.0;
    const double offsetY = (static_cast<double>(ui->imageViewerLabel->height()) - static_cast<double>(viewerPixmap.height())) / 2.0;
    const double localX = static_cast<double>(viewerPoint.x()) - offsetX;
    const double localY = static_cast<double>(viewerPoint.y()) - offsetY;
    if (localX < 0.0 || localY < 0.0
        || localX >= static_cast<double>(viewerPixmap.width())
        || localY >= static_cast<double>(viewerPixmap.height())) {
        return false;
    }

    const qint32 sourceWidth = (!cursorReadoutImage.isNull() && cursorReadoutImage.width() > 0)
        ? cursorReadoutImage.width()
        : tbcSource.getFrameWidth();
    const qint32 sourceHeight = (!cursorReadoutImage.isNull() && cursorReadoutImage.height() > 0)
        ? cursorReadoutImage.height()
        : tbcSource.getFrameHeight();
    if (sourceWidth <= 0 || sourceHeight <= 0) {
        return false;
    }

    sourceX = qBound<qint32>(0,
                             static_cast<qint32>((localX / static_cast<double>(viewerPixmap.width())) * sourceWidth),
                             sourceWidth - 1);
    sourceY = qBound<qint32>(0,
                             static_cast<qint32>((localY / static_cast<double>(viewerPixmap.height())) * sourceHeight),
                             sourceHeight - 1);
    return true;
}

void MainWindow::updateCursorReadout(const QPoint &viewerPoint)
{
    qint32 sourceX = 0;
    qint32 sourceY = 0;
    if (!mapViewerToSourceCoordinates(viewerPoint, sourceX, sourceY)) {
        clearCursorReadout();
        return;
    }

    QColor rgb = Qt::black;
    if (!cursorReadoutImage.isNull()
        && sourceX >= 0 && sourceX < cursorReadoutImage.width()
        && sourceY >= 0 && sourceY < cursorReadoutImage.height()) {
        rgb = cursorReadoutImage.pixelColor(sourceX, sourceY);
    }

    QString rawHex = QStringLiteral("----");
    const bool asyncNnRenderActive = asyncFrameRenderInProgress && shouldRenderFrameAsync();
    if (!asyncNnRenderActive) {
        const TbcSource::ScanLineData scanLineData = tbcSource.getScanLineData(sourceY + 1);
        if (!scanLineData.composite.isEmpty() && sourceX >= 0 && sourceX < scanLineData.composite.size()) {
            const quint16 rawValue = static_cast<quint16>(qBound(0, scanLineData.composite[sourceX], 65535));
            rawHex = QStringLiteral("0x%1").arg(rawValue, 4, 16, QChar('0')).toUpper();
        }
    }

    const QString rgbHex = QStringLiteral("#%1%2%3")
                               .arg(rgb.red(), 2, 16, QChar('0'))
                               .arg(rgb.green(), 2, 16, QChar('0'))
                               .arg(rgb.blue(), 2, 16, QChar('0'))
                               .toUpper();
    cursorStatus.setText(QStringLiteral(" Cursor X:%1 Y:%2 RGB:%3 Raw:%4")
                             .arg(sourceX)
                             .arg(sourceY)
                             .arg(rgbHex)
                             .arg(rawHex));
}

void MainWindow::clearCursorReadout()
{
    cursorStatus.setText(tr(" Cursor: --"));
}

void MainWindow::dragEnterEvent(QDragEnterEvent *event)
{
    if (!event) {
        return;
    }

    if (!firstSupportedDroppedFile(event->mimeData()).isEmpty()) {
        event->acceptProposedAction();
    } else {
        event->ignore();
    }
}

void MainWindow::dragMoveEvent(QDragMoveEvent *event)
{
    if (!event) {
        return;
    }

    if (!firstSupportedDroppedFile(event->mimeData()).isEmpty()) {
        event->acceptProposedAction();
    } else {
        event->ignore();
    }
}

void MainWindow::dropEvent(QDropEvent *event)
{
    if (!event) {
        return;
    }

    const QString droppedFile = firstSupportedDroppedFile(event->mimeData());
    if (droppedFile.isEmpty()) {
        event->ignore();
        return;
    }

    event->acceptProposedAction();
    if (isTeletextStreamInputExtension(droppedFile)) {
        if (!teletextViewerDialog) {
            teletextViewerDialog = new TeletextViewerDialog(nullptr);
            teletextViewerDialog->setWindowFlag(Qt::Window, true);
        }
        QString errorMessage;
        if (!teletextViewerDialog->openTeletextStream(droppedFile, &errorMessage)) {
            QMessageBox::warning(this, tr("Teletext open failed"),
                                 errorMessage.isEmpty()
                                     ? tr("Could not open the dropped .tXX teletext stream.")
                                     : errorMessage);
            return;
        }
        teletextViewerDialog->show();
        teletextViewerDialog->raise();
        teletextViewerDialog->activateWindow();
        if (statusBar()) {
            statusBar()->showMessage(tr("Opened teletext stream: %1").arg(droppedFile), 5000);
        }
        return;
    }

    lastFilename = droppedFile;
    requestSourceOpen(droppedFile);
}

void MainWindow::requestSourceOpen(const QString &inputFileName)
{
    const QString normalizedInputFileName = QDir::cleanPath(inputFileName.trimmed());
    if (normalizedInputFileName.isEmpty()) {
        return;
    }

    lastFilename = normalizedInputFileName;
    const bool busy = sourceOperationInProgress
                      || (busyDialog && busyDialog->isVisible())
                      || !isEnabled();
    if (busy) {
        pendingSourceOpenFilename = normalizedInputFileName;
        return;
    }

    loadTbcFile(normalizedInputFileName);
}

void MainWindow::processPendingSourceOpenRequest()
{
    if (pendingSourceOpenFilename.isEmpty()) {
        return;
    }

    const bool busy = sourceOperationInProgress
                      || (busyDialog && busyDialog->isVisible())
                      || !isEnabled();
    if (busy) {
        return;
    }

    const QString queuedInputFile = pendingSourceOpenFilename;
    pendingSourceOpenFilename.clear();
    QTimer::singleShot(0, this, [this, queuedInputFile]() {
        requestSourceOpen(queuedInputFile);
    });
}

TbcSource& MainWindow::getTbcSource()
{
	return this->tbcSource;
}

void MainWindow::resetGui()
{
    setPlaybackRunning(false);
    ui->posNumberSpinBox->setMinimum(1);
    ui->posHorizontalSlider->setMinimum(1);

    ui->posNumberSpinBox->setValue(1);
    if (ui->posTimecodeLineEdit) {
        ui->posTimecodeLineEdit->setText(frameToTimecode(1));
        ui->posTimecodeLineEdit->setVisible(true);
    }
    ui->posHorizontalSlider->setValue(1);
   (this->width() >= 930) ? ui->dropoutsPushButton->setText(tr("Dropouts Off")) : ui->dropoutsPushButton->setText(tr("Drop N"));

    setViewValues();

    // Allow the next and previous frame buttons to auto-repeat
    ui->previousPushButton->setAutoRepeat(true);
    ui->previousPushButton->setAutoRepeatDelay(500);
    ui->previousPushButton->setAutoRepeatInterval(1);
    ui->nextPushButton->setAutoRepeat(true);
    ui->nextPushButton->setAutoRepeatDelay(500);
    ui->nextPushButton->setAutoRepeatInterval(1);

    // Set option button states
    updateVideoPushButton();
    displayAspectRatio = true;
    updateAspectPushButton();
    updateSourcesPushButton();
    if (this->width() > 1000)
		ui->fieldOrderPushButton->setText(tr("Normal Field-order"));
	else if (this->width() >= 930)
		ui->fieldOrderPushButton->setText(tr("Normal order"));
	else
		ui->fieldOrderPushButton->setText(tr("Normal"));

    // Zoom button options
    ui->zoomInPushButton->setAutoRepeat(true);
    ui->zoomInPushButton->setAutoRepeatDelay(500);
    ui->zoomInPushButton->setAutoRepeatInterval(100);
    ui->zoomOutPushButton->setAutoRepeat(true);
    ui->zoomOutPushButton->setAutoRepeatDelay(500);
    ui->zoomOutPushButton->setAutoRepeatInterval(100);

    // Initialize field stretch to 2:1 by default
    tbcSource.setStretchField(true);

    // Update the video parameters dialogue
    videoParametersDialog->setVideoParameters(tbcSource.getVideoParameters());

    // Update the chroma decoder configuration dialogue
    chromaDecoderConfigDialog->setConfiguration(tbcSource.getSystem(), tbcSource.getPalConfiguration(),
                                                tbcSource.getNtscConfiguration(),
                                                tbcSource.getMonoConfiguration(),
                                                tbcSource.getSourceMode(),
                                                true); // set to true because the chroma decoder is already init
    chromaDecoderConfigDialog->setVideoLevels(tbcSource.getVideoParameters());
}

// Method to update the GUI when a file is loaded
void MainWindow::updateGuiLoaded()
{
    // Enable the GUI controls
    setGuiEnabled(true);
    setPlaybackRunning(false);
    const bool metadataOnly = tbcSource.getIsMetadataOnly();

    if (metadataOnly) {
        ui->actionSave_frame_as_PNG->setEnabled(false);
        ui->actionLine_scope->setEnabled(false);
        ui->actionRGB_scope->setEnabled(false);
        ui->actionYUV_range_scope->setEnabled(false);
        ui->actionVectorscope->setEnabled(false);
        ui->actionField_timing_scope->setEnabled(false);
    }

    // Update the status bar readout
    updateBottomStatusReadout();
    // Update video mode button
    updateVideoPushButton();

    // Update source mode button
    updateSourcesPushButton();

    // Load and show the current image
    showImage();

    // Update the video parameters dialogue
    videoParametersDialog->setVideoParameters(tbcSource.getVideoParameters());

    // Update the chroma decoder configuration dialogue
    chromaDecoderConfigDialog->setConfiguration(tbcSource.getSystem(), tbcSource.getPalConfiguration(),
                                                tbcSource.getNtscConfiguration(),
                                                tbcSource.getMonoConfiguration(),
                                                tbcSource.getSourceMode(),
                                                false); // set to false to init the chroma decoder selection
    chromaDecoderConfigDialog->setVideoLevels(tbcSource.getVideoParameters());

    // Ensure the busy dialogue is hidden
    busyDialog->hide();

    // Disable "Save Metadata", now we've loaded the metadata into the GUI
    ui->actionSave_Metadata->setEnabled(false);

    // Keep load-time sizing stable: either fit frame to existing window, or
    // resize window to image size (legacy auto-resize behavior), but not both.
    if (resizeFrameWithWindow) {
        resizeTimer->start();
    } else if (autoResize) {
        resize_on_aspect();
    }
    updateTimelineMarkers();
    updateNotesViewerState();

    updateMetadataStatusPanel();
}

// Method to update the GUI when a file is unloaded
void MainWindow::updateGuiUnloaded()
{
    setPlaybackRunning(false);
    vectorscopeSelectionDragging = false;
    exportBoundaryDragHandle = ExportBoundaryHandle::None;
    exportBoundarySelectedHandle = ExportBoundaryHandle::None;
    updateExportBoundaryHoverCursor(QPoint(-1, -1));
    if (vectorscopeSelectionPushButton) {
        vectorscopeSelectionPushButton->setChecked(false);
    }
    // Disable the GUI controls
    setGuiEnabled(false);

    // Update the current field/frame number
    setCurrentFrame(1);
    ui->posNumberSpinBox->setValue(1);
    ui->posNumberSpinBox->setVisible(false);
    if (ui->posNumberSpinBoxLabel) {
        ui->posNumberSpinBoxLabel->setVisible(false);
    }
    if (ui->posTimecodeLineEdit) {
        ui->posTimecodeLineEdit->setText(frameToTimecode(1));
        ui->posTimecodeLineEdit->setVisible(true);
    }
    ui->posHorizontalSlider->setValue(1);

    // Set the window title
    this->setWindowTitle(tr("ld-analyse"));

    // Set the status bar text
    sourceVideoStatus.setText(tr("No source video file loaded"));
    fieldNumberStatus.setText(tr(" Frames: ./. Fields: ./."));
    vbiStatus.hide();
    timeCodeStatus.hide();

    // Set option button states
    updateVideoPushButton();
    (this->width() >= 930) ? ui->dropoutsPushButton->setText(tr("Dropouts Off")) : ui->dropoutsPushButton->setText(tr("Drop N"));
    displayAspectRatio = false;
    updateAspectPushButton();
    updateSourcesPushButton();
    if (this->width() > 1000)
		ui->fieldOrderPushButton->setText(tr("Normal Field-order"));
	else if (this->width() >= 930)
		ui->fieldOrderPushButton->setText(tr("Normal order"));
	else
		ui->fieldOrderPushButton->setText(tr("Normal"));

    // Hide the displayed image
    hideImage();

    // Hide graphs
    blackSnrAnalysisDialog->hide();
    whiteSnrAnalysisDialog->hide();
    dropoutAnalysisDialog->hide();

    // Hide configuration dialogues
    videoParametersDialog->hide();
    chromaDecoderConfigDialog->hide();
    fieldTimingDialog->hide();
    rgbScopeDialog->hide();
    yuvRangeDialog->hide();
    if (rgbScopeRefreshTimer) {
        rgbScopeRefreshTimer->stop();
    }
    rgbScopeRefreshPending = false;
    rgbScopeLastRefreshMs = 0;
    if (yuvRangeScopeRefreshTimer) {
        yuvRangeScopeRefreshTimer->stop();
    }
    yuvRangeScopeRefreshPending = false;
    yuvRangeScopeLastRefreshMs = 0;
    updateTimelineMarkers();
    updateNotesViewerState();

    updateMetadataStatusPanel();
    if (exportDialog) {
        exportDialog->setSource(&tbcSource);
    }
}

void MainWindow::updateVideoPushButton()
{
    if (!ui || !ui->videoPushButton) {
        return;
    }

    if (!tbcSource.getChromaDecoder()) {
        ui->videoPushButton->setText(tr("Source"));
        return;
    }

    switch (tbcSource.getChromaDecodeMode()) {
    case TbcSource::ACTIVE_ONLY_CHROMA_MODE:
        ui->videoPushButton->setText(tr("Active"));
        break;
    case TbcSource::HYBRID_CHROMA_MODE:
        ui->videoPushButton->setText(tr("Hybrid"));
        break;
    case TbcSource::FULL_FRAME_CHROMA_MODE:
        ui->videoPushButton->setText(tr("Chroma"));
        break;
    }
}
// Update the aspect ratio button
void MainWindow::updateAspectPushButton()
{
    if (!displayAspectRatio) {
        ui->aspectPushButton->setText(tr("SAR 1:1"));
    } else if (tbcSource.getIsWidescreen()) {
        (this->width() >= 1020) ? ui->aspectPushButton->setText(tr("DAR 16:9")) : ui->aspectPushButton->setText(tr("16:9"));
    } else {
        ui->aspectPushButton->setText(tr("DAR 4:3"));
    }
}

// Update the source selection button
void MainWindow::updateSourcesPushButton()
{
	// Only show the button if there are multiple sources (not ONE_SOURCE) AND a source is loaded
	if (tbcSource.getSourceMode() != TbcSource::ONE_SOURCE && tbcSource.getIsSourceLoaded()) {
		ui->sourcesPushButton->setVisible(true);
	} else {
		// Hide the button by default (no source loaded or only one source)
		ui->sourcesPushButton->setVisible(false);
		chromaDecoderConfigDialog->updateSourceMode(tbcSource.getSourceMode());
		return;
	}
	
	if (this->width() >= 930)
	{
		switch (tbcSource.getSourceMode()) {
		case TbcSource::ONE_SOURCE:
			// This case should not be reached due to early return above
			break;
		case TbcSource::LUMA_SOURCE:
			ui->sourcesPushButton->setText(tr("Y Source"));
			break;
		case TbcSource::CHROMA_SOURCE:
			ui->sourcesPushButton->setText(tr("C Source"));
			break;
		case TbcSource::BOTH_SOURCES:
			ui->sourcesPushButton->setText(tr("Y/C Sources"));
			break;
		}
	}
	else
	{
		switch (tbcSource.getSourceMode()) {
		case TbcSource::ONE_SOURCE:
			// This case should not be reached due to early return above
			break;
		case TbcSource::LUMA_SOURCE:
			ui->sourcesPushButton->setText(tr("Y"));
			break;
		case TbcSource::CHROMA_SOURCE:
			ui->sourcesPushButton->setText(tr("C"));
			break;
		case TbcSource::BOTH_SOURCES:
			ui->sourcesPushButton->setText(tr("Y/C"));
			break;
		}
	}
	chromaDecoderConfigDialog->updateSourceMode(tbcSource.getSourceMode());
}

void MainWindow::updateMetadataStatusPanel()
{
    if (!ui || !metadataStatusDialog) {
        return;
    }
    MetadataStatusData data;

    if (!tbcSource.getIsSourceLoaded()) {
        data.dbPath = QStringLiteral("—");
        data.jsonPath = QStringLiteral("—");
        data.videoSystem = QStringLiteral("—");
        data.chromaDecoder = QStringLiteral("—");
        data.chromaGain = QStringLiteral("—");
        data.chromaPhase = QStringLiteral("—");
        data.lumaNr = QStringLiteral("—");
        data.ntscAdaptive = QStringLiteral("—");
        data.ntscAdaptThreshold = QStringLiteral("—");
        data.ntscChromaWeight = QStringLiteral("—");
        data.ntscPhaseComp = QStringLiteral("—");
        data.palTransformThreshold = QStringLiteral("—");
        data.savePending = QStringLiteral("No");
        metadataStatusDialog->updateStatus(data);
        return;
    }

    const TbcMetaData::VideoParameters &videoParameters = tbcSource.getVideoParameters();

    const QString metadataPath = tbcSource.getCurrentMetadataFilename();
    const bool metadataIsJson = metadataPath.endsWith(".json", Qt::CaseInsensitive);
    if (metadataIsJson) {
        data.dbPath = QStringLiteral("—");
        data.jsonPath = metadataPath;
    } else {
        data.dbPath = formatOptionalString(metadataPath);
        if (metadataJsonLoaded && !metadataJsonFilename.isEmpty()) {
            data.jsonPath = metadataJsonFilename;
        } else {
            data.jsonPath = QStringLiteral("—");
        }
    }
    data.videoSystem = tbcSource.getSystemDescription();
    data.chromaDecoder = formatOptionalString(videoParameters.chromaDecoder);
    data.chromaGain = formatOptionalDouble(videoParameters.chromaGain);
    data.chromaPhase = formatOptionalDouble(videoParameters.chromaPhase);
    data.lumaNr = formatOptionalDouble(videoParameters.lumaNR);
    data.ntscAdaptive = formatOptionalBoolFromInt(videoParameters.ntscAdaptive);
    data.ntscAdaptThreshold = formatOptionalDouble(videoParameters.ntscAdaptThreshold);
    data.ntscChromaWeight = formatOptionalDouble(videoParameters.ntscChromaWeight);
    data.ntscPhaseComp = formatOptionalBoolFromInt(videoParameters.ntscPhaseCompensation);
    data.palTransformThreshold = formatOptionalDouble(videoParameters.palTransformThreshold);
    data.savePending = ui->actionSave_Metadata->isEnabled() ? QStringLiteral("Yes") : QStringLiteral("No");

    metadataStatusDialog->updateStatus(data);
}

// Frame display methods ----------------------------------------------------------------------------------------------

bool MainWindow::shouldRenderFrameAsync() const
{
    if (!tbcSource.getIsSourceLoaded() || tbcSource.getIsMetadataOnly()) {
        return false;
    }
    if (!tbcSource.getChromaDecoder()) {
        return false;
    }
    if (tbcSource.getSystem() != NTSC) {
        return false;
    }
    return tbcSource.getNtscConfiguration().nnTransform3D;
}

void MainWindow::startAsyncFrameRender()
{
    if (asyncFrameRenderInProgress || !shouldRenderFrameAsync()) {
        return;
    }

    asyncFrameRenderInProgress = true;
    asyncFrameRenderQueued = false;
    asyncFrameRenderFrameNumber = currentFrameNumber;
    asyncFrameRenderFieldNumber = currentFieldNumber;

#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    QFuture<QImage> renderFuture = QtConcurrent::run(&tbcSource, &TbcSource::getImage);
#else
    QFuture<QImage> renderFuture = QtConcurrent::run(&TbcSource::getImage, &tbcSource);
#endif
    asyncFrameRenderWatcher.setFuture(renderFuture);

    if (statusBar()) {
        statusBar()->showMessage(tr("Rendering nnTransform3D frame..."));
    }
}

void MainWindow::on_asyncFrameRenderFinished()
{
    asyncFrameRenderInProgress = false;
    if (!tbcSource.getIsSourceLoaded() || tbcSource.getIsMetadataOnly()) {
        asyncFrameImage = QImage();
        asyncFrameRenderQueued = false;
        if (statusBar()) {
            statusBar()->clearMessage();
        }
        return;
    }
    asyncFrameImage = asyncFrameRenderWatcher.result();

    if (asyncFrameRenderQueued
        || asyncFrameRenderFrameNumber != currentFrameNumber
        || asyncFrameRenderFieldNumber != currentFieldNumber) {
        asyncFrameRenderQueued = false;
        QTimer::singleShot(0, this, [this]() {
            showImage();
        });
        return;
    }

    if (statusBar()) {
        statusBar()->clearMessage();
    }
    updateImage();
}

void MainWindow::cancelInFlightAsyncFrameRender()
{
    if (!asyncFrameRenderInProgress) {
        return;
    }
    tbcSource.requestNnTransform3DCancel();
    asyncFrameRenderWatcher.waitForFinished();
    asyncFrameRenderInProgress = false;
    asyncFrameRenderQueued = false;
    asyncFrameRenderFrameNumber = -1;
    asyncFrameRenderFieldNumber = -1;
    asyncFrameImage = QImage();
    if (statusBar()) {
        statusBar()->clearMessage();
    }
}

// Update the UI and displays when currentFrameNumber or currentFieldNumber has changed
void MainWindow::showImage()
{
    if (asyncFrameRenderInProgress) {
        if (shouldRenderFrameAsync()) {
            asyncFrameRenderQueued = true;
            return;
        }
        cancelInFlightAsyncFrameRender();
        asyncFrameImage = QImage();
    }
    tbcSource.load(currentFrameNumber, currentFieldNumber);

    updateBottomStatusReadout();

    // Show VBI position in the status bar, if available
    if (tbcSource.getIsFrameVbiValid()) {
        VbiDecoder::Vbi vbi = tbcSource.getFrameVbi();
        if (vbi.clvHr != -1) {
            vbiStatus.setText(QString(" -  CLV time code: %1:%2:%3")
                                  .arg(vbi.clvHr, 2, 10, QChar('0'))
                                  .arg(vbi.clvMin, 2, 10, QChar('0'))
                                  .arg(vbi.clvSec, 2, 10, QChar('0')));
            vbiStatus.show();
        } else if (vbi.picNo != -1) {
            vbiStatus.setText(QString(" -  CAV picture number: %1")
                                  .arg(vbi.picNo, 5, 10, QChar('0')));
            vbiStatus.show();
        } else {
            vbiStatus.hide();
        }
    } else {
        vbiStatus.hide();
    }

    // Show timecode in the status bar, if available
    if (tbcSource.getIsFrameVitcValid()) {
        // Use ; rather than : if the drop flag is set (as ffmpeg does)
        VitcDecoder::Vitc vitc = tbcSource.getFrameVitc();
        timeCodeStatus.setText(QString(" -  VITC time code: %1:%2:%3%4%5")
                                   .arg(vitc.hour, 2, 10, QChar('0'))
                                   .arg(vitc.minute, 2, 10, QChar('0'))
                                   .arg(vitc.second, 2, 10, QChar('0'))
                                   .arg(vitc.isDropFrame ? QChar(';') : QChar(':'))
                                   .arg(vitc.frame, 2, 10, QChar('0')));
        timeCodeStatus.show();
    } else {
        timeCodeStatus.hide();
    }

    // If there are dropouts in the frame, highlight the show dropouts button
    if (tbcSource.getIsDropoutPresent()) {
        QPalette tempPalette = buttonPalette;
        tempPalette.setColor(QPalette::Button, QColor(Qt::lightGray));
        ui->dropoutsPushButton->setAutoFillBackground(true);
        ui->dropoutsPushButton->setPalette(tempPalette);
        ui->dropoutsPushButton->update();
    } else {
        ui->dropoutsPushButton->setAutoFillBackground(true);
        ui->dropoutsPushButton->setPalette(buttonPalette);
        ui->dropoutsPushButton->update();
    }

    // Update the VBI dialogue
    if (vbiDialog->isVisible()) {
        vbiDialog->updateVbi(tbcSource.getFrameVbi(), tbcSource.getIsFrameVbiValid());
        vbiDialog->updateVideoId(tbcSource.getFrameVideoId(), tbcSource.getIsFrameVideoIdValid());
    }

    // Add the QImage to the QLabel in the dialogue
    ui->imageViewerLabel->clear();
    ui->imageViewerLabel->setScaledContents(false);
    ui->imageViewerLabel->setAlignment(Qt::AlignCenter);

    // Update the field/frame image
    if (shouldRenderFrameAsync()) {
        startAsyncFrameRender();
        updateImageViewer();
    } else {
        updateImage();
    }

    // Update the closed caption dialog
    closedCaptionDialog->addData(currentFrameNumber, tbcSource.getCcData0(), tbcSource.getCcData1());

    // QT Bug workaround for some macOS versions
    #if defined(Q_OS_MACOS)
    	repaint();
    #endif
}

// Redraw all the GUI elements that depend on the decoded field/frame
void MainWindow::updateImage()
{
    // Update the image viewer
    updateImageViewer();
    if (asyncFrameRenderInProgress && shouldRenderFrameAsync()) {
        return;
    }

    // If the scope dialogues are open, update them
    if (oscilloscopeDialog->isVisible()) {
        updateOscilloscopeDialogue();
    }
    if (rgbScopeDialog->isVisible() && !rgbScopeDialog->isMinimized()) {
        updateRgbScopeDialogue();
    }
    if (yuvRangeDialog->isVisible() && !yuvRangeDialog->isMinimized()) {
        updateYuvRangeScopeDialogue();
    }
    if (vectorscopeDialog->isVisible()) {
        updateVectorscopeDialogue();
    }
    if (waveformMonitorDialog->isVisible()) {
        updateWaveformMonitorDialogue();
    }
    if (fieldTimingDialog->isVisible()) {
        updateFieldTimingDialogue();
    }
}

// Return the width adjustment for the current aspect mode
qint32 MainWindow::getAspectAdjustment() const {
    // Using source aspect ratio? No adjustment
    if (!displayAspectRatio) return 0;

    if (tbcSource.getSystem() == PAL) {
        // 625 lines
        if (tbcSource.getIsWidescreen()) return 103; // 16:9
        else return -196; // 4:3
    } else {
        // 525 lines
        if (tbcSource.getIsWidescreen()) return 122; // 16:9
        else return -150; // 4:3
    }
}

bool MainWindow::isViewerTabActive() const
{
    if (!ui || !ui->mainTabWidget || !ui->viewerTab) {
        return true;
    }
    return ui->mainTabWidget->currentWidget() == ui->viewerTab;
}

QImage MainWindow::renderedCurrentImageForExport()
{
    const bool useAsyncRender = shouldRenderFrameAsync();
    if (asyncFrameRenderInProgress && !useAsyncRender) {
        cancelInFlightAsyncFrameRender();
        asyncFrameImage = QImage();
    }
    QImage imageToSave;
    if (useAsyncRender) {
        imageToSave = asyncFrameImage;
        if (imageToSave.isNull() && !asyncFrameRenderInProgress) {
            startAsyncFrameRender();
        }
    } else {
        imageToSave = tbcSource.getImage();
    }
    if (imageToSave.isNull()) {
        return imageToSave;
    }

    const qint32 adjustment = getAspectAdjustment();
    if (adjustment != 0) {
        const qint32 adjustedWidth = imageToSave.size().width() + adjustment;
        if (adjustedWidth <= 0 || imageToSave.size().height() <= 0) {
            return QImage();
        }
        imageToSave = imageToSave.scaled(adjustedWidth,
                                         imageToSave.size().height(),
                                         Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    }
    return imageToSave;
}

QString MainWindow::outputRootDirectoryForCurrentSource()
{
    QString directoryPath;
    const QString currentSourceFilename = tbcSource.getCurrentSourceFilename();
    if (!currentSourceFilename.isEmpty()) {
        const QFileInfo sourceInfo(currentSourceFilename);
        directoryPath = sourceInfo.absolutePath();
    }
    if (directoryPath.isEmpty() && !lastFilename.isEmpty()) {
        const QFileInfo sourceInfo(lastFilename);
        directoryPath = sourceInfo.absolutePath();
    }
    if (directoryPath.isEmpty()) {
        directoryPath = configuration.getSourceDirectory();
    }
    if (directoryPath.isEmpty()) {
        directoryPath = QDir::currentPath();
    }
    return directoryPath;
}

QString MainWindow::outputBaseNameForCurrentSource()
{
    QString baseName;
    const QString currentSourceFilename = tbcSource.getCurrentSourceFilename();
    if (!currentSourceFilename.isEmpty()) {
        baseName = QFileInfo(currentSourceFilename).completeBaseName();
    }
    if (baseName.isEmpty() && !lastFilename.isEmpty()) {
        baseName = QFileInfo(lastFilename).completeBaseName();
    }
    if (baseName.isEmpty()) {
        baseName = QStringLiteral("tbc_capture");
    }
    return sanitizedFileToken(baseName);
}

void MainWindow::applyEfmHandlerAutoloads(const QString &directoryPath)
{
    if (!efmHandlerDialog) {
        return;
    }

    const QString normalizedDirectory = QDir::cleanPath(directoryPath.trimmed());
    const QFileInfo directoryInfo(normalizedDirectory);
    if (normalizedDirectory.isEmpty() || !directoryInfo.exists() || !directoryInfo.isDir()) {
        return;
    }

    QString sourceBaseName;
    const QString sourceFilename = tbcSource.getCurrentSourceFilename();
    if (!sourceFilename.isEmpty()) {
        sourceBaseName = QFileInfo(sourceFilename).completeBaseName();
    } else if (!lastFilename.isEmpty()) {
        sourceBaseName = QFileInfo(lastFilename).completeBaseName();
    }

    const EfmAutoloadCandidates candidates =
        discoverEfmAutoloadCandidates(normalizedDirectory, sourceBaseName);
    for (const QString &efmInput : candidates.efmInputs) {
        efmHandlerDialog->setDefaultEfmInput(efmInput);
    }
    if (!candidates.ac3Input.isEmpty()) {
        efmHandlerDialog->setDefaultAc3Input(candidates.ac3Input);
    }

    if (exportDialog
        && !candidates.pcmInputs.isEmpty()
        && !exportDialog->hasAudioTracksConfigured()) {
        const QStringList trackFiles = candidates.pcmInputs.mid(0, 4);
        QStringList trackNames;
        trackNames.reserve(trackFiles.size());
        for (const QString &trackFile : trackFiles) {
            trackNames << QFileInfo(trackFile).completeBaseName();
        }
        exportDialog->loadAudioTracksForExport(trackFiles, trackNames);
    }
}

// Redraw the viewer (for example, when scaleFactor has been changed)
void MainWindow::updateImageViewer()
{
    const bool useAsyncRender = shouldRenderFrameAsync();
    if (asyncFrameRenderInProgress && !useAsyncRender) {
        cancelInFlightAsyncFrameRender();
        asyncFrameImage = QImage();
    }
    QImage image;
    if (useAsyncRender) {
        image = asyncFrameImage;
        if (image.isNull() && !asyncFrameRenderInProgress) {
            startAsyncFrameRender();
        }
    } else {
        image = tbcSource.getImage();
        asyncFrameImage = QImage();
    }
    cursorReadoutImage = image;
    if (image.isNull() || image.width() == 0 || image.height() == 0) {
        cursorReadoutImage = QImage();
        clearCursorReadout();
        if (tbcSource.getIsMetadataOnly()) {
            ui->imageViewerLabel->setText(tr("Metadata-only mode (no TBC image data)"));
        } else if (useAsyncRender && asyncFrameRenderInProgress) {
            ui->imageViewerLabel->setText(tr("Rendering nnTransform3D frame..."));
        }
    }

    if (ui->mouseModePushButton->isChecked() && !image.isNull() && image.width() > 0 && image.height() > 0) {
        // Create a painter object
        QPainter imagePainter;
        imagePainter.begin(&image);

        // Draw lines to indicate the current scope position
        imagePainter.setPen(QColor(0, 255, 0, 127));
        imagePainter.drawLine(0, lastScopeLine - 1, tbcSource.getFrameWidth(), lastScopeLine - 1);
        imagePainter.drawLine(lastScopeDot, 0, lastScopeDot, tbcSource.getFrameHeight());

        // End the painter object
        imagePainter.end();
    }

    QPixmap pixmap = QPixmap::fromImage(image);

    // Get the aspect ratio adjustment if required
    qint32 adjustment = getAspectAdjustment();

    // Scale and apply the pixmap (only if it's valid)
    if (!pixmap.isNull()) {
        const int width = static_cast<int>(scaleFactor * (pixmap.size().width() + adjustment));
        const int height = static_cast<int>(scaleFactor * pixmap.size().height());
        QPixmap scaledPixmap = pixmap.scaled(width, height,
                                             Qt::IgnoreAspectRatio, Qt::SmoothTransformation);

        if (yuvRangeSettings.overlayEnabled && !tbcSource.getIsMetadataOnly()) {
            const QImage overlayImage = tbcSource.getYuvRangeOverlayImage(yuvRangeSettings);
            if (!overlayImage.isNull() && overlayImage.width() > 0 && overlayImage.height() > 0) {
                const QPixmap scaledOverlay = QPixmap::fromImage(overlayImage).scaled(
                    scaledPixmap.size(), Qt::IgnoreAspectRatio, Qt::FastTransformation);
                QPainter painter(&scaledPixmap);
                painter.setRenderHint(QPainter::Antialiasing, false);
                painter.drawPixmap(0, 0, scaledOverlay);
            }
        }

        if (showExportBoundary) {
            const QVector<QRect> activeRects = getActiveVideoRects();
            if (!activeRects.isEmpty() && pixmap.width() > 0 && pixmap.height() > 0) {
                const double scaleX = static_cast<double>(scaledPixmap.width()) / static_cast<double>(pixmap.width());
                const double scaleY = static_cast<double>(scaledPixmap.height()) / static_cast<double>(pixmap.height());
                QPainter painter(&scaledPixmap);
                painter.setRenderHint(QPainter::Antialiasing, false);

                QPen pen(QColor(255, 0, 0));
                const int thickness = (exportBoundaryThickness < 1) ? 1 : ((exportBoundaryThickness > 8) ? 8 : exportBoundaryThickness);
                pen.setWidth(thickness);
                pen.setJoinStyle(Qt::MiterJoin);
                painter.setPen(pen);
                painter.setBrush(Qt::NoBrush);

                const int inset = pen.width() / 2;
                for (const QRect &rect : activeRects) {
                    QRect scaledRect(qRound(rect.x() * scaleX),
                                     qRound(rect.y() * scaleY),
                                     qRound(rect.width() * scaleX),
                                     qRound(rect.height() * scaleY));
                    QRect borderRect = scaledRect.adjusted(inset, inset, -inset, -inset);
                    if (borderRect.width() > 0 && borderRect.height() > 0) {
                        painter.drawRect(borderRect);
                    }
                }
            }
        }

        const bool showVectorscopeSelection = vectorscopeDialog
            && (vectorscopeDialog->isCustomAreaModeSelected() || vectorscopeSelectionDragging);
        if (showVectorscopeSelection && !cursorReadoutImage.isNull()
            && cursorReadoutImage.width() > 0 && cursorReadoutImage.height() > 0) {
            const QRect sourceSelectionRect = vectorscopeDialog->customAreaRect();
            if (sourceSelectionRect.width() > 0 && sourceSelectionRect.height() > 0) {
                const double scaleX = static_cast<double>(scaledPixmap.width()) / static_cast<double>(cursorReadoutImage.width());
                const double scaleY = static_cast<double>(scaledPixmap.height()) / static_cast<double>(cursorReadoutImage.height());
                QRect scaledSelectionRect(qRound(sourceSelectionRect.x() * scaleX),
                                          qRound(sourceSelectionRect.y() * scaleY),
                                          qMax(1, qRound(sourceSelectionRect.width() * scaleX)),
                                          qMax(1, qRound(sourceSelectionRect.height() * scaleY)));
                scaledSelectionRect = scaledSelectionRect.intersected(QRect(0, 0, scaledPixmap.width(), scaledPixmap.height()));

                if (scaledSelectionRect.width() > 0 && scaledSelectionRect.height() > 0) {
                    QPainter painter(&scaledPixmap);
                    painter.setRenderHint(QPainter::Antialiasing, false);
                    QPen selectionPen(QColor(255, 255, 0, 220));
                    selectionPen.setWidth(2);
                    selectionPen.setStyle(Qt::DashLine);
                    painter.setPen(selectionPen);
                    painter.setBrush(Qt::NoBrush);
                    painter.drawRect(scaledSelectionRect.adjusted(0, 0, -1, -1));
                }
            }
        }

        ui->imageViewerLabel->setPixmap(scaledPixmap);
    }

    // Update the current frame markers on the graphs
    blackSnrAnalysisDialog->updateFrameMarker(currentFrameNumber);
    whiteSnrAnalysisDialog->updateFrameMarker(currentFrameNumber);
    dropoutAnalysisDialog->updateFrameMarker(currentFrameNumber);
    visibleDropoutAnalysisDialog->updateFrameMarker(currentFrameNumber);

    // QT Bug workaround for some macOS versions
    #if defined(Q_OS_MACOS)
    	repaint();
    #endif
}

QVector<QRect> MainWindow::getActiveVideoRects() const
{
    QVector<QRect> rects;

    if (!tbcSource.getIsSourceLoaded()) {
        return rects;
    }

    const TbcMetaData::VideoParameters &videoParameters = tbcSource.getVideoParameters();
    if (videoParameters.activeVideoStart < 0 || videoParameters.activeVideoEnd <= videoParameters.activeVideoStart) {
        return rects;
    }

    const int frameWidth = tbcSource.getFrameWidth();
    const int frameHeight = tbcSource.getFrameHeight();
    if (frameWidth <= 0 || frameHeight <= 0) {
        return rects;
    }

    const int rectWidth = videoParameters.activeVideoEnd - videoParameters.activeVideoStart;

    auto appendRect = [&](int x, int y, int width, int height) {
        if (width <= 0 || height <= 0) {
            return;
        }
        QRect rect(x, y, width, height);
        rect = rect.intersected(QRect(0, 0, frameWidth, frameHeight));
        if (rect.width() > 0 && rect.height() > 0) {
            rects.append(rect);
        }
    };

    switch (tbcSource.getViewMode()) {
    case TbcSource::ViewMode::FRAME_VIEW: {
        if (videoParameters.firstActiveFrameLine < 0 ||
            videoParameters.lastActiveFrameLine <= videoParameters.firstActiveFrameLine) {
            return rects;
        }
        const int height = videoParameters.lastActiveFrameLine - videoParameters.firstActiveFrameLine;
        appendRect(videoParameters.activeVideoStart, videoParameters.firstActiveFrameLine, rectWidth, height);
        break;
    }
    case TbcSource::ViewMode::SPLIT_VIEW: {
        if (videoParameters.firstActiveFieldLine < 0 ||
            videoParameters.lastActiveFieldLine <= videoParameters.firstActiveFieldLine) {
            return rects;
        }
        const int fieldHeight = videoParameters.lastActiveFieldLine - videoParameters.firstActiveFieldLine;
        appendRect(videoParameters.activeVideoStart, videoParameters.firstActiveFieldLine - 1, rectWidth, fieldHeight);
        appendRect(videoParameters.activeVideoStart,
                   videoParameters.firstActiveFieldLine + (frameHeight / 2),
                   rectWidth,
                   fieldHeight);
        break;
    }
    case TbcSource::ViewMode::FIELD_VIEW: {
        if (videoParameters.firstActiveFieldLine < 0 ||
            videoParameters.lastActiveFieldLine <= videoParameters.firstActiveFieldLine) {
            return rects;
        }
        const int fieldHeight = videoParameters.lastActiveFieldLine - videoParameters.firstActiveFieldLine;
        if (tbcSource.getStretchField()) {
            appendRect(videoParameters.activeVideoStart,
                       (videoParameters.firstActiveFieldLine - 1) * 2,
                       rectWidth,
                       fieldHeight * 2);
        } else {
            appendRect(videoParameters.activeVideoStart,
                       (videoParameters.firstActiveFieldLine - 1) + (frameHeight / 4),
                       rectWidth,
                       fieldHeight);
        }
        break;
    }
    case TbcSource::ViewMode::RGB_SCOPE_VIEW:
        break;
    }

    return rects;
}

bool MainWindow::isExportBoundaryDragAvailable() const
{
    if (!showExportBoundary || !tbcSource.getIsSourceLoaded() || !isViewerTabActive()) {
        return false;
    }
    if (!ui || !ui->imageViewerLabel || !ui->mouseModePushButton) {
        return false;
    }
    if (ui->mouseModePushButton->isChecked()) {
        return false;
    }
    if (vectorscopeSelectionDragging) {
        return false;
    }
    if (vectorscopeSelectionPushButton && vectorscopeSelectionPushButton->isChecked()) {
        return false;
    }
    if (tbcSource.getViewMode() != TbcSource::ViewMode::FRAME_VIEW) {
        return false;
    }
    return !getActiveVideoRects().isEmpty();
}

MainWindow::ExportBoundaryHandle MainWindow::exportBoundaryHandleAtViewerPoint(const QPoint &viewerPoint) const
{
    if (!isExportBoundaryDragAvailable()) {
        return ExportBoundaryHandle::None;
    }

    qint32 sourceX = 0;
    qint32 sourceY = 0;
    if (!mapViewerToSourceCoordinates(viewerPoint, sourceX, sourceY)) {
        return ExportBoundaryHandle::None;
    }

    const QVector<QRect> activeRects = getActiveVideoRects();
    if (activeRects.isEmpty()) {
        return ExportBoundaryHandle::None;
    }
    const QRect activeRect = activeRects.first();
    if (activeRect.width() <= 0 || activeRect.height() <= 0) {
        return ExportBoundaryHandle::None;
    }

    const qint32 left = activeRect.left();
    const qint32 right = activeRect.right();
    const qint32 top = activeRect.top();
    const qint32 bottom = activeRect.bottom();
    const qint32 handleTolerance = qMax<qint32>(
        2,
        static_cast<qint32>(qCeil(6.0 / qMax(0.1, scaleFactor))));

    ExportBoundaryHandle bestHandle = ExportBoundaryHandle::None;
    qint32 bestDistance = handleTolerance + 1;
    const auto considerHandle = [&](ExportBoundaryHandle handle, qint32 distance) {
        if (distance > handleTolerance) {
            return;
        }
        if (bestHandle == ExportBoundaryHandle::None || distance < bestDistance) {
            bestHandle = handle;
            bestDistance = distance;
        }
    };

    if (sourceY >= top - handleTolerance && sourceY <= bottom + handleTolerance) {
        considerHandle(ExportBoundaryHandle::Left, qAbs(sourceX - left));
        considerHandle(ExportBoundaryHandle::Right, qAbs(sourceX - right));
    }
    if (sourceX >= left - handleTolerance && sourceX <= right + handleTolerance) {
        considerHandle(ExportBoundaryHandle::Top, qAbs(sourceY - top));
    }

    return bestHandle;
}

void MainWindow::updateExportBoundaryHoverCursor(const QPoint &viewerPoint)
{
    if (!ui || !ui->imageViewerLabel) {
        return;
    }
    ExportBoundaryHandle hoverHandle = ExportBoundaryHandle::None;
    if (exportBoundaryDragHandle != ExportBoundaryHandle::None) {
        hoverHandle = exportBoundaryDragHandle;
    } else {
        hoverHandle = exportBoundaryHandleAtViewerPoint(viewerPoint);
        if (hoverHandle == ExportBoundaryHandle::None
            && exportBoundarySelectedHandle != ExportBoundaryHandle::None
            && isExportBoundaryDragAvailable()) {
            hoverHandle = exportBoundarySelectedHandle;
        }
    }

    Qt::CursorShape cursorShape = Qt::ArrowCursor;
    if (hoverHandle == ExportBoundaryHandle::Top) {
        cursorShape = Qt::SizeVerCursor;
    } else if (hoverHandle == ExportBoundaryHandle::Left
               || hoverHandle == ExportBoundaryHandle::Right) {
        cursorShape = Qt::SizeHorCursor;
    }

    if (ui->imageViewerLabel->cursor().shape() != cursorShape) {
        ui->imageViewerLabel->setCursor(cursorShape);
    }
    if (ui->scrollArea && ui->scrollArea->viewport()
        && ui->scrollArea->viewport()->cursor().shape() != cursorShape) {
        ui->scrollArea->viewport()->setCursor(cursorShape);
    }
}

void MainWindow::applyExportBoundaryDragAtViewerPoint(const QPoint &viewerPoint)
{
    if (exportBoundaryDragHandle == ExportBoundaryHandle::None || !isExportBoundaryDragAvailable()) {
        return;
    }

    qint32 sourceX = 0;
    qint32 sourceY = 0;
    if (!mapViewerToSourceCoordinates(viewerPoint, sourceX, sourceY)) {
        return;
    }

    TbcMetaData::VideoParameters videoParameters = tbcSource.getVideoParameters();
    bool changed = false;

    switch (exportBoundaryDragHandle) {
    case ExportBoundaryHandle::Top: {
        const qint32 minimumLine = minActiveFrameLineForSystem(videoParameters.system);
        const qint32 maximumLine = qMax(minimumLine, videoParameters.lastActiveFrameLine);
        const qint32 targetFirstActiveFrameLine = qBound(minimumLine, sourceY, maximumLine);
        if (videoParameters.firstActiveFrameLine != targetFirstActiveFrameLine) {
            videoParameters.firstActiveFrameLine = targetFirstActiveFrameLine;
            changed = true;
        }
        break;
    }
    case ExportBoundaryHandle::Left: {
        const qint32 minimumStart = qBound<qint32>(
            0,
            videoParameters.colourBurstEnd,
            qMax<qint32>(0, videoParameters.fieldWidth - 1));
        const qint32 maximumStart = qMax(minimumStart, videoParameters.activeVideoEnd - 1);
        const qint32 targetActiveVideoStart = qBound(minimumStart, sourceX, maximumStart);
        if (videoParameters.activeVideoStart != targetActiveVideoStart) {
            videoParameters.activeVideoStart = targetActiveVideoStart;
            changed = true;
        }
        break;
    }
    case ExportBoundaryHandle::Right: {
        const qint32 minimumEnd = videoParameters.activeVideoStart + 1;
        const qint32 maximumEnd = qMax(minimumEnd, videoParameters.fieldWidth);
        const qint32 targetActiveVideoEnd = qBound(minimumEnd, sourceX + 1, maximumEnd);
        if (videoParameters.activeVideoEnd != targetActiveVideoEnd) {
            videoParameters.activeVideoEnd = targetActiveVideoEnd;
            changed = true;
        }
        break;
    }
    case ExportBoundaryHandle::None:
    default:
        break;
    }

    if (!changed) {
        return;
    }

    if (videoParametersDialog) {
        videoParametersDialog->setVideoParameters(videoParameters);
    }
    videoParametersChangedSignalHandler(videoParameters);
}

void MainWindow::applyExportBoundaryWheelStep(qint32 step)
{
    if (step == 0 || exportBoundarySelectedHandle == ExportBoundaryHandle::None) {
        return;
    }
    if (!isExportBoundaryDragAvailable()) {
        exportBoundarySelectedHandle = ExportBoundaryHandle::None;
        return;
    }

    TbcMetaData::VideoParameters videoParameters = tbcSource.getVideoParameters();
    bool changed = false;
    switch (exportBoundarySelectedHandle) {
    case ExportBoundaryHandle::Top: {
        const qint32 minimumLine = minActiveFrameLineForSystem(videoParameters.system);
        const qint32 maximumLine = qMax(minimumLine, videoParameters.lastActiveFrameLine);
        const qint32 targetFirstActiveFrameLine = qBound(minimumLine,
                                                         videoParameters.firstActiveFrameLine + step,
                                                         maximumLine);
        if (videoParameters.firstActiveFrameLine != targetFirstActiveFrameLine) {
            videoParameters.firstActiveFrameLine = targetFirstActiveFrameLine;
            changed = true;
        }
        break;
    }
    case ExportBoundaryHandle::Left: {
        const qint32 minimumStart = qBound<qint32>(
            0,
            videoParameters.colourBurstEnd,
            qMax<qint32>(0, videoParameters.fieldWidth - 1));
        const qint32 maximumStart = qMax(minimumStart, videoParameters.activeVideoEnd - 1);
        const qint32 targetActiveVideoStart = qBound(minimumStart,
                                                     videoParameters.activeVideoStart + step,
                                                     maximumStart);
        if (videoParameters.activeVideoStart != targetActiveVideoStart) {
            videoParameters.activeVideoStart = targetActiveVideoStart;
            changed = true;
        }
        break;
    }
    case ExportBoundaryHandle::Right: {
        const qint32 minimumEnd = videoParameters.activeVideoStart + 1;
        const qint32 maximumEnd = qMax(minimumEnd, videoParameters.fieldWidth);
        const qint32 targetActiveVideoEnd = qBound(minimumEnd,
                                                   videoParameters.activeVideoEnd + step,
                                                   maximumEnd);
        if (videoParameters.activeVideoEnd != targetActiveVideoEnd) {
            videoParameters.activeVideoEnd = targetActiveVideoEnd;
            changed = true;
        }
        break;
    }
    case ExportBoundaryHandle::None:
    default:
        break;
    }

    if (!changed) {
        return;
    }
    if (videoParametersDialog) {
        videoParametersDialog->setVideoParameters(videoParameters);
    }
    videoParametersChangedSignalHandler(videoParameters);
}
// Method to hide the current image
void MainWindow::hideImage()
{
    ui->imageViewerLabel->clear();
    vectorscopeSelectionDragging = false;
    exportBoundaryDragHandle = ExportBoundaryHandle::None;
    exportBoundarySelectedHandle = ExportBoundaryHandle::None;
    updateExportBoundaryHoverCursor(QPoint(-1, -1));
    asyncFrameImage = QImage();
    asyncFrameRenderQueued = false;
    cursorReadoutImage = QImage();
    clearCursorReadout();
}

// Misc private methods -----------------------------------------------------------------------------------------------

// Load a TBC file based on the passed file name
void MainWindow::loadTbcFile(QString inputFileName, bool forceMetadataOnly, bool preserveStatusDuringReload)
{
    setPlaybackRunning(false);
    if (asyncFrameRenderInProgress) {
        cancelInFlightAsyncFrameRender();
    }
    asyncFrameRenderQueued = false;
    asyncFrameImage = QImage();
    if (preserveStatusDuringReload) {
        setGuiEnabled(false);
        sourceVideoStatus.setText(tr("Reloading metadata and analysis..."));
        fieldNumberStatus.setText(tr(" Frames: .../... Fields: .../..."));
        vbiStatus.hide();
        timeCodeStatus.hide();
    } else {
        // Update the GUI
        updateGuiUnloaded();
    }

    // Close current source video (if loaded)
    if (tbcSource.getIsSourceLoaded()) {
        tbcSource.unloadSource();
    }

    cleanupTempMetadataFile();
    metadataJsonLoaded = false;
    metadataJsonFilename.clear();
    metadataTempSqliteFilename.clear();

    QString resolvedInput = inputFileName;
    QFileInfo inputInfo(resolvedInput);
    QString suffix = inputInfo.suffix().toLower();
    bool isJson = (suffix == "json");
    bool isSqlite = (suffix == "db");
    const bool isMetadataInput = isJson || isSqlite;
    bool isMetadataOnly = forceMetadataOnly;

    if (forceMetadataOnly && !isJson && !isSqlite) {
        QString dbCandidate = resolvedInput;
        if (!dbCandidate.endsWith(".db", Qt::CaseInsensitive)) {
            dbCandidate += ".db";
        }
        QString jsonCandidate = resolvedInput;
        if (!jsonCandidate.endsWith(".json", Qt::CaseInsensitive)) {
            jsonCandidate += ".json";
        }

        QString metadataCandidate;
        if (QFileInfo::exists(jsonCandidate)) {
            metadataCandidate = jsonCandidate;
        } else if (QFileInfo::exists(dbCandidate)) {
            metadataCandidate = dbCandidate;
        }

        if (metadataCandidate.isEmpty()) {
            QMessageBox messageBox;
            messageBox.warning(this, tr("Error"),
                               tr("Metadata-only mode requires a .db or .json file. '%1' and '%2' were not found.")
                               .arg(dbCandidate, jsonCandidate));
            return;
        }

        resolvedInput = metadataCandidate;
        suffix = QFileInfo(resolvedInput).suffix().toLower();
        isJson = (suffix == "json");
        isSqlite = (suffix == "db");
        isMetadataOnly = true;
    }

    if (!forceMetadataOnly && (isJson || isSqlite)) {
        const QString resolvedSourceFilename = resolveSourceFilenameForMetadata(resolvedInput);
        if (!resolvedSourceFilename.isEmpty()) {
            if (isJson) {
                metadataJsonLoaded = true;
                metadataJsonFilename = resolvedInput;
            }

            // Keep reload behaviour aligned with the file the user selected.
            lastFilename = resolvedInput;
            tbcSource.loadSource(resolvedSourceFilename, resolvedInput);
            return;
        }

        isMetadataOnly = true;
    } else if (isMetadataInput) {
        isMetadataOnly = true;
    }

    if (isMetadataOnly) {
        QString metadataDisplayName = resolvedInput;
        if (isJson) {
            metadataJsonLoaded = true;
            metadataJsonFilename = resolvedInput;
        }

        lastFilename = metadataDisplayName;
        tbcSource.loadMetadata(resolvedInput, metadataDisplayName);
        return;
    }

    lastFilename = inputFileName;
    tbcSource.loadSource(inputFileName);

    // Note: loading continues in the background...
}

void MainWindow::cleanupTempMetadataFile()
{
    if (metadataTempSqliteFilename.isEmpty()) {
        return;
    }

    QFile::remove(metadataTempSqliteFilename);
    metadataTempSqliteFilename.clear();
}

// Method to update the line oscilloscope based on the frame number and scan line
void MainWindow::updateOscilloscopeDialogue()
{
    if (asyncFrameRenderInProgress && shouldRenderFrameAsync()) {
        return;
    }
    // Update the oscilloscope dialogue
    oscilloscopeDialog->showTraceImage(tbcSource.getScanLineData(lastScopeLine),
                                       lastScopeDot, lastScopeLine - 1,
                                       tbcSource.getFrameWidth(), tbcSource.getFrameHeight(), tbcSource.getSourceMode() == TbcSource::SourceMode::BOTH_SOURCES);
}

void MainWindow::updateRgbScopeDialogue(bool force)
{
    if (!rgbScopeDialog || !tbcSource.getIsSourceLoaded() || tbcSource.getIsMetadataOnly()) {
        return;
    }
    if (asyncFrameRenderInProgress && shouldRenderFrameAsync()) {
        return;
    }

    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    const qint64 minRefreshIntervalMs = playbackRunning ? 180 : 80;
    if (!force && rgbScopeLastRefreshMs > 0) {
        const qint64 elapsedMs = nowMs - rgbScopeLastRefreshMs;
        if (elapsedMs < minRefreshIntervalMs) {
            if (rgbScopeRefreshTimer && !rgbScopeRefreshPending) {
                rgbScopeRefreshPending = true;
                rgbScopeRefreshTimer->start(static_cast<int>(minRefreshIntervalMs - elapsedMs));
            }
            return;
        }
    }

    rgbScopeDialog->showScopeImage(tbcSource.getRgbScopeImage(rgbScopeDialog->scopeRenderTargetSize()));
    rgbScopeLastRefreshMs = nowMs;
    if (rgbScopeRefreshTimer && rgbScopeRefreshPending) {
        rgbScopeRefreshTimer->stop();
        rgbScopeRefreshPending = false;
    }
}

void MainWindow::updateYuvRangeScopeDialogue(bool force)
{
    if (!yuvRangeDialog || !tbcSource.getIsSourceLoaded() || tbcSource.getIsMetadataOnly()) {
        return;
    }
    if (asyncFrameRenderInProgress && shouldRenderFrameAsync()) {
        return;
    }

    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    const qint64 minRefreshIntervalMs = playbackRunning ? 180 : 80;
    if (!force && yuvRangeScopeLastRefreshMs > 0) {
        const qint64 elapsedMs = nowMs - yuvRangeScopeLastRefreshMs;
        if (elapsedMs < minRefreshIntervalMs) {
            if (yuvRangeScopeRefreshTimer && !yuvRangeScopeRefreshPending) {
                yuvRangeScopeRefreshPending = true;
                yuvRangeScopeRefreshTimer->start(static_cast<int>(minRefreshIntervalMs - elapsedMs));
            }
            return;
        }
    }

    yuvRangeDialog->showScopeImage(tbcSource.getYuvRangeScopeImage(yuvRangeDialog->scopeRenderTargetSize(), yuvRangeSettings));
    yuvRangeScopeLastRefreshMs = nowMs;
    if (yuvRangeScopeRefreshTimer && yuvRangeScopeRefreshPending) {
        yuvRangeScopeRefreshTimer->stop();
        yuvRangeScopeRefreshPending = false;
    }
}

// Method to update the vectorscope
void MainWindow::updateVectorscopeDialogue()
{
    if (asyncFrameRenderInProgress && shouldRenderFrameAsync()) {
        return;
    }
    // Update the vectorscope dialogue
    vectorscopeDialog->showTraceImage(tbcSource.getComponentFrame(), tbcSource.getVideoParameters(),
                                      tbcSource.getViewMode(), currentFieldNumber % 2);
}

// Method to update the waveform monitor — feeds the per-field 16-bit TBC
// samples through the dialog, which converts them to the 10-bit CVBS domain.
void MainWindow::updateWaveformMonitorDialogue()
{
    if (asyncFrameRenderInProgress && shouldRenderFrameAsync()) {
        return;
    }
    if (!tbcSource.getIsSourceLoaded() || tbcSource.getIsMetadataOnly()) {
        return;
    }
    const TbcSource::FieldTimingData timingData = tbcSource.getFieldTimingData();
    if (!timingData.valid) {
        return;
    }
    // Interleave the two fields into a single flat composite/luma/chroma
    // buffer per channel (field 1 then field 2), matching the waveform
    // monitor's expected line-contiguous layout.
    auto interleave = [](const std::vector<uint16_t>& first,
                         const std::vector<uint16_t>& second) -> std::vector<uint16_t> {
        std::vector<uint16_t> out;
        out.reserve(first.size() + second.size());
        out.insert(out.end(), first.begin(), first.end());
        out.insert(out.end(), second.begin(), second.end());
        return out;
    };

    const std::vector<uint16_t> composite =
        interleave(timingData.firstFieldComposite, timingData.secondFieldComposite);
    const std::vector<uint16_t> luma =
        interleave(timingData.firstFieldLuma, timingData.secondFieldLuma);
    const std::vector<uint16_t> chroma =
        interleave(timingData.firstFieldChroma, timingData.secondFieldChroma);

    waveformMonitorDialog->setData(composite, luma, chroma,
                                   timingData.firstFieldHeight,
                                   timingData.secondFieldHeight,
                                   tbcSource.getVideoParameters());
}

// Method to update the field timing scope
void MainWindow::updateFieldTimingDialogue()
{
    if (asyncFrameRenderInProgress && shouldRenderFrameAsync()) {
        return;
    }
    const TbcSource::FieldTimingData timingData = tbcSource.getFieldTimingData();
    if (!timingData.valid) {
        return;
    }

    const std::optional<qint32> secondFieldNumber = timingData.hasSecondField
        ? std::optional<qint32>(timingData.secondFieldNumber)
        : std::nullopt;
    const std::optional<TbcMetaData::VideoParameters> videoParameters = tbcSource.getVideoParameters();

    fieldTimingDialog->setFieldData(
        tbcSource.getCurrentSourceFilename(),
        timingData.firstFieldNumber,
        timingData.firstFieldComposite,
        secondFieldNumber,
        timingData.secondFieldComposite,
        timingData.firstFieldLuma,
        timingData.firstFieldChroma,
        timingData.secondFieldLuma,
        timingData.secondFieldChroma,
        videoParameters,
        timingData.firstFieldHeight,
        timingData.secondFieldHeight);
}

// Method to set the view (field/frame) values
double MainWindow::timecodeFrameRate() const
{
    if (!tbcSource.getIsSourceLoaded()) {
        return 30000.0 / 1001.0;
    }
    return frameRateForSystem(tbcSource.getSystem());
}

int MainWindow::timecodeFrameBaseRate() const
{
    if (!tbcSource.getIsSourceLoaded()) {
        return 30;
    }
    return nominalFrameRateForSystem(tbcSource.getSystem());
}

QString MainWindow::frameToTimecode(qint32 frameNumber) const
{
    const double fps = qMax(0.001, timecodeFrameRate());
    const int frameBase = qMax(1, timecodeFrameBaseRate());
    const qint64 frameIndex = qMax<qint64>(0, static_cast<qint64>(frameNumber) - 1);
    const double totalSecondsExact = static_cast<double>(frameIndex) / fps;
    const qint64 totalSeconds = static_cast<qint64>(qFloor(totalSecondsExact));
    const double fractionalSeconds = totalSecondsExact - static_cast<double>(totalSeconds);
    const int framePart = qBound(0, static_cast<int>(qFloor((fractionalSeconds * frameBase) + 1e-9)), frameBase - 1);
    const qint64 hours = totalSeconds / 3600;
    const qint64 minutes = (totalSeconds % 3600) / 60;
    const qint64 seconds = totalSeconds % 60;
    return QStringLiteral("%1:%2:%3:%4")
        .arg(hours, 2, 10, QChar('0'))
        .arg(minutes, 2, 10, QChar('0'))
        .arg(seconds, 2, 10, QChar('0'))
        .arg(framePart, 2, 10, QChar('0'));
}

QString MainWindow::framesToDurationTimecode(qint32 frameCount) const
{
    const double fps = qMax(0.001, timecodeFrameRate());
    const int frameBase = qMax(1, timecodeFrameBaseRate());
    const qint64 safeFrameCount = qMax<qint64>(0, frameCount);
    const double totalSecondsExact = static_cast<double>(safeFrameCount) / fps;
    const qint64 totalSeconds = static_cast<qint64>(qFloor(totalSecondsExact));
    const double fractionalSeconds = totalSecondsExact - static_cast<double>(totalSeconds);
    const int framePart = qBound(0, static_cast<int>(qFloor((fractionalSeconds * frameBase) + 1e-9)), frameBase - 1);
    const qint64 hours = totalSeconds / 3600;
    const qint64 minutes = (totalSeconds % 3600) / 60;
    const qint64 seconds = totalSeconds % 60;
    return QStringLiteral("%1:%2:%3:%4")
        .arg(hours, 2, 10, QChar('0'))
        .arg(minutes, 2, 10, QChar('0'))
        .arg(seconds, 2, 10, QChar('0'))
        .arg(framePart, 2, 10, QChar('0'));
}

qint32 MainWindow::timecodeToFrame(const QString &timecodeText, bool *ok) const
{
    if (ok) {
        *ok = false;
    }

    const QString trimmed = timecodeText.trimmed();
    if (trimmed.isEmpty()) {
        return 1;
    }

    bool numericOk = false;
    const qint32 numericFrame = trimmed.toInt(&numericOk);
    if (numericOk) {
        if (ok) {
            *ok = true;
        }
        return qMax(1, numericFrame);
    }

    static const QRegularExpression pattern(
        QStringLiteral("^\\s*(\\d+):(\\d{1,2}):(\\d{1,2}):(\\d{1,2})\\s*$"));
    const QRegularExpressionMatch match = pattern.match(trimmed);
    if (!match.hasMatch()) {
        return 1;
    }

    bool hoursOk = false;
    bool minutesOk = false;
    bool secondsOk = false;
    bool framesOk = false;
    const qint64 hours = match.captured(1).toLongLong(&hoursOk);
    const int minutes = match.captured(2).toInt(&minutesOk);
    const int seconds = match.captured(3).toInt(&secondsOk);
    const int frames = match.captured(4).toInt(&framesOk);
    const double fps = qMax(0.001, timecodeFrameRate());
    const int frameBase = qMax(1, timecodeFrameBaseRate());
    if (!hoursOk || !minutesOk || !secondsOk || !framesOk
        || minutes < 0 || minutes >= 60
        || seconds < 0 || seconds >= 60
        || frames < 0 || frames >= frameBase) {
        return 1;
    }
    const double totalSecondsExact = static_cast<double>((hours * 3600) + (minutes * 60) + seconds)
                                     + (static_cast<double>(frames) / static_cast<double>(frameBase));
    const qint64 totalFrames = qRound64(totalSecondsExact * fps) + 1;
    if (ok) {
        *ok = true;
    }
    return static_cast<qint32>(qMax<qint64>(1, totalFrames));
}

void MainWindow::updatePositionEditorValue(qint32 currentNumber)
{
    if (ui->posNumberSpinBox) {
        const QSignalBlocker blocker(ui->posNumberSpinBox);
        ui->posNumberSpinBox->setValue(currentNumber);
    }
    if (ui->posTimecodeLineEdit && !tbcSource.getFieldViewEnabled()) {
        const QSignalBlocker blocker(ui->posTimecodeLineEdit);
        ui->posTimecodeLineEdit->setText(frameToTimecode(currentNumber));
    }
}

bool MainWindow::playbackUseFastMode() const
{
    if (!tbcSource.getIsSourceLoaded()) {
        return false;
    }

    if (!tbcSource.getChromaDecoder()) {
        return true;
    }

    return tbcSource.getSourceMode() == TbcSource::LUMA_SOURCE;
}

QString MainWindow::playbackStartToolTip() const
{
    const bool isPalSystem = (tbcSource.getIsSourceLoaded() && tbcSource.getSystem() == PAL);
    if (playbackUseFastMode()) {
        const QString fastFpsText = isPalSystem ? QStringLiteral("50.00") : QStringLiteral("59.94");
        return tr("Play at %1 fps (luma/source mode)").arg(fastFpsText);
    }

    const QString fpsText = isPalSystem ? QStringLiteral("25.00") : QStringLiteral("29.97");
    return tr("Play at %1 fps").arg(fpsText);
}

double MainWindow::playbackFrameIntervalMs() const
{
    if (tbcSource.getIsSourceLoaded() && tbcSource.getSystem() == PAL) {
        return 1000.0 / 25.0;
    }
    return 1000.0 * 1001.0 / 30000.0;
}

void MainWindow::scheduleNextPlaybackTick()
{
    if (!playbackRunning || !playbackTimer) {
        return;
    }
    double intervalMs = playbackFrameIntervalMs();
    if (playbackUseFastMode()) {
        intervalMs *= 0.5;
    }
    playbackTickCarryMs += intervalMs;
    const int timerIntervalMs = qMax(1, static_cast<int>(qFloor(playbackTickCarryMs)));
    playbackTickCarryMs -= static_cast<double>(timerIntervalMs);
    playbackTimer->start(timerIntervalMs);
}

void MainWindow::setPlaybackRunning(bool running)
{
    playbackRunning = running && tbcSource.getIsSourceLoaded();
    if (playbackTimer && !playbackRunning) {
        playbackTimer->stop();
    }

    if (!ui || !ui->playPushButton) {
        return;
    }

    {
        const QSignalBlocker blocker(ui->playPushButton);
        ui->playPushButton->setChecked(playbackRunning);
    }

    if (playbackRunning) {
        playbackTickCarryMs = 0.0;
        ensureSvgButtonIcon(ui->playPushButton, QStringLiteral(":/icons/Graphics/stop-playback.svg"));
        ui->playPushButton->setToolTip(tr("Stop playback"));
        scheduleNextPlaybackTick();
        return;
    }

    ensureSvgButtonIcon(ui->playPushButton, QStringLiteral(":/icons/Graphics/start-playback.svg"));
    ui->playPushButton->setToolTip(playbackStartToolTip());
}

void MainWindow::stepPlayback()
{
    if (!playbackRunning || !tbcSource.getIsSourceLoaded()) {
        setPlaybackRunning(false);
        return;
    }

    qint32 currentNumber = currentFrameNumber;
    if (tbcSource.getFieldViewEnabled()) {
        const qint32 nextField = currentFieldNumber + 2;
        if (nextField > tbcSource.getNumberOfFields()) {
            setPlaybackRunning(false);
            return;
        }
        setCurrentField(nextField);
        currentNumber = currentFieldNumber;
    } else {
        const qint32 nextFrame = currentFrameNumber + 1;
        if (nextFrame > tbcSource.getNumberOfFrames()) {
            setPlaybackRunning(false);
            return;
        }
        setCurrentFrame(nextFrame);
        currentNumber = currentFrameNumber;
    }

    updatePositionEditorValue(currentNumber);
    if (ui->posHorizontalSlider) {
        const QSignalBlocker blocker(ui->posHorizontalSlider);
        ui->posHorizontalSlider->setValue(currentNumber);
    }

    scheduleNextPlaybackTick();
}

void MainWindow::updateBottomStatusReadout()
{
    if (!tbcSource.getIsSourceLoaded()) {
        sourceVideoStatus.setText(tr("No source video file loaded"));
        fieldNumberStatus.setText(tr(" Frames: ./. Fields: ./."));
        return;
    }

    QString sourcePrefix;
    if (!tbcSource.getVideoParameters().tapeFormat.isEmpty()) {
        sourcePrefix += tbcSource.getVideoParameters().tapeFormat + QLatin1Char(' ');
    }
    sourcePrefix += tbcSource.getSystemDescription();
    sourcePrefix += tbcSource.getIsMetadataOnly()
                        ? tr(" Metadata Duration: ")
                        : tr(" Source Duration: ");
    sourcePrefix += framesToDurationTimecode(tbcSource.getNumberOfFrames());
    sourceVideoStatus.setText(sourcePrefix);

    const qint32 totalFrames = qMax(1, tbcSource.getNumberOfFrames());
    const qint32 totalFields = qMax(1, tbcSource.getNumberOfFields());
    const qint32 frameCurrent = qBound(1, currentFrameNumber, totalFrames);
    const qint32 firstField = qBound(1, tbcSource.getFirstFieldNumber(), totalFields);
    const qint32 secondField = qBound(1, tbcSource.getSecondFieldNumber(), totalFields);
    fieldNumberStatus.setText(QStringLiteral(" Frames: %1/%2 Fields: %3/%4")
                                  .arg(frameCurrent)
                                  .arg(totalFrames)
                                  .arg(firstField)
                                  .arg(secondField));
}
void MainWindow::setViewValues()
{
    qint32 currentNumber, maximum;
    QString buttonLabel, spinLabel;

	if (this->width() >= 930)
	{
		if (tbcSource.getFieldViewEnabled()) {
			currentNumber = currentFieldNumber;
			maximum = tbcSource.getNumberOfFields();
			spinLabel = QString("Field #:");
			if (tbcSource.getStretchField()) {
				buttonLabel = QString("Field 2:1");
			} else {
				buttonLabel = QString("Field 1:1");
			}
		} else {
			currentNumber = currentFrameNumber;
			maximum = tbcSource.getNumberOfFrames();
			spinLabel = QString("Frame #:");

			if (tbcSource.getSplitViewEnabled()) {
				buttonLabel = QString("Split View");
			} else {
				buttonLabel = QString("Frame View");
			}
		}
	}
	else
	{
		if (tbcSource.getFieldViewEnabled()) {
			currentNumber = currentFieldNumber;
			maximum = tbcSource.getNumberOfFields();
			spinLabel = QString("Field #:");
			if (tbcSource.getStretchField()) {
				buttonLabel = QString("Field 2:1");
			} else {
				buttonLabel = QString("Field 1:1");
			}
		} else {
			currentNumber = currentFrameNumber;
			maximum = tbcSource.getNumberOfFrames();
			spinLabel = QString("Frame #:");

			if (tbcSource.getSplitViewEnabled()) {
				buttonLabel = QString("Split");
			} else {
				buttonLabel = QString("Frame");
			}
		}
	}

    ui->posNumberSpinBox->setMaximum(maximum);
    updatePositionEditorValue(currentNumber);
    ui->posHorizontalSlider->setMaximum(maximum);
    ui->posHorizontalSlider->setPageStep(maximum / 100);
    ui->posHorizontalSlider->setValue(currentNumber);

    if (ui->posTimecodeLineEdit) {
        const bool fieldView = tbcSource.getFieldViewEnabled();
        ui->posNumberSpinBox->setVisible(fieldView);
        ui->posTimecodeLineEdit->setVisible(!fieldView);
        if (!fieldView) {
            ui->posTimecodeLineEdit->setText(frameToTimecode(currentNumber));
        }
    }

    ui->viewPushButton->setText(buttonLabel);
    if (ui->posNumberSpinBoxLabel) {
        const bool fieldView = tbcSource.getFieldViewEnabled();
        ui->posNumberSpinBoxLabel->setVisible(fieldView);
        if (fieldView) {
            ui->posNumberSpinBoxLabel->setText(spinLabel);
        }
    }

    updateTimelineMarkers();
}
qint32 MainWindow::sliderPositionForFrame(qint32 frameNumber) const
{
    if (frameNumber <= 0 || !tbcSource.getIsSourceLoaded()) {
        return -1;
    }

    if (tbcSource.getFieldViewEnabled()) {
        const qint32 totalFields = qMax<qint32>(1, tbcSource.getNumberOfFields());
        const qint32 fieldPosition = (frameNumber * 2) - 1;
        return qBound<qint32>(1, fieldPosition, totalFields);
    }

    const qint32 totalFrames = qMax<qint32>(1, tbcSource.getNumberOfFrames());
    return qBound<qint32>(1, frameNumber, totalFrames);
}

qint32 MainWindow::frameForSliderPosition(qint32 sliderPosition) const
{
    if (!tbcSource.getIsSourceLoaded()) {
        return 1;
    }
    if (tbcSource.getFieldViewEnabled()) {
        const qint32 totalFields = qMax<qint32>(1, tbcSource.getNumberOfFields());
        const qint32 clamped = qBound<qint32>(1, sliderPosition, totalFields);
        return qBound<qint32>(1, (clamped + 1) / 2, qMax<qint32>(1, tbcSource.getNumberOfFrames()));
    }
    return qBound<qint32>(1, sliderPosition, qMax<qint32>(1, tbcSource.getNumberOfFrames()));
}

void MainWindow::updateTimelineMarkers()
{
    if (!timelineMarkerSlider) {
        return;
    }

    if (!tbcSource.getIsSourceLoaded()) {
        timelineMarkerSlider->setMarkerFrames(-1, -1, {});
        return;
    }
    const qint32 totalFrames = qMax<qint32>(1, tbcSource.getNumberOfFrames());

    const TbcMetaData::VideoParameters videoParameters = tbcSource.getVideoParameters();
    const qint32 inPosition = sliderPositionForFrame(videoParameters.userEditInSelection);
    const qint32 outPosition = sliderPositionForFrame(videoParameters.userEditOutSelection);
    const QVector<UserNoteMarker> noteMarkers = userNoteMarkersFromVideoParameters(videoParameters, totalFrames);
    QVector<qint32> notePositions;
    notePositions.reserve(noteMarkers.size());
    for (const UserNoteMarker &noteMarker : noteMarkers) {
        const qint32 markerPosition = sliderPositionForFrame(noteMarker.frame);
        if (markerPosition > 0) {
            notePositions.append(markerPosition);
        }
    }
    timelineMarkerSlider->setMarkerFrames(inPosition, outPosition, notePositions);
}

void MainWindow::updateNotesViewerState()
{
    if (!notesViewerDialog) {
        return;
    }

    NotesViewerState state;
    state.frameRate = timecodeFrameRate();
    state.frameBaseRate = timecodeFrameBaseRate();

    if (tbcSource.getIsSourceLoaded()) {
        const qint32 totalFrames = qMax<qint32>(1, tbcSource.getNumberOfFrames());
        const TbcMetaData::VideoParameters videoParameters = tbcSource.getVideoParameters();
        state.totalFrames = totalFrames;
        state.currentFrame = qBound<qint32>(1, currentFrameNumber, totalFrames);
        state.inFrame = videoParameters.userEditInSelection;
        state.outFrame = videoParameters.userEditOutSelection;
        const QVector<UserNoteMarker> noteMarkers = userNoteMarkersFromVideoParameters(videoParameters, totalFrames);
        noteMarkerListsFromMarkers(noteMarkers, state.noteFrames, state.noteComments);
    }

    notesViewerDialog->setState(state);
}

void MainWindow::setInPointAtCurrentFrame()
{
    if (!exportDialog || !tbcSource.getIsSourceLoaded() || tbcSource.getIsMetadataOnly()) {
        return;
    }

    const qint32 playbackPositionValue = tbcSource.getFieldViewEnabled() ? currentFieldNumber : currentFrameNumber;
    const qint32 totalFrames = qMax<qint32>(1, tbcSource.getNumberOfFrames());
    const qint32 framePoint = qBound<qint32>(1, frameForSliderPosition(playbackPositionValue), totalFrames);
    const QString framePointTimecode = frameToTimecode(framePoint);

    exportDialog->setInPoint(framePoint);
    statusBar()->showMessage(tr("Export In point set to frame %1 (%2)").arg(framePoint).arg(framePointTimecode), 3000);
}

void MainWindow::setOutPointAtCurrentFrame()
{
    if (!exportDialog || !tbcSource.getIsSourceLoaded() || tbcSource.getIsMetadataOnly()) {
        return;
    }

    const qint32 playbackPositionValue = tbcSource.getFieldViewEnabled() ? currentFieldNumber : currentFrameNumber;
    const qint32 totalFrames = qMax<qint32>(1, tbcSource.getNumberOfFrames());
    const qint32 framePoint = qBound<qint32>(1, frameForSliderPosition(playbackPositionValue), totalFrames);
    const QString framePointTimecode = frameToTimecode(framePoint);

    exportDialog->setOutPoint(framePoint);
    statusBar()->showMessage(tr("Export Out point set to frame %1 (%2)").arg(framePoint).arg(framePointTimecode), 3000);
}

// Set the current frame, field is updated based on frame number
void MainWindow::setCurrentFrame(qint32 number)
{
    if (number == currentFrameNumber) return;

    currentFrameNumber = number;
    currentFieldNumber = (number * 2) - 1;

    sanitizeCurrentPosition();
    showImage();
}

// Set the current field, frame is updated based on field number
void MainWindow::setCurrentField(qint32 number)
{
    if (number == currentFieldNumber) return;

    currentFieldNumber = number;
    currentFrameNumber = std::ceil((double)number / 2);

    sanitizeCurrentPosition();
    showImage();
}

void MainWindow::skipFramesBy(qint32 frameDelta)
{
    if (!tbcSource.getIsSourceLoaded()) {
        return;
    }

    const qint32 totalFrames = qMax<qint32>(1, tbcSource.getNumberOfFrames());
    const qint32 targetFrame = qBound<qint32>(1, currentFrameNumber + frameDelta, totalFrames);
    if (targetFrame == currentFrameNumber) {
        return;
    }

    setCurrentFrame(targetFrame);
    const qint32 uiNumber = tbcSource.getFieldViewEnabled() ? currentFieldNumber : currentFrameNumber;
    updatePositionEditorValue(uiNumber);
    ui->posHorizontalSlider->setValue(uiNumber);
}

void MainWindow::sanitizeCurrentPosition()
{
    if (currentFrameNumber > tbcSource.getNumberOfFrames() || currentFieldNumber > tbcSource.getNumberOfFields()) {
        currentFrameNumber = tbcSource.getNumberOfFrames();
        currentFieldNumber = tbcSource.getNumberOfFields();
    }

    if (currentFrameNumber == 0)
    {
        currentFrameNumber = 1;
    }

    if (currentFieldNumber == 0) {
        currentFieldNumber = 1;
    }
}

bool MainWindow::runExternalToolWithProgress(const QString &program, const QStringList &arguments,
                                             const QString &toolDisplayName, QString *errorMessage)
{
    QProcess process;
    process.setProcessChannelMode(QProcess::MergedChannels);
    process.start(program, arguments);
    QDialog progressDialog(this);
    progressDialog.setWindowTitle(toolDisplayName);
    progressDialog.setWindowModality(Qt::ApplicationModal);
    progressDialog.setWindowFlags(Qt::Dialog | Qt::WindowTitleHint | Qt::CustomizeWindowHint);
    progressDialog.setMinimumWidth(540);

    auto *dialogLayout = new QVBoxLayout(&progressDialog);
    dialogLayout->setContentsMargins(14, 12, 14, 12);
    dialogLayout->setSpacing(8);

    auto *stageLabel = new QLabel(&progressDialog);
    stageLabel->setWordWrap(true);
    dialogLayout->addWidget(stageLabel);

    auto *countsLabel = new QLabel(&progressDialog);
    countsLabel->setWordWrap(true);
    QFont countsFont = countsLabel->font();
    countsFont.setStyleHint(QFont::TypeWriter);
    countsLabel->setFont(countsFont);
    dialogLayout->addWidget(countsLabel);

    auto *percentLabel = new QLabel(&progressDialog);
    percentLabel->setWordWrap(true);
    dialogLayout->addWidget(percentLabel);

    auto *progressBar = new QProgressBar(&progressDialog);
    progressBar->setRange(0, 0);
    progressBar->setTextVisible(true);
    progressBar->setFormat(tr("Working..."));
    dialogLayout->addWidget(progressBar);

    auto *buttonRowLayout = new QHBoxLayout();
    buttonRowLayout->addStretch(1);
    auto *cancelButton = new QPushButton(tr("Cancel"), &progressDialog);
    buttonRowLayout->addWidget(cancelButton);
    dialogLayout->addLayout(buttonRowLayout);

    ExternalToolStage stage = ExternalToolStage::Starting;
    qint32 totalFields = 0;
    qint32 processedFields = 0;
    qint32 teletextProgressPercent = -1;
    QString lastOutputLine;
    bool cancelRequested = false;
    bool terminateSent = false;
    QElapsedTimer cancelTimer;

    connect(cancelButton, &QPushButton::clicked, &progressDialog, [&]() {
        if (cancelRequested) {
            return;
        }
        cancelRequested = true;
        cancelButton->setEnabled(false);
        stageLabel->setText(tr("%1: Cancel requested...").arg(toolDisplayName));
        percentLabel->setText(tr("Cancelling..."));
        QCoreApplication::processEvents();
    });

    const QRegularExpression totalFieldsExpression(
        QStringLiteral("Using\\s+\\d+\\s+threads\\s+to\\s+process\\s+([0-9,]+)\\s+fields"),
        QRegularExpression::CaseInsensitiveOption);
    const auto parseCountText = [](QString text, bool *ok) -> qint32 {
        text.remove(QLatin1Char(','));
        return text.toInt(ok);
    };
    const QRegularExpression progressFieldsExpression(
        QStringLiteral("(?:Info:\\s*)?Processing\\s+fields\\s+([0-9,]+)\\s*/\\s*([0-9,]+)"),
        QRegularExpression::CaseInsensitiveOption);
    const QRegularExpression legacyFieldExpression(
        QStringLiteral("(?:Info:\\s*)?Processing\\s+field\\s+([0-9,]+)"),
        QRegularExpression::CaseInsensitiveOption);
    const QRegularExpression completeFieldsExpression(
        QStringLiteral("(?:Info:\\s*)?Processing\\s+complete\\s*-\\s*([0-9,]+)\\s*fields"),
        QRegularExpression::CaseInsensitiveOption);
    const QRegularExpression ansiEscapeExpression(QStringLiteral("\\x1B\\[[0-9;]*[A-Za-z]"));
    const QRegularExpression teletextTqdmPercentExpression(QStringLiteral("(\\d{1,3})%\\|"));

    auto updateProgressDialog = [&]() {
        const bool teletextStage = externalToolStageIsTeletext(stage);
        int percent = 0;
        bool hasDeterminateProgress = false;

        if (teletextStage && teletextProgressPercent >= 0) {
            percent = qBound<qint32>(0, teletextProgressPercent, 100);
            hasDeterminateProgress = true;
        } else if (!teletextStage && totalFields > 0) {
            percent = externalToolProgressPercent(processedFields, totalFields);
            hasDeterminateProgress = true;
        }

        if (hasDeterminateProgress) {
            progressBar->setRange(0, 100);
            progressBar->setValue(percent);
            progressBar->setFormat(QStringLiteral("%p%"));
        } else {
            progressBar->setRange(0, 0);
            progressBar->setFormat(tr("Working..."));
        }
        const QString progressLine = hasDeterminateProgress
            ? tr("Progress: %1%").arg(percent)
            : tr("Progress: --");
        stageLabel->setText(externalToolStageLabel(toolDisplayName, stage));
        if (teletextStage) {
            countsLabel->setText(tr("%1\n%2")
                                     .arg(externalToolProgressSummary(processedFields, totalFields),
                                          externalToolTeletextProgressSummary(teletextProgressPercent)));
        } else {
            countsLabel->setText(externalToolProgressSummary(processedFields, totalFields));
        }
        percentLabel->setText(progressLine);
    };

    auto processOutputLine = [&](const QString &line) {
        QString normalizedLine = line;
        normalizedLine.remove(ansiEscapeExpression);
        const QString trimmedLine = normalizedLine.trimmed();
        if (trimmedLine.isEmpty()) {
            return;
        }
        lastOutputLine = trimmedLine;

        if (trimmedLine.contains(QStringLiteral("Reading metadata from"), Qt::CaseInsensitive)) {
            stage = ExternalToolStage::LoadingMetadata;
        } else if (trimmedLine.contains(QStringLiteral("Beginning"), Qt::CaseInsensitive)
                   && trimmedLine.contains(QStringLiteral("processing"), Qt::CaseInsensitive)) {
            stage = ExternalToolStage::AnalysingMetadata;
        } else if (trimmedLine.contains(QStringLiteral("Writing metadata file"), Qt::CaseInsensitive)) {
            stage = ExternalToolStage::WritingMetadata;
            if (totalFields > 0) {
                processedFields = totalFields;
            }
        } else if (trimmedLine.contains(QStringLiteral("Teletext export output directory"), Qt::CaseInsensitive)
                   || trimmedLine.contains(QStringLiteral("Teletext export: using Python executable"),
                                           Qt::CaseInsensitive)
                   || trimmedLine.contains(QStringLiteral("Teletext Python dependency check"),
                                           Qt::CaseInsensitive)) {
            stage = ExternalToolStage::TeletextDependencyCheck;
            teletextProgressPercent = qMax<qint32>(teletextProgressPercent, 0);
        } else if (trimmedLine.contains(QStringLiteral("Teletext export backend probe:"),
                                        Qt::CaseInsensitive)
                   || trimmedLine.contains(QStringLiteral("optional Python modules missing"),
                                           Qt::CaseInsensitive)) {
            stage = ExternalToolStage::TeletextBackendProbe;
            teletextProgressPercent = qMax<qint32>(teletextProgressPercent, 5);
        } else if (trimmedLine.contains(QStringLiteral("Teletext export: starting deconvolution with"),
                                        Qt::CaseInsensitive)
                   || trimmedLine.contains(QStringLiteral("Teletext export: deconvolution attempt failed"),
                                           Qt::CaseInsensitive)
                   || trimmedLine.contains(QStringLiteral("Teletext export: deconvolution succeeded after fallback"),
                                           Qt::CaseInsensitive)) {
            stage = ExternalToolStage::TeletextDeconvolution;
            teletextProgressPercent = qMax<qint32>(teletextProgressPercent, 10);
        } else if (trimmedLine.contains(QStringLiteral("Teletext export: squashing duplicate packets"),
                                        Qt::CaseInsensitive)) {
            stage = ExternalToolStage::TeletextSquashing;
            teletextProgressPercent = qMax<qint32>(teletextProgressPercent, 70);
        } else if (trimmedLine.contains(QStringLiteral("Teletext export: generating HTML pages"),
                                        Qt::CaseInsensitive)) {
            stage = ExternalToolStage::TeletextHtmlGeneration;
            teletextProgressPercent = qMax<qint32>(teletextProgressPercent, 80);
        } else if (trimmedLine.contains(QStringLiteral("Teletext export complete"), Qt::CaseInsensitive)
                   || trimmedLine.contains(QStringLiteral("Teletext export finished but generated no HTML pages"),
                                           Qt::CaseInsensitive)) {
            stage = ExternalToolStage::Finishing;
            teletextProgressPercent = 100;
        }

        const QRegularExpressionMatch totalFieldsMatch = totalFieldsExpression.match(trimmedLine);
        if (totalFieldsMatch.hasMatch()) {
            bool ok = false;
            const qint32 parsedTotal = parseCountText(totalFieldsMatch.captured(1), &ok);
            if (ok && parsedTotal > 0) {
                totalFields = parsedTotal;
                processedFields = qBound<qint32>(0, processedFields, totalFields);
                stage = ExternalToolStage::AnalysingMetadata;
            }
        }

        const QRegularExpressionMatch progressFieldsMatch = progressFieldsExpression.match(trimmedLine);
        if (progressFieldsMatch.hasMatch()) {
            bool processedOk = false;
            bool totalOk = false;
            const qint32 parsedProcessed = parseCountText(progressFieldsMatch.captured(1), &processedOk);
            const qint32 parsedTotal = parseCountText(progressFieldsMatch.captured(2), &totalOk);
            if (processedOk && totalOk && parsedTotal > 0) {
                totalFields = parsedTotal;
                processedFields = qBound<qint32>(0, parsedProcessed, totalFields);
                stage = ExternalToolStage::AnalysingMetadata;
            }
        } else {
            const QRegularExpressionMatch legacyFieldMatch = legacyFieldExpression.match(trimmedLine);
            if (legacyFieldMatch.hasMatch()) {
                bool processedOk = false;
                const qint32 parsedProcessed = parseCountText(legacyFieldMatch.captured(1), &processedOk);
                if (processedOk) {
                    if (totalFields > 0) {
                        processedFields = qBound<qint32>(0, parsedProcessed, totalFields);
                    } else {
                        processedFields = qMax(processedFields, parsedProcessed);
                    }
                    stage = ExternalToolStage::AnalysingMetadata;
                }
            }
        }

        const QRegularExpressionMatch completeFieldsMatch = completeFieldsExpression.match(trimmedLine);
        if (completeFieldsMatch.hasMatch()) {
            bool completeOk = false;
            const qint32 parsedTotal = parseCountText(completeFieldsMatch.captured(1), &completeOk);
            if (completeOk && parsedTotal > 0) {
                totalFields = parsedTotal;
                processedFields = totalFields;
                stage = ExternalToolStage::Finishing;
            }
        }

        const QRegularExpressionMatch teletextPercentMatch = teletextTqdmPercentExpression.match(trimmedLine);
        if (teletextPercentMatch.hasMatch()) {
            bool percentOk = false;
            const qint32 parsedPercent = teletextPercentMatch.captured(1).toInt(&percentOk);
            if (percentOk) {
                teletextProgressPercent = qBound<qint32>(0, parsedPercent, 100);
                if (!externalToolStageIsTeletext(stage)) {
                    stage = ExternalToolStage::TeletextHtmlGeneration;
                }
            }
        }

        updateProgressDialog();
    };

    auto processOutputChunk = [&](const QByteArray &chunk, QByteArray &buffer) {
        QByteArray normalizedChunk = chunk;
        normalizedChunk.replace('\r', '\n');
        buffer.append(normalizedChunk);
        qsizetype newLineIndex = -1;
        while ((newLineIndex = buffer.indexOf('\n')) != -1) {
            QByteArray lineBytes = buffer.left(newLineIndex);
            buffer.remove(0, newLineIndex + 1);
            if (!lineBytes.isEmpty() && lineBytes.endsWith('\r')) {
                lineBytes.chop(1);
            }
            processOutputLine(QString::fromLocal8Bit(lineBytes));
        }
    };

    updateProgressDialog();
    progressDialog.show();
    progressDialog.raise();
    progressDialog.activateWindow();
    QElapsedTimer progressVisibleTimer;
    progressVisibleTimer.start();
    QCoreApplication::processEvents();

    if (!process.waitForStarted(5000)) {
        progressDialog.close();
        if (errorMessage) {
            *errorMessage = tr("Unable to start %1.").arg(toolDisplayName.toLower());
        }
        return false;
    }

    QByteArray outputBuffer;
    while (process.state() != QProcess::NotRunning) {
        if (cancelRequested) {
            if (!terminateSent) {
#if defined(Q_OS_UNIX)
                const qint64 processId = process.processId();
                if (processId > 0) {
                    ::kill(static_cast<pid_t>(processId), SIGINT);
                } else {
                    process.terminate();
                }
#else
                process.terminate();
#endif
                terminateSent = true;
                cancelTimer.start();
            } else if (cancelTimer.isValid() && cancelTimer.elapsed() > 2000) {
                process.kill();
            }
        }
        process.waitForReadyRead(100);
        const QByteArray outputChunk = process.readAllStandardOutput();
        if (!outputChunk.isEmpty()) {
            processOutputChunk(outputChunk, outputBuffer);
        }
        QCoreApplication::processEvents();
    }

    const QByteArray trailingChunk = process.readAllStandardOutput();
    if (!trailingChunk.isEmpty()) {
        processOutputChunk(trailingChunk, outputBuffer);
    }
    if (!outputBuffer.trimmed().isEmpty()) {
        processOutputLine(QString::fromLocal8Bit(outputBuffer));
    }

    stage = ExternalToolStage::Finishing;
    if (totalFields > 0) {
        processedFields = totalFields;
    }
    teletextProgressPercent = qMax<qint32>(teletextProgressPercent, 100);
    updateProgressDialog();
    QCoreApplication::processEvents();
    constexpr qint64 minimumVisibleMilliseconds = 700;
    const qint64 visibleMilliseconds = progressVisibleTimer.elapsed();
    if (visibleMilliseconds < minimumVisibleMilliseconds) {
        QEventLoop delayLoop;
        QTimer::singleShot(static_cast<int>(minimumVisibleMilliseconds - visibleMilliseconds),
                           &delayLoop, &QEventLoop::quit);
        delayLoop.exec();
    }
    progressDialog.close();

    if (cancelRequested) {
        if (errorMessage) {
            *errorMessage = tr("%1 cancelled by user.").arg(toolDisplayName);
        }
        return false;
    }

    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
        if (errorMessage) {
            *errorMessage = !lastOutputLine.isEmpty()
                ? lastOutputLine
                : tr("%1 failed with exit code %2.").arg(toolDisplayName).arg(process.exitCode());
        }
        return false;
    }

    return true;
}

MainWindow::UiStateSnapshot MainWindow::captureUiStateSnapshot() const
{
    UiStateSnapshot snapshot;
    if (!tbcSource.getIsSourceLoaded()) {
        return snapshot;
    }

    snapshot.valid = true;
    snapshot.frameNumber = currentFrameNumber;
    snapshot.fieldNumber = currentFieldNumber;
    snapshot.viewMode = tbcSource.getViewMode();
    snapshot.stretchField = tbcSource.getStretchField();
    snapshot.reverseFieldOrder = tbcSource.getFieldOrder();
    snapshot.highlightDropouts = tbcSource.getHighlightDropouts();
    snapshot.chromaEnabled = tbcSource.getChromaDecoder();
    snapshot.chromaDecodeMode = tbcSource.getChromaDecodeMode();
    snapshot.sourceMode = tbcSource.getSourceMode();
    snapshot.aspectRatioEnabled = displayAspectRatio;
    snapshot.imageScaleFactor = scaleFactor;
    snapshot.activeMainTabIndex = (ui && ui->mainTabWidget) ? ui->mainTabWidget->currentIndex() : 0;

    snapshot.showVbiDialog = vbiDialog && vbiDialog->isVisible();
    snapshot.showDropoutDialog = dropoutAnalysisDialog && dropoutAnalysisDialog->isVisible();
    snapshot.showVisibleDropoutDialog = visibleDropoutAnalysisDialog && visibleDropoutAnalysisDialog->isVisible();
    snapshot.showBlackSnrDialog = blackSnrAnalysisDialog && blackSnrAnalysisDialog->isVisible();
    snapshot.showWhiteSnrDialog = whiteSnrAnalysisDialog && whiteSnrAnalysisDialog->isVisible();
    snapshot.showVideoParametersDialog = videoParametersDialog && videoParametersDialog->isVisible();
    snapshot.showChromaConfigDialog = chromaDecoderConfigDialog && chromaDecoderConfigDialog->isVisible();
    return snapshot;
}

void MainWindow::applyUiStateSnapshot(const UiStateSnapshot &snapshot)
{
    if (!snapshot.valid || !tbcSource.getIsSourceLoaded()) {
        return;
    }

    displayAspectRatio = snapshot.aspectRatioEnabled;
    scaleFactor = snapshot.imageScaleFactor;
    tbcSource.setViewMode(snapshot.viewMode);
    tbcSource.setStretchField(snapshot.stretchField);
    tbcSource.setFieldOrder(snapshot.reverseFieldOrder);
    tbcSource.setHighlightDropouts(snapshot.highlightDropouts);
    tbcSource.setChromaDecodeMode(snapshot.chromaDecodeMode);
    tbcSource.setChromaDecoder(snapshot.chromaEnabled);
    if (tbcSource.getSourceMode() != TbcSource::ONE_SOURCE) {
        tbcSource.setSourceMode(snapshot.sourceMode);
    }

    const qint32 totalFrames = qMax<qint32>(1, tbcSource.getNumberOfFrames());
    const qint32 totalFields = qMax<qint32>(1, tbcSource.getNumberOfFields());
    if (snapshot.viewMode == TbcSource::ViewMode::FIELD_VIEW) {
        const qint32 targetField = qBound<qint32>(1, snapshot.fieldNumber, totalFields);
        if (targetField != currentFieldNumber) {
            setCurrentField(targetField);
        } else {
            currentFieldNumber = targetField;
            currentFrameNumber = static_cast<qint32>(std::ceil(static_cast<double>(targetField) / 2.0));
            sanitizeCurrentPosition();
        }
    } else {
        const qint32 targetFrame = qBound<qint32>(1, snapshot.frameNumber, totalFrames);
        if (targetFrame != currentFrameNumber) {
            setCurrentFrame(targetFrame);
        } else {
            currentFrameNumber = targetFrame;
            currentFieldNumber = (targetFrame * 2) - 1;
            sanitizeCurrentPosition();
        }
    }

    setViewValues();
    updateBottomStatusReadout();
    updateAspectPushButton();
    updateVideoPushButton();
    updateSourcesPushButton();
    updateImage();

    if (ui && ui->mainTabWidget
        && snapshot.activeMainTabIndex >= 0
        && snapshot.activeMainTabIndex < ui->mainTabWidget->count()) {
        ui->mainTabWidget->setCurrentIndex(snapshot.activeMainTabIndex);
    }

    if (snapshot.showVbiDialog && vbiDialog) {
        vbiDialog->updateVbi(tbcSource.getFrameVbi(), tbcSource.getIsFrameVbiValid());
        vbiDialog->updateVideoId(tbcSource.getFrameVideoId(), tbcSource.getIsFrameVideoIdValid());
        vbiDialog->show();
    }
    if (snapshot.showDropoutDialog && dropoutAnalysisDialog) {
        dropoutAnalysisDialog->show();
    }
    if (snapshot.showVisibleDropoutDialog && visibleDropoutAnalysisDialog) {
        visibleDropoutAnalysisDialog->show();
    }
    if (snapshot.showBlackSnrDialog && blackSnrAnalysisDialog) {
        blackSnrAnalysisDialog->show();
    }
    if (snapshot.showWhiteSnrDialog && whiteSnrAnalysisDialog) {
        whiteSnrAnalysisDialog->show();
    }
    if (snapshot.showVideoParametersDialog && videoParametersDialog) {
        videoParametersDialog->show();
    }
    if (snapshot.showChromaConfigDialog && chromaDecoderConfigDialog) {
        chromaDecoderConfigDialog->show();
    }
}

void MainWindow::queueAnalysisRefreshPreservingUserState(const QString &processedInputFile)
{
    if (!tbcSource.getIsSourceLoaded()) {
        return;
    }
    if (!sameFilePath(processedInputFile, tbcSource.getCurrentSourceFilename())) {
        return;
    }

    pendingUiStateSnapshot = captureUiStateSnapshot();
    restoreUiStateAfterReload = pendingUiStateSnapshot.valid;
    if (statusBar()) {
        statusBar()->clearMessage();
    }

    const QString reloadTarget = lastFilename.isEmpty() ? processedInputFile : lastFilename;
    loadTbcFile(reloadTarget, false, true);
}

// Menu bar signal handlers -------------------------------------------------------------------------------------------

void MainWindow::on_actionExit_triggered()
{
    tbcDebugStream() << "MainWindow::on_actionExit_triggered(): Called";

    // Quit the application
    qApp->quit();
}

// Load a TBC file based on the file selection from the GUI
void MainWindow::on_actionOpen_TBC_file_triggered()
{
    tbcDebugStream() << "MainWindow::on_actionOpen_TBC_file_triggered(): Called";
    if (busyDialog && busyDialog->isVisible()) {
        busyDialog->hide();
    }
    if (!isEnabled()) {
        setEnabled(true);
    }
    QString startPath = configuration.getSourceDirectory();
    QFileInfo startPathInfo(startPath);
    if (startPath.isEmpty() || !startPathInfo.exists()) {
        startPath = QDir::homePath();
    } else if (startPathInfo.isFile()) {
        startPath = startPathInfo.absolutePath();
    }

    QStringList filters;
    filters << tr("TBC/Metadata (*.tbc *.ytbc *.ctbc *.tbcy *.tbcc *.db *.json)")
            << tr("TBC output (*.tbc *.ytbc *.ctbc *.tbcy *.tbcc)")
            << tr("Metadata (*.db *.json)")
            << tr("All Files (*)");

    QString inputFileName;
#if defined(Q_OS_MACOS)
    inputFileName = chooseFileViaAppleScript(startPath);
#else
    QFileDialog fileDialog(this, tr("Open TBC/metadata file"), startPath);
    fileDialog.setFileMode(QFileDialog::ExistingFile);
    fileDialog.setNameFilters(filters);
    fileDialog.selectNameFilter(filters.first());
    fileDialog.setAcceptMode(QFileDialog::AcceptOpen);
    if (fileDialog.exec() == QDialog::Accepted) {
        const QStringList selectedFiles = fileDialog.selectedFiles();
        if (!selectedFiles.isEmpty()) {
            inputFileName = selectedFiles.first();
        }
    }
#endif

    if (!inputFileName.isEmpty() && !isSupportedInputExtension(inputFileName)) {
        QMessageBox::warning(this, tr("Unsupported file"),
                             tr("Please select a supported file type (.tbc, .ytbc, .ctbc, .tbcy, .tbcc, .db, .json)."));
        return;
    }

    // Was a filename specified?
    if (!inputFileName.isEmpty() && !inputFileName.isNull()) {
        requestSourceOpen(inputFileName);
    }
}

// Reload the current TBC selection from the GUI
void MainWindow::on_actionReload_TBC_triggered()
{
    // Reload the current TBC file
    if (!lastFilename.isEmpty() && !lastFilename.isNull()) {
        requestSourceOpen(lastFilename);
    }
}

void MainWindow::on_actionMetadata_Conversion_triggered()
{
    metadataConversionDialog->setSourceDirectory(configuration.getSourceDirectory());
    QString defaultInput;
    if (metadataJsonLoaded && !metadataJsonFilename.isEmpty()) {
        defaultInput = metadataJsonFilename;
    } else if (tbcSource.getIsSourceLoaded()) {
        defaultInput = tbcSource.getCurrentMetadataFilename();
    }
    metadataConversionDialog->setDefaultInput(defaultInput);
    metadataConversionDialog->show();
    metadataConversionDialog->raise();
    metadataConversionDialog->activateWindow();
}

void MainWindow::on_actionMetadata_Status_triggered()
{
    updateMetadataStatusPanel();
    metadataStatusDialog->show();
    metadataStatusDialog->raise();
    metadataStatusDialog->activateWindow();
}
void MainWindow::on_actionExport_Decode_Metadata_triggered()
{
    QString defaultInput;
    if (tbcSource.getIsSourceLoaded()) {
        defaultInput = tbcSource.getCurrentMetadataFilename();
        if (!defaultInput.endsWith(QStringLiteral(".db"), Qt::CaseInsensitive)
            && !defaultInput.endsWith(QStringLiteral(".json"), Qt::CaseInsensitive)) {
            const QString sourceMetadataCandidate =
                resolveMetadataFilenameForSource(tbcSource.getCurrentSourceFilename());
            if (sourceMetadataCandidate.endsWith(QStringLiteral(".db"), Qt::CaseInsensitive)
                || sourceMetadataCandidate.endsWith(QStringLiteral(".json"), Qt::CaseInsensitive)) {
                defaultInput = sourceMetadataCandidate;
            }
        }
    }

    if (!defaultInput.endsWith(QStringLiteral(".db"), Qt::CaseInsensitive)
        && !defaultInput.endsWith(QStringLiteral(".json"), Qt::CaseInsensitive)
        && metadataJsonLoaded) {
        const QString jsonPath = metadataJsonFilename.trimmed();
        if (!jsonPath.isEmpty() && QFileInfo::exists(jsonPath)) {
            defaultInput = jsonPath;
        }
    }
    if (!defaultInput.endsWith(QStringLiteral(".db"), Qt::CaseInsensitive)
        && !defaultInput.endsWith(QStringLiteral(".json"), Qt::CaseInsensitive)) {
        defaultInput.clear();
    }

    const QString toolPath = resolveExternalExecutable({QStringLiteral("tbc-export-metadata")});
    if (toolPath.isEmpty()) {
        QMessageBox::warning(this, tr("Tool not found"),
                             tr("tbc-export-metadata was not found in PATH or alongside the application."));
        return;
    }

    if (!metadataExportDialog) {
        metadataExportDialog = new MetadataExportDialog(this);
        metadataExportDialog->setModal(false);
        metadataExportDialog->setWindowModality(Qt::NonModal);
        metadataExportDialog->setWindowFlag(Qt::Dialog, true);
        metadataExportDialog->setWindowFlag(Qt::WindowContextHelpButtonHint, false);
        metadataExportDialog->setWindowFlag(Qt::WindowMinimizeButtonHint, false);
        connect(metadataExportDialog, &QObject::destroyed, this, [this]() {
            metadataExportDialog = nullptr;
        });
    }

    metadataExportDialog->setModal(false);
    metadataExportDialog->setWindowModality(Qt::NonModal);
    metadataExportDialog->setExportExecutablePath(toolPath);

    const QString sourceDirectory = configuration.getSourceDirectory();
    if (!sourceDirectory.isEmpty()) {
        metadataExportDialog->setSourceDirectory(sourceDirectory);
    }
    if (!defaultInput.isEmpty()) {
        metadataExportDialog->setDefaultInputFile(defaultInput);
    }

    if (!metadataExportDialog->windowHandle()) {
        metadataExportDialog->winId();
    }
    if (metadataExportDialog->windowHandle() && windowHandle()) {
        metadataExportDialog->windowHandle()->setTransientParent(windowHandle());
    }
    metadataExportDialog->show();
    metadataExportDialog->raise();
    metadataExportDialog->activateWindow();

    if (!defaultInput.isEmpty()) {
        statusBar()->showMessage(tr("Opened Metadata Export GUI with %1").arg(defaultInput), 5000);
    } else {
        statusBar()->showMessage(tr("Opened Metadata Export GUI. Select a metadata input file (.db or .json)."), 5000);
    }
}

void MainWindow::on_actionProcess_VBI_triggered()
{
    QString defaultInput;
    if (tbcSource.getIsSourceLoaded()) {
        defaultInput = tbcSource.getCurrentSourceFilename();
    }
    QString inputFileName;
    if (tbcSource.getIsSourceLoaded() && !tbcSource.getIsMetadataOnly()) {
        const QString loadedSourceFilename = tbcSource.getCurrentSourceFilename();
        if (!loadedSourceFilename.isEmpty() && QFileInfo::exists(loadedSourceFilename)) {
            inputFileName = loadedSourceFilename;
        }
    }

    if (inputFileName.isEmpty()) {
        const QString startPath = defaultInput.isEmpty() ? configuration.getSourceDirectory() : defaultInput;
#if defined(Q_OS_MACOS)
        inputFileName = chooseFileViaAppleScript(startPath);
#else
        inputFileName = QFileDialog::getOpenFileName(this,
                                                     tr("Select TBC file for VBI processing"),
                                                     startPath,
                                                     tr("TBC files (*.tbc *.ytbc *.ctbc *.tbcy *.tbcc);;All Files (*)"));
#endif
    }
    if (inputFileName.isEmpty()) {
        return;
    }

    const QString toolPath = resolveExternalExecutable({QStringLiteral("ld-process-vbi")});
    if (toolPath.isEmpty()) {
        QMessageBox::warning(this, tr("Tool not found"),
                             tr("ld-process-vbi was not found in PATH or alongside the application."));
        return;
    }

    QString preferredMetadataFilename;
    if (tbcSource.getIsSourceLoaded()
        && sameFilePath(inputFileName, tbcSource.getCurrentSourceFilename())) {
        preferredMetadataFilename = tbcSource.getCurrentMetadataFilename();
    }
    const QString metadataFilename = resolveMetadataFilenameForSource(inputFileName, preferredMetadataFilename);
    if (metadataFilename.isEmpty()) {
        QMessageBox::warning(this, tr("Metadata not found"),
                             tr("Could not find metadata for:\n%1\n\nExpected .db or .json metadata file.")
                                 .arg(inputFileName));
        return;
    }

    QString backupErrorMessage;
    if (!createTimestampedMetadataBackup(metadataFilename, QStringLiteral(".bup"), &backupErrorMessage)) {
        QMessageBox::warning(this, tr("Backup failed"),
                             backupErrorMessage.isEmpty()
                                 ? tr("Could not create metadata backup before processing.")
                                 : backupErrorMessage);
        return;
    }

    QStringList toolArguments = {
        metadataInputOptionForTool(toolPath), metadataFilename
    };
    const QString noBackupOption = noBackupOptionForTool(toolPath);
    if (!noBackupOption.isEmpty()) {
        toolArguments << noBackupOption;
    }
    const QString teletextOutputDirectory = defaultTeletextHtmlDirectoryForInput(inputFileName);
    if (!teletextOutputDirectory.isEmpty()
        && toolHelpListsOption(toolPath, QStringLiteral("--teletext-html-dir"))) {
        toolArguments << QStringLiteral("--teletext-html-dir") << teletextOutputDirectory;
    }
    toolArguments << inputFileName;

    QString errorMessage;
    if (!runExternalToolWithProgress(toolPath, toolArguments, tr("VBI processing"), &errorMessage)) {
        if (errorMessage.contains(tr("cancelled"), Qt::CaseInsensitive)) {
            statusBar()->showMessage(tr("VBI processing cancelled for %1").arg(inputFileName), 4000);
            return;
        }
        QMessageBox::warning(this, tr("Process failed"),
                             errorMessage.isEmpty()
                                 ? tr("ld-process-vbi failed.")
                                 : errorMessage);
        return;
    }

    const bool reloadingCurrentSource = tbcSource.getIsSourceLoaded()
                                        && sameFilePath(inputFileName, tbcSource.getCurrentSourceFilename());

    const QString autoTeletextDirectory = resolveTeletextHtmlDirectoryFromHints({
        teletextOutputDirectory,
        inputFileName,
        metadataFilename
    });
    if (!autoTeletextDirectory.isEmpty()) {
        if (!teletextViewerDialog) {
            teletextViewerDialog = new TeletextViewerDialog(nullptr);
            teletextViewerDialog->setWindowFlag(Qt::Window, true);
        }
        if (teletextViewerDialog->directory().compare(autoTeletextDirectory, Qt::CaseInsensitive) != 0) {
            teletextViewerDialog->setDirectory(autoTeletextDirectory);
        }
        teletextViewerDialog->show();
        teletextViewerDialog->raise();
        teletextViewerDialog->activateWindow();
    }
    if (reloadingCurrentSource) {
        queueAnalysisRefreshPreservingUserState(inputFileName);
    } else {
        if (!autoTeletextDirectory.isEmpty()) {
            statusBar()->showMessage(
                tr("VBI processing completed for %1 (teletext loaded from %2)")
                    .arg(inputFileName, autoTeletextDirectory),
                5000
            );
        } else {
            statusBar()->showMessage(tr("VBI processing completed for %1").arg(inputFileName), 4000);
        }
    }
}

void MainWindow::on_actionProcess_VITS_triggered()
{
    QString defaultInput;
    if (tbcSource.getIsSourceLoaded()) {
        defaultInput = tbcSource.getCurrentSourceFilename();
    }
    QString inputFileName;
    if (tbcSource.getIsSourceLoaded() && !tbcSource.getIsMetadataOnly()) {
        const QString loadedSourceFilename = tbcSource.getCurrentSourceFilename();
        if (!loadedSourceFilename.isEmpty() && QFileInfo::exists(loadedSourceFilename)) {
            inputFileName = loadedSourceFilename;
        }
    }

    if (inputFileName.isEmpty()) {
        const QString startPath = defaultInput.isEmpty() ? configuration.getSourceDirectory() : defaultInput;
#if defined(Q_OS_MACOS)
        inputFileName = chooseFileViaAppleScript(startPath);
#else
        inputFileName = QFileDialog::getOpenFileName(this,
                                                     tr("Select TBC file for VITS processing"),
                                                     startPath,
                                                     tr("TBC files (*.tbc *.ytbc *.ctbc *.tbcy *.tbcc);;All Files (*)"));
#endif
    }
    if (inputFileName.isEmpty()) {
        return;
    }

    const QString toolPath = resolveExternalExecutable({QStringLiteral("ld-process-vits")});
    if (toolPath.isEmpty()) {
        QMessageBox::warning(this, tr("Tool not found"),
                             tr("ld-process-vits was not found in PATH or alongside the application."));
        return;
    }

    QString preferredMetadataFilename;
    if (tbcSource.getIsSourceLoaded()
        && sameFilePath(inputFileName, tbcSource.getCurrentSourceFilename())) {
        preferredMetadataFilename = tbcSource.getCurrentMetadataFilename();
    }
    const QString metadataFilename = resolveMetadataFilenameForSource(inputFileName, preferredMetadataFilename);
    if (metadataFilename.isEmpty()) {
        QMessageBox::warning(this, tr("Metadata not found"),
                             tr("Could not find metadata for:\n%1\n\nExpected .db or .json metadata file.")
                                 .arg(inputFileName));
        return;
    }

    QString backupErrorMessage;
    if (!createTimestampedMetadataBackup(metadataFilename, QStringLiteral(".vbup"), &backupErrorMessage)) {
        QMessageBox::warning(this, tr("Backup failed"),
                             backupErrorMessage.isEmpty()
                                 ? tr("Could not create metadata backup before processing.")
                                 : backupErrorMessage);
        return;
    }

    QStringList toolArguments = {
        metadataInputOptionForTool(toolPath), metadataFilename
    };
    const QString noBackupOption = noBackupOptionForTool(toolPath);
    if (!noBackupOption.isEmpty()) {
        toolArguments << noBackupOption;
    }
    toolArguments << inputFileName;

    QString errorMessage;
    if (!runExternalToolWithProgress(toolPath, toolArguments, tr("VITS processing"), &errorMessage)) {
        if (errorMessage.contains(tr("cancelled"), Qt::CaseInsensitive)) {
            statusBar()->showMessage(tr("VITS processing cancelled for %1").arg(inputFileName), 4000);
            return;
        }
        QMessageBox::warning(this, tr("Process failed"),
                             errorMessage.isEmpty()
                                 ? tr("ld-process-vits failed.")
                                 : errorMessage);
        return;
    }

    const bool reloadingCurrentSource = tbcSource.getIsSourceLoaded()
                                        && sameFilePath(inputFileName, tbcSource.getCurrentSourceFilename());
    if (reloadingCurrentSource) {
        queueAnalysisRefreshPreservingUserState(inputFileName);
    } else {
        statusBar()->showMessage(tr("VITS processing completed for %1").arg(inputFileName), 4000);
    }
}

void MainWindow::on_actionFix_JSON_SNR_triggered()
{
    const auto isMetadataFile = [](const QString &filename) {
        return filename.endsWith(QStringLiteral(".json"), Qt::CaseInsensitive)
               || filename.endsWith(QStringLiteral(".db"), Qt::CaseInsensitive);
    };
    const auto isTbcSourceFile = [](const QString &filename) {
        return filename.endsWith(QStringLiteral(".tbc"), Qt::CaseInsensitive)
               || filename.endsWith(QStringLiteral(".ytbc"), Qt::CaseInsensitive)
               || filename.endsWith(QStringLiteral(".ctbc"), Qt::CaseInsensitive)
               || filename.endsWith(QStringLiteral(".tbcy"), Qt::CaseInsensitive)
               || filename.endsWith(QStringLiteral(".tbcc"), Qt::CaseInsensitive);
    };

    const QString toolPath = resolveExternalExecutable({QStringLiteral("ld-process-vits")});
    if (toolPath.isEmpty()) {
        QMessageBox::warning(this, tr("Tool not found"),
                             tr("ld-process-vits was not found in PATH or alongside the application."));
        return;
    }

    QString metadataFilename;
    QString inputTbcFilename;
    const QString currentMetadataFilename = tbcSource.getCurrentMetadataFilename();
    if (!currentMetadataFilename.isEmpty()
        && QFileInfo::exists(currentMetadataFilename)
        && isMetadataFile(currentMetadataFilename)) {
        metadataFilename = currentMetadataFilename;
    } else if (metadataJsonLoaded
               && !metadataJsonFilename.isEmpty()
               && QFileInfo::exists(metadataJsonFilename)
               && isMetadataFile(metadataJsonFilename)) {
        metadataFilename = metadataJsonFilename;
    }

    if (metadataFilename.isEmpty()) {
        QMessageBox::warning(this, tr("Metadata not found"),
                             tr("Could not determine the currently loaded metadata file.\n"
                                "Load the metadata you want to fix in Analyse, then run Fix JSON SNR again."));
        return;
    }
    if (!isMetadataFile(metadataFilename)) {
        QMessageBox::warning(this, tr("Unsupported metadata file"),
                             tr("The currently loaded metadata is not a .json or .db file.\n"
                                "Load a supported metadata file in Analyse first."));
        return;
    }

    if (!tbcSource.getIsMetadataOnly()) {
        const QString currentSourceFilename = tbcSource.getCurrentSourceFilename();
        if (!currentSourceFilename.isEmpty()
            && QFileInfo::exists(currentSourceFilename)
            && isTbcSourceFile(currentSourceFilename)) {
            inputTbcFilename = currentSourceFilename;
        }
    }

    if (inputTbcFilename.isEmpty()) {
        inputTbcFilename = resolveSourceFilenameForMetadata(metadataFilename);
    }
    if (inputTbcFilename.isEmpty()) {
        QMessageBox::warning(this, tr("Source TBC not found"),
                             tr("Could not determine the source TBC for the currently loaded metadata:\n%1\n\n"
                                "Load the matching source/metadata in Analyse first, then run Fix JSON SNR again.")
                                 .arg(metadataFilename));
        return;
    }

    if (!isTbcSourceFile(inputTbcFilename)) {
        QMessageBox::warning(this, tr("Unsupported TBC file"),
                             tr("The resolved source file is not a supported TBC input:\n%1")
                                 .arg(inputTbcFilename));
        return;
    }

    QString backupErrorMessage;
    if (!createTimestampedMetadataBackup(metadataFilename, QStringLiteral(".vbup"), &backupErrorMessage)) {
        QMessageBox::warning(this, tr("Backup failed"),
                             backupErrorMessage.isEmpty()
                                 ? tr("Could not create metadata backup before processing.")
                                 : backupErrorMessage);
        return;
    }

    QStringList toolArguments = {
        metadataInputOptionForTool(toolPath), metadataFilename
    };
    const QString noBackupOption = noBackupOptionForTool(toolPath);
    if (!noBackupOption.isEmpty()) {
        toolArguments << noBackupOption;
    }
    toolArguments << inputTbcFilename;

    QString errorMessage;
    if (!runExternalToolWithProgress(toolPath, toolArguments, tr("Fix JSON SNR"), &errorMessage)) {
        if (errorMessage.contains(tr("cancelled"), Qt::CaseInsensitive)) {
            statusBar()->showMessage(tr("Fix JSON SNR cancelled for %1").arg(metadataFilename), 4000);
            return;
        }
        QMessageBox::warning(this, tr("Process failed"),
                             errorMessage.isEmpty()
                                 ? tr("Fix JSON SNR failed.")
                                 : errorMessage);
        return;
    }

    const bool reloadingCurrentSource = tbcSource.getIsSourceLoaded()
                                        && sameFilePath(inputTbcFilename, tbcSource.getCurrentSourceFilename());
    const bool reloadingCurrentMetadata = tbcSource.getIsSourceLoaded()
                                          && (sameFilePath(metadataFilename, tbcSource.getCurrentMetadataFilename())
                                              || sameFilePath(metadataFilename, metadataJsonFilename));
    if (reloadingCurrentSource) {
        queueAnalysisRefreshPreservingUserState(inputTbcFilename);
    } else if (reloadingCurrentMetadata && !lastFilename.isEmpty()) {
        loadTbcFile(lastFilename, false, true);
    } else {
        statusBar()->showMessage(tr("Fix JSON SNR completed for %1").arg(metadataFilename), 4000);
    }
}

void MainWindow::on_actionAuto_Audio_Align_triggered()
{

    QString defaultJsonPath;
    if (metadataJsonLoaded && !metadataJsonFilename.isEmpty()) {
        defaultJsonPath = metadataJsonFilename;
    } else if (tbcSource.getIsSourceLoaded()) {
        const QString currentMetadataFilename = tbcSource.getCurrentMetadataFilename();
        if (currentMetadataFilename.endsWith(QStringLiteral(".json"), Qt::CaseInsensitive)) {
            defaultJsonPath = currentMetadataFilename;
        } else if (currentMetadataFilename.endsWith(QStringLiteral(".db"), Qt::CaseInsensitive)) {
            QString candidateJsonPath = currentMetadataFilename;
            candidateJsonPath.chop(3);
            candidateJsonPath += QStringLiteral(".json");
            if (QFileInfo::exists(candidateJsonPath)) {
                defaultJsonPath = candidateJsonPath;
            }
        }

        if (defaultJsonPath.isEmpty()) {
            const QString currentSourceFilename = tbcSource.getCurrentSourceFilename();
            if (!currentSourceFilename.isEmpty()) {
                const QString sidecarJsonPath = currentSourceFilename + QStringLiteral(".json");
                if (QFileInfo::exists(sidecarJsonPath)) {
                    defaultJsonPath = sidecarJsonPath;
                } else {
                    const QFileInfo sourceInfo(currentSourceFilename);
                    const QString stemJsonPath = QDir(sourceInfo.absolutePath())
                                                     .filePath(sourceInfo.completeBaseName()
                                                               + QStringLiteral(".json"));
                    if (QFileInfo::exists(stemJsonPath)) {
                        defaultJsonPath = stemJsonPath;
                    }
                }
            }
        }
    }

    if (!audioAlignmentDialog) {
        audioAlignmentDialog = new AudioAlignmentDialog(this);
        audioAlignmentDialog->setModal(false);
        audioAlignmentDialog->setWindowModality(Qt::NonModal);
        audioAlignmentDialog->setWindowFlags(Qt::Window
                                             | Qt::CustomizeWindowHint
                                             | Qt::WindowTitleHint
                                             | Qt::WindowSystemMenuHint
                                             | Qt::WindowMinimizeButtonHint
                                             | Qt::WindowCloseButtonHint);
        audioAlignmentDialog->setAttribute(Qt::WA_TranslucentBackground, false);
        audioAlignmentDialog->setAttribute(Qt::WA_NoSystemBackground, false);
        audioAlignmentDialog->setAutoFillBackground(true);
        audioAlignmentDialog->setWindowOpacity(1.0);
        connect(audioAlignmentDialog, &QObject::destroyed, this, [this]() {
            audioAlignmentDialog = nullptr;
        });
        connect(audioAlignmentDialog,
                &AudioAlignmentDialog::exportTracksPrepared,
                this,
                [this](const QStringList &trackFiles, const QStringList &trackNames) {
            if (!exportDialog || trackFiles.isEmpty()) {
                return;
            }
            exportDialog->loadAudioTracksForExport(trackFiles, trackNames);
            if (ui && ui->mainTabWidget) {
                ui->mainTabWidget->setCurrentWidget(exportDialog);
            }
            statusBar()->showMessage(tr("Loaded %1 aligned audio track(s) into Export.")
                                         .arg(trackFiles.size()),
                                     5000);
        });
    }
    audioAlignmentDialog->setModal(false);
    audioAlignmentDialog->setWindowModality(Qt::NonModal);
    audioAlignmentDialog->setWindowOpacity(1.0);

    const QString sourceDirectory = configuration.getSourceDirectory();
    if (!sourceDirectory.isEmpty()) {
        audioAlignmentDialog->setSourceDirectory(sourceDirectory);
    }
    audioAlignmentDialog->setExportTrackOutputFile(QString());
    if (!defaultJsonPath.isEmpty()) {
        audioAlignmentDialog->setDefaultJson(defaultJsonPath);
    }

    audioAlignmentDialog->show();
    if (audioAlignmentDialog->windowHandle() && windowHandle()) {
        audioAlignmentDialog->windowHandle()->setTransientParent(windowHandle());
    }
    audioAlignmentDialog->raise();
    audioAlignmentDialog->activateWindow();

    if (!defaultJsonPath.isEmpty()) {
        statusBar()->showMessage(tr("Opened Auto Audio Align with %1").arg(defaultJsonPath), 5000);
    } else {
        statusBar()->showMessage(tr("Opened Auto Audio Align. Select metadata JSON and audio input files."), 5000);
    }
}

void MainWindow::on_actionEFM_Handler_triggered()
{
    if (!efmHandlerDialog) {
        efmHandlerDialog = new EfmHandlerDialog(this);
        efmHandlerDialog->setModal(false);
        efmHandlerDialog->setWindowModality(Qt::NonModal);
        efmHandlerDialog->setWindowFlags(Qt::Window
                                         | Qt::CustomizeWindowHint
                                         | Qt::WindowTitleHint
                                         | Qt::WindowSystemMenuHint
                                         | Qt::WindowMinimizeButtonHint
                                         | Qt::WindowCloseButtonHint);
        efmHandlerDialog->setAttribute(Qt::WA_TranslucentBackground, false);
        efmHandlerDialog->setAttribute(Qt::WA_NoSystemBackground, false);
        efmHandlerDialog->setAutoFillBackground(true);
        efmHandlerDialog->setWindowOpacity(1.0);
        connect(efmHandlerDialog, &QObject::destroyed, this, [this]() {
            efmHandlerDialog = nullptr;
        });
        connect(efmHandlerDialog, &EfmHandlerDialog::exportTracksPrepared, this,
                [this](const QStringList &trackFiles, const QStringList &trackNames) {
                    if (!exportDialog || trackFiles.isEmpty()) {
                        return;
                    }
                    exportDialog->loadAudioTracksForExport(trackFiles, trackNames);
                    if (ui && ui->mainTabWidget) {
                        ui->mainTabWidget->setCurrentWidget(exportDialog);
                    }
                    statusBar()->showMessage(tr("Loaded %1 decoded EFM audio track(s) into Export.")
                                                 .arg(trackFiles.size()),
                                             5000);
                });
    }

    efmHandlerDialog->setModal(false);
    efmHandlerDialog->setWindowModality(Qt::NonModal);
    efmHandlerDialog->setWindowOpacity(1.0);

    QString sourceDirectory = outputRootDirectoryForCurrentSource();
    if (sourceDirectory.isEmpty()) {
        sourceDirectory = configuration.getSourceDirectory();
    }
    if (!sourceDirectory.isEmpty()) {
        efmHandlerDialog->setSourceDirectory(sourceDirectory);
    }

    QString suggestedOutputBase;
    const QString sourceFilename = tbcSource.getCurrentSourceFilename();
    if (!sourceFilename.isEmpty()) {
        const QFileInfo sourceInfo(sourceFilename);
        suggestedOutputBase = QDir(sourceInfo.absolutePath())
                                  .filePath(sourceInfo.completeBaseName());
    } else if (!lastFilename.isEmpty()) {
        const QFileInfo sourceInfo(lastFilename);
        suggestedOutputBase = QDir(sourceInfo.absolutePath())
                                  .filePath(sourceInfo.completeBaseName());
    }
    if (!suggestedOutputBase.isEmpty()) {
        efmHandlerDialog->setSuggestedOutputBase(suggestedOutputBase);
    }
    applyEfmHandlerAutoloads(sourceDirectory);

    efmHandlerDialog->show();
    if (efmHandlerDialog->windowHandle() && windowHandle()) {
        efmHandlerDialog->windowHandle()->setTransientParent(windowHandle());
    }
    efmHandlerDialog->raise();
    efmHandlerDialog->activateWindow();
    statusBar()->showMessage(tr("Opened EFM Handler. Configure EFM/AC3 stages and run the pipeline."), 5000);
}

void MainWindow::on_actionLDS_Converter_triggered()
{
    const QString toolPath = resolveExternalExecutable({QStringLiteral("ld-lds-converter")});
    if (toolPath.isEmpty()) {
        QMessageBox::warning(this, tr("Tool not found"),
                             tr("ld-lds-converter was not found in PATH or alongside the application."));
        return;
    }

    const QStringList toolArguments = {QStringLiteral("--gui")};
    const QString workingDirectory = QFileInfo(toolPath).absolutePath();
    qint64 launchedProcessId = 0;
    const bool started = QProcess::startDetached(toolPath,
                                                 toolArguments,
                                                 workingDirectory,
                                                 &launchedProcessId);
    if (!started) {
        QMessageBox::warning(this, tr("Launch failed"),
                             tr("Could not launch ld-lds-converter as an independent application."));
        return;
    }

    if (statusBar()) {
        statusBar()->showMessage(tr("Opened LDS Converter as an independent application."), 5000);
    }
}
// Start saving the modified metadata
void MainWindow::on_actionSave_Metadata_triggered()
{
    tbcSource.saveSourceMetadata();

    // Saving continues in the background...
}

// Display the scan line oscilloscope view
void MainWindow::on_actionLine_scope_triggered()
{
    if (tbcSource.getIsSourceLoaded()) {
        // Show the oscilloscope dialogue for the selected scan-line
        if (!oscilloscopeDialog->isVisible()) {
            oscilloscopeDialog->show();
        }
        updateOscilloscopeDialogue();
        oscilloscopeDialog->raise();
        oscilloscopeDialog->activateWindow();
    }
}

// Display the vectorscope view
void MainWindow::on_actionVectorscope_triggered()
{
    if (tbcSource.getIsSourceLoaded()) {
        // Show the vectorscope dialogue
        if (!vectorscopeDialog->isVisible()) {
            vectorscopeDialog->show();
        }
        updateVectorscopeDialogue();
        vectorscopeDialog->raise();
        vectorscopeDialog->activateWindow();
    }
}

// Display the waveform monitor view
void MainWindow::on_actionWaveform_monitor_triggered()
{
    if (tbcSource.getIsSourceLoaded() && !tbcSource.getIsMetadataOnly()) {
        if (!waveformMonitorDialog->isVisible()) {
            waveformMonitorDialog->show();
        }
        updateWaveformMonitorDialogue();
        waveformMonitorDialog->raise();
        waveformMonitorDialog->activateWindow();
    }
}

// Display the RGB scope pop-out view
void MainWindow::on_actionRGB_scope_triggered()
{
    if (!tbcSource.getIsSourceLoaded() || tbcSource.getIsMetadataOnly()) {
        return;
    }
    if (!rgbScopeDialog->isVisible()) {
        rgbScopeDialog->show();
    }
    updateRgbScopeDialogue(true);
    rgbScopeDialog->raise();
    rgbScopeDialog->activateWindow();
}

// Display the YUV range scope pop-out view
void MainWindow::on_actionYUV_range_scope_triggered()
{
    if (!tbcSource.getIsSourceLoaded() || tbcSource.getIsMetadataOnly()) {
        return;
    }
    if (!yuvRangeDialog->isVisible()) {
        yuvRangeDialog->show();
    }
    updateYuvRangeScopeDialogue(true);
    yuvRangeDialog->raise();
    yuvRangeDialog->activateWindow();
}

// Display the field timing scope view
void MainWindow::on_actionField_timing_scope_triggered()
{
    if (tbcSource.getIsSourceLoaded()) {
        updateFieldTimingDialogue();
        fieldTimingDialog->show();
        fieldTimingDialog->raise();
        fieldTimingDialog->activateWindow();
    }
}

// Open the TBC Tools Wiki page
void MainWindow::on_actionTBC_Tools_Wiki_triggered()
{
    const QUrl wikiUrl(QStringLiteral("https://github.com/harrypm/tbc-tools/wiki"));
    if (!QDesktopServices::openUrl(wikiUrl)) {
        QMessageBox::warning(this, tr("Warning"),
                             tr("Could not open TBC Tools Wiki URL:\n%1").arg(wikiUrl.toString()));
        return;
    }

    if (statusBar()) {
        statusBar()->showMessage(tr("Opened TBC Tools Wiki."), 4000);
    }
}

// Show the about window
void MainWindow::on_actionAbout_ld_analyse_triggered()
{
    aboutDialog->show();
}

// Show the Plugin Manager (Plugins > Plugin Manager...)
void MainWindow::on_actionPluginManager_triggered()
{
    if (!pluginManagerDialog) {
        pluginManagerDialog = new PluginManagerDialog(this);
        pluginManagerDialog->setWindowFlag(Qt::Window, true);
    }
    pluginManagerDialog->show();
    pluginManagerDialog->raise();
    pluginManagerDialog->activateWindow();
}

// Show the Metadata Editor (Tools > Metadata Editor...)
void MainWindow::on_actionMetadata_Editor_triggered()
{
    if (!tbcSource.getIsSourceLoaded()) {
        statusBar()->showMessage(tr("No source loaded to edit metadata for."), 3000);
        return;
    }
    metadataEditorDialog->setVideoParameters(tbcSource.getVideoParameters());
    metadataEditorDialog->setPcmAudioParameters(tbcSource.getPcmAudioParameters());
    // Push the current frame's first-field SECAM line identity so the per-field
    // checkbox reflects the loaded source. Only meaningful for SECAM-family.
    {
        const VideoSystem system = tbcSource.getSystem();
        const bool isSecamFamily = (system == SECAM || system == MESECAM);
        const qint32 firstField = tbcSource.getFirstFieldNumber();
        const bool firstLineIsRed = (firstField >= 1) ? tbcSource.getSecamFirstLineIsRed(firstField) : false;
        metadataEditorDialog->setSecamFieldContext(firstField, firstLineIsRed, isSecamFamily);
    }
    metadataEditorDialog->show();
    metadataEditorDialog->raise();
    metadataEditorDialog->activateWindow();
}

// Check for updates - manual trigger from the Help menu
void MainWindow::on_actionCheck_for_Updates_triggered()
{
    if (!updateChecker) {
        return;
    }
    // Manual checks always present their full result to the user.
    updateCheckSilent = false;
    if (statusBar()) {
        statusBar()->showMessage(tr("Checking GitHub for updates..."), 0);
    }
    updateChecker->checkForUpdates();
}

// Automatic background check, throttled to once per week via the stored timestamp.
void MainWindow::maybePerformWeeklyUpdateCheck()
{
    if (!updateChecker) {
        return;
    }
    if (!configuration.getUpdateCheckEnabled()) {
        return;
    }

    // Skip the automatic check for unversioned/dev builds to avoid noisy prompts.
    const QString currentVersion = QString::fromUtf8(APP_VERSION);
    if (currentVersion.isEmpty() || currentVersion == QStringLiteral("0.0.0")) {
        return;
    }

    const QString lastTimestamp = configuration.getLastUpdateCheckTimestamp();
    if (!lastTimestamp.isEmpty()) {
        const QDateTime lastCheck = QDateTime::fromString(lastTimestamp, Qt::ISODate);
        if (lastCheck.isValid()) {
            const qint64 secsSinceLastCheck = lastCheck.secsTo(QDateTime::currentDateTimeUtc());
            const qint64 weekSeconds = 7 * 24 * 60 * 60;
            if (secsSinceLastCheck < weekSeconds) {
                tbcDebugStream() << "MainWindow::maybePerformWeeklyUpdateCheck(): last check"
                                 << secsSinceLastCheck << "s ago, not due yet";
                return;
            }
        }
    }

    tbcDebugStream() << "MainWindow::maybePerformWeeklyUpdateCheck(): performing weekly update check";
    updateCheckSilent = true;
    updateChecker->checkForUpdates();
}

// Persist the timestamp of this check attempt so the weekly throttle works.
void MainWindow::recordUpdateCheckAttempt()
{
    configuration.setLastUpdateCheckTimestamp(QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
    configuration.writeConfiguration();
}

void MainWindow::onUpdateAvailable(const QString &latestVersion, const QString &releaseUrl, const QString &releaseName)
{
    recordUpdateCheckAttempt();
    if (statusBar()) {
        statusBar()->clearMessage();
    }

    if (updateCheckSilent) {
        // Honour a previously-saved "Skip this version" for the automatic check.
        if (!latestVersion.isEmpty()
            && latestVersion == configuration.getSkippedUpdateVersion()) {
            tbcDebugStream() << "MainWindow::onUpdateAvailable(): version" << latestVersion
                             << "skipped by user; suppressing automatic prompt";
            return;
        }
    }

    showUpdateAvailableDialog(latestVersion, releaseUrl, releaseName);
}

void MainWindow::onUpdateUpToDate(const QString &currentVersion, const QString &latestVersion, const QString &releaseUrl)
{
    Q_UNUSED(releaseUrl)
    recordUpdateCheckAttempt();
    if (statusBar()) {
        statusBar()->clearMessage();
    }

    if (updateCheckSilent) {
        if (statusBar()) {
            statusBar()->showMessage(tr("tbc-tools is up to date (v%1).").arg(currentVersion), 5000);
        }
        tbcDebugStream() << "MainWindow::onUpdateUpToDate(): up to date (" << currentVersion << ")";
        return;
    }

    QMessageBox::information(this, tr("Up to date"),
        tr("You are running the latest version of tbc-tools.\n\n"
           "Installed: %1\nLatest: %2").arg(currentVersion, latestVersion));
}

void MainWindow::onUpdateCheckFailed(const QString &errorString)
{
    recordUpdateCheckAttempt();
    if (statusBar()) {
        statusBar()->clearMessage();
    }

    tbcDebugStream() << "MainWindow::onUpdateCheckFailed():" << errorString;

    if (updateCheckSilent) {
        // Stay quiet on automatic failures - the user can still check manually.
        return;
    }

    QMessageBox box(this);
    box.setIcon(QMessageBox::Warning);
    box.setWindowTitle(tr("Update check failed"));
    box.setText(tr("Could not check for updates."));
    box.setInformativeText(errorString + QStringLiteral("\n\n") +
        tr("You can view the latest release in your browser instead."));
    QPushButton *openButton = box.addButton(tr("Open releases page"), QMessageBox::AcceptRole);
    box.addButton(QMessageBox::Close);
    box.exec();
    if (box.clickedButton() == openButton) {
        QDesktopServices::openUrl(QUrl(UpdateChecker::releasesUrl()));
    }
}

void MainWindow::showUpdateAvailableDialog(const QString &latestVersion, const QString &releaseUrl, const QString &releaseName)
{
    const QString currentVersion = QString::fromUtf8(APP_VERSION);

    QMessageBox box(this);
    box.setIcon(QMessageBox::Information);
    box.setWindowTitle(tr("Update available"));
    box.setText(tr("A new version of tbc-tools is available."));
    QString info = tr("Installed: %1\nLatest: %2").arg(currentVersion, latestVersion);
    if (!releaseName.isEmpty()) {
        info += QStringLiteral("\n") + releaseName;
    }
    info += QStringLiteral("\n\n") + tr("Would you like to open the release page to download it?");
    box.setInformativeText(info);

    QPushButton *downloadButton = box.addButton(tr("Download"), QMessageBox::AcceptRole);
    QPushButton *skipButton = box.addButton(tr("Skip this version"), QMessageBox::ActionRole);
    QPushButton *laterButton = box.addButton(tr("Remind me later"), QMessageBox::RejectRole);
    box.exec();

    QAbstractButton *clicked = box.clickedButton();
    if (clicked == downloadButton) {
        const QUrl url(releaseUrl.isEmpty() ? UpdateChecker::releasesUrl() : releaseUrl);
        if (!QDesktopServices::openUrl(url)) {
            QMessageBox::warning(this, tr("Warning"),
                tr("Could not open the release URL:\n%1").arg(url.toString()));
        }
    } else if (clicked == skipButton) {
        configuration.setSkippedUpdateVersion(latestVersion);
        configuration.writeConfiguration();
        if (statusBar()) {
            statusBar()->showMessage(tr("Skipped version %1. You can still check manually via Help > Check for Updates.").arg(latestVersion), 6000);
        }
    } else if (clicked == laterButton) {
        // No-op; the timestamp was already recorded so it won't prompt again for a week.
    }
}
void MainWindow::on_actionTeletext_Viewer_triggered()
{
    if (!teletextViewerDialog) {
        teletextViewerDialog = new TeletextViewerDialog(nullptr);
        teletextViewerDialog->setWindowFlag(Qt::Window, true);
    }
    const QString suggestedDirectory = resolveTeletextHtmlDirectoryFromHints({
        tbcSource.getCurrentSourceFilename(),
        lastFilename,
        configuration.getSourceDirectory()
    });

    if (!suggestedDirectory.isEmpty()
        && teletextViewerDialog->directory().compare(suggestedDirectory, Qt::CaseInsensitive) != 0) {
        teletextViewerDialog->setDirectory(suggestedDirectory);
    }

    teletextViewerDialog->show();
    teletextViewerDialog->raise();
    teletextViewerDialog->activateWindow();
}

// Show the VBI window
void MainWindow::on_actionVBI_triggered()
{
    // Show the VBI dialogue
    vbiDialog->updateVbi(tbcSource.getFrameVbi(), tbcSource.getIsFrameVbiValid());
    vbiDialog->updateVideoId(tbcSource.getFrameVideoId(), tbcSource.getIsFrameVideoIdValid());
    vbiDialog->show();
}

// Show the drop out analysis graph
void MainWindow::on_actionDropout_analysis_triggered()
{
    // Show the dropout analysis dialogue
    dropoutAnalysisDialog->show();
}

// Show the visible drop out analysis graph
void MainWindow::on_actionVisible_Dropout_analysis_triggered()
{
    // Show the visible dropout analysis dialogue
    visibleDropoutAnalysisDialog->show();
}

// Show the Black SNR analysis graph
void MainWindow::on_actionSNR_analysis_triggered()
{
    // Show the black SNR analysis dialogue
    blackSnrAnalysisDialog->show();
}

// Show the White SNR analysis graph
void MainWindow::on_actionWhite_SNR_analysis_triggered()
{
    // Show the white SNR analysis dialogue
    whiteSnrAnalysisDialog->show();
}

// Save current frame as PNG
void MainWindow::on_actionSave_frame_as_PNG_triggered()
{
    tbcDebugStream() << "MainWindow::on_actionSave_frame_as_PNG_triggered(): Called";
    if (!tbcSource.getIsSourceLoaded()) {
        QMessageBox::warning(this, tr("Warning"), tr("No source file loaded."));
        return;
    }
    if (tbcSource.getIsMetadataOnly()) {
        QMessageBox::warning(this, tr("Warning"), tr("Metadata-only mode cannot export PNG images."));
        return;
    }

    const QImage imageToSave = renderedCurrentImageForExport();
    if (imageToSave.isNull()) {
        QMessageBox::warning(this, tr("Warning"), tr("No image data is available to export as PNG."));
        return;
    }

    QString outputDirectory = configuration.getPngDirectory().trimmed();
    if (outputDirectory.isEmpty()) {
        outputDirectory = outputRootDirectoryForCurrentSource();
    }
    if (outputDirectory.isEmpty()) {
        outputDirectory = QDir::homePath();
    }

    // Create a suggestion for the filename
    QString filenameStem;
    switch (tbcSource.getViewMode()) {
    case TbcSource::ViewMode::FRAME_VIEW:
        filenameStem += tr("frame_");
        break;

    case TbcSource::ViewMode::SPLIT_VIEW:
        filenameStem += tr("fields_");
        break;

    case TbcSource::ViewMode::FIELD_VIEW:
        filenameStem += tr("field_");
        break;

    case TbcSource::ViewMode::RGB_SCOPE_VIEW:
        filenameStem += tr("rgb_scope_");
        break;
    }

    if (tbcSource.getSystem() == PAL) filenameStem += tr("pal_");
    else if (tbcSource.getSystem() == PAL_M) filenameStem += tr("palm_");
    else filenameStem += tr("ntsc_");

    if (!tbcSource.getChromaDecoder()) filenameStem += tr("source_");
    else filenameStem += tr("chroma_");

    if (displayAspectRatio) {
        if (tbcSource.getIsWidescreen()) filenameStem += tr("ar169_");
        else filenameStem += tr("ar43_");
    }

    if (tbcSource.getViewMode() == TbcSource::ViewMode::FIELD_VIEW) {
        filenameStem += QString::number(currentFieldNumber);
    } else {
        filenameStem += QString::number(currentFrameNumber);
    }

    const QString sourceFileName = QFileInfo(tbcSource.getCurrentSourceFilename()).fileName();
    if (!sourceFileName.isEmpty()) {
        filenameStem += QStringLiteral("_");
        filenameStem += sanitizedFileToken(QFileInfo(sourceFileName).completeBaseName());
    }
    const QString filenameSuggestion =
        QDir(outputDirectory).filePath(filenameStem + tr(".png"));

    QString pngFilename = QFileDialog::getSaveFileName(this,
                tr("Save PNG file"),
                filenameSuggestion,
                tr("PNG image (*.png);;All Files (*)"));

    // Was a filename specified?
    if (!pngFilename.isEmpty() && !pngFilename.isNull()) {
        if (QFileInfo(pngFilename).suffix().isEmpty()) {
            pngFilename += QStringLiteral(".png");
        }

        // Save the current frame as a PNG
        tbcDebugStream() << "MainWindow::on_actionSave_frame_as_PNG_triggered(): Saving current frame as" << pngFilename;

        // Save the QImage as PNG
        if (!imageToSave.save(pngFilename)) {
            tbcDebugStream() << "MainWindow::on_actionSave_frame_as_PNG_triggered(): Failed to save file as" << pngFilename;

            QMessageBox messageBox;
            messageBox.warning(this, tr("Warning"),tr("Could not save a PNG using the specified filename!"));
        }

        // Update the configuration for the PNG directory
        QFileInfo pngFileInfo(pngFilename);
        configuration.setPngDirectory(pngFileInfo.absolutePath());
        tbcDebugStream() << "MainWindow::on_actionSave_frame_as_PNG_triggered(): Setting PNG directory to:" << pngFileInfo.absolutePath();
        configuration.writeConfiguration();
    }
}

void MainWindow::copyCurrentFrameToClipboard()
{
    if (!tbcSource.getIsSourceLoaded()) {
        statusBar()->showMessage(tr("No source loaded to copy."), 3000);
        return;
    }

    const QImage imageToCopy = renderedCurrentImageForExport();
    if (imageToCopy.isNull()) {
        statusBar()->showMessage(tr("No display image available to copy."), 3000);
        return;
    }

    QClipboard *clipboard = QApplication::clipboard();
    if (!clipboard) {
        statusBar()->showMessage(tr("Clipboard is unavailable."), 3000);
        return;
    }

    clipboard->setImage(imageToCopy);
    statusBar()->showMessage(tr("Copied current frame to clipboard."), 3000);
}

void MainWindow::on_actionCopy_current_display_to_clipboard_triggered()
{
    if (!tbcSource.getIsSourceLoaded()) {
        statusBar()->showMessage(tr("No source loaded to copy."), 3000);
        return;
    }

    if (!isViewerTabActive()) {
        QWidget *focus = QApplication::focusWidget();
        if (focus && (qobject_cast<QLineEdit *>(focus)
                      || qobject_cast<QTextEdit *>(focus)
                      || qobject_cast<QPlainTextEdit *>(focus)
                      || qobject_cast<QAbstractSpinBox *>(focus))) {
            QMetaObject::invokeMethod(focus, "copy");
            return;
        }

        statusBar()->showMessage(tr("Switch to the Viewer tab to copy the current display."), 3000);
        return;
    }

    copyCurrentFrameToClipboard();
}

void MainWindow::on_actionSave_all_modes_as_PNGs_triggered()
{
    if (!tbcSource.getIsSourceLoaded()) {
        QMessageBox::warning(this, tr("Warning"), tr("No source file loaded."));
        return;
    }
    if (tbcSource.getIsMetadataOnly()) {
        QMessageBox::warning(this, tr("Warning"), tr("Metadata-only mode cannot export PNG images."));
        return;
    }

    QMessageBox exportModeDialog(this);
    exportModeDialog.setIcon(QMessageBox::Question);
    exportModeDialog.setWindowTitle(tr("Save all mode views as PNGs"));
    exportModeDialog.setText(tr("Choose what to export:"));
    QAbstractButton *everythingButton = exportModeDialog.addButton(tr("Everything"), QMessageBox::AcceptRole);
    QAbstractButton *imageButton = exportModeDialog.addButton(tr("Image"), QMessageBox::ActionRole);
    QAbstractButton *scopesButton = exportModeDialog.addButton(tr("Scopes"), QMessageBox::ActionRole);
    QAbstractButton *snrGraphsButton = exportModeDialog.addButton(tr("SNR Graphs"), QMessageBox::ActionRole);
    if (QPushButton *defaultButton = qobject_cast<QPushButton *>(everythingButton)) {
        exportModeDialog.setDefaultButton(defaultButton);
    }
    QShortcut escapeShortcut(QKeySequence(Qt::Key_Escape), &exportModeDialog);
    connect(&escapeShortcut, &QShortcut::activated, &exportModeDialog, &QDialog::reject);
    exportModeDialog.exec();

    const QAbstractButton *selectedExportModeButton = exportModeDialog.clickedButton();
    if (!selectedExportModeButton) {
        return;
    }

    const bool exportImages =
        (selectedExportModeButton == everythingButton || selectedExportModeButton == imageButton);
    const bool exportScopes =
        (selectedExportModeButton == everythingButton || selectedExportModeButton == scopesButton);
    const bool exportSnrGraphs =
        (selectedExportModeButton == everythingButton || selectedExportModeButton == snrGraphsButton);

    QString exportSelectionToken = QStringLiteral("all_modes");
    if (selectedExportModeButton == imageButton) {
        exportSelectionToken = QStringLiteral("image");
    } else if (selectedExportModeButton == scopesButton) {
        exportSelectionToken = QStringLiteral("scopes");
    } else if (selectedExportModeButton == snrGraphsButton) {
        exportSelectionToken = QStringLiteral("snr_graphs");
    }

    const QString outputRoot = outputRootDirectoryForCurrentSource();
    if (outputRoot.isEmpty()) {
        QMessageBox::warning(this, tr("Warning"), tr("Could not determine output directory."));
        return;
    }

    const QString baseName = outputBaseNameForCurrentSource();
    const QString frameToken = tbcSource.getFieldViewEnabled()
                                   ? QStringLiteral("field_%1").arg(currentFieldNumber, 6, 10, QChar('0'))
                                   : QStringLiteral("frame_%1").arg(currentFrameNumber, 6, 10, QChar('0'));
    const QString outputFolderName = QStringLiteral("%1_%2_%3_pngs")
                                         .arg(baseName, frameToken, exportSelectionToken);
    const QString outputFolderPath = QDir(outputRoot).filePath(outputFolderName);
    if (!QDir().mkpath(outputFolderPath)) {
        QMessageBox::warning(this, tr("Warning"),
                             tr("Could not create output folder:\n%1").arg(outputFolderPath));
        return;
    }

    const bool originalPlaybackRunning = playbackRunning;
    const bool originalDisplayAspectRatio = displayAspectRatio;
    const bool originalMouseMode = (ui && ui->mouseModePushButton) ? ui->mouseModePushButton->isChecked() : false;
    const TbcSource::SourceMode originalSourceMode = tbcSource.getSourceMode();
    const bool originalChromaEnabled = tbcSource.getChromaDecoder();
    const TbcSource::ChromaDecodeMode originalChromaMode = tbcSource.getChromaDecodeMode();
    const TbcSource::ViewMode originalViewMode = tbcSource.getViewMode();
    const bool originalStretchField = tbcSource.getStretchField();
    const qint32 originalFrameNumber = currentFrameNumber;
    const qint32 originalFieldNumber = currentFieldNumber;

    auto restoreState = [&]() {
        tbcSource.setSourceMode(originalSourceMode);
        tbcSource.setChromaDecodeMode(originalChromaMode);
        tbcSource.setChromaDecoder(originalChromaEnabled);
        tbcSource.setViewMode(originalViewMode);
        tbcSource.setStretchField(originalStretchField);
        displayAspectRatio = originalDisplayAspectRatio;
        if (originalViewMode == TbcSource::ViewMode::FIELD_VIEW) {
            setCurrentField(originalFieldNumber);
        } else {
            setCurrentFrame(originalFrameNumber);
        }
        if (ui && ui->mouseModePushButton) {
            ui->mouseModePushButton->setChecked(originalMouseMode);
        }
        updateVideoPushButton();
        updateSourcesPushButton();
        updateAspectPushButton();
        setViewValues();
        showImage();
        setPlaybackRunning(originalPlaybackRunning);
    };

    setPlaybackRunning(false);
    if (ui && ui->mainTabWidget && ui->viewerTab) {
        ui->mainTabWidget->setCurrentWidget(ui->viewerTab);
    }
    if (ui && ui->mouseModePushButton && ui->mouseModePushButton->isChecked()) {
        ui->mouseModePushButton->setChecked(false);
    }

    struct VideoModeSnapshot {
        QString token;
        bool chromaEnabled;
        TbcSource::ChromaDecodeMode chromaMode;
    };
    const QVector<VideoModeSnapshot> videoModes = {
        {QStringLiteral("source"), false, originalChromaMode},
        {QStringLiteral("hybrid"), true, TbcSource::HYBRID_CHROMA_MODE},
        {QStringLiteral("chroma"), true, TbcSource::FULL_FRAME_CHROMA_MODE}
    };

    struct SourceModeSnapshot {
        QString token;
        TbcSource::SourceMode sourceMode;
    };
    QVector<SourceModeSnapshot> sourceModes;
    if (originalSourceMode == TbcSource::ONE_SOURCE) {
        sourceModes.append({QStringLiteral("single"), TbcSource::ONE_SOURCE});
    } else {
        sourceModes.append({QStringLiteral("y"), TbcSource::LUMA_SOURCE});
        sourceModes.append({QStringLiteral("c"), TbcSource::CHROMA_SOURCE});
        sourceModes.append({QStringLiteral("yc"), TbcSource::BOTH_SOURCES});
    }

    struct ViewModeSnapshot {
        QString token;
        TbcSource::ViewMode viewMode;
        bool stretchField;
    };
    QVector<ViewModeSnapshot> viewModes;
    if (exportImages) {
        viewModes.append({QStringLiteral("frame"), TbcSource::ViewMode::FRAME_VIEW, false});
        viewModes.append({QStringLiteral("split"), TbcSource::ViewMode::SPLIT_VIEW, false});
        viewModes.append({QStringLiteral("field_1x"), TbcSource::ViewMode::FIELD_VIEW, false});
        viewModes.append({QStringLiteral("field_2x"), TbcSource::ViewMode::FIELD_VIEW, true});
    }
    if (exportScopes) {
        viewModes.append({QStringLiteral("rgb_scope"), TbcSource::ViewMode::RGB_SCOPE_VIEW, false});
    }

    int savedCount = 0;
    QStringList failedFiles;
    const QString systemToken = (tbcSource.getSystem() == PAL)
                                    ? QStringLiteral("pal")
                                    : ((tbcSource.getSystem() == PAL_M)
                                           ? QStringLiteral("palm")
                                           : QStringLiteral("ntsc"));

    if (!viewModes.isEmpty()) {
        for (const SourceModeSnapshot &sourceModeSnapshot : sourceModes) {
            tbcSource.setSourceMode(sourceModeSnapshot.sourceMode);

            for (const VideoModeSnapshot &videoModeSnapshot : videoModes) {
                if (videoModeSnapshot.chromaEnabled) {
                    tbcSource.setChromaDecodeMode(videoModeSnapshot.chromaMode);
                    tbcSource.setChromaDecoder(true);
                } else {
                    tbcSource.setChromaDecoder(false);
                }

                for (const ViewModeSnapshot &viewModeSnapshot : viewModes) {
                    tbcSource.setViewMode(viewModeSnapshot.viewMode);
                    tbcSource.setStretchField(viewModeSnapshot.stretchField);

                    if (viewModeSnapshot.viewMode == TbcSource::ViewMode::FIELD_VIEW) {
                        setCurrentField(originalFieldNumber);
                    } else {
                        setCurrentFrame(originalFrameNumber);
                    }

                    updateVideoPushButton();
                    updateSourcesPushButton();
                    updateAspectPushButton();
                    setViewValues();
                    showImage();

                    const QImage imageToSave = renderedCurrentImageForExport();
                    const QString outputStem = QStringLiteral("%1_%2_%3_%4_%5")
                                                   .arg(baseName,
                                                        systemToken,
                                                        videoModeSnapshot.token,
                                                        sourceModeSnapshot.token,
                                                        viewModeSnapshot.token);
                    const QString outputFilePath = QDir(outputFolderPath)
                                                       .filePath(sanitizedFileToken(outputStem) + QStringLiteral(".png"));

                    if (imageToSave.isNull() || !imageToSave.save(outputFilePath)) {
                        failedFiles << outputFilePath;
                    } else {
                        savedCount++;
                    }
                }
            }
        }
    }

    restoreState();

    auto saveDialogSnapshot = [&](QDialog *dialog, const QString &token) {
        if (!dialog) {
            return;
        }

        dialog->update();
        QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
        QPixmap snapshot = dialog->grab();
        if (snapshot.isNull()) {
            const bool wasVisible = dialog->isVisible();
            if (!wasVisible) {
                dialog->show();
                QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
            }
            snapshot = dialog->grab();
            if (!wasVisible) {
                dialog->hide();
            }
        }

        const QString outputStem = QStringLiteral("%1_%2_%3")
                                       .arg(baseName, systemToken, token);
        const QString outputFilePath = QDir(outputFolderPath)
                                           .filePath(sanitizedFileToken(outputStem) + QStringLiteral(".png"));
        if (snapshot.isNull() || !snapshot.save(outputFilePath)) {
            failedFiles << outputFilePath;
        } else {
            savedCount++;
        }
    };

    if (exportScopes || exportSnrGraphs) {
        const bool resumePlayback = playbackRunning;
        setPlaybackRunning(false);

        if (exportScopes) {
            tbcSource.setViewMode(TbcSource::ViewMode::FRAME_VIEW);
            tbcSource.setStretchField(false);
            setCurrentFrame(originalFrameNumber);
            setViewValues();
            showImage();
            const bool originalOscilloscopeAdvancedTab = oscilloscopeDialog->isAdvancedTabSelected();
            QCheckBox *lineYcCheckBox = oscilloscopeDialog->findChild<QCheckBox *>(QStringLiteral("YCcheckBox"));
            QCheckBox *lineYCheckBox = oscilloscopeDialog->findChild<QCheckBox *>(QStringLiteral("YcheckBox"));
            QCheckBox *lineCCheckBox = oscilloscopeDialog->findChild<QCheckBox *>(QStringLiteral("CcheckBox"));
            QCheckBox *lineDropoutsCheckBox = oscilloscopeDialog->findChild<QCheckBox *>(QStringLiteral("dropoutsCheckBox"));

            const bool originalLineYcChecked = lineYcCheckBox ? lineYcCheckBox->isChecked() : false;
            const bool originalLineYChecked = lineYCheckBox ? lineYCheckBox->isChecked() : false;
            const bool originalLineCChecked = lineCCheckBox ? lineCCheckBox->isChecked() : false;
            const bool originalLineDropoutsChecked =
                lineDropoutsCheckBox ? lineDropoutsCheckBox->isChecked() : false;

            const auto setButtonCheckedWithoutSignals = [](QAbstractButton *button, bool checked) {
                if (!button) {
                    return;
                }
                const QSignalBlocker blocker(button);
                button->setChecked(checked);
            };

            struct LineScopePreset {
                QString token;
                bool advanced;
                bool yc;
                bool y;
                bool c;
                bool dropouts;
            };
            const QVector<LineScopePreset> lineScopePresets = {
                {QStringLiteral("line_scope_basic_yc"), false, true, false, false, true},
                {QStringLiteral("line_scope_basic_y"), false, false, true, false, true},
                {QStringLiteral("line_scope_basic_c"), false, false, false, true, true},
                {QStringLiteral("line_scope_basic_yc_y_c"), false, true, true, true, true},
                {QStringLiteral("line_scope_basic_yc_no_dropouts"), false, true, false, false, false},
                {QStringLiteral("line_scope_advanced_yc"), true, true, false, false, true},
                {QStringLiteral("line_scope_advanced_y"), true, false, true, false, true},
                {QStringLiteral("line_scope_advanced_c"), true, false, false, true, true},
                {QStringLiteral("line_scope_advanced_yc_y_c"), true, true, true, true, true}
            };
            for (const LineScopePreset &lineScopePreset : lineScopePresets) {
                setButtonCheckedWithoutSignals(lineYcCheckBox, lineScopePreset.yc);
                setButtonCheckedWithoutSignals(lineYCheckBox, lineScopePreset.y);
                setButtonCheckedWithoutSignals(lineCCheckBox, lineScopePreset.c);
                setButtonCheckedWithoutSignals(lineDropoutsCheckBox, lineScopePreset.dropouts);
                if (lineYcCheckBox && lineYCheckBox && lineCCheckBox
                    && !lineYcCheckBox->isChecked() && !lineYCheckBox->isChecked() && !lineCCheckBox->isChecked()) {
                    setButtonCheckedWithoutSignals(lineYcCheckBox, true);
                }
                if (!oscilloscopeDialog->setAdvancedTabSelected(lineScopePreset.advanced)) {
                    continue;
                }
                updateOscilloscopeDialogue();
                saveDialogSnapshot(oscilloscopeDialog, lineScopePreset.token);
            }

            setButtonCheckedWithoutSignals(lineYcCheckBox, originalLineYcChecked);
            setButtonCheckedWithoutSignals(lineYCheckBox, originalLineYChecked);
            setButtonCheckedWithoutSignals(lineCCheckBox, originalLineCChecked);
            setButtonCheckedWithoutSignals(lineDropoutsCheckBox, originalLineDropoutsChecked);
            oscilloscopeDialog->setAdvancedTabSelected(originalOscilloscopeAdvancedTab);
            updateOscilloscopeDialogue();

            const bool originalVectorscopeAdvancedMode = vectorscopeDialog->isAdvancedRenderModeSelected();
            const bool originalVectorscopeFullAreaMode = vectorscopeDialog->isFullAreaModeSelected();
            const bool originalVectorscopeCustomAreaMode = vectorscopeDialog->isCustomAreaModeSelected();
            const QRect originalVectorscopeCustomAreaRect = vectorscopeDialog->customAreaRect();
            QRadioButton *fieldSelectAllRadioButton =
                vectorscopeDialog->findChild<QRadioButton *>(QStringLiteral("fieldSelectAllRadioButton"));
            QRadioButton *fieldSelectFirstRadioButton =
                vectorscopeDialog->findChild<QRadioButton *>(QStringLiteral("fieldSelectFirstRadioButton"));
            QRadioButton *fieldSelectSecondRadioButton =
                vectorscopeDialog->findChild<QRadioButton *>(QStringLiteral("fieldSelectSecondRadioButton"));
            QRadioButton *graticuleNoneRadioButton =
                vectorscopeDialog->findChild<QRadioButton *>(QStringLiteral("graticuleNoneRadioButton"));
            QRadioButton *graticule75RadioButton =
                vectorscopeDialog->findChild<QRadioButton *>(QStringLiteral("graticule75RadioButton"));
            QRadioButton *graticule100RadioButton =
                vectorscopeDialog->findChild<QRadioButton *>(QStringLiteral("graticule100RadioButton"));
            QCheckBox *vectorscopeDefocusCheckBox =
                vectorscopeDialog->findChild<QCheckBox *>(QStringLiteral("defocusCheckBox"));
            QCheckBox *vectorscopeBlendColorsCheckBox =
                vectorscopeDialog->findChild<QCheckBox *>(QStringLiteral("blendColorCheckBox"));

            const bool originalFieldAllChecked =
                fieldSelectAllRadioButton ? fieldSelectAllRadioButton->isChecked() : false;
            const bool originalFieldFirstChecked =
                fieldSelectFirstRadioButton ? fieldSelectFirstRadioButton->isChecked() : false;
            const bool originalFieldSecondChecked =
                fieldSelectSecondRadioButton ? fieldSelectSecondRadioButton->isChecked() : false;
            const bool originalGraticuleNoneChecked =
                graticuleNoneRadioButton ? graticuleNoneRadioButton->isChecked() : false;
            const bool originalGraticule75Checked =
                graticule75RadioButton ? graticule75RadioButton->isChecked() : false;
            const bool originalGraticule100Checked =
                graticule100RadioButton ? graticule100RadioButton->isChecked() : false;
            const bool originalVectorscopeDefocusChecked =
                vectorscopeDefocusCheckBox ? vectorscopeDefocusCheckBox->isChecked() : false;
            const bool originalVectorscopeBlendChecked =
                vectorscopeBlendColorsCheckBox ? vectorscopeBlendColorsCheckBox->isChecked() : false;

            struct RadioModePreset {
                QString token;
                QRadioButton *radioButton;
            };
            QVector<RadioModePreset> fieldPresets;
            if (fieldSelectAllRadioButton) {
                fieldPresets.append({QStringLiteral("all"), fieldSelectAllRadioButton});
            }
            if (fieldSelectFirstRadioButton) {
                fieldPresets.append({QStringLiteral("field_1"), fieldSelectFirstRadioButton});
            }
            if (fieldSelectSecondRadioButton) {
                fieldPresets.append({QStringLiteral("field_2"), fieldSelectSecondRadioButton});
            }
            if (fieldPresets.isEmpty()) {
                fieldPresets.append({QStringLiteral("current"), nullptr});
            }

            QVector<RadioModePreset> graticulePresets;
            if (graticuleNoneRadioButton) {
                graticulePresets.append({QStringLiteral("none"), graticuleNoneRadioButton});
            }
            if (graticule75RadioButton) {
                graticulePresets.append({QStringLiteral("75"), graticule75RadioButton});
            }
            if (graticule100RadioButton) {
                graticulePresets.append({QStringLiteral("100"), graticule100RadioButton});
            }
            if (graticulePresets.isEmpty()) {
                graticulePresets.append({QStringLiteral("current"), nullptr});
            }

            struct RenderModePreset {
                QString token;
                bool advanced;
            };
            QVector<RenderModePreset> renderModePresets;
            renderModePresets.append({QStringLiteral("point_plot"), false});
            const bool supportsDensityRenderMode = vectorscopeDialog->setAdvancedRenderModeSelected(true);
            vectorscopeDialog->setAdvancedRenderModeSelected(false);
            if (supportsDensityRenderMode) {
                renderModePresets.append({QStringLiteral("density_plot"), true});
            }

            setButtonCheckedWithoutSignals(vectorscopeDefocusCheckBox, false);
            setButtonCheckedWithoutSignals(vectorscopeBlendColorsCheckBox, false);
            vectorscopeDialog->setFullAreaModeSelected(false);
            vectorscopeDialog->setCustomAreaModeSelected(false);
            for (const RadioModePreset &fieldPreset : fieldPresets) {
                setButtonCheckedWithoutSignals(fieldPreset.radioButton, true);
                for (const RadioModePreset &graticulePreset : graticulePresets) {
                    setButtonCheckedWithoutSignals(graticulePreset.radioButton, true);
                    for (const RenderModePreset &renderModePreset : renderModePresets) {
                        if (renderModePreset.advanced
                            && !vectorscopeDialog->setAdvancedRenderModeSelected(true)) {
                            continue;
                        }
                        if (!renderModePreset.advanced) {
                            vectorscopeDialog->setAdvancedRenderModeSelected(false);
                        }
                        updateVectorscopeDialogue();
                        saveDialogSnapshot(vectorscopeDialog,
                                           QStringLiteral("vectorscope_field_%1_graticule_%2_%3")
                                               .arg(fieldPreset.token,
                                                    graticulePreset.token,
                                                    renderModePreset.token));
                    }
                }
            }

            setButtonCheckedWithoutSignals(fieldSelectAllRadioButton, true);
            setButtonCheckedWithoutSignals(graticule75RadioButton, true);
            vectorscopeDialog->setAdvancedRenderModeSelected(false);
            vectorscopeDialog->setFullAreaModeSelected(false);
            vectorscopeDialog->setCustomAreaModeSelected(false);

            if (vectorscopeDialog->setFullAreaModeSelected(true)) {
                updateVectorscopeDialogue();
                saveDialogSnapshot(vectorscopeDialog, QStringLiteral("vectorscope_scope_area_full_frame"));
                vectorscopeDialog->setFullAreaModeSelected(false);
            }
            if (originalVectorscopeCustomAreaMode) {
                if (originalVectorscopeCustomAreaRect.isValid()) {
                    vectorscopeDialog->setCustomAreaRect(originalVectorscopeCustomAreaRect);
                }
                vectorscopeDialog->setCustomAreaModeSelected(true);
                updateVectorscopeDialogue();
                saveDialogSnapshot(vectorscopeDialog, QStringLiteral("vectorscope_scope_area_custom"));
                vectorscopeDialog->setCustomAreaModeSelected(false);
            }
            if (vectorscopeDefocusCheckBox) {
                setButtonCheckedWithoutSignals(vectorscopeDefocusCheckBox, true);
                updateVectorscopeDialogue();
                saveDialogSnapshot(vectorscopeDialog, QStringLiteral("vectorscope_defocus"));
                setButtonCheckedWithoutSignals(vectorscopeDefocusCheckBox, false);
            }
            if (vectorscopeBlendColorsCheckBox) {
                setButtonCheckedWithoutSignals(vectorscopeBlendColorsCheckBox, true);
                updateVectorscopeDialogue();
                saveDialogSnapshot(vectorscopeDialog, QStringLiteral("vectorscope_blend_colors"));
                setButtonCheckedWithoutSignals(vectorscopeBlendColorsCheckBox, false);
            }

            setButtonCheckedWithoutSignals(fieldSelectAllRadioButton, originalFieldAllChecked);
            setButtonCheckedWithoutSignals(fieldSelectFirstRadioButton, originalFieldFirstChecked);
            setButtonCheckedWithoutSignals(fieldSelectSecondRadioButton, originalFieldSecondChecked);
            setButtonCheckedWithoutSignals(graticuleNoneRadioButton, originalGraticuleNoneChecked);
            setButtonCheckedWithoutSignals(graticule75RadioButton, originalGraticule75Checked);
            setButtonCheckedWithoutSignals(graticule100RadioButton, originalGraticule100Checked);
            setButtonCheckedWithoutSignals(vectorscopeDefocusCheckBox, originalVectorscopeDefocusChecked);
            setButtonCheckedWithoutSignals(vectorscopeBlendColorsCheckBox, originalVectorscopeBlendChecked);
            vectorscopeDialog->setAdvancedRenderModeSelected(originalVectorscopeAdvancedMode);
            if (originalVectorscopeCustomAreaRect.isValid()) {
                vectorscopeDialog->setCustomAreaRect(originalVectorscopeCustomAreaRect);
            }
            if (originalVectorscopeCustomAreaMode) {
                vectorscopeDialog->setCustomAreaModeSelected(true);
            } else {
                vectorscopeDialog->setFullAreaModeSelected(originalVectorscopeFullAreaMode);
            }
            updateVectorscopeDialogue();

            updateRgbScopeDialogue(true);
            updateFieldTimingDialogue();
            saveDialogSnapshot(rgbScopeDialog, QStringLiteral("rgb_scope_window"));
            saveDialogSnapshot(fieldTimingDialog, QStringLiteral("field_timing_scope"));
        }

        if (exportSnrGraphs) {
            blackSnrAnalysisDialog->updateFrameMarker(currentFrameNumber);
            whiteSnrAnalysisDialog->updateFrameMarker(currentFrameNumber);
            QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);

            saveDialogSnapshot(blackSnrAnalysisDialog, QStringLiteral("black_snr_graph"));
            saveDialogSnapshot(whiteSnrAnalysisDialog, QStringLiteral("white_snr_graph"));
        }

        setPlaybackRunning(resumePlayback);
    }
    configuration.setPngDirectory(outputFolderPath);
    configuration.writeConfiguration();

    if (failedFiles.isEmpty()) {
        QMessageBox::information(this, tr("Save complete"),
                                 tr("Saved %1 PNG files to:\n%2")
                                     .arg(savedCount)
                                     .arg(outputFolderPath));
        return;
    }

    QMessageBox::warning(this, tr("Save partially complete"),
                         tr("Saved %1 PNG files to:\n%2\n\nFailed to save %3 file(s).")
                             .arg(savedCount)
                             .arg(outputFolderPath)
                             .arg(failedFiles.size()));
}

// Zoom in menu option
void MainWindow::on_actionZoom_In_triggered()
{
    on_zoomInPushButton_clicked();
	MainWindow::resize_on_aspect();
}

// Zoom out menu option
void MainWindow::on_actionZoom_Out_triggered()
{
    on_zoomOutPushButton_clicked();
	MainWindow::resize_on_aspect();
}

// Original size 1:1 zoom menu option
void MainWindow::on_actionZoom_1x_triggered()
{
    on_originalSizePushButton_clicked();
	MainWindow::resize_on_aspect();
}

// 2:1 zoom menu option
void MainWindow::on_actionZoom_2x_triggered()
{
    scaleFactor = 2.0;
    updateImageViewer();
	MainWindow::resize_on_aspect();
}

// 3:1 zoom menu option
void MainWindow::on_actionZoom_3x_triggered()
{
    scaleFactor = 3.0;
    updateImageViewer();
	MainWindow::resize_on_aspect();
}

// Show closed captions
void MainWindow::on_actionClosed_Captions_triggered()
{
    closedCaptionDialog->show();
}

// Show video parameters dialogue
void MainWindow::on_actionVideo_parameters_triggered()
{
    videoParametersDialog->show();
}

// Show chroma decoder configuration
void MainWindow::on_actionChroma_decoder_configuration_triggered()
{
    chromaDecoderConfigDialog->show();
}

// Toggle chroma during seek option
void MainWindow::on_actionToggleChromaDuringSeek_triggered()
{
    bool enabled = ui->actionToggleChromaDuringSeek->isChecked();
    configuration.setToggleChromaDuringSeek(enabled);
    configuration.writeConfiguration();

}

// Media control frame signal handlers --------------------------------------------------------------------------------

// Previous field/frame button has been clicked
void MainWindow::on_previousPushButton_clicked()
{
    setPlaybackRunning(false);
    // Enter chroma seek mode if appropriate
    enterChromaSeekMode(ui->previousPushButton);
    
    // Normal frame navigation (works the same in both Source and Chroma modes)
    qint32 currentNumber;
    if (tbcSource.getFieldViewEnabled()) {
        setCurrentField(currentFieldNumber - 1);
        currentNumber = currentFieldNumber;
    } else {
        setCurrentFrame(currentFrameNumber - 1);
        currentNumber = currentFrameNumber;
    }

    updatePositionEditorValue(currentNumber);
    ui->posHorizontalSlider->setValue(currentNumber);
}

// Next field/frame button has been clicked
void MainWindow::on_nextPushButton_clicked()
{
    setPlaybackRunning(false);
    // Enter chroma seek mode if appropriate
    enterChromaSeekMode(ui->nextPushButton);
    
    // Normal frame navigation (works the same in both Source and Chroma modes)
    qint32 currentNumber;
    if (tbcSource.getFieldViewEnabled()) {
        setCurrentField(currentFieldNumber + 1);
        currentNumber = currentFieldNumber;
    } else {
        setCurrentFrame(currentFrameNumber + 1);
        currentNumber = currentFrameNumber;
    }

    updatePositionEditorValue(currentNumber);
    ui->posHorizontalSlider->setValue(currentNumber);
}

// Previous button pressed (for chroma toggle during seek)
void MainWindow::on_previousPushButton_pressed()
{
    if (configuration.getToggleChromaDuringSeek() && tbcSource.getChromaDecoder()) {
        // Start timer to detect if this is a hold (not just a click)
        seekTimer->start();
    }
}

// Previous button released (for chroma toggle during seek)
void MainWindow::on_previousPushButton_released()
{
    // Stop the hold detection timer if still running
    seekTimer->stop();
    
    exitChromaSeekMode(ui->previousPushButton);
}

// Next button pressed (for chroma toggle during seek)
void MainWindow::on_nextPushButton_pressed()
{
    if (configuration.getToggleChromaDuringSeek() && tbcSource.getChromaDecoder()) {
        // Start timer to detect if this is a hold (not just a click)
        seekTimer->start();
    }
}

// Next button released (for chroma toggle during seek)
void MainWindow::on_nextPushButton_released()
{
    // Stop the hold detection timer if still running
    seekTimer->stop();
    
    exitChromaSeekMode(ui->nextPushButton);
}

void MainWindow::on_endPushButton_pressed()
{
    if (!tbcSource.getIsSourceLoaded()) {
        return;
    }
    suppressEndButtonClick = false;
    chapterSkipHoldButton = ui->endPushButton;
    chapterSkipHoldDeltaFrames = 10;
    chapterSkipHoldTriggered = false;
    chapterSkipHoldDelayTimer->start();
}

void MainWindow::on_endPushButton_released()
{
    if (chapterSkipHoldButton == ui->endPushButton && chapterSkipHoldTriggered) {
        suppressEndButtonClick = true;
    }
    chapterSkipHoldDelayTimer->stop();
    chapterSkipHoldRepeatTimer->stop();
    chapterSkipHoldButton = nullptr;
    chapterSkipHoldDeltaFrames = 0;
    chapterSkipHoldTriggered = false;
}

void MainWindow::on_startPushButton_pressed()
{
    if (!tbcSource.getIsSourceLoaded()) {
        return;
    }
    suppressStartButtonClick = false;
    chapterSkipHoldButton = ui->startPushButton;
    chapterSkipHoldDeltaFrames = -10;
    chapterSkipHoldTriggered = false;
    chapterSkipHoldDelayTimer->start();
}

void MainWindow::on_startPushButton_released()
{
    if (chapterSkipHoldButton == ui->startPushButton && chapterSkipHoldTriggered) {
        suppressStartButtonClick = true;
    }
    chapterSkipHoldDelayTimer->stop();
    chapterSkipHoldRepeatTimer->stop();
    chapterSkipHoldButton = nullptr;
    chapterSkipHoldDeltaFrames = 0;
    chapterSkipHoldTriggered = false;
}

// Skip to the next chapter (note: this button was repurposed from 'end frame')
void MainWindow::on_endPushButton_clicked()
{
    if (suppressEndButtonClick) {
        suppressEndButtonClick = false;
        return;
    }
    setPlaybackRunning(false);
    const qint32 totalFrames = qMax<qint32>(1, tbcSource.getNumberOfFrames());
    qint32 targetFrame = currentFrameNumber;
    if (tbcSource.hasChapterMap()) {
        targetFrame = tbcSource.startOfNextChapter(currentFrameNumber);
    } else {
        const QVector<UserNoteMarker> userChapterMarkers = userNoteMarkersFromVideoParameters(
            tbcSource.getVideoParameters(), totalFrames);
        if (!userChapterMarkers.isEmpty()) {
            targetFrame = nextUserMarkerChapterFrame(userChapterMarkers, currentFrameNumber, totalFrames);
        } else {
            targetFrame = qMin(totalFrames, currentFrameNumber + 10);
        }
    }
    setCurrentFrame(targetFrame);
    auto uiNumber = currentFrameNumber;

    if (tbcSource.getFieldViewEnabled()) {
        uiNumber = currentFieldNumber;
    }

    updatePositionEditorValue(uiNumber);
    ui->posHorizontalSlider->setValue(uiNumber);
}

// Skip to the start of chapter (note: this button was repurposed from 'start frame')
void MainWindow::on_startPushButton_clicked()
{
    if (suppressStartButtonClick) {
        suppressStartButtonClick = false;
        return;
    }
    setPlaybackRunning(false);
    qint32 targetFrame = currentFrameNumber;
    if (tbcSource.hasChapterMap()) {
        targetFrame = tbcSource.startOfChapter(currentFrameNumber);
    } else {
        const qint32 totalFrames = qMax<qint32>(1, tbcSource.getNumberOfFrames());
        const QVector<UserNoteMarker> userChapterMarkers = userNoteMarkersFromVideoParameters(
            tbcSource.getVideoParameters(), totalFrames);
        if (!userChapterMarkers.isEmpty()) {
            targetFrame = previousUserMarkerChapterFrame(userChapterMarkers, currentFrameNumber);
        } else {
            targetFrame = qMax<qint32>(1, currentFrameNumber - 10);
        }
    }
    setCurrentFrame(targetFrame);
    auto uiNumber = currentFrameNumber;

    if (tbcSource.getFieldViewEnabled()) {
        uiNumber = currentFieldNumber;
    }

    updatePositionEditorValue(uiNumber);
    ui->posHorizontalSlider->setValue(uiNumber);
}

// Field/Frame number spin box editing has finished
void MainWindow::on_posNumberSpinBox_editingFinished()
{
    setPlaybackRunning(false);
    if (!tbcSource.getIsSourceLoaded() || !tbcSource.getFieldViewEnabled()) {
        return;
    }
    qint32 currentNumber;
    qint32 totalNumber;
    currentNumber = currentFieldNumber;
    totalNumber = tbcSource.getNumberOfFields();

    if (ui->posNumberSpinBox->value() != currentNumber) {
        if (ui->posNumberSpinBox->value() < 1) ui->posNumberSpinBox->setValue(1);
        if (ui->posNumberSpinBox->value() > totalNumber) ui->posNumberSpinBox->setValue(totalNumber);
        setCurrentField(ui->posNumberSpinBox->value());
        currentNumber = currentFieldNumber;

        ui->posHorizontalSlider->setValue(currentNumber);
    }
}

void MainWindow::on_posTimecodeLineEdit_editingFinished()
{
    setPlaybackRunning(false);
    if (!tbcSource.getIsSourceLoaded() || tbcSource.getFieldViewEnabled() || !ui->posTimecodeLineEdit) {
        return;
    }

    bool ok = false;
    qint32 requestedFrame = timecodeToFrame(ui->posTimecodeLineEdit->text(), &ok);
    if (!ok) {
        ui->posTimecodeLineEdit->setText(frameToTimecode(currentFrameNumber));
        statusBar()->showMessage(tr("Invalid timecode. Use HH:MM:SS:FF."), 3000);
        return;
    }

    const qint32 clampedFrame = qBound<qint32>(1, requestedFrame, qMax(1, tbcSource.getNumberOfFrames()));
    if (clampedFrame != currentFrameNumber) {
        setCurrentFrame(clampedFrame);
        ui->posHorizontalSlider->setValue(clampedFrame);
    }
    ui->posTimecodeLineEdit->setText(frameToTimecode(currentFrameNumber));
}

void MainWindow::on_playPushButton_toggled(bool checked)
{
    setPlaybackRunning(checked);
}

// Field/frame slider value has changed
void MainWindow::on_posHorizontalSlider_valueChanged(int value)
{
    if (!tbcSource.getIsSourceLoaded()) {
        return;
    }
    setPlaybackRunning(false);

    if (tbcSource.getFieldViewEnabled()) {
        setCurrentField(value);
        updatePositionEditorValue(currentFieldNumber);
    } else {
        setCurrentFrame(value);
        updatePositionEditorValue(currentFrameNumber);
    }
}

// User started dragging the slider
void MainWindow::on_posHorizontalSlider_sliderPressed()
{
    setPlaybackRunning(false);
}

// User finished dragging the slider - now update
void MainWindow::on_posHorizontalSlider_sliderReleased()
{
    if (!tbcSource.getIsSourceLoaded()) {
        return;
    }
    const qint32 currentNumber = tbcSource.getFieldViewEnabled() ? currentFieldNumber : currentFrameNumber;
    updatePositionEditorValue(currentNumber);
}

void MainWindow::on_posHorizontalSlider_customContextMenuRequested(const QPoint &pos)
{
    if (!ui || !ui->posHorizontalSlider || !exportDialog || !tbcSource.getIsSourceLoaded()) {
        return;
    }
    const int clickedSliderValue = sliderValueForContextPoint(ui->posHorizontalSlider, pos);
    int framePoint = frameForSliderPosition(clickedSliderValue);
    const int totalFrames = qMax(1, tbcSource.getNumberOfFrames());
    framePoint = qBound(1, framePoint, totalFrames);
    const QString framePointTimecode = frameToTimecode(framePoint);
    // In/Out point actions operate on the currently displayed frame, not the click position.
    const qint32 currentPlaybackValue = tbcSource.getFieldViewEnabled() ? currentFieldNumber : currentFrameNumber;
    const qint32 currentFramePoint = qBound<qint32>(1, frameForSliderPosition(currentPlaybackValue), totalFrames);
    const QString currentFramePointTimecode = frameToTimecode(currentFramePoint);
    const TbcMetaData::VideoParameters currentVideoParameters = tbcSource.getVideoParameters();
    QVector<UserNoteMarker> noteMarkers = userNoteMarkersFromVideoParameters(currentVideoParameters, totalFrames);
    const qint32 noteIndexAtFrame = noteMarkerIndexForFrame(noteMarkers, framePoint);
    const bool noteAtFrameSet = noteIndexAtFrame >= 0;
    const qint32 nearestNoteIndex = nearestNoteMarkerIndexForFrame(noteMarkers, framePoint);
    const qint32 removableNoteIndex = noteAtFrameSet ? noteIndexAtFrame : nearestNoteIndex;
    const bool noteRemovalAvailable = removableNoteIndex >= 0;
    const qint32 removableFrame = noteRemovalAvailable ? noteMarkers.at(removableNoteIndex).frame : 0;
    const QString removableFrameTimecode = noteRemovalAvailable ? frameToTimecode(removableFrame) : QString();
    const QString noteCommentAtFrame = noteAtFrameSet ? noteMarkers.at(noteIndexAtFrame).comment : QString();

    QMenu sliderMenu(this);
    QAction *setInPointAction = sliderMenu.addAction(tr("Set In Point (%1 | %2)")
                                                        .arg(currentFramePoint)
                                                        .arg(currentFramePointTimecode));
    QAction *setOutPointAction = sliderMenu.addAction(tr("Set Out Point (%1 | %2)")
                                                         .arg(currentFramePoint)
                                                         .arg(currentFramePointTimecode));
    sliderMenu.addSeparator();
    QAction *setMarkerAction = sliderMenu.addAction(noteAtFrameSet
                                                        ? tr("Edit Marker (%1 | %2)...")
                                                              .arg(framePoint)
                                                              .arg(framePointTimecode)
                                                        : tr("Add Marker (%1 | %2)...")
                                                              .arg(framePoint)
                                                              .arg(framePointTimecode));
    QAction *removeNoteAction = sliderMenu.addAction(noteRemovalAvailable
                                                         ? (noteAtFrameSet
                                                                ? tr("Remove Marker (%1 | %2)")
                                                                      .arg(removableFrame)
                                                                      .arg(removableFrameTimecode)
                                                                : tr("Remove Nearest Marker (%1 | %2)")
                                                                      .arg(removableFrame)
                                                                      .arg(removableFrameTimecode))
                                                         : tr("Remove Marker"));
    removeNoteAction->setEnabled(noteRemovalAvailable);
    sliderMenu.addSeparator();
    QAction *openNotesViewerAction = sliderMenu.addAction(tr("Open Marker Viewer..."));
    QAction *selectedAction = sliderMenu.exec(ui->posHorizontalSlider->mapToGlobal(pos));
    if (!selectedAction) {
        return;
    }
    auto updateMarkerMetadata = [this](const QVector<UserNoteMarker> &updatedNoteMarkers) {
        TbcMetaData::VideoParameters videoParameters = tbcSource.getVideoParameters();
        if (!applyUserNoteMarkersToVideoParameters(videoParameters, updatedNoteMarkers)) {
            return false;
        }
        tbcSource.setVideoParameters(videoParameters);
        ui->actionSave_Metadata->setEnabled(true);
        updateTimelineMarkers();
        updateNotesViewerState();
        updateMetadataStatusPanel();
        return true;
    };
    if (selectedAction == setInPointAction) {
        setInPointAtCurrentFrame();
    } else if (selectedAction == setOutPointAction) {
        setOutPointAtCurrentFrame();
    } else if (selectedAction == setMarkerAction) {
        bool ok = false;
        const QString markerComment = QInputDialog::getText(this,
                                                            noteAtFrameSet ? tr("Edit Marker") : tr("Add Marker"),
                                                            tr("Marker name/comment for frame %1 (%2):")
                                                                .arg(framePoint)
                                                                .arg(framePointTimecode),
                                                            QLineEdit::Normal,
                                                            noteCommentAtFrame,
                                                            &ok);
        if (!ok) {
            return;
        }
        const QString trimmedComment = markerComment.trimmed();
        if (noteAtFrameSet) {
            noteMarkers[noteIndexAtFrame].comment = trimmedComment;
        } else {
            noteMarkers.append({framePoint, trimmedComment});
        }
        noteMarkers = normaliseUserNoteMarkers(noteMarkers, totalFrames);
        if (updateMarkerMetadata(noteMarkers)) {
            statusBar()->showMessage(tr("Marker saved at frame %1 (%2)")
                                         .arg(framePoint)
                                         .arg(framePointTimecode), 3000);
        }
    } else if (selectedAction == removeNoteAction && noteRemovalAvailable) {
        noteMarkers.removeAt(removableNoteIndex);
        noteMarkers = normaliseUserNoteMarkers(noteMarkers, totalFrames);
        if (updateMarkerMetadata(noteMarkers)) {
            statusBar()->showMessage(noteAtFrameSet
                                         ? tr("Marker removed from frame %1 (%2)")
                                               .arg(removableFrame)
                                               .arg(removableFrameTimecode)
                                         : tr("Nearest marker removed from frame %1 (%2)")
                                               .arg(removableFrame)
                                               .arg(removableFrameTimecode), 3000);
        }
    } else if (selectedAction == openNotesViewerAction) {
        updateNotesViewerState();
        notesViewerDialog->show();
        notesViewerDialog->raise();
        notesViewerDialog->activateWindow();
    }
}


// Source/Chroma select button clicked
void MainWindow::on_videoPushButton_clicked()
{
    const bool resumePlayback = playbackRunning;
    setPlaybackRunning(false);
    cancelInFlightAsyncFrameRender();
    if (!tbcSource.getChromaDecoder()) {
        tbcSource.setChromaDecodeMode(TbcSource::HYBRID_CHROMA_MODE);
        tbcSource.setChromaDecoder(true);
    } else if (tbcSource.getChromaDecodeMode() == TbcSource::FULL_FRAME_CHROMA_MODE) {
        tbcSource.setChromaDecoder(false);
    } else {
        tbcSource.setChromaDecodeMode(TbcSource::FULL_FRAME_CHROMA_MODE);
    }
    updateVideoPushButton();

    // Show the current image
    showImage();
    if (resumePlayback) {
        setPlaybackRunning(true);
    } else if (ui && ui->playPushButton) {
        ui->playPushButton->setToolTip(playbackStartToolTip());
    }
}

// Aspect ratio button clicked
void MainWindow::on_aspectPushButton_clicked()
{
    displayAspectRatio = !displayAspectRatio;

    // Update the button text
	updateAspectPushButton();

    // Update the image viewer (the scopes don't depend on this)
    updateImageViewer();

	//resize the windows to fit the new size
	resize_on_aspect();
}

void MainWindow::resize_on_aspect()
{
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    QPixmap pixmap = ui->imageViewerLabel->pixmap();
#elif QT_VERSION >= QT_VERSION_CHECK(5, 15, 0)
    QPixmap pixmap = ui->imageViewerLabel->pixmap(Qt::ReturnByValue);
#else
    QPixmap pixmap = *(ui->imageViewerLabel->pixmap());
#endif
    if (pixmap.isNull() || !ui || !ui->scrollArea) {
        return;
    }
    if (this->isFullScreen() || this->isMaximized() || !autoResize) {
        return;
    }

    const QSize viewportSize = ui->scrollArea->viewport()->size();
    if (viewportSize.width() < 1 || viewportSize.height() < 1) {
        return;
    }

    const int nonViewerWidth = this->width() - viewportSize.width();
    const int nonViewerHeight = this->height() - viewportSize.height();
    QSize targetWindowSize(pixmap.width() + nonViewerWidth,
                           pixmap.height() + nonViewerHeight);
    targetWindowSize = targetWindowSize.expandedTo(this->minimumSize());

    if (QScreen *windowScreen = this->screen()) {
        const QSize maxWindowSize = windowScreen->availableGeometry().size() - QSize(12, 12);
        targetWindowSize = targetWindowSize.boundedTo(maxWindowSize);
    }

    this->resize(targetWindowSize);
}

// Resize the frame to fit within the current window size
void MainWindow::resizeFrameToWindow()
{
	if (!tbcSource.getIsSourceLoaded()) {
		return;
	}
    if (!isViewerTabActive()) {
        return;
    }

	// Get the scroll area size (which contains the imageViewerLabel)
	QScrollArea* scrollArea = ui->scrollArea;
	QSize availableSize = scrollArea->viewport()->size() - QSize(2, 2);
	
	// Ensure we have a valid size - sometimes during resize events the size might be invalid
	if (availableSize.width() <= 0 || availableSize.height() <= 0) {
		// Use the central widget size as fallback, accounting for margins and toolbars
		QSize centralSize = ui->centralWidget->size();
		availableSize = QSize(centralSize.width() - 40, centralSize.height() - 200); // Account for UI elements
	}
	
	// Get the original image size
	QImage originalImage;
	const bool useAsyncRender = shouldRenderFrameAsync();
	if (asyncFrameRenderInProgress && !useAsyncRender) {
		cancelInFlightAsyncFrameRender();
		asyncFrameImage = QImage();
	}
	if (useAsyncRender) {
		originalImage = asyncFrameImage;
		if (originalImage.isNull()) {
			if (!asyncFrameRenderInProgress) {
				startAsyncFrameRender();
			}
			return;
		}
	} else {
		originalImage = tbcSource.getImage();
	}
	if (originalImage.isNull()) {
		return;
	}

	// Calculate scale factor to fit image within available space while maintaining aspect ratio
	qint32 adjustment = getAspectAdjustment();
	double scaleX = static_cast<double>(availableSize.width()) / static_cast<double>(originalImage.width() + adjustment);
	double scaleY = static_cast<double>(availableSize.height()) / static_cast<double>(originalImage.height());
	
	// Use the smaller scale factor to maintain aspect ratio
	double newScaleFactor = qMin(scaleX, scaleY);
	
	// Apply a minimum scale factor to prevent the image from becoming too small
	if (newScaleFactor < 0.1) {
		newScaleFactor = 0.1;
	}
	
	// Only update if there's a significant change to avoid constant tiny adjustments
	if (qAbs(newScaleFactor - scaleFactor) > 0.001) {
		scaleFactor = newScaleFactor;
		updateImageViewer();
	}
}

// Helper method to enter chroma seek mode
void MainWindow::enterChromaSeekMode(QPushButton* button)
{
    if (!chromaSeekMode && !seekTimer->isActive() && configuration.getToggleChromaDuringSeek() && tbcSource.getChromaDecoder() && button->isDown()) {
        chromaSeekMode = true;
        originalChromaState = true;
        tbcSource.setChromaDecoder(false);
        updateVideoPushButton();
    }
}

// Helper method to exit chroma seek mode
void MainWindow::exitChromaSeekMode(QPushButton* button)
{
    if (chromaSeekMode) {
        // Use a shorter timer to check if button is truly released (not just auto-repeat)
        QTimer::singleShot(5, this, [this, button]() {
            if (!button->isDown()) {
                // Exit seek mode and restore chroma
                chromaSeekMode = false;
                tbcSource.setChromaDecoder(originalChromaState);
                updateVideoPushButton();
                updateImage(); // Fast refresh without reloading - frame data already loaded
            }
        });
    }
}

// Show/hide dropouts button clicked
void MainWindow::on_dropoutsPushButton_clicked()
{
	int width = this->width();

    if (tbcSource.getHighlightDropouts()) {
        tbcSource.setHighlightDropouts(false);
		if (width >= 930)
		{
			ui->dropoutsPushButton->setText(tr("Dropouts Off"));
		}
		else
		{
			ui->dropoutsPushButton->setText(tr("Drop N"));
		}
    } else {
        tbcSource.setHighlightDropouts(true);
        if (width >= 930)
		{
			ui->dropoutsPushButton->setText(tr("Dropouts On"));
		}
		else
		{
			ui->dropoutsPushButton->setText(tr("Drop Y"));
		}
    }

    // Show the current image (why isn't this option passed?)
    showImage();
}

// Source selection button clicked
void MainWindow::on_sourcesPushButton_clicked()
{
    const bool resumePlayback = playbackRunning;
    setPlaybackRunning(false);
    cancelInFlightAsyncFrameRender();
    switch (tbcSource.getSourceMode()) {
    case TbcSource::ONE_SOURCE:
        // Do nothing - the button's disabled anyway
        break;
    case TbcSource::LUMA_SOURCE:
        tbcSource.setSourceMode(TbcSource::CHROMA_SOURCE);
        break;
    case TbcSource::CHROMA_SOURCE:
        tbcSource.setSourceMode(TbcSource::BOTH_SOURCES);
        break;
    case TbcSource::BOTH_SOURCES:
        tbcSource.setSourceMode(TbcSource::LUMA_SOURCE);
        break;
    }

    // Update the button
    updateSourcesPushButton();

    // Show the current image
    showImage();
    if (resumePlayback) {
        setPlaybackRunning(true);
    } else if (ui && ui->playPushButton) {
        ui->playPushButton->setToolTip(playbackStartToolTip());
    }
}

// Frame/Field view button clicked
void MainWindow::on_viewPushButton_clicked()
{
    switch (tbcSource.getViewMode()) {
        case TbcSource::ViewMode::FRAME_VIEW:
            tbcDebugStream() << "Changing to SPLIT_VIEW mode";

            // Set split mode
            tbcSource.setViewMode(TbcSource::ViewMode::SPLIT_VIEW);
            break;

        case TbcSource::ViewMode::SPLIT_VIEW:
            tbcDebugStream() << "Changing to FIELD_VIEW mode (1:1)";

            // Set field mode with 1:1 aspect
            tbcSource.setViewMode(TbcSource::ViewMode::FIELD_VIEW);
            tbcSource.setStretchField(false);
            break;

        case TbcSource::ViewMode::FIELD_VIEW:
            if (!tbcSource.getStretchField()) {
                tbcDebugStream() << "Changing to FIELD_VIEW mode (2:1)";

                // Set field mode with 2:1 aspect
                tbcSource.setStretchField(true);
            } else {
                tbcDebugStream() << "Changing to FRAME_VIEW mode";

                // Set frame mode
                tbcSource.setViewMode(TbcSource::ViewMode::FRAME_VIEW);
                tbcSource.setStretchField(false);
            }
            break;

        case TbcSource::ViewMode::RGB_SCOPE_VIEW:
            tbcDebugStream() << "Changing to FRAME_VIEW mode";

            // Set frame mode
            tbcSource.setViewMode(TbcSource::ViewMode::FRAME_VIEW);
            break;
    }

    setViewValues();
    updateGuiLoaded();

    // Show the current image
    showImage();
}

// Normal/Reverse field order button clicked
void MainWindow::on_fieldOrderPushButton_clicked()
{
	int width = this->width();

    if (tbcSource.getFieldOrder()) {
        tbcSource.setFieldOrder(false);

        // If the TBC field order is changed, the number of available frames can change, so we need to update the GUI
        resetGui();
        updateGuiLoaded();
        if (width > 1000)
			ui->fieldOrderPushButton->setText(tr("Normal Field-order"));
		else if (width >= 930)
			ui->fieldOrderPushButton->setText(tr("Normal order"));
		else
			ui->fieldOrderPushButton->setText(tr("Normal"));
    } else {
        tbcSource.setFieldOrder(true);

        // If the TBC field order is changed, the number of available frames can change, so we need to update the GUI
        resetGui();
        updateGuiLoaded();
        if (width > 1000)
			ui->fieldOrderPushButton->setText(tr("Reverse Field-order"));
		else if (width >= 930)
			ui->fieldOrderPushButton->setText(tr("Reverse order"));
		else
			ui->fieldOrderPushButton->setText(tr("Reverse"));
    }

    // Show the current image
    showImage();
}

void MainWindow::on_toggleAutoResize_toggled(bool checked)
{
	autoResize = checked;
}

void MainWindow::on_actionResizeFrameWithWindow_toggled(bool checked)
{
	resizeFrameWithWindow = checked;
	
	// Save the setting to configuration
	configuration.setResizeFrameWithWindow(checked);
	configuration.writeConfiguration();
	
	// If resizeFrameWithWindow is now enabled, resize frame to fit current window
	if (checked && tbcSource.getIsSourceLoaded()) {
		resizeTimer->start();
	}
}

// Zoom in
void MainWindow::on_zoomInPushButton_clicked()
{
    constexpr double factor = 1.1;
    if (((scaleFactor * factor) > 0.333) && ((scaleFactor * factor) < 3.0)) {
        scaleFactor *= factor;
    }

    updateImageViewer();
    resize_on_aspect();
}

// Zoom out
void MainWindow::on_zoomOutPushButton_clicked()
{
    constexpr double factor = 0.9;
    if (((scaleFactor * factor) > 0.333) && ((scaleFactor * factor) < 3.0)) {
        scaleFactor *= factor;
    }

    updateImageViewer();
    resize_on_aspect();
}

// Original size 1:1 zoom
void MainWindow::on_originalSizePushButton_clicked()
{
    scaleFactor = 1.0;
    updateImageViewer();
    resize_on_aspect();
}



// Mouse mode button clicked
void MainWindow::on_mouseModePushButton_clicked()
{
    if (ui->mouseModePushButton->isChecked()) {
        exportBoundaryDragHandle = ExportBoundaryHandle::None;
        exportBoundarySelectedHandle = ExportBoundaryHandle::None;
        if (vectorscopeSelectionPushButton && vectorscopeSelectionPushButton->isChecked()) {
            vectorscopeSelectionPushButton->setChecked(false);
        }
        // Show the oscilloscope view if currently hidden
        if (!oscilloscopeDialog->isVisible()) {
            oscilloscopeDialog->show();
        }
        updateOscilloscopeDialogue();
    }

    // Update the image viewer to display/hide the indicator line
    updateImageViewer();
}

void MainWindow::on_vectorscopeSelectionPushButton_toggled(bool checked)
{
    vectorscopeSelectionDragging = false;
    if (checked) {
        exportBoundaryDragHandle = ExportBoundaryHandle::None;
        exportBoundarySelectedHandle = ExportBoundaryHandle::None;
    }
    if (checked && ui->mouseModePushButton && ui->mouseModePushButton->isChecked()) {
        ui->mouseModePushButton->setChecked(false);
    }
    if (vectorscopeDialog) {
        vectorscopeDialog->setCustomAreaModeSelected(checked);
    }
    if (tbcSource.getIsSourceLoaded()) {
        updateVectorscopeDialogue();
    }

    updateImageViewer();
}

// Miscellaneous handler methods --------------------------------------------------------------------------------------

// Handler called when another class changes the currently selected scan line
void MainWindow::scopeCoordsChangedSignalHandler(qint32 xCoord, qint32 yCoord)
{
    tbcDebugStream() << "MainWindow::scanLineChangedSignalHandler(): Called with xCoord =" << xCoord << "and yCoord =" << yCoord;
    if (!tbcSource.getIsSourceLoaded() || !isViewerTabActive()) {
        return;
    }

    // Show the oscilloscope dialogue for the selected scan-line
    lastScopeDot = xCoord;
    lastScopeLine = yCoord + 1;
    if (!oscilloscopeDialog->isVisible()) {
        oscilloscopeDialog->show();
    }
    updateOscilloscopeDialogue();

    // Update the image viewer
    updateImageViewer();
}

// Handler called when vectorscope settings are changed
void MainWindow::vectorscopeChangedSignalHandler()
{
    tbcDebugStream() << "MainWindow::vectorscopeChangedSignalHandler(): Called";
    if (vectorscopeDialog && vectorscopeSelectionPushButton) {
        const bool customAreaSelected = vectorscopeDialog->isCustomAreaModeSelected();
        if (vectorscopeSelectionPushButton->isChecked() != customAreaSelected) {
            const QSignalBlocker blocker(vectorscopeSelectionPushButton);
            vectorscopeSelectionPushButton->setChecked(customAreaSelected);
        }
        if (!customAreaSelected) {
            vectorscopeSelectionDragging = false;
        }
    }

    if (tbcSource.getIsSourceLoaded()) {
        // Update the vectorscope
        updateVectorscopeDialogue();
        updateImageViewer();
    }
}

void MainWindow::keyPressEvent(QKeyEvent *event)
{
    if (!event) {
        return;
    }

    const bool markerKeyPressed = (event->key() == Qt::Key_M)
        && (event->modifiers() == Qt::NoModifier || event->modifiers() == Qt::ShiftModifier);
    const bool markerViewerKeyPressed = (event->key() == Qt::Key_C)
        && (event->modifiers() == Qt::NoModifier);
    const bool setInPointKeyPressed = (event->key() == Qt::Key_BracketLeft)
        && (event->modifiers() == Qt::NoModifier);
    const bool setOutPointKeyPressed = (event->key() == Qt::Key_BracketRight)
        && (event->modifiers() == Qt::NoModifier);
    if (!markerKeyPressed && !markerViewerKeyPressed && !setInPointKeyPressed && !setOutPointKeyPressed) {
        QMainWindow::keyPressEvent(event);
        return;
    }
    if (event->isAutoRepeat()) {
        event->accept();
        return;
    }

    QWidget *focusWidget = QApplication::focusWidget();
    const bool typingContext = focusWidget
        && (qobject_cast<QLineEdit *>(focusWidget)
            || qobject_cast<QTextEdit *>(focusWidget)
            || qobject_cast<QPlainTextEdit *>(focusWidget)
            || qobject_cast<QAbstractSpinBox *>(focusWidget));
    if (typingContext || !tbcSource.getIsSourceLoaded()) {
        QMainWindow::keyPressEvent(event);
        return;
    }

    if (setInPointKeyPressed) {
        setInPointAtCurrentFrame();
        event->accept();
        return;
    }
    if (setOutPointKeyPressed) {
        setOutPointAtCurrentFrame();
        event->accept();
        return;
    }
    if (markerViewerKeyPressed) {
        if (notesViewerDialog) {
            updateNotesViewerState();
            notesViewerDialog->show();
            notesViewerDialog->raise();
            notesViewerDialog->activateWindow();
        }
        event->accept();
        return;
    }

    const qint32 playbackPositionValue = tbcSource.getFieldViewEnabled() ? currentFieldNumber : currentFrameNumber;
    const qint32 totalFrames = qMax<qint32>(1, tbcSource.getNumberOfFrames());
    const qint32 framePoint = qBound<qint32>(1, frameForSliderPosition(playbackPositionValue), totalFrames);
    const QString framePointTimecode = frameToTimecode(framePoint);

    QVector<UserNoteMarker> noteMarkers = userNoteMarkersFromVideoParameters(tbcSource.getVideoParameters(), totalFrames);
    const qint32 noteIndexAtFrame = noteMarkerIndexForFrame(noteMarkers, framePoint);
    const bool noteAtFrameSet = noteIndexAtFrame >= 0;
    const QString noteCommentAtFrame = noteAtFrameSet ? noteMarkers.at(noteIndexAtFrame).comment : QString();

    // M opens the Add/Edit Marker comment dialog at the current frame.
    bool ok = false;
    const QString markerComment = QInputDialog::getText(this,
                                                        noteAtFrameSet ? tr("Edit Marker") : tr("Add Marker"),
                                                        tr("Marker name/comment for frame %1 (%2):")
                                                            .arg(framePoint)
                                                            .arg(framePointTimecode),
                                                        QLineEdit::Normal,
                                                        noteCommentAtFrame,
                                                        &ok);
    if (!ok) {
        event->accept();
        return;
    }

    const QString trimmedComment = markerComment.trimmed();
    if (noteAtFrameSet) {
        noteMarkers[noteIndexAtFrame].comment = trimmedComment;
    } else {
        noteMarkers.append({framePoint, trimmedComment});
    }
    noteMarkers = normaliseUserNoteMarkers(noteMarkers, totalFrames);

    TbcMetaData::VideoParameters videoParameters = tbcSource.getVideoParameters();
    if (applyUserNoteMarkersToVideoParameters(videoParameters, noteMarkers)) {
        tbcSource.setVideoParameters(videoParameters);
        ui->actionSave_Metadata->setEnabled(true);
        updateTimelineMarkers();
        updateNotesViewerState();
        updateMetadataStatusPanel();
        statusBar()->showMessage(tr("Marker saved at frame %1 (%2)")
                                     .arg(framePoint)
                                     .arg(framePointTimecode), 3000);
    }

    event->accept();
}
// Mouse press event handler
void MainWindow::mousePressEvent(QMouseEvent *event)
{
    if (!event) {
        return;
    }
    if (!tbcSource.getIsSourceLoaded() || !isViewerTabActive()) {
        QMainWindow::mousePressEvent(event);
        return;
    }

    // Get the mouse position relative to our scene
    QPoint origin = ui->imageViewerLabel->mapFromGlobal(QCursor::pos());

    // Check that the mouse click is within bounds of the current picture
    qint32 oX = origin.x();
    qint32 oY = origin.y();

    if (oX + 1 >= 0 &&
            oY >= 0 &&
            oX + 1 <= ui->imageViewerLabel->width() &&
            oY <= ui->imageViewerLabel->height()) {
        if (vectorscopeSelectionPushButton
            && vectorscopeSelectionPushButton->isChecked()
            && vectorscopeDialog
            && event->button() == Qt::LeftButton) {
            qint32 sourceX = 0;
            qint32 sourceY = 0;
            if (mapViewerToSourceCoordinates(origin, sourceX, sourceY)) {
                vectorscopeSelectionDragging = true;
                vectorscopeSelectionAnchor = QPoint(sourceX, sourceY);
                vectorscopeDialog->setCustomAreaModeSelected(true);
                vectorscopeDialog->setCustomAreaRect(QRect(sourceX, sourceY, 1, 1));
                updateImageViewer();
                updateVectorscopeDialogue();
            }
            event->accept();
            return;
        }
        if (event->button() == Qt::LeftButton && isExportBoundaryDragAvailable()) {
            const ExportBoundaryHandle dragHandle = exportBoundaryHandleAtViewerPoint(origin);
            if (dragHandle != ExportBoundaryHandle::None) {
                exportBoundarySelectedHandle = dragHandle;
                exportBoundaryDragHandle = dragHandle;
                applyExportBoundaryDragAtViewerPoint(origin);
                updateExportBoundaryHoverCursor(origin);
                event->accept();
                return;
            }
        }
        if (event->button() == Qt::LeftButton) {
            exportBoundarySelectedHandle = ExportBoundaryHandle::None;
            exportBoundaryDragHandle = ExportBoundaryHandle::None;
        }
        if (exportBoundaryDragHandle == ExportBoundaryHandle::None) {
            updateExportBoundaryHoverCursor(origin);
        }

        mouseScanLineSelect(oX, oY);
        event->accept();
        return;
    }

    QMainWindow::mousePressEvent(event);
}

// Mouse move event
void MainWindow::mouseMoveEvent(QMouseEvent *event)
{
    if (!event) {
        return;
    }
    if (!tbcSource.getIsSourceLoaded() || !isViewerTabActive()) {
        QMainWindow::mouseMoveEvent(event);
        return;
    }

    // Get the mouse position relative to our scene
    QPoint origin = ui->imageViewerLabel->mapFromGlobal(QCursor::pos());
    if (exportBoundaryDragHandle != ExportBoundaryHandle::None
        && !(event->buttons() & Qt::LeftButton)) {
        exportBoundaryDragHandle = ExportBoundaryHandle::None;
    }
    if (exportBoundaryDragHandle != ExportBoundaryHandle::None
        && (event->buttons() & Qt::LeftButton)) {
        applyExportBoundaryDragAtViewerPoint(origin);
        updateExportBoundaryHoverCursor(origin);
        event->accept();
        return;
    }

    // Check that the mouse click is within bounds of the current picture
    qint32 oX = origin.x();
    qint32 oY = origin.y();

    if (oX + 1 >= 0 &&
            oY >= 0 &&
            oX + 1 <= ui->imageViewerLabel->width() &&
            oY <= ui->imageViewerLabel->height()) {
        if (vectorscopeSelectionDragging && vectorscopeDialog) {
            qint32 sourceX = 0;
            qint32 sourceY = 0;
            if (mapViewerToSourceCoordinates(origin, sourceX, sourceY)) {
                const qint32 left = qMin(vectorscopeSelectionAnchor.x(), sourceX);
                const qint32 right = qMax(vectorscopeSelectionAnchor.x(), sourceX);
                const qint32 top = qMin(vectorscopeSelectionAnchor.y(), sourceY);
                const qint32 bottom = qMax(vectorscopeSelectionAnchor.y(), sourceY);
                vectorscopeDialog->setCustomAreaRect(QRect(left, top, (right - left) + 1, (bottom - top) + 1));
                updateImageViewer();
                updateVectorscopeDialogue();
            }
            event->accept();
            return;
        }
        if (exportBoundaryDragHandle == ExportBoundaryHandle::None) {
            updateExportBoundaryHoverCursor(origin);
        }

        mouseScanLineSelect(oX, oY);
        event->accept();
        return;
    }

    if (!vectorscopeSelectionDragging) {
        clearCursorReadout();
        if (exportBoundaryDragHandle == ExportBoundaryHandle::None) {
            updateExportBoundaryHoverCursor(QPoint(-1, -1));
        }
    }
    QMainWindow::mouseMoveEvent(event);
}

void MainWindow::mouseReleaseEvent(QMouseEvent *event)
{
    if (!event) {
        return;
    }
    if (!tbcSource.getIsSourceLoaded() || !isViewerTabActive()) {
        QMainWindow::mouseReleaseEvent(event);
        return;
    }

    if (vectorscopeSelectionDragging && event->button() == Qt::LeftButton) {
        vectorscopeSelectionDragging = false;
        QPoint origin = ui->imageViewerLabel->mapFromGlobal(QCursor::pos());
        qint32 sourceX = 0;
        qint32 sourceY = 0;
        if (vectorscopeDialog && mapViewerToSourceCoordinates(origin, sourceX, sourceY)) {
            const qint32 left = qMin(vectorscopeSelectionAnchor.x(), sourceX);
            const qint32 right = qMax(vectorscopeSelectionAnchor.x(), sourceX);
            const qint32 top = qMin(vectorscopeSelectionAnchor.y(), sourceY);
            const qint32 bottom = qMax(vectorscopeSelectionAnchor.y(), sourceY);
            vectorscopeDialog->setCustomAreaModeSelected(true);
            vectorscopeDialog->setCustomAreaRect(QRect(left, top, (right - left) + 1, (bottom - top) + 1));
            updateVectorscopeDialogue();
        }
        updateImageViewer();
        event->accept();
        return;
    }
    if (event->button() == Qt::LeftButton
        && exportBoundaryDragHandle != ExportBoundaryHandle::None) {
        QPoint origin = ui->imageViewerLabel->mapFromGlobal(QCursor::pos());
        applyExportBoundaryDragAtViewerPoint(origin);
        exportBoundaryDragHandle = ExportBoundaryHandle::None;
        updateExportBoundaryHoverCursor(origin);
        event->accept();
        return;
    }

    QMainWindow::mouseReleaseEvent(event);
}

// Perform mouse based scan line selection
void MainWindow::mouseScanLineSelect(qint32 oX, qint32 oY)
{
    if (!isViewerTabActive()) {
        return;
    }
    const QPoint viewerPoint(oX, oY);
    updateCursorReadout(viewerPoint);

    qint32 sourceX = 0;
    qint32 sourceY = 0;
    if (!mapViewerToSourceCoordinates(viewerPoint, sourceX, sourceY)) {
        return;
    }

    // Show the oscilloscope dialogue for the selected scan-line (if the right mouse mode is selected)
    if (ui->mouseModePushButton->isChecked()) {
        // Remember the last line rendered
        lastScopeLine = qBound<qint32>(1, sourceY + 1, tbcSource.getFrameHeight());
        lastScopeDot = sourceX;
        if (!oscilloscopeDialog->isVisible()) {
            oscilloscopeDialog->show();
        }

        updateOscilloscopeDialogue();

        // Update the image viewer
        updateImageViewer();
    }
}

// Handle parameters changed signal from the video parameters dialogue
void MainWindow::videoParametersChangedSignalHandler(const TbcMetaData::VideoParameters &videoParameters)
{
    cancelInFlightAsyncFrameRender();
    // Update the VideoParameters in the source
    tbcSource.setVideoParameters(videoParameters);
    if (exportDialog) {
        exportDialog->refreshResolutionOptions();
    }

    // Enable the "Save Metadata" action, since the metadata has been modified
    ui->actionSave_Metadata->setEnabled(true);

    // Update the aspect button's label
    updateAspectPushButton();

    // Update the image viewer
    updateImage();
    updateVideoPushButton();
    updateTimelineMarkers();
    updateNotesViewerState();

    updateMetadataStatusPanel();
}
void MainWindow::videoLevelsChangedSignalHandler(qint32 blackLevel, qint32 whiteLevel)
{
    TbcMetaData::VideoParameters videoParameters = tbcSource.getVideoParameters();
    videoParameters.black16bIre = blackLevel;
    videoParameters.white16bIre = whiteLevel;
    videoParametersChangedSignalHandler(videoParameters);
}
void MainWindow::exportRangeSelectionChangedSignalHandler(int inPoint, int outPoint, bool clearMetadataValues)
{
    if (!tbcSource.getIsSourceLoaded() || tbcSource.getIsMetadataOnly()) {
        return;
    }

    const qint32 totalFrames = qMax<qint32>(1, tbcSource.getNumberOfFrames());
    qint32 clampedIn = qBound<qint32>(1, inPoint, totalFrames);
    qint32 clampedOut = qBound<qint32>(1, outPoint, totalFrames);
    if (clampedOut < clampedIn) {
        clampedOut = clampedIn;
    }

    const qint32 metadataIn = clearMetadataValues ? -1 : clampedIn;
    const qint32 metadataOut = clearMetadataValues ? -1 : clampedOut;

    TbcMetaData::VideoParameters videoParameters = tbcSource.getVideoParameters();
    if (videoParameters.userEditInSelection == metadataIn
        && videoParameters.userEditOutSelection == metadataOut) {
        return;
    }

    videoParameters.userEditInSelection = metadataIn;
    videoParameters.userEditOutSelection = metadataOut;
    tbcSource.setVideoParameters(videoParameters);

    ui->actionSave_Metadata->setEnabled(true);
    updateTimelineMarkers();
    updateNotesViewerState();
    updateMetadataStatusPanel();
}

void MainWindow::exportBoundaryToggledSignalHandler(bool enabled)
{
    showExportBoundary = enabled;
    if (!enabled) {
        exportBoundaryDragHandle = ExportBoundaryHandle::None;
        exportBoundarySelectedHandle = ExportBoundaryHandle::None;
        updateExportBoundaryHoverCursor(QPoint(-1, -1));
    }
    configuration.setShowExportBoundary(enabled);
    configuration.writeConfiguration();

    updateImageViewer();
}

void MainWindow::exportBoundaryThicknessChangedSignalHandler(int thickness)
{
    exportBoundaryThickness = thickness;
    configuration.setExportBoundaryThickness(thickness);
    configuration.writeConfiguration();

    updateImageViewer();
}

// Handle configuration changed signal from the chroma decoder configuration dialogue
void MainWindow::chromaDecoderConfigChangedSignalHandler()
{
    const bool resumePlayback = playbackRunning;
    setPlaybackRunning(false);
    cancelInFlightAsyncFrameRender();
    const PalColour::Configuration &palConfig = chromaDecoderConfigDialog->getPalConfiguration();
    const Comb::Configuration &ntscConfig = chromaDecoderConfigDialog->getNtscConfiguration();
    // Set the new configuration
    tbcSource.setChromaConfiguration(palConfig, ntscConfig);
    tbcSource.setSecamPredemodFirstLineIsRedOverride(chromaDecoderConfigDialog->getSecamPredemodFirstLineIsRedOverride());

    TbcMetaData::VideoParameters videoParameters = tbcSource.getVideoParameters();
    videoParameters.chromaGain = palConfig.chromaGain;
    videoParameters.chromaPhase = palConfig.chromaPhase;
    videoParameters.lumaNR = (tbcSource.getSystem() == NTSC) ? ntscConfig.yNRLevel : palConfig.yNRLevel;
    videoParameters.ntscAdaptive = ntscConfig.adaptive ? 1 : 0;
    videoParameters.ntscAdaptThreshold = ntscConfig.adaptThreshold;
    videoParameters.ntscChromaWeight = ntscConfig.chromaWeight;
    videoParameters.ntscPhaseCompensation = ntscConfig.phaseCompensation ? 1 : 0;
    videoParameters.palTransformThreshold = palConfig.transformThreshold;

    const QString decoderName = chromaDecoderNameFromConfig(tbcSource.getSystem(), palConfig, ntscConfig);
    if (!decoderName.isEmpty()) {
        videoParameters.chromaDecoder = decoderName;
    }

    tbcSource.setVideoParameters(videoParameters);

    // Enable the \"Save Metadata\" action, since the metadata has been modified
    ui->actionSave_Metadata->setEnabled(true);

    // Update the image viewer
    updateImage();
    updateVideoPushButton();
    if (resumePlayback) {
        setPlaybackRunning(true);
    } else if (ui && ui->playPushButton) {
        ui->playPushButton->setToolTip(playbackStartToolTip());
    }

    updateMetadataStatusPanel();
}

// TbcSource class signal handlers ------------------------------------------------------------------------------------

// Signal handler for busy signal from TbcSource class
void MainWindow::on_busy(QString infoMessage)
{
    setPlaybackRunning(false);
    tbcDebugStream() << "MainWindow::on_busy(): Got signal with message" << infoMessage;
    sourceOperationInProgress = true;
    // Set the busy message and centre the dialog in the parent window
    busyDialog->setMessage(infoMessage);
    busyDialog->move(this->geometry().center() - busyDialog->rect().center());

    if (!busyDialog->isVisible()) {
        // Disable the main window during loading
        this->setEnabled(false);
        busyDialog->setEnabled(true);

        busyDialog->show();
    }
}

// Signal handler for finishedLoading signal from TbcSource class
void MainWindow::on_finishedLoading(bool success)
{
    tbcDebugStream() << "MainWindow::on_finishedLoading(): Called";
    setPlaybackRunning(false);
    sourceOperationInProgress = false;

    // Hide the busy dialogue
    busyDialog->hide();

    // Ensure source loaded ok
    if (success) {
        // Generate the graph data
        dropoutAnalysisDialog->startUpdate(tbcSource.getNumberOfFrames());
        visibleDropoutAnalysisDialog->startUpdate(tbcSource.getNumberOfFrames());
        blackSnrAnalysisDialog->startUpdate(tbcSource.getNumberOfFrames());
        whiteSnrAnalysisDialog->startUpdate(tbcSource.getNumberOfFrames());

        QVector<double> doGraphData = tbcSource.getDropOutGraphData();
        QVector<double> visibleDoGraphData = tbcSource.getVisibleDropOutGraphData();
        QVector<double> blackSnrGraphData = tbcSource.getBlackSnrGraphData();
        QVector<double> whiteSnrGraphData = tbcSource.getWhiteSnrGraphData();

        for (qint32 frameNumber = 0; frameNumber < tbcSource.getNumberOfFrames(); frameNumber++) {
            dropoutAnalysisDialog->addDataPoint(frameNumber + 1, doGraphData[frameNumber]);
            visibleDropoutAnalysisDialog->addDataPoint(frameNumber + 1, visibleDoGraphData[frameNumber]);
            blackSnrAnalysisDialog->addDataPoint(frameNumber + 1, blackSnrGraphData[frameNumber]);
            whiteSnrAnalysisDialog->addDataPoint(frameNumber + 1, whiteSnrGraphData[frameNumber]);
        }

        dropoutAnalysisDialog->finishUpdate(currentFrameNumber);
        visibleDropoutAnalysisDialog->finishUpdate(currentFrameNumber);
        blackSnrAnalysisDialog->finishUpdate(currentFrameNumber);
        whiteSnrAnalysisDialog->finishUpdate(currentFrameNumber);

        // Update the GUI
        resetGui();
        updateGuiLoaded();
        if (restoreUiStateAfterReload) {
            applyUiStateSnapshot(pendingUiStateSnapshot);
            pendingUiStateSnapshot = UiStateSnapshot();
            restoreUiStateAfterReload = false;
        } else if (ui && ui->mainTabWidget && ui->viewerTab) {
            ui->mainTabWidget->setCurrentWidget(ui->viewerTab);
        }
        if (exportDialog) {
            exportDialog->setSource(&tbcSource);
        }

        // Set the main window title
        this->setWindowTitle(tr("ld-analyse - ") + tbcSource.getCurrentSourceFilename());

        // Update the configuration for the source directory
        QFileInfo inFileInfo(tbcSource.getCurrentSourceFilename());
        configuration.setSourceDirectory(inFileInfo.absolutePath());
        tbcDebugStream() << "MainWindow::loadTbcFile(): Setting source directory to:" << inFileInfo.absolutePath();
        configuration.writeConfiguration();
        applyEfmHandlerAutoloads(inFileInfo.absolutePath());
    } else {
        // Load failed
        updateGuiUnloaded();
        pendingUiStateSnapshot = UiStateSnapshot();
        restoreUiStateAfterReload = false;

        // Show the error to the user
        QMessageBox messageBox;
        messageBox.warning(this, "Error", tbcSource.getLastIOError());
    }

    // Enable the main window
    this->setEnabled(true);
    processPendingSourceOpenRequest();
}

// Signal handler for finishedSaving signal from TbcSource class
void MainWindow::on_finishedSaving(bool success)
{
    tbcDebugStream() << "MainWindow::on_finishedSaving(): Called";
    sourceOperationInProgress = false;

    // Hide the busy dialogue
    busyDialog->hide();

    if (success) {
        // Disable the "Save Metadata" action until the metadata is modified again
        ui->actionSave_Metadata->setEnabled(false);

        // Reload the source with the newly-saved metadata so the GUI reflects
        // the edited values (e.g. TV system change, chroma decoder switch).
        if (tbcSource.getIsSourceLoaded()) {
            pendingUiStateSnapshot = captureUiStateSnapshot();
            restoreUiStateAfterReload = true;
            const QString currentSource = tbcSource.getCurrentSourceFilename();
            const QString currentMetadata = tbcSource.getCurrentMetadataFilename();
            if (!currentSource.isEmpty()) {
                loadTbcFile(currentSource, false, true);
            } else if (!currentMetadata.isEmpty()) {
                loadTbcFile(currentMetadata, true, true);
            }
        }
    } else {
        // Show the error to the user
        QMessageBox messageBox;
        messageBox.warning(this, tr("Error"), tbcSource.getLastIOError());
    }

    updateMetadataStatusPanel();

    // Enable the main window
    this->setEnabled(true);
    processPendingSourceOpenRequest();
}

void MainWindow::resizeEvent(QResizeEvent *event)
{
    QMainWindow::resizeEvent(event);
    int width = this->width();
    const auto setButtonMaxWidth = [](QPushButton *button, int maxWidth) {
        if (button) {
            button->setMaximumWidth(maxWidth);
        }
    };

    if (width > 1000) {
        setButtonMaxWidth(ui->videoPushButton, 80);
        setButtonMaxWidth(ui->aspectPushButton, 70);
        setButtonMaxWidth(ui->dropoutsPushButton, 115);
        setButtonMaxWidth(ui->sourcesPushButton, 110);
        setButtonMaxWidth(ui->viewPushButton, 100);
        setButtonMaxWidth(ui->fieldOrderPushButton, 145);
        if (ui->horizontalSpacer) {
            ui->horizontalSpacer->changeSize(12, 30, QSizePolicy::Maximum, QSizePolicy::Minimum);
        }
        if (ui->horizontalSpacer_2) {
            ui->horizontalSpacer_2->changeSize(12, 20, QSizePolicy::Maximum, QSizePolicy::Minimum);
        }
    } else if (width >= 930) {
        setButtonMaxWidth(ui->videoPushButton, 72);
        setButtonMaxWidth(ui->aspectPushButton, 64);
        setButtonMaxWidth(ui->dropoutsPushButton, 102);
        setButtonMaxWidth(ui->sourcesPushButton, 98);
        setButtonMaxWidth(ui->viewPushButton, 92);
        setButtonMaxWidth(ui->fieldOrderPushButton, 118);
        if (ui->horizontalSpacer) {
            ui->horizontalSpacer->changeSize(8, 30, QSizePolicy::Maximum, QSizePolicy::Minimum);
        }
        if (ui->horizontalSpacer_2) {
            ui->horizontalSpacer_2->changeSize(8, 20, QSizePolicy::Maximum, QSizePolicy::Minimum);
        }
    } else {
        setButtonMaxWidth(ui->videoPushButton, 58);
        setButtonMaxWidth(ui->aspectPushButton, 52);
        setButtonMaxWidth(ui->dropoutsPushButton, 74);
        setButtonMaxWidth(ui->sourcesPushButton, 70);
        setButtonMaxWidth(ui->viewPushButton, 66);
        setButtonMaxWidth(ui->fieldOrderPushButton, 88);
        if (ui->horizontalSpacer) {
            ui->horizontalSpacer->changeSize(4, 30, QSizePolicy::Maximum, QSizePolicy::Minimum);
        }
        if (ui->horizontalSpacer_2) {
            ui->horizontalSpacer_2->changeSize(4, 20, QSizePolicy::Maximum, QSizePolicy::Minimum);
        }
    }
    if (ui->horizontalLayout_3) {
        ui->horizontalLayout_3->invalidate();
    }

	//field order rename depending on size
	if (!tbcSource.getFieldOrder())
	{
		if (width > 1000)
			ui->fieldOrderPushButton->setText(tr("Normal Field-order"));
		else if (width >= 930)
			ui->fieldOrderPushButton->setText(tr("Normal order"));
		else
			ui->fieldOrderPushButton->setText(tr("Normal"));
	}
	else
	{
		if (width > 1000)
			ui->fieldOrderPushButton->setText(tr("Reverse Field-order"));
		else if (width >= 930)
			ui->fieldOrderPushButton->setText(tr("Reverse order"));
		else
			ui->fieldOrderPushButton->setText(tr("Reverse"));
	}

	//source label depending on size
	updateSourcesPushButton();

	//dropout label
	if (!tbcSource.getHighlightDropouts())
	{
		if (width >= 930)
		{
			ui->dropoutsPushButton->setText(tr("Dropouts Off"));
		}
		else
		{
			ui->dropoutsPushButton->setText(tr("Drop N"));
		}

	}
	else
	{
		if (width >= 930)
		{
			ui->dropoutsPushButton->setText(tr("Dropouts On"));
		}
		else
		{
			ui->dropoutsPushButton->setText(tr("Drop Y"));
		}
	}

	//view label
	if (this->width() >= 930)
	{
		if (tbcSource.getFieldViewEnabled()) {
			if (tbcSource.getStretchField()) {
				ui->viewPushButton->setText(tr("Field 2:1"));
			} else {
				ui->viewPushButton->setText(tr("Field 1:1"));
			}
		} else {
			if (tbcSource.getSplitViewEnabled()) {
				ui->viewPushButton->setText(tr("Split View"));
			} else {
				ui->viewPushButton->setText(tr("Frame View"));
			}
		}
	}
	else
	{
		if (tbcSource.getFieldViewEnabled()) {
			if (tbcSource.getStretchField()) {
				ui->viewPushButton->setText(tr("Field 2:1"));
			} else {
				ui->viewPushButton->setText(tr("Field 1:1"));
			}
		} else {
			if (tbcSource.getSplitViewEnabled()) {
				ui->viewPushButton->setText(tr("Split"));
			} else {
				ui->viewPushButton->setText(tr("Frame"));
			}
		}
	}

	//aspect ratio label
	updateAspectPushButton();

	// Resize frame with window if resizeFrameWithWindow is enabled
	if (resizeFrameWithWindow && tbcSource.getIsSourceLoaded()) {
		resizeTimer->start();
	}
}
