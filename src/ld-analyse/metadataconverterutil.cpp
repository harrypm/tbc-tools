#include "metadataconverterutil.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <QStandardPaths>

namespace {
QString resolveToolPath(const QStringList &toolNames)
{
    for (const QString &name : toolNames) {
        const QString toolPath = QStandardPaths::findExecutable(name);
        if (!toolPath.isEmpty()) {
            return toolPath;
        }
    }

    const QString appDir = QCoreApplication::applicationDirPath();
    const QStringList candidateDirs = {
        QStringLiteral("."),
        QStringLiteral(".."),
        QStringLiteral("../bin"),
        QStringLiteral("../../bin")
    };

    for (const QString &dir : candidateDirs) {
        for (const QString &name : toolNames) {
            const QString localCandidate = QDir(appDir).filePath(dir + QLatin1Char('/') + name);
            if (QFileInfo::exists(localCandidate)) {
                return localCandidate;
            }
        }
    }

    return QString();
}
} // namespace

namespace MetadataConverterUtil {
QString resolveMetadataConverterPath()
{
    const QStringList converterNames = {
        QStringLiteral("tbc-metadata-converter"),
        QStringLiteral("metadata-converter"),
        QStringLiteral("ld-json-converter")
    };

    return resolveToolPath(converterNames);
}

QString defaultMetadataOutputPath(const QString &inputFilename, bool jsonToSqlite)
{
    QFileInfo info(inputFilename);
    if (jsonToSqlite) {
        if (info.suffix().compare(QStringLiteral("json"), Qt::CaseInsensitive) == 0) {
            return info.path() + QDir::separator() + info.completeBaseName() + QStringLiteral(".db");
        }
        return inputFilename + QStringLiteral(".db");
    }

    if (info.suffix().compare(QStringLiteral("db"), Qt::CaseInsensitive) == 0) {
        return info.path() + QDir::separator() + info.completeBaseName() + QStringLiteral(".json");
    }
    return inputFilename + QStringLiteral(".json");
}
QString resolveExportDecodeMetadataPath()
{
    const QStringList toolNames = {
        QStringLiteral("ld-export-decode-metadata")
    };

    return resolveToolPath(toolNames);
}

QString defaultExportDecodeMetadataOutputPath(const QString &inputFilename)
{
    QFileInfo info(inputFilename);
    if (info.suffix().compare(QStringLiteral("db"), Qt::CaseInsensitive) == 0) {
        return info.path() + QDir::separator() + info.completeBaseName() + QStringLiteral(".export.json");
    }
    return inputFilename + QStringLiteral(".export.json");
}

bool runMetadataConverter(const QString &direction,
                          const QString &inputFilename,
                          const QString &outputFilename,
                          QString *errorMessage)
{
    const QString converterPath = resolveMetadataConverterPath();
    if (converterPath.isEmpty()) {
        if (errorMessage) {
            *errorMessage = QObject::tr("tbc-metadata-converter not found in PATH or alongside the application.");
        }
        return false;
    }

    QStringList arguments;
    arguments << QStringLiteral("--direction") << direction;
    if (direction == QLatin1String("json-to-sqlite")) {
        arguments << QStringLiteral("--input-json") << inputFilename
                  << QStringLiteral("--output-sqlite") << outputFilename;
    } else {
        arguments << QStringLiteral("--input-sqlite") << inputFilename
                  << QStringLiteral("--output-json") << outputFilename;
    }

    QProcess process;
    process.start(converterPath, arguments);
    if (!process.waitForFinished(-1)) {
        if (errorMessage) {
            *errorMessage = QObject::tr("tbc-metadata-converter did not finish.");
        }
        return false;
    }

    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
        const QString stdErr = QString::fromLocal8Bit(process.readAllStandardError()).trimmed();
        const QString stdOut = QString::fromLocal8Bit(process.readAllStandardOutput()).trimmed();
        if (errorMessage) {
            *errorMessage = stdErr.isEmpty() ? stdOut : stdErr;
            if (errorMessage->isEmpty()) {
                *errorMessage = QObject::tr("tbc-metadata-converter failed with exit code %1.").arg(process.exitCode());
            }
        }
        return false;
    }

    return true;
}

bool runExportDecodeMetadata(const QString &inputFilename,
                             const QString &outputFilename,
                             QString *errorMessage)
{
    const QString toolPath = resolveExportDecodeMetadataPath();
    if (toolPath.isEmpty()) {
        if (errorMessage) {
            *errorMessage = QObject::tr("ld-export-decode-metadata not found in PATH or alongside the application.");
        }
        return false;
    }

    QStringList arguments;
    arguments << QStringLiteral("--input-sqlite") << inputFilename
              << QStringLiteral("--output-json") << outputFilename;

    QProcess process;
    process.start(toolPath, arguments);
    if (!process.waitForFinished(-1)) {
        if (errorMessage) {
            *errorMessage = QObject::tr("ld-export-decode-metadata did not finish.");
        }
        return false;
    }

    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
        const QString stdErr = QString::fromLocal8Bit(process.readAllStandardError()).trimmed();
        const QString stdOut = QString::fromLocal8Bit(process.readAllStandardOutput()).trimmed();
        if (errorMessage) {
            *errorMessage = stdErr.isEmpty() ? stdOut : stdErr;
            if (errorMessage->isEmpty()) {
                *errorMessage = QObject::tr("ld-export-decode-metadata failed with exit code %1.").arg(process.exitCode());
            }
        }
        return false;
    }

    return true;
}
} // namespace MetadataConverterUtil
