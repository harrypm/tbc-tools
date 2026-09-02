/******************************************************************************
 * audioalignmentutil.cpp
 * tbc-audio-align - Audio alignment helper for ld-decode
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 *
 * This file is part of tbc-tools.
 ******************************************************************************/

#include "audioalignmentutil.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QTemporaryDir>

namespace {
QString normalizeForPathParsing(const QString &path)
{
    QString normalized = path.trimmed();
    normalized.replace(QLatin1Char('\\'), QLatin1Char('/'));
    return QDir::cleanPath(normalized);
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

void appendDerivedRootCandidates(QStringList &candidates, const QString &baseName)
{
    QString current = baseName.trimmed();
    if (current.isEmpty()) {
        return;
    }

    appendUniqueCandidate(candidates, current);
    bool changed = true;
    while (changed && !current.isEmpty()) {
        changed = false;

        if (current.endsWith(QStringLiteral(".tbc"), Qt::CaseInsensitive)) {
            current.chop(4);
            appendUniqueCandidate(candidates, current);
            changed = true;
        }
        if (current.endsWith(QStringLiteral("_decoded"), Qt::CaseInsensitive)) {
            current.chop(8);
            appendUniqueCandidate(candidates, current);
            changed = true;
        } else if (current.endsWith(QStringLiteral("-decoded"), Qt::CaseInsensitive)) {
            current.chop(8);
            appendUniqueCandidate(candidates, current);
            changed = true;
        }
    }

    // The standard vhs-decode naming convention puts the metadata at
    // <stem>-video.tbc.json. After stripping ".tbc" the root is
    // <stem>-video, which the linear (<stem>-linear.flac) and hifi
    // (<stem>-hifi.flac) inputs do not relate to under the prefix-match
    // rules, so preferred-detect fails and the Any fallback targets the RF
    // dump. Strip a single trailing "-video" / "_video" segment so the
    // capture <stem> is also a root candidate; the linear/hifi inputs then
    // match <stem>-linear / <stem>-hifi. Verified against a real capture dir.
    if (current.endsWith(QStringLiteral("-video"), Qt::CaseInsensitive)) {
        current.chop(6);
        appendUniqueCandidate(candidates, current);
    } else if (current.endsWith(QStringLiteral("_video"), Qt::CaseInsensitive)) {
        current.chop(6);
        appendUniqueCandidate(candidates, current);
    }
}

QString findFirstExistingFile(const QStringList &candidates)
{
    for (const QString &candidate : candidates) {
        if (QFileInfo::exists(candidate) && QFileInfo(candidate).isFile()) {
            return candidate;
        }
    }
    return QString();
}

QString findFirstExistingExecutable(const QStringList &candidates)
{
    for (const QString &candidate : candidates) {
        const QFileInfo candidateInfo(candidate);
        if (candidateInfo.exists() && candidateInfo.isFile() && candidateInfo.isExecutable()) {
            return candidateInfo.absoluteFilePath();
        }
    }
    return QString();
}

QStringList executableNameCandidates(const QString &baseName)
{
    QStringList names;
#if defined(Q_OS_WIN)
    names << (baseName + QStringLiteral(".exe"));
#endif
    names << baseName;
    return names;
}

QString resolveBundledOrPathTool(const QString &baseName)
{
    const QStringList names = executableNameCandidates(baseName);
    if (names.isEmpty()) {
        return QString();
    }

    // applicationDirPath() may point at the bundled loader's dir (usr/lib) in
    // the Linux AppImage rather than the binaries' dir (usr/bin) — see the
    // comment in resolveAudioAlignExecutablePath. Probe ../bin (usr/lib ->
    // usr/bin) and honor an explicit TBC_TOOLS_APP_BIN_DIR override so the
    // bundled ffmpeg/ffprobe are found via the bundle, not just PATH.
    const QString appDir = QCoreApplication::applicationDirPath();
    const QString envBinDir = qEnvironmentVariable("TBC_TOOLS_APP_BIN_DIR");
    QStringList candidateDirs;
    appendUniqueCandidate(candidateDirs, appDir);
    appendUniqueCandidate(candidateDirs, QDir(appDir).filePath(QStringLiteral("bin")));
    appendUniqueCandidate(candidateDirs, QDir(appDir).filePath(QStringLiteral("../bin")));
    appendUniqueCandidate(candidateDirs, QDir(appDir).filePath(QStringLiteral("../MacOS")));
    appendUniqueCandidate(candidateDirs, QDir(appDir).filePath(QStringLiteral("../Resources/bin")));
    appendUniqueCandidate(candidateDirs, QDir(appDir).filePath(QStringLiteral("../../MacOS")));
    appendUniqueCandidate(candidateDirs, QDir(appDir).filePath(QStringLiteral("../../Resources/bin")));
    if (!envBinDir.isEmpty()) {
        appendUniqueCandidate(candidateDirs, envBinDir);
    }

    const QString currentDir = QDir::currentPath();
    appendUniqueCandidate(candidateDirs, currentDir);
    appendUniqueCandidate(candidateDirs, QDir(currentDir).filePath(QStringLiteral("build/bin")));

    const QString homeDir = QDir::homePath();
    if (!homeDir.isEmpty()) {
        appendUniqueCandidate(candidateDirs, QDir(homeDir).filePath(QStringLiteral(".local/bin")));
        appendUniqueCandidate(candidateDirs, QDir(homeDir).filePath(QStringLiteral("bin")));
    }

    QStringList bundledCandidates;
    for (const QString &directoryPath : candidateDirs) {
        const QDir directory(directoryPath);
        for (const QString &name : names) {
            appendUniqueCandidate(bundledCandidates, directory.filePath(name));
        }
    }

    const QString bundledTool = findFirstExistingExecutable(bundledCandidates);
    if (!bundledTool.isEmpty()) {
        return bundledTool;
    }

    for (const QString &name : names) {
        const QString fromPath = QStandardPaths::findExecutable(name);
        if (!fromPath.isEmpty()) {
            return fromPath;
        }
    }

    return QString();
}

QString resolveAudioAlignExecutablePath()
{
    QStringList pathCandidates;
    // Native names: a PATH-installed native build, or the self-contained
    // AppImage the Linux CI builds from the vendored source (bundles Mono,
    // so no host Mono is needed at runtime). The AppImage is searched first
    // so Linux picks it over the Windows .exe that needs host Mono.
    appendUniqueCandidate(pathCandidates, QStandardPaths::findExecutable(QStringLiteral("vhs-decode-aaa.AppImage")));
    appendUniqueCandidate(pathCandidates, QStandardPaths::findExecutable(QStringLiteral("vhs-decode-auto-audio-align")));
    appendUniqueCandidate(pathCandidates, QStandardPaths::findExecutable(QStringLiteral("VhsDecodeAutoAudioAlign.exe")));
    appendUniqueCandidate(pathCandidates, QStandardPaths::findExecutable(QStringLiteral("vhs-decode-auto-audio-align.exe")));

    // QCoreApplication::applicationDirPath() returns the directory of the
    // running executable. In the Linux AppImage the ld-analyse bash wrapper
    // launches the real ELF via the bundled glibc loader
    // (exec <usr/lib/ld-linux-x86-64.so.2> --library-path usr/lib <usr/bin/.ld-analyse.real>),
    // so /proc/self/exe (and thus applicationDirPath()) points at usr/lib,
    // NOT usr/bin where the vendored AAA lives. Probe several relative
    // layouts so the bundled AAA is found regardless of which directory Qt
    // reports: ../bin (AppImage loader case: usr/lib -> usr/bin), the usual
    // vendor subdirectories, and an explicit TBC_TOOLS_APP_BIN_DIR override
    // the launcher may export to declare the real binary directory.
    const QString appDir = QCoreApplication::applicationDirPath();
    const QString envBinDir = qEnvironmentVariable("TBC_TOOLS_APP_BIN_DIR");
    QStringList candidateDirs;
    // An explicit launcher override takes priority over heuristic appDir
    // probing: the Linux AppImage/tbc-tools-run launcher knows the real binary
    // directory even when applicationDirPath() reports the bundled loader's dir.
    if (!envBinDir.isEmpty()) {
        appendUniqueCandidate(candidateDirs, QDir(envBinDir).filePath(QStringLiteral("vendor/vhs_decode_auto_audio_align")));
        appendUniqueCandidate(candidateDirs, envBinDir);
    }
    appendUniqueCandidate(candidateDirs, appDir);
    appendUniqueCandidate(candidateDirs, QDir(appDir).filePath(QStringLiteral("../bin")));
    appendUniqueCandidate(candidateDirs, QDir(appDir).filePath(QStringLiteral("vendor/vhs_decode_auto_audio_align")));
    appendUniqueCandidate(candidateDirs, QDir(appDir).filePath(QStringLiteral("../bin/vendor/vhs_decode_auto_audio_align")));
    appendUniqueCandidate(candidateDirs, QDir(appDir).filePath(QStringLiteral("../vendor/vhs_decode_auto_audio_align")));
    appendUniqueCandidate(candidateDirs, QDir(appDir).filePath(QStringLiteral("../../vendor/vhs_decode_auto_audio_align")));
    appendUniqueCandidate(candidateDirs, QDir(appDir).filePath(QStringLiteral("../share/tbc-audio-align/vendor/vhs_decode_auto_audio_align")));
    appendUniqueCandidate(candidateDirs, QDir(appDir).filePath(QStringLiteral("../../share/tbc-audio-align/vendor/vhs_decode_auto_audio_align")));
#if defined(AUDIO_ALIGN_VENDOR_DIR)
    appendUniqueCandidate(candidateDirs, QString::fromUtf8(AUDIO_ALIGN_VENDOR_DIR));
#endif

    for (const QString &directoryPath : candidateDirs) {
        const QDir directory(directoryPath);
        // Self-contained AppImage (Linux CI build) first — it bundles Mono,
        // so no host Mono is needed. On macOS/Windows this file is absent
        // and the search falls through to the .exe below.
        appendUniqueCandidate(pathCandidates, directory.filePath(QStringLiteral("vhs-decode-aaa.AppImage")));
        appendUniqueCandidate(pathCandidates, directory.filePath(QStringLiteral("VhsDecodeAutoAudioAlign.exe")));
        appendUniqueCandidate(pathCandidates, directory.filePath(QStringLiteral("vhs-decode-auto-audio-align")));
        appendUniqueCandidate(pathCandidates, directory.filePath(QStringLiteral("vhs-decode-auto-audio-align.exe")));
    }

    return findFirstExistingFile(pathCandidates);
}

bool resolveRunner(QString *program,
                   QStringList *launcherPrefixArguments,
                   QString *errorMessage)
{
    if (errorMessage) {
        errorMessage->clear();
    }

    const QString toolPath = resolveAudioAlignExecutablePath();
    if (toolPath.isEmpty()) {
        if (errorMessage) {
            *errorMessage = QObject::tr("VhsDecodeAutoAudioAlign executable not found in PATH or vendored locations.");
        }
        return false;
    }

#if defined(Q_OS_WIN)
    *program = toolPath;
    launcherPrefixArguments->clear();
    return true;
#else
    // A self-contained AppImage (the Linux CI build) bundles the Mono
    // runtime, so run it directly with APPIMAGE_EXTRACT_AND_RUN=1 — no host
    // Mono required. The `env` launcher matches the mono-wrapper pattern
    // below (program + prefix args) without changing runProcess's signature.
    if (toolPath.endsWith(QStringLiteral(".AppImage"), Qt::CaseInsensitive)) {
        if (!QFileInfo(toolPath).isExecutable()) {
            if (errorMessage) {
                *errorMessage = QObject::tr("VhsDecodeAutoAudioAlign AppImage is not executable: %1").arg(toolPath);
            }
            return false;
        }
        const QString envPath = QStandardPaths::findExecutable(QStringLiteral("env"));
        if (envPath.isEmpty()) {
            if (errorMessage) {
                *errorMessage = QObject::tr("Could not locate `env` to launch the VhsDecodeAutoAudioAlign AppImage.");
            }
            return false;
        }
        *program = envPath;
        launcherPrefixArguments->clear();
        launcherPrefixArguments->append(QStringLiteral("APPIMAGE_EXTRACT_AND_RUN=1"));
        launcherPrefixArguments->append(toolPath);
        return true;
    }

    if (toolPath.endsWith(QStringLiteral(".exe"), Qt::CaseInsensitive)) {
        const QString monoPath = QStandardPaths::findExecutable(QStringLiteral("mono"));
        if (monoPath.isEmpty()) {
            if (errorMessage) {
                *errorMessage = QObject::tr("mono is required to run VhsDecodeAutoAudioAlign.exe on this platform.");
            }
            return false;
        }
        *program = monoPath;
        launcherPrefixArguments->clear();
        launcherPrefixArguments->append(toolPath);
        return true;
    }

    *program = toolPath;
    launcherPrefixArguments->clear();
    return true;
#endif
}

QString resolveFfmpegPath()
{
    return resolveBundledOrPathTool(QStringLiteral("ffmpeg"));
}

QString resolveFfprobePath()
{
    return resolveBundledOrPathTool(QStringLiteral("ffprobe"));
}
int extractPercentFromOutputLine(const QString &line)
{
    static const QRegularExpression percentRegex(QStringLiteral("([0-9]{1,3}(?:\\.[0-9]+)?)\\s*%"));
    QRegularExpressionMatchIterator iterator = percentRegex.globalMatch(line);
    double parsedPercent = -1.0;
    while (iterator.hasNext()) {
        const QRegularExpressionMatch match = iterator.next();
        bool ok = false;
        const double capturedPercent = match.captured(1).toDouble(&ok);
        if (ok) {
            parsedPercent = capturedPercent;
        }
    }
    if (parsedPercent < 0.0) {
        return -1;
    }
    if (parsedPercent < 0.0) {
        parsedPercent = 0.0;
    }
    if (parsedPercent > 100.0) {
        parsedPercent = 100.0;
    }
    return static_cast<int>(parsedPercent + 0.5);
}

void emitProgress(const AudioAlignmentUtil::ProgressCallback &progressCallback,
                  int percent,
                  const QString &statusMessage)
{
    if (!progressCallback) {
        return;
    }
    int clampedPercent = percent;
    if (clampedPercent < 0) {
        clampedPercent = 0;
    }
    if (clampedPercent > 100) {
        clampedPercent = 100;
    }
    progressCallback(clampedPercent, statusMessage);
}

bool runProcess(const QString &program,
                const QStringList &arguments,
                const QString &workingDirectory,
                const std::function<void(const QString &)> &outputLineCallback,
                const AudioAlignmentUtil::CancelCallback &cancelCallback,
                QString *combinedOutput,
                QString *errorMessage)
{
    if (combinedOutput) {
        combinedOutput->clear();
    }
    if (errorMessage) {
        errorMessage->clear();
    }

    QProcess process;
    process.setProcessChannelMode(QProcess::MergedChannels);
    if (!workingDirectory.isEmpty()) {
        process.setWorkingDirectory(workingDirectory);
    }
    process.start(program, arguments);
    if (!process.waitForStarted(5000)) {
        if (errorMessage) {
            *errorMessage = QObject::tr("Unable to start process: %1").arg(program);
        }
        return false;
    }

    QByteArray combinedOutputData;
    QString pendingOutputLine;
    auto consumeChunk = [&](const QByteArray &outputChunk) {
        if (outputChunk.isEmpty()) {
            return;
        }
        combinedOutputData.append(outputChunk);

        QString normalizedChunk = QString::fromLocal8Bit(outputChunk);
        normalizedChunk.replace(QLatin1Char('\r'), QLatin1Char('\n'));
        pendingOutputLine.append(normalizedChunk);

        qsizetype newlineIndex = -1;
        while ((newlineIndex = pendingOutputLine.indexOf(QLatin1Char('\n'))) >= 0) {
            const QString outputLine = pendingOutputLine.left(newlineIndex).trimmed();
            pendingOutputLine.remove(0, newlineIndex + 1);
            if (!outputLine.isEmpty() && outputLineCallback) {
                outputLineCallback(outputLine);
            }
        }
    };

    auto cancellationRequested = [&cancelCallback]() -> bool {
        return cancelCallback && cancelCallback();
    };
    while (process.state() != QProcess::NotRunning) {
        if (cancellationRequested()) {
            process.terminate();
            if (!process.waitForFinished(1000)) {
                process.kill();
                process.waitForFinished(3000);
            }
            consumeChunk(process.readAllStandardOutput());
            const QString cancelledOutput = QString::fromLocal8Bit(combinedOutputData).trimmed();
            if (combinedOutput) {
                *combinedOutput = cancelledOutput;
            }
            if (errorMessage) {
                *errorMessage = QObject::tr("Operation cancelled by user.");
                if (!cancelledOutput.isEmpty()) {
                    *errorMessage += QStringLiteral("\n") + cancelledOutput;
                }
            }
            return false;
        }
        process.waitForReadyRead(100);
        consumeChunk(process.readAllStandardOutput());
    }
    if (cancellationRequested()) {
        const QString cancelledOutput = QString::fromLocal8Bit(combinedOutputData).trimmed();
        if (combinedOutput) {
            *combinedOutput = cancelledOutput;
        }
        if (errorMessage) {
            *errorMessage = QObject::tr("Operation cancelled by user.");
            if (!cancelledOutput.isEmpty()) {
                *errorMessage += QStringLiteral("\n") + cancelledOutput;
            }
        }
        return false;
    }
    consumeChunk(process.readAllStandardOutput());
    if (!pendingOutputLine.trimmed().isEmpty() && outputLineCallback) {
        outputLineCallback(pendingOutputLine.trimmed());
    }

    const QString processOutput = QString::fromLocal8Bit(combinedOutputData).trimmed();
    if (combinedOutput) {
        *combinedOutput = processOutput;
    }

    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
        if (errorMessage) {
            *errorMessage = processOutput.isEmpty()
                ? QObject::tr("Process failed (%1), exit code %2.").arg(program).arg(process.exitCode())
                : processOutput;
        }
        return false;
    }

    return true;
}

bool detectAudioSampleRateHz(const QString &ffprobePath,
                             const QString &inputFilename,
                             quint32 *sampleRateHz,
                             QString *errorMessage)
{
    QString output;
    QString processError;
    const QStringList arguments = {
        QStringLiteral("-v"), QStringLiteral("error"),
        QStringLiteral("-select_streams"), QStringLiteral("a:0"),
        QStringLiteral("-show_entries"), QStringLiteral("stream=sample_rate"),
        QStringLiteral("-of"), QStringLiteral("default=noprint_wrappers=1:nokey=1"),
        inputFilename
    };
    if (!runProcess(ffprobePath,
                    arguments,
                    QString(),
                    {},
                    AudioAlignmentUtil::CancelCallback(),
                    &output,
                    &processError)) {
        if (errorMessage) {
            *errorMessage = processError.isEmpty()
                ? QObject::tr("Unable to detect audio sample rate using ffprobe.")
                : processError;
        }
        return false;
    }

    bool ok = false;
    const quint32 parsedSampleRate = output.trimmed().toUInt(&ok);
    if (!ok || parsedSampleRate == 0) {
        if (errorMessage) {
            *errorMessage = QObject::tr("ffprobe returned an invalid sample rate: %1").arg(output);
        }
        return false;
    }

    *sampleRateHz = parsedSampleRate;
    return true;
}

// Detects the channel count of the first audio stream via ffprobe. AAA always
// receives interleaved stereo s24le (sample-size-bytes 6 = 24-bit * 2ch), so the
// decode step must up-mix/down-mix to stereo first; this count selects the
// correct channel filter (mono has no FL/FR labels, so the stereo identity
// channelmap must not be used for it). Returns false on ffprobe failure.
bool detectAudioChannelCount(const QString &ffprobePath,
                             const QString &inputFilename,
                             int *channelCount,
                             QString *errorMessage)
{
    QString output;
    QString processError;
    const QStringList arguments = {
        QStringLiteral("-v"), QStringLiteral("error"),
        QStringLiteral("-select_streams"), QStringLiteral("a:0"),
        QStringLiteral("-show_entries"), QStringLiteral("stream=channels"),
        QStringLiteral("-of"), QStringLiteral("default=noprint_wrappers=1:nokey=1"),
        inputFilename
    };
    if (!runProcess(ffprobePath,
                    arguments,
                    QString(),
                    {},
                    AudioAlignmentUtil::CancelCallback(),
                    &output,
                    &processError)) {
        if (errorMessage) {
            *errorMessage = processError.isEmpty()
                ? QObject::tr("Unable to detect audio channel count using ffprobe.")
                : processError;
        }
        return false;
    }

    bool ok = false;
    const int parsedChannels = output.trimmed().toInt(&ok);
    if (!ok || parsedChannels <= 0) {
        if (errorMessage) {
            *errorMessage = QObject::tr("ffprobe returned an invalid channel count: %1").arg(output);
        }
        return false;
    }

    *channelCount = parsedChannels;
    return true;
}

bool isAudioInputExtensionSupported(const QString &suffix)
{
    return suffix == QStringLiteral("wav") || suffix == QStringLiteral("flac");
}

enum class AudioPreference {
    Any,
    Linear,
    Hifi
};

int commonPrefixLength(const QString &left, const QString &right)
{
    const int maxLength = qMin(left.size(), right.size());
    int index = 0;
    while (index < maxLength && left.at(index) == right.at(index)) {
        index++;
    }
    return index;
}

QString comparablePathForMatch(const QString &path)
{
    const QFileInfo info(path);
    const QString canonicalPath = info.canonicalFilePath();
    if (!canonicalPath.isEmpty()) {
        return canonicalPath;
    }
    return info.absoluteFilePath();
}
bool containsBasebandKeyword(const QString &audioBaseLower)
{
    return audioBaseLower.contains(QStringLiteral("baseband"))
        || audioBaseLower.contains(QStringLiteral("base_band"))
        || audioBaseLower.contains(QStringLiteral("base-band"))
        || audioBaseLower.contains(QStringLiteral("base band"));
}

bool hasExcludedAutoInputKeyword(const QString &audioFilenameLower)
{
    // Exclude aligned outputs and the RF/video payload dumps. The RF video
    // capture is commonly stored as <stem>-video.flac (and chroma as
    // <stem>-video_chroma.tbc, already filtered by extension). These use audio
    // containers but are not alignment inputs, so without excluding "video"
    // and "chroma" the Any-preference fallback would auto-target the RF dump
    // (verified against a real capture dir). The bare-substring style matches
    // the existing "rf"/"align" checks.
    return audioFilenameLower.contains(QStringLiteral("rf"))
        || audioFilenameLower.contains(QStringLiteral("align"))
        || audioFilenameLower.contains(QStringLiteral("aligned"))
        || audioFilenameLower.contains(QStringLiteral("video"))
        || audioFilenameLower.contains(QStringLiteral("chroma"));
}

bool hasPreferredKeyword(const QString &audioBaseLower, AudioPreference preference)
{
    if (preference == AudioPreference::Linear) {
        return audioBaseLower.contains(QStringLiteral("linear"))
            || containsBasebandKeyword(audioBaseLower);
    }
    if (preference == AudioPreference::Hifi) {
        return audioBaseLower.contains(QStringLiteral("hifi"));
    }
    return true;
}

bool isBasebandStereoCh1Ch2Name(const QString &audioBaseLower)
{
    return audioBaseLower.contains(QStringLiteral("baseband_stereo_ch1_ch2"))
        || (containsBasebandKeyword(audioBaseLower)
            && audioBaseLower.contains(QStringLiteral("stereo"))
            && audioBaseLower.contains(QStringLiteral("ch1"))
            && audioBaseLower.contains(QStringLiteral("ch2")));
}

bool isBasebandStereoCh3Ch4Name(const QString &audioBaseLower)
{
    return audioBaseLower.contains(QStringLiteral("baseband_stereo_ch3_ch4"))
        || (containsBasebandKeyword(audioBaseLower)
            && audioBaseLower.contains(QStringLiteral("stereo"))
            && audioBaseLower.contains(QStringLiteral("ch3"))
            && audioBaseLower.contains(QStringLiteral("ch4")));
}

int audioCandidateScore(const QFileInfo &audioInfo,
                        const QStringList &rootCandidatesLower,
                        AudioPreference preference)
{
    const QString audioBaseLower = audioInfo.completeBaseName().toLower();
    if (audioBaseLower.endsWith(QStringLiteral("_aligned"))) {
        return -1;
    }

    int bestRootScore = 0;
    for (const QString &rootCandidateLower : rootCandidatesLower) {
        if (rootCandidateLower.isEmpty()) {
            continue;
        }

        int scoreForRoot = 0;
        bool isRelatedToRoot = false;
        if (audioBaseLower == rootCandidateLower) {
            scoreForRoot = 1200;
            isRelatedToRoot = true;
        } else if (audioBaseLower.startsWith(rootCandidateLower + QStringLiteral("_"))
                   || audioBaseLower.startsWith(rootCandidateLower + QStringLiteral("-"))) {
            scoreForRoot = 950;
            isRelatedToRoot = true;
        } else if (audioBaseLower.startsWith(rootCandidateLower)) {
            scoreForRoot = 850;
            isRelatedToRoot = true;
        } else if (audioBaseLower.contains(rootCandidateLower)) {
            scoreForRoot = 700;
            isRelatedToRoot = true;
        }
        if (!isRelatedToRoot) {
            continue;
        }
        scoreForRoot += commonPrefixLength(audioBaseLower, rootCandidateLower);
        bestRootScore = qMax(bestRootScore, scoreForRoot);
    }

    if (bestRootScore <= 0) {
        return -1;
    }
    switch (preference) {
    case AudioPreference::Linear:
        if (isBasebandStereoCh1Ch2Name(audioBaseLower)) {
            bestRootScore += 780;
        }
        if (audioBaseLower.contains(QStringLiteral("linear"))) {
            bestRootScore += 340;
        }
        if (containsBasebandKeyword(audioBaseLower)) {
            bestRootScore += 320;
        }
        if (isBasebandStereoCh3Ch4Name(audioBaseLower)) {
            bestRootScore -= 220;
        }
        if (audioBaseLower.contains(QStringLiteral("hifi"))) {
            bestRootScore -= 260;
        }
        break;
    case AudioPreference::Hifi:
        if (audioBaseLower.contains(QStringLiteral("hifi_decoded"))) {
            bestRootScore += 420;
        } else if (audioBaseLower.contains(QStringLiteral("hifi"))) {
            bestRootScore += 300;
        }
        if (audioBaseLower.contains(QStringLiteral("linear"))
            || containsBasebandKeyword(audioBaseLower)) {
            bestRootScore -= 260;
        }
        break;
    case AudioPreference::Any:
    default:
        if (audioBaseLower.contains(QStringLiteral("hifi_decoded"))) {
            bestRootScore += 260;
        } else if (audioBaseLower.contains(QStringLiteral("hifi"))) {
            bestRootScore += 170;
        }
        if (audioBaseLower.contains(QStringLiteral("linear"))) {
            bestRootScore += 220;
        }
        if (containsBasebandKeyword(audioBaseLower)) {
            bestRootScore += 200;
        }
        break;
    }
    if (audioBaseLower.contains(QStringLiteral("audio"))) {
        bestRootScore += 70;
    }
    if (audioInfo.suffix().compare(QStringLiteral("flac"), Qt::CaseInsensitive) == 0) {
        bestRootScore += 30;
    }

    return bestRootScore;
}

QString autoDetectInputAudioFileInternal(const QString &jsonFilename,
                                         const QString &excludeFile,
                                         AudioPreference preference,
                                         bool requirePreferredKeyword)
{
    const QString normalizedJson = normalizeForPathParsing(jsonFilename);
    if (normalizedJson.isEmpty()) {
        return QString();
    }

    const QFileInfo jsonInfo(normalizedJson);
    if (!jsonInfo.exists() || !jsonInfo.isFile()) {
        return QString();
    }

    QStringList rootCandidates;
    appendDerivedRootCandidates(rootCandidates, jsonInfo.completeBaseName());

    QStringList rootCandidatesLower;
    for (const QString &rootCandidate : rootCandidates) {
        appendUniqueCandidate(rootCandidatesLower, rootCandidate.toLower());
    }

    const QString normalizedExclude = excludeFile.trimmed().isEmpty()
        ? QString()
        : comparablePathForMatch(excludeFile);

    const QDir directory(jsonInfo.absolutePath());
    const QFileInfoList entries = directory.entryInfoList(QDir::Files | QDir::Readable | QDir::NoSymLinks,
                                                          QDir::Name | QDir::IgnoreCase);
    int bestScore = -1;
    QFileInfo bestMatch;
    for (const QFileInfo &entryInfo : entries) {
        const QString suffixLower = entryInfo.suffix().toLower();
        if (!isAudioInputExtensionSupported(suffixLower)) {
            continue;
        }
        if (!normalizedExclude.isEmpty() && comparablePathForMatch(entryInfo.absoluteFilePath()) == normalizedExclude) {
            continue;
        }
        const QString audioFilenameLower = entryInfo.fileName().toLower();
        if (hasExcludedAutoInputKeyword(audioFilenameLower)) {
            continue;
        }

        const QString audioBaseLower = entryInfo.completeBaseName().toLower();
        if (requirePreferredKeyword && !hasPreferredKeyword(audioBaseLower, preference)) {
            continue;
        }

        const int candidateScore = audioCandidateScore(entryInfo, rootCandidatesLower, preference);
        int effectiveScore = candidateScore;
        if (effectiveScore <= 0
            && preference == AudioPreference::Linear
            && containsBasebandKeyword(audioBaseLower)) {
            effectiveScore = 260;
            if (isBasebandStereoCh1Ch2Name(audioBaseLower)) {
                effectiveScore += 120;
            }
            if (isBasebandStereoCh3Ch4Name(audioBaseLower)) {
                effectiveScore -= 80;
            }
            if (suffixLower == QStringLiteral("flac")) {
                effectiveScore += 30;
            }
        }

        if (effectiveScore > bestScore) {
            bestScore = effectiveScore;
            bestMatch = entryInfo;
        }
    }

    if (bestScore <= 0 || !bestMatch.exists()) {
        return QString();
    }

    return QDir::toNativeSeparators(bestMatch.absoluteFilePath());
}

// Reads a positive sample-rate value from a JSON object, accepting either an
// integer or a numeric string (some metadata writers serialise rates as
// strings). Returns 0 if the field is absent or not a usable positive number.
quint32 positiveRateValueFromJson(const QJsonObject &object, const QString &key)
{
    if (object.isEmpty() || key.isEmpty()) {
        return 0;
    }
    const QJsonValue value = object.value(key);
    if (value.isDouble()) {
        const quint32 parsed = static_cast<quint32>(value.toDouble(0.0));
        return parsed == 0 ? 0 : parsed;
    }
    if (value.isString()) {
        bool ok = false;
        const quint32 parsed = value.toString().trimmed().toUInt(&ok);
        return (ok && parsed > 0) ? parsed : 0;
    }
    return 0;
}
} // namespace

namespace AudioAlignmentUtil {
QString normalizePathForCurrentPlatform(const QString &path)
{
    const QString normalized = normalizeForPathParsing(path);
    if (normalized.isEmpty()) {
        return QString();
    }
    return QDir::toNativeSeparators(normalized);
}

QString defaultAlignedOutputPath(const QString &inputFilename)
{
    const QString normalizedInput = normalizeForPathParsing(inputFilename);
    if (normalizedInput.isEmpty()) {
        return QString();
    }

    const QFileInfo inputInfo(normalizedInput);
    QString completeBaseName = inputInfo.completeBaseName().trimmed();
    if (completeBaseName.isEmpty()) {
        completeBaseName = QStringLiteral("audio");
    }
    if (!completeBaseName.endsWith(QStringLiteral("_aligned"), Qt::CaseInsensitive)) {
        completeBaseName += QStringLiteral("_aligned");
    }

    const QString outputFileName = completeBaseName + QStringLiteral(".flac");
    const QString outputPath = QDir(inputInfo.absolutePath()).filePath(outputFileName);
    return QDir::toNativeSeparators(outputPath);
}
QString autoDetectInputAudioFile(const QString &jsonFilename, const QString &excludeFile)
{
    return autoDetectInputAudioFileInternal(jsonFilename, excludeFile, AudioPreference::Any, false);
}

QString autoDetectLinearInputAudioFile(const QString &jsonFilename, const QString &excludeFile)
{
    // Only fill the Linear field with an actual linear/baseband track. Do not
    // cross-fill the hifi track (or any other root-related file) when no linear
    // track is present — leave the field empty so the user chooses explicitly.
    // Preferred-detect already accepts both the "linear" keyword and baseband
    // variants, and the baseband fallback block inside the internal detector
    // still rescues unrelated baseband files, so no weaker fallback is needed.
    return autoDetectInputAudioFileInternal(jsonFilename, excludeFile, AudioPreference::Linear, true);
}

QString autoDetectHifiInputAudioFile(const QString &jsonFilename, const QString &excludeFile)
{
    // Only fill the HiFi field with an actual hifi track. Do not cross-fill the
    // linear/baseband track when no hifi track is present — leave the field
    // empty so the user chooses explicitly.
    return autoDetectInputAudioFileInternal(jsonFilename, excludeFile, AudioPreference::Hifi, true);
}

quint32 detectRfSourceSampleRateFromJson(const QString &jsonFilename)
{
    // Boundary rule (user-provided): videoParameters.sampleRate is the DECODED
    // .tbc format rate, NOT the source RF capture timebase that AAA uses for
    // alignment (they differ on resampled captures — e.g. decoded 4Fsc PAL
    // 17.7 MHz vs source cxadc 40 Msps). This function therefore only returns
    // an EXPLICIT RF-source field when the metadata carries one; it must NEVER
    // fall back to videoParameters.sampleRate. Returns 0 when no explicit
    // RF-source field is present, leaving the dialog at its 40 MHz default.
    const QString normalizedJson = normalizeForPathParsing(jsonFilename);
    if (normalizedJson.isEmpty()) {
        return 0;
    }
    const QFileInfo jsonInfo(normalizedJson);
    if (!jsonInfo.exists() || !jsonInfo.isFile()) {
        return 0;
    }

    QFile jsonFile(jsonInfo.absoluteFilePath());
    if (!jsonFile.open(QIODevice::ReadOnly)) {
        return 0;
    }
    const QJsonDocument document = QJsonDocument::fromJson(jsonFile.readAll());
    jsonFile.close();
    if (!document.isObject()) {
        return 0;
    }

    const QJsonObject root = document.object();
    const QJsonValue videoParametersValue = root.value(QStringLiteral("videoParameters"));
    if (!videoParametersValue.isObject()) {
        return 0;
    }
    const QJsonObject videoParameters = videoParametersValue.toObject();

    // Recognised forward-compatible RF-source field names (Hz), checked in
    // priority order. Names are matched exactly here; JSON object keys are
    // case-sensitive in practice for ld-decode metadata, so no case folding.
    const QStringList rfSourceKeys = {
        QStringLiteral("rfSourceSampleRate"),
        QStringLiteral("rfSourceSampleRateHz"),
        QStringLiteral("rfSourceFreq"),
        QStringLiteral("rfSampleRate")
    };
    for (const QString &key : rfSourceKeys) {
        const quint32 parsed = positiveRateValueFromJson(videoParameters, key);
        if (parsed > 0) {
            return parsed;
        }
    }

    // Deliberately do NOT fall back to videoParameters.sampleRate — see the
    // boundary rule in the comment above.
    return 0;
}

QString resolvedAudioAlignPath(QString *errorMessage)
{
    if (errorMessage) {
        errorMessage->clear();
    }
    const QString toolPath = resolveAudioAlignExecutablePath();
    if (toolPath.isEmpty()) {
        if (errorMessage) {
            *errorMessage = QObject::tr("VhsDecodeAutoAudioAlign executable not found in PATH or vendored locations.");
        }
    }
    return toolPath;
}

QStringList audioAlignRunnerCommand(QString *errorMessage)
{
    if (errorMessage) {
        errorMessage->clear();
    }
    QString program;
    QStringList prefixArguments;
    if (!resolveRunner(&program, &prefixArguments, errorMessage)) {
        return {};
    }
    QStringList runnerCommand;
    runnerCommand.append(program);
    runnerCommand.append(prefixArguments);
    return runnerCommand;
}

bool runStreamAlign(const QString &jsonFilename,
                    const QString &inputFilename,
                    const QString &outputFilename,
                    quint32 rfVideoSampleRateHz,
                    bool overwriteOutput,
                    const ProgressCallback &progressCallback,
                    const CancelCallback &cancelCallback,
                    QString *errorMessage,
                    bool convertMonoToStereo)
{
    if (errorMessage) {
        errorMessage->clear();
    }
    auto cancellationRequested = [&cancelCallback]() -> bool {
        return cancelCallback && cancelCallback();
    };
    auto setCancelledError = [errorMessage]() {
        if (errorMessage) {
            *errorMessage = QObject::tr("Operation cancelled by user.");
        }
    };
    if (cancellationRequested()) {
        setCancelledError();
        return false;
    }
    emitProgress(progressCallback, 0, QObject::tr("Preparing alignment..."));

    const QString normalizedJson = QFileInfo(normalizePathForCurrentPlatform(jsonFilename)).absoluteFilePath();
    const QString normalizedInput = QFileInfo(normalizePathForCurrentPlatform(inputFilename)).absoluteFilePath();
    QString normalizedOutput = QFileInfo(normalizePathForCurrentPlatform(outputFilename)).absoluteFilePath();
    if (normalizedOutput.isEmpty()) {
        normalizedOutput = defaultAlignedOutputPath(normalizedInput);
    }

    if (normalizedJson.isEmpty() || !QFileInfo::exists(normalizedJson)) {
        if (errorMessage) {
            *errorMessage = QObject::tr("Metadata JSON not found:\n%1").arg(normalizedJson);
        }
        return false;
    }
    if (normalizedInput.isEmpty() || !QFileInfo::exists(normalizedInput)) {
        if (errorMessage) {
            *errorMessage = QObject::tr("Input audio file not found:\n%1").arg(normalizedInput);
        }
        return false;
    }
    if (QFileInfo::exists(normalizedOutput) && !overwriteOutput) {
        if (errorMessage) {
            *errorMessage = QObject::tr("Output file exists and overwrite is disabled:\n%1").arg(normalizedOutput);
        }
        return false;
    }

    const QString outputDirectory = QFileInfo(normalizedOutput).absolutePath();
    if (!outputDirectory.isEmpty() && !QDir().mkpath(outputDirectory)) {
        if (errorMessage) {
            *errorMessage = QObject::tr("Unable to create output directory:\n%1").arg(outputDirectory);
        }
        return false;
    }

    const QString ffmpegPath = resolveFfmpegPath();
    if (ffmpegPath.isEmpty()) {
        if (errorMessage) {
            *errorMessage = QObject::tr("ffmpeg is required but was not found in bundled tool locations or PATH.");
        }
        return false;
    }
    const QString ffprobePath = resolveFfprobePath();
    if (ffprobePath.isEmpty()) {
        if (errorMessage) {
            *errorMessage = QObject::tr("ffprobe is required but was not found in bundled tool locations or PATH.");
        }
        return false;
    }

    QString alignProgram;
    QStringList alignLauncherPrefix;
    if (!resolveRunner(&alignProgram, &alignLauncherPrefix, errorMessage)) {
        return false;
    }

    quint32 streamSampleRateHz = 0;
    if (!detectAudioSampleRateHz(ffprobePath, normalizedInput, &streamSampleRateHz, errorMessage)) {
        return false;
    }
    int streamChannelCount = 0;
    if (!detectAudioChannelCount(ffprobePath, normalizedInput, &streamChannelCount, errorMessage)) {
        return false;
    }
    if (cancellationRequested()) {
        setCancelledError();
        return false;
    }
    emitProgress(progressCallback, 5, QObject::tr("Preparation complete."));

    QTemporaryDir temporaryDirectory;
    if (!temporaryDirectory.isValid()) {
        if (errorMessage) {
            *errorMessage = QObject::tr("Unable to create temporary directory for alignment processing.");
        }
        return false;
    }
    const QString rawInputPath = QDir(temporaryDirectory.path()).filePath(QStringLiteral("audio_input.s24le"));
    const QString rawAlignedPath = QDir(temporaryDirectory.path()).filePath(QStringLiteral("audio_aligned.s24le"));

    QString processOutput;
    QString processError;

    // AAA aligns interleaved s24le; one "sample" is sample-size-bytes (24-bit
    // * outputChannels). By default mono is up-mixed to stereo (2ch, 6 bytes) so
    // AAA receives its expected stereo feed. When the caller opts to keep a mono
    // input mono (convertMonoToStereo=false), the decode emits 1ch (3 bytes) and
    // AAA is invoked with --sample-size-bytes 3. The legacy identity remap
    // channelmap=map=FL-FL|FR-FR assumes the input already exposes FL/FR and
    // fails on mono ("input channel 'FL' not available from input layout
    // 'mono'") — the exact regression reported on a mono linear-audio capture.
    // Up-mix mono->stereo with pan (referencing the single channel by index c0
    // so it is layout-agnostic) when conversion is requested; pass mono through
    // unchanged (no channel filter) when it is not; keep the identity remap for
    // multi-channel inputs that already expose FL/FR.
    const bool inputIsMono = (streamChannelCount == 1);
    const bool keepMono = inputIsMono && !convertMonoToStereo;
    const int outputChannels = keepMono ? 1 : 2;
    const int sampleSizeBytes = outputChannels * 3; // 24-bit samples

    QString decodeChannelFilter;
    if (inputIsMono) {
        decodeChannelFilter = keepMono
            ? QString()
            : QStringLiteral("pan=stereo|FL=c0|FR=c0");
    } else {
        decodeChannelFilter = QStringLiteral("channelmap=map=FL-FL|FR-FR");
    }

    QStringList decodeArguments;
    decodeArguments << QStringLiteral("-y")
                    << QStringLiteral("-i") << normalizedInput;
    if (!decodeChannelFilter.isEmpty()) {
        decodeArguments << QStringLiteral("-filter_complex") << decodeChannelFilter;
    }
    decodeArguments << QStringLiteral("-f") << QStringLiteral("s24le")
                    << QStringLiteral("-ac") << QString::number(outputChannels)
                    << QStringLiteral("-ar") << QString::number(streamSampleRateHz)
                    << rawInputPath;
    emitProgress(progressCallback, 10, QObject::tr("Decoding input audio..."));
    if (!runProcess(ffmpegPath, decodeArguments, QString(),
                    [&](const QString &outputLine) {
                        const int stagePercent = extractPercentFromOutputLine(outputLine);
                        if (stagePercent >= 0) {
                            emitProgress(progressCallback,
                                         10 + ((stagePercent * 20) / 100),
                                         QObject::tr("Decoding input audio..."));
                        }
                    },
                    cancelCallback,
                    &processOutput,
                    &processError)) {
        if (errorMessage) {
            *errorMessage = processError.isEmpty()
                ? QObject::tr("Failed to decode input audio for alignment.")
                : processError;
        }
        return false;
    }
    emitProgress(progressCallback, 30, QObject::tr("Decoded input audio."));

    QStringList alignArguments = alignLauncherPrefix;
    alignArguments << QStringLiteral("stream-align")
                   << QStringLiteral("--sample-size-bytes") << QString::number(sampleSizeBytes)
                   << QStringLiteral("--stream-sample-rate-hz") << QString::number(streamSampleRateHz)
                   << QStringLiteral("--json") << normalizedJson
                   << QStringLiteral("--rf-video-sample-rate-hz") << QString::number(rfVideoSampleRateHz)
                   << QStringLiteral("--input-file") << rawInputPath
                   << QStringLiteral("--output-file") << rawAlignedPath
                   << QStringLiteral("--overwrite");
    emitProgress(progressCallback, 35, QObject::tr("Aligning audio stream..."));
    if (!runProcess(alignProgram, alignArguments, QString(),
                    [&](const QString &outputLine) {
                        const int stagePercent = extractPercentFromOutputLine(outputLine);
                        if (stagePercent >= 0) {
                            emitProgress(progressCallback,
                                         35 + ((stagePercent * 55) / 100),
                                         QObject::tr("Aligning audio stream..."));
                        }
                    },
                    cancelCallback,
                    &processOutput,
                    &processError)) {
        if (errorMessage) {
            *errorMessage = processError.isEmpty()
                ? QObject::tr("VhsDecodeAutoAudioAlign failed during stream alignment.")
                : processError;
        }
        return false;
    }
    emitProgress(progressCallback, 90, QObject::tr("Audio stream alignment complete."));

    QStringList encodeArguments = {
        QStringLiteral("-y"),
        QStringLiteral("-f"), QStringLiteral("s24le"),
        QStringLiteral("-ar"), QString::number(streamSampleRateHz),
        QStringLiteral("-ac"), QString::number(outputChannels),
        QStringLiteral("-i"), rawAlignedPath
    };
    if (streamSampleRateHz != 48000) {
        encodeArguments << QStringLiteral("-af") << QStringLiteral("aresample=48000");
    }
    encodeArguments << QStringLiteral("-sample_fmt") << QStringLiteral("s32")
                    << normalizedOutput;
    emitProgress(progressCallback, 92, QObject::tr("Encoding aligned output..."));
    if (!runProcess(ffmpegPath, encodeArguments, QString(),
                    [&](const QString &outputLine) {
                        const int stagePercent = extractPercentFromOutputLine(outputLine);
                        if (stagePercent >= 0) {
                            emitProgress(progressCallback,
                                         92 + ((stagePercent * 8) / 100),
                                         QObject::tr("Encoding aligned output..."));
                        }
                    },
                    cancelCallback,
                    &processOutput,
                    &processError)) {
        if (errorMessage) {
            *errorMessage = processError.isEmpty()
                ? QObject::tr("Failed to encode aligned audio output.")
                : processError;
        }
        return false;
    }

    if (!QFileInfo::exists(normalizedOutput)) {
        if (errorMessage) {
            *errorMessage = QObject::tr("Alignment completed but output file was not created:\n%1")
                                .arg(normalizedOutput);
        }
        return false;
    }
    emitProgress(progressCallback, 100, QObject::tr("Alignment complete."));

    return true;
}
} // namespace AudioAlignmentUtil
