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
