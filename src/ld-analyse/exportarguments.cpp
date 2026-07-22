#include "exportarguments.h"
#include <QDir>
#include <QFileInfo>
#include <QStringList>

namespace ExportArguments {

namespace {
bool hasKnownOutputContainerSuffix(const QString &suffix)
{
    static const QStringList knownSuffixes = {
        QStringLiteral("mkv"),
        QStringLiteral("mov"),
        QStringLiteral("mp4"),
        QStringLiteral("mxf"),
        QStringLiteral("avi")
    };
    for (const QString &knownSuffix : knownSuffixes) {
        if (suffix.compare(knownSuffix, Qt::CaseInsensitive) == 0) {
            return true;
        }
    }
    return false;
}

QString stripKnownOutputContainerSuffix(const QString &fileName)
{
    const int lastDot = fileName.lastIndexOf(QLatin1Char('.'));
    if (lastDot <= 0) {
        return fileName;
    }
    const QString suffix = fileName.mid(lastDot + 1);
    if (!hasKnownOutputContainerSuffix(suffix)) {
        return fileName;
    }
    const QString strippedName = fileName.left(lastDot);
    return strippedName.isEmpty() ? fileName : strippedName;
}
} // namespace

bool shouldDisableDropoutCorrection(const QString &dropoutMode, int startFrameOneBased)
{
    Q_UNUSED(startFrameOneBased);
    const QString normalizedMode = dropoutMode.trimmed().toLower();
    return normalizedMode == QStringLiteral("disabled");
}
bool isDefaultActiveAreaFraming(const QString &resolutionMode, bool hasAnyVerticalLineAdjustment)
{
    const QString normalizedMode = resolutionMode.trimmed().toLower();
    return normalizedMode == QStringLiteral("active_area") && !hasAnyVerticalLineAdjustment;
}
QString sanitizeOutputBasePath(const QString &path)
{
    const QString trimmedPath = path.trimmed();
    if (trimmedPath.isEmpty()) {
        return QString();
    }

    const QFileInfo info(trimmedPath);
    const QString fileName = info.fileName().trimmed();
    if (fileName.isEmpty()) {
        return QDir::cleanPath(info.absoluteFilePath());
    }

    const QString sanitizedFileName = stripKnownOutputContainerSuffix(fileName);
    return QDir(info.absolutePath()).filePath(sanitizedFileName);
}

}
