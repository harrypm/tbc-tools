/************************************************************************

    teletextintegration.cpp

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

#include "teletextintegration.h"

#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QIODevice>
#include <QProcess>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QStringList>
#include <QTextStream>

namespace {
void setError(QString *errorMessage, const QString &message)
{
    if (errorMessage) {
        *errorMessage = message;
    }
}

void appendUniqueCandidate(QStringList &candidates, const QString &candidate)
{
    if (candidate.trimmed().isEmpty()) {
        return;
    }
    if (!candidates.contains(candidate)) {
        candidates.append(candidate);
    }
}

bool isTeletextVendorDirectory(const QString &directoryPath)
{
    if (directoryPath.trimmed().isEmpty()) {
        return false;
    }

    const QDir directory(directoryPath);
    if (!directory.exists()) {
        return false;
    }

    const QString teletextModulePath = directory.filePath(QStringLiteral("teletext/__main__.py"));
    return QFileInfo::exists(teletextModulePath);
}

QString resolveTeletextVendorDirectory()
{
    QStringList candidates;

#ifdef TELETEXT_VENDOR_DIR
    appendUniqueCandidate(candidates, QString::fromUtf8(TELETEXT_VENDOR_DIR));
#endif

    const QString appDirectory = QCoreApplication::applicationDirPath();
    appendUniqueCandidate(candidates, QDir(appDirectory).filePath(QStringLiteral("vendor/vhs-teletext")));
    appendUniqueCandidate(candidates, QDir(appDirectory).filePath(QStringLiteral("../vendor/vhs-teletext")));
    appendUniqueCandidate(candidates, QDir(appDirectory).filePath(QStringLiteral("../../vendor/vhs-teletext")));

    for (const QString &candidate : candidates) {
        if (isTeletextVendorDirectory(candidate)) {
            return QDir::cleanPath(candidate);
        }
    }

    return QString();
}
QString runPythonProbe(const QString &pythonExecutable,
                       const QString &script,
                       const QProcessEnvironment &environment,
                       bool *ok)
{
    if (ok) {
        *ok = false;
    }

    QProcess probeProcess;
    probeProcess.setProcessEnvironment(environment);
    probeProcess.start(pythonExecutable, QStringList() << QStringLiteral("-c") << script);

    if (!probeProcess.waitForStarted(5000)) {
        return QStringLiteral("python probe failed to start");
    }
    if (!probeProcess.waitForFinished(30000)) {
        probeProcess.kill();
        probeProcess.waitForFinished(1000);
        return QStringLiteral("python probe timed out");
    }

    const QString stdoutText = QString::fromLocal8Bit(probeProcess.readAllStandardOutput()).trimmed();
    const QString stderrText = QString::fromLocal8Bit(probeProcess.readAllStandardError()).trimmed();

    if (probeProcess.exitStatus() != QProcess::NormalExit || probeProcess.exitCode() != 0) {
        QString errorDetails = stderrText;
        if (errorDetails.isEmpty()) {
            errorDetails = stdoutText;
        }
        if (errorDetails.isEmpty()) {
            errorDetails = QStringLiteral("unknown error");
        }
        return errorDetails;
    }

    if (ok) {
        *ok = true;
    }
    return stdoutText;
}

QString resolvePythonExecutable()
{
    const QString overrideValue =
        qEnvironmentVariable("TELETEXT_PYTHON").trimmed();
    if (!overrideValue.isEmpty()) {
        const QString overrideExecutable = QStandardPaths::findExecutable(overrideValue);
        if (!overrideExecutable.isEmpty()) {
            return overrideExecutable;
        }

        const QFileInfo overrideInfo(overrideValue);
        if (overrideInfo.exists() && overrideInfo.isFile() && overrideInfo.isExecutable()) {
            return overrideInfo.absoluteFilePath();
        }
    }
    const QStringList candidates = {
        QStringLiteral("python3"),
        QStringLiteral("python")
    };

    for (const QString &candidate : candidates) {
        const QString executablePath = QStandardPaths::findExecutable(candidate);
        if (!executablePath.isEmpty()) {
            return executablePath;
        }
    }

    return QString();
}

bool runPythonStep(const QString &pythonExecutable,
                   const QStringList &arguments,
                   const QProcessEnvironment &environment,
                   const QString &stepDescription,
                   QString *errorMessage,
                   qint64 timeoutMilliseconds = -1,
                   const QString &standardOutputFile = QString())
{
    QProcess process;
    process.setProcessEnvironment(environment);
    if (!standardOutputFile.trimmed().isEmpty()) {
        process.setStandardOutputFile(standardOutputFile, QIODevice::Truncate);
    }
    process.start(pythonExecutable, arguments);

    if (!process.waitForStarted(5000)) {
        setError(errorMessage, QObject::tr("Could not start %1 step.").arg(stepDescription));
        return false;
    }
    QElapsedTimer elapsedTimer;
    elapsedTimer.start();
    QString standardOutputBuffer;
    QString standardErrorBuffer;

    while (process.state() != QProcess::NotRunning) {
        process.waitForReadyRead(200);
        const QByteArray stdoutChunk = process.readAllStandardOutput();
        if (!stdoutChunk.isEmpty()) {
            standardOutputBuffer += QString::fromLocal8Bit(stdoutChunk);
        }
        const QByteArray stderrChunk = process.readAllStandardError();
        if (!stderrChunk.isEmpty()) {
            standardErrorBuffer += QString::fromLocal8Bit(stderrChunk);
        }
        if (timeoutMilliseconds > 0 && elapsedTimer.elapsed() > timeoutMilliseconds) {
            process.kill();
            process.waitForFinished(3000);
            setError(errorMessage,
                     QObject::tr("%1 step timed out after %2 seconds.")
                         .arg(stepDescription)
                         .arg(timeoutMilliseconds / 1000));
            return false;
        }
    }

    const QByteArray trailingStdout = process.readAllStandardOutput();
    if (!trailingStdout.isEmpty()) {
        standardOutputBuffer += QString::fromLocal8Bit(trailingStdout);
    }
    const QByteArray trailingStderr = process.readAllStandardError();
    if (!trailingStderr.isEmpty()) {
        standardErrorBuffer += QString::fromLocal8Bit(trailingStderr);
    }

    const QString standardOutput = standardOutputBuffer.trimmed();
    const QString standardError = standardErrorBuffer.trimmed();
    if (!standardOutput.isEmpty()) {
        qInfo().noquote() << standardOutput;
    }
    if (!standardError.isEmpty()) {
        qWarning().noquote() << standardError;
    }

    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
        setError(errorMessage,
                 QObject::tr("%1 step failed with exit code %2.")
                     .arg(stepDescription)
                     .arg(process.exitCode()));
        return false;
    }

    return true;
}

bool copyFileReplacing(const QString &sourcePath, const QString &targetPath, QString *errorMessage)
{
    if (!QFileInfo::exists(sourcePath)) {
        return true;
    }

    if (QFileInfo::exists(targetPath) && !QFile::remove(targetPath)) {
        setError(errorMessage, QObject::tr("Could not replace existing file: %1").arg(targetPath));
        return false;
    }

    if (!QFile::copy(sourcePath, targetPath)) {
        setError(errorMessage, QObject::tr("Could not copy file from %1 to %2").arg(sourcePath, targetPath));
        return false;
    }

    return true;
}

QString embeddedTeletextFontFace(const QString &fontFamily, const QByteArray &fontData)
{
    return QStringLiteral(
               "@font-face {font-family:%1; src:url(\"data:font/ttf;base64,%2\") format(\"truetype\"); font-display:block;}")
        .arg(fontFamily, QString::fromLatin1(fontData.toBase64()));
}

bool embedTeletextFontsInCss(const QString &outputDirectoryPath, QString *errorMessage)
{
    QDir outputDirectory(outputDirectoryPath);
    const QString cssPath = outputDirectory.filePath(QStringLiteral("teletext-noscanlines.css"));
    QFile cssFile(cssPath);
    if (!cssFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        setError(errorMessage, QObject::tr("Could not open teletext CSS for font embedding: %1").arg(cssPath));
        return false;
    }
    QString cssContent = QString::fromUtf8(cssFile.readAll());
    cssFile.close();

    auto readFont = [&](const QString &fontFilename, QByteArray *fontData) {
        const QString fontPath = outputDirectory.filePath(fontFilename);
        QFile fontFile(fontPath);
        if (!fontFile.open(QIODevice::ReadOnly)) {
            setError(errorMessage, QObject::tr("Could not open teletext font for embedding: %1").arg(fontPath));
            return false;
        }
        *fontData = fontFile.readAll();
        return true;
    };

    QByteArray teletext2Data;
    QByteArray teletext4Data;
    if (!readFont(QStringLiteral("teletext2.ttf"), &teletext2Data)
        || !readFont(QStringLiteral("teletext4.ttf"), &teletext4Data)) {
        return false;
    }

    const QString teletext2ExternalRule =
        QStringLiteral("@font-face {font-family:teletext2; src:url('teletext2.ttf')}");
    const QString teletext4ExternalRule =
        QStringLiteral("@font-face {font-family:teletext4; src:url('teletext4.ttf')}");
    const QString teletext2EmbeddedRule = embeddedTeletextFontFace(QStringLiteral("teletext2"), teletext2Data);
    const QString teletext4EmbeddedRule = embeddedTeletextFontFace(QStringLiteral("teletext4"), teletext4Data);

    if (cssContent.contains(teletext2ExternalRule)) {
        cssContent.replace(teletext2ExternalRule, teletext2EmbeddedRule);
    } else if (!cssContent.contains(QStringLiteral("font-family:teletext2"), Qt::CaseInsensitive)) {
        cssContent.prepend(teletext2EmbeddedRule + QLatin1Char('\n'));
    }
    if (cssContent.contains(teletext4ExternalRule)) {
        cssContent.replace(teletext4ExternalRule, teletext4EmbeddedRule);
    } else if (!cssContent.contains(QStringLiteral("font-family:teletext4"), Qt::CaseInsensitive)) {
        cssContent.prepend(teletext4EmbeddedRule + QLatin1Char('\n'));
    }

    cssContent.replace(
        QStringLiteral("font-family: teletext2;"),
        QStringLiteral("font-family: teletext2, \"Courier New\", \"Liberation Mono\", \"DejaVu Sans Mono\", monospace;")
    );
    cssContent.replace(
        QStringLiteral("font-family: teletext4;"),
        QStringLiteral("font-family: teletext4, teletext2, \"Courier New\", \"Liberation Mono\", \"DejaVu Sans Mono\", monospace;")
    );

    if (!cssFile.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        setError(errorMessage, QObject::tr("Could not write teletext CSS after font embedding: %1").arg(cssPath));
        return false;
    }
    QTextStream cssStream(&cssFile);
    cssStream << cssContent;
    cssFile.close();
    return true;
}

void ensureCssSwitchScript(const QString &outputDirectory)
{
    const QString cssSwitchPath = QDir(outputDirectory).filePath(QStringLiteral("cssswitch.js"));
    if (QFileInfo::exists(cssSwitchPath)) {
        return;
    }

    QFile cssSwitchFile(cssSwitchPath);
    if (!cssSwitchFile.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        return;
    }

    QTextStream stream(&cssSwitchFile);
    stream << "function set_style_from_cookie() {\n";
    stream << "    // Placeholder helper for generated teletext HTML pages.\n";
    stream << "}\n";
    cssSwitchFile.close();
}
}

bool runTeletextHtmlExport(const TeletextIntegrationOptions &options, QString *errorMessage)
{
    setError(errorMessage, QString());

    const QString inputFilename = options.inputFilename.trimmed();
    const QString outputDirectoryPath = QDir::cleanPath(options.outputDirectory.trimmed());

    if (inputFilename.isEmpty()) {
        setError(errorMessage, QObject::tr("Teletext export input filename is empty."));
        return false;
    }
    if (!QFileInfo::exists(inputFilename)) {
        setError(errorMessage, QObject::tr("Teletext export input file does not exist: %1").arg(inputFilename));
        return false;
    }
    if (outputDirectoryPath.isEmpty()) {
        setError(errorMessage, QObject::tr("Teletext export output directory is empty."));
        return false;
    }

    QDir outputDirectory(outputDirectoryPath);
    if (!outputDirectory.exists() && !outputDirectory.mkpath(QStringLiteral("."))) {
        setError(errorMessage, QObject::tr("Could not create teletext output directory: %1").arg(outputDirectoryPath));
        return false;
    }

    const QString vendorDirectory = resolveTeletextVendorDirectory();
    if (vendorDirectory.isEmpty()) {
        setError(errorMessage, QObject::tr("Could not locate vendored vhs-teletext runtime directory."));
        return false;
    }

    const QString pythonExecutable = resolvePythonExecutable();
    if (pythonExecutable.isEmpty()) {
        setError(errorMessage, QObject::tr("Could not locate Python interpreter (python3/python)."));
        return false;
    }
    qInfo() << "Teletext export: using Python executable:" << pythonExecutable;
    if (!qEnvironmentVariableIsEmpty("TELETEXT_PYTHON")) {
        qInfo() << "Teletext export: TELETEXT_PYTHON override set to"
                << qEnvironmentVariable("TELETEXT_PYTHON");
    }

    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    const QString existingPythonPath = environment.value(QStringLiteral("PYTHONPATH"));
    if (existingPythonPath.isEmpty()) {
        environment.insert(QStringLiteral("PYTHONPATH"), vendorDirectory);
    } else {
        environment.insert(QStringLiteral("PYTHONPATH"),
                           vendorDirectory + QDir::listSeparator() + existingPythonPath);
    }
    const QString forceCpuRaw = qEnvironmentVariable("TELETEXT_FORCE_CPU").trimmed().toLower();
    const bool forceCpu =
        (forceCpuRaw == QStringLiteral("1")
         || forceCpuRaw == QStringLiteral("true")
         || forceCpuRaw == QStringLiteral("yes"));
    bool preferOpenCl = false;
    const QString preferOpenClRaw = qEnvironmentVariable("TELETEXT_PREFER_OPENCL").trimmed().toLower();
    if (preferOpenClRaw == QStringLiteral("1")
        || preferOpenClRaw == QStringLiteral("true")
        || preferOpenClRaw == QStringLiteral("yes")) {
        preferOpenCl = true;
    } else if (preferOpenClRaw == QStringLiteral("0")
               || preferOpenClRaw == QStringLiteral("false")
               || preferOpenClRaw == QStringLiteral("no")) {
        preferOpenCl = false;
    } else {
#if defined(Q_OS_MACOS)
        preferOpenCl = true;
#endif
    }
    environment.insert(QStringLiteral("USE_GPU"), forceCpu ? QStringLiteral("0") : QStringLiteral("1"));
    if (forceCpu) {
        qInfo() << "Teletext export: GPU acceleration disabled by TELETEXT_FORCE_CPU";
    } else if (preferOpenCl) {
        qInfo() << "Teletext export: OpenCL-preferred backend ordering is enabled";
    }

    const QString dependencyProbeScript = QStringLiteral(R"PY(
import importlib.util,sys
required=("numpy","scipy","matplotlib","click","tqdm","zmq")
optional=("watchdog","serial")
missing=[m for m in required if importlib.util.find_spec(m) is None]
missing_optional=[m for m in optional if importlib.util.find_spec(m) is None]
if missing:
    print("missing:"+",".join(missing))
    sys.exit(2)
if missing_optional:
    print("optional_missing:"+",".join(missing_optional))
print("ok")
)PY");
    bool dependencyProbeOk = false;
    const QString dependencyProbeOutput =
        runPythonProbe(pythonExecutable, dependencyProbeScript, environment, &dependencyProbeOk);
    if (!dependencyProbeOk) {
        setError(errorMessage,
                 QObject::tr("Teletext Python dependency check failed: %1")
                     .arg(dependencyProbeOutput));
        return false;
    }
    if (dependencyProbeOutput.startsWith(QStringLiteral("missing:"))) {
        setError(errorMessage,
                 QObject::tr("Teletext Python dependencies missing: %1")
                     .arg(dependencyProbeOutput.mid(QStringLiteral("missing:").size())));
        return false;
    }
    const QStringList dependencyProbeLines = dependencyProbeOutput.split(
        QRegularExpression(QStringLiteral("[\\r\\n]+")), Qt::SkipEmptyParts);
    for (const QString &line : dependencyProbeLines) {
        const QString trimmedLine = line.trimmed();
        if (trimmedLine.startsWith(QStringLiteral("optional_missing:"))) {
            qWarning() << "Teletext export: optional Python modules missing:"
                       << trimmedLine.mid(QStringLiteral("optional_missing:").size());
        }
    }

    const QString backendProbeScript = QStringLiteral(R"PY(
import importlib.util
print("pycuda=" + ("yes" if importlib.util.find_spec("pycuda") else "no"))
if importlib.util.find_spec("pyopencl") is None:
    print("pyopencl=no")
    print("pyopencl_runtime=no")
else:
    print("pyopencl=yes")
    try:
        import pyopencl as cl
        platforms = cl.get_platforms()
        selected_ctx = ""
        first_error = ""
        if len(platforms) > 0:
            for pidx, platform in enumerate(platforms):
                for didx, device in enumerate(platform.get_devices()):
                    try:
                        ctx = cl.Context([device])
                        selected_ctx = f"{pidx}:{didx}"
                        del ctx
                        break
                    except Exception as exc:
                        if not first_error:
                            first_error = type(exc).__name__ + ":" + str(exc)
                if selected_ctx:
                    break
        print("pyopencl_runtime=" + ("yes" if selected_ctx else "no"))
        if selected_ctx:
            print("pyopencl_selected_ctx=" + selected_ctx)
        elif first_error:
            print("pyopencl_runtime_error=" + first_error)
    except Exception as exc:
        print("pyopencl_runtime=no")
        print("pyopencl_runtime_error=" + type(exc).__name__ + ":" + str(exc))
)PY");
    bool backendProbeOk = false;
    const QString backendProbeOutput =
        runPythonProbe(pythonExecutable, backendProbeScript, environment, &backendProbeOk);
    bool pycudaAvailable = false;
    bool pyopenclAvailable = false;
    bool pyopenclRuntimeAvailable = false;
    bool pyopenclRuntimeReported = false;
    QString pyopenclRuntimeError;
    QString pyopenclSelectedContext;
    if (backendProbeOk) {
        const QStringList backendLines = backendProbeOutput.split(
            QRegularExpression(QStringLiteral("[\\r\\n]+")), Qt::SkipEmptyParts);
        for (const QString &line : backendLines) {
            const QString trimmedLine = line.trimmed();
            qInfo() << "Teletext export backend probe:" << trimmedLine;
            const qint32 separatorIndex = trimmedLine.indexOf(QLatin1Char('='));
            if (separatorIndex <= 0) {
                continue;
            }
            const QString key = trimmedLine.left(separatorIndex).trimmed();
            const QString value = trimmedLine.mid(separatorIndex + 1).trimmed();
            if (key == QStringLiteral("pycuda")) {
                pycudaAvailable = (value == QStringLiteral("yes"));
            } else if (key == QStringLiteral("pyopencl")) {
                pyopenclAvailable = (value == QStringLiteral("yes"));
            } else if (key == QStringLiteral("pyopencl_runtime")) {
                pyopenclRuntimeReported = true;
                pyopenclRuntimeAvailable = (value == QStringLiteral("yes"));
            } else if (key == QStringLiteral("pyopencl_runtime_error")) {
                pyopenclRuntimeError = value;
            } else if (key == QStringLiteral("pyopencl_selected_ctx")) {
                pyopenclSelectedContext = value;
            }
        }
    } else {
        qWarning() << "Teletext export: GPU backend probe failed:" << backendProbeOutput;
    }
    if (!pyopenclRuntimeReported) {
        pyopenclRuntimeAvailable = pyopenclAvailable;
    }
    if (!pyopenclRuntimeAvailable) {
        if (!pyopenclRuntimeError.isEmpty()) {
            qWarning() << "Teletext export: OpenCL runtime unavailable:" << pyopenclRuntimeError;
        } else {
            qWarning() << "Teletext export: OpenCL runtime unavailable";
        }
    } else if (pyopenclSelectedContext.isEmpty()) {
        qWarning() << "Teletext export: OpenCL runtime reported available but no stable context was selected";
    }
    if (!pycudaAvailable) {
        qWarning() << "Teletext export: CUDA Python module not available";
    }
    const bool pyopenclUsable = pyopenclRuntimeAvailable && !pyopenclSelectedContext.isEmpty();

    const qint32 teletextThreads = qMax<qint32>(1, options.maxThreads);
    const qint32 minDuplicates = qMax<qint32>(1, options.minDuplicates);
    const QString tapeFormat = options.tapeFormat.trimmed().isEmpty()
        ? QStringLiteral("vhs")
        : options.tapeFormat.trimmed();

    const QString rawStreamPath = outputDirectory.filePath(QStringLiteral("teletext_raw.t42"));
    const QString squashedStreamPath = outputDirectory.filePath(QStringLiteral("teletext_squashed.t42"));

    auto deconvolutionArguments = [&](bool useCpuMode, bool preferOpenClMode) {
        QStringList arguments = {
            QStringLiteral("-m"), QStringLiteral("teletext"),
            QStringLiteral("deconvolve")
        };
        if (useCpuMode) {
            arguments << QStringLiteral("-C");
        } else if (preferOpenClMode) {
            arguments << QStringLiteral("-O");
        }
        arguments << QStringLiteral("-c") << QStringLiteral("tbc")
                  << QStringLiteral("--tape-format") << tapeFormat
                  << QStringLiteral("--threads") << QString::number(teletextThreads)
                  << QStringLiteral("--no-progress")
                  << inputFilename;
        return arguments;
    };

    auto deconvolutionEnvironment = [&](bool useCpuMode, bool preferOpenClMode) {
        QProcessEnvironment runEnvironment = environment;
        runEnvironment.insert(QStringLiteral("USE_GPU"),
                              useCpuMode ? QStringLiteral("0") : QStringLiteral("1"));
        if (preferOpenClMode && !pyopenclSelectedContext.isEmpty()) {
            runEnvironment.insert(QStringLiteral("PYOPENCL_CTX"), pyopenclSelectedContext);
        } else if (runEnvironment.contains(QStringLiteral("PYOPENCL_CTX"))) {
            runEnvironment.remove(QStringLiteral("PYOPENCL_CTX"));
        }
        return runEnvironment;
    };
    struct DeconvolutionAttempt {
        bool useCpuMode;
        bool preferOpenClMode;
        QString description;
        QString stepLabel;
    };

    QList<DeconvolutionAttempt> attempts;
    if (forceCpu) {
        attempts.append({
            true,
            false,
            QStringLiteral("CPU backend"),
            QObject::tr("Teletext deconvolution (CPU)")
        });
    } else if (preferOpenCl) {
        if (pyopenclUsable) {
            attempts.append({
                false,
                true,
                QStringLiteral("OpenCL-first backend ordering"),
                QObject::tr("Teletext deconvolution (OpenCL-first)")
            });
        } else {
            qWarning() << "Teletext export: skipping OpenCL-first attempt because OpenCL runtime is unavailable";
        }
        attempts.append({
            false,
            false,
            QStringLiteral("CUDA/OpenCL auto-detect backend ordering"),
            QObject::tr("Teletext deconvolution")
        });
        attempts.append({
            true,
            false,
            QStringLiteral("CPU fallback backend"),
            QObject::tr("Teletext deconvolution (CPU fallback)")
        });
    } else {
        attempts.append({
            false,
            false,
            QStringLiteral("CUDA/OpenCL auto-detect backend ordering"),
            QObject::tr("Teletext deconvolution")
        });
        if (pyopenclUsable) {
            attempts.append({
                false,
                true,
                QStringLiteral("OpenCL-first fallback backend ordering"),
                QObject::tr("Teletext deconvolution (OpenCL fallback)")
            });
        } else {
            qWarning() << "Teletext export: skipping OpenCL fallback attempt because OpenCL runtime is unavailable";
        }
        attempts.append({
            true,
            false,
            QStringLiteral("CPU fallback backend"),
            QObject::tr("Teletext deconvolution (CPU fallback)")
        });
    }

    bool deconvolved = false;
    QStringList deconvolutionFailures;
    bool deconvolutionTimeoutOk = false;
    const qint32 deconvolutionTimeoutSecondsRaw =
        qEnvironmentVariableIntValue("TELETEXT_DECONV_TIMEOUT_SEC", &deconvolutionTimeoutOk);
    const qint64 deconvolutionTimeoutMilliseconds = static_cast<qint64>(
        (deconvolutionTimeoutOk && deconvolutionTimeoutSecondsRaw > 0)
            ? deconvolutionTimeoutSecondsRaw
            : 180
    ) * 1000;
    for (qint32 attemptIndex = 0; attemptIndex < attempts.size(); ++attemptIndex) {
        const DeconvolutionAttempt &attempt = attempts.at(attemptIndex);
        qInfo() << "Teletext export: starting deconvolution with" << attempt.description;
        QString attemptError;
        if (runPythonStep(pythonExecutable,
                          deconvolutionArguments(attempt.useCpuMode, attempt.preferOpenClMode),
                          deconvolutionEnvironment(attempt.useCpuMode, attempt.preferOpenClMode),
                          attempt.stepLabel,
                          &attemptError,
                          deconvolutionTimeoutMilliseconds,
                          rawStreamPath)) {
            if (attemptIndex > 0) {
                qInfo() << "Teletext export: deconvolution succeeded after fallback to"
                        << attempt.description;
            }
            deconvolved = true;
            break;
        }
        deconvolutionFailures.append(attempt.stepLabel + QStringLiteral(": ") + attemptError);
        if (attemptIndex + 1 < attempts.size()) {
            qWarning() << "Teletext export: deconvolution attempt failed, trying next backend";
        }
    }

    if (!deconvolved) {
        setError(errorMessage,
                 QObject::tr("Teletext deconvolution failed after trying all backends: %1")
                     .arg(deconvolutionFailures.join(QStringLiteral(" | "))));
        return false;
    }

    qInfo() << "Teletext export: squashing duplicate packets";
    const QStringList squashArguments = {
        QStringLiteral("-m"), QStringLiteral("teletext"),
        QStringLiteral("squash"),
        QStringLiteral("--min-duplicates"), QString::number(minDuplicates),
        QStringLiteral("--no-progress"),
        rawStreamPath
    };
    if (!runPythonStep(pythonExecutable, squashArguments, environment,
                       QObject::tr("Teletext squash"), errorMessage,
                       -1, squashedStreamPath)) {
        return false;
    }

    qInfo() << "Teletext export: generating HTML pages";
    const QStringList htmlArguments = {
        QStringLiteral("-m"), QStringLiteral("teletext"),
        QStringLiteral("html"),
        QStringLiteral("--no-progress"),
        outputDirectoryPath,
        squashedStreamPath
    };
    if (!runPythonStep(pythonExecutable, htmlArguments, environment,
                       QObject::tr("Teletext HTML generation"), errorMessage)) {
        return false;
    }

    if (!copyFileReplacing(QDir(vendorDirectory).filePath(QStringLiteral("misc/teletext.css")),
                           outputDirectory.filePath(QStringLiteral("teletext.css")),
                           errorMessage)) {
        return false;
    }
    if (!copyFileReplacing(QDir(vendorDirectory).filePath(QStringLiteral("misc/teletext-noscanlines.css")),
                           outputDirectory.filePath(QStringLiteral("teletext-noscanlines.css")),
                           errorMessage)) {
        return false;
    }
    if (!copyFileReplacing(QDir(vendorDirectory).filePath(QStringLiteral("misc/teletext2.ttf")),
                           outputDirectory.filePath(QStringLiteral("teletext2.ttf")),
                           errorMessage)) {
        return false;
    }
    if (!copyFileReplacing(QDir(vendorDirectory).filePath(QStringLiteral("misc/teletext4.ttf")),
                           outputDirectory.filePath(QStringLiteral("teletext4.ttf")),
                           errorMessage)) {
        return false;
    }
    if (!embedTeletextFontsInCss(outputDirectoryPath, errorMessage)) {
        return false;
    }
    ensureCssSwitchScript(outputDirectoryPath);

    const QStringList generatedHtmlFiles = outputDirectory.entryList(
        QStringList() << QStringLiteral("*.html"),
        QDir::Files,
        QDir::Name | QDir::IgnoreCase
    );
    if (generatedHtmlFiles.isEmpty()) {
        qWarning() << "Teletext export finished but generated no HTML pages";
    } else {
        qInfo() << "Teletext export complete - generated" << generatedHtmlFiles.size() << "HTML pages in"
                << outputDirectoryPath;
    }

    return true;
}
