/******************************************************************************
 * teletextviewerdialog.cpp
 * tbc-analyse - TBC output analysis GUI
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2018-2026 Simon Inns
 * SPDX-FileCopyrightText: 2026 Harry Munday
 *
 * This file is part of tbc-tools.
 ******************************************************************************/

#include "teletextviewerdialog.h"
#include "teletextnativeviewwidget.h"

#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDesktopServices>
#include <QDir>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QEvent>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMap>
#include <QMessageBox>
#include <QMimeData>
#include <QProcess>
#include <QProcessEnvironment>
#include <QPushButton>
#include <QRegularExpression>
#include <QShortcut>
#include <QStackedWidget>
#include <QStandardPaths>
#include <QScreen>
#include <QShowEvent>
#include <QTextBrowser>
#include <QTextDocument>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>
#include <QWheelEvent>
#include <QWindow>
namespace {
const QRegularExpression kTeletextStreamSuffixPattern(
    QStringLiteral("^t\\d\\d$"),
    QRegularExpression::CaseInsensitiveOption
);
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

bool directoryContainsHtmlPages(const QString &directoryPath)
{
    if (directoryPath.trimmed().isEmpty()) {
        return false;
    }
    const QDir directory(directoryPath);
    if (!directory.exists()) {
        return false;
    }
    const QStringList htmlPages = directory.entryList(
        QStringList() << QStringLiteral("*.html"),
        QDir::Files,
        QDir::Name | QDir::IgnoreCase
    );
    return !htmlPages.isEmpty();
}

struct TeletextInputSelection {
    QString directoryPath;
    QString htmlPageName;
    QString streamFilePath;

    bool isValid() const
    {
        return !directoryPath.isEmpty() || !streamFilePath.isEmpty();
    }
};

bool isTeletextStreamFile(const QFileInfo &pathInfo)
{
    return pathInfo.exists()
           && pathInfo.isFile()
           && kTeletextStreamSuffixPattern.match(pathInfo.suffix()).hasMatch();
}

TeletextInputSelection resolveTeletextInputFromHint(const QString &pathHint)
{
    TeletextInputSelection selection;
    const QFileInfo pathInfo(pathHint);
    if (!pathInfo.exists()) {
        return selection;
    }

    if (pathInfo.isFile() && pathInfo.suffix().compare(QStringLiteral("html"), Qt::CaseInsensitive) == 0) {
        const QDir htmlDirectory(pathInfo.absolutePath());
        if (directoryContainsHtmlPages(htmlDirectory.absolutePath())) {
            selection.directoryPath = htmlDirectory.absolutePath();
            selection.htmlPageName = pathInfo.fileName();
            return selection;
        }
    }

    if (pathInfo.isDir() && directoryContainsHtmlPages(pathInfo.absoluteFilePath())) {
        selection.directoryPath = QDir(pathInfo.absoluteFilePath()).absolutePath();
        return selection;
    }

    if (isTeletextStreamFile(pathInfo)) {
        selection.streamFilePath = pathInfo.absoluteFilePath();
        return selection;
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
        if (!directoryContainsHtmlPages(candidate)) {
            continue;
        }
        selection.directoryPath = QDir(candidate).absolutePath();
        return selection;
    }
    return selection;
}

TeletextInputSelection resolveTeletextInputFromHints(const QStringList &pathHints)
{
    TeletextInputSelection selection;
    for (const QString &pathHint : pathHints) {
        const TeletextInputSelection resolvedSelection = resolveTeletextInputFromHint(pathHint);
        if (!resolvedSelection.isValid()) {
            continue;
        }
        return resolvedSelection;
    }
    return selection;
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
    return QFileInfo::exists(directory.filePath(QStringLiteral("teletext/__main__.py")))
           && QFileInfo::exists(directory.filePath(QStringLiteral("misc/teletext.css")));
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
    appendUniqueCandidate(candidates, QDir::current().filePath(QStringLiteral("src/ld-process-vbi/vendor/vhs-teletext")));

    for (const QString &candidate : candidates) {
        if (isTeletextVendorDirectory(candidate)) {
            return QDir::cleanPath(candidate);
        }
    }
    return QString();
}

QString resolvePythonExecutable()
{
    const QString overrideValue = qEnvironmentVariable("TELETEXT_PYTHON").trimmed();
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
                   QString *errorMessage)
{
    QProcess process;
    process.setProcessEnvironment(environment);
    process.start(pythonExecutable, arguments);

    if (!process.waitForStarted(5000)) {
        if (errorMessage) {
            *errorMessage = QObject::tr("Could not start Python teletext converter.");
        }
        return false;
    }
    if (!process.waitForFinished(120000)) {
        process.kill();
        process.waitForFinished(3000);
        if (errorMessage) {
            *errorMessage = QObject::tr("Teletext conversion timed out.");
        }
        return false;
    }

    const QString stdoutText = QString::fromLocal8Bit(process.readAllStandardOutput()).trimmed();
    const QString stderrText = QString::fromLocal8Bit(process.readAllStandardError()).trimmed();
    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
        QString details = stderrText;
        if (details.isEmpty()) {
            details = stdoutText;
        }
        if (details.isEmpty()) {
            details = QObject::tr("unknown error");
        }
        if (errorMessage) {
            *errorMessage = QObject::tr("Teletext conversion failed: %1").arg(details);
        }
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
        if (errorMessage) {
            *errorMessage = QObject::tr("Could not replace existing file: %1").arg(targetPath);
        }
        return false;
    }
    if (!QFile::copy(sourcePath, targetPath)) {
        if (errorMessage) {
            *errorMessage = QObject::tr("Could not copy file from %1 to %2").arg(sourcePath, targetPath);
        }
        return false;
    }
    return true;
}

void ensureCssSwitchScript(const QString &outputDirectoryPath)
{
    const QString cssSwitchPath = QDir(outputDirectoryPath).filePath(QStringLiteral("cssswitch.js"));
    if (QFileInfo::exists(cssSwitchPath)) {
        return;
    }

    QFile cssSwitchFile(cssSwitchPath);
    if (!cssSwitchFile.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        return;
    }
    cssSwitchFile.write("function set_style_from_cookie() {\n");
    cssSwitchFile.write("    // Placeholder helper for generated teletext HTML pages.\n");
    cssSwitchFile.write("}\n");
    cssSwitchFile.close();
}

QString cacheDirectoryForTeletextStream(const QFileInfo &streamInfo)
{
    QString rootDirectory = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    if (rootDirectory.trimmed().isEmpty()) {
        rootDirectory = QDir::tempPath();
    }

    QString streamBaseName = streamInfo.completeBaseName();
    if (streamBaseName.trimmed().isEmpty()) {
        streamBaseName = QStringLiteral("teletext");
    }
    streamBaseName.replace(QRegularExpression(QStringLiteral("[^A-Za-z0-9._-]")), QStringLiteral("_"));

    QByteArray keyData = streamInfo.absoluteFilePath().toUtf8();
    keyData.append('|');
    keyData.append(QByteArray::number(streamInfo.size()));
    keyData.append('|');
    keyData.append(QByteArray::number(streamInfo.lastModified().toMSecsSinceEpoch()));
    keyData.append("|teletext-viewer-v2");
    const QString streamHash = QString::fromLatin1(
        QCryptographicHash::hash(keyData, QCryptographicHash::Sha1).toHex().left(16)
    );

    return QDir(rootDirectory).filePath(
        QStringLiteral("teletext_viewer_cache/%1_%2").arg(streamBaseName, streamHash)
    );
}

qint32 bitCount(quint8 value)
{
    qint32 count = 0;
    quint8 bits = value;
    while (bits != 0) {
        count += bits & 0x01;
        bits >>= 1;
    }
    return count;
}

qint32 decodeHamming8Nibble(quint8 value)
{
    static const quint8 hamming8Enc[16] = {
        0x15, 0x02, 0x49, 0x5e, 0x64, 0x73, 0x38, 0x2f,
        0xd0, 0xc7, 0x8c, 0x9b, 0xa1, 0xb6, 0xfd, 0xea
    };

    qint32 bestNibble = 0;
    qint32 bestDistance = 9;
    for (qint32 nibble = 0; nibble < 16; ++nibble) {
        const qint32 distance = bitCount(static_cast<quint8>(hamming8Enc[nibble] ^ value));
        if (distance < bestDistance) {
            bestDistance = distance;
            bestNibble = nibble;
            if (distance == 0) {
                break;
            }
        }
    }
    return bestNibble;
}

qint32 decodeHamming16Value(quint8 lowByte, quint8 highByte)
{
    const qint32 lowNibble = decodeHamming8Nibble(lowByte);
    const qint32 highNibble = decodeHamming8Nibble(highByte);
    return lowNibble | (highNibble << 4);
}

QChar teletextDefaultG0Char(quint8 value)
{
    switch (value) {
    case 0x23:
        return QChar(0x00A3);
    case 0x5B:
        return QChar(0x2190);
    case 0x5C:
        return QChar(0x00BD);
    case 0x5D:
        return QChar(0x2192);
    case 0x5E:
        return QChar(0x2191);
    case 0x5F:
        return QChar(0x0023);
    case 0x60:
        return QChar(0x2014);
    case 0x7B:
        return QChar(0x00BC);
    case 0x7C:
        return QChar(0x2016);
    case 0x7D:
        return QChar(0x00BE);
    case 0x7E:
        return QChar(0x00F7);
    case 0x7F:
        return QChar(0x25A0);
    default:
        return QChar(value);
    }
}

struct TeletextRowParseState {
    quint8 foreground = 7;
    quint8 background = 0;
    bool doubleWidth = false;
    bool doubleHeight = false;
    bool mosaic = false;
    bool solidMosaic = true;
    bool flash = false;
    bool conceal = false;
    bool boxed = false;
    bool rendered = true;
    QChar heldMosaic = QChar(u' ');
    bool heldSolidMosaic = true;
    bool holdMosaic = false;
    bool escape = false;
};

QChar teletextCharacterForCode(quint8 value, const TeletextRowParseState &state)
{
    if (state.mosaic && (value < 0x40 || value > 0x5F)) {
        const char32_t codePoint = static_cast<char32_t>(
            (state.solidMosaic ? 0xEE00 : 0xEDE0) + value
        );
        return QChar(static_cast<ushort>(codePoint));
    }
    return teletextDefaultG0Char(value);
}

TeletextNativeViewWidget::Cell makeTeletextCell(QChar character, const TeletextRowParseState &state)
{
    TeletextNativeViewWidget::Cell cell;
    cell.character = character;
    cell.foreground = state.foreground;
    cell.background = state.background;
    cell.doubleHeight = state.doubleHeight;
    cell.flash = state.flash;
    cell.conceal = state.conceal;
    cell.boxed = state.boxed;
    return cell;
}

TeletextNativeViewWidget::Row parseTeletextDisplayRow(const QByteArray &rawRowBytes)
{
    TeletextNativeViewWidget::Row row;
    row.reserve(40);
    TeletextRowParseState state;

    auto emitCharacter = [&](QChar character) {
        row.append(makeTeletextCell(character, state));
        if (state.doubleWidth) {
            state.rendered = !state.rendered;
        } else {
            state.rendered = true;
        }
    };

    auto emitControlPlaceholder = [&]() {
        if (state.holdMosaic) {
            const bool previousSolidState = state.solidMosaic;
            state.solidMosaic = state.heldSolidMosaic;
            emitCharacter(state.heldMosaic);
            state.solidMosaic = previousSolidState;
        } else {
            emitCharacter(QChar(u' '));
        }
    };

    qint32 previousCode = -1;
    for (qint32 index = 0; index < rawRowBytes.size(); ++index) {
        const quint8 code = static_cast<quint8>(rawRowBytes.at(index)) & 0x7F;
        const quint8 highNibble = code & 0xF0;
        const quint8 lowNibble = code & 0x0F;

        if (highNibble == 0x00) {
            if (lowNibble < 0x08) {
                emitControlPlaceholder();
                state.foreground = lowNibble;
                state.mosaic = false;
                state.conceal = false;
                state.heldMosaic = QChar(u' ');
            } else if (lowNibble == 0x08) {
                emitControlPlaceholder();
                state.flash = true;
            } else if (lowNibble == 0x09) {
                state.flash = false;
                emitControlPlaceholder();
            } else if (lowNibble == 0x0A) {
                if (previousCode == 0x0A) {
                    state.boxed = false;
                    emitControlPlaceholder();
                } else {
                    emitControlPlaceholder();
                }
            } else if (lowNibble == 0x0B) {
                if (previousCode == 0x0B) {
                    state.boxed = true;
                    emitControlPlaceholder();
                } else {
                    emitControlPlaceholder();
                }
            } else {
                const bool nextDoubleHeight = (lowNibble & 0x01) != 0;
                const bool nextDoubleWidth = (lowNibble & 0x02) != 0;
                if (nextDoubleHeight || nextDoubleWidth) {
                    emitControlPlaceholder();
                    state.doubleHeight = nextDoubleHeight;
                    state.doubleWidth = nextDoubleWidth;
                    state.heldMosaic = QChar(u' ');
                } else {
                    state.doubleHeight = false;
                    state.doubleWidth = false;
                    state.heldMosaic = QChar(u' ');
                    emitControlPlaceholder();
                }
            }
            previousCode = code;
            continue;
        }

        if (highNibble == 0x10) {
            if (lowNibble < 0x08) {
                emitControlPlaceholder();
                state.foreground = lowNibble;
                state.mosaic = true;
                state.conceal = false;
            } else if (lowNibble == 0x08) {
                state.conceal = true;
                emitControlPlaceholder();
            } else if (lowNibble == 0x09) {
                state.solidMosaic = true;
                emitControlPlaceholder();
            } else if (lowNibble == 0x0A) {
                state.solidMosaic = false;
                emitControlPlaceholder();
            } else if (lowNibble == 0x0B) {
                emitControlPlaceholder();
                state.escape = !state.escape;
            } else if (lowNibble == 0x0C) {
                state.background = 0;
                emitControlPlaceholder();
            } else if (lowNibble == 0x0D) {
                state.background = state.foreground;
                emitControlPlaceholder();
            } else if (lowNibble == 0x0E) {
                state.holdMosaic = true;
                emitControlPlaceholder();
            } else if (lowNibble == 0x0F) {
                emitControlPlaceholder();
                state.holdMosaic = false;
            }
            previousCode = code;
            continue;
        }

        const QChar teletextCharacter = teletextCharacterForCode(code, state);
        if (state.mosaic && (code & 0x20) != 0) {
            state.heldMosaic = teletextCharacter;
            state.heldSolidMosaic = state.solidMosaic;
        }
        emitCharacter(teletextCharacter);
        previousCode = code;
    }

    while (row.size() < 40) {
        row.append(makeTeletextCell(QChar(u' '), state));
    }
    if (row.size() > 40) {
        row.resize(40);
    }
    return row;
}

QString teletextCellCssClass(const TeletextNativeViewWidget::Cell &cell)
{
    QString cssClass = QStringLiteral("f%1 b%2").arg(cell.foreground).arg(cell.background);
    if (cell.doubleHeight) {
        cssClass += QStringLiteral(" dh");
    }
    if (cell.flash) {
        cssClass += QStringLiteral(" fl");
    }
    if (cell.conceal) {
        cssClass += QStringLiteral(" cn");
    }
    cssClass += cell.boxed ? QStringLiteral(" bx") : QStringLiteral(" nx");
    return cssClass;
}

QString htmlEscapeTeletextCharacter(const QChar &character)
{
    if (character == QLatin1Char('&')) {
        return QStringLiteral("&amp;");
    }
    if (character == QLatin1Char('<')) {
        return QStringLiteral("&lt;");
    }
    if (character == QLatin1Char('>')) {
        return QStringLiteral("&gt;");
    }
    return QString(character);
}

QString teletextRowToHtml(const TeletextNativeViewWidget::Row &row)
{
    QString html = QStringLiteral("<span class=\"row\">");
    QString activeClass;
    bool spanOpen = false;

    for (const TeletextNativeViewWidget::Cell &cell : row) {
        const QString cellClass = teletextCellCssClass(cell);
        if (!spanOpen || cellClass != activeClass) {
            if (spanOpen) {
                html += QStringLiteral("</span>");
            }
            html += QStringLiteral("<span class=\"") + cellClass + QStringLiteral("\">");
            activeClass = cellClass;
            spanOpen = true;
        }
        html += htmlEscapeTeletextCharacter(cell.character);
    }

    if (spanOpen) {
        html += QStringLiteral("</span>");
    }
    html += QStringLiteral("</span>");
    return html;
}

bool rowContainsDoubleHeightControl(const QByteArray &rawRowBytes)
{
    for (qint32 index = 0; index < rawRowBytes.size(); ++index) {
        if ((static_cast<quint8>(rawRowBytes.at(index)) & 0x7F) == 0x0D) {
            return true;
        }
    }
    return false;
}

struct DecodedTeletextSubpage {
    bool hasHeader = false;
    qint32 magazine = 1;
    qint32 page = 0x00;
    qint32 subpage = 0x0000;
    QByteArray headerDisplay;
    QMap<qint32, QByteArray> rowPayloads;
};

QString teletextPageKey(qint32 magazine, qint32 page)
{
    return QStringLiteral("%1%2").arg(magazine).arg(page, 2, 16, QLatin1Char('0'));
}

QString buildTeletextHtmlFromSubpage(const DecodedTeletextSubpage &subpage)
{
    QByteArray headerRow(40, static_cast<char>(0x20));
    const QByteArray pageText = QStringLiteral("P%1").arg(teletextPageKey(subpage.magazine, subpage.page)).toLatin1();
    for (qint32 index = 0; index < pageText.size() && (3 + index) < headerRow.size(); ++index) {
        headerRow[3 + index] = pageText.at(index);
    }
    for (qint32 index = 0; index < subpage.headerDisplay.size() && (8 + index) < headerRow.size(); ++index) {
        headerRow[8 + index] = subpage.headerDisplay.at(index);
    }

    QString body;
    body += QStringLiteral("<div class=\"subpage\" id=\"%1\">")
                .arg(subpage.subpage, 4, 16, QLatin1Char('0'));
    body += teletextRowToHtml(parseTeletextDisplayRow(headerRow));

    QByteArray previousRowPayload;
    for (qint32 row = 1; row <= 24; ++row) {
        const QByteArray rowPayload = subpage.rowPayloads.value(row, QByteArray(40, static_cast<char>(0x20)));
        if (row > 1 && rowContainsDoubleHeightControl(previousRowPayload)) {
            previousRowPayload = rowPayload;
            continue;
        }
        body += teletextRowToHtml(parseTeletextDisplayRow(rowPayload));
        previousRowPayload = rowPayload;
    }
    body += QStringLiteral("</div>");

    QString html;
    html += QStringLiteral("<html><head>");
    html += QStringLiteral("<meta http-equiv=\"Content-Type\" content=\"text/html; charset=utf-8\">");
    html += QStringLiteral("<title>Page %1</title>").arg(teletextPageKey(subpage.magazine, subpage.page));
    html += QStringLiteral("<link rel=\"stylesheet\" type=\"text/css\" href=\"teletext.css\" title=\"Default Style\"/>");
    html += QStringLiteral("<link rel=\"alternative stylesheet\" type=\"text/css\" href=\"teletext-noscanlines.css\" title=\"No Scanlines\"/>");
    html += QStringLiteral("<script type=\"text/javascript\" src=\"cssswitch.js\"></script>");
    html += QStringLiteral("</head><body onload=\"set_style_from_cookie()\">");
    html += body;
    html += QStringLiteral("</body></html>");
    return html;
}

bool writeTeletextHtmlFromT42Native(const QString &streamPath,
                                    const QString &outputDirectoryPath,
                                    QString *errorMessage)
{
    QFile streamFile(streamPath);
    if (!streamFile.open(QIODevice::ReadOnly)) {
        if (errorMessage) {
            *errorMessage = QObject::tr("Could not open teletext stream file: %1").arg(streamPath);
        }
        return false;
    }

    QMap<qint32, DecodedTeletextSubpage> activeSubpagesByMagazine;
    QMap<QString, DecodedTeletextSubpage> selectedSubpagesByPage;

    auto commitSubpage = [&](const DecodedTeletextSubpage &subpage) {
        if (!subpage.hasHeader) {
            return;
        }
        const QString pageKey = teletextPageKey(subpage.magazine, subpage.page);
        if (!selectedSubpagesByPage.contains(pageKey)
            || subpage.subpage < selectedSubpagesByPage.value(pageKey).subpage) {
            selectedSubpagesByPage.insert(pageKey, subpage);
        }
    };

    while (true) {
        const QByteArray packet = streamFile.read(42);
        if (packet.size() == 0) {
            break;
        }
        if (packet.size() != 42) {
            break;
        }

        const quint8 byte0 = static_cast<quint8>(packet.at(0));
        const quint8 byte1 = static_cast<quint8>(packet.at(1));
        const qint32 mrag = decodeHamming16Value(byte0, byte1);
        qint32 magazine = decodeHamming8Nibble(byte0) & 0x07;
        if (magazine == 0) {
            magazine = 8;
        }
        const qint32 row = mrag >> 3;
        if (row < 0 || row > 31) {
            continue;
        }

        if (row == 0) {
            if (activeSubpagesByMagazine.contains(magazine)) {
                commitSubpage(activeSubpagesByMagazine.value(magazine));
            }

            DecodedTeletextSubpage subpage;
            subpage.hasHeader = true;
            subpage.magazine = magazine;
            subpage.page = decodeHamming16Value(static_cast<quint8>(packet.at(2)),
                                                static_cast<quint8>(packet.at(3))) & 0xFF;
            const qint32 subpageLow = decodeHamming16Value(static_cast<quint8>(packet.at(4)),
                                                           static_cast<quint8>(packet.at(5)));
            const qint32 subpageHigh = decodeHamming16Value(static_cast<quint8>(packet.at(6)),
                                                            static_cast<quint8>(packet.at(7)));
            subpage.subpage = (subpageLow & 0x7F) | ((subpageHigh & 0x3F) << 8);
            subpage.headerDisplay = packet.mid(10, 32);
            activeSubpagesByMagazine.insert(magazine, subpage);
            continue;
        }

        if (row >= 1 && row <= 24 && activeSubpagesByMagazine.contains(magazine)) {
            DecodedTeletextSubpage &subpage = activeSubpagesByMagazine[magazine];
            if (subpage.hasHeader) {
                subpage.rowPayloads.insert(row, packet.mid(2, 40));
            }
        }
    }

    for (auto it = activeSubpagesByMagazine.cbegin(); it != activeSubpagesByMagazine.cend(); ++it) {
        commitSubpage(it.value());
    }

    if (selectedSubpagesByPage.isEmpty()) {
        if (errorMessage) {
            *errorMessage = QObject::tr("No decodable teletext pages were found in the .tXX stream.");
        }
        return false;
    }

    for (auto it = selectedSubpagesByPage.cbegin(); it != selectedSubpagesByPage.cend(); ++it) {
        const QString pageHtmlPath = QDir(outputDirectoryPath).filePath(it.key() + QStringLiteral(".html"));
        QFile pageFile(pageHtmlPath);
        if (!pageFile.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
            if (errorMessage) {
                *errorMessage = QObject::tr("Could not write teletext HTML page: %1").arg(pageHtmlPath);
            }
            return false;
        }

        pageFile.write(buildTeletextHtmlFromSubpage(it.value()).toUtf8());
        pageFile.close();
    }

    return true;
}

bool convertTeletextStreamToHtmlDirectory(const QString &streamPath,
                                          QString *outputDirectoryPath,
                                          QString *errorMessage)
{
    const QFileInfo streamInfo(streamPath);
    if (!isTeletextStreamFile(streamInfo)) {
        if (errorMessage) {
            *errorMessage = QObject::tr("Selected file is not a .tXX teletext stream: %1").arg(streamPath);
        }
        return false;
    }

    const QString vendorDirectory = resolveTeletextVendorDirectory();
    if (vendorDirectory.isEmpty()) {
        if (errorMessage) {
            *errorMessage = QObject::tr("Could not locate vendored vhs-teletext runtime directory.");
        }
        return false;
    }

    const QString pythonExecutable = resolvePythonExecutable();

    const QString outputPath = QDir::cleanPath(cacheDirectoryForTeletextStream(streamInfo));
    QDir outputDirectory(outputPath);
    if (!outputDirectory.exists() && !outputDirectory.mkpath(QStringLiteral("."))) {
        if (errorMessage) {
            *errorMessage = QObject::tr("Could not create teletext output directory: %1").arg(outputPath);
        }
        return false;
    }
    ensureCssSwitchScript(outputPath);

    if (!directoryContainsHtmlPages(outputPath)) {
        QString pythonConversionError;
        bool convertedByPython = false;

        if (!pythonExecutable.isEmpty()) {
            QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
            const QString existingPythonPath = environment.value(QStringLiteral("PYTHONPATH"));
            if (existingPythonPath.trimmed().isEmpty()) {
                environment.insert(QStringLiteral("PYTHONPATH"), vendorDirectory);
            } else {
                environment.insert(
                    QStringLiteral("PYTHONPATH"),
                    vendorDirectory + QDir::listSeparator() + existingPythonPath
                );
            }

            const QString htmlExportScript = QStringLiteral(R"PY(
import sys
import types

try:
    import tqdm  # noqa: F401
except ModuleNotFoundError:
    tqdm_module = types.ModuleType("tqdm")
    tqdm_module.tqdm = lambda iterable=None, **kwargs: iterable
    sys.modules["tqdm"] = tqdm_module

from teletext.file import FileChunker
from teletext.packet import Packet
from teletext.service import Service

stream_path = sys.argv[1]
output_directory = sys.argv[2]

with open(stream_path, "rb") as stream_file:
    packet_stream = (Packet(data, number) for number, data in FileChunker(stream_file, 42))
    service = Service.from_packets((packet for packet in packet_stream if not packet.is_padding()))
service.to_html(output_directory, None, None)
)PY");
            const QStringList htmlArguments = {
                QStringLiteral("-c"),
                htmlExportScript,
                streamInfo.absoluteFilePath(),
                outputPath
            };
            convertedByPython = runPythonStep(
                pythonExecutable,
                htmlArguments,
                environment,
                &pythonConversionError
            );
        } else {
            pythonConversionError = QObject::tr("Python interpreter was not found.");
        }

        if (!convertedByPython) {
            QString nativeConversionError;
            if (!writeTeletextHtmlFromT42Native(streamInfo.absoluteFilePath(),
                                                outputPath,
                                                &nativeConversionError)) {
                if (errorMessage) {
                    *errorMessage = QObject::tr("Teletext conversion failed. Python path error: %1 | Native fallback error: %2")
                                        .arg(pythonConversionError.isEmpty()
                                                 ? QObject::tr("unknown error")
                                                 : pythonConversionError,
                                             nativeConversionError.isEmpty()
                                                 ? QObject::tr("unknown error")
                                                 : nativeConversionError);
                }
                return false;
            }
        }
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

    if (!directoryContainsHtmlPages(outputPath)) {
        if (errorMessage) {
            *errorMessage = QObject::tr("Teletext conversion completed, but no HTML pages were generated.");
        }
        return false;
    }

    if (outputDirectoryPath) {
        *outputDirectoryPath = outputPath;
    }
    return true;
}

QStringList droppedLocalFiles(const QMimeData *mimeData)
{
    QStringList filePaths;
    if (!mimeData || !mimeData->hasUrls()) {
        return filePaths;
    }

    for (const QUrl &url : mimeData->urls()) {
        if (!url.isLocalFile()) {
            continue;
        }
        appendUniqueCandidate(filePaths, url.toLocalFile());
    }
    return filePaths;
}

QString qtTeletextCompatibilityStyle()
{
    return QStringLiteral(
        "<style id=\"qt-teletext-compat\">"
        "body { background: black !important; margin: 0 !important; padding: 0 !important; }"
        ".subpage {"
        "  float: none !important;"
        "  display: inline-block !important;"
        "  white-space: pre !important;"
        "  border: 0 !important;"
        "  text-shadow: none !important;"
        "  filter: none !important;"
        "  font-family: teletext2, \"Courier New\", monospace !important;"
        "  font-size: 20px !important;"
        "  line-height: 1.0 !important;"
        "  min-width: 40ch !important;"
        "  min-height: 25em !important;"
        "}"
        ".row { display: block !important; white-space: pre !important; }"
        ".dh {"
        "  font-family: teletext4, teletext2, \"Courier New\", monospace !important;"
        "  font-size: 200% !important;"
        "  line-height: 1.0 !important;"
        "}"
        "</style>");
}

QString normalizedTeletextHtmlForQt(QString htmlContent)
{
    if (htmlContent.contains(QStringLiteral("qt-teletext-compat"), Qt::CaseInsensitive)) {
        return htmlContent;
    }

    static const QRegularExpression rowBoundaryPattern(
        QStringLiteral("</span>\\s*<span\\s+class=\"row\">"),
        QRegularExpression::CaseInsensitiveOption
    );
    htmlContent.replace(rowBoundaryPattern, QStringLiteral("</span><br/><span class=\"row\">"));

    const QString styleBlock = qtTeletextCompatibilityStyle();
    const qint32 headCloseIndex = htmlContent.indexOf(QStringLiteral("</head>"), 0, Qt::CaseInsensitive);
    if (headCloseIndex >= 0) {
        htmlContent.insert(headCloseIndex, styleBlock);
    } else {
        htmlContent.prepend(QStringLiteral("<head>") + styleBlock + QStringLiteral("</head>"));
    }
    return htmlContent;
}
} // namespace

TeletextViewerDialog::TeletextViewerDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Teletext Viewer"));
    setModal(false);
    setAttribute(Qt::WA_DeleteOnClose, false);
    setMinimumSize(640, 520);
    setAcceptDrops(true);

    auto *mainLayout = new QVBoxLayout(this);

    auto *directoryLayout = new QHBoxLayout();
    directoryLayout->addWidget(new QLabel(tr("Directory:"), this));
    directoryLineEdit = new QLineEdit(this);
    directoryLineEdit->setReadOnly(true);
    directoryLayout->addWidget(directoryLineEdit, 1);
    browseDirectoryButton = new QPushButton(tr("Browse HTML..."), this);
    browseStreamButton = new QPushButton(tr("Open .tXX..."), this);
    refreshListButton = new QPushButton(tr("Refresh List"), this);
    directoryLayout->addWidget(browseDirectoryButton);
    directoryLayout->addWidget(browseStreamButton);
    directoryLayout->addWidget(refreshListButton);
    mainLayout->addLayout(directoryLayout);

    auto *pageLayout = new QHBoxLayout();
    pageLayout->addWidget(new QLabel(tr("Page:"), this));
    pageComboBox = new QComboBox(this);
    pageComboBox->setMinimumContentsLength(10);
    pageLayout->addWidget(pageComboBox, 1);
    refreshPageButton = new QPushButton(tr("Refresh Page"), this);
    openInBrowserButton = new QPushButton(tr("Open in Browser"), this);
    pageLayout->addWidget(refreshPageButton);
    pageLayout->addWidget(openInBrowserButton);
    mainLayout->addLayout(pageLayout);

    auto *optionsLayout = new QHBoxLayout();
    autoRefreshCheckBox = new QCheckBox(tr("Live refresh"), this);
    autoRefreshCheckBox->setChecked(true);
    optionsLayout->addWidget(autoRefreshCheckBox);
    optionsLayout->addSpacing(12);
    optionsLayout->addWidget(new QLabel(tr("Renderer:"), this));
    rendererComboBox = new QComboBox(this);
    rendererComboBox->addItem(tr("HTML"));
    rendererComboBox->addItem(tr("Native"));
    optionsLayout->addWidget(rendererComboBox);
    flashAnimationCheckBox = new QCheckBox(tr("Animate flash"), this);
    flashAnimationCheckBox->setChecked(true);
    flashAnimationCheckBox->setEnabled(false);
    optionsLayout->addWidget(flashAnimationCheckBox);
    optionsLayout->addStretch(1);
    mainLayout->addLayout(optionsLayout);
    viewerStack = new QStackedWidget(this);
    pageViewer = new QTextBrowser(viewerStack);
    pageViewer->setOpenExternalLinks(false);
    pageViewer->setOpenLinks(false);
    nativePageViewer = new TeletextNativeViewWidget(viewerStack);
    nativePageViewer->setPlaceholderText(tr("No teletext page loaded"));
    nativePageViewer->setAnimationsEnabled(true);
    viewerStack->addWidget(pageViewer);
    viewerStack->addWidget(nativePageViewer);
    viewerStack->setCurrentWidget(pageViewer);
    mainLayout->addWidget(viewerStack, 1);

    refreshTimer = new QTimer(this);
    refreshTimer->setInterval(1000);
    refreshTimer->setSingleShot(false);

    connect(browseDirectoryButton, &QPushButton::clicked,
            this, &TeletextViewerDialog::browseForDirectory);
    connect(browseStreamButton, &QPushButton::clicked,
            this, &TeletextViewerDialog::browseForTeletextStream);
    connect(refreshListButton, &QPushButton::clicked,
            this, &TeletextViewerDialog::refreshPageList);
    connect(pageComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &TeletextViewerDialog::loadSelectedPage);
    connect(refreshPageButton, &QPushButton::clicked,
            this, &TeletextViewerDialog::loadSelectedPage);
    connect(pageViewer, &QTextBrowser::anchorClicked,
            this, &TeletextViewerDialog::handlePageLinkClicked);
    connect(openInBrowserButton, &QPushButton::clicked,
            this, &TeletextViewerDialog::openSelectedPageInBrowser);
    connect(autoRefreshCheckBox, &QCheckBox::toggled,
            this, &TeletextViewerDialog::setAutoRefreshEnabled);
    connect(refreshTimer, &QTimer::timeout,
            this, &TeletextViewerDialog::handlePeriodicRefresh);
    connect(rendererComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int index) { setNativeRendererEnabled(index == 1); });
    connect(flashAnimationCheckBox, &QCheckBox::toggled,
            this, &TeletextViewerDialog::setFlashAnimationEnabled);
    auto *previousPageShortcut = new QShortcut(QKeySequence(Qt::Key_PageUp), this);
    previousPageShortcut->setContext(Qt::WidgetWithChildrenShortcut);
    connect(previousPageShortcut, &QShortcut::activated, this, [this]() {
        cyclePageSelection(-1);
    });
    auto *nextPageShortcut = new QShortcut(QKeySequence(Qt::Key_PageDown), this);
    nextPageShortcut->setContext(Qt::WidgetWithChildrenShortcut);
    connect(nextPageShortcut, &QShortcut::activated, this, [this]() {
        cyclePageSelection(1);
    });
    const QList<QWidget *> dropTargets = {
        directoryLineEdit,
        pageComboBox,
        pageViewer,
        nativePageViewer,
        viewerStack
    };
    for (QWidget *dropTarget : dropTargets) {
        if (!dropTarget) {
            continue;
        }
        dropTarget->setAcceptDrops(true);
        dropTarget->installEventFilter(this);
    }
    if (pageViewer && pageViewer->viewport()) {
        pageViewer->viewport()->setAcceptDrops(true);
        pageViewer->viewport()->installEventFilter(this);
    }

    setAutoRefreshEnabled(autoRefreshCheckBox->isChecked());
    setNativeRendererEnabled(false);
}

void TeletextViewerDialog::setDirectory(const QString &directoryPath)
{
    const QString normalizedPath = QDir::cleanPath(directoryPath.trimmed());
    if (normalizedPath.isEmpty()) {
        return;
    }

    currentDirectoryPath = normalizedPath;
    autoWindowSizePending = true;
    directoryLineEdit->setText(currentDirectoryPath);
    if (nativePageViewer) {
        nativePageViewer->setAssetDirectory(currentDirectoryPath);
    }
    refreshPageList();
}

QString TeletextViewerDialog::directory() const
{
    return currentDirectoryPath;
}

bool TeletextViewerDialog::openTeletextStream(const QString &streamPath, QString *errorMessage)
{
    return openTeletextStreamPath(streamPath, errorMessage);
}

bool TeletextViewerDialog::eventFilter(QObject *watched, QEvent *event)
{
    if (!event) {
        return QDialog::eventFilter(watched, event);
    }

    if (event->type() == QEvent::DragEnter) {
        auto *dragEnterEvent = static_cast<QDragEnterEvent *>(event);
        if (canAcceptDrop(dragEnterEvent->mimeData())) {
            dragEnterEvent->acceptProposedAction();
            return true;
        }
    } else if (event->type() == QEvent::DragMove) {
        auto *dragMoveEvent = static_cast<QDragMoveEvent *>(event);
        if (canAcceptDrop(dragMoveEvent->mimeData())) {
            dragMoveEvent->acceptProposedAction();
            return true;
        }
    } else if (event->type() == QEvent::Drop) {
        auto *dropEvent = static_cast<QDropEvent *>(event);
        if (handleDrop(dropEvent->mimeData())) {
            dropEvent->acceptProposedAction();
            return true;
        }
    } else if (event->type() == QEvent::Wheel) {
        const bool navigationTarget = watched == pageComboBox
                                      || watched == pageViewer
                                      || watched == nativePageViewer
                                      || watched == viewerStack
                                      || (pageViewer && watched == pageViewer->viewport());
        if (navigationTarget) {
            auto *wheelEvent = static_cast<QWheelEvent *>(event);
            const int wheelDeltaY = wheelEvent->angleDelta().y();
            if (wheelDeltaY != 0) {
                const int direction = wheelDeltaY > 0 ? -1 : 1;
                const int stepCount = qMax(1, qAbs(wheelDeltaY) / 120);
                bool pageChanged = false;
                for (int step = 0; step < stepCount; ++step) {
                    if (cyclePageSelection(direction)) {
                        pageChanged = true;
                    }
                }
                if (pageChanged) {
                    wheelEvent->accept();
                    return true;
                }
            }
        }
    }

    return QDialog::eventFilter(watched, event);
}

void TeletextViewerDialog::dragEnterEvent(QDragEnterEvent *event)
{
    if (!event) {
        return;
    }
    if (canAcceptDrop(event->mimeData())) {
        event->acceptProposedAction();
    } else {
        event->ignore();
    }
}

void TeletextViewerDialog::showEvent(QShowEvent *event)
{
    QDialog::showEvent(event);
    if (!lastLoadedPagePath.isEmpty()) {
        autoSizeWindowForCurrentRenderer();
        autoWindowSizePending = false;
    }
}

void TeletextViewerDialog::dragMoveEvent(QDragMoveEvent *event)
{
    if (!event) {
        return;
    }
    if (canAcceptDrop(event->mimeData())) {
        event->acceptProposedAction();
    } else {
        event->ignore();
    }
}

void TeletextViewerDialog::dropEvent(QDropEvent *event)
{
    if (!event) {
        return;
    }
    if (handleDrop(event->mimeData())) {
        event->acceptProposedAction();
    } else {
        event->ignore();
    }
}

void TeletextViewerDialog::browseForDirectory()
{
    const QString startPath = currentDirectoryPath.isEmpty() ? QDir::homePath() : currentDirectoryPath;
    const QString selectedDirectory = QFileDialog::getExistingDirectory(
        this,
        tr("Select teletext HTML directory"),
        startPath,
        QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks
    );
    if (selectedDirectory.isEmpty()) {
        return;
    }

    setDirectory(selectedDirectory);
}
void TeletextViewerDialog::browseForTeletextStream()
{
    const QString startPath = currentDirectoryPath.isEmpty() ? QDir::homePath() : currentDirectoryPath;
    const QString selectedStreamFile = QFileDialog::getOpenFileName(
        this,
        tr("Select teletext stream file"),
        startPath,
        tr("Teletext stream files (*.t??);;All Files (*)")
    );
    if (selectedStreamFile.isEmpty()) {
        return;
    }

    QString errorMessage;
    if (!openTeletextStreamPath(selectedStreamFile, &errorMessage)) {
        QMessageBox::warning(this, tr("Teletext open failed"),
                             errorMessage.isEmpty()
                                 ? tr("Could not open the selected teletext stream.")
                                 : errorMessage);
    }
}

bool TeletextViewerDialog::openTeletextStreamPath(const QString &streamPath, QString *errorMessage)
{
    QString outputDirectoryPath;
    if (!convertTeletextStreamToHtmlDirectory(streamPath, &outputDirectoryPath, errorMessage)) {
        return false;
    }
    setDirectory(outputDirectoryPath);
    return true;
}

void TeletextViewerDialog::refreshPageList()
{
    pageComboBox->blockSignals(true);

    const QString previousSelection = pageComboBox->currentText();
    pageComboBox->clear();

    if (!directoryContainsHtml()) {
        pageComboBox->blockSignals(false);
        pageViewer->setHtml(tr("<html><body><p>No teletext HTML pages were found in this directory.</p></body></html>"));
        if (nativePageViewer) {
            nativePageViewer->clearPage();
        }
        setWindowTitle(tr("Teletext Viewer"));
        lastLoadedPagePath.clear();
        lastLoadedPageModified = QDateTime();
        return;
    }

    const QDir directory(currentDirectoryPath);
    const QStringList htmlPages = directory.entryList(
        QStringList() << QStringLiteral("*.html"),
        QDir::Files,
        QDir::Name | QDir::IgnoreCase
    );
    pageComboBox->addItems(htmlPages);

    qint32 selectedIndex = htmlPages.indexOf(previousSelection);
    if (selectedIndex < 0) {
        selectedIndex = htmlPages.indexOf(QStringLiteral("100.html"));
    }
    if (selectedIndex < 0 && !htmlPages.isEmpty()) {
        selectedIndex = 0;
    }
    if (selectedIndex >= 0) {
        pageComboBox->setCurrentIndex(selectedIndex);
    }

    pageComboBox->blockSignals(false);
    loadSelectedPage();
}

void TeletextViewerDialog::loadSelectedPage()
{
    const QString pagePath = selectedPagePath();
    if (pagePath.isEmpty()) {
        return;
    }

    const QFileInfo pageInfo(pagePath);
    if (!pageInfo.exists()) {
        return;
    }

    if (lastLoadedPagePath == pagePath && lastLoadedPageModified == pageInfo.lastModified()) {
        if (autoWindowSizePending) {
            autoSizeWindowForCurrentRenderer();
            autoWindowSizePending = false;
        }
        return;
    }

    QFile pageFile(pagePath);
    if (!pageFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return;
    }

    const QString rawPageHtml = QString::fromUtf8(pageFile.readAll());
    QString pageHtml = normalizedTeletextHtmlForQt(rawPageHtml);
    pageViewer->setSearchPaths(QStringList() << currentDirectoryPath);
    pageViewer->document()->setBaseUrl(QUrl::fromLocalFile(pagePath));
    pageViewer->setHtml(pageHtml);
    if (nativePageViewer) {
        nativePageViewer->setAssetDirectory(currentDirectoryPath);
        const QVector<TeletextNativeViewWidget::Row> nativeRows =
            TeletextNativeViewWidget::parseHtmlPage(rawPageHtml);
        if (nativeRows.isEmpty()) {
            nativePageViewer->clearPage();
        } else {
            nativePageViewer->setPage(nativeRows);
        }
    }

    lastLoadedPagePath = pagePath;
    lastLoadedPageModified = pageInfo.lastModified();
    setWindowTitle(tr("Teletext Viewer - %1").arg(pageInfo.fileName()));
    if (autoWindowSizePending) {
        autoSizeWindowForCurrentRenderer();
        autoWindowSizePending = false;
    }
}

void TeletextViewerDialog::handlePageLinkClicked(const QUrl &linkUrl)
{
    if (!linkUrl.isValid()) {
        return;
    }

    if (linkUrl.path().isEmpty() && !linkUrl.fragment().isEmpty()) {
        pageViewer->scrollToAnchor(linkUrl.fragment());
        return;
    }

    QUrl resolvedUrl = linkUrl;
    if (resolvedUrl.isRelative() && !lastLoadedPagePath.isEmpty()) {
        resolvedUrl = QUrl::fromLocalFile(lastLoadedPagePath).resolved(linkUrl);
    }

    if (resolvedUrl.isLocalFile()) {
        const QFileInfo linkedFileInfo(QDir::cleanPath(resolvedUrl.toLocalFile()));
        if (linkedFileInfo.exists() &&
            linkedFileInfo.isFile() &&
            linkedFileInfo.suffix().compare(QStringLiteral("html"), Qt::CaseInsensitive) == 0) {
            if (currentDirectoryPath.compare(linkedFileInfo.absolutePath(), Qt::CaseInsensitive) != 0) {
                setDirectory(linkedFileInfo.absolutePath());
            }

            const qint32 linkedPageIndex = pageComboBox->findText(
                linkedFileInfo.fileName(),
                Qt::MatchFixedString
            );
            if (linkedPageIndex >= 0) {
                if (pageComboBox->currentIndex() != linkedPageIndex) {
                    pageComboBox->setCurrentIndex(linkedPageIndex);
                } else {
                    loadSelectedPage();
                }
                return;
            }
        }
    }

    QDesktopServices::openUrl(resolvedUrl);
}

void TeletextViewerDialog::openSelectedPageInBrowser()
{
    const QString pagePath = selectedPagePath();
    if (pagePath.isEmpty()) {
        return;
    }

    const QUrl pageUrl = QUrl::fromLocalFile(pagePath);
    if (!QDesktopServices::openUrl(pageUrl)) {
        QMessageBox::warning(this, tr("Warning"),
                             tr("Could not open teletext page in browser:\n%1").arg(pagePath));
    }
}

void TeletextViewerDialog::setAutoRefreshEnabled(bool enabled)
{
    if (enabled) {
        refreshTimer->start();
    } else {
        refreshTimer->stop();
    }
}

void TeletextViewerDialog::handlePeriodicRefresh()
{
    refreshPageList();
}

void TeletextViewerDialog::setNativeRendererEnabled(bool enabled)
{
    if (!viewerStack || !pageViewer || !nativePageViewer) {
        return;
    }
    viewerStack->setCurrentWidget(enabled ? static_cast<QWidget *>(nativePageViewer)
                                          : static_cast<QWidget *>(pageViewer));
    if (flashAnimationCheckBox) {
        flashAnimationCheckBox->setEnabled(enabled);
    }
    if (!lastLoadedPagePath.isEmpty()) {
        autoSizeWindowForCurrentRenderer();
    }
}

void TeletextViewerDialog::setFlashAnimationEnabled(bool enabled)
{
    if (!nativePageViewer) {
        return;
    }
    nativePageViewer->setAnimationsEnabled(enabled);
}

bool TeletextViewerDialog::cyclePageSelection(int direction)
{
    if (!pageComboBox || direction == 0) {
        return false;
    }

    const int pageCount = pageComboBox->count();
    if (pageCount <= 1) {
        return false;
    }

    int currentIndex = pageComboBox->currentIndex();
    if (currentIndex < 0) {
        currentIndex = 0;
    }

    const int normalizedDirection = direction > 0 ? 1 : -1;
    int nextIndex = (currentIndex + normalizedDirection) % pageCount;
    if (nextIndex < 0) {
        nextIndex += pageCount;
    }
    if (nextIndex == currentIndex) {
        return false;
    }

    pageComboBox->setCurrentIndex(nextIndex);
    return true;
}

bool TeletextViewerDialog::canAcceptDrop(const QMimeData *mimeData) const
{
    const QStringList filePaths = droppedLocalFiles(mimeData);
    if (filePaths.isEmpty()) {
        return false;
    }
    const TeletextInputSelection selection = resolveTeletextInputFromHints(filePaths);
    return selection.isValid();
}

bool TeletextViewerDialog::handleDrop(const QMimeData *mimeData)
{
    const QStringList filePaths = droppedLocalFiles(mimeData);
    if (filePaths.isEmpty()) {
        return false;
    }

    const TeletextInputSelection selection = resolveTeletextInputFromHints(filePaths);
    if (!selection.isValid()) {
        return false;
    }

    if (!selection.streamFilePath.isEmpty()) {
        QString errorMessage;
        if (!openTeletextStreamPath(selection.streamFilePath, &errorMessage)) {
            QMessageBox::warning(this, tr("Teletext open failed"),
                                 errorMessage.isEmpty()
                                     ? tr("Could not open the dropped .tXX teletext stream.")
                                     : errorMessage);
            return false;
        }
        return true;
    }

    QString droppedHtmlPageName;
    for (const QString &filePath : filePaths) {
        const QFileInfo fileInfo(filePath);
        if (!fileInfo.exists() || !fileInfo.isFile()) {
            continue;
        }
        if (fileInfo.suffix().compare(QStringLiteral("html"), Qt::CaseInsensitive) != 0) {
            continue;
        }
        if (fileInfo.absolutePath().compare(selection.directoryPath, Qt::CaseInsensitive) == 0) {
            droppedHtmlPageName = fileInfo.fileName();
            break;
        }
    }
    if (droppedHtmlPageName.isEmpty() && !selection.htmlPageName.isEmpty()) {
        droppedHtmlPageName = selection.htmlPageName;
    }
    setDirectory(selection.directoryPath);
    if (!droppedHtmlPageName.isEmpty()) {
        const qint32 pageIndex = pageComboBox->findText(droppedHtmlPageName, Qt::MatchFixedString);
        if (pageIndex >= 0 && pageComboBox->currentIndex() != pageIndex) {
            pageComboBox->setCurrentIndex(pageIndex);
        } else if (pageIndex >= 0) {
            loadSelectedPage();
        }
    }
    return true;
}

bool TeletextViewerDialog::directoryContainsHtml() const
{
    return directoryContainsHtmlPages(currentDirectoryPath);
}

void TeletextViewerDialog::autoSizeWindowForCurrentRenderer()
{
    const bool nativeRendererActive = viewerStack
                                      && viewerStack->currentWidget() == nativePageViewer;
    QSize targetSize = nativeRendererActive
        ? QSize(981, 916)
        : QSize(981, 928);
    targetSize = targetSize.expandedTo(minimumSize());

    if (QScreen *screen = windowHandle() ? windowHandle()->screen() : QGuiApplication::primaryScreen()) {
        const QRect available = screen->availableGeometry();
        const QSize maxSize(static_cast<qint32>(available.width() * 0.96),
                            static_cast<qint32>(available.height() * 0.96));
        targetSize.setWidth(qMin(targetSize.width(), maxSize.width()));
        targetSize.setHeight(qMin(targetSize.height(), maxSize.height()));
    }

    resize(targetSize);
}

QString TeletextViewerDialog::selectedPagePath() const
{
    if (currentDirectoryPath.trimmed().isEmpty()) {
        return QString();
    }
    if (pageComboBox->currentIndex() < 0) {
        return QString();
    }

    return QDir(currentDirectoryPath).filePath(pageComboBox->currentText());
}
