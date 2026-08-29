#include "audioalignmentutil.h"

#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QStandardPaths>
#include <QStringList>
#include <QTemporaryDir>
#include <QTextStream>

namespace {
constexpr int kSkipExitCode = 77;

int skipTest(const QString &message)
{
    QTextStream(stdout) << "SKIP: " << message << Qt::endl;
    return kSkipExitCode;
}

int failTest(const QString &message)
{
    QTextStream(stderr) << "FAIL: " << message << Qt::endl;
    return 1;
}
} // namespace

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("test-audio-align-runtime"));

    QCommandLineParser parser;
    parser.setApplicationDescription(
        QStringLiteral("Runtime AAA test using fixture JSON/audio data."));
    parser.addHelpOption();

    QCommandLineOption jsonOption(
        QStringList() << QStringLiteral("json"),
        QStringLiteral("Input metadata JSON fixture path."),
        QStringLiteral("path"));
    parser.addOption(jsonOption);

    QCommandLineOption inputAudioOption(
        QStringList() << QStringLiteral("input-audio"),
        QStringLiteral("Input audio fixture path."),
        QStringLiteral("path"));
    parser.addOption(inputAudioOption);

    QCommandLineOption outputAudioOption(
        QStringList() << QStringLiteral("output-audio"),
        QStringLiteral("Output aligned audio path."),
        QStringLiteral("path"));
    parser.addOption(outputAudioOption);

    QCommandLineOption rfVideoSampleRateOption(
        QStringList() << QStringLiteral("rf-video-sample-rate-hz"),
        QStringLiteral("RF video sample rate in Hz."),
        QStringLiteral("hz"),
        QStringLiteral("40000000"));
    parser.addOption(rfVideoSampleRateOption);

    QCommandLineOption detectAaaOption(
        QStringList() << QStringLiteral("detect-aaa"),
        QStringLiteral("Resolve the VhsDecodeAutoAudioAlign executable the way ld-analyse does "
                       "(application-dir-relative vendor lookup, then PATH) and launch it with "
                       "show-build-info to confirm it is actually found and runnable. Exits 0 on "
                       "success, 1 if not found/launchable. No fixtures or ffmpeg required."));
    parser.addOption(detectAaaOption);

    QCommandLineOption detectInputsOption(
        QStringList() << QStringLiteral("detect-inputs"),
        QStringLiteral("Auto-detect selection test: create a temp capture dir with the standard "
                       "vhs-decode naming (<stem>-video.tbc.json + <stem>-linear.flac + "
                       "<stem>-hifi.flac + <stem>-video.flac) and assert the linear/hifi detectors "
                       "pick the right tracks and never the RF <stem>-video.flac. No ffmpeg/AAA "
                       "required. Exits 0 on success, 1 on failure."));
    parser.addOption(detectInputsOption);

    QCommandLineOption detectRfSourceRateOption(
        QStringList() << QStringLiteral("detect-rf-source-rate"),
        QStringLiteral("RF source-rate provision test: assert detectRfSourceSampleRateFromJson "
                       "returns an explicit videoParameters.rfSourceSampleRate value when present, "
                       "and returns 0 (no fallback to the decoded videoParameters.sampleRate) when "
                       "no RF-source field is present. No ffmpeg/AAA required. Exits 0 on success, "
                       "1 on failure."));
    parser.addOption(detectRfSourceRateOption);

    QCommandLineOption detectAaaEnvBinOption(
        QStringList() << QStringLiteral("detect-aaa-envbin"),
        QStringLiteral("Bundled-resolution override test: stage a fake bundled AAA AppImage under a "
                       "temp dir's vendor path, export TBC_TOOLS_APP_BIN_DIR to that dir, and assert "
                       "resolvedAudioAlignPath finds it there. This covers the Linux AppImage/loader-"
                       "wrapper case where applicationDirPath() points at the bundled loader's dir "
                       "(usr/lib) instead of the binaries' dir (usr/bin). No ffmpeg/AAA launch "
                       "required. Exits 0 on success, 1 on failure."));
    parser.addOption(detectAaaEnvBinOption);

    parser.process(app);

    if (parser.isSet(detectAaaOption)) {
        // Detection (must succeed): the resolver must find the AAA executable
        // the same way ld-analyse does (application-dir-relative vendor lookup,
        // then PATH). This is the core check — a broken bundle layout or
        // resolver regression makes this FAIL, not skip.
        QString detectError;
        const QString resolvedPath = AudioAlignmentUtil::resolvedAudioAlignPath(&detectError);
        if (resolvedPath.isEmpty()) {
            return failTest(QStringLiteral("AAA not detected: %1").arg(detectError));
        }
        QTextStream(stdout) << "Detected AAA: " << resolvedPath << Qt::endl;

        // Launch (best-effort): build the runner argv. If the resolved tool is
        // the .exe and mono is not on PATH (e.g. a build tree without Mono
        // installed), the runner cannot be built — SKIP rather than fail,
        // since detection itself succeeded and launchability is validated
        // against the self-contained AppImage in the bundle verifier instead.
        QString runnerError;
        const QStringList runnerCommand = AudioAlignmentUtil::audioAlignRunnerCommand(&runnerError);
        if (runnerCommand.isEmpty()) {
            return skipTest(
                QStringLiteral("AAA detected at %1 but not launchable here: %2")
                    .arg(resolvedPath, runnerError));
        }
        QTextStream(stdout) << "Runner: " << runnerCommand.join(QLatin1Char(' ')) << Qt::endl;

        // Launch AAA exactly as resolveRunner would (program + prefix args),
        // appending show-build-info, and confirm it actually runs.
        QProcess aaaProcess;
        aaaProcess.setProcessChannelMode(QProcess::MergedChannels);
        QStringList arguments = runnerCommand;
        const QString program = arguments.takeFirst();
        arguments << QStringLiteral("show-build-info");
        aaaProcess.start(program, arguments);
        if (!aaaProcess.waitForStarted(5000)) {
            return failTest(QStringLiteral("Could not start AAA runner: %1").arg(program));
        }
        if (!aaaProcess.waitForFinished(60000)
            || aaaProcess.exitStatus() != QProcess::NormalExit
            || aaaProcess.exitCode() != 0) {
            return failTest(QStringLiteral("AAA runner did not complete successfully (exit=%1):\n%2")
                                .arg(aaaProcess.exitCode())
                                .arg(QString::fromLocal8Bit(aaaProcess.readAllStandardOutput())));
        }
        const QString aaaOutput = QString::fromLocal8Bit(aaaProcess.readAllStandardOutput());
        if (!aaaOutput.contains(QStringLiteral("audio"), Qt::CaseInsensitive)) {
            return failTest(QStringLiteral("AAA show-build-info did not report audio (output did not contain 'audio'):\n%1")
                                .arg(aaaOutput));
        }
    QTextStream(stdout) << "AAA detection + launch test passed." << Qt::endl;
    return 0;
}

    if (parser.isSet(detectInputsOption)) {
        // Build a temp capture dir mirroring the standard vhs-decode naming:
        //   <stem>-video.tbc.json  (metadata)
        //   <stem>-linear.flac     (linear/baseband audio input)
        //   <stem>-hifi.flac       (hifi audio input)
        //   <stem>-video.flac      (RF video dump in an audio container — must NOT be picked)
        // Empty files are enough; the detectors only inspect names/paths.
        QTemporaryDir tempDir;
        if (!tempDir.isValid()) {
            return failTest(QStringLiteral("Could not create temp dir for detect-inputs test."));
        }
        const QString stem = QStringLiteral("capture_demo_2026-01-01_12_00_00");
        const QString dirPath = tempDir.path();
        const QString jsonPath = dirPath + QStringLiteral("/") + stem + QStringLiteral("-video.tbc.json");
        const QString linearPath = dirPath + QStringLiteral("/") + stem + QStringLiteral("-linear.flac");
        const QString hifiPath = dirPath + QStringLiteral("/") + stem + QStringLiteral("-hifi.flac");
        const QString rfVideoPath = dirPath + QStringLiteral("/") + stem + QStringLiteral("-video.flac");
        const QStringList createPaths = {jsonPath, linearPath, hifiPath, rfVideoPath};
        for (const QString &path : createPaths) {
            QFile f(path);
            if (!f.open(QIODevice::WriteOnly)) {
                return failTest(QStringLiteral("Could not create fixture file: %1").arg(path));
            }
            f.close();
        }

        const QString detectedLinear = AudioAlignmentUtil::autoDetectLinearInputAudioFile(jsonPath);
        if (detectedLinear.isEmpty()) {
            return failTest(QStringLiteral("Linear auto-detect returned nothing for %1").arg(jsonPath));
        }
        if (!detectedLinear.endsWith(QStringLiteral("-linear.flac"), Qt::CaseInsensitive)) {
            return failTest(QStringLiteral("Linear auto-detect picked the wrong file: %1").arg(detectedLinear));
        }
        if (detectedLinear.compare(rfVideoPath, Qt::CaseInsensitive) == 0) {
            return failTest(QStringLiteral("Linear auto-detect mistakenly targeted the RF video dump: %1").arg(detectedLinear));
        }

        const QString detectedHifi = AudioAlignmentUtil::autoDetectHifiInputAudioFile(jsonPath, detectedLinear);
        if (detectedHifi.isEmpty()) {
            return failTest(QStringLiteral("Hifi auto-detect returned nothing for %1").arg(jsonPath));
        }
        if (!detectedHifi.endsWith(QStringLiteral("-hifi.flac"), Qt::CaseInsensitive)) {
            return failTest(QStringLiteral("Hifi auto-detect picked the wrong file: %1").arg(detectedHifi));
        }
        if (detectedHifi.compare(rfVideoPath, Qt::CaseInsensitive) == 0) {
            return failTest(QStringLiteral("Hifi auto-detect mistakenly targeted the RF video dump: %1").arg(detectedHifi));
        }

        // The Any fallback must also never return the RF video dump.
        const QString detectedAny = AudioAlignmentUtil::autoDetectInputAudioFile(jsonPath);
        if (!detectedAny.isEmpty()
            && detectedAny.compare(rfVideoPath, Qt::CaseInsensitive) == 0) {
            return failTest(QStringLiteral("Any auto-detect mistakenly targeted the RF video dump: %1").arg(detectedAny));
        }

        // No cross-fill: when only the hifi track is present (no linear file),
        // the Linear detector must return empty rather than filling in hifi;
        // and vice-versa. Each field only fills with its own track type.
        QTemporaryDir hifiOnlyDir;
        if (!hifiOnlyDir.isValid()) {
            return failTest(QStringLiteral("Could not create temp dir for hifi-only detect test."));
        }
        const QString hifiOnlyJson = hifiOnlyDir.path() + QStringLiteral("/") + stem + QStringLiteral("-video.tbc.json");
        const QString hifiOnlyHifi = hifiOnlyDir.path() + QStringLiteral("/") + stem + QStringLiteral("-hifi.flac");
        const QString hifiOnlyVideo = hifiOnlyDir.path() + QStringLiteral("/") + stem + QStringLiteral("-video.flac");
        for (const QString &path : {hifiOnlyJson, hifiOnlyHifi, hifiOnlyVideo}) {
            QFile f(path);
            if (!f.open(QIODevice::WriteOnly)) {
                return failTest(QStringLiteral("Could not create hifi-only fixture: %1").arg(path));
            }
            f.close();
        }
        const QString linearWhenHifiOnly = AudioAlignmentUtil::autoDetectLinearInputAudioFile(hifiOnlyJson);
        if (!linearWhenHifiOnly.isEmpty()) {
            return failTest(QStringLiteral("Linear auto-detect must stay empty when no linear track exists, but picked: %1").arg(linearWhenHifiOnly));
        }
        const QString hifiWhenHifiOnly = AudioAlignmentUtil::autoDetectHifiInputAudioFile(hifiOnlyJson);
        if (hifiWhenHifiOnly.isEmpty() || !hifiWhenHifiOnly.endsWith(QStringLiteral("-hifi.flac"), Qt::CaseInsensitive)) {
            return failTest(QStringLiteral("Hifi auto-detect should still find the hifi track when no linear exists, got: %1").arg(hifiWhenHifiOnly));
        }

        QTemporaryDir linearOnlyDir;
        if (!linearOnlyDir.isValid()) {
            return failTest(QStringLiteral("Could not create temp dir for linear-only detect test."));
        }
        const QString linearOnlyJson = linearOnlyDir.path() + QStringLiteral("/") + stem + QStringLiteral("-video.tbc.json");
        const QString linearOnlyLinear = linearOnlyDir.path() + QStringLiteral("/") + stem + QStringLiteral("-linear.flac");
        const QString linearOnlyVideo = linearOnlyDir.path() + QStringLiteral("/") + stem + QStringLiteral("-video.flac");
        for (const QString &path : {linearOnlyJson, linearOnlyLinear, linearOnlyVideo}) {
            QFile f(path);
            if (!f.open(QIODevice::WriteOnly)) {
                return failTest(QStringLiteral("Could not create linear-only fixture: %1").arg(path));
            }
            f.close();
        }
        const QString hifiWhenLinearOnly = AudioAlignmentUtil::autoDetectHifiInputAudioFile(linearOnlyJson);
        if (!hifiWhenLinearOnly.isEmpty()) {
            return failTest(QStringLiteral("Hifi auto-detect must stay empty when no hifi track exists, but picked: %1").arg(hifiWhenLinearOnly));
        }
        const QString linearWhenLinearOnly = AudioAlignmentUtil::autoDetectLinearInputAudioFile(linearOnlyJson);
        if (linearWhenLinearOnly.isEmpty() || !linearWhenLinearOnly.endsWith(QStringLiteral("-linear.flac"), Qt::CaseInsensitive)) {
            return failTest(QStringLiteral("Linear auto-detect should still find the linear track when no hifi exists, got: %1").arg(linearWhenLinearOnly));
        }

        QTextStream(stdout) << "AAA auto-detect inputs test passed. linear="
                            << detectedLinear << " hifi=" << detectedHifi << Qt::endl;
        return 0;
    }

    if (parser.isSet(detectRfSourceRateOption)) {
        // Case 1: metadata carries an explicit RF-source field -> it is returned.
        QTemporaryDir tempDir1;
        if (!tempDir1.isValid()) {
            return failTest(QStringLiteral("Could not create temp dir for detect-rf-source-rate test (case 1)."));
        }
        const QString jsonWithRf = tempDir1.path() + QStringLiteral("/with_rf.json");
        {
            QFile f(jsonWithRf);
            if (!f.open(QIODevice::WriteOnly)) {
                return failTest(QStringLiteral("Could not write with_rf.json fixture."));
            }
            f.write("{\"videoParameters\":{\"system\":\"PAL\",\"sampleRate\":17734475,\"rfSourceSampleRate\":40000000}}");
            f.close();
        }
        const quint32 detectedWithRf = AudioAlignmentUtil::detectRfSourceSampleRateFromJson(jsonWithRf);
        if (detectedWithRf != 40000000) {
            return failTest(QStringLiteral("detectRfSourceSampleRateFromJson should return 40000000 from rfSourceSampleRate, got %1").arg(detectedWithRf));
        }

        // Case 2: metadata has only the decoded sampleRate, no RF-source field
        //         -> must return 0 (NO fallback to the decoded rate).
        QTemporaryDir tempDir2;
        if (!tempDir2.isValid()) {
            return failTest(QStringLiteral("Could not create temp dir for detect-rf-source-rate test (case 2)."));
        }
        const QString jsonWithoutRf = tempDir2.path() + QStringLiteral("/without_rf.json");
        {
            QFile f(jsonWithoutRf);
            if (!f.open(QIODevice::WriteOnly)) {
                return failTest(QStringLiteral("Could not write without_rf.json fixture."));
            }
            f.write("{\"videoParameters\":{\"system\":\"PAL\",\"sampleRate\":17734475}}");
            f.close();
        }
        const quint32 detectedWithoutRf = AudioAlignmentUtil::detectRfSourceSampleRateFromJson(jsonWithoutRf);
        if (detectedWithoutRf != 0) {
            return failTest(QStringLiteral("detectRfSourceSampleRateFromJson must return 0 when no RF-source field is present (no fallback to decoded sampleRate), got %1").arg(detectedWithoutRf));
        }

        QTextStream(stdout) << "AAA RF source-rate provision test passed. with_rf="
                            << detectedWithRf << " without_rf=" << detectedWithoutRf << Qt::endl;
        return 0;
    }

    if (parser.isSet(detectAaaEnvBinOption)) {
        // Reproduces the Linux AppImage loader-wrapper condition: the bundled
        // glibc loader launches the real ELF, so QCoreApplication::applicationDirPath()
        // returns the loader's dir (usr/lib) rather than the binaries' dir (usr/bin)
        // where the vendored AAA lives. The resolver honors an explicit
        // TBC_TOOLS_APP_BIN_DIR override (and a ../bin heuristic) to find the
        // bundled AAA regardless. Stage a fake bundled AppImage under a temp
        // dir's vendor path, point TBC_TOOLS_APP_BIN_DIR at that dir, and assert
        // resolvedAudioAlignPath reaches it. qputenv must run before
        // QCoreApplication exists (it already does at this point); the env var
        // is read lazily inside resolveAudioAlignExecutablePath.
        QTemporaryDir stageDir;
        if (!stageDir.isValid()) {
            return failTest(QStringLiteral("Could not create temp dir for detect-aaa-envbin test."));
        }
        const QString binDir = stageDir.path() + QStringLiteral("/bin");
        const QString vendorDir = binDir + QStringLiteral("/vendor/vhs_decode_auto_audio_align");
        if (!QDir().mkpath(vendorDir)) {
            return failTest(QStringLiteral("Could not create staged vendor dir: %1").arg(vendorDir));
        }
        const QString stagedAppImage = vendorDir + QStringLiteral("/vhs-decode-aaa.AppImage");
        {
            QFile f(stagedAppImage);
            if (!f.open(QIODevice::WriteOnly)) {
                return failTest(QStringLiteral("Could not create staged AAA AppImage: %1").arg(stagedAppImage));
            }
            f.write("#!/bin/sh\necho fake-aaa\n");
            f.close();
        }
        if (!QFile::setPermissions(stagedAppImage,
                QFile::permissions(stagedAppImage) | QFile::ExeOwner | QFile::ExeUser | QFile::ExeGroup | QFile::ExeOther)) {
            return failTest(QStringLiteral("Could not make staged AAA AppImage executable."));
        }
        // Export the override so the resolver probes binDir/vendor/... even though
        // applicationDirPath() (this test binary's dir) is unrelated to stageDir.
        const QByteArray envBin = binDir.toUtf8();
        qputenv("TBC_TOOLS_APP_BIN_DIR", envBin.constData());

        // The env-bin vendor candidate (envBinDir/vendor/vhs_decode_auto_audio_align)
        // is appended to the resolver's candidate list before the AUDIO_ALIGN_VENDOR_DIR
        // candidate, and no earlier candidate in the ctest environment ships an
        // AppImage, so the resolver must return the staged AppImage. A regression that
        // drops the env-bin candidate makes this fail.
        QString detectError;
        const QString resolvedPath = AudioAlignmentUtil::resolvedAudioAlignPath(&detectError);
        if (resolvedPath.isEmpty()) {
            return failTest(QStringLiteral("resolvedAudioAlignPath returned nothing with TBC_TOOLS_APP_BIN_DIR=%1: %2").arg(binDir, detectError));
        }
        if (resolvedPath.compare(stagedAppImage, Qt::CaseInsensitive) != 0) {
            return failTest(QStringLiteral("resolvedAudioAlignPath should return the staged env-bin AAA (%1), got %2")
                                .arg(stagedAppImage, resolvedPath));
        }
        QTextStream(stdout) << "AAA env-bin bundled-resolution test passed. resolved=" << resolvedPath << Qt::endl;
        return 0;
    }

    const QString jsonPath = parser.value(jsonOption).trimmed();
    const QString inputAudioPath = parser.value(inputAudioOption).trimmed();
    const QString outputAudioPath = parser.value(outputAudioOption).trimmed();
    bool rfOk = false;
    const quint32 rfVideoSampleRateHz = parser.value(rfVideoSampleRateOption).toUInt(&rfOk);

    if (jsonPath.isEmpty() || inputAudioPath.isEmpty() || outputAudioPath.isEmpty() || !rfOk
        || rfVideoSampleRateHz == 0) {
        return failTest(
            QStringLiteral("Missing required arguments (--json, --input-audio, --output-audio, --rf-video-sample-rate-hz)."));
    }

    if (!QFileInfo::exists(jsonPath) || !QFileInfo(jsonPath).isFile()) {
        return failTest(QStringLiteral("JSON fixture not found: %1").arg(jsonPath));
    }
    if (!QFileInfo::exists(inputAudioPath) || !QFileInfo(inputAudioPath).isFile()) {
        return failTest(QStringLiteral("Audio fixture not found: %1").arg(inputAudioPath));
    }

    if (QStandardPaths::findExecutable(QStringLiteral("ffmpeg")).isEmpty()) {
        return skipTest(QStringLiteral("ffmpeg not found in PATH."));
    }
    if (QStandardPaths::findExecutable(QStringLiteral("ffprobe")).isEmpty()) {
        return skipTest(QStringLiteral("ffprobe not found in PATH."));
    }
#if !defined(Q_OS_WIN)
    if (QStandardPaths::findExecutable(QStringLiteral("mono")).isEmpty()) {
        return skipTest(QStringLiteral("mono not found in PATH."));
    }
#endif

    const QFileInfo outputInfo(outputAudioPath);
    if (!outputInfo.absoluteDir().exists()
        && !QDir().mkpath(outputInfo.absoluteDir().absolutePath())) {
        return failTest(QStringLiteral("Unable to create output directory: %1")
                            .arg(outputInfo.absoluteDir().absolutePath()));
    }
    QFile::remove(outputAudioPath);

    QString errorMessage;
    const bool ok = AudioAlignmentUtil::runStreamAlign(
        jsonPath,
        inputAudioPath,
        outputAudioPath,
        rfVideoSampleRateHz,
        true,
        AudioAlignmentUtil::ProgressCallback(),
        AudioAlignmentUtil::CancelCallback(),
        &errorMessage);
    if (!ok) {
        return failTest(QStringLiteral("AudioAlignmentUtil::runStreamAlign failed: %1")
                            .arg(errorMessage));
    }

    const QFileInfo alignedInfo(outputAudioPath);
    if (!alignedInfo.exists() || !alignedInfo.isFile() || alignedInfo.size() <= 0) {
        return failTest(QStringLiteral("Aligned output was not created or is empty: %1")
                            .arg(outputAudioPath));
    }

    QProcess ffprobe;
    ffprobe.setProcessChannelMode(QProcess::MergedChannels);
    ffprobe.start(
        QStringLiteral("ffprobe"),
        QStringList() << QStringLiteral("-v") << QStringLiteral("error")
                      << QStringLiteral("-select_streams") << QStringLiteral("a:0")
                      << QStringLiteral("-show_entries")
                      << QStringLiteral("stream=sample_rate,channels")
                      << QStringLiteral("-of") << QStringLiteral("json")
                      << alignedInfo.absoluteFilePath());
    if (!ffprobe.waitForStarted(5000)) {
        return failTest(QStringLiteral("Failed to start ffprobe for output validation."));
    }
    if (!ffprobe.waitForFinished(30000) || ffprobe.exitStatus() != QProcess::NormalExit
        || ffprobe.exitCode() != 0) {
        return failTest(QStringLiteral("ffprobe failed for aligned output:\n%1")
                            .arg(QString::fromLocal8Bit(ffprobe.readAllStandardOutput())));
    }

    QJsonParseError parseError;
    const QJsonDocument probeDoc =
        QJsonDocument::fromJson(ffprobe.readAllStandardOutput(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !probeDoc.isObject()) {
        return failTest(QStringLiteral("Could not parse ffprobe JSON output: %1")
                            .arg(parseError.errorString()));
    }

    const QJsonArray streams = probeDoc.object().value(QStringLiteral("streams")).toArray();
    if (streams.isEmpty() || !streams.at(0).isObject()) {
        return failTest(QStringLiteral("No audio stream found in aligned output."));
    }
    const QJsonObject firstStream = streams.at(0).toObject();
    bool sampleRateOk = false;
    const int sampleRate =
        firstStream.value(QStringLiteral("sample_rate")).toString().toInt(&sampleRateOk);
    int channels = firstStream.value(QStringLiteral("channels")).toInt(-1);
    if (channels <= 0) {
        bool channelsOk = false;
        channels = firstStream.value(QStringLiteral("channels")).toString().toInt(&channelsOk);
        if (!channelsOk) {
            channels = -1;
        }
    }

    if (!sampleRateOk || sampleRate <= 0 || channels <= 0) {
        return failTest(QStringLiteral("Invalid aligned output audio metadata (sample_rate=%1, channels=%2).")
                            .arg(firstStream.value(QStringLiteral("sample_rate")).toVariant().toString())
                            .arg(firstStream.value(QStringLiteral("channels")).toVariant().toString()));
    }

    QTextStream(stdout) << "AAA runtime fixture test passed. Output: "
                        << alignedInfo.absoluteFilePath() << Qt::endl;
    return 0;
}
