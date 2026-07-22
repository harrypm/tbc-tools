#include "exportdialog.h"
#include "exportarguments.h"
#include "ui_exportdialog.h"

#include "tbcsource.h"

#include <QCoreApplication>
#include <QDir>
#include <QDateTime>
#include <QFileDialog>
#include <QFile>
#include <QFileInfo>
#include <QFontMetrics>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QMessageBox>
#include <QLineEdit>
#include <QComboBox>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QStandardPaths>
#include <QTextStream>
#include <QtMath>
#include <QUuid>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QHeaderView>
#include <QAbstractItemView>
#include <QSizePolicy>
#include <QSplitter>
#include <QTimer>
#include <QUrl>
#include <algorithm>
#include <signal.h>

namespace {
const QString kStatsFeedMain = QStringLiteral("main");
const QString kStatsFeedProxy = QStringLiteral("proxy");

QString normalizedStatsFeedTag(const QString &feedTag)
{
    const QString normalized = feedTag.trimmed().toLower();
    return normalized == kStatsFeedProxy ? kStatsFeedProxy : kStatsFeedMain;
}

void setTableItem(QTableWidget *table, int row, int column, const QString &text)
{
    if (!table) {
        return;
    }
    QTableWidgetItem *item = table->item(row, column);
    if (!item) {
        item = new QTableWidgetItem();
        item->setFlags(item->flags() & ~Qt::ItemIsEditable);
        table->setItem(row, column, item);
    }
    item->setText(text);
}

QString bt601NonSquareSarForSystem(int system)
{
    // 625-line (PAL): 702->4:3 maps to 128:117
    // 525-line (NTSC/PAL-M): 702->4:3 maps to 12:13
    return (system == PAL) ? QStringLiteral("128/117")
                           : QStringLiteral("12/13");
}

QString sd480SarForMode(const QString &outputResolutionMode)
{
    if (outputResolutionMode == QStringLiteral("ntsc_480_169")) {
        return QStringLiteral("32/27");
    }
    return QStringLiteral("8/9");
}
QString stripAnsiSequences(const QString &input)
{
    static const QRegularExpression ansiPattern(QStringLiteral("\\x1B\\[[0-9;?]*[A-Za-z]"));
    QString cleaned = input;
    cleaned.replace(ansiPattern, QString());
    return cleaned;
}
bool isIgnorableLogLine(const QString &line)
{
    return line.contains(QStringLiteral("No support for ANSI escape sequences"), Qt::CaseInsensitive);
}
bool isTransientProcessTelemetryLine(const QString &line)
{
    const QString normalized = line.trimmed().toLower();
    if (normalized.startsWith(QStringLiteral("frame type:"))) {
        return true;
    }
    const bool sizePrefix = normalized.startsWith(QStringLiteral("size:"))
                            || normalized.startsWith(QStringLiteral("size="));
    const bool hasDuration = normalized.contains(QStringLiteral("duration:"))
                             || normalized.contains(QStringLiteral("time="));
    const bool hasBitrate = normalized.contains(QStringLiteral("bitrate:"))
                            || normalized.contains(QStringLiteral("bitrate="));
    return sizePrefix && hasDuration && hasBitrate;
}
QString quoteArgument(const QString &arg)
{
    if (arg.isEmpty()) {
        return QStringLiteral("\"\"");
    }
    if (arg.contains(QRegularExpression(QStringLiteral("[\\s\"]")))) {
        QString escaped = arg;
        escaped.replace(QStringLiteral("\""), QStringLiteral("\\\""));
        return QStringLiteral("\"%1\"").arg(escaped);
    }
    return arg;
}

QString shellQuoteArgument(const QString &arg)
{
    if (arg.isEmpty()) {
        return QStringLiteral("''");
    }
    QString escaped = arg;
    escaped.replace(QStringLiteral("'"), QStringLiteral("'\"'\"'"));
    return QStringLiteral("'%1'").arg(escaped);
}

QString formatShellCommand(const QString &program, const QStringList &args)
{
    QStringList parts;
    parts << shellQuoteArgument(program);
    for (const QString &arg : args) {
        parts << shellQuoteArgument(arg);
    }
    return parts.join(' ');
}

QWidget *dialogParentWidget(QWidget *widget)
{
    if (!widget) {
        return nullptr;
    }

    QWidget *window = widget->window();
    return window ? window : widget;
}

QStringList dialogNameFilters(const QString &filters)
{
    return filters.split(QStringLiteral(";;"), Qt::SkipEmptyParts);
}

void applyCommonFileDialogOptions(QFileDialog *dialog)
{
    if (!dialog) {
        return;
    }

    dialog->setOption(QFileDialog::DontResolveSymlinks, true);
#if defined(Q_OS_MACOS)
    dialog->setOption(QFileDialog::DontUseNativeDialog, true);
#endif
}

QString runOpenFileDialog(QWidget *parent,
                          const QString &title,
                          const QString &startPath,
                          const QString &filters)
{
    QFileDialog dialog(dialogParentWidget(parent), title, startPath);
    dialog.setAcceptMode(QFileDialog::AcceptOpen);
    dialog.setFileMode(QFileDialog::ExistingFile);
    dialog.setNameFilters(dialogNameFilters(filters));
    applyCommonFileDialogOptions(&dialog);
    if (dialog.exec() != QDialog::Accepted || dialog.selectedFiles().isEmpty()) {
        return QString();
    }
    return dialog.selectedFiles().constFirst();
}

QString runSaveFileDialog(QWidget *parent,
                          const QString &title,
                          const QString &startPath,
                          const QString &filters)
{
    QFileDialog dialog(dialogParentWidget(parent), title, startPath);
    dialog.setAcceptMode(QFileDialog::AcceptSave);
    dialog.setFileMode(QFileDialog::AnyFile);
    dialog.setNameFilters(dialogNameFilters(filters));
    dialog.setDefaultSuffix(QStringLiteral("json"));
    applyCommonFileDialogOptions(&dialog);
    if (dialog.exec() != QDialog::Accepted || dialog.selectedFiles().isEmpty()) {
        return QString();
    }
    return dialog.selectedFiles().constFirst();
}

QString runDirectoryDialog(QWidget *parent,
                           const QString &title,
                           const QString &startPath)
{
    QFileDialog dialog(dialogParentWidget(parent), title, startPath);
    dialog.setFileMode(QFileDialog::Directory);
    dialog.setOption(QFileDialog::ShowDirsOnly, true);
    applyCommonFileDialogOptions(&dialog);
    if (dialog.exec() != QDialog::Accepted || dialog.selectedFiles().isEmpty()) {
        return QString();
    }
    return dialog.selectedFiles().constFirst();
}
QString normalizeAudioTrackPathInput(const QString &path)
{
    QString normalized = path.trimmed();
    if (normalized.isEmpty()) {
        return QString();
    }

    const QStringList lines =
        normalized.split(QRegularExpression(QStringLiteral("[\\r\\n]+")), Qt::SkipEmptyParts);
    if (!lines.isEmpty()) {
        normalized = lines.constFirst().trimmed();
    }
    if (normalized.isEmpty()) {
        return QString();
    }

    if (normalized.startsWith(QStringLiteral("file:"), Qt::CaseInsensitive)) {
        const QUrl parsedUrl = QUrl::fromUserInput(normalized);
        if (parsedUrl.isValid() && parsedUrl.isLocalFile()) {
            const QString localPath = parsedUrl.toLocalFile();
            if (!localPath.isEmpty()) {
                normalized = localPath;
            }
        }
    }

    normalized = QDir::cleanPath(QDir::fromNativeSeparators(normalized));
    return QDir::toNativeSeparators(normalized);
}

QString pythonQuotedStringLiteral(const QString &value)
{
    QString escaped = value;
    escaped.replace(QStringLiteral("\\"), QStringLiteral("\\\\"));
    escaped.replace(QStringLiteral("'"), QStringLiteral("\\'"));
    return QStringLiteral("'%1'").arg(escaped);
}

QString pythonLiteralFromJsonValue(const QJsonValue &value)
{
    if (value.isUndefined() || value.isNull()) {
        return QStringLiteral("None");
    }
    if (value.isBool()) {
        return value.toBool() ? QStringLiteral("True") : QStringLiteral("False");
    }
    if (value.isDouble()) {
        const double number = value.toDouble();
        return qIsFinite(number)
                   ? QString::number(number, 'g', 16)
                   : QStringLiteral("None");
    }
    if (value.isString()) {
        return pythonQuotedStringLiteral(value.toString());
    }
    if (value.isArray()) {
        const QJsonArray nestedArray = value.toArray();
        QStringList nestedItems;
        nestedItems.reserve(nestedArray.size());
        for (const QJsonValue &nestedValue : nestedArray) {
            nestedItems << pythonLiteralFromJsonValue(nestedValue);
        }
        return QStringLiteral("[%1]").arg(nestedItems.join(QStringLiteral(",")));
    }
    return QStringLiteral("None");
}

QString pythonLiteralFromJsonArray(const QJsonArray &array)
{
    QStringList items;
    items.reserve(array.size());
    for (const QJsonValue &value : array) {
        items << pythonLiteralFromJsonValue(value);
    }
    return QStringLiteral("[%1]").arg(items.join(QStringLiteral(",")));
}

QString normalizedProresProfileName(const QString &variantOrProfileName)
{
    const QString normalized = variantOrProfileName.trimmed().toLower();
    if (normalized == QStringLiteral("hq") || normalized == QStringLiteral("prores_hq")) {
        return QStringLiteral("prores_hq");
    }
    if (normalized == QStringLiteral("4444xq")
        || normalized == QStringLiteral("prores_4444xq")) {
        return QStringLiteral("prores_4444xq");
    }
    if (normalized == QStringLiteral("std")
        || normalized == QStringLiteral("standard")
        || normalized == QStringLiteral("prores_standard")
        || normalized == QStringLiteral("prores_422")
        || normalized == QStringLiteral("prores")) {
        return QStringLiteral("prores");
    }
    if (normalized == QStringLiteral("lt") || normalized == QStringLiteral("prores_lt")) {
        return QStringLiteral("prores_lt");
    }
    if (normalized == QStringLiteral("proxy") || normalized == QStringLiteral("prores_proxy")) {
        return QStringLiteral("prores_proxy");
    }
    return QString();
}

bool isLossyProresProfileName(const QString &profileName)
{
    const QString normalized = normalizedProresProfileName(profileName);
    return normalized == QStringLiteral("prores")
           || normalized == QStringLiteral("prores_lt")
           || normalized == QStringLiteral("prores_proxy");
}

QString proresLossyWarningText()
{
    return QStringLiteral("WARNING THESE ARE LOSSY, Please use ProRes HQ for visually trasparent and 4444XQ for losless");
}

void normalizeAudioTrackLineEditPath(QLineEdit *lineEdit)
{
    if (!lineEdit) {
        return;
    }
    const QString normalized = normalizeAudioTrackPathInput(lineEdit->text());
    if (normalized == lineEdit->text()) {
        return;
    }
    const QSignalBlocker blocker(lineEdit);
    lineEdit->setText(normalized);
}

QStringList defaultExecutableSearchDirs(const QString &currentInputFile)
{
    QStringList candidateDirs;
    auto appendUniqueDir = [&candidateDirs](const QString &dirPath) {
        if (dirPath.isEmpty()) {
            return;
        }
        for (const QString &existing : candidateDirs) {
            if (existing.compare(dirPath, Qt::CaseInsensitive) == 0) {
                return;
            }
        }
        candidateDirs << dirPath;
    };
    const QString appFilePath = QCoreApplication::applicationFilePath();
    if (!appFilePath.isEmpty()) {
        const QFileInfo appFileInfo(appFilePath);
        const QString canonicalAppFilePath = appFileInfo.canonicalFilePath();
        if (!canonicalAppFilePath.isEmpty()) {
            appendUniqueDir(QFileInfo(canonicalAppFilePath).absolutePath());
        }
        appendUniqueDir(appFileInfo.absolutePath());
    }

    const QString appDir = QCoreApplication::applicationDirPath();
    if (!appDir.isEmpty()) {
        appendUniqueDir(appDir);

        QDir parentDir(appDir);
        for (int i = 0; i < 4; ++i) {
            if (!parentDir.cdUp()) {
                break;
            }
            appendUniqueDir(parentDir.absolutePath());
        }
    }

    const QString currentDir = QDir::currentPath();
    if (!currentDir.isEmpty()) {
        appendUniqueDir(currentDir);
        appendUniqueDir(QDir(currentDir).filePath(QStringLiteral("build/bin")));
    }

    if (!currentInputFile.isEmpty()) {
        const QString inputDir = QFileInfo(currentInputFile).absolutePath();
        appendUniqueDir(inputDir);
    }

    const QString homeDir = QDir::homePath();
    if (!homeDir.isEmpty()) {
        const QString localBin = QDir(homeDir).filePath(QStringLiteral(".local/bin"));
        const QString userBin = QDir(homeDir).filePath(QStringLiteral("bin"));
        appendUniqueDir(localBin);
        appendUniqueDir(userBin);
    }

#if defined(Q_OS_MACOS)
    const QStringList platformDirs = {
        QStringLiteral("/opt/homebrew/bin"),
        QStringLiteral("/opt/homebrew/sbin"),
        QStringLiteral("/usr/local/bin"),
        QStringLiteral("/usr/local/sbin"),
        QStringLiteral("/usr/bin"),
        QStringLiteral("/bin")
    };
#elif defined(Q_OS_UNIX)
    const QStringList platformDirs = {
        QStringLiteral("/usr/local/bin"),
        QStringLiteral("/usr/local/sbin"),
        QStringLiteral("/usr/bin"),
        QStringLiteral("/bin"),
        QStringLiteral("/snap/bin")
    };
#else
    const QStringList platformDirs;
#endif

    for (const QString &dir : platformDirs) {
        appendUniqueDir(dir);
    }

    return candidateDirs;
}

void prependUniquePathEntries(QStringList *pathEntries, const QStringList &prependDirs)
{
    if (!pathEntries) {
        return;
    }

    for (auto it = prependDirs.crbegin(); it != prependDirs.crend(); ++it) {
        if (!it->isEmpty() && !pathEntries->contains(*it)) {
            pathEntries->prepend(*it);
        }
    }
}

QStringList parseProfiles(const QString &rawOutput, QString *defaultProfile)
{
    QStringList profiles;
    const QString cleaned = stripAnsiSequences(rawOutput);
    const QStringList lines = cleaned.split(QRegularExpression(QStringLiteral("[\\r\\n]+")), Qt::SkipEmptyParts);
    for (const QString &line : lines) {
        const QString trimmed = line.trimmed();
        if (!trimmed.startsWith(QStringLiteral("--"))) {
            continue;
        }
        QString name = trimmed.mid(2).trimmed();
        const int spaceIndex = name.indexOf(QRegularExpression(QStringLiteral("\\s")));
        if (spaceIndex > 0) {
            name = name.left(spaceIndex);
        }
        if (name.isEmpty()) {
            continue;
        }
        if (defaultProfile && trimmed.contains(QStringLiteral("default"), Qt::CaseInsensitive)) {
            *defaultProfile = name;
        }
        if (!profiles.contains(name)) {
            profiles << name;
        }
    }
    if (!profiles.isEmpty()) {
        return profiles;
    }

    const QRegularExpression profilePattern(QStringLiteral("--([A-Za-z0-9_\\-]+)"));
    QRegularExpressionMatchIterator matches = profilePattern.globalMatch(cleaned);
    while (matches.hasNext()) {
        const QRegularExpressionMatch match = matches.next();
        const QString name = match.captured(1);
        if (!name.isEmpty() && !profiles.contains(name)) {
            profiles << name;
        }
    }
    return profiles;
}


struct ActiveAreaFrameDefaults {
    int ffll = 0;
    int lfll = 0;
    int ffrl = 0;
    int lfrl = 0;
};
constexpr int kVerticalFramingAutoUserDefinedThreshold = 20;

ActiveAreaFrameDefaults activeAreaDefaultsForSystem(int system)
{
    switch (system) {
    case PAL:
        return {22, 308, 44, 620};
    case PAL_M:
    case NTSC:
    default:
        return {20, 263, 40, 525};
    }
}


bool hasExplicitVerticalFraming(const LdDecodeMetaData::VideoParameters &videoParameters)
{
    return videoParameters.firstActiveFieldLine > 0
           && videoParameters.lastActiveFieldLine > 0
           && videoParameters.firstActiveFrameLine > 0
           && videoParameters.lastActiveFrameLine > 0;
}
bool hasVerticalFramingDeltaBeyondThreshold(const LdDecodeMetaData::VideoParameters &videoParameters,
                                            int thresholdLines)
{
    if (!videoParameters.isValid || !hasExplicitVerticalFraming(videoParameters)) {
        return false;
    }

    const ActiveAreaFrameDefaults defaults = activeAreaDefaultsForSystem(videoParameters.system);
    const int clampedThreshold = qMax(0, thresholdLines);
    const auto exceedsThreshold = [clampedThreshold](int value, int defaultValue) {
        return value > 0 && qAbs(value - defaultValue) > clampedThreshold;
    };

    return exceedsThreshold(videoParameters.firstActiveFieldLine, defaults.ffll)
           || exceedsThreshold(videoParameters.lastActiveFieldLine, defaults.lfll)
           || exceedsThreshold(videoParameters.firstActiveFrameLine, defaults.ffrl)
           || exceedsThreshold(videoParameters.lastActiveFrameLine, defaults.lfrl);
}

bool usesCustomVerticalFraming(const LdDecodeMetaData::VideoParameters &videoParameters)
{
    return hasVerticalFramingDeltaBeyondThreshold(videoParameters,
                                                  kVerticalFramingAutoUserDefinedThreshold);
}

bool hasAnyNonDefaultVerticalFraming(const LdDecodeMetaData::VideoParameters &videoParameters)
{
    if (!videoParameters.isValid) {
        return false;
    }

    const ActiveAreaFrameDefaults defaults = activeAreaDefaultsForSystem(videoParameters.system);
    return (videoParameters.firstActiveFieldLine > 0
            && videoParameters.firstActiveFieldLine != defaults.ffll)
           || (videoParameters.lastActiveFieldLine > 0
               && videoParameters.lastActiveFieldLine != defaults.lfll)
           || (videoParameters.firstActiveFrameLine > 0
               && videoParameters.firstActiveFrameLine != defaults.ffrl)
           || (videoParameters.lastActiveFrameLine > 0
               && videoParameters.lastActiveFrameLine != defaults.lfrl);
}

double frameRateForSystem(int system)
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

int nominalFrameRateForSystem(int system)
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

bool isFfv1ProfileName(const QString &profileName)
{
    return profileName.trimmed().compare(QStringLiteral("ffv1"), Qt::CaseInsensitive) == 0;
}
bool isWebProfileName(const QString &profileName)
{
    const QString normalized = profileName.trimmed().toLower();
    return normalized == QStringLiteral("web")
           || normalized == QStringLiteral("h264_web")
           || normalized == QStringLiteral("h265_web")
           || normalized == QStringLiteral("x264_web")
           || normalized == QStringLiteral("x265_web")
           || normalized == QStringLiteral("av1_web")
           || normalized.startsWith(QStringLiteral("h264_web_"))
           || normalized.startsWith(QStringLiteral("h265_web_"))
           || normalized.startsWith(QStringLiteral("x264_web_"))
           || normalized.startsWith(QStringLiteral("x265_web_"));
}

bool isPalFamilySystem(int system)
{
    return system == PAL || system == PAL_M;
}

bool isValidChromaDecoderForSystem(const QString &decoderName, int system)
{
    const QString normalizedDecoder = decoderName.trimmed().toLower();
    if (normalizedDecoder.isEmpty()) {
        return false;
    }
    static const QStringList palDecoders = {
        QStringLiteral("pal2d"),
        QStringLiteral("transform2d"),
        QStringLiteral("transform3d"),
        QStringLiteral("mono")
    };
    static const QStringList ntscDecoders = {
        QStringLiteral("ntsc1d"),
        QStringLiteral("ntsc2d"),
        QStringLiteral("ntsc3d"),
        QStringLiteral("nntransform3d"),
        QStringLiteral("nntsc3d"),
        QStringLiteral("ntsc3dnoadapt"),
        QStringLiteral("mono")
    };

    if (isPalFamilySystem(system)) {
        return palDecoders.contains(normalizedDecoder);
    }
    if (system == NTSC) {
        return ntscDecoders.contains(normalizedDecoder);
    }
    return false;
}

bool listContainsCaseInsensitive(const QStringList &values, const QString &target)
{
    for (const QString &value : values) {
        if (value.compare(target, Qt::CaseInsensitive) == 0) {
            return true;
        }
    }
    return false;
}

const QList<int> &supportedFfv1SlicesValues()
{
    static const QList<int> values = {4, 6, 8, 9, 12, 15, 16, 18, 20, 24, 25, 28, 30, 32};
    return values;
}

bool isSupportedFfv1SlicesValue(int slices)
{
    return supportedFfv1SlicesValues().contains(slices);
}

QString supportedFfv1SlicesValuesText()
{
    QStringList valuesText;
    const QList<int> values = supportedFfv1SlicesValues();
    valuesText.reserve(values.size());
    for (const int value : values) {
        valuesText << QString::number(value);
    }
    return valuesText.join(QStringLiteral(", "));
}

const QList<int> &supportedFullFrameFfv1SlicesValuesForSystem(int system);

QString supportedFullFrameFfv1SlicesValuesTextForSystem(int system)
{
    QStringList valuesText;
    const QList<int> values = supportedFullFrameFfv1SlicesValuesForSystem(system);
    valuesText.reserve(values.size());
    for (const int value : values) {
        valuesText << QString::number(value);
    }
    return valuesText.join(QStringLiteral(", "));
}

const QList<int> &supportedPalFullFrameFfv1SlicesValues()
{
    static const QList<int> values = {6, 9, 15, 20, 25, 28};
    return values;
}

const QList<int> &supportedPalMFullFrameFfv1SlicesValues()
{
    static const QList<int> values = {4, 6, 9};
    return values;
}

const QList<int> &supportedFullFrameFfv1SlicesValuesForSystem(int system)
{
    if (system == PAL) {
        return supportedPalFullFrameFfv1SlicesValues();
    }
    if (system == PAL_M) {
        return supportedPalMFullFrameFfv1SlicesValues();
    }
    return supportedFfv1SlicesValues();
}


QString effectiveResolutionMode(const TbcSource *source, const QComboBox *resolutionModeComboBox)
{
    QString resolutionMode = resolutionModeComboBox
                                 ? resolutionModeComboBox->currentData().toString()
                                 : QStringLiteral("active_area");
    Q_UNUSED(source);
    if (resolutionMode.isEmpty()) {
        resolutionMode = QStringLiteral("active_area");
    }
    return resolutionMode;
}

bool isFullFrame4fscResolutionMode(const QString &resolutionMode)
{
    return resolutionMode == QStringLiteral("full_frame_4fsc")
           || resolutionMode == QStringLiteral("hybrid_4fsc");
}

bool supportsOutputResolutionResample(const QString &resolutionMode)
{
    return resolutionMode == QStringLiteral("active_area")
           || resolutionMode == QStringLiteral("active_vbi");
}

QString effectiveOutputResolutionMode(const QComboBox *outputResolutionModeComboBox)
{
    return outputResolutionModeComboBox
               ? outputResolutionModeComboBox->currentData().toString()
               : QStringLiteral("default_safe");
}

int activeAreaOutputHeightForSystem(int system)
{
    return system == PAL ? 576 : 486;
}

int activeVbiOutputHeightForSystem(int system)
{
    return system == PAL ? 608 : 512;
}

struct VbiFrameLineRange {
    int firstFrameLine = 0;
    int lastFrameLine = 0;
};

VbiFrameLineRange vbiFrameLinesForSystem(int system)
{
    switch (system) {
    case PAL:
        return {12, 620};
    case PAL_M:
        return {17, 525};
    case NTSC:
    default:
        return {16, 525};
    }
}

int vbiNativeOutputHeightForSystem(int system)
{
    const VbiFrameLineRange lines = vbiFrameLinesForSystem(system);
    return qMax(1, lines.lastFrameLine - lines.firstFrameLine);
}

int nativeActiveWidthForVideoParameters(const LdDecodeMetaData::VideoParameters &videoParameters)
{
    if (!videoParameters.isValid) {
        return 0;
    }
    const int width = videoParameters.activeVideoEnd - videoParameters.activeVideoStart;
    if (width > 0) {
        return width;
    }
    return qMax(0, videoParameters.fieldWidth);
}

int nativeActiveHeightForVideoParameters(const LdDecodeMetaData::VideoParameters &videoParameters)
{
    if (!videoParameters.isValid) {
        return 0;
    }
    const int height = videoParameters.lastActiveFrameLine - videoParameters.firstActiveFrameLine;
    if (height > 0) {
        return height;
    }
    return qMax(0, (videoParameters.fieldHeight * 2) - 1);
}

int fullFrameWidthForSystemFallback(int system)
{
    switch (system) {
    case PAL:
        return 1135;
    case PAL_M:
        return 909;
    case NTSC:
    default:
        return 910;
    }
}

int fullFrameHeightForSystemFallback(int system)
{
    return system == PAL ? 625 : 525;
}


struct OutputResamplePlan {
    bool enabled = false;
    int width = 0;
    int height = 0;
    QString sampleAspectRatio = QStringLiteral("1/1");
};

OutputResamplePlan outputResamplePlanForModes(int system,
                                              const QString &resolutionMode,
                                              const QString &outputResolutionMode)
{
    OutputResamplePlan plan;
    if (outputResolutionMode == QStringLiteral("tool_native")) {
        return plan;
    }
    if (!supportsOutputResolutionResample(resolutionMode)) {
        return plan;
    }

    if (resolutionMode == QStringLiteral("active_vbi")) {
        plan.enabled = true;
        plan.width = 720;
        plan.height = activeVbiOutputHeightForSystem(system);
        plan.sampleAspectRatio = bt601NonSquareSarForSystem(system);
        return plan;
    }

    plan.enabled = true;
    plan.height = activeAreaOutputHeightForSystem(system);

    if (outputResolutionMode == QStringLiteral("default_safe")
        || outputResolutionMode == QStringLiteral("bt601_720")) {
        plan.width = 720;
        plan.sampleAspectRatio = bt601NonSquareSarForSystem(system);
    } else if ((outputResolutionMode == QStringLiteral("ntsc_480_43")
                || outputResolutionMode == QStringLiteral("ntsc_480_169"))
               && system != PAL) {
        plan.width = 720;
        plan.height = 480;
        plan.sampleAspectRatio = sd480SarForMode(outputResolutionMode);
    } else if (outputResolutionMode == QStringLiteral("bt601_702")) {
        plan.width = 702;
        plan.sampleAspectRatio = bt601NonSquareSarForSystem(system);
    } else if (outputResolutionMode == QStringLiteral("bt601_704")) {
        plan.width = 704;
        plan.sampleAspectRatio = bt601NonSquareSarForSystem(system);
    } else if (outputResolutionMode == QStringLiteral("square_768")) {
        plan.width = 768;
        plan.sampleAspectRatio = QStringLiteral("1/1");
    } else if (outputResolutionMode == QStringLiteral("square_786")) {
        plan.width = 786;
        plan.sampleAspectRatio = QStringLiteral("1/1");
    } else {
        plan.enabled = false;
    }

    return plan;
}

QString outputResampleFilter(const OutputResamplePlan &plan, bool interlacedScaling)
{
    if (!plan.enabled || plan.width < 1 || plan.height < 1) {
        return QString();
    }
    if (interlacedScaling) {
        return QStringLiteral("scale=%1:%2:flags=lanczos:interl=1,setsar=%3")
            .arg(plan.width)
            .arg(plan.height)
            .arg(plan.sampleAspectRatio);
    }
    return QStringLiteral("scale=%1:%2:flags=lanczos,setsar=%3")
        .arg(plan.width)
        .arg(plan.height)
        .arg(plan.sampleAspectRatio);
}

bool isFfv1SlicesCompatibleWithResolutionMode(int slices, int system, const QString &resolutionMode)
{
    if (!isSupportedFfv1SlicesValue(slices)) {
        return false;
    }
    if (isFullFrame4fscResolutionMode(resolutionMode)) {
        return supportedFullFrameFfv1SlicesValuesForSystem(system).contains(slices);
    }
    return true;
}

int recommendedFfv1SlicesForResolutionMode(int system, const QString &resolutionMode)
{
    if (isFullFrame4fscResolutionMode(resolutionMode)) {
        if (system == PAL) {
            return 20;
        }
        if (system == PAL_M) {
            return 4;
        }
    }
    return 4;
}

int recommendedCompressionFfv1SlicesForResolutionMode(int system, const QString &resolutionMode)
{
    if (isFullFrame4fscResolutionMode(resolutionMode)) {
        const QList<int> compatibleValues = supportedFullFrameFfv1SlicesValuesForSystem(system);
        if (!compatibleValues.isEmpty()) {
            return compatibleValues.first();
        }
    }
    return 4;
}

QStringList profileVideoProfileNames(const QJsonObject &profileObject)
{
    QStringList names;
    const QJsonValue videoProfileValue = profileObject.value(QStringLiteral("video_profile"));
    if (videoProfileValue.isString()) {
        const QString name = videoProfileValue.toString().trimmed();
        if (!name.isEmpty()) {
            names << name;
        }
    } else if (videoProfileValue.isArray()) {
        const QJsonArray values = videoProfileValue.toArray();
        for (const QJsonValue &value : values) {
            if (!value.isString()) {
                continue;
            }
            const QString name = value.toString().trimmed();
            if (!name.isEmpty() && !names.contains(name)) {
                names << name;
            }
        }
    }
    return names;
}

void setOrAppendNumericOption(QJsonObject *videoProfileObject, const QString &optionName, int optionValue)
{
    if (!videoProfileObject) {
        return;
    }

    QJsonArray opts = videoProfileObject->value(QStringLiteral("opts")).toArray();
    for (int i = 0; i + 1 < opts.size(); ++i) {
        if (!opts.at(i).isString() || opts.at(i).toString() != optionName) {
            continue;
        }
        opts.replace(i + 1, optionValue);
        videoProfileObject->insert(QStringLiteral("opts"), opts);
        return;
    }

    opts.append(optionName);
    opts.append(optionValue);
    videoProfileObject->insert(QStringLiteral("opts"), opts);
}


bool parseRealtimeProgressLine(const QString &line, ExportDialog::ExportProcessStat *stat)
{
    if (!stat) {
        return false;
    }
    static const QRegularExpression progressPattern(
        QStringLiteral("^\\s*[/\\\\\\-|●○]?\\s*([A-Za-z0-9_.\\-]+)\\s*(?:\\(([^)]+)\\))?\\s*([A-Za-z0-9_%/\\-]+)\\s*:\\s*([0-9,]+)(?:/([0-9,]+))?.*?errors:\\s*([0-9,]+)(?:.*?fps:\\s*([0-9.,]+))?"),
        QRegularExpression::CaseInsensitiveOption);
    const QRegularExpressionMatch match = progressPattern.match(line);
    if (!match.hasMatch()) {
        return false;
    }
    stat->process = match.captured(1).trimmed();
    stat->tbcType = match.captured(2).trimmed();
    stat->trackedName = match.captured(3).trimmed();
    stat->current = match.captured(4).trimmed();
    stat->total = match.captured(5).trimmed();
    stat->errors = match.captured(6).trimmed();
    stat->fps = match.captured(7).trimmed();
    if (stat->tbcType.isEmpty()) {
        stat->tbcType = QStringLiteral("—");
    }
    if (stat->trackedName.isEmpty()) {
        stat->trackedName = QStringLiteral("—");
    }
    if (stat->total.isEmpty()) {
        stat->total = QStringLiteral("—");
    }
    if (stat->errors.isEmpty()) {
        stat->errors = QStringLiteral("0");
    }
    if (stat->fps.isEmpty()) {
        stat->fps = QStringLiteral("—");
    }
    return !stat->process.isEmpty();
}
bool parseFfmpegProgressLine(const QString &line, ExportDialog::ExportProcessStat *stat)
{
    if (!stat) {
        return false;
    }
    static const QRegularExpression ffmpegPattern(
        QStringLiteral("frame\\s*=\\s*([0-9,]+)\\s+fps\\s*=\\s*([0-9.,]+)"),
        QRegularExpression::CaseInsensitiveOption);
    QRegularExpressionMatchIterator matches = ffmpegPattern.globalMatch(line);
    if (!matches.hasNext()) {
        return false;
    }
    QRegularExpressionMatch match;
    while (matches.hasNext()) {
        match = matches.next();
    }

    stat->process = QStringLiteral("ffmpeg");
    stat->tbcType = QStringLiteral("—");
    stat->trackedName = QStringLiteral("frame");
    stat->current = match.captured(1).trimmed();
    stat->total = QStringLiteral("—");
    stat->errors = QStringLiteral("0");
    stat->fps = match.captured(2).trimmed();
    return !stat->current.isEmpty();
}

bool parseInfoProgressLine(const QString &line, ExportDialog::ExportProcessStat *stat)
{
    if (!stat) {
        return false;
    }

    static const QRegularExpression framesProcessedPattern(
        QStringLiteral("^\\s*Info:\\s*([0-9,]+)\\s+frames\\s+processed\\s*-\\s*([0-9.,]+)\\s+FPS\\s*$"),
        QRegularExpression::CaseInsensitiveOption);
    QRegularExpressionMatch match = framesProcessedPattern.match(line);
    if (match.hasMatch()) {
        stat->process = QStringLiteral("ld-chroma-decoder");
        stat->tbcType = QStringLiteral("—");
        stat->trackedName = QStringLiteral("frame");
        stat->current = match.captured(1).trimmed();
        stat->total = QStringLiteral("—");
        stat->errors = QStringLiteral("0");
        stat->fps = match.captured(2).trimmed();
        return !stat->current.isEmpty();
    }

    static const QRegularExpression writtenFramePattern(
        QStringLiteral("^\\s*Info:\\s*Processed\\s+and\\s+written\\s+frame\\s+([0-9,]+)\\s*$"),
        QRegularExpression::CaseInsensitiveOption);
    match = writtenFramePattern.match(line);
    if (match.hasMatch()) {
        stat->process = QStringLiteral("ld-chroma-decoder");
        stat->tbcType = QStringLiteral("—");
        stat->trackedName = QStringLiteral("frame");
        stat->current = match.captured(1).trimmed();
        stat->total = QStringLiteral("—");
        stat->errors = QStringLiteral("0");
        stat->fps = QStringLiteral("—");
        return !stat->current.isEmpty();
    }

    static const QRegularExpression startLengthPattern(
        QStringLiteral("^\\s*Info:\\s*Processing\\s+from\\s+start\\s+frame\\s*#\\s*([0-9,]+)\\s+with\\s+a\\s+length\\s+of\\s+([0-9,]+)\\s+frames\\s*$"),
        QRegularExpression::CaseInsensitiveOption);
    match = startLengthPattern.match(line);
    if (match.hasMatch()) {
        const QString startFrameText = match.captured(1).trimmed();
        const QString totalText = match.captured(2).trimmed();
        QString startFrameDigits = startFrameText;
        bool startOk = false;
        const int startFrame = startFrameDigits.remove(QLatin1Char(',')).toInt(&startOk);
        stat->process = QStringLiteral("ld-chroma-decoder");
        stat->tbcType = QStringLiteral("—");
        stat->trackedName = QStringLiteral("frame");
        stat->current = startOk && startFrame > 0 ? QString::number(startFrame - 1) : QStringLiteral("0");
        stat->total = totalText.isEmpty() ? QStringLiteral("—") : totalText;
        stat->errors = QStringLiteral("0");
        stat->fps = QStringLiteral("—");
        return true;
    }

    static const QRegularExpression dropoutWorkloadPattern(
        QStringLiteral("^\\s*Info:\\s*Using\\s+[0-9,]+\\s+threads\\s+to\\s+process\\s+([0-9,]+)\\s+frames\\s*$"),
        QRegularExpression::CaseInsensitiveOption);
    match = dropoutWorkloadPattern.match(line);
    if (match.hasMatch()) {
        stat->process = QStringLiteral("ld-dropout-correct");
        stat->tbcType = QStringLiteral("—");
        stat->trackedName = QStringLiteral("frame");
        stat->current = QStringLiteral("0");
        stat->total = match.captured(1).trimmed();
        stat->errors = QStringLiteral("0");
        stat->fps = QStringLiteral("—");
        return true;
    }

    return false;
}

bool parseCompletionSummaryLine(const QString &line, ExportDialog::ExportProcessStat *stat)
{
    if (!stat) {
        return false;
    }

    static const QRegularExpression dropoutCompletePattern(
        QStringLiteral("^\\s*Info:\\s*Dropout correction complete\\s*-\\s*([0-9,]+)\\s+frames?\\s+in\\s+[0-9.]+\\s+seconds\\s*\\(\\s*([0-9.,]+)\\s+FPS\\s*\\)"),
        QRegularExpression::CaseInsensitiveOption);
    QRegularExpressionMatch match = dropoutCompletePattern.match(line);
    if (match.hasMatch()) {
        stat->process = QStringLiteral("ld-dropout-correct");
        stat->tbcType = QStringLiteral("—");
        stat->trackedName = QStringLiteral("frame");
        stat->current = match.captured(1).trimmed();
        stat->total = stat->current;
        stat->errors = QStringLiteral("0");
        stat->fps = match.captured(2).trimmed();
        return !stat->current.isEmpty();
    }

    static const QRegularExpression processingCompletePattern(
        QStringLiteral("^\\s*Info:\\s*Processing complete\\s*-\\s*([0-9,]+)\\s+frames?\\s+in\\s+[0-9.]+\\s+seconds\\s*\\(\\s*([0-9.,]+)\\s+FPS\\s*\\)"),
        QRegularExpression::CaseInsensitiveOption);
    match = processingCompletePattern.match(line);
    if (match.hasMatch()) {
        stat->process = QStringLiteral("ld-chroma-decoder");
        stat->tbcType = QStringLiteral("—");
        stat->trackedName = QStringLiteral("frame");
        stat->current = match.captured(1).trimmed();
        stat->total = stat->current;
        stat->errors = QStringLiteral("0");
        stat->fps = match.captured(2).trimmed();
        return !stat->current.isEmpty();
    }

    return false;
}

bool parseProgressLine(const QString &line, ExportDialog::ExportProcessStat *stat)
{
    return parseRealtimeProgressLine(line, stat)
        || parseInfoProgressLine(line, stat)
        || parseFfmpegProgressLine(line, stat)
        || parseCompletionSummaryLine(line, stat);
}

bool executableSupportsOption(const QString &program, const QString &option)
{
    if (program.isEmpty() || option.isEmpty()) {
        return false;
    }

    static QHash<QString, bool> supportCache;
    const QString cacheKey = program + QStringLiteral("|") + option;
    if (supportCache.contains(cacheKey)) {
        return supportCache.value(cacheKey);
    }

    QProcess helpProcess;
    helpProcess.setProcessChannelMode(QProcess::MergedChannels);
    helpProcess.start(program, QStringList() << QStringLiteral("--help"));

    bool supportsOption = false;
    if (helpProcess.waitForStarted(3000) && helpProcess.waitForFinished(5000)) {
        const QString helpOutput = QString::fromLocal8Bit(helpProcess.readAllStandardOutput());
        supportsOption = helpOutput.contains(option);
    }

    supportCache.insert(cacheKey, supportsOption);
    return supportsOption;
}

bool executableSupportsD1OutputSizing(const QString &program)
{
    return executableSupportsOption(program, QStringLiteral("--d1"))
           || executableSupportsOption(program, QStringLiteral("--standard"));
}

bool shouldUseD1OutputSizing(const QString &resolutionMode, const QString &outputResolutionMode)
{
    if (resolutionMode != QStringLiteral("active_area")) {
        return false;
    }

    return outputResolutionMode == QStringLiteral("default_safe")
           || outputResolutionMode == QStringLiteral("bt601_720");
}

QString normalizedProxyCodecId(const QString &proxyCodecId)
{
    const QString normalized = proxyCodecId.trimmed().toLower();
    if (normalized == QStringLiteral("hevc") || normalized == QStringLiteral("h265")) {
        return QStringLiteral("hevc");
    }
    if (normalized == QStringLiteral("av1")) {
        return QStringLiteral("av1");
    }
    return QStringLiteral("h264");
}

QString dropoutInterfieldCorrectionValue(const TbcSource *source, const QString &correctionMode)
{
    if (correctionMode == QStringLiteral("intra")) {
        return QStringLiteral("none");
    }
    const bool interfieldMode = correctionMode == QStringLiteral("innerfield")
                                || correctionMode == QStringLiteral("interfield");
    if (!interfieldMode) {
        return QString();
    }
    if (!source) {
        return QStringLiteral("combined");
    }
    switch (source->getSourceMode()) {
    case TbcSource::LUMA_SOURCE:
        return QStringLiteral("luma");
    case TbcSource::CHROMA_SOURCE:
        return QStringLiteral("chroma");
    case TbcSource::BOTH_SOURCES:
    case TbcSource::ONE_SOURCE:
    default:
        return QStringLiteral("combined");
    }
}

QString formatVitcTimecodeForFfmpeg(const VitcDecoder::Vitc &vitc)
{
    if (!vitc.isValid) {
        return QString();
    }
    const QChar separator = vitc.isDropFrame ? QLatin1Char(';') : QLatin1Char(':');
    return QStringLiteral("%1:%2:%3%4%5")
        .arg(vitc.hour, 2, 10, QLatin1Char('0'))
        .arg(vitc.minute, 2, 10, QLatin1Char('0'))
        .arg(vitc.second, 2, 10, QLatin1Char('0'))
        .arg(separator)
        .arg(vitc.frame, 2, 10, QLatin1Char('0'));
}

QString firstValidVitcTimecodeForRange(const QString &metadataSnapshotPath,
                                       int startFrameOneBased,
                                       int lengthFrames)
{
    if (metadataSnapshotPath.trimmed().isEmpty()) {
        return QString();
    }

    LdDecodeMetaData metadata;
    if (!metadata.read(metadataSnapshotPath)) {
        return QString();
    }

    const int totalFrames = metadata.getNumberOfFrames();
    if (totalFrames < 1) {
        return QString();
    }

    const int clampedStartFrame = qBound(1, qMax(1, startFrameOneBased), totalFrames);
    const int maxLength = qMax(1, totalFrames - clampedStartFrame + 1);
    const int clampedLength = qBound(1, qMax(1, lengthFrames), maxLength);
    const int clampedEndFrame = qBound(clampedStartFrame,
                                       clampedStartFrame + clampedLength - 1,
                                       totalFrames);

    const int startFieldOneBased = metadata.getFirstFieldNumber(clampedStartFrame);
    const int endFieldOneBased = metadata.getSecondFieldNumber(clampedEndFrame);
    if (startFieldOneBased < 1 || endFieldOneBased < startFieldOneBased) {
        return QString();
    }

    const VideoSystem system = metadata.getVideoParameters().system;
    for (int fieldNumber = startFieldOneBased; fieldNumber <= endFieldOneBased; ++fieldNumber) {
        const LdDecodeMetaData::Vitc &fieldVitc = metadata.getFieldVitc(fieldNumber);
        if (!fieldVitc.inUse) {
            continue;
        }
        const VitcDecoder::Vitc decoded = VitcDecoder::decode(fieldVitc.vitcData, system);
        const QString timecode = formatVitcTimecodeForFfmpeg(decoded);
        if (!timecode.isEmpty()) {
            return timecode;
        }
    }
    return QString();
}

}

ExportDialog::ExportDialog(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::ExportDialog)
{
    ui->setupUi(this);

    ui->inputLineEdit->setReadOnly(true);
    ui->profileComboBox->setEditable(false);
    if (ui->profileLabel) {
        ui->profileLabel->setText(tr("Encoding"));
    }
    const auto configureCompactedProfileControl = [](QLabel *label, QComboBox *comboBox, const QString &tooltipText) {
        if (comboBox) {
            comboBox->setToolTip(tooltipText);
        }
        if (label) {
            label->setToolTip(tooltipText);
            label->setVisible(false);
        }
    };
    configureCompactedProfileControl(ui->outputResolutionModeLabel,
                                     ui->outputResolutionModeComboBox,
                                     tr("Output resolution preset for the selected framing mode."));
    configureCompactedProfileControl(ui->mainContainerLabel,
                                     ui->mainContainerComboBox,
                                     tr("Main output container format."));
    configureCompactedProfileControl(ui->mainBitDepthLabel,
                                     ui->mainBitDepthComboBox,
                                     tr("Main output bit depth."));
    configureCompactedProfileControl(ui->audioProfileLabel,
                                     ui->audioProfileComboBox,
                                     tr("Audio encoding profile."));
    if (ui->mainContainerComboBox) {
        ui->mainContainerComboBox->clear();
        ui->mainContainerComboBox->addItem(tr("MKV"), QStringLiteral("mkv"));
        ui->mainContainerComboBox->addItem(tr("MXF"), QStringLiteral("mxf"));
        ui->mainContainerComboBox->addItem(tr("MOV"), QStringLiteral("mov"));
        ui->mainContainerComboBox->addItem(tr("MP4"), QStringLiteral("mp4"));
        const int defaultIndex = ui->mainContainerComboBox->findData(QStringLiteral("mkv"));
        ui->mainContainerComboBox->setCurrentIndex(defaultIndex >= 0 ? defaultIndex : 0);
    }
    if (ui->mainBitDepthComboBox) {
        ui->mainBitDepthComboBox->clear();
        ui->mainBitDepthComboBox->addItem(tr("10-bit"), 10);
        ui->mainBitDepthComboBox->addItem(tr("8-bit"), 8);
        const int defaultIndex = ui->mainBitDepthComboBox->findData(10);
        ui->mainBitDepthComboBox->setCurrentIndex(defaultIndex >= 0 ? defaultIndex : 0);
    }
    if (ui->resolutionModeComboBox) {
        ui->resolutionModeComboBox->clear();
        ui->resolutionModeComboBox->addItem(tr("Active Area"), QStringLiteral("active_area"));
        ui->resolutionModeComboBox->addItem(tr("Active + VBI"), QStringLiteral("active_vbi"));
        ui->resolutionModeComboBox->addItem(tr("Full-Frame 4fsc"), QStringLiteral("full_frame_4fsc"));
        ui->resolutionModeComboBox->addItem(tr("Hybrid 4fsc"), QStringLiteral("hybrid_4fsc"));
        ui->resolutionModeComboBox->addItem(tr("User Defined"), QStringLiteral("user_defined"));
        const int activeAreaIndex = ui->resolutionModeComboBox->findData(QStringLiteral("active_area"));
        ui->resolutionModeComboBox->setCurrentIndex(activeAreaIndex >= 0 ? activeAreaIndex : 0);
    }
    if (ui->outputResolutionModeComboBox) {
        ui->outputResolutionModeComboBox->clear();
    }
    if (ui->audioProfileComboBox) {
        ui->audioProfileComboBox->clear();
        ui->audioProfileComboBox->addItem(tr("FLAC 16-bit"), QStringLiteral("flac_16"));
        ui->audioProfileComboBox->addItem(tr("FLAC 24-bit"), QStringLiteral("flac_24"));
        ui->audioProfileComboBox->addItem(tr("PCM 16-bit"), QStringLiteral("pcm_16"));
        ui->audioProfileComboBox->addItem(tr("PCM 24-bit"), QStringLiteral("pcm_24"));
        ui->audioProfileComboBox->addItem(tr("AAC 16-bit"), QStringLiteral("aac_16"));
        ui->audioProfileComboBox->addItem(tr("AAC 24-bit"), QStringLiteral("aac_24"));
        ui->audioProfileComboBox->addItem(tr("Opus 16-bit"), QStringLiteral("opus_16"));
        ui->audioProfileComboBox->addItem(tr("Opus 24-bit"), QStringLiteral("opus_24"));
        const int pcm24Index = ui->audioProfileComboBox->findData(QStringLiteral("pcm_24"));
        ui->audioProfileComboBox->setCurrentIndex(pcm24Index >= 0 ? pcm24Index : 0);
    }
    if (ui->proresVariantComboBox) {
        ui->proresVariantComboBox->clear();
        ui->proresVariantComboBox->addItem(tr("422 (STD/Standard)"), QStringLiteral("prores"));
        ui->proresVariantComboBox->addItem(tr("422 LT"), QStringLiteral("prores_lt"));
        ui->proresVariantComboBox->addItem(tr("422 Proxy"), QStringLiteral("prores_proxy"));
        ui->proresVariantComboBox->addItem(tr("422 HQ"), QStringLiteral("prores_hq"));
        ui->proresVariantComboBox->addItem(tr("4444 XQ"), QStringLiteral("prores_4444xq"));
        const int defaultIndex = ui->proresVariantComboBox->findData(QStringLiteral("prores_hq"));
        ui->proresVariantComboBox->setCurrentIndex(defaultIndex >= 0 ? defaultIndex : 0);
    }
    if (ui->webCodecComboBox) {
        ui->webCodecComboBox->clear();
        ui->webCodecComboBox->addItem(tr("AVC/H.264"), QStringLiteral("h264"));
        ui->webCodecComboBox->addItem(tr("HEVC/H.265"), QStringLiteral("hevc"));
        ui->webCodecComboBox->addItem(tr("AV1"), QStringLiteral("av1"));
        const int defaultIndex = ui->webCodecComboBox->findData(QStringLiteral("h264"));
        ui->webCodecComboBox->setCurrentIndex(defaultIndex >= 0 ? defaultIndex : 0);
    }
    if (ui->avcRangeComboBox) {
        ui->avcRangeComboBox->clear();
        ui->avcRangeComboBox->addItem(tr("High quality"), QStringLiteral("standard"));
        ui->avcRangeComboBox->addItem(tr("Web"), QStringLiteral("web"));
        ui->avcRangeComboBox->addItem(tr("Lossless"), QStringLiteral("lossless"));
        const int defaultIndex = ui->avcRangeComboBox->findData(QStringLiteral("standard"));
        ui->avcRangeComboBox->setCurrentIndex(defaultIndex >= 0 ? defaultIndex : 0);
    }
    if (ui->hevcRangeComboBox) {
        ui->hevcRangeComboBox->clear();
        ui->hevcRangeComboBox->addItem(tr("High quality"), QStringLiteral("standard"));
        ui->hevcRangeComboBox->addItem(tr("Web"), QStringLiteral("web"));
        ui->hevcRangeComboBox->addItem(tr("Lossless"), QStringLiteral("lossless"));
        const int defaultIndex = ui->hevcRangeComboBox->findData(QStringLiteral("standard"));
        ui->hevcRangeComboBox->setCurrentIndex(defaultIndex >= 0 ? defaultIndex : 0);
    }
    if (ui->dropoutModeComboBox) {
        ui->dropoutModeComboBox->clear();
        ui->dropoutModeComboBox->addItem(tr("Basic"), QStringLiteral("basic"));
        ui->dropoutModeComboBox->addItem(tr("Heavy"), QStringLiteral("heavy"));
        ui->dropoutModeComboBox->addItem(tr("Disabled"), QStringLiteral("disabled"));
        const int basicModeIndex = ui->dropoutModeComboBox->findData(QStringLiteral("basic"));
        ui->dropoutModeComboBox->setCurrentIndex(basicModeIndex >= 0 ? basicModeIndex : 0);
    }
    if (ui->dropoutFieldModeComboBox) {
        ui->dropoutFieldModeComboBox->clear();
        ui->dropoutFieldModeComboBox->addItem(tr("Intra"), QStringLiteral("intra"));
        ui->dropoutFieldModeComboBox->addItem(tr("Interfield"), QStringLiteral("innerfield"));
        const int interfieldModeIndex = ui->dropoutFieldModeComboBox->findData(QStringLiteral("innerfield"));
        ui->dropoutFieldModeComboBox->setCurrentIndex(interfieldModeIndex >= 0 ? interfieldModeIndex : 0);
    }
    const auto updateDropoutFieldModeControls = [this]() {
        const bool dropoutCorrectionEnabled = !ui->dropoutModeComboBox
                                              || ui->dropoutModeComboBox->currentData().toString() != QStringLiteral("disabled");
        if (ui->dropoutFieldModeLabel) {
            ui->dropoutFieldModeLabel->setEnabled(dropoutCorrectionEnabled);
        }
        if (ui->dropoutFieldModeComboBox) {
            ui->dropoutFieldModeComboBox->setEnabled(dropoutCorrectionEnabled);
        }
    };
    if (ui->dropoutModeComboBox) {
        connect(ui->dropoutModeComboBox,
                static_cast<void (QComboBox::*)(int)>(&QComboBox::currentIndexChanged),
                this,
                [updateDropoutFieldModeControls](int) { updateDropoutFieldModeControls(); });
    }
    updateDropoutFieldModeControls();
    if (ui->letterboxCropCheckBox) {
        ui->letterboxCropCheckBox->setChecked(false);
        connect(ui->letterboxCropCheckBox, &QCheckBox::toggled, this, [this](bool) {
            updateProfileDependentControls();
        });
    }
    if (ui->forceAnamorphicCheckBox) {
        ui->forceAnamorphicCheckBox->setChecked(false);
        connect(ui->forceAnamorphicCheckBox, &QCheckBox::toggled, this, [this](bool) {
            updateProfileDependentControls();
        });
    }
    if (ui->resolutionModeComboBox) {
        connect(ui->resolutionModeComboBox,
                static_cast<void (QComboBox::*)(int)>(&QComboBox::currentIndexChanged),
                this,
                [this](int) {
                    refreshOutputResolutionModeOptions();
                    updateProfileDependentControls();
                });
    }
    refreshOutputResolutionModeOptions();
    if (ui->ffv1SlicesSpinBox) {
        ui->ffv1SlicesSpinBox->setMinimum(1);
        ui->ffv1SlicesSpinBox->setMaximum(32);
        ui->ffv1SlicesSpinBox->setValue(4);
        const QString tooltipText =
            tr("Supported FFV1 slice counts: %1").arg(supportedFfv1SlicesValuesText());
        ui->ffv1SlicesSpinBox->setToolTip(tooltipText);
        if (ui->ffv1SlicesLabel) {
            ui->ffv1SlicesLabel->setToolTip(tooltipText);
        }
    }
    if (ui->proxyCodecComboBox) {
        ui->proxyCodecComboBox->clear();
        ui->proxyCodecComboBox->addItem(tr("AVC/H.264"), QStringLiteral("h264"));
        ui->proxyCodecComboBox->addItem(tr("HEVC/H.265"), QStringLiteral("hevc"));
        ui->proxyCodecComboBox->addItem(tr("AV1"), QStringLiteral("av1"));
        const int h264Index = ui->proxyCodecComboBox->findData(QStringLiteral("h264"));
        ui->proxyCodecComboBox->setCurrentIndex(h264Index >= 0 ? h264Index : 0);
    }
    if (ui->generateProxyCheckBox) {
        ui->generateProxyCheckBox->setChecked(false);
        connect(ui->generateProxyCheckBox, &QCheckBox::toggled, this, [this](bool checked) {
            if ((!exportProcess || exportProcess->state() == QProcess::NotRunning)
                && (!parallelProxyProcess || parallelProxyProcess->state() == QProcess::NotRunning)) {
                splitStatsByFeed = checked;
                updateProcessStatsPaneMode();
                updateFeedLogPaneMode();
                resetProcessStats();
                initializeProcessStats();
            }
            updateProfileDependentControls();
            emit proxyGenerationPreferenceChanged(checked);
        });
    }
    if (ui->exportProfileConfigCheckBox) {
        ui->exportProfileConfigCheckBox->setChecked(false);
    }
    if (ui->exportProfileConfigLineEdit) {
        ui->exportProfileConfigLineEdit->setReadOnly(true);
        ui->exportProfileConfigLineEdit->setPlaceholderText(tr("Default tbc-video-export profile set"));
    }
    if (ui->exportProfileConfigLoadButton) {
        ui->exportProfileConfigLoadButton->setToolTip(
            tr("Load a custom tbc-video-export JSON profile set."));
    }
    if (ui->exportProfileConfigEjectButton) {
        ui->exportProfileConfigEjectButton->setToolTip(
            tr("Eject the default tbc-video-export profile set to an editable JSON file."));
    }
    updateExportProfileConfigPathUi();
    if (ui->exportMetadataCheckBox) {
        ui->exportMetadataCheckBox->setChecked(true);
        ui->exportMetadataCheckBox->setToolTip(
            tr("Include FFmpeg-compatible metadata and closed captions from tbc-export-metadata."));
    }
    if (ui->disableMetadataEmbeddingCheckBox) {
        ui->disableMetadataEmbeddingCheckBox->setChecked(false);
        ui->disableMetadataEmbeddingCheckBox->setToolTip(
            tr("Disable embedding the decode .tbc.json file as an attachment in supported containers."));
    }
    const auto configureRangeTimecodeLineEdit = [this](QLineEdit *lineEdit) {
        if (!lineEdit) {
            return;
        }
        lineEdit->setPlaceholderText(tr("HH:MM:SS:FF"));
        lineEdit->setMaxLength(15);
        lineEdit->setAlignment(Qt::AlignCenter);
        lineEdit->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);

        QFont timecodeFont = lineEdit->font();
        timecodeFont.setPointSize(qMax(12, timecodeFont.pointSize() + 3));
        lineEdit->setFont(timecodeFont);
        lineEdit->setTextMargins(3, 0, 1, 0);
        lineEdit->setMinimumHeight(30);
        lineEdit->setMaximumHeight(30);

        const QFontMetrics timecodeMetrics(timecodeFont);
        const int timecodeMinWidth = timecodeMetrics.horizontalAdvance(QStringLiteral("00:00:00:00")) + 8;
        lineEdit->setMinimumWidth(timecodeMinWidth);
        lineEdit->setMaximumWidth(timecodeMinWidth + 4);
    };
    configureRangeTimecodeLineEdit(ui->inPointTimecodeLineEdit);
    configureRangeTimecodeLineEdit(ui->outPointTimecodeLineEdit);
    if (ui->rangeLengthValueLabel) {
        QFont durationFont = ui->rangeLengthValueLabel->font();
        durationFont.setPointSize(qMax(10, durationFont.pointSize() + 1));
        ui->rangeLengthValueLabel->setFont(durationFont);
    }
    if (ui->actionLayout) {
        if (ui->actionSpacer) {
            ui->actionSpacer->changeSize(0, 0, QSizePolicy::Fixed, QSizePolicy::Minimum);
        }
        if (ui->outPointLabel && ui->outPointTimecodeLineEdit) {
            ui->actionLayout->removeWidget(ui->outPointLabel);
            const int outEditIndex = ui->actionLayout->indexOf(ui->outPointTimecodeLineEdit);
            ui->actionLayout->insertWidget(qMax(0, outEditIndex), ui->outPointLabel);
        }
        if (ui->rangeLengthLabel && ui->rangeLengthValueLabel && ui->outPointTimecodeLineEdit) {
            ui->actionLayout->removeWidget(ui->rangeLengthLabel);
            ui->actionLayout->removeWidget(ui->rangeLengthValueLabel);
            const int outEditIndex = ui->actionLayout->indexOf(ui->outPointTimecodeLineEdit);
            int insertIndex = qMax(0, outEditIndex + 1);
            ui->actionLayout->insertWidget(insertIndex++, ui->rangeLengthLabel);
            ui->actionLayout->insertWidget(insertIndex, ui->rangeLengthValueLabel);
        }
        if (ui->cancelButton) {
            const int cancelIndex = ui->actionLayout->indexOf(ui->cancelButton);
            if (cancelIndex >= 0) {
                ui->actionLayout->insertStretch(cancelIndex, 1);
            }
        }
        ui->actionLayout->invalidate();
    }
    if (ui->inPointSpinBox && ui->outPointSpinBox) {
        ui->inPointSpinBox->setMinimum(1);
        ui->inPointSpinBox->setMaximum(1);
        ui->inPointSpinBox->setValue(1);
        ui->outPointSpinBox->setMinimum(1);
        ui->outPointSpinBox->setMaximum(1);
        ui->outPointSpinBox->setValue(1);

        connect(ui->inPointSpinBox, &QSpinBox::valueChanged, this, [this](int value) {
            if (!ui->outPointSpinBox) {
                return;
            }
            if (ui->outPointSpinBox->value() < value) {
                ui->outPointSpinBox->setValue(value);
            }
            updateRangeLengthLabel();
            syncRangeTimecodeEditors();
            emitRangeSelectionChanged(false);
        });
        connect(ui->outPointSpinBox, &QSpinBox::valueChanged, this, [this](int value) {
            if (!ui->inPointSpinBox) {
                return;
            }
            if (value < ui->inPointSpinBox->value()) {
                ui->inPointSpinBox->setValue(value);
            }
            updateRangeLengthLabel();
            syncRangeTimecodeEditors();
            emitRangeSelectionChanged(false);
        });
    }
    if (ui->inPointSpinBox && ui->outPointSpinBox
        && ui->inPointTimecodeLineEdit && ui->outPointTimecodeLineEdit) {
        ui->inPointSpinBox->setVisible(false);
        ui->outPointSpinBox->setVisible(false);
        connect(ui->inPointTimecodeLineEdit, &QLineEdit::editingFinished, this, [this]() {
            if (!ui || !ui->inPointTimecodeLineEdit || !ui->inPointSpinBox || !ui->outPointSpinBox) {
                return;
            }
            bool ok = false;
            int inFrame = timecodeToFrame(ui->inPointTimecodeLineEdit->text(), &ok);
            if (!ok) {
                syncRangeTimecodeEditors();
                appendStatus(tr("Invalid In timecode. Use HH:MM:SS:FF."));
                return;
            }
            inFrame = qBound(ui->inPointSpinBox->minimum(), inFrame, ui->inPointSpinBox->maximum());
            ui->inPointSpinBox->setValue(inFrame);
            if (ui->outPointSpinBox->value() < inFrame) {
                ui->outPointSpinBox->setValue(inFrame);
            }
            syncRangeTimecodeEditors();
        });
        connect(ui->outPointTimecodeLineEdit, &QLineEdit::editingFinished, this, [this]() {
            if (!ui || !ui->outPointTimecodeLineEdit || !ui->outPointSpinBox || !ui->inPointSpinBox) {
                return;
            }
            bool ok = false;
            int outFrame = timecodeToFrame(ui->outPointTimecodeLineEdit->text(), &ok);
            if (!ok) {
                syncRangeTimecodeEditors();
                appendStatus(tr("Invalid Out timecode. Use HH:MM:SS:FF."));
                return;
            }
            outFrame = qBound(ui->outPointSpinBox->minimum(), outFrame, ui->outPointSpinBox->maximum());
            ui->outPointSpinBox->setValue(outFrame);
            if (outFrame < ui->inPointSpinBox->value()) {
                ui->inPointSpinBox->setValue(outFrame);
            }
            syncRangeTimecodeEditors();
        });
    }
    updateRangeLengthLabel();
    mainProcessStatsTable = ui->processStatsTable;
    const auto configureProcessStatsTable = [this](QTableWidget *table, const QString &toolTip) {
        if (!table) {
            return;
        }
        table->setColumnCount(7);
        table->setHorizontalHeaderLabels({
            tr("Process"),
            tr("TBC"),
            tr("Track"),
            tr("Current"),
            tr("Total"),
            tr("Errors"),
            tr("FPS")
        });
        table->verticalHeader()->setVisible(false);
        table->setEditTriggers(QAbstractItemView::NoEditTriggers);
        table->setSelectionMode(QAbstractItemView::NoSelection);
        table->setSelectionBehavior(QAbstractItemView::SelectRows);
        table->setAlternatingRowColors(true);
        table->horizontalHeader()->setStretchLastSection(true);
        table->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
        table->setMinimumHeight(110);
        table->setToolTip(toolTip);
        table->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    };
    configureProcessStatsTable(mainProcessStatsTable, tr("Main export process stats"));
    if (mainProcessStatsTable && ui->mainLayout) {
        const int processStatsIndex = ui->mainLayout->indexOf(mainProcessStatsTable);
        processStatsSplitter = new QSplitter(Qt::Horizontal, this);
        processStatsSplitter->setObjectName(QStringLiteral("processStatsSplitter"));
        processStatsSplitter->setChildrenCollapsible(false);
        processStatsSplitter->setHandleWidth(2);
        processStatsSplitter->setMinimumHeight(110);
        processStatsSplitter->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

        proxyProcessStatsTable = new QTableWidget(processStatsSplitter);
        proxyProcessStatsTable->setObjectName(QStringLiteral("proxyProcessStatsTable"));
        configureProcessStatsTable(proxyProcessStatsTable, tr("Proxy export process stats"));

        ui->mainLayout->removeWidget(mainProcessStatsTable);
        mainProcessStatsTable->setParent(processStatsSplitter);
        processStatsSplitter->addWidget(mainProcessStatsTable);
        processStatsSplitter->addWidget(proxyProcessStatsTable);
        processStatsSplitter->setStretchFactor(0, 1);
        processStatsSplitter->setStretchFactor(1, 1);
        processStatsSplitter->setSizes(QList<int>({1, 1}));

        if (processStatsIndex >= 0) {
            ui->mainLayout->insertWidget(processStatsIndex, processStatsSplitter);
        } else {
            ui->mainLayout->addWidget(processStatsSplitter);
        }
    }
    updateProcessStatsPaneMode();
    if (ui->logTextEdit) {
        ui->logTextEdit->setMaximumBlockCount(10);
        ui->logTextEdit->setPlaceholderText(tr("Main export feed"));
        ui->logTextEdit->setLineWrapMode(QPlainTextEdit::NoWrap);
        ui->logTextEdit->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        if (ui->proxyLogTextEdit) {
            ui->proxyLogTextEdit->setMaximumBlockCount(10);
            ui->proxyLogTextEdit->setPlaceholderText(tr("Proxy export feed"));
            ui->proxyLogTextEdit->setLineWrapMode(QPlainTextEdit::NoWrap);
            ui->proxyLogTextEdit->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        }
        if (ui->feedLogSplitter) {
            ui->feedLogSplitter->setChildrenCollapsible(false);
            ui->feedLogSplitter->setHandleWidth(2);
            ui->feedLogSplitter->setStretchFactor(0, 1);
            ui->feedLogSplitter->setStretchFactor(1, 1);
            ui->feedLogSplitter->setSizes(QList<int>({1, 1}));
            ui->feedLogSplitter->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        }
        updateFeedLogPaneSizing();
    }
    if (ui->mainLayout) {
        QWidget *statsWidget = processStatsSplitter
                                   ? static_cast<QWidget *>(processStatsSplitter)
                                   : static_cast<QWidget *>(mainProcessStatsTable);
        if (statsWidget) {
            const int statsIndex = ui->mainLayout->indexOf(statsWidget);
            if (statsIndex >= 0) {
                ui->mainLayout->setStretch(statsIndex, 2);
            }
        }
        if (ui->feedLogSplitter) {
            const int feedIndex = ui->mainLayout->indexOf(ui->feedLogSplitter);
            if (feedIndex >= 0) {
                ui->mainLayout->setStretch(feedIndex, 3);
            }
        }
    }
    if (ui->cancelButton) {
        ui->cancelButton->setEnabled(false);
    }
    connect(ui->outputLineEdit, &QLineEdit::textEdited, this, [this]() {
        outputAutoSet = false;
    });
    if (ui->profileComboBox) {
        connect(ui->profileComboBox, &QComboBox::currentTextChanged, this,
                [this](const QString &) { updateProfileDependentControls(); });
    }
    const QList<QLineEdit *> audioTrackLineEdits = {
        ui->audio1LineEdit,
        ui->audio2LineEdit,
        ui->audio3LineEdit,
        ui->audio4LineEdit
    };
    for (QLineEdit *audioTrackLineEdit : audioTrackLineEdits) {
        if (!audioTrackLineEdit) {
            continue;
        }
        connect(audioTrackLineEdit, &QLineEdit::textChanged, this, [audioTrackLineEdit](const QString &text) {
            if (!text.contains(QStringLiteral("file://"), Qt::CaseInsensitive)
                && !text.contains(QLatin1Char('\r'))
                && !text.contains(QLatin1Char('\n'))) {
                return;
            }
            normalizeAudioTrackLineEditPath(audioTrackLineEdit);
        });
        connect(audioTrackLineEdit, &QLineEdit::editingFinished, this, [audioTrackLineEdit]() {
            normalizeAudioTrackLineEditPath(audioTrackLineEdit);
        });
    }
    if (ui->proresVariantComboBox) {
        connect(ui->proresVariantComboBox, &QComboBox::currentTextChanged, this,
                [this](const QString &) { updateProfileDependentControls(); });
    }
    if (ui->webCodecComboBox) {
        connect(ui->webCodecComboBox, &QComboBox::currentTextChanged, this,
                [this](const QString &) { updateProfileDependentControls(); });
    }
    if (ui->avcRangeComboBox) {
        connect(ui->avcRangeComboBox, &QComboBox::currentTextChanged, this,
                [this](const QString &) { updateProfileDependentControls(); });
    }
    if (ui->hevcRangeComboBox) {
        connect(ui->hevcRangeComboBox, &QComboBox::currentTextChanged, this,
                [this](const QString &) { updateProfileDependentControls(); });
    }

    exportProcess = new QProcess(this);
    connect(exportProcess, &QProcess::finished, this, &ExportDialog::handleProcessFinished);
    connect(exportProcess, &QProcess::errorOccurred, this, &ExportDialog::handleProcessError);
    connect(exportProcess, &QProcess::readyReadStandardOutput, this, &ExportDialog::handleProcessStdout);
    connect(exportProcess, &QProcess::readyReadStandardError, this, &ExportDialog::handleProcessStderr);
    connect(exportProcess, &QProcess::started, this, [this]() {
        const QString stageName = (activeRunStage == RunStage::ProxyExport)
                                      ? tr("Proxy generation")
                                      : tr("tbc-video-export");
        appendLog(tr("%1 started (PID %2).").arg(stageName).arg(exportProcess->processId()));
    });
    parallelProxyProcess = new QProcess(this);
    connect(parallelProxyProcess, &QProcess::finished,
            this, &ExportDialog::handleParallelProxyProcessFinished);
    connect(parallelProxyProcess, &QProcess::errorOccurred,
            this, &ExportDialog::handleParallelProxyProcessError);
    connect(parallelProxyProcess, &QProcess::readyReadStandardOutput,
            this, &ExportDialog::handleParallelProxyProcessStdout);
    connect(parallelProxyProcess, &QProcess::readyReadStandardError,
            this, &ExportDialog::handleParallelProxyProcessStderr);
    connect(parallelProxyProcess, &QProcess::started, this, [this]() {
        appendLog(tr("Parallel proxy export started (PID %1).").arg(parallelProxyProcess->processId()));
    });
    splitStatsByFeed = shouldGenerateProxyForSelection();
    updateProcessStatsPaneMode();
    updateFeedLogPaneMode();
    updateProfileDependentControls();

    appendStatus(tr("Ready."));
    appendLog(tr("Ready."));
}

ExportDialog::~ExportDialog()
{
    cleanupTemporaryMetadataSnapshot();
    delete ui;
}

void ExportDialog::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    updateFeedLogPaneSizing();
}
void ExportDialog::updateFeedLogPaneMode()
{
    if (!ui || !ui->logTextEdit) {
        return;
    }

    const bool splitFeedLogs = splitStatsByFeed && ui->proxyLogTextEdit;
    ui->logTextEdit->setVisible(true);
    if (ui->proxyLogTextEdit) {
        ui->proxyLogTextEdit->setVisible(splitFeedLogs);
    }
    if (!ui->feedLogSplitter) {
        return;
    }
    ui->feedLogSplitter->setHandleWidth(splitFeedLogs ? 2 : 0);
    if (splitFeedLogs) {
        ui->feedLogSplitter->setSizes(QList<int>({1, 1}));
    } else {
        ui->feedLogSplitter->setSizes(QList<int>({1, 0}));
    }
}

void ExportDialog::updateFeedLogPaneSizing()
{
    if (!ui || !ui->logTextEdit) {
        return;
    }

    const int lineHeight = QFontMetrics(ui->logTextEdit->font()).lineSpacing();
    const int baseVisibleLines = 8;
    const int basePadding = 10;
    const int dynamicExtraPx = qBound(20, height() / 12, 220);
    const int logHeight = (lineHeight * baseVisibleLines) + basePadding + dynamicExtraPx;
    const int dynamicVisibleLines = qMax(baseVisibleLines, (logHeight - basePadding) / qMax(1, lineHeight));
    const int maxLogBlocks = qMax(60, dynamicVisibleLines * 12);
    if (ui->logTextEdit->minimumHeight() != logHeight) {
        ui->logTextEdit->setMinimumHeight(logHeight);
    }
    if (ui->logTextEdit->maximumBlockCount() != maxLogBlocks) {
        ui->logTextEdit->setMaximumBlockCount(maxLogBlocks);
    }
    if (ui->proxyLogTextEdit && ui->proxyLogTextEdit->minimumHeight() != logHeight) {
        ui->proxyLogTextEdit->setMinimumHeight(logHeight);
    }
    if (ui->proxyLogTextEdit && ui->proxyLogTextEdit->maximumBlockCount() != maxLogBlocks) {
        ui->proxyLogTextEdit->setMaximumBlockCount(maxLogBlocks);
    }
    if (ui->feedLogSplitter && ui->feedLogSplitter->minimumHeight() != logHeight) {
        ui->feedLogSplitter->setMinimumHeight(logHeight);
    }
}
void ExportDialog::updateProcessStatsPaneMode()
{
    if (!mainProcessStatsTable && ui) {
        mainProcessStatsTable = ui->processStatsTable;
    }
    if (!mainProcessStatsTable) {
        return;
    }

    if (!processStatsSplitter) {
        if (proxyProcessStatsTable) {
            proxyProcessStatsTable->setVisible(splitStatsByFeed);
        }
        return;
    }

    mainProcessStatsTable->setVisible(true);
    if (proxyProcessStatsTable) {
        proxyProcessStatsTable->setVisible(splitStatsByFeed);
    }

    processStatsSplitter->setHandleWidth(splitStatsByFeed ? 2 : 0);
    if (splitStatsByFeed) {
        processStatsSplitter->setSizes(QList<int>({1, 1}));
    } else {
        processStatsSplitter->setSizes(QList<int>({1, 0}));
    }
}

QTableWidget *ExportDialog::processStatsTableForFeed(const QString &feedTag) const
{
    const QString normalizedFeedTag = normalizedStatsFeedTag(feedTag);
    if (splitStatsByFeed
        && normalizedFeedTag == kStatsFeedProxy
        && proxyProcessStatsTable) {
        return proxyProcessStatsTable;
    }
    if (mainProcessStatsTable) {
        return mainProcessStatsTable;
    }
    return ui ? ui->processStatsTable : nullptr;
}

void ExportDialog::setSource(TbcSource *source)
{
    tbcSource = source;
    updateFromSource();
    refreshResolutionOptions();
    refreshProfiles();
    updateProfileDependentControls();
}
void ExportDialog::setGenerateProxyEnabledPreference(bool enabled)
{
    if (!ui || !ui->generateProxyCheckBox) {
        return;
    }

    const QSignalBlocker blocker(ui->generateProxyCheckBox);
    ui->generateProxyCheckBox->setChecked(enabled);
    if ((!exportProcess || exportProcess->state() == QProcess::NotRunning)
        && (!parallelProxyProcess || parallelProxyProcess->state() == QProcess::NotRunning)) {
        splitStatsByFeed = enabled;
        updateProcessStatsPaneMode();
        updateFeedLogPaneMode();
        resetProcessStats();
        initializeProcessStats();
    }
    updateProfileDependentControls();
}

void ExportDialog::setExportProfileConfigPreference(bool enabled, const QString &configPath)
{
    if (!enabled) {
        exportProfileConfigPath.clear();
    } else {
        exportProfileConfigPath = configPath.trimmed();
        if (!exportProfileConfigPath.isEmpty()) {
            exportProfileConfigPath = QFileInfo(exportProfileConfigPath).absoluteFilePath();
        }
    }

    if (ui && ui->exportProfileConfigCheckBox) {
        const QSignalBlocker blocker(ui->exportProfileConfigCheckBox);
        ui->exportProfileConfigCheckBox->setChecked(enabled);
    }

    updateExportProfileConfigPathUi();
    refreshProfiles();
    updateProfileDependentControls();
}

void ExportDialog::updateExportProfileConfigPathUi()
{
    if (!ui || !ui->exportProfileConfigLineEdit) {
        return;
    }

    const QString nativePath = exportProfileConfigPath.isEmpty()
                                   ? QString()
                                   : QDir::toNativeSeparators(exportProfileConfigPath);
    ui->exportProfileConfigLineEdit->setText(nativePath);
    ui->exportProfileConfigLineEdit->setCursorPosition(0);

    const bool externalProfilesEnabled = ui->exportProfileConfigCheckBox
                                         && ui->exportProfileConfigCheckBox->isChecked();
    const QString tooltip = nativePath.isEmpty()
                                ? (externalProfilesEnabled
                                       ? tr("External profile set is enabled but no JSON profile file is selected yet.")
                                       : tr("Using the default built-in tbc-video-export profile set."))
                                : tr("Loaded JSON profile set: %1").arg(nativePath);
    ui->exportProfileConfigLineEdit->setToolTip(tooltip);
    if (ui->exportProfileConfigCheckBox) {
        ui->exportProfileConfigCheckBox->setToolTip(tooltip);
    }
}

void ExportDialog::emitExportProfileConfigPreferenceChanged()
{
    const bool enabled = ui
                         && ui->exportProfileConfigCheckBox
                         && ui->exportProfileConfigCheckBox->isChecked();
    emit exportProfileConfigPreferenceChanged(enabled, exportProfileConfigPath.trimmed());
}

void ExportDialog::setInPoint(int frameNumber)
{
    if (!ui || !ui->inPointSpinBox || !ui->outPointSpinBox) {
        return;
    }
    const int clamped = qBound(ui->inPointSpinBox->minimum(), frameNumber, ui->inPointSpinBox->maximum());
    ui->inPointSpinBox->setValue(clamped);
    if (ui->outPointSpinBox->value() < clamped) {
        ui->outPointSpinBox->setValue(clamped);
    }
    updateRangeLengthLabel();
    syncRangeTimecodeEditors();
}

void ExportDialog::setOutPoint(int frameNumber)
{
    if (!ui || !ui->inPointSpinBox || !ui->outPointSpinBox) {
        return;
    }
    const int clamped = qBound(ui->outPointSpinBox->minimum(), frameNumber, ui->outPointSpinBox->maximum());
    ui->outPointSpinBox->setValue(clamped);
    if (clamped < ui->inPointSpinBox->value()) {
        ui->inPointSpinBox->setValue(clamped);
    }
    updateRangeLengthLabel();
    syncRangeTimecodeEditors();
}

bool ExportDialog::hasAudioTracksConfigured() const
{
    if (!ui) {
        return false;
    }

    const QStringList trackCandidates = {
        ui->audio1LineEdit ? ui->audio1LineEdit->text().trimmed() : QString(),
        ui->audio2LineEdit ? ui->audio2LineEdit->text().trimmed() : QString(),
        ui->audio3LineEdit ? ui->audio3LineEdit->text().trimmed() : QString(),
        ui->audio4LineEdit ? ui->audio4LineEdit->text().trimmed() : QString()
    };
    for (const QString &candidate : trackCandidates) {
        if (!candidate.isEmpty()) {
            return true;
        }
    }
    return false;
}

void ExportDialog::loadAudioTracksForExport(const QStringList &trackFiles, const QStringList &trackNames)
{
    if (!ui) {
        return;
    }

    QLineEdit *trackFileEdits[] = {
        ui->audio1LineEdit,
        ui->audio2LineEdit,
        ui->audio3LineEdit,
        ui->audio4LineEdit
    };
    QLineEdit *trackNameEdits[] = {
        ui->audio1NameLineEdit,
        ui->audio2NameLineEdit,
        ui->audio3NameLineEdit,
        ui->audio4NameLineEdit
    };
    const int slotCount = static_cast<int>(sizeof(trackFileEdits) / sizeof(trackFileEdits[0]));

    for (int slot = 0; slot < slotCount; ++slot) {
        if (trackFileEdits[slot]) {
            trackFileEdits[slot]->clear();
        }
        if (trackNameEdits[slot]) {
            trackNameEdits[slot]->clear();
        }
    }

    int loadedTrackCount = 0;
    for (const QString &trackFileValue : trackFiles) {
        const QString trackFile = normalizeAudioTrackPathInput(trackFileValue);
        if (trackFile.isEmpty()) {
            continue;
        }
        if (loadedTrackCount >= slotCount) {
            break;
        }

        if (trackFileEdits[loadedTrackCount]) {
            trackFileEdits[loadedTrackCount]->setText(trackFile);
        }
        QString trackName = loadedTrackCount < trackNames.size()
                                ? trackNames.at(loadedTrackCount).trimmed()
                                : QString();
        if (trackName.isEmpty()) {
            trackName = QFileInfo(trackFile).completeBaseName();
        }
        if (trackNameEdits[loadedTrackCount]) {
            trackNameEdits[loadedTrackCount]->setText(trackName);
        }
        loadedTrackCount++;
    }

    if (loadedTrackCount > 0) {
        appendStatus(tr("Loaded %1 audio track(s) from Auto Audio Align.").arg(loadedTrackCount));
        appendLog(tr("Loaded %1 audio track(s) from Auto Audio Align.").arg(loadedTrackCount));
    }
}

void ExportDialog::refreshResolutionOptions()
{
    syncResolutionModeSelectionFromSource();
    refreshOutputResolutionModeOptions();
}

void ExportDialog::syncResolutionModeSelectionFromSource()
{
    if (!ui || !ui->resolutionModeComboBox) {
        return;
    }

    QString targetMode = ui->resolutionModeComboBox->currentData().toString();
    if (targetMode.isEmpty()) {
        targetMode = QStringLiteral("active_area");
    }

    if (tbcSource && tbcSource->getIsSourceLoaded() && !tbcSource->getIsMetadataOnly()) {
        const LdDecodeMetaData::VideoParameters &videoParameters = tbcSource->getVideoParameters();
        if (usesCustomVerticalFraming(videoParameters)) {
            targetMode = QStringLiteral("user_defined");
        } else if (targetMode == QStringLiteral("user_defined")) {
            targetMode = QStringLiteral("active_area");
        }
    } else if (targetMode == QStringLiteral("user_defined")) {
        targetMode = QStringLiteral("active_area");
    }

    const int modeIndex = ui->resolutionModeComboBox->findData(targetMode);
    if (modeIndex >= 0 && modeIndex != ui->resolutionModeComboBox->currentIndex()) {
        const QSignalBlocker blocker(ui->resolutionModeComboBox);
        ui->resolutionModeComboBox->setCurrentIndex(modeIndex);
    }
}

void ExportDialog::refreshOutputResolutionModeOptions()
{
    if (!ui || !ui->outputResolutionModeComboBox) {
        return;
    }

    const QString previousMode = effectiveOutputResolutionMode(ui->outputResolutionModeComboBox);
    const QString resolutionMode = effectiveResolutionMode(tbcSource, ui ? ui->resolutionModeComboBox : nullptr);

    int system = NTSC;
    LdDecodeMetaData::VideoParameters videoParameters;
    const bool hasVideoParameters = tbcSource && tbcSource->getIsSourceLoaded();
    if (hasVideoParameters) {
        videoParameters = tbcSource->getVideoParameters();
        system = videoParameters.system;
    }

    int activeNativeWidth = nativeActiveWidthForVideoParameters(videoParameters);
    int activeNativeHeight = nativeActiveHeightForVideoParameters(videoParameters);
    if (activeNativeWidth < 1) {
        activeNativeWidth = 720;
    }
    if (activeNativeHeight < 1) {
        activeNativeHeight = activeAreaOutputHeightForSystem(system);
    }

    const int standardHeight = activeAreaOutputHeightForSystem(system);
    const int imxHeight = activeVbiOutputHeightForSystem(system);
    const int vbiNativeHeight = vbiNativeOutputHeightForSystem(system);

    int fullFrameWidth = videoParameters.isValid ? qMax(1, videoParameters.fieldWidth) : 0;
    int fullFrameHeight = videoParameters.isValid ? qMax(1, (videoParameters.fieldHeight * 2) - 1) : 0;
    if (fullFrameWidth < 1) {
        fullFrameWidth = fullFrameWidthForSystemFallback(system);
    }
    if (fullFrameHeight < 1) {
        fullFrameHeight = fullFrameHeightForSystemFallback(system);
    }

    const QSignalBlocker blocker(ui->outputResolutionModeComboBox);
    ui->outputResolutionModeComboBox->clear();

    if (resolutionMode == QStringLiteral("active_vbi")) {
        ui->outputResolutionModeComboBox->addItem(tr("D10 720x%1").arg(imxHeight), QStringLiteral("default_safe"));
        ui->outputResolutionModeComboBox->addItem(tr("Native %1x%2")
                                                      .arg(activeNativeWidth)
                                                      .arg(vbiNativeHeight),
                                                  QStringLiteral("tool_native"));
    } else if (isFullFrame4fscResolutionMode(resolutionMode)) {
        ui->outputResolutionModeComboBox->addItem(tr("Native %1x%2")
                                                      .arg(fullFrameWidth)
                                                      .arg(fullFrameHeight),
                                                  QStringLiteral("tool_native"));
    } else if (resolutionMode == QStringLiteral("user_defined")) {
        ui->outputResolutionModeComboBox->addItem(tr("Native %1x%2")
                                                      .arg(activeNativeWidth)
                                                      .arg(activeNativeHeight),
                                                  QStringLiteral("tool_native"));
    } else {
        ui->outputResolutionModeComboBox->addItem(tr("D1 720x%1").arg(standardHeight), QStringLiteral("default_safe"));
        if (system != PAL) {
            ui->outputResolutionModeComboBox->addItem(tr("SD 720x480 (4:3)"),
                                                      QStringLiteral("ntsc_480_43"));
            ui->outputResolutionModeComboBox->addItem(tr("SD 720x480 (16:9)"),
                                                      QStringLiteral("ntsc_480_169"));
        }
        ui->outputResolutionModeComboBox->addItem(tr("Native %1x%2")
                                                      .arg(activeNativeWidth)
                                                      .arg(activeNativeHeight),
                                                  QStringLiteral("tool_native"));
        ui->outputResolutionModeComboBox->addItem(tr("BT.601 702x%1")
                                                      .arg(standardHeight),
                                                  QStringLiteral("bt601_702"));
        ui->outputResolutionModeComboBox->addItem(tr("Square 768x%1")
                                                      .arg(standardHeight),
                                                  QStringLiteral("square_768"));
    }

    int targetIndex = ui->outputResolutionModeComboBox->findData(previousMode);
    if (targetIndex < 0) {
        const QString fallbackMode = isFullFrame4fscResolutionMode(resolutionMode)
                                         ? QStringLiteral("tool_native")
                                         : supportsOutputResolutionResample(resolutionMode)
                                               ? QStringLiteral("default_safe")
                                               : QStringLiteral("tool_native");
        targetIndex = ui->outputResolutionModeComboBox->findData(fallbackMode);
    }
    if (targetIndex < 0 && ui->outputResolutionModeComboBox->count() > 0) {
        targetIndex = 0;
    }
    if (targetIndex >= 0) {
        ui->outputResolutionModeComboBox->setCurrentIndex(targetIndex);
    }

    const bool controlsEnabled = exportAvailable
                                 && exportProcess
                                 && exportProcess->state() == QProcess::NotRunning;
    updateOutputResolutionModeControlsEnabledState(controlsEnabled);
}

void ExportDialog::updateOutputResolutionModeControlsEnabledState(bool enabled)
{
    if (!ui) {
        return;
    }
    const QString framingMode = effectiveResolutionMode(tbcSource, ui ? ui->resolutionModeComboBox : nullptr);
    const bool outputModeRelevant = supportsOutputResolutionResample(framingMode);
    if (ui->outputResolutionModeLabel) {
        ui->outputResolutionModeLabel->setEnabled(enabled && outputModeRelevant);
    }
    if (ui->outputResolutionModeComboBox) {
        ui->outputResolutionModeComboBox->setEnabled(enabled && outputModeRelevant);
    }
}
void ExportDialog::emitRangeSelectionChanged(bool clearMetadataValues)
{
    if (!ui || !ui->inPointSpinBox || !ui->outPointSpinBox) {
        return;
    }
    if (!tbcSource || !tbcSource->getIsSourceLoaded() || tbcSource->getIsMetadataOnly()) {
        return;
    }

    const int totalFrames = qMax(1, tbcSource->getNumberOfFrames());
    int inPoint = qBound(1, ui->inPointSpinBox->value(), totalFrames);
    int outPoint = qBound(1, ui->outPointSpinBox->value(), totalFrames);
    if (outPoint < inPoint) {
        outPoint = inPoint;
    }

    emit userEditRangeSelectionChanged(inPoint, outPoint, clearMetadataValues);
}

void ExportDialog::updateRangeControlsForSource(bool resetToFullRange)
{
    if (!ui || !ui->inPointSpinBox || !ui->outPointSpinBox) {
        return;
    }

    const bool canUseRange = exportAvailable && tbcSource && tbcSource->getIsSourceLoaded() && !tbcSource->getIsMetadataOnly();
    if (!canUseRange) {
        const QSignalBlocker blockIn(ui->inPointSpinBox);
        const QSignalBlocker blockOut(ui->outPointSpinBox);
        ui->inPointSpinBox->setMinimum(1);
        ui->inPointSpinBox->setMaximum(1);
        ui->inPointSpinBox->setValue(1);
        ui->outPointSpinBox->setMinimum(1);
        ui->outPointSpinBox->setMaximum(1);
        ui->outPointSpinBox->setValue(1);
        updateRangeLengthLabel();
        syncRangeTimecodeEditors();
        return;
    }

    const int totalFrames = qMax(1, tbcSource->getNumberOfFrames());
    int inPoint = ui->inPointSpinBox->value();
    int outPoint = ui->outPointSpinBox->value();
    if (resetToFullRange) {
        bool restoredFromMetadata = false;
        const LdDecodeMetaData::VideoParameters &videoParameters = tbcSource->getVideoParameters();
        if (videoParameters.userEditInSelection > 0 && videoParameters.userEditOutSelection > 0) {
            inPoint = qBound(1, videoParameters.userEditInSelection, totalFrames);
            outPoint = qBound(1, videoParameters.userEditOutSelection, totalFrames);
            if (outPoint < inPoint) {
                outPoint = inPoint;
            }
            restoredFromMetadata = true;
        }
        if (!restoredFromMetadata) {
            inPoint = 1;
            outPoint = totalFrames;
        }
    } else {
        inPoint = qBound(1, inPoint, totalFrames);
        outPoint = qBound(1, outPoint, totalFrames);
        if (outPoint < inPoint) {
            outPoint = inPoint;
        }
    }

    const QSignalBlocker blockIn(ui->inPointSpinBox);
    const QSignalBlocker blockOut(ui->outPointSpinBox);
    ui->inPointSpinBox->setMinimum(1);
    ui->inPointSpinBox->setMaximum(totalFrames);
    ui->inPointSpinBox->setValue(inPoint);
    ui->outPointSpinBox->setMinimum(1);
    ui->outPointSpinBox->setMaximum(totalFrames);
    ui->outPointSpinBox->setValue(outPoint);

    updateRangeLengthLabel();
    syncRangeTimecodeEditors();
}

void ExportDialog::updateRangeLengthLabel()
{
    if (!ui || !ui->rangeLengthValueLabel || !ui->inPointSpinBox || !ui->outPointSpinBox) {
        return;
    }
    if (!exportAvailable) {
        ui->rangeLengthValueLabel->setText(QStringLiteral("—"));
        return;
    }
    const int inPoint = ui->inPointSpinBox->value();
    const int outPoint = ui->outPointSpinBox->value();
    const qint64 frameCount = qMax<qint64>(0, static_cast<qint64>(outPoint) - static_cast<qint64>(inPoint) + 1);
    const double fps = qMax(0.001, timecodeFrameRate());
    const int frameBase = qMax(1, timecodeFrameBaseRate());
    const double totalSecondsExact = static_cast<double>(frameCount) / fps;
    const qint64 totalSeconds = static_cast<qint64>(qFloor(totalSecondsExact));
    const double fractionalSeconds = totalSecondsExact - static_cast<double>(totalSeconds);
    const int framePart = qBound(0, static_cast<int>(qFloor((fractionalSeconds * frameBase) + 1e-9)), frameBase - 1);
    const qint64 hours = totalSeconds / 3600;
    const qint64 minutes = (totalSeconds % 3600) / 60;
    const qint64 seconds = totalSeconds % 60;
    ui->rangeLengthValueLabel->setText(QStringLiteral("%1:%2:%3:%4")
                                           .arg(hours, 2, 10, QChar('0'))
                                           .arg(minutes, 2, 10, QChar('0'))
                                           .arg(seconds, 2, 10, QChar('0'))
                                           .arg(framePart, 2, 10, QChar('0')));
}

double ExportDialog::timecodeFrameRate() const
{
    int system = NTSC;
    if (tbcSource && tbcSource->getIsSourceLoaded()) {
        system = tbcSource->getVideoParameters().system;
    }
    return frameRateForSystem(system);
}

int ExportDialog::timecodeFrameBaseRate() const
{
    int system = NTSC;
    if (tbcSource && tbcSource->getIsSourceLoaded()) {
        system = tbcSource->getVideoParameters().system;
    }
    return nominalFrameRateForSystem(system);
}

QString ExportDialog::frameToTimecode(int frameNumber) const
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

int ExportDialog::timecodeToFrame(const QString &timecodeText, bool *ok) const
{
    if (ok) {
        *ok = false;
    }
    const QString trimmed = timecodeText.trimmed();
    if (trimmed.isEmpty()) {
        return 1;
    }

    bool numericOk = false;
    const int numericFrame = trimmed.toInt(&numericOk);
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
    const qint64 frameNumber = qRound64(totalSecondsExact * fps) + 1;
    if (ok) {
        *ok = true;
    }
    return static_cast<int>(qMax<qint64>(1, frameNumber));
}

void ExportDialog::syncRangeTimecodeEditors()
{
    if (!ui || !ui->inPointSpinBox || !ui->outPointSpinBox) {
        return;
    }
    if (ui->inPointTimecodeLineEdit) {
        const QSignalBlocker blocker(ui->inPointTimecodeLineEdit);
        ui->inPointTimecodeLineEdit->setText(frameToTimecode(ui->inPointSpinBox->value()));
    }
    if (ui->outPointTimecodeLineEdit) {
        const QSignalBlocker blocker(ui->outPointTimecodeLineEdit);
        ui->outPointTimecodeLineEdit->setText(frameToTimecode(ui->outPointSpinBox->value()));
    }
}

void ExportDialog::updateFromSource()
{
    const bool hasSource = tbcSource && tbcSource->getIsSourceLoaded();
    const bool metadataOnly = hasSource && tbcSource->getIsMetadataOnly();
    exportAvailable = hasSource && !metadataOnly;

    if (!hasSource) {
        ui->inputLineEdit->clear();
        updateRangeControlsForSource(true);
        refreshResolutionOptions();
        appendStatus(tr("No source loaded."));
        setBusy(exportProcess->state() != QProcess::NotRunning);
        return;
    }

    if (metadataOnly) {
        ui->inputLineEdit->clear();
        updateRangeControlsForSource(true);
        refreshResolutionOptions();
        appendStatus(tr("Metadata-only mode (no TBC source)."));
        setBusy(exportProcess->state() != QProcess::NotRunning);
        return;
    }

    const QString inputFile = tbcSource->getCurrentSourceFilename();
    bool sourceChanged = false;
    if (!inputFile.isEmpty()) {
        if (currentInputFile != inputFile) {
            sourceChanged = true;
            currentInputFile = inputFile;
            ui->inputLineEdit->setText(inputFile);
            if (outputAutoSet || ui->outputLineEdit->text().trimmed().isEmpty()) {
                ui->outputLineEdit->setText(defaultOutputBaseName(inputFile));
                outputAutoSet = true;
            }
        }
    }
    updateRangeControlsForSource(sourceChanged);
    refreshResolutionOptions();

    setBusy(exportProcess->state() != QProcess::NotRunning);
    if (exportAvailable && exportProcess->state() == QProcess::NotRunning) {
        appendStatus(tr("Ready to export."));
    }
}

void ExportDialog::refreshProfiles()
{
    const QString previousMainCodec = selectedMainCodecId();
    const QString previousAudioProfile = ui && ui->audioProfileComboBox
                                             ? ui->audioProfileComboBox->currentData().toString().trimmed()
                                             : QString();

    QStringList audioProfileIds;
    QStringList audioProfileLabels;
    const auto appendAudioProfile = [&audioProfileIds, &audioProfileLabels](const QString &profileId,
                                                                             const QString &profileLabel) {
        const QString normalizedProfileId = profileId.trimmed();
        if (normalizedProfileId.isEmpty() || listContainsCaseInsensitive(audioProfileIds, normalizedProfileId)) {
            return;
        }
        audioProfileIds << normalizedProfileId;
        audioProfileLabels << profileLabel.trimmed();
    };
    appendAudioProfile(QStringLiteral("flac_16"), tr("FLAC 16-bit"));
    appendAudioProfile(QStringLiteral("flac_24"), tr("FLAC 24-bit"));
    appendAudioProfile(QStringLiteral("pcm_16"), tr("PCM 16-bit"));
    appendAudioProfile(QStringLiteral("pcm_24"), tr("PCM 24-bit"));
    appendAudioProfile(QStringLiteral("aac_16"), tr("AAC 16-bit"));
    appendAudioProfile(QStringLiteral("aac_24"), tr("AAC 24-bit"));
    appendAudioProfile(QStringLiteral("opus_16"), tr("Opus 16-bit"));
    appendAudioProfile(QStringLiteral("opus_24"), tr("Opus 24-bit"));

    QString selectedProfileConfigError;
    const QString selectedProfileConfigPath = selectedExportProfileConfigPath(&selectedProfileConfigError);
    if (!selectedProfileConfigError.isEmpty()) {
        appendLog(selectedProfileConfigError);
    }
    if (!selectedProfileConfigPath.isEmpty()) {
        QFile configFile(selectedProfileConfigPath);
        if (!configFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
            appendLog(tr("Failed to read external profile set; using built-in audio profile options: %1")
                          .arg(QDir::toNativeSeparators(selectedProfileConfigPath)));
        } else {
            QJsonParseError parseError;
            const QJsonDocument configDoc = QJsonDocument::fromJson(configFile.readAll(), &parseError);
            if (configDoc.isNull() || !configDoc.isObject()) {
                appendLog(tr("External profile set is invalid JSON (%1); using built-in audio profile options.")
                              .arg(parseError.errorString()));
            } else {
                const QJsonArray jsonAudioProfiles = configDoc.object().value(QStringLiteral("audio_profiles")).toArray();
                QStringList loadedAudioProfileIds;
                QStringList loadedAudioProfileLabels;
                for (const QJsonValue &profileValue : jsonAudioProfiles) {
                    if (!profileValue.isObject()) {
                        continue;
                    }
                    const QJsonObject profileObject = profileValue.toObject();
                    const QString profileId = profileObject.value(QStringLiteral("name")).toString().trimmed();
                    if (profileId.isEmpty() || listContainsCaseInsensitive(loadedAudioProfileIds, profileId)) {
                        continue;
                    }
                    QString profileLabel = profileObject.value(QStringLiteral("description")).toString().trimmed();
                    if (profileLabel.isEmpty()) {
                        profileLabel = profileId;
                    }
                    loadedAudioProfileIds << profileId;
                    loadedAudioProfileLabels << profileLabel;
                }
                if (!loadedAudioProfileIds.isEmpty()) {
                    audioProfileIds = loadedAudioProfileIds;
                    audioProfileLabels = loadedAudioProfileLabels;
                } else {
                    appendLog(tr("External profile set did not define any audio profiles; using built-in audio profile options."));
                }
            }
        }
    }
    if (ui && ui->audioProfileComboBox) {
        const QSignalBlocker blocker(ui->audioProfileComboBox);
        ui->audioProfileComboBox->clear();
        for (int i = 0; i < audioProfileIds.size(); ++i) {
            const QString profileId = audioProfileIds.at(i);
            const QString profileLabel = audioProfileLabels.value(i, profileId);
            ui->audioProfileComboBox->addItem(profileLabel, profileId);
        }
        int targetIndex = ui->audioProfileComboBox->findData(previousAudioProfile);
        if (targetIndex < 0) {
            targetIndex = ui->audioProfileComboBox->findData(QStringLiteral("pcm_24"));
        }
        if (targetIndex < 0 && ui->audioProfileComboBox->count() > 0) {
            targetIndex = 0;
        }
        if (targetIndex >= 0) {
            ui->audioProfileComboBox->setCurrentIndex(targetIndex);
        }
    }

    const QString exportPath = resolveVideoExportPath();
    if (exportPath.isEmpty()) {
        appendStatus(tr("tbc-video-export not found."));
        appendLog(tr("tbc-video-export not found."));
        updateProfileDependentControls();
        return;
    }
    QStringList availableProfiles;

    QProcess listProcess;
    listProcess.setProcessChannelMode(QProcess::MergedChannels);
    QStringList listArguments;
    if (!selectedProfileConfigPath.isEmpty()) {
        listArguments << QStringLiteral("--config-file") << selectedProfileConfigPath;
    }
    listArguments << QStringLiteral("--list-profiles");
    listProcess.start(exportPath, listArguments);
    if (!listProcess.waitForStarted(3000)) {
        appendLog(tr("Failed to start profile listing; using built-in condensed profile options."));
    } else if (!listProcess.waitForFinished(10000)) {
        listProcess.kill();
        appendLog(tr("Profile list timed out; using built-in condensed profile options."));
    } else {
        const QString profileOutput = QString::fromLocal8Bit(listProcess.readAllStandardOutput());
        if (listProcess.exitStatus() == QProcess::NormalExit && listProcess.exitCode() == 0) {
            QString ignoredDefault;
            availableProfiles = parseProfiles(profileOutput, &ignoredDefault);
        } else {
            const QString errorMessage = profileOutput.trimmed().isEmpty()
                                             ? tr("Failed to list profiles.")
                                             : profileOutput.trimmed();
            appendLog(tr("%1 Using built-in condensed profile options.").arg(errorMessage));
        }
    }

    if (ui->profileComboBox) {
        const QSignalBlocker blocker(ui->profileComboBox);
        ui->profileComboBox->clear();
        ui->profileComboBox->addItem(tr("FFV1"), QStringLiteral("ffv1"));
        ui->profileComboBox->addItem(tr("ProRes"), QStringLiteral("prores"));
        ui->profileComboBox->addItem(tr("D10 MPEG-2"), QStringLiteral("d10"));
        ui->profileComboBox->addItem(tr("V210"), QStringLiteral("v210"));
        ui->profileComboBox->addItem(tr("J2K (JPEG 2000)"), QStringLiteral("j2k"));
        ui->profileComboBox->addItem(tr("AVC/H.264"), QStringLiteral("avc"));
        ui->profileComboBox->addItem(tr("HEVC/H.265"), QStringLiteral("hevc"));
        ui->profileComboBox->addItem(tr("Web"), QStringLiteral("web"));

        int targetIndex = ui->profileComboBox->findData(previousMainCodec);
        if (targetIndex < 0) {
            targetIndex = ui->profileComboBox->findData(QStringLiteral("ffv1"));
        }
        if (targetIndex < 0 && ui->profileComboBox->count() > 0) {
            targetIndex = 0;
        }
        if (targetIndex >= 0) {
            ui->profileComboBox->setCurrentIndex(targetIndex);
        }
    }

    if (!availableProfiles.isEmpty()) {
        const QString resolvedProfile = selectedExportProfileName();
        if (!isLossyProresProfileName(resolvedProfile)
            && !listContainsCaseInsensitive(availableProfiles, resolvedProfile)) {
            appendLog(tr("Selected condensed profile maps to '%1', which was not found in tbc-video-export profile list.")
                          .arg(resolvedProfile));
        }
    }
    updateProfileDependentControls();
}

void ExportDialog::updateProfileDependentControls()
{
    const QString mainCodecId = selectedMainCodecId();
    const bool ffv1Selected = mainCodecId == QStringLiteral("ffv1");
    const bool proresSelected = mainCodecId == QStringLiteral("prores");
    const bool webSelected = mainCodecId == QStringLiteral("web");
    const bool avcSelected = mainCodecId == QStringLiteral("avc");
    const bool hevcSelected = mainCodecId == QStringLiteral("hevc");
    const bool lossyProresSelected = proresSelected && isLossyProresProfileName(selectedExportProfileName());
    const QString proresVariantTooltip = lossyProresSelected ? proresLossyWarningText() : QString();
    const bool canEdit = exportAvailable
                         && exportProcess
                         && exportProcess->state() == QProcess::NotRunning;
    const bool proxyRequested = ui
                                && ui->generateProxyCheckBox
                                && ui->generateProxyCheckBox->isChecked();
    if (ui->ffv1SlicesLabel) {
        ui->ffv1SlicesLabel->setVisible(ffv1Selected);
        ui->ffv1SlicesLabel->setEnabled(canEdit && ffv1Selected);
    }
    if (ui->ffv1SlicesSpinBox) {
        ui->ffv1SlicesSpinBox->setVisible(ffv1Selected);
        ui->ffv1SlicesSpinBox->setEnabled(canEdit && ffv1Selected);
    }
    if (ui->proresVariantLabel) {
        ui->proresVariantLabel->setVisible(proresSelected);
        ui->proresVariantLabel->setEnabled(canEdit && proresSelected);
        ui->proresVariantLabel->setToolTip(proresVariantTooltip);
    }
    if (ui->proresVariantComboBox) {
        ui->proresVariantComboBox->setVisible(proresSelected);
        ui->proresVariantComboBox->setEnabled(canEdit && proresSelected);
        ui->proresVariantComboBox->setToolTip(proresVariantTooltip);
    }
    if (ui->webCodecLabel) {
        ui->webCodecLabel->setVisible(webSelected);
        ui->webCodecLabel->setEnabled(canEdit && webSelected);
    }
    if (ui->webCodecComboBox) {
        ui->webCodecComboBox->setVisible(webSelected);
        ui->webCodecComboBox->setEnabled(canEdit && webSelected);
    }
    if (ui->avcRangeLabel) {
        ui->avcRangeLabel->setVisible(avcSelected);
        ui->avcRangeLabel->setEnabled(canEdit && avcSelected);
    }
    if (ui->avcRangeComboBox) {
        ui->avcRangeComboBox->setVisible(avcSelected);
        ui->avcRangeComboBox->setEnabled(canEdit && avcSelected);
    }
    if (ui->hevcRangeLabel) {
        ui->hevcRangeLabel->setVisible(hevcSelected);
        ui->hevcRangeLabel->setEnabled(canEdit && hevcSelected);
    }
    if (ui->hevcRangeComboBox) {
        ui->hevcRangeComboBox->setVisible(hevcSelected);
        ui->hevcRangeComboBox->setEnabled(canEdit && hevcSelected);
    }
    if (ui->generateProxyCheckBox) {
        ui->generateProxyCheckBox->setVisible(true);
        ui->generateProxyCheckBox->setEnabled(canEdit);
    }
    if (ui->proxyCodecLabel) {
        ui->proxyCodecLabel->setVisible(proxyRequested);
        ui->proxyCodecLabel->setEnabled(canEdit && proxyRequested);
    }
    if (ui->proxyCodecComboBox) {
        ui->proxyCodecComboBox->setVisible(proxyRequested);
        ui->proxyCodecComboBox->setEnabled(canEdit && proxyRequested);
    }
    bool externalProfilesEnabled = ui
                                   && ui->exportProfileConfigCheckBox
                                   && ui->exportProfileConfigCheckBox->isChecked();
    if (ui->exportProfileConfigCheckBox) {
        ui->exportProfileConfigCheckBox->setEnabled(canEdit);
    }
    const bool showExternalProfileControls = externalProfilesEnabled;
    if (ui->exportProfileConfigLineEdit) {
        ui->exportProfileConfigLineEdit->setVisible(showExternalProfileControls);
        ui->exportProfileConfigLineEdit->setEnabled(canEdit && showExternalProfileControls);
    }
    if (ui->exportProfileConfigLoadButton) {
        ui->exportProfileConfigLoadButton->setVisible(showExternalProfileControls);
        ui->exportProfileConfigLoadButton->setEnabled(canEdit && showExternalProfileControls);
    }
    if (ui->exportProfileConfigEjectButton) {
        ui->exportProfileConfigEjectButton->setVisible(showExternalProfileControls);
        ui->exportProfileConfigEjectButton->setEnabled(canEdit && showExternalProfileControls);
    }
    const QString resolutionMode = effectiveResolutionMode(tbcSource, ui ? ui->resolutionModeComboBox : nullptr);
    const bool letterboxCropAllowed = resolutionMode == QStringLiteral("active_area");
    if (ui->letterboxCropCheckBox && !letterboxCropAllowed && ui->letterboxCropCheckBox->isChecked()) {
        const QSignalBlocker blocker(ui->letterboxCropCheckBox);
        ui->letterboxCropCheckBox->setChecked(false);
    }
    const bool letterboxRequested = ui
                                    && ui->letterboxCropCheckBox
                                    && ui->letterboxCropCheckBox->isChecked();
    if (ui->forceAnamorphicCheckBox
        && letterboxRequested
        && ui->forceAnamorphicCheckBox->isChecked()) {
        const QSignalBlocker blocker(ui->forceAnamorphicCheckBox);
        ui->forceAnamorphicCheckBox->setChecked(false);
    }
    const bool anamorphicRequested = ui
                                     && ui->forceAnamorphicCheckBox
                                     && ui->forceAnamorphicCheckBox->isChecked();
    if (ui->letterboxCropCheckBox) {
        ui->letterboxCropCheckBox->setEnabled(canEdit && letterboxCropAllowed && !anamorphicRequested);
    }
    if (ui->forceAnamorphicCheckBox) {
        ui->forceAnamorphicCheckBox->setEnabled(canEdit && !letterboxRequested);
    }
}


void ExportDialog::on_outputBrowseButton_clicked()
{
    QString suggestedOutput = ui->outputLineEdit->text().trimmed();
    if (suggestedOutput.isEmpty() && !currentInputFile.isEmpty()) {
        suggestedOutput = defaultOutputBaseName(currentInputFile);
    }
    QFileInfo suggestedInfo(suggestedOutput);
    QString initialDirectory;
    if (!suggestedOutput.isEmpty()) {
        if (suggestedInfo.exists() && suggestedInfo.isDir()) {
            initialDirectory = suggestedInfo.absoluteFilePath();
        } else {
            initialDirectory = suggestedInfo.absolutePath();
        }
    }
    if (initialDirectory.isEmpty() && !currentInputFile.isEmpty()) {
        initialDirectory = QFileInfo(currentInputFile).absolutePath();
    }
    if (initialDirectory.isEmpty()) {
        initialDirectory = QDir::homePath();
    }
    const QString selectedDirectory = runDirectoryDialog(this,
                                                         tr("Select output directory"),
                                                         initialDirectory);
    if (selectedDirectory.isEmpty()) {
        return;
    }
    const QString sanitizedSuggestedOutput = sanitizeOutputBaseName(suggestedOutput);
    QString baseName = QFileInfo(sanitizedSuggestedOutput).fileName();
    if (baseName.isEmpty() && !currentInputFile.isEmpty()) {
        baseName = QFileInfo(defaultOutputBaseName(currentInputFile)).fileName();
    }
    if (baseName.isEmpty()) {
        baseName = QStringLiteral("output");
    }

    ui->outputLineEdit->setText(QDir(selectedDirectory).filePath(baseName));
    outputAutoSet = false;
}

void ExportDialog::on_audio1BrowseButton_clicked()
{
    const QString selected = runOpenFileDialog(this,
                                               tr("Select audio track 1"),
                                               currentInputFile.isEmpty() ? QString() : QFileInfo(currentInputFile).absolutePath(),
                                               tr("Audio Files (*.wav *.flac *.aiff *.aif *.mp3 *.m4a *.aac *.ogg *.pcm);;All Files (*)"));
    if (!selected.isEmpty()) {
        ui->audio1LineEdit->setText(selected);
    }
}

void ExportDialog::on_audio2BrowseButton_clicked()
{
    const QString selected = runOpenFileDialog(this,
                                               tr("Select audio track 2"),
                                               currentInputFile.isEmpty() ? QString() : QFileInfo(currentInputFile).absolutePath(),
                                               tr("Audio Files (*.wav *.flac *.aiff *.aif *.mp3 *.m4a *.aac *.ogg *.pcm);;All Files (*)"));
    if (!selected.isEmpty()) {
        ui->audio2LineEdit->setText(selected);
    }
}

void ExportDialog::on_audio3BrowseButton_clicked()
{
    const QString selected = runOpenFileDialog(this,
                                               tr("Select audio track 3"),
                                               currentInputFile.isEmpty() ? QString() : QFileInfo(currentInputFile).absolutePath(),
                                               tr("Audio Files (*.wav *.flac *.aiff *.aif *.mp3 *.m4a *.aac *.ogg *.pcm);;All Files (*)"));
    if (!selected.isEmpty()) {
        ui->audio3LineEdit->setText(selected);
    }
}

void ExportDialog::on_audio4BrowseButton_clicked()
{
    const QString selected = runOpenFileDialog(this,
                                               tr("Select audio track 4"),
                                               currentInputFile.isEmpty() ? QString() : QFileInfo(currentInputFile).absolutePath(),
                                               tr("Audio Files (*.wav *.flac *.aiff *.aif *.mp3 *.m4a *.aac *.ogg *.pcm);;All Files (*)"));
    if (!selected.isEmpty()) {
        ui->audio4LineEdit->setText(selected);
    }
}

void ExportDialog::on_exportProfileConfigCheckBox_toggled(bool checked)
{
    if (!checked) {
        exportProfileConfigPath.clear();
    }
    updateExportProfileConfigPathUi();
    refreshProfiles();
    updateProfileDependentControls();
    emitExportProfileConfigPreferenceChanged();
}

void ExportDialog::on_exportProfileConfigLoadButton_clicked()
{
    if (!ui) {
        return;
    }

    QString startPath = exportProfileConfigPath.trimmed();
    if (!startPath.isEmpty()) {
        startPath = QFileInfo(startPath).absolutePath();
    } else if (!currentInputFile.isEmpty()) {
        startPath = QFileInfo(currentInputFile).absolutePath();
    } else {
        startPath = QDir::homePath();
    }

    const QString selectedPath = runOpenFileDialog(this,
                                                   tr("Select custom tbc-video-export JSON profile set"),
                                                   startPath,
                                                   tr("JSON Files (*.json);;All Files (*)"));
    if (selectedPath.isEmpty()) {
        updateProfileDependentControls();
        return;
    }

    const QFileInfo selectedInfo(selectedPath);
    if (!selectedInfo.exists() || !selectedInfo.isFile() || !selectedInfo.isReadable()) {
        const QString errorText = tr("Selected JSON profile set is not readable: %1")
                                      .arg(QDir::toNativeSeparators(selectedPath));
        appendStatus(errorText);
        appendLog(errorText);
        QMessageBox::warning(this, tr("Error"), errorText);
        updateProfileDependentControls();
        return;
    }

    exportProfileConfigPath = selectedInfo.absoluteFilePath();
    if (ui->exportProfileConfigCheckBox) {
        const QSignalBlocker blocker(ui->exportProfileConfigCheckBox);
        ui->exportProfileConfigCheckBox->setChecked(true);
    }
    updateExportProfileConfigPathUi();
    refreshProfiles();
    updateProfileDependentControls();
    emitExportProfileConfigPreferenceChanged();
}

void ExportDialog::on_exportProfileConfigEjectButton_clicked()
{
    if (!ui) {
        return;
    }

    QString startPath = exportProfileConfigPath.trimmed();
    if (startPath.isEmpty() && !currentInputFile.isEmpty()) {
        startPath = QDir(QFileInfo(currentInputFile).absolutePath())
                        .filePath(QStringLiteral("tbc-video-export.json"));
    }
    if (startPath.isEmpty()) {
        startPath = QDir(QDir::homePath()).filePath(QStringLiteral("tbc-video-export.json"));
    }

    QString selectedPath = runSaveFileDialog(this,
                                             tr("Eject default tbc-video-export profile set"),
                                             startPath,
                                             tr("JSON Files (*.json);;All Files (*)"));
    if (selectedPath.isEmpty()) {
        updateProfileDependentControls();
        return;
    }
    if (QFileInfo(selectedPath).suffix().isEmpty()) {
        selectedPath += QStringLiteral(".json");
    }
    selectedPath = QFileInfo(selectedPath).absoluteFilePath();

    const QString exportPath = resolveVideoExportPath();
    if (exportPath.isEmpty()) {
        const QString errorText = tr("tbc-video-export not found.");
        appendStatus(errorText);
        appendLog(errorText);
        QMessageBox::warning(this, tr("Error"), errorText);
        return;
    }

    QString dumpDirPath = sourceStorageTemporaryPath(
        QStringLiteral("ld-analyse-export-config-dump-%1")
            .arg(QUuid::createUuid().toString(QUuid::WithoutBraces)));
    const auto cleanupDumpDir = [this, &dumpDirPath]() {
        if (dumpDirPath.isEmpty()) {
            return;
        }
        QDir dumpDir(dumpDirPath);
        if (dumpDir.exists()) {
            dumpDir.removeRecursively();
        }
        dumpDirPath.clear();
        cleanupSourceStorageTemporaryRoot();
    };
    if (!QDir().mkpath(dumpDirPath)) {
        const QString errorText = tr("Failed to create temporary directory for default profile ejection.");
        appendStatus(errorText);
        appendLog(errorText);
        QMessageBox::warning(this, tr("Error"), errorText);
        return;
    }

    QProcess dumpProcess;
    dumpProcess.setProcessChannelMode(QProcess::MergedChannels);
    dumpProcess.setWorkingDirectory(dumpDirPath);
    dumpProcess.start(exportPath, QStringList() << QStringLiteral("--dump-default-config"));
    if (!dumpProcess.waitForStarted(3000)) {
        cleanupDumpDir();
        const QString errorText = tr("Failed to start default profile ejection.");
        appendStatus(errorText);
        appendLog(errorText);
        QMessageBox::warning(this, tr("Error"), errorText);
        return;
    }
    if (!dumpProcess.waitForFinished(15000)) {
        dumpProcess.kill();
        cleanupDumpDir();
        const QString errorText = tr("Default profile ejection timed out.");
        appendStatus(errorText);
        appendLog(errorText);
        QMessageBox::warning(this, tr("Error"), errorText);
        return;
    }
    const QString dumpOutput = QString::fromLocal8Bit(dumpProcess.readAllStandardOutput()).trimmed();
    if (dumpProcess.exitStatus() != QProcess::NormalExit || dumpProcess.exitCode() != 0) {
        cleanupDumpDir();
        const QString errorText = dumpOutput.isEmpty()
                                      ? tr("Failed to eject default profile set.")
                                      : dumpOutput;
        appendStatus(errorText);
        appendLog(errorText);
        QMessageBox::warning(this, tr("Error"), errorText);
        return;
    }

    const QString dumpedConfigPath = QDir(dumpDirPath).filePath(QStringLiteral("tbc-video-export.json"));
    QFile dumpedConfigFile(dumpedConfigPath);
    if (!dumpedConfigFile.open(QIODevice::ReadOnly)) {
        cleanupDumpDir();
        const QString errorText = tr("Failed to read ejected default profile set.");
        appendStatus(errorText);
        appendLog(errorText);
        QMessageBox::warning(this, tr("Error"), errorText);
        return;
    }
    const QByteArray configData = dumpedConfigFile.readAll();
    dumpedConfigFile.close();

    QFile outputConfigFile(selectedPath);
    if (!outputConfigFile.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        cleanupDumpDir();
        const QString errorText = tr("Failed to write ejected profile set: %1")
                                      .arg(QDir::toNativeSeparators(selectedPath));
        appendStatus(errorText);
        appendLog(errorText);
        QMessageBox::warning(this, tr("Error"), errorText);
        return;
    }
    outputConfigFile.write(configData);
    outputConfigFile.close();
    cleanupDumpDir();

    exportProfileConfigPath = QFileInfo(selectedPath).absoluteFilePath();
    if (ui->exportProfileConfigCheckBox) {
        const QSignalBlocker blocker(ui->exportProfileConfigCheckBox);
        ui->exportProfileConfigCheckBox->setChecked(true);
    }
    updateExportProfileConfigPathUi();
    refreshProfiles();
    updateProfileDependentControls();
    emitExportProfileConfigPreferenceChanged();

    const QString statusText = tr("Ejected default profile set to: %1")
                                   .arg(QDir::toNativeSeparators(exportProfileConfigPath));
    appendStatus(statusText);
    appendLog(statusText);
}
void ExportDialog::on_resetInOutButton_clicked()
{
    if (!ui || !ui->inPointSpinBox || !ui->outPointSpinBox) {
        return;
    }
    if (!tbcSource || !tbcSource->getIsSourceLoaded() || tbcSource->getIsMetadataOnly()) {
        return;
    }

    const int totalFrames = qMax(1, tbcSource->getNumberOfFrames());
    {
        const QSignalBlocker blockIn(ui->inPointSpinBox);
        const QSignalBlocker blockOut(ui->outPointSpinBox);
        ui->inPointSpinBox->setMinimum(1);
        ui->inPointSpinBox->setMaximum(totalFrames);
        ui->outPointSpinBox->setMinimum(1);
        ui->outPointSpinBox->setMaximum(totalFrames);
        ui->inPointSpinBox->setValue(1);
        ui->outPointSpinBox->setValue(totalFrames);
    }

    updateRangeLengthLabel();
    syncRangeTimecodeEditors();
    emitRangeSelectionChanged(true);
    appendStatus(tr("In/Out range reset to full source span."));
    appendLog(tr("Reset in/out range and cleared stored UserEditInSelection/UserEditOutSelection."));
}

void ExportDialog::on_exportButton_clicked()
{
    if (!exportAvailable) {
        if (!tbcSource || !tbcSource->getIsSourceLoaded()) {
            appendStatus(tr("Export unavailable: no source loaded."));
            appendLog(tr("Export unavailable: no source loaded."));
            QMessageBox::warning(this, tr("Error"), tr("No source file loaded."));
        } else if (tbcSource->getIsMetadataOnly()) {
            appendStatus(tr("Export unavailable: metadata-only mode."));
            appendLog(tr("Export unavailable: metadata-only mode."));
            QMessageBox::warning(this, tr("Error"), tr("Metadata-only mode does not include a TBC source file."));
        } else {
            appendStatus(tr("Export unavailable."));
            appendLog(tr("Export unavailable."));
            QMessageBox::warning(this, tr("Error"), tr("Export is not available."));
        }
        return;
    }
    if (exportProcess->state() != QProcess::NotRunning) {
        appendStatus(tr("Export already running."));
        appendLog(tr("Export already running."));
        return;
    }
    clearRunState();
    cancelRequested = false;
    appendStatus(tr("Starting export..."));
    appendLog(tr("Starting export..."));
    const QString selectedProfile = selectedExportProfileName();
    const bool ffv1CompressionProfileSelected = isFfv1ProfileName(selectedProfile);
    if (selectedProfile.isEmpty()) {
        appendStatus(tr("No export profile selected."));
        appendLog(tr("No export profile selected."));
        QMessageBox::warning(this, tr("Error"), tr("Please select a valid export profile."));
        return;
    }
    if (isLossyProresProfileName(selectedProfile)) {
        const QString lossyWarning = proresLossyWarningText();
        const QString promptText = tr("%1\n\nContinue with export?").arg(lossyWarning);
        const QMessageBox::StandardButton warningChoice = QMessageBox::warning(
            this,
            tr("Lossy ProRes profile selected"),
            promptText,
            QMessageBox::Yes | QMessageBox::No,
            QMessageBox::No);
        if (warningChoice != QMessageBox::Yes) {
            appendStatus(tr("Export cancelled: lossy ProRes profile warning not accepted."));
            appendLog(tr("Export cancelled by user after lossy ProRes warning."));
            return;
        }
        appendLog(lossyWarning);
    }
    QString exportProfileConfigErrorMessage;
    const QString baseConfigOverridePath = selectedExportProfileConfigPath(&exportProfileConfigErrorMessage);
    if (!exportProfileConfigErrorMessage.isEmpty()) {
        appendStatus(exportProfileConfigErrorMessage);
        appendLog(exportProfileConfigErrorMessage);
        QMessageBox::warning(this, tr("Error"), exportProfileConfigErrorMessage);
        return;
    }
    if (!baseConfigOverridePath.isEmpty()) {
        appendLog(tr("Using custom JSON profile set: %1")
                      .arg(QDir::toNativeSeparators(baseConfigOverridePath)));
    }
    QString snapshotErrorMessage;
    const QString metadataSnapshotPath = createTemporaryMetadataSnapshot(&snapshotErrorMessage);
    if (metadataSnapshotPath.isEmpty()) {
        const QString errorToShow = snapshotErrorMessage.isEmpty()
                                        ? tr("Could not prepare metadata for export.")
                                        : snapshotErrorMessage;
        appendStatus(errorToShow);
        appendLog(errorToShow);
        QMessageBox::warning(this, tr("Error"), errorToShow);
        return;
    }
    bool overwriteExisting = false;
    const QString outputBase = sanitizeOutputBaseName(ui->outputLineEdit->text().trimmed());
    const bool generateProxyRequested = shouldGenerateProxyForSelection();
    const QString selectedProxyCodec = selectedProxyCodecId();
    const QString plannedProxyOutputPath = generateProxyRequested
                                               ? proxyOutputPath(outputBase, selectedProxyCodec)
                                               : QString();
    QStringList existingOutputs;
    findExistingOutputFiles(outputBase, &existingOutputs);
    if (generateProxyRequested
        && !plannedProxyOutputPath.isEmpty()
        && QFileInfo::exists(plannedProxyOutputPath)) {
        existingOutputs << plannedProxyOutputPath;
    }
    existingOutputs.removeDuplicates();
    if (!existingOutputs.isEmpty()) {
        const int previewCount = qMin(existingOutputs.size(), 6);
        QStringList previewLines;
        for (int i = 0; i < previewCount; ++i) {
            previewLines << existingOutputs.at(i);
        }
        if (existingOutputs.size() > previewCount) {
            previewLines << tr("... and %1 more").arg(existingOutputs.size() - previewCount);
        }

        const QString promptText = tr("Output file(s) already exist for this base name:\n%1\n\nOverwrite existing output file(s)?")
                                       .arg(previewLines.join(QLatin1Char('\n')));
        const QMessageBox::StandardButton overwriteChoice = QMessageBox::question(
            this, tr("Output already exists"), promptText, QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (overwriteChoice != QMessageBox::Yes) {
            cleanupTemporaryMetadataSnapshot();
            appendStatus(tr("Export cancelled: output already exists."));
            appendLog(tr("Export cancelled by user: overwrite was not confirmed."));
            return;
        }
        overwriteExisting = true;
        appendLog(tr("Overwrite confirmed for existing output file(s)."));
    }
    QString configErrorMessage;
    const QString selectedAudioProfile =
        ui->audioProfileComboBox ? ui->audioProfileComboBox->currentData().toString() : QString();
    const int videoSystem = tbcSource ? tbcSource->getVideoParameters().system : NTSC;
    const QString resolutionMode = effectiveResolutionMode(tbcSource, ui ? ui->resolutionModeComboBox : nullptr);
    {
        const QString exportPath = resolveVideoExportPath();
        const QString dropoutMode = ui->dropoutModeComboBox
                                        ? ui->dropoutModeComboBox->currentData().toString()
                                        : QStringLiteral("basic");
        const QString correctionMode = ui->dropoutFieldModeComboBox
                                           ? ui->dropoutFieldModeComboBox->currentData().toString()
                                           : QStringLiteral("innerfield");
        const bool supportsDropoutInterfieldCorrection = executableSupportsOption(
            exportPath, QStringLiteral("--dropout-interfield-correction"));
        const QString outputResolutionMode = effectiveOutputResolutionMode(ui ? ui->outputResolutionModeComboBox : nullptr);
        const bool supportsAppendVideoFilter = executableSupportsOption(exportPath, QStringLiteral("--append-video-filter"));
        const bool supportsD1OutputSizing = executableSupportsD1OutputSizing(exportPath);
        const bool deinterlacedOutputProfile = isWebProfileName(selectedProfile);
        if (dropoutMode == QStringLiteral("heavy")
            && !executableSupportsOption(exportPath, QStringLiteral("--overcorrect"))) {
            appendLog(tr("Dropout mode 'Heavy' selected, but --overcorrect is not supported by the detected tbc-video-export. Falling back to basic dropout correction."));
        }
        if (dropoutMode != QStringLiteral("disabled")
            && correctionMode == QStringLiteral("intra")
            && !supportsDropoutInterfieldCorrection
            && !executableSupportsOption(exportPath, QStringLiteral("--intra"))) {
            appendLog(tr("Dropout field mode 'Intra' selected, but --intra is not supported by the detected tbc-video-export. Falling back to tool default correction mode."));
        }
        if (dropoutMode != QStringLiteral("disabled")
            && correctionMode == QStringLiteral("innerfield")
            && !supportsDropoutInterfieldCorrection
            && !executableSupportsOption(exportPath, QStringLiteral("--innerfield"))
            && !executableSupportsOption(exportPath, QStringLiteral("--interfield"))) {
            appendLog(tr("Dropout field mode 'Interfield' selected, but no explicit inner/inter-field option is supported by the detected tbc-video-export. Using tool default correction mode."));
        }
        const OutputResamplePlan outputResamplePlan = outputResamplePlanForModes(videoSystem,
                                                                                  resolutionMode,
                                                                                  outputResolutionMode);
        const bool useD1OutputSizing = outputResamplePlan.enabled
                                       && shouldUseD1OutputSizing(resolutionMode, outputResolutionMode)
                                       && supportsD1OutputSizing
                                       && !deinterlacedOutputProfile;
        if (outputResamplePlan.enabled) {
            if (useD1OutputSizing) {
                appendLog(tr("Output resolution mode '%1' will export %2x%3 with SAR %4 using --d1.")
                              .arg(ui->outputResolutionModeComboBox
                                       ? ui->outputResolutionModeComboBox->currentText()
                                       : tr("Default (D1)"))
                              .arg(outputResamplePlan.width)
                              .arg(outputResamplePlan.height)
                              .arg(outputResamplePlan.sampleAspectRatio));
            } else if (supportsAppendVideoFilter) {
                appendLog(tr("Output resolution mode '%1' will export %2x%3 with SAR %4.")
                              .arg(ui->outputResolutionModeComboBox
                                       ? ui->outputResolutionModeComboBox->currentText()
                                       : tr("Default (D1)"))
                              .arg(outputResamplePlan.width)
                              .arg(outputResamplePlan.height)
                              .arg(outputResamplePlan.sampleAspectRatio));
                if (resolutionMode == QStringLiteral("active_vbi")
                    && outputResolutionMode != QStringLiteral("default_safe")
                    && outputResolutionMode != QStringLiteral("tool_native")) {
                    appendLog(tr("Active + VBI framing uses fixed safe output sizing (720x%1).")
                                  .arg(outputResamplePlan.height));
                }
            } else {
                appendLog(tr("Output resolution mode is selected, but neither --d1/--standard nor --append-video-filter is supported by the detected tbc-video-export. Using native output sizing."));
            }
        } else if (outputResolutionMode != QStringLiteral("tool_native")) {
            appendLog(tr("Output resolution mode applies to Active Area and Active + VBI framing only. Using native output sizing for current framing."));
        }
    }
    if (isFfv1ProfileName(selectedProfile) && ui->ffv1SlicesSpinBox) {
        int ffv1Slices = ui->ffv1SlicesSpinBox->value();
        if (ffv1CompressionProfileSelected) {
            const int compressionSlices = recommendedCompressionFfv1SlicesForResolutionMode(videoSystem, resolutionMode);
            if (ffv1Slices != compressionSlices) {
                const int originalSlices = ffv1Slices;
                {
                    const QSignalBlocker blocker(ui->ffv1SlicesSpinBox);
                    ui->ffv1SlicesSpinBox->setValue(compressionSlices);
                }
                ffv1Slices = compressionSlices;
                const QString compressionMessage = isFullFrame4fscResolutionMode(resolutionMode)
                                                       ? tr("FFV1 profile adjusted slices from %1 to %2 for 4fsc framing (%3 compatibility).")
                                                             .arg(originalSlices)
                                                             .arg(ffv1Slices)
                                                             .arg(videoSystemArg(videoSystem).toUpper())
                                                       : tr("FFV1 profile adjusted slices from %1 to %2 for improved compression.")
                                                             .arg(originalSlices)
                                                             .arg(ffv1Slices);
                appendStatus(compressionMessage);
                appendLog(compressionMessage);
            }
        }
        if (!isSupportedFfv1SlicesValue(ffv1Slices)) {
            const QString supportedValuesText = supportedFfv1SlicesValuesText();
            const QString errorToShow = tr("Unsupported FFV1 slices value %1. Supported values: %2.")
                                            .arg(ffv1Slices)
                                            .arg(supportedValuesText);
            cleanupTemporaryMetadataSnapshot();
            appendStatus(errorToShow);
            appendLog(errorToShow);
            QMessageBox::warning(this, tr("Error"), errorToShow);
            return;
        }
        if (!isFfv1SlicesCompatibleWithResolutionMode(ffv1Slices, videoSystem, resolutionMode)) {
            const int adjustedSlices = recommendedFfv1SlicesForResolutionMode(videoSystem, resolutionMode);
            if (!isSupportedFfv1SlicesValue(adjustedSlices)
                || !isFfv1SlicesCompatibleWithResolutionMode(adjustedSlices, videoSystem, resolutionMode)) {
                const QString errorToShow = tr("FFV1 slices value %1 is not compatible with 4fsc framing for %2. Supported values: %3.")
                                                .arg(ffv1Slices)
                                                .arg(videoSystemArg(videoSystem).toUpper())
                                                .arg(supportedFullFrameFfv1SlicesValuesTextForSystem(videoSystem));
                cleanupTemporaryMetadataSnapshot();
                appendStatus(errorToShow);
                appendLog(errorToShow);
                QMessageBox::warning(this, tr("Error"), errorToShow);
                return;
            }

            const int originalSlices = ffv1Slices;
            {
                const QSignalBlocker blocker(ui->ffv1SlicesSpinBox);
                ui->ffv1SlicesSpinBox->setValue(adjustedSlices);
            }
            ffv1Slices = adjustedSlices;

            const QString adjustMessage = tr("Adjusted FFV1 slices from %1 to %2 for 4fsc framing (%3 compatibility).")
                                              .arg(originalSlices)
                                              .arg(ffv1Slices)
                                              .arg(videoSystemArg(videoSystem).toUpper());
            appendStatus(adjustMessage);
            appendLog(adjustMessage);
        }
    }
    const QString configOverridePath = createTemporaryExportConfig(&configErrorMessage,
                                                                   selectedProfile,
                                                                   selectedAudioProfile,
                                                                   baseConfigOverridePath);
    if (configOverridePath.isEmpty()) {
        cleanupTemporaryMetadataSnapshot();
        const QString errorToShow = configErrorMessage.isEmpty()
                                        ? tr("Could not prepare export profile configuration.")
                                        : configErrorMessage;
        appendStatus(errorToShow);
        appendLog(errorToShow);
        QMessageBox::warning(this, tr("Error"), errorToShow);
        return;
    }
    if (isFfv1ProfileName(selectedProfile) && ui->ffv1SlicesSpinBox) {
        appendLog(tr("FFV1 slices override set to %1.").arg(ui->ffv1SlicesSpinBox->value()));
    }
    const int totalFrames = qMax(1, tbcSource->getNumberOfFrames());
    int inPoint = ui->inPointSpinBox ? ui->inPointSpinBox->value() : 1;
    int outPoint = ui->outPointSpinBox ? ui->outPointSpinBox->value() : totalFrames;
    inPoint = qBound(1, inPoint, totalFrames);
    outPoint = qBound(1, outPoint, totalFrames);
    if (outPoint < inPoint) {
        cleanupTemporaryMetadataSnapshot();
        const QString errorToShow = tr("Out point must be greater than or equal to In point.");
        appendStatus(errorToShow);
        appendLog(errorToShow);
        QMessageBox::warning(this, tr("Error"), errorToShow);
        return;
    }
    const int startFrameOneBased = inPoint;
    const int rangeLength = outPoint - inPoint + 1;
    const QString vitcFfmpegTimecode = firstValidVitcTimecodeForRange(metadataSnapshotPath,
                                                                       startFrameOneBased,
                                                                       rangeLength);
    if (!vitcFfmpegTimecode.isEmpty()) {
        appendLog(tr("VITC start timecode (FFmpeg style): %1").arg(vitcFfmpegTimecode));
    } else {
        appendLog(tr("VITC start timecode (FFmpeg style): unavailable for selected range."));
    }

    QStringList exportAudioTracks = collectAudioTracks();
    audioConfiguredForCurrentRun = !exportAudioTracks.isEmpty();

    QString errorMessage;
    const QStringList arguments = buildArguments(&errorMessage,
                                                 metadataSnapshotPath,
                                                 overwriteExisting,
                                                 configOverridePath,
                                                 exportAudioTracks,
                                                 startFrameOneBased,
                                                 rangeLength);
    if (arguments.isEmpty()) {
        cleanupTemporaryMetadataSnapshot();
        if (!errorMessage.isEmpty()) {
            appendStatus(errorMessage);
            appendLog(errorMessage);
            QMessageBox::warning(this, tr("Error"), errorMessage);
        }
        return;
    }

    QStringList parallelProxyArguments;
    if (generateProxyRequested) {
        QString proxyArgsError;
        const QString proxyProfile = proxyExportProfileName(selectedProxyCodec);
        const QString proxyOutputBase = sanitizeOutputBaseName(plannedProxyOutputPath);
        parallelProxyArguments = buildArguments(&proxyArgsError,
                                                metadataSnapshotPath,
                                                overwriteExisting,
                                                configOverridePath,
                                                exportAudioTracks,
                                                startFrameOneBased,
                                                rangeLength,
                                                proxyProfile,
                                                proxyOutputBase);
        if (parallelProxyArguments.isEmpty()) {
            cleanupTemporaryMetadataSnapshot();
            const QString errorToShow = proxyArgsError.isEmpty()
                                            ? tr("Could not prepare parallel proxy export arguments.")
                                            : proxyArgsError;
            appendStatus(errorToShow);
            appendLog(errorToShow);
            QMessageBox::warning(this, tr("Error"), errorToShow);
            return;
        }
    }

    const QString exportPath = resolveVideoExportPath();
    if (exportPath.isEmpty()) {
        cleanupTemporaryMetadataSnapshot();
        appendStatus(tr("tbc-video-export not found."));
        appendLog(tr("tbc-video-export not found."));
        QMessageBox::warning(this, tr("Error"), tr("tbc-video-export not found in PATH or alongside ld-analyse."));
        return;
    }
    QString programToRun;
    QStringList argsToRun;
    prepareVideoExportLaunch(exportPath, arguments, tr("main export"), &programToRun, &argsToRun);
    appendLog(tr("Command: %1").arg(formatCommand(programToRun, argsToRun)));

    QString proxyProgramToRun;
    QStringList proxyArgsToRun;
    if (generateProxyRequested && !parallelProxyArguments.isEmpty()) {
        prepareVideoExportLaunch(exportPath, parallelProxyArguments, tr("parallel proxy export"),
                                 &proxyProgramToRun, &proxyArgsToRun);
        appendLog(tr("Parallel proxy command: %1")
                      .arg(formatCommand(proxyProgramToRun, proxyArgsToRun)));
    }
    activeRunStage = RunStage::MainExport;
    generateProxyForCurrentRun = generateProxyRequested;
    overwriteExistingForCurrentRun = overwriteExisting;
    outputBaseForCurrentRun = outputBase;
    proxyCodecForCurrentRun = selectedProxyCodec;
    proxyOutputPathForCurrentRun = plannedProxyOutputPath;
    splitStatsByFeed = generateProxyRequested;
    updateProcessStatsPaneMode();
    updateFeedLogPaneMode();
    clearFeedLogLines();
    if (splitStatsByFeed && ui) {
        if (ui->logTextEdit) {
            ui->logTextEdit->clear();
        }
        if (ui->proxyLogTextEdit) {
            ui->proxyLogTextEdit->clear();
        }
    }
    resetProcessStats();
    initializeProcessStats();

    processStdout.clear();
    processStderr.clear();
    pendingStdoutBuffer.clear();
    pendingStderrBuffer.clear();
    parallelProxyStdout.clear();
    parallelProxyStderr.clear();
    pendingParallelProxyStdoutBuffer.clear();
    pendingParallelProxyStderrBuffer.clear();
    parallelProxyRunning = false;
    parallelProxyFinished = false;
    parallelProxySucceeded = false;
    mainExportFinished = false;
    mainExportSucceeded = false;
    setBusy(true);
    exportProcess->setWorkingDirectory(QFileInfo(currentInputFile).absolutePath());
    QProcessEnvironment launchEnvironment = QProcessEnvironment::systemEnvironment();
    {
        const QStringList prependDirs = defaultExecutableSearchDirs(currentInputFile);
        QStringList pathEntries = launchEnvironment.value(QStringLiteral("PATH"))
                                      .split(QDir::listSeparator(), Qt::SkipEmptyParts);
        prependUniquePathEntries(&pathEntries, prependDirs);
        if (!pathEntries.isEmpty()) {
            launchEnvironment.insert(QStringLiteral("PATH"), pathEntries.join(QDir::listSeparator()));
        }
        exportProcess->setProcessEnvironment(launchEnvironment);
    }
    exportProcess->start(programToRun, argsToRun);
    if (!exportProcess->waitForStarted(5000)) {
        cleanupTemporaryMetadataSnapshot();
        appendStatus(tr("Failed to start tbc-video-export."));
        appendLog(tr("Failed to start tbc-video-export."));
        const QString startErrorText = exportProcess ? exportProcess->errorString().trimmed() : QString();
        showExportFailureNotification(tr("Export failed"),
                                      tr("Failed to start tbc-video-export."),
                                      startErrorText,
                                      tr("tbc-video-export could not be started."));
        clearRunState();
        setBusy(false);
        return;
    }

    appendStatus(tr("Export running..."));
    appendLog(tr("Export running..."));

    if (generateProxyRequested && !parallelProxyArguments.isEmpty()) {
        parallelProxyProcess->setWorkingDirectory(QFileInfo(currentInputFile).absolutePath());
        parallelProxyProcess->setProcessEnvironment(launchEnvironment);
        parallelProxyProcess->start(proxyProgramToRun, proxyArgsToRun);
        if (parallelProxyProcess->waitForStarted(5000)) {
            parallelProxyRunning = true;
            generateProxyForCurrentRun = false;
            appendLog(tr("Parallel proxy export running using profile '%1'.")
                          .arg(proxyExportProfileName(selectedProxyCodec)));
        } else {
            appendLog(tr("Parallel proxy export failed to start; falling back to post-export proxy re-run via tbc-video-export."));
        }
    }
}
void ExportDialog::on_cancelButton_clicked()
{
    const bool mainRunning = exportProcess && exportProcess->state() != QProcess::NotRunning;
    const bool proxyRunning = parallelProxyProcess && parallelProxyProcess->state() != QProcess::NotRunning;
    if (!mainRunning && !proxyRunning) {
        appendStatus(tr("No export running."));
        appendLog(tr("No export running."));
        return;
    }
    if (cancelRequested) {
        appendStatus(tr("Cancel already requested..."));
        appendLog(tr("Cancel already requested; waiting for process to exit."));
        return;
    }
    cancelRequested = true;
    const bool proxyStage = activeRunStage == RunStage::ProxyExport;
    const QString processLabel = proxyStage ? tr("proxy generation") : tr("tbc-video-export");

    appendStatus(tr("Cancel requested..."));
    appendLog(tr("Cancel requested."));

#if defined(Q_OS_UNIX)
    const auto sendSigInt = [this](QProcess *process, const QString &label) {
        if (!process || process->state() == QProcess::NotRunning) {
            return;
        }
        const qint64 pid = process->processId();
        if (pid > 0) {
            if (::kill(static_cast<pid_t>(pid), SIGINT) == 0) {
                appendLog(tr("Sent SIGINT to %1.").arg(label));
            } else {
                appendLog(tr("Failed to send SIGINT to %1.").arg(label));
            }
        } else {
            appendLog(tr("Process ID unavailable; unable to send SIGINT for %1.").arg(label));
        }
    };
    sendSigInt(exportProcess, processLabel);
    sendSigInt(parallelProxyProcess, tr("parallel proxy export"));
    QTimer::singleShot(3000, this, [this]() {
        if (exportProcess && exportProcess->state() != QProcess::NotRunning) {
            appendLog(tr("Process still running after SIGINT; sending kill()."));
            exportProcess->kill();
        }
        if (parallelProxyProcess && parallelProxyProcess->state() != QProcess::NotRunning) {
            appendLog(tr("Parallel proxy process still running after SIGINT; sending kill()."));
            parallelProxyProcess->kill();
        }
    });
#else
    const auto taskkillProcessTree = [this](QProcess *process, const QString &label) {
        if (!process || process->state() == QProcess::NotRunning) {
            return;
        }
        const qint64 pid = process->processId();
        if (pid <= 0) {
            appendLog(tr("Process ID unavailable; cannot run taskkill by PID for %1.").arg(label));
            process->terminate();
            return;
        }
        const QString taskkillPath = QStandardPaths::findExecutable(QStringLiteral("taskkill"));
        const QString taskkillProgram = taskkillPath.isEmpty() ? QStringLiteral("taskkill") : taskkillPath;
        const QStringList taskkillArgs = {
            QStringLiteral("/PID"),
            QString::number(pid),
            QStringLiteral("/T"),
            QStringLiteral("/F")
        };
        const bool taskkillStarted = QProcess::startDetached(taskkillProgram, taskkillArgs);
        if (taskkillStarted) {
            appendLog(tr("Invoked taskkill /PID %1 /T /F for %2 process tree.").arg(pid).arg(label));
        } else {
            appendLog(tr("Failed to launch taskkill for %1 process tree.").arg(label));
        }
        process->terminate();
        appendLog(taskkillStarted
                      ? tr("Sent terminate request to %1 (taskkill already requested).").arg(label)
                      : tr("Sent terminate request to %1 (fallback path).").arg(label));
    };
    taskkillProcessTree(exportProcess, processLabel);
    taskkillProcessTree(parallelProxyProcess, tr("parallel proxy export"));
    QTimer::singleShot(1500, this, [this]() {
        if (exportProcess && exportProcess->state() != QProcess::NotRunning) {
            appendLog(tr("Process still running after terminate; sending kill()."));
            exportProcess->kill();
        }
        if (parallelProxyProcess && parallelProxyProcess->state() != QProcess::NotRunning) {
            appendLog(tr("Parallel proxy process still running after terminate; sending kill()."));
            parallelProxyProcess->kill();
        }
    });
#endif
}


void ExportDialog::handleProcessFinished(int exitCode, QProcess::ExitStatus exitStatus)
{
    const RunStage finishedStage = activeRunStage;
    const QString feedTag = finishedStage == RunStage::ProxyExport ? kStatsFeedProxy : kStatsFeedMain;
    flushPendingProcessOutput(feedTag);
    const bool wasCancelRequested = cancelRequested;
    cancelRequested = false;
    const QString combinedOutput = processStdout + QStringLiteral("\n") + processStderr;
    const bool reportedFailure =
        combinedOutput.contains(QRegularExpression(QStringLiteral("\\bExport failed\\b"),
                                                   QRegularExpression::CaseInsensitiveOption))
        || combinedOutput.contains(QStringLiteral("InvalidOptsError"))
        || combinedOutput.contains(QStringLiteral("FileIOError"))
        || combinedOutput.contains(QStringLiteral("ProcessError"));

    const bool success = exitStatus == QProcess::NormalExit && exitCode == 0 && !reportedFailure;
    if (finishedStage != RunStage::ProxyExport) {
        mainExportFinished = true;
        mainExportSucceeded = success;
    }

    if (finishedStage == RunStage::ProxyExport) {
        setBusy(false);
        if (success) {
            appendStatus(tr("Export complete."));
            appendLog(tr("Proxy generation complete."));
        } else if (wasCancelRequested) {
            appendStatus(tr("Export cancelled."));
            appendLog(tr("Proxy generation cancelled."));
        } else {
            appendStatus(tr("Proxy generation failed."));
            appendLog(tr("Proxy generation failed."));
            showExportFailureNotification(tr("Proxy generation failed"),
                                          tr("Proxy generation failed."),
                                          combinedOutput,
                                          tr("tbc-video-export reported failure while generating proxy output."));
        }
        clearRunState();
        return;
    }

    if (success) {
        if (parallelProxyRunning) {
            appendStatus(tr("Main export complete. Waiting for proxy export..."));
            appendLog(tr("Main export completed; waiting for parallel proxy export to finish."));
            return;
        }
        if (parallelProxyFinished && !parallelProxySucceeded) {
            appendLog(tr("Parallel proxy export failed; attempting post-export proxy re-run via tbc-video-export."));
            generateProxyForCurrentRun = true;
            QString proxyFallbackError;
            if (startProxyExport(&proxyFallbackError, true)) {
                return;
            }
            const QString proxyCombinedOutput = parallelProxyStdout
                                                + QStringLiteral("\n")
                                                + parallelProxyStderr
                                                + QStringLiteral("\n")
                                                + proxyFallbackError;
            setBusy(false);
            appendStatus(tr("Export complete (proxy failed)."));
            appendLog(proxyFallbackError.isEmpty()
                          ? tr("Parallel proxy export failed and fallback proxy generation could not be started.")
                          : proxyFallbackError);
            showExportFailureNotification(tr("Proxy generation failed"),
                                          tr("Main export completed, but proxy generation failed."),
                                          proxyCombinedOutput,
                                          tr("Parallel proxy export failed, and fallback proxy generation could not be started."));
            clearRunState();
            return;
        }
        if (generateProxyForCurrentRun) {
            QString proxyErrorMessage;
            if (startProxyExport(&proxyErrorMessage)) {
                return;
            }

            setBusy(false);
            appendStatus(tr("Export complete (proxy failed)."));
            appendLog(proxyErrorMessage.isEmpty()
                          ? tr("Main export completed, but proxy generation could not be started.")
                          : proxyErrorMessage);
            showExportFailureNotification(tr("Proxy generation failed"),
                                          tr("Main export completed, but proxy generation could not be started."),
                                          proxyErrorMessage,
                                          tr("Main export completed, but proxy generation could not be started."));
            clearRunState();
            return;
        }

        setBusy(false);
        appendStatus(tr("Export complete."));
        appendLog(tr("Export complete."));
        clearRunState();
        return;
    }

    if (parallelProxyProcess && parallelProxyProcess->state() != QProcess::NotRunning) {
        parallelProxyProcess->kill();
    }
    parallelProxyRunning = false;
    setBusy(false);

    if (wasCancelRequested) {
        appendStatus(tr("Export cancelled."));
        appendLog(tr("Export cancelled."));
        clearRunState();
        return;
    }
    appendStatus(tr("Export failed."));
    appendLog(tr("Export failed."));
    showExportFailureNotification(tr("Export failed"),
                                  tr("Export failed."),
                                  combinedOutput,
                                  tr("tbc-video-export reported failure."));
    clearRunState();
}

void ExportDialog::handleProcessError(QProcess::ProcessError)
{
    setBusy(false);
    const RunStage failedStage = activeRunStage;
    const QString feedTag = failedStage == RunStage::ProxyExport ? kStatsFeedProxy : kStatsFeedMain;
    flushPendingProcessOutput(feedTag);
    const QString errorText = exportProcess ? exportProcess->errorString() : QString();
    if (failedStage != RunStage::ProxyExport
        && parallelProxyProcess
        && parallelProxyProcess->state() != QProcess::NotRunning) {
        parallelProxyProcess->kill();
    }
    if (failedStage != RunStage::ProxyExport) {
        parallelProxyRunning = false;
    }
    if (cancelRequested) {
        appendLog(errorText.isEmpty()
                      ? tr("Process reported an error during cancellation.")
                      : tr("Process reported an error during cancellation: %1").arg(errorText));
        clearRunState();
        return;
    }
    if (failedStage == RunStage::ProxyExport) {
        const QString proxySummary = tr("Proxy generation failed to start.");
        appendStatus(proxySummary);
        appendLog(errorText.isEmpty() ? proxySummary : tr("%1 %2").arg(proxySummary, errorText));
        const QString detailedOutput = processStdout
                                       + QStringLiteral("\n")
                                       + processStderr
                                       + QStringLiteral("\n")
                                       + errorText;
        showExportFailureNotification(tr("Proxy generation failed"),
                                      proxySummary,
                                      detailedOutput,
                                      tr("tbc-video-export could not be started for proxy generation."));
    } else {
        const QString exportSummary = tr("Export failed to start.");
        appendStatus(exportSummary);
        appendLog(errorText.isEmpty() ? exportSummary : tr("%1 %2").arg(exportSummary, errorText));
        const QString detailedOutput = processStdout
                                       + QStringLiteral("\n")
                                       + processStderr
                                       + QStringLiteral("\n")
                                       + errorText;
        showExportFailureNotification(tr("Export failed"),
                                      exportSummary,
                                      detailedOutput,
                                      tr("tbc-video-export could not be started."));
    }
    clearRunState();
}
void ExportDialog::processOutputLine(const QString &line, QString *lastStatus, const QString &feedTag)
{
    const QString trimmed = line.trimmed();
    if (trimmed.isEmpty() || isIgnorableLogLine(trimmed)) {
        return;
    }

    ExportProcessStat stat;
    if (parseProgressLine(trimmed, &stat)) {
        stat.feedTag = feedTag;
        updateProcessStat(stat);
        return;
    }

    if (lastStatus) {
        *lastStatus = trimmed;
    }
    if (splitStatsByFeed) {
        if (isTransientProcessTelemetryLine(trimmed)) {
            return;
        }
        appendFeedLog(trimmed, feedTag);
        return;
    }
    appendLog(trimmed);
}

void ExportDialog::consumeProcessOutputChunk(const QString &chunk, QString *pendingBuffer, const QString &feedTag)
{
    if (!pendingBuffer || chunk.isEmpty()) {
        return;
    }

    QString buffer = *pendingBuffer + stripAnsiSequences(chunk);
    ExportProcessStat ffmpegStat;
    if (parseFfmpegProgressLine(buffer, &ffmpegStat)) {
        ffmpegStat.feedTag = feedTag;
        updateProcessStat(ffmpegStat);
    }
    buffer.replace(QLatin1Char('\r'), QLatin1Char('\n'));

    const int lastNewline = buffer.lastIndexOf(QLatin1Char('\n'));
    if (lastNewline < 0) {
        if (buffer.size() > 32768) {
            buffer = buffer.right(32768);
        }
        *pendingBuffer = buffer;
        return;
    }

    const QString completeLines = buffer.left(lastNewline);
    *pendingBuffer = buffer.mid(lastNewline + 1);

    QString lastStatus;
    const QStringList lines = completeLines.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    for (const QString &line : lines) {
        processOutputLine(line, &lastStatus, feedTag);
    }
    if (!lastStatus.isEmpty()) {
        appendFeedStatus(lastStatus, feedTag);
    }
}

void ExportDialog::flushPendingProcessOutput(const QString &feedTag)
{
    consumeProcessOutputChunk(QStringLiteral("\n"), &pendingStdoutBuffer, feedTag);
    consumeProcessOutputChunk(QStringLiteral("\n"), &pendingStderrBuffer, feedTag);
    pendingStdoutBuffer.clear();
    pendingStderrBuffer.clear();
}

void ExportDialog::handleProcessStdout()
{
    const QString chunk = QString::fromLocal8Bit(exportProcess->readAllStandardOutput());
    processStdout += chunk;
    const QString feedTag = activeRunStage == RunStage::ProxyExport ? kStatsFeedProxy : kStatsFeedMain;
    consumeProcessOutputChunk(chunk, &pendingStdoutBuffer, feedTag);
}

void ExportDialog::handleProcessStderr()
{
    const QString chunk = QString::fromLocal8Bit(exportProcess->readAllStandardError());
    processStderr += chunk;
    const QString feedTag = activeRunStage == RunStage::ProxyExport ? kStatsFeedProxy : kStatsFeedMain;
    consumeProcessOutputChunk(chunk, &pendingStderrBuffer, feedTag);
}

void ExportDialog::handleParallelProxyProcessStdout()
{
    const QString chunk = QString::fromLocal8Bit(parallelProxyProcess->readAllStandardOutput());
    parallelProxyStdout += chunk;
    consumeProcessOutputChunk(chunk, &pendingParallelProxyStdoutBuffer, kStatsFeedProxy);
}

void ExportDialog::handleParallelProxyProcessStderr()
{
    const QString chunk = QString::fromLocal8Bit(parallelProxyProcess->readAllStandardError());
    parallelProxyStderr += chunk;
    consumeProcessOutputChunk(chunk, &pendingParallelProxyStderrBuffer, kStatsFeedProxy);
}

void ExportDialog::handleParallelProxyProcessFinished(int exitCode, QProcess::ExitStatus exitStatus)
{
    consumeProcessOutputChunk(QStringLiteral("\n"), &pendingParallelProxyStdoutBuffer, kStatsFeedProxy);
    consumeProcessOutputChunk(QStringLiteral("\n"), &pendingParallelProxyStderrBuffer, kStatsFeedProxy);
    pendingParallelProxyStdoutBuffer.clear();
    pendingParallelProxyStderrBuffer.clear();

    const QString combinedOutput = parallelProxyStdout + QStringLiteral("\n") + parallelProxyStderr;
    const bool reportedFailure =
        combinedOutput.contains(QRegularExpression(QStringLiteral("\\bExport failed\\b"),
                                                   QRegularExpression::CaseInsensitiveOption))
        || combinedOutput.contains(QStringLiteral("InvalidOptsError"))
        || combinedOutput.contains(QStringLiteral("FileIOError"))
        || combinedOutput.contains(QStringLiteral("ProcessError"));

    parallelProxyRunning = false;
    parallelProxyFinished = true;
    parallelProxySucceeded = (exitStatus == QProcess::NormalExit && exitCode == 0 && !reportedFailure);

    if (parallelProxySucceeded) {
        appendLog(tr("Parallel proxy export complete."));
    } else if (cancelRequested) {
        appendLog(tr("Parallel proxy export cancelled."));
    } else {
        appendLog(tr("Parallel proxy export failed."));
    }

    if (!mainExportFinished || !mainExportSucceeded) {
        return;
    }
    if (parallelProxySucceeded) {
        setBusy(false);
        appendStatus(tr("Export complete."));
        appendLog(tr("Export complete."));
        clearRunState();
        return;
    }

    appendLog(tr("Parallel proxy export failed after main export completion; attempting post-export proxy re-run via tbc-video-export."));
    generateProxyForCurrentRun = true;
    QString proxyFallbackError;
    if (startProxyExport(&proxyFallbackError, true)) {
        return;
    }

    setBusy(false);
    appendStatus(tr("Export complete (proxy failed)."));
    appendLog(proxyFallbackError.isEmpty()
                  ? tr("Parallel proxy export failed and fallback proxy generation could not be started.")
                  : proxyFallbackError);
    const QString detailedOutput = parallelProxyStdout
                                   + QStringLiteral("\n")
                                   + parallelProxyStderr
                                   + QStringLiteral("\n")
                                   + proxyFallbackError;
    showExportFailureNotification(tr("Proxy generation failed"),
                                  tr("Main export completed, but proxy generation failed."),
                                  detailedOutput,
                                  tr("Parallel proxy export failed, and fallback proxy generation could not be started."));
    clearRunState();
}

void ExportDialog::handleParallelProxyProcessError(QProcess::ProcessError)
{
    const QString errorText = parallelProxyProcess ? parallelProxyProcess->errorString() : QString();
    parallelProxyRunning = false;
    parallelProxyFinished = true;
    parallelProxySucceeded = false;
    appendLog(errorText.isEmpty()
                  ? tr("Parallel proxy export failed to start.")
                  : tr("Parallel proxy export error: %1").arg(errorText));

    if (!mainExportFinished || !mainExportSucceeded) {
        return;
    }

    appendLog(tr("Parallel proxy export failed to start after main export completion; attempting post-export proxy re-run via tbc-video-export."));
    generateProxyForCurrentRun = true;
    QString proxyFallbackError;
    if (startProxyExport(&proxyFallbackError, true)) {
        return;
    }

    setBusy(false);
    appendStatus(tr("Export complete (proxy failed)."));
    const QString detailedOutput = parallelProxyStdout
                                   + QStringLiteral("\n")
                                   + parallelProxyStderr
                                   + QStringLiteral("\n")
                                   + errorText
                                   + QStringLiteral("\n")
                                   + proxyFallbackError;
    appendLog(proxyFallbackError.isEmpty()
                  ? tr("Parallel proxy export failed to start and fallback proxy generation could not be started.")
                  : proxyFallbackError);
    showExportFailureNotification(tr("Proxy generation failed"),
                                  tr("Main export completed, but proxy generation failed to start."),
                                  detailedOutput,
                                  tr("Parallel proxy export failed to start, and fallback proxy generation could not be started."));
    clearRunState();
}
void ExportDialog::resetProcessStats()
{
    processRowMap.clear();
    if (mainProcessStatsTable) {
        mainProcessStatsTable->setRowCount(0);
    }
    if (proxyProcessStatsTable) {
        proxyProcessStatsTable->setRowCount(0);
    }
}

void ExportDialog::initializeProcessStats()
{
    if (!tbcSource || !mainProcessStatsTable) {
        return;
    }

    const int totalFramesValue = qMax(0, tbcSource->getNumberOfFrames());
    const QString totalFrames = totalFramesValue > 0 ? QString::number(totalFramesValue) : QStringLiteral("—");
    const TbcSource::SourceMode mode = tbcSource->getSourceMode();
    const auto seedStat = [this, &totalFrames](const QString &process,
                                               const QString &tbcType,
                                               const QString &feedTag) {
        ExportProcessStat stat;
        stat.process = process;
        stat.tbcType = tbcType;
        stat.trackedName = QStringLiteral("frame");
        stat.current = QStringLiteral("0");
        stat.total = totalFrames;
        stat.errors = QStringLiteral("0");
        stat.fps = QStringLiteral("—");
        stat.feedTag = feedTag;
        updateProcessStat(stat);
    };
    const auto seedFeed = [&seedStat, mode](const QString &feedTag) {
        if (mode == TbcSource::BOTH_SOURCES) {
            seedStat(QStringLiteral("ld-dropout-correct"), QStringLiteral("LUMA"), feedTag);
            seedStat(QStringLiteral("ld-dropout-correct"), QStringLiteral("CHROMA"), feedTag);
            seedStat(QStringLiteral("ld-chroma-decoder"), QStringLiteral("LUMA"), feedTag);
            seedStat(QStringLiteral("ld-chroma-decoder"), QStringLiteral("CHROMA"), feedTag);
        } else if (mode == TbcSource::LUMA_SOURCE) {
            seedStat(QStringLiteral("ld-dropout-correct"), QStringLiteral("LUMA"), feedTag);
            seedStat(QStringLiteral("ld-chroma-decoder"), QStringLiteral("LUMA"), feedTag);
        } else if (mode == TbcSource::CHROMA_SOURCE) {
            seedStat(QStringLiteral("ld-dropout-correct"), QStringLiteral("CHROMA"), feedTag);
            seedStat(QStringLiteral("ld-chroma-decoder"), QStringLiteral("CHROMA"), feedTag);
        } else {
            seedStat(QStringLiteral("ld-dropout-correct"), QStringLiteral("COMBINED"), feedTag);
            seedStat(QStringLiteral("ld-chroma-decoder"), QStringLiteral("COMBINED"), feedTag);
        }
        seedStat(QStringLiteral("ffmpeg"), QStringLiteral("—"), feedTag);
    };
    seedFeed(kStatsFeedMain);
    if (splitStatsByFeed) {
        seedFeed(kStatsFeedProxy);
    }
}

void ExportDialog::updateProcessStat(const ExportProcessStat &stat)
{
    const QString feedTag = normalizedStatsFeedTag(stat.feedTag);
    const QString keyFeed = splitStatsByFeed ? feedTag : kStatsFeedMain;
    QTableWidget *targetTable = processStatsTableForFeed(keyFeed);
    if (!targetTable) {
        return;
    }
    const QString key = keyFeed + QStringLiteral("|") + stat.process + QStringLiteral("|") + stat.tbcType;
    int row = processRowMap.value(key, -1);
    if (row < 0 || row >= targetTable->rowCount()) {
        row = targetTable->rowCount();
        targetTable->insertRow(row);
        processRowMap.insert(key, row);
    }
    setTableItem(targetTable, row, 0, stat.process);
    setTableItem(targetTable, row, 1, stat.tbcType);
    setTableItem(targetTable, row, 2, stat.trackedName);
    setTableItem(targetTable, row, 3, stat.current);
    setTableItem(targetTable, row, 4, stat.total);
    setTableItem(targetTable, row, 5, stat.errors);
    setTableItem(targetTable, row, 6, stat.fps);
}

void ExportDialog::setBusy(bool busy)
{
    const bool enabled = exportAvailable && !busy;
    const bool browseEnabled = !busy;
    ui->exportButton->setEnabled(enabled);
    if (ui->cancelButton) {
        ui->cancelButton->setEnabled(busy);
    }
    ui->outputBrowseButton->setEnabled(browseEnabled);
    ui->audio1BrowseButton->setEnabled(enabled);
    ui->audio2BrowseButton->setEnabled(enabled);
    ui->audio3BrowseButton->setEnabled(enabled);
    ui->audio4BrowseButton->setEnabled(enabled);
    ui->profileComboBox->setEnabled(enabled);
    if (ui->mainContainerLabel) {
        ui->mainContainerLabel->setEnabled(enabled);
    }
    if (ui->mainContainerComboBox) {
        ui->mainContainerComboBox->setEnabled(enabled);
    }
    if (ui->mainBitDepthLabel) {
        ui->mainBitDepthLabel->setEnabled(enabled);
    }
    if (ui->mainBitDepthComboBox) {
        ui->mainBitDepthComboBox->setEnabled(enabled);
    }
    if (ui->resolutionModeComboBox) {
        ui->resolutionModeComboBox->setEnabled(enabled);
    }
    updateOutputResolutionModeControlsEnabledState(enabled);
    if (ui->audioProfileComboBox) {
        ui->audioProfileComboBox->setEnabled(enabled);
    }
    if (ui->logProcessOutputCheckBox) {
        ui->logProcessOutputCheckBox->setEnabled(enabled);
    }
    if (ui->dropoutModeLabel) {
        ui->dropoutModeLabel->setEnabled(enabled);
    }
    if (ui->dropoutModeComboBox) {
        ui->dropoutModeComboBox->setEnabled(enabled);
    }
    const bool dropoutCorrectionEnabled = !ui->dropoutModeComboBox
                                          || ui->dropoutModeComboBox->currentData().toString() != QStringLiteral("disabled");
    if (ui->dropoutFieldModeLabel) {
        ui->dropoutFieldModeLabel->setEnabled(enabled && dropoutCorrectionEnabled);
    }
    if (ui->dropoutFieldModeComboBox) {
        ui->dropoutFieldModeComboBox->setEnabled(enabled && dropoutCorrectionEnabled);
    }
    if (ui->disableMetadataEmbeddingCheckBox) {
        ui->disableMetadataEmbeddingCheckBox->setEnabled(enabled);
    }
    if (ui->exportMetadataCheckBox) {
        ui->exportMetadataCheckBox->setEnabled(enabled);
    }
    if (ui->ffv1SlicesLabel) {
        ui->ffv1SlicesLabel->setEnabled(enabled);
    }
    if (ui->ffv1SlicesSpinBox) {
        ui->ffv1SlicesSpinBox->setEnabled(enabled);
    }
    if (ui->generateProxyCheckBox) {
        ui->generateProxyCheckBox->setEnabled(enabled);
    }
    if (ui->proxyCodecLabel) {
        ui->proxyCodecLabel->setEnabled(enabled);
    }
    if (ui->proxyCodecComboBox) {
        ui->proxyCodecComboBox->setEnabled(enabled);
    }
    ui->outputLineEdit->setEnabled(browseEnabled);
    if (ui->audio1NameLineEdit) {
        ui->audio1NameLineEdit->setEnabled(enabled);
    }
    if (ui->audio2NameLineEdit) {
        ui->audio2NameLineEdit->setEnabled(enabled);
    }
    if (ui->audio3NameLineEdit) {
        ui->audio3NameLineEdit->setEnabled(enabled);
    }
    if (ui->audio4NameLineEdit) {
        ui->audio4NameLineEdit->setEnabled(enabled);
    }
    ui->audio1LineEdit->setEnabled(enabled);
    ui->audio2LineEdit->setEnabled(enabled);
    ui->audio3LineEdit->setEnabled(enabled);
    ui->audio4LineEdit->setEnabled(enabled);
    if (ui->inPointSpinBox) {
        ui->inPointSpinBox->setEnabled(enabled);
    }
    if (ui->outPointSpinBox) {
        ui->outPointSpinBox->setEnabled(enabled);
    }
    if (ui->inPointTimecodeLineEdit) {
        ui->inPointTimecodeLineEdit->setEnabled(enabled);
    }
    if (ui->outPointTimecodeLineEdit) {
        ui->outPointTimecodeLineEdit->setEnabled(enabled);
    }
    if (ui->resetInOutButton) {
        ui->resetInOutButton->setEnabled(enabled);
    }
    updateProfileDependentControls();
}

QString ExportDialog::resolveVideoExportPath() const
{
    const QStringList candidateDirs = defaultExecutableSearchDirs(currentInputFile);

    QStringList candidateNames;
#if defined(Q_OS_WIN)
    candidateNames << QStringLiteral("tbc-video-export.exe") << QStringLiteral("tbc-video-export");
#else
    candidateNames << QStringLiteral("tbc-video-export");
#endif

    for (const QString &dir : candidateDirs) {
        for (const QString &name : candidateNames) {
            const QString candidate = QDir(dir).filePath(name);
            QFileInfo candidateInfo(candidate);
            if (candidateInfo.exists() && candidateInfo.isExecutable()) {
                return candidate;
            }
        }
    }
    const QString fromPath = QStandardPaths::findExecutable(QStringLiteral("tbc-video-export"));
    if (!fromPath.isEmpty()) {
        return fromPath;
    }

    return QString();
}

QString ExportDialog::resolveFfmpegPath() const
{
    const QStringList candidateDirs = defaultExecutableSearchDirs(currentInputFile);

    QStringList candidateNames;
#if defined(Q_OS_WIN)
    candidateNames << QStringLiteral("ffmpeg.exe") << QStringLiteral("ffmpeg");
#else
    candidateNames << QStringLiteral("ffmpeg");
#endif

    for (const QString &dir : candidateDirs) {
        for (const QString &name : candidateNames) {
            const QString candidate = QDir(dir).filePath(name);
            QFileInfo candidateInfo(candidate);
            if (candidateInfo.exists() && candidateInfo.isExecutable()) {
                return candidate;
            }
        }
    }

    const QString fromPath = QStandardPaths::findExecutable(QStringLiteral("ffmpeg"));
    if (!fromPath.isEmpty()) {
        return fromPath;
    }

    return QString();
}

QString ExportDialog::defaultOutputBaseName(const QString &inputFile) const
{
    QFileInfo info(inputFile);
    const QString baseName = info.completeBaseName();
    const QString dir = info.absolutePath();
    if (dir.isEmpty()) {
        return baseName;
    }
    return QDir(dir).filePath(baseName);
}
QString ExportDialog::sourceDataDirectory() const
{
    QString sourcePath = currentInputFile.trimmed();
    if (sourcePath.isEmpty() && tbcSource) {
        sourcePath = tbcSource->getCurrentSourceFilename().trimmed();
    }
    if (sourcePath.isEmpty()) {
        return QString();
    }
    return QFileInfo(sourcePath).absolutePath();
}

QString ExportDialog::sourceStorageTemporaryRoot() const
{
    const QString sourceDirectory = sourceDataDirectory();
    if (!sourceDirectory.isEmpty()) {
        const QString sourceLocalTempRoot =
            QDir(sourceDirectory).filePath(QStringLiteral(".ld-analyse-temp"));
        if (QDir().mkpath(sourceLocalTempRoot)) {
            return sourceLocalTempRoot;
        }
        return sourceDirectory;
    }
    return QDir::tempPath();
}

QString ExportDialog::sourceStorageTemporaryPath(const QString &fileName) const
{
    return QDir(sourceStorageTemporaryRoot()).filePath(fileName);
}

bool ExportDialog::findExistingOutputFiles(const QString &outputBase, QStringList *existingFiles) const
{
    if (existingFiles) {
        existingFiles->clear();
    }

    const QString sanitizedBase = sanitizeOutputBaseName(outputBase.trimmed());
    if (sanitizedBase.isEmpty()) {
        return false;
    }

    QFileInfo outputInfo(sanitizedBase);
    QStringList foundFiles;
    if (outputInfo.exists()) {
        foundFiles << outputInfo.absoluteFilePath();
    }

    const QDir outputDir = outputInfo.absoluteDir();
    const QString baseName = outputInfo.fileName();
    if (outputDir.exists() && !baseName.isEmpty()) {
        const QStringList matchedNames = outputDir.entryList(
            QStringList() << (baseName + QStringLiteral(".*")),
            QDir::Files | QDir::NoDotAndDotDot,
            QDir::Name);
        for (const QString &name : matchedNames) {
            foundFiles << outputDir.filePath(name);
        }
    }

    foundFiles.removeDuplicates();
    if (existingFiles) {
        *existingFiles = foundFiles;
    }
    return !foundFiles.isEmpty();
}

QString ExportDialog::selectedMainCodecId() const
{
    if (!ui || !ui->profileComboBox) {
        return QStringLiteral("ffv1");
    }
    const QString selectedId = ui->profileComboBox->currentData().toString().trimmed().toLower();
    return selectedId.isEmpty() ? QStringLiteral("ffv1") : selectedId;
}

QString ExportDialog::selectedMainContainerId() const
{
    if (!ui || !ui->mainContainerComboBox) {
        return QStringLiteral("mkv");
    }
    const QString selectedId = ui->mainContainerComboBox->currentData().toString().trimmed().toLower();
    return selectedId.isEmpty() ? QStringLiteral("mkv") : selectedId;
}

QString ExportDialog::selectedExportProfileConfigPath(QString *errorMessage) const
{
    if (errorMessage) {
        errorMessage->clear();
    }
    const bool enabled = ui
                         && ui->exportProfileConfigCheckBox
                         && ui->exportProfileConfigCheckBox->isChecked();
    if (!enabled) {
        return QString();
    }

    const QString selectedPath = exportProfileConfigPath.trimmed();
    if (selectedPath.isEmpty()) {
        if (errorMessage) {
            *errorMessage = tr("External profile set is enabled but no profile file is selected.");
        }
        return QString();
    }

    const QFileInfo selectedInfo(selectedPath);
    if (!selectedInfo.exists() || !selectedInfo.isFile() || !selectedInfo.isReadable()) {
        if (errorMessage) {
            *errorMessage = tr("External profile set is not accessible: %1")
                                .arg(QDir::toNativeSeparators(selectedPath));
        }
        return QString();
    }
    return selectedInfo.absoluteFilePath();
}

int ExportDialog::selectedMainBitDepth() const
{
    if (!ui || !ui->mainBitDepthComboBox) {
        return 10;
    }
    const int selectedBitDepth = ui->mainBitDepthComboBox->currentData().toInt();
    if (selectedBitDepth == 8 || selectedBitDepth == 10) {
        return selectedBitDepth;
    }
    return 10;
}

QString ExportDialog::selectedExportProfileName() const
{
    const QString mainCodecId = selectedMainCodecId();
    if (mainCodecId == QStringLiteral("ffv1")) {
        return QStringLiteral("ffv1");
    }
    if (mainCodecId == QStringLiteral("v210")) {
        return QStringLiteral("v210");
    }
    if (mainCodecId == QStringLiteral("j2k")) {
        return QStringLiteral("j2k");
    }
    if (mainCodecId == QStringLiteral("d10")) {
        return QStringLiteral("d10");
    }
    if (mainCodecId == QStringLiteral("prores")) {
        const QString proresVariant = ui && ui->proresVariantComboBox
                                          ? ui->proresVariantComboBox->currentData().toString().trimmed()
                                          : QStringLiteral("prores_hq");
        const QString normalizedProfileName = normalizedProresProfileName(proresVariant);
        return normalizedProfileName.isEmpty() ? QStringLiteral("prores_hq") : normalizedProfileName;
    }
    if (mainCodecId == QStringLiteral("web")) {
        const QString webCodec = ui && ui->webCodecComboBox
                                     ? ui->webCodecComboBox->currentData().toString().trimmed().toLower()
                                     : QStringLiteral("h264");
        if (webCodec == QStringLiteral("hevc")) {
            return QStringLiteral("h265_web");
        }
        if (webCodec == QStringLiteral("av1")) {
            return QStringLiteral("av1_web");
        }
        return QStringLiteral("h264_web");
    }
    if (mainCodecId == QStringLiteral("avc")) {
        const QString avcRange = ui && ui->avcRangeComboBox
                                     ? ui->avcRangeComboBox->currentData().toString().trimmed().toLower()
                                     : QStringLiteral("standard");
        if (avcRange == QStringLiteral("lossless")) {
            return QStringLiteral("h264_lossless");
        }
        if (avcRange == QStringLiteral("web")) {
            return QStringLiteral("h264_web");
        }
        return QStringLiteral("h264");
    }
    if (mainCodecId == QStringLiteral("hevc")) {
        const QString hevcRange = ui && ui->hevcRangeComboBox
                                      ? ui->hevcRangeComboBox->currentData().toString().trimmed().toLower()
                                      : QStringLiteral("standard");
        if (hevcRange == QStringLiteral("lossless")) {
            return QStringLiteral("h265_lossless");
        }
        if (hevcRange == QStringLiteral("web")) {
            return QStringLiteral("h265_web");
        }
        return QStringLiteral("h265");
    }
    return QStringLiteral("ffv1");
}

QString ExportDialog::proxyExportProfileName(const QString &proxyCodecId) const
{
    const QString normalizedCodec = normalizedProxyCodecId(proxyCodecId);
    if (normalizedCodec == QStringLiteral("hevc")) {
        return QStringLiteral("h265_web");
    }
    if (normalizedCodec == QStringLiteral("av1")) {
        return QStringLiteral("av1_web");
    }
    return QStringLiteral("h264_web");
}

QString ExportDialog::sanitizeOutputBaseName(const QString &path) const
{
    return ExportArguments::sanitizeOutputBasePath(path);
}

QString ExportDialog::videoSystemArg(int system) const
{
    switch (static_cast<VideoSystem>(system)) {
    case PAL:
        return QStringLiteral("pal");
    case PAL_M:
        return QStringLiteral("pal-m");
    case NTSC:
    default:
        return QStringLiteral("ntsc");
    }
}

QStringList ExportDialog::collectAudioTracks() const
{
    QStringList tracks;
    const QStringList candidates = {
        normalizeAudioTrackPathInput(ui->audio1LineEdit->text()),
        normalizeAudioTrackPathInput(ui->audio2LineEdit->text()),
        normalizeAudioTrackPathInput(ui->audio3LineEdit->text()),
        normalizeAudioTrackPathInput(ui->audio4LineEdit->text())
    };
    for (const QString &track : candidates) {
        if (!track.isEmpty()) {
            tracks << track;
        }
    }
    return tracks;
}

bool ExportDialog::shouldGenerateProxyForSelection() const
{
    return ui && ui->generateProxyCheckBox && ui->generateProxyCheckBox->isChecked();
}

QString ExportDialog::selectedProxyCodecId() const
{
    if (!ui || !ui->proxyCodecComboBox) {
        return QStringLiteral("h264");
    }
    return normalizedProxyCodecId(ui->proxyCodecComboBox->currentData().toString());
}

QString ExportDialog::proxyOutputPath(const QString &outputBase, const QString &proxyCodecId) const
{
    const QString sanitizedBase = sanitizeOutputBaseName(outputBase);
    if (sanitizedBase.isEmpty()) {
        return QString();
    }
    Q_UNUSED(proxyCodecId);
    return QStringLiteral("%1_Proxy.mp4").arg(sanitizedBase);
}

// proxy deinterlace routed via tbc-video-export web profile -- do not hand-roll ffmpeg
// bwdif/parity graphs here; field-order (parity) resolution lives in tbc-video-export
// (common/field_order.py) and is inherited by the web profiles (h264_web/h265_web/av1_web).
bool ExportDialog::startProxyExport(QString *errorMessage, bool forceOverwrite)
{
    if (errorMessage) {
        errorMessage->clear();
    }
    if (!generateProxyForCurrentRun) {
        if (errorMessage) {
            *errorMessage = tr("Proxy generation is not enabled for this export.");
        }
        return false;
    }
    if (!tbcSource) {
        if (errorMessage) {
            *errorMessage = tr("No source loaded.");
        }
        return false;
    }

    const QString exportPath = resolveVideoExportPath();
    if (exportPath.isEmpty()) {
        if (errorMessage) {
            *errorMessage = tr("tbc-video-export not found in PATH or alongside ld-analyse.");
        }
        return false;
    }

    const QString proxyProfile = proxyExportProfileName(proxyCodecForCurrentRun);
    const QString proxyOutputBase = sanitizeOutputBaseName(proxyOutputPathForCurrentRun);
    if (proxyOutputBase.isEmpty()) {
        if (errorMessage) {
            *errorMessage = tr("Could not determine proxy output file path.");
        }
        return false;
    }

    const bool overwriteProxyOutput = overwriteExistingForCurrentRun || forceOverwrite;

    // Reuse the same tbc-video-export argument construction the parallel proxy path uses
    // (on_exportButton_clicked). Empty audio tracks + -1/-1 let buildArguments collect audio
    // and in/out points from the UI, matching the parallel run. The metadata snapshot and
    // export config prepared for the main run are still valid here (clearRunState has not run).
    QString proxyArgsError;
    const QStringList proxyArguments = buildArguments(&proxyArgsError,
                                                      temporaryInputJsonPath,
                                                      overwriteProxyOutput,
                                                      temporaryExportConfigPath,
                                                      QStringList(),
                                                      -1,
                                                      -1,
                                                      proxyProfile,
                                                      proxyOutputBase);
    if (proxyArguments.isEmpty()) {
        if (errorMessage) {
            *errorMessage = proxyArgsError.isEmpty()
                                ? tr("Could not prepare proxy export arguments.")
                                : proxyArgsError;
        }
        return false;
    }

    splitStatsByFeed = true;
    updateProcessStatsPaneMode();
    updateFeedLogPaneMode();
    processStdout.clear();
    processStderr.clear();
    pendingStdoutBuffer.clear();
    pendingStderrBuffer.clear();

    QString programToRun;
    QStringList argsToRun;
    prepareVideoExportLaunch(exportPath, proxyArguments, tr("proxy export"), &programToRun, &argsToRun);
    const QString proxyCommand = formatCommand(programToRun, argsToRun);
    appendLog(tr("Proxy command: %1").arg(proxyCommand));
    appendStatus(tr("Generating proxy via tbc-video-export..."));
    appendLog(tr("Generating proxy via tbc-video-export web profile '%1'...").arg(proxyProfile));

    ExportProcessStat stat;
    stat.process = QStringLiteral("tbc-video-export");
    stat.tbcType = QStringLiteral("—");
    stat.trackedName = QStringLiteral("frame");
    stat.current = QStringLiteral("0");
    stat.total = QStringLiteral("—");
    stat.errors = QStringLiteral("0");
    stat.fps = QStringLiteral("—");
    stat.feedTag = kStatsFeedProxy;
    updateProcessStat(stat);

    exportProcess->setWorkingDirectory(QFileInfo(currentInputFile).absolutePath());
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    const QStringList prependDirs = defaultExecutableSearchDirs(currentInputFile);
    QStringList pathEntries = env.value(QStringLiteral("PATH"))
                                  .split(QDir::listSeparator(), Qt::SkipEmptyParts);
    prependUniquePathEntries(&pathEntries, prependDirs);
    if (!pathEntries.isEmpty()) {
        env.insert(QStringLiteral("PATH"), pathEntries.join(QDir::listSeparator()));
    }
    exportProcess->setProcessEnvironment(env);

    activeRunStage = RunStage::ProxyExport;
    exportProcess->start(programToRun, argsToRun);
    if (!exportProcess->waitForStarted(5000)) {
        activeRunStage = RunStage::Idle;
        if (errorMessage) {
            *errorMessage = tr("Failed to start tbc-video-export for proxy generation.");
        }
        return false;
    }
    return true;
}

void ExportDialog::prepareVideoExportLaunch(const QString &exportPath,
                                            const QStringList &exportArgs,
                                            const QString &contextLabel,
                                            QString *programOut,
                                            QStringList *argsOut)
{
    if (!programOut || !argsOut) {
        return;
    }

    *programOut = exportPath;
    *argsOut = exportArgs;

#if defined(Q_OS_UNIX)
    const QString scriptPath = QStandardPaths::findExecutable(QStringLiteral("script"));
    if (!scriptPath.isEmpty()) {
#if defined(Q_OS_MACOS)
        *programOut = scriptPath;
        *argsOut = {QStringLiteral("-q"), QStringLiteral("/dev/null"), exportPath};
        argsOut->append(exportArgs);
        appendLog(tr("Using script (bsd) to enable ANSI progress output for %1.")
                      .arg(contextLabel));
#else
        const QString scriptCommand = formatShellCommand(exportPath, exportArgs);
        *programOut = scriptPath;
        *argsOut = {QStringLiteral("-q"), QStringLiteral("-e"), QStringLiteral("-c"),
                    scriptCommand, QStringLiteral("/dev/null")};
        appendLog(tr("Using script to enable ANSI progress output for %1.")
                      .arg(contextLabel));
#endif
        return;
    }

    if (!argsOut->contains(QStringLiteral("--show-process-output"))) {
        argsOut->append(QStringLiteral("--show-process-output"));
        appendLog(tr("script command not found; using --show-process-output for %1.")
                      .arg(contextLabel));
    }
#else
    Q_UNUSED(contextLabel);
#endif
}

void ExportDialog::clearRunState()
{
    if (parallelProxyProcess && parallelProxyProcess->state() != QProcess::NotRunning) {
        parallelProxyProcess->kill();
    }
    activeRunStage = RunStage::Idle;
    generateProxyForCurrentRun = false;
    audioConfiguredForCurrentRun = false;
    overwriteExistingForCurrentRun = false;
    outputBaseForCurrentRun.clear();
    proxyCodecForCurrentRun.clear();
    proxyOutputPathForCurrentRun.clear();
    parallelProxyRunning = false;
    parallelProxyFinished = false;
    parallelProxySucceeded = false;
    mainExportFinished = false;
    mainExportSucceeded = false;
    parallelProxyStdout.clear();
    parallelProxyStderr.clear();
    pendingParallelProxyStdoutBuffer.clear();
    pendingParallelProxyStderrBuffer.clear();
    clearFeedStatusLines();
    clearFeedLogLines();
    splitStatsByFeed = shouldGenerateProxyForSelection();
    updateProcessStatsPaneMode();
    updateFeedLogPaneMode();
    cleanupTemporaryMetadataSnapshot();
}
bool ExportDialog::prepareTrimmedAudioTracks(int zeroBasedStartFrame,
                                             int rangeLengthFrames,
                                             QStringList *audioTracks,
                                             QString *errorMessage)
{
    if (errorMessage) {
        errorMessage->clear();
    }
    if (!audioTracks) {
        if (errorMessage) {
            *errorMessage = tr("Internal error: no audio track list supplied.");
        }
        return false;
    }
    if (audioTracks->isEmpty()) {
        return true;
    }
    if (!tbcSource) {
        if (errorMessage) {
            *errorMessage = tr("No source loaded.");
        }
        return false;
    }

    for (const QString &path : temporaryAudioTrackPaths) {
        QFile::remove(path);
    }
    temporaryAudioTrackPaths.clear();

    const QString ffmpegPath = resolveFfmpegPath();
    if (ffmpegPath.isEmpty()) {
        if (errorMessage) {
            *errorMessage = tr("ffmpeg not found in PATH or alongside ld-analyse.");
        }
        return false;
    }

    const int totalFrames = qMax(1, tbcSource->getNumberOfFrames());
    const int clampedStart = qBound(0, zeroBasedStartFrame, qMax(0, totalFrames - 1));
    const int maxLength = qMax(0, totalFrames - clampedStart);
    const int clampedLength = qBound(1, rangeLengthFrames, qMax(1, maxLength));
    if (clampedLength <= 0) {
        if (errorMessage) {
            *errorMessage = tr("Invalid audio trim range.");
        }
        return false;
    }

    const LdDecodeMetaData::VideoParameters &videoParameters = tbcSource->getVideoParameters();
    const double fps = frameRateForSystem(videoParameters.system);
    if (fps <= 0.0) {
        if (errorMessage) {
            *errorMessage = tr("Invalid video frame rate for audio trimming.");
        }
        return false;
    }

    const QString startArg = QString::number(static_cast<double>(clampedStart) / fps, 'f', 6);
    const QString durationArg = QString::number(static_cast<double>(clampedLength) / fps, 'f', 6);
    const double trimDurationSeconds = static_cast<double>(clampedLength) / fps;
    const qint64 minTrimTimeoutMs = 120000LL;
    const qint64 maxTrimTimeoutMs = 21600000LL;
    const qint64 estimatedTrimTimeoutMs =
        static_cast<qint64>((trimDurationSeconds + 60.0) * 4000.0);
    const qint64 trimTimeoutMs = qMax(minTrimTimeoutMs,
                                      qMin(estimatedTrimTimeoutMs, maxTrimTimeoutMs));

    QStringList trimmedTracks;
    trimmedTracks.reserve(audioTracks->size());
    for (int index = 0; index < audioTracks->size(); ++index) {
        const QString sourceTrack = normalizeAudioTrackPathInput(audioTracks->at(index));
        if (sourceTrack.isEmpty()) {
            continue;
        }

        const QFileInfo sourceInfo(sourceTrack);
        if (!sourceInfo.exists() || !sourceInfo.isFile()) {
            for (const QString &path : trimmedTracks) {
                QFile::remove(path);
            }
            trimmedTracks.clear();
            temporaryAudioTrackPaths.clear();
            if (errorMessage) {
                *errorMessage = tr("Audio track not found: %1").arg(sourceTrack);
            }
            return false;
        }

        const QString trimmedPath = sourceStorageTemporaryPath(
            QStringLiteral("ld-analyse-audio-range-%1-%2.wav")
                .arg(index + 1)
                .arg(QUuid::createUuid().toString(QUuid::WithoutBraces)));
        const bool isRawPcmTrack = sourceInfo.suffix().compare(QStringLiteral("pcm"), Qt::CaseInsensitive) == 0;
        QStringList ffmpegArgs;
        ffmpegArgs << QStringLiteral("-hide_banner")
                   << QStringLiteral("-v") << QStringLiteral("error")
                   << QStringLiteral("-nostdin")
                   << QStringLiteral("-y")
                   << QStringLiteral("-ss") << startArg
                   << QStringLiteral("-t") << durationArg;
        if (isRawPcmTrack) {
            ffmpegArgs << QStringLiteral("-f") << QStringLiteral("s16le")
                       << QStringLiteral("-ar") << QStringLiteral("44100")
                       << QStringLiteral("-ac") << QStringLiteral("2");
        }
        ffmpegArgs << QStringLiteral("-i") << sourceTrack
                   << QStringLiteral("-map") << QStringLiteral("0:a:0")
                   << QStringLiteral("-vn")
                   << QStringLiteral("-sn")
                   << QStringLiteral("-dn")
                   << QStringLiteral("-c:a") << QStringLiteral("pcm_s24le")
                   << trimmedPath;

        QProcess ffmpegProcess;
        ffmpegProcess.setProcessChannelMode(QProcess::MergedChannels);
        ffmpegProcess.start(ffmpegPath, ffmpegArgs);
        if (!ffmpegProcess.waitForStarted(5000)) {
            for (const QString &path : trimmedTracks) {
                QFile::remove(path);
            }
            trimmedTracks.clear();
            temporaryAudioTrackPaths.clear();
            if (errorMessage) {
                *errorMessage = tr("Failed to start ffmpeg for audio track trimming.");
            }
            return false;
        }
        if (!ffmpegProcess.waitForFinished(trimTimeoutMs)) {
            ffmpegProcess.kill();
            for (const QString &path : trimmedTracks) {
                QFile::remove(path);
            }
            trimmedTracks.clear();
            temporaryAudioTrackPaths.clear();
            if (errorMessage) {
                *errorMessage = tr("ffmpeg timed out while trimming audio tracks (timeout: %1 seconds).")
                                    .arg(trimTimeoutMs / 1000);
            }
            return false;
        }

        const QString ffmpegOutput = QString::fromLocal8Bit(ffmpegProcess.readAllStandardOutput()).trimmed();
        const bool ffmpegOk = ffmpegProcess.exitStatus() == QProcess::NormalExit
                              && ffmpegProcess.exitCode() == 0
                              && QFileInfo::exists(trimmedPath);
        if (!ffmpegOk) {
            QFile::remove(trimmedPath);
            for (const QString &path : trimmedTracks) {
                QFile::remove(path);
            }
            trimmedTracks.clear();
            temporaryAudioTrackPaths.clear();
            if (errorMessage) {
                *errorMessage = ffmpegOutput.isEmpty()
                                    ? tr("ffmpeg failed while trimming audio tracks.")
                                    : ffmpegOutput;
            }
            return false;
        }

        trimmedTracks << trimmedPath;
    }

    temporaryAudioTrackPaths = trimmedTracks;
    *audioTracks = trimmedTracks;
    return true;
}

QStringList ExportDialog::buildArguments(QString *errorMessage, const QString &inputTbcJsonOverride,
                                         bool overwriteExisting,
                                         const QString &configFileOverride,
                                         const QStringList &audioTracks,
                                         int startFrameOneBasedOverride,
                                         int lengthOverride,
                                         const QString &profileOverride,
                                         const QString &outputBaseOverride) const
{
    QStringList args;
    if (!tbcSource) {
        if (errorMessage) {
            *errorMessage = tr("No source loaded.");
        }
        return args;
    }

    const QString inputFile = tbcSource->getCurrentSourceFilename();
    if (inputFile.isEmpty()) {
        if (errorMessage) {
            *errorMessage = tr("No input file available.");
        }
        return args;
    }
    const QString configuredOutputBase = outputBaseOverride.trimmed().isEmpty()
                                             ? ui->outputLineEdit->text().trimmed()
                                             : outputBaseOverride.trimmed();
    const QString outputBase = sanitizeOutputBaseName(configuredOutputBase);
    if (outputBase.isEmpty()) {
        if (errorMessage) {
            *errorMessage = tr("Please provide an output base name.");
        }
        return args;
    }
    const int totalFrames = qMax(1, tbcSource->getNumberOfFrames());
    int startFrameOneBased = startFrameOneBasedOverride;
    int rangeLength = lengthOverride;
    if (startFrameOneBased <= 0 || rangeLength <= 0) {
        int inPoint = ui->inPointSpinBox ? ui->inPointSpinBox->value() : 1;
        int outPoint = ui->outPointSpinBox ? ui->outPointSpinBox->value() : totalFrames;
        inPoint = qBound(1, inPoint, totalFrames);
        outPoint = qBound(1, outPoint, totalFrames);
        if (outPoint < inPoint) {
            if (errorMessage) {
                *errorMessage = tr("Out point must be greater than or equal to In point.");
            }
            return args;
        }
        startFrameOneBased = inPoint;
        rangeLength = outPoint - inPoint + 1;
    }
    startFrameOneBased = qBound(1, startFrameOneBased, totalFrames);
    rangeLength = qMin(rangeLength, totalFrames - startFrameOneBased + 1);
    if (rangeLength <= 0) {
        if (errorMessage) {
            *errorMessage = tr("Invalid export range.");
        }
        return args;
    }
    args << QStringLiteral("--start") << QString::number(startFrameOneBased);
    args << QStringLiteral("--length") << QString::number(rangeLength);

    const bool usingProfileOverride = !profileOverride.trimmed().isEmpty();
    const QString profile = usingProfileOverride
                                ? profileOverride.trimmed()
                                : selectedExportProfileName();
    const bool exportMetadataRequested = !ui
                                         || !ui->exportMetadataCheckBox
                                         || ui->exportMetadataCheckBox->isChecked();
    const bool includeExportMetadata = exportMetadataRequested && !isWebProfileName(profile);
    if (includeExportMetadata) {
        args << QStringLiteral("--export-metadata");
    }
    const QString exportPath = resolveVideoExportPath();
    const bool deinterlacedOutputProfile = isWebProfileName(profile);
    if (!configFileOverride.isEmpty()) {
        args << QStringLiteral("--config-file") << configFileOverride;
    }
    if (!profile.isEmpty()) {
        args << QStringLiteral("--profile") << profile;
    }
    if (!usingProfileOverride) {
        const QString mainContainer = selectedMainContainerId();
        if (!mainContainer.isEmpty()) {
            args << QStringLiteral("--profile-container") << mainContainer;
        }
        const int bitDepth = selectedMainBitDepth();
        if (bitDepth == 8) {
            args << QStringLiteral("--8bit");
        } else if (bitDepth == 10) {
            args << QStringLiteral("--10bit");
        }
    }
    const QString dropoutMode = ui->dropoutModeComboBox
                                    ? ui->dropoutModeComboBox->currentData().toString()
                                    : QStringLiteral("basic");
    const QString correctionMode = ui->dropoutFieldModeComboBox
                                       ? ui->dropoutFieldModeComboBox->currentData().toString()
                                       : QStringLiteral("innerfield");
    const bool supportsDropoutInterfieldCorrection = executableSupportsOption(
        exportPath, QStringLiteral("--dropout-interfield-correction"));
    const QString outputResolutionMode = effectiveOutputResolutionMode(ui ? ui->outputResolutionModeComboBox : nullptr);
    if (ExportArguments::shouldDisableDropoutCorrection(dropoutMode, startFrameOneBased)) {
        args << QStringLiteral("--no-dropout-correct");
    } else {
        if (dropoutMode == QStringLiteral("heavy")
            && executableSupportsOption(exportPath, QStringLiteral("--overcorrect"))) {
            args << QStringLiteral("--overcorrect");
        }
        if (supportsDropoutInterfieldCorrection) {
            const QString correctionValue = dropoutInterfieldCorrectionValue(tbcSource, correctionMode);
            if (!correctionValue.isEmpty()) {
                args << QStringLiteral("--dropout-interfield-correction") << correctionValue;
            }
        } else if (correctionMode == QStringLiteral("intra")) {
            if (executableSupportsOption(exportPath, QStringLiteral("--intra"))) {
                args << QStringLiteral("--intra");
            }
        } else if (correctionMode == QStringLiteral("innerfield")
                   || correctionMode == QStringLiteral("interfield")) {
            if (executableSupportsOption(exportPath, QStringLiteral("--innerfield"))) {
                args << QStringLiteral("--innerfield");
            } else if (executableSupportsOption(exportPath, QStringLiteral("--interfield"))) {
                args << QStringLiteral("--interfield");
            }
        }
    }
    if (ui->disableMetadataEmbeddingCheckBox && ui->disableMetadataEmbeddingCheckBox->isChecked()) {
        args << QStringLiteral("--no-attach-json");
    }

    const QStringList tracksToUse = audioTracks.isEmpty() ? collectAudioTracks() : audioTracks;
    QStringList trackTitles;
    const auto appendTrackTitle = [&trackTitles](const QLineEdit *fileEdit, const QLineEdit *titleEdit) {
        if (!fileEdit) {
            return;
        }
        if (fileEdit->text().trimmed().isEmpty()) {
            return;
        }
        trackTitles << (titleEdit ? titleEdit->text().trimmed() : QString());
    };
    if (ui) {
        appendTrackTitle(ui->audio1LineEdit, ui->audio1NameLineEdit);
        appendTrackTitle(ui->audio2LineEdit, ui->audio2NameLineEdit);
        appendTrackTitle(ui->audio3LineEdit, ui->audio3NameLineEdit);
        appendTrackTitle(ui->audio4LineEdit, ui->audio4NameLineEdit);
    }
    for (int i = 0; i < tracksToUse.size(); ++i) {
        const QString track = normalizeAudioTrackPathInput(tracksToUse.at(i));
        if (track.isEmpty()) {
            continue;
        }
        const QString title = (i < trackTitles.size()) ? trackTitles.at(i).trimmed() : QString();
        const bool isRawPcmTrack =
            QFileInfo(track).suffix().compare(QStringLiteral("pcm"), Qt::CaseInsensitive) == 0;
        if (isRawPcmTrack) {
            QJsonArray advancedTrack;
            advancedTrack.append(track);
            advancedTrack.append(title.isEmpty() ? QJsonValue(QJsonValue::Null)
                                                 : QJsonValue(title));
            advancedTrack.append(QJsonValue(QJsonValue::Null));
            advancedTrack.append(44100);
            advancedTrack.append(QStringLiteral("s16le"));
            advancedTrack.append(2);
            args << QStringLiteral("--audio-track-advanced")
                 << pythonLiteralFromJsonArray(advancedTrack);
            continue;
        }
        if (!title.isEmpty()) {
            QJsonArray advancedTrack;
            advancedTrack.append(track);
            advancedTrack.append(title);
            args << QStringLiteral("--audio-track-advanced")
                 << pythonLiteralFromJsonArray(advancedTrack);
            continue;
        }
        args << QStringLiteral("--audio-track") << track;
    }
    if (ui->logProcessOutputCheckBox && ui->logProcessOutputCheckBox->isChecked()) {
        args << QStringLiteral("--log-process-output");
    }
#if defined(Q_OS_WIN)
    args << QStringLiteral("--show-process-output");
#endif
    if (overwriteExisting) {
        args << QStringLiteral("--overwrite");
    }

    const LdDecodeMetaData::VideoParameters &videoParameters = tbcSource->getVideoParameters();
    const bool isPalSystem = (videoParameters.system == PAL || videoParameters.system == PAL_M);
    const bool isNtscSystem = (videoParameters.system == NTSC);
    if (videoParameters.isValid) {
        args << QStringLiteral("--video-system") << videoSystemArg(videoParameters.system);
    }

    QString normalizedDecoderName;
    if (!videoParameters.chromaDecoder.isEmpty()) {
        normalizedDecoderName = videoParameters.chromaDecoder.trimmed().toLower();
        if (isValidChromaDecoderForSystem(normalizedDecoderName, videoParameters.system)) {
            args << QStringLiteral("--chroma-decoder") << normalizedDecoderName;
        }
    }
    const bool monoDecoderSelected = normalizedDecoderName == QStringLiteral("mono");
    if (!monoDecoderSelected && videoParameters.chromaGain >= 0.0) {
        args << QStringLiteral("--chroma-gain") << QString::number(videoParameters.chromaGain, 'f', 6);
    }
    if (!monoDecoderSelected && videoParameters.chromaPhase != -1.0) {
        args << QStringLiteral("--chroma-phase") << QString::number(videoParameters.chromaPhase, 'f', 3);
    }
    const bool isSplitSource = tbcSource && tbcSource->getSourceMode() != TbcSource::ONE_SOURCE;
    if (!isSplitSource && videoParameters.lumaNR >= 0.0) {
        args << QStringLiteral("--luma-nr") << QString::number(videoParameters.lumaNR, 'f', 3);
    }
    const QString resolutionMode = effectiveResolutionMode(tbcSource, ui ? ui->resolutionModeComboBox : nullptr);

    auto appendFramingArgs = [&args](int ffll, int lfll, int ffrl, int lfrl) {
        if (ffll > 0) {
            args << QStringLiteral("--ffll") << QString::number(ffll);
        }
        if (lfll > 0) {
            args << QStringLiteral("--lfll") << QString::number(lfll);
        }
        if (ffrl > 0) {
            args << QStringLiteral("--ffrl") << QString::number(ffrl);
        }
        if (lfrl > 0) {
            args << QStringLiteral("--lfrl") << QString::number(lfrl);
        }
    };
    const bool hasAnyVerticalLineAdjustment = hasAnyNonDefaultVerticalFraming(videoParameters);
    const bool isDefaultActiveAreaFraming =
        ExportArguments::isDefaultActiveAreaFraming(resolutionMode, hasAnyVerticalLineAdjustment);
    const bool letterboxCropRequested = ui->letterboxCropCheckBox
                                        && ui->letterboxCropCheckBox->isChecked();
    const bool forceAnamorphicRequested = ui->forceAnamorphicCheckBox
                                          && ui->forceAnamorphicCheckBox->isChecked();
    auto unsupportedOptionError = [this, &exportPath](const QString &featureLabel, const QString &optionName) {
        if (exportPath.isEmpty()) {
            return tr("%1 requires tbc-video-export option %2, but no export executable was found.")
                .arg(featureLabel)
                .arg(optionName);
        }
        return tr("%1 requires tbc-video-export option %2. Detected export tool does not support it: %3")
            .arg(featureLabel)
            .arg(optionName)
            .arg(exportPath);
    };
    if (letterboxCropRequested && forceAnamorphicRequested) {
        if (errorMessage) {
            *errorMessage = tr("Force anamorphic cannot be used with letterbox crop.");
        }
        return QStringList();
    }
    if (letterboxCropRequested) {
        if (!isDefaultActiveAreaFraming) {
            if (errorMessage) {
                *errorMessage = tr("Letterbox crop is only available with default Active Area framing.");
            }
            return QStringList();
        }
        if (!executableSupportsOption(exportPath, QStringLiteral("--letterbox"))) {
            if (errorMessage) {
                *errorMessage = unsupportedOptionError(tr("Letterbox crop"), QStringLiteral("--letterbox"));
            }
            return QStringList();
        }
        args << QStringLiteral("--letterbox");
    }
    if (forceAnamorphicRequested) {
        if (!executableSupportsOption(exportPath, QStringLiteral("--force-anamorphic"))) {
            if (errorMessage) {
                *errorMessage =
                    unsupportedOptionError(tr("Force anamorphic"), QStringLiteral("--force-anamorphic"));
            }
            return QStringList();
        }
        args << QStringLiteral("--force-anamorphic");
    }
    if (isFullFrame4fscResolutionMode(resolutionMode)) {
        const bool supportsFullFrame = executableSupportsOption(exportPath, QStringLiteral("--full-frame"));
        if (supportsFullFrame) {
            args << QStringLiteral("--full-frame");
        } else {
            const QString framingLabel = resolutionMode == QStringLiteral("hybrid_4fsc")
                                             ? tr("Hybrid 4fsc")
                                             : tr("Full-Frame 4fsc");
            if (errorMessage) {
                if (exportPath.isEmpty()) {
                    *errorMessage = tr("%1 export requires tbc-video-export with --full-frame support, but no export executable was found.")
                                        .arg(framingLabel);
                } else {
                    *errorMessage = tr("%1 export requires tbc-video-export with --full-frame support. Detected export tool does not support it: %2")
                                        .arg(framingLabel)
                                        .arg(exportPath);
                }
            }
            return QStringList();
        }
    } else if (resolutionMode == QStringLiteral("active_vbi")) {
        args << QStringLiteral("--vbi");
    } else if (resolutionMode == QStringLiteral("user_defined") || hasAnyVerticalLineAdjustment) {
        appendFramingArgs(videoParameters.firstActiveFieldLine,
                          videoParameters.lastActiveFieldLine,
                          videoParameters.firstActiveFrameLine,
                          videoParameters.lastActiveFrameLine);
    }

    if (isNtscSystem && videoParameters.ntscPhaseCompensation >= 0) {
        args << (videoParameters.ntscPhaseCompensation ? QStringLiteral("--ntsc-phase-comp")
                                                       : QStringLiteral("--no-ntsc-phase-comp"));
    }
    if (isPalSystem && videoParameters.palTransformThreshold >= 0.0) {
        args << QStringLiteral("--transform-threshold") << QString::number(videoParameters.palTransformThreshold, 'f', 3);
    }
    {
        const OutputResamplePlan outputResamplePlan = outputResamplePlanForModes(videoParameters.system,
                                                                                  resolutionMode,
                                                                                  outputResolutionMode);
        const bool supportsD1OutputSizing = executableSupportsD1OutputSizing(exportPath);
        const bool supportsAppendVideoFilter =
            executableSupportsOption(exportPath, QStringLiteral("--append-video-filter"));
        const bool requiresD1OutputSizing = outputResamplePlan.enabled
                                            && shouldUseD1OutputSizing(resolutionMode,
                                                                       outputResolutionMode);
        const bool mainProfileIsDeinterlaced = isWebProfileName(selectedExportProfileName());
        const bool proxyNtscD1Fallback = requiresD1OutputSizing
                                         && usingProfileOverride
                                         && deinterlacedOutputProfile
                                         && isNtscSystem
                                         && !mainProfileIsDeinterlaced
                                         && !supportsAppendVideoFilter
                                         && supportsD1OutputSizing;
        const bool useD1OutputSizing = requiresD1OutputSizing
                                       && supportsD1OutputSizing
                                       && (!deinterlacedOutputProfile || proxyNtscD1Fallback);
        if (useD1OutputSizing) {
            args << QStringLiteral("--d1");
        } else if (outputResamplePlan.enabled && supportsAppendVideoFilter) {
            const QString filterExpr = outputResampleFilter(outputResamplePlan,
                                                            !deinterlacedOutputProfile);
            if (!filterExpr.isEmpty()) {
                args << QStringLiteral("--append-video-filter") << filterExpr;
            }
        }
    }

    if (!inputTbcJsonOverride.isEmpty()) {
        args << QStringLiteral("--input-tbc-json") << inputTbcJsonOverride;
    }

    args << inputFile << outputBase;
    return args;
}
QString ExportDialog::createTemporaryExportConfig(QString *errorMessage,
                                                  const QString &profileName,
                                                  const QString &audioProfileName,
                                                  const QString &baseConfigOverridePath)
{
    if (errorMessage) {
        errorMessage->clear();
    }

    if (!temporaryExportConfigPath.isEmpty()) {
        QFile::remove(temporaryExportConfigPath);
        temporaryExportConfigPath.clear();
    }

    const QString selectedProfile = profileName.trimmed();
    const QString selectedAudioProfile = audioProfileName.trimmed();
    if (selectedProfile.isEmpty() || selectedAudioProfile.isEmpty()) {
        if (errorMessage) {
            *errorMessage = tr("Please select both video and audio profiles.");
        }
        return QString();
    }

    QString dumpDirPath;
    const auto cleanupDumpDir = [&dumpDirPath]() {
        if (dumpDirPath.isEmpty()) {
            return;
        }
        QDir dumpDir(dumpDirPath);
        if (dumpDir.exists()) {
            dumpDir.removeRecursively();
        }
        dumpDirPath.clear();
    };

    QByteArray configData;
    const QString normalizedBaseConfigPath = baseConfigOverridePath.trimmed();
    if (!normalizedBaseConfigPath.isEmpty()) {
        QFile baseConfigFile(normalizedBaseConfigPath);
        if (!baseConfigFile.open(QIODevice::ReadOnly)) {
            if (errorMessage) {
                *errorMessage = tr("Failed to read custom JSON profile set: %1")
                                    .arg(QDir::toNativeSeparators(normalizedBaseConfigPath));
            }
            return QString();
        }
        configData = baseConfigFile.readAll();
        baseConfigFile.close();
    } else {
        const QString exportPath = resolveVideoExportPath();
        if (exportPath.isEmpty()) {
            if (errorMessage) {
                *errorMessage = tr("tbc-video-export not found.");
            }
            return QString();
        }

        dumpDirPath = sourceStorageTemporaryPath(
            QStringLiteral("ld-analyse-export-config-dump-%1")
                .arg(QUuid::createUuid().toString(QUuid::WithoutBraces)));
        if (!QDir().mkpath(dumpDirPath)) {
            if (errorMessage) {
                *errorMessage = tr("Failed to create temporary directory for export configuration.");
            }
            return QString();
        }

        QProcess dumpProcess;
        dumpProcess.setProcessChannelMode(QProcess::MergedChannels);
        dumpProcess.setWorkingDirectory(dumpDirPath);
        dumpProcess.start(exportPath, QStringList() << QStringLiteral("--dump-default-config"));
        if (!dumpProcess.waitForStarted(3000)) {
            cleanupDumpDir();
            if (errorMessage) {
                *errorMessage = tr("Failed to start export config generation.");
            }
            return QString();
        }
        if (!dumpProcess.waitForFinished(15000)) {
            dumpProcess.kill();
            cleanupDumpDir();
            if (errorMessage) {
                *errorMessage = tr("Export config generation timed out.");
            }
            return QString();
        }
        const QString dumpOutput = QString::fromLocal8Bit(dumpProcess.readAllStandardOutput()).trimmed();
        if (dumpProcess.exitStatus() != QProcess::NormalExit || dumpProcess.exitCode() != 0) {
            cleanupDumpDir();
            if (errorMessage) {
                *errorMessage = dumpOutput.isEmpty()
                                    ? tr("Failed to generate default export configuration.")
                                    : dumpOutput;
            }
            return QString();
        }

        const QString dumpedConfigPath = QDir(dumpDirPath).filePath(QStringLiteral("tbc-video-export.json"));
        QFile dumpedConfigFile(dumpedConfigPath);
        if (!dumpedConfigFile.open(QIODevice::ReadOnly)) {
            cleanupDumpDir();
            if (errorMessage) {
                *errorMessage = tr("Failed to read generated export configuration.");
            }
            return QString();
        }
        configData = dumpedConfigFile.readAll();
        dumpedConfigFile.close();
    }

    QJsonParseError parseError;
    const QJsonDocument configDoc = QJsonDocument::fromJson(configData, &parseError);
    if (configDoc.isNull() || !configDoc.isObject()) {
        cleanupDumpDir();
        if (errorMessage) {
            *errorMessage = tr("Invalid export configuration JSON: %1").arg(parseError.errorString());
        }
        return QString();
    }

    QJsonObject root = configDoc.object();
    QJsonArray audioProfiles = root.value(QStringLiteral("audio_profiles")).toArray();
    auto upsertAudioProfile = [&audioProfiles](const QJsonObject &profileObject) {
        const QString targetName = profileObject.value(QStringLiteral("name")).toString();
        for (int i = 0; i < audioProfiles.size(); ++i) {
            const QJsonObject existing = audioProfiles.at(i).toObject();
            if (existing.value(QStringLiteral("name")).toString() == targetName) {
                audioProfiles.replace(i, profileObject);
                return;
            }
        }
        audioProfiles.append(profileObject);
    };

    {
        QJsonObject flac16Profile;
        flac16Profile.insert(QStringLiteral("name"), QStringLiteral("flac_16"));
        flac16Profile.insert(QStringLiteral("description"), QStringLiteral("FLAC 16-bits"));
        flac16Profile.insert(QStringLiteral("codec"), QStringLiteral("flac"));
        QJsonArray opts;
        opts.append(QStringLiteral("-compression_level"));
        opts.append(12);
        opts.append(QStringLiteral("-sample_fmt"));
        opts.append(QStringLiteral("s16"));
        flac16Profile.insert(QStringLiteral("opts"), opts);
        upsertAudioProfile(flac16Profile);
    }
    {
        QJsonObject flac24Profile;
        flac24Profile.insert(QStringLiteral("name"), QStringLiteral("flac_24"));
        flac24Profile.insert(QStringLiteral("description"), QStringLiteral("FLAC 24-bits"));
        flac24Profile.insert(QStringLiteral("codec"), QStringLiteral("flac"));
        QJsonArray opts;
        opts.append(QStringLiteral("-compression_level"));
        opts.append(12);
        opts.append(QStringLiteral("-sample_fmt"));
        opts.append(QStringLiteral("s32"));
        flac24Profile.insert(QStringLiteral("opts"), opts);
        upsertAudioProfile(flac24Profile);
    }
    {
        QJsonObject aac16Profile;
        aac16Profile.insert(QStringLiteral("name"), QStringLiteral("aac_16"));
        aac16Profile.insert(QStringLiteral("description"), QStringLiteral("AAC 16-bits"));
        aac16Profile.insert(QStringLiteral("codec"), QStringLiteral("aac"));
        QJsonArray opts;
        opts.append(QStringLiteral("-ar"));
        opts.append(48000);
        opts.append(QStringLiteral("-b:a"));
        opts.append(QStringLiteral("256K"));
        opts.append(QStringLiteral("-sample_fmt"));
        opts.append(QStringLiteral("s16"));
        aac16Profile.insert(QStringLiteral("opts"), opts);
        upsertAudioProfile(aac16Profile);
    }
    {
        QJsonObject aac24Profile;
        aac24Profile.insert(QStringLiteral("name"), QStringLiteral("aac_24"));
        aac24Profile.insert(QStringLiteral("description"), QStringLiteral("AAC 24-bits"));
        aac24Profile.insert(QStringLiteral("codec"), QStringLiteral("aac"));
        QJsonArray opts;
        opts.append(QStringLiteral("-ar"));
        opts.append(48000);
        opts.append(QStringLiteral("-b:a"));
        opts.append(QStringLiteral("320K"));
        opts.append(QStringLiteral("-sample_fmt"));
        opts.append(QStringLiteral("s32"));
        aac24Profile.insert(QStringLiteral("opts"), opts);
        upsertAudioProfile(aac24Profile);
    }
    {
        QJsonObject opus16Profile;
        opus16Profile.insert(QStringLiteral("name"), QStringLiteral("opus_16"));
        opus16Profile.insert(QStringLiteral("description"), QStringLiteral("Opus 16-bits"));
        opus16Profile.insert(QStringLiteral("codec"), QStringLiteral("libopus"));
        QJsonArray opts;
        opts.append(QStringLiteral("-ar"));
        opts.append(48000);
        opts.append(QStringLiteral("-b:a"));
        opts.append(QStringLiteral("192K"));
        opts.append(QStringLiteral("-sample_fmt"));
        opts.append(QStringLiteral("s16"));
        opus16Profile.insert(QStringLiteral("opts"), opts);
        upsertAudioProfile(opus16Profile);
    }
    {
        QJsonObject opus24Profile;
        opus24Profile.insert(QStringLiteral("name"), QStringLiteral("opus_24"));
        opus24Profile.insert(QStringLiteral("description"), QStringLiteral("Opus 24-bits"));
        opus24Profile.insert(QStringLiteral("codec"), QStringLiteral("libopus"));
        QJsonArray opts;
        opts.append(QStringLiteral("-ar"));
        opts.append(48000);
        opts.append(QStringLiteral("-b:a"));
        opts.append(QStringLiteral("256K"));
        opts.append(QStringLiteral("-sample_fmt"));
        opts.append(QStringLiteral("s32"));
        opus24Profile.insert(QStringLiteral("opts"), opts);
        upsertAudioProfile(opus24Profile);
    }
    root.insert(QStringLiteral("audio_profiles"), audioProfiles);

    bool audioProfileExists = false;
    for (int i = 0; i < audioProfiles.size(); ++i) {
        const QJsonObject audioProfileObject = audioProfiles.at(i).toObject();
        if (audioProfileObject.value(QStringLiteral("name")).toString() == selectedAudioProfile) {
            audioProfileExists = true;
            break;
        }
    }
    if (!audioProfileExists) {
        cleanupDumpDir();
        if (errorMessage) {
            *errorMessage = tr("Unsupported audio profile selected: %1").arg(selectedAudioProfile);
        }
        return QString();
    }

    QJsonArray profiles = root.value(QStringLiteral("profiles")).toArray();
    QJsonArray videoProfiles = root.value(QStringLiteral("video_profiles")).toArray();
    const auto profileObjectByNameExists = [](const QJsonArray &profileArray, const QString &targetName) {
        for (const QJsonValue &profileValue : profileArray) {
            if (!profileValue.isObject()) {
                continue;
            }
            const QJsonObject profileObject = profileValue.toObject();
            const QString profileName = profileObject.value(QStringLiteral("name")).toString();
            if (profileName.compare(targetName, Qt::CaseInsensitive) == 0) {
                return true;
            }
        }
        return false;
    };
    const auto appendProfileObjectIfMissing = [&profileObjectByNameExists](QJsonArray *profileArray,
                                                                            const QJsonObject &profileObject) {
        if (!profileArray) {
            return;
        }
        const QString profileName = profileObject.value(QStringLiteral("name")).toString();
        if (profileName.isEmpty() || profileObjectByNameExists(*profileArray, profileName)) {
            return;
        }
        profileArray->append(profileObject);
    };
    const auto buildProresVideoProfileObject = [](const QString &profileName,
                                                  const QString &profileDescription,
                                                  int ffmpegProfileValue,
                                                  bool useVideoToolbox) {
        QJsonObject videoProfileObject;
        videoProfileObject.insert(QStringLiteral("name"), profileName);
        videoProfileObject.insert(
            QStringLiteral("description"),
            useVideoToolbox
                ? QStringLiteral("%1 - Apple Video Toolbox").arg(profileDescription)
                : profileDescription);
        videoProfileObject.insert(QStringLiteral("codec"),
                                  useVideoToolbox
                                      ? QStringLiteral("prores_videotoolbox")
                                      : QStringLiteral("prores_ks"));
        videoProfileObject.insert(QStringLiteral("video_format"),
                                  useVideoToolbox
                                      ? QStringLiteral("uyvy422")
                                      : QStringLiteral("yuv422p10le"));
        videoProfileObject.insert(QStringLiteral("container"), QStringLiteral("mov"));
        QJsonArray opts;
        opts.append(QStringLiteral("-profile:v"));
        opts.append(QString::number(ffmpegProfileValue));
        if (!useVideoToolbox) {
            opts.append(QStringLiteral("-vendor"));
            opts.append(QStringLiteral("apl0"));
        }
        opts.append(QStringLiteral("-flags"));
        opts.append(QStringLiteral("+ildct+ilme"));
        videoProfileObject.insert(QStringLiteral("opts"), opts);
        if (useVideoToolbox) {
            videoProfileObject.insert(QStringLiteral("hardware_accel"), QStringLiteral("videotoolbox"));
        }
        return videoProfileObject;
    };
    const auto ensureProresVariantProfile = [&appendProfileObjectIfMissing,
                                             &buildProresVideoProfileObject,
                                             &profiles,
                                             &videoProfiles](const QString &profileName,
                                                             const QString &profileDescription,
                                                             const QString &videoProfileName,
                                                             const QString &videoProfileVideoToolboxName,
                                                             int ffmpegProfileValue) {
        appendProfileObjectIfMissing(
            &videoProfiles,
            buildProresVideoProfileObject(videoProfileName,
                                          profileDescription,
                                          ffmpegProfileValue,
                                          false));
        appendProfileObjectIfMissing(
            &videoProfiles,
            buildProresVideoProfileObject(videoProfileVideoToolboxName,
                                          profileDescription,
                                          ffmpegProfileValue,
                                          true));

        QJsonObject profileObject;
        profileObject.insert(QStringLiteral("name"), profileName);
        QJsonArray profileVideoProfiles;
        profileVideoProfiles.append(videoProfileName);
        profileVideoProfiles.append(videoProfileVideoToolboxName);
        profileObject.insert(QStringLiteral("video_profile"), profileVideoProfiles);
        profileObject.insert(QStringLiteral("audio_profile"), QStringLiteral("pcm_24"));
        appendProfileObjectIfMissing(&profiles, profileObject);
    };
    const QString normalizedSelectedProresProfile = normalizedProresProfileName(selectedProfile);
    if (normalizedSelectedProresProfile == QStringLiteral("prores")) {
        ensureProresVariantProfile(QStringLiteral("prores"),
                                   QStringLiteral("ProRes 422"),
                                   QStringLiteral("prores_422"),
                                   QStringLiteral("prores_422_videotoolbox"),
                                   2);
        root.insert(QStringLiteral("video_profiles"), videoProfiles);
    } else if (normalizedSelectedProresProfile == QStringLiteral("prores_lt")) {
        ensureProresVariantProfile(QStringLiteral("prores_lt"),
                                   QStringLiteral("ProRes 422 LT"),
                                   QStringLiteral("prores_422_lt"),
                                   QStringLiteral("prores_422_lt_videotoolbox"),
                                   1);
        root.insert(QStringLiteral("video_profiles"), videoProfiles);
    } else if (normalizedSelectedProresProfile == QStringLiteral("prores_proxy")) {
        ensureProresVariantProfile(QStringLiteral("prores_proxy"),
                                   QStringLiteral("ProRes 422 Proxy"),
                                   QStringLiteral("prores_422_proxy"),
                                   QStringLiteral("prores_422_proxy_videotoolbox"),
                                   0);
        root.insert(QStringLiteral("video_profiles"), videoProfiles);
    }
    bool profileFound = false;
    QJsonObject selectedProfileObject;
    for (int i = 0; i < profiles.size(); ++i) {
        const QJsonValue currentValue = profiles.at(i);
        if (!currentValue.isObject()) {
            continue;
        }
        QJsonObject profileObject = currentValue.toObject();
        if (profileObject.value(QStringLiteral("name")).toString().compare(selectedProfile, Qt::CaseInsensitive) != 0) {
            continue;
        }
        profileObject.insert(QStringLiteral("audio_profile"), selectedAudioProfile);
        selectedProfileObject = profileObject;
        profiles.replace(i, profileObject);
        profileFound = true;
        break;
    }
    if (!profileFound) {
        cleanupDumpDir();
        if (errorMessage) {
            *errorMessage = tr("Selected video profile is not available in the export configuration.");
        }
        return QString();
    }
    root.insert(QStringLiteral("profiles"), profiles);
    if (isWebProfileName(selectedProfile)) {
        const QStringList selectedVideoProfiles = profileVideoProfileNames(selectedProfileObject);
        if (!selectedVideoProfiles.isEmpty()) {
            QJsonArray videoProfiles = root.value(QStringLiteral("video_profiles")).toArray();
            bool changedVideoProfiles = false;
            for (int i = 0; i < videoProfiles.size(); ++i) {
                if (!videoProfiles.at(i).isObject()) {
                    continue;
                }
                QJsonObject videoProfileObject = videoProfiles.at(i).toObject();
                const QString videoProfileName = videoProfileObject.value(QStringLiteral("name")).toString();
                if (!listContainsCaseInsensitive(selectedVideoProfiles, videoProfileName)) {
                    continue;
                }
                if (videoProfileObject.value(QStringLiteral("container")).toString().compare(
                        QStringLiteral("mp4"), Qt::CaseInsensitive) == 0) {
                    continue;
                }
                videoProfileObject.insert(QStringLiteral("container"), QStringLiteral("mp4"));
                videoProfiles.replace(i, videoProfileObject);
                changedVideoProfiles = true;
            }
            if (changedVideoProfiles) {
                root.insert(QStringLiteral("video_profiles"), videoProfiles);
            }
        }
    }
    if (isFfv1ProfileName(selectedProfile) && ui->ffv1SlicesSpinBox) {
        const int ffv1Slices = qBound(1, ui->ffv1SlicesSpinBox->value(), 32);
        const QStringList selectedVideoProfiles = profileVideoProfileNames(selectedProfileObject);
        QJsonArray videoProfiles = root.value(QStringLiteral("video_profiles")).toArray();
        bool changedVideoProfiles = false;
        bool updatedSelectedVideoProfile = false;
        for (int i = 0; i < videoProfiles.size(); ++i) {
            if (!videoProfiles.at(i).isObject()) {
                continue;
            }
            QJsonObject videoProfileObject = videoProfiles.at(i).toObject();
            const QString videoProfileName = videoProfileObject.value(QStringLiteral("name")).toString();
            const bool selectedVideoProfile = listContainsCaseInsensitive(selectedVideoProfiles, videoProfileName);
            if (!selectedVideoProfiles.isEmpty() && !selectedVideoProfile) {
                continue;
            }
            const QString codec = videoProfileObject.value(QStringLiteral("codec")).toString();
            const bool isFfv1VideoProfile = codec.compare(QStringLiteral("ffv1"), Qt::CaseInsensitive) == 0
                                            || videoProfileName.contains(QStringLiteral("ffv1"), Qt::CaseInsensitive);
            if (!isFfv1VideoProfile) {
                continue;
            }
            setOrAppendNumericOption(&videoProfileObject, QStringLiteral("-slices"), ffv1Slices);
            videoProfiles.replace(i, videoProfileObject);
            changedVideoProfiles = true;
            updatedSelectedVideoProfile = true;
        }
        if (!updatedSelectedVideoProfile) {
            for (int i = 0; i < videoProfiles.size(); ++i) {
                if (!videoProfiles.at(i).isObject()) {
                    continue;
                }
                QJsonObject videoProfileObject = videoProfiles.at(i).toObject();
                const QString videoProfileName = videoProfileObject.value(QStringLiteral("name")).toString();
                const QString codec = videoProfileObject.value(QStringLiteral("codec")).toString();
                const bool isFfv1VideoProfile = codec.compare(QStringLiteral("ffv1"), Qt::CaseInsensitive) == 0
                                                || videoProfileName.contains(QStringLiteral("ffv1"), Qt::CaseInsensitive);
                if (!isFfv1VideoProfile) {
                    continue;
                }
                setOrAppendNumericOption(&videoProfileObject, QStringLiteral("-slices"), ffv1Slices);
                videoProfiles.replace(i, videoProfileObject);
                changedVideoProfiles = true;
            }
        }
        if (changedVideoProfiles) {
            root.insert(QStringLiteral("video_profiles"), videoProfiles);
        }
    }

    const QString configPath = sourceStorageTemporaryPath(
        QStringLiteral("ld-analyse-export-config-%1.json")
            .arg(QUuid::createUuid().toString(QUuid::WithoutBraces)));
    QFile outputConfigFile(configPath);
    if (!outputConfigFile.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        cleanupDumpDir();
        if (errorMessage) {
            *errorMessage = tr("Failed to write temporary export configuration.");
        }
        return QString();
    }
    outputConfigFile.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    outputConfigFile.close();

    cleanupDumpDir();
    temporaryExportConfigPath = configPath;
    return temporaryExportConfigPath;
}

QString ExportDialog::createTemporaryMetadataSnapshot(QString *errorMessage)
{
    cleanupTemporaryMetadataSnapshot();

    if (!tbcSource) {
        if (errorMessage) {
            *errorMessage = tr("No source loaded.");
        }
        return QString();
    }

    const QString tempPath = sourceStorageTemporaryPath(
        QStringLiteral("ld-analyse-export-%1.tbc.json")
            .arg(QUuid::createUuid().toString(QUuid::WithoutBraces)));

    QString snapshotError;
    if (!tbcSource->writeMetadataSnapshot(tempPath, &snapshotError)) {
        if (errorMessage) {
            *errorMessage = snapshotError.isEmpty()
                                ? tr("Failed to create metadata snapshot.")
                                : snapshotError;
        }
        return QString();
    }

    temporaryInputJsonPath = tempPath;
    return temporaryInputJsonPath;
}

void ExportDialog::cleanupTemporaryMetadataSnapshot()
{
    if (!temporaryInputJsonPath.isEmpty()) {
        QFile::remove(temporaryInputJsonPath);
        temporaryInputJsonPath.clear();
    }
    if (!temporaryExportConfigPath.isEmpty()) {
        QFile::remove(temporaryExportConfigPath);
        temporaryExportConfigPath.clear();
    }
    for (const QString &path : temporaryAudioTrackPaths) {
        QFile::remove(path);
    }
    temporaryAudioTrackPaths.clear();
    cleanupSourceStorageTemporaryRoot();
}

void ExportDialog::cleanupSourceStorageTemporaryRoot()
{
    const QString sourceDirectory = sourceDataDirectory();
    if (sourceDirectory.isEmpty()) {
        return;
    }
    const QString tempRoot = QDir(sourceDirectory).filePath(QStringLiteral(".ld-analyse-temp"));
    if (tempRoot.isEmpty() || tempRoot == sourceDirectory) {
        return;
    }
    QDir tempDir(tempRoot);
    if (!tempDir.exists()) {
        return;
    }
    // Best-effort removal of our own leftover temp files (always prefixed "ld-analyse-").
    const QStringList leftovers = tempDir.entryList(
        QStringList() << QStringLiteral("ld-analyse-*"),
        QDir::Files | QDir::NoDotAndDotDot);
    for (const QString &name : leftovers) {
        QFile::remove(tempDir.filePath(name));
    }
    // Only drop the folder itself when nothing else remains, so we never
    // clobber files we do not own.
    const QStringList remaining = tempDir.entryList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot);
    if (remaining.isEmpty()) {
        tempDir.removeRecursively();
    }
}

void ExportDialog::appendStatus(const QString &message)
{
    ui->statusLabel->setText(message);
}
void ExportDialog::appendFeedStatus(const QString &message, const QString &feedTag)
{
    const QString trimmed = message.trimmed();
    if (trimmed.isEmpty()) {
        return;
    }
    if (!splitStatsByFeed) {
        appendStatus(trimmed);
        return;
    }

    const QString normalizedFeedTag = normalizedStatsFeedTag(feedTag);
    if (normalizedFeedTag == kStatsFeedProxy) {
        proxyFeedStatusLine = trimmed;
    } else {
        mainFeedStatusLine = trimmed;
    }

    const QString mainLine = mainFeedStatusLine.isEmpty() ? QStringLiteral("—") : mainFeedStatusLine;
    const QString proxyLine = proxyFeedStatusLine.isEmpty() ? QStringLiteral("—") : proxyFeedStatusLine;
    appendStatus(QStringLiteral("Main: %1\nProxy: %2").arg(mainLine, proxyLine));
}

void ExportDialog::clearFeedStatusLines()
{
    mainFeedStatusLine.clear();
    proxyFeedStatusLine.clear();
}

void ExportDialog::appendFeedLog(const QString &message, const QString &feedTag)
{
    const QString trimmed = message.trimmed();
    if (trimmed.isEmpty()) {
        return;
    }
    if (!splitStatsByFeed || !ui || !ui->logTextEdit || !ui->proxyLogTextEdit) {
        appendLog(trimmed);
        return;
    }

    const QString normalizedFeedTag = normalizedStatsFeedTag(feedTag);
    QString *lineTarget = normalizedFeedTag == kStatsFeedProxy ? &proxyFeedLogLine : &mainFeedLogLine;
    QPlainTextEdit *targetLog = normalizedFeedTag == kStatsFeedProxy ? ui->proxyLogTextEdit : ui->logTextEdit;
    if (*lineTarget == trimmed) {
        return;
    }
    *lineTarget = trimmed;

    const QString timestamp = QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss"));
    const QString rendered = QStringLiteral("[%1] %2").arg(timestamp, trimmed);
    if (!rendered.isEmpty()) {
        targetLog->appendPlainText(rendered);
    }
}

void ExportDialog::clearFeedLogLines()
{
    mainFeedLogLine.clear();
    proxyFeedLogLine.clear();
    if (ui) {
        if (ui->logTextEdit) {
            ui->logTextEdit->clear();
        }
        if (ui->proxyLogTextEdit) {
            ui->proxyLogTextEdit->clear();
        }
    }
}

void ExportDialog::appendLog(const QString &message)
{
    if (!ui || !ui->logTextEdit) {
        return;
    }
    const QString trimmed = message.trimmed();
    if (trimmed.isEmpty()) {
        return;
    }
    const QString timestamp = QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss"));
    const QString line = QStringLiteral("[%1] %2").arg(timestamp, trimmed);
    ui->logTextEdit->appendPlainText(line);
}
QString ExportDialog::writeExportFailureLog(const QString &failureTitle,
                                            const QString &summaryMessage,
                                            const QString &detailedOutput) const
{
    QString outputDirectory;
    if (!outputBaseForCurrentRun.trimmed().isEmpty()) {
        outputDirectory = QFileInfo(outputBaseForCurrentRun).absolutePath();
    }
    if (outputDirectory.isEmpty() && !currentInputFile.trimmed().isEmpty()) {
        outputDirectory = QFileInfo(currentInputFile).absolutePath();
    }
    if (outputDirectory.isEmpty()) {
        outputDirectory = QDir::tempPath();
    }
    if (!QDir().mkpath(outputDirectory)) {
        outputDirectory = QDir::tempPath();
    }

    const QString timestampToken = QDateTime::currentDateTime().toString(QStringLiteral("dd.MM.yyyy-hh.mm.ss"));
    QDir outputDir(outputDirectory);
    QString logPath = outputDir.filePath(QStringLiteral("export-fail-%1.log").arg(timestampToken));
    int collisionIndex = 1;
    while (QFileInfo::exists(logPath)) {
        logPath = outputDir.filePath(QStringLiteral("export-fail-%1-%2.log")
                                         .arg(timestampToken)
                                         .arg(collisionIndex));
        collisionIndex++;
    }

    QFile logFile(logPath);
    if (!logFile.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
        return QString();
    }

    QTextStream stream(&logFile);
    stream << "Timestamp: "
           << QDateTime::currentDateTime().toString(Qt::ISODateWithMs)
           << '\n';
    stream << "Failure: " << failureTitle.trimmed() << '\n';
    if (!summaryMessage.trimmed().isEmpty()) {
        stream << "Summary: " << summaryMessage.trimmed() << '\n';
    }
    if (!currentInputFile.trimmed().isEmpty()) {
        stream << "Input: " << QDir::toNativeSeparators(currentInputFile) << '\n';
    }
    if (!outputBaseForCurrentRun.trimmed().isEmpty()) {
        stream << "Output base: " << QDir::toNativeSeparators(outputBaseForCurrentRun) << '\n';
    }
    if (!proxyOutputPathForCurrentRun.trimmed().isEmpty()) {
        stream << "Proxy output: " << QDir::toNativeSeparators(proxyOutputPathForCurrentRun) << '\n';
    }
    stream << '\n';

    stream << "=== Process output ===\n";
    if (detailedOutput.trimmed().isEmpty()) {
        stream << "(none captured)\n";
    } else {
        stream << detailedOutput.trimmed() << '\n';
    }

    if (ui && ui->logTextEdit) {
        const QString exportLog = ui->logTextEdit->toPlainText().trimmed();
        if (!exportLog.isEmpty()) {
            stream << '\n';
            stream << "=== Main export dialog log ===\n";
            stream << exportLog << '\n';
        }
    }
    if (ui && ui->proxyLogTextEdit) {
        const QString proxyExportLog = ui->proxyLogTextEdit->toPlainText().trimmed();
        if (!proxyExportLog.isEmpty()) {
            stream << '\n';
            stream << "=== Proxy export dialog log ===\n";
            stream << proxyExportLog << '\n';
        }
    }

    stream.flush();
    if (stream.status() != QTextStream::Ok) {
        logFile.close();
        QFile::remove(logPath);
        return QString();
    }
    logFile.close();
    return logPath;
}

void ExportDialog::showExportFailureNotification(const QString &dialogTitle,
                                                 const QString &summaryMessage,
                                                 const QString &detailedOutput,
                                                 const QString &fallbackMessage)
{
    QString finalSummary = summaryMessage.trimmed();
    if (finalSummary.isEmpty()) {
        finalSummary = fallbackMessage.trimmed();
    }
    if (finalSummary.isEmpty()) {
        finalSummary = tr("Export failed.");
    }

    const QString logPath = writeExportFailureLog(dialogTitle, finalSummary, detailedOutput);
    QString dialogMessage = finalSummary;
    if (!logPath.isEmpty()) {
        const QString nativeLogPath = QDir::toNativeSeparators(logPath);
        appendLog(tr("Export failure log saved to: %1").arg(nativeLogPath));
        dialogMessage += tr("\n\nDetailed error output was saved to:\n%1").arg(nativeLogPath);
    } else {
        appendLog(tr("Failed to write export failure log file."));
        dialogMessage += tr("\n\nDetailed error output could not be saved. Check the Export log pane.");
    }

    QMessageBox::warning(this, dialogTitle, dialogMessage);
}

QString ExportDialog::formatCommand(const QString &program, const QStringList &args) const
{
    QStringList parts;
    parts << quoteArgument(program);
    for (const QString &arg : args) {
        parts << quoteArgument(arg);
    }
    return parts.join(' ');
}
